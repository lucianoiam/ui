#include "wapis/dom.hpp"
#include "wapis/dom_adapter.h"
#include "wapis/whatwg.h"
#include <quickjs.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
// Pretty HTML formatting
#include "input/input.h"
#include "renderer/css_parser.h"
#include "renderer/layout_yoga.h"
#include "renderer/renderer.h"
#include "renderer/scheduler.h"
#include "renderer/sk_canvas_view.h"
#include "renderer/viewport.h" // default viewport size
#import <Cocoa/Cocoa.h>
#ifdef __APPLE__
#import <Metal/Metal.h>
#import <QuartzCore/CAMetalLayer.h>
#endif
#include "include/gpu/ganesh/GrDirectContext.h"
#include "include/gpu/ganesh/GrBackendSurface.h"
#include "include/gpu/ganesh/SkSurfaceGanesh.h"
#include "include/gpu/ganesh/mtl/GrMtlTypes.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendContext.h"
#include "include/gpu/ganesh/mtl/GrMtlDirectContext.h"
#include "include/gpu/ganesh/mtl/GrMtlBackendSurface.h"
#include <include/core/SkCanvas.h>
#include <include/core/SkImage.h>
#include <include/core/SkSamplingOptions.h>
#include <include/core/SkSurface.h>
#include <include/core/SkColorSpace.h>
#include <lexbor/html/html.h>
#include <memory>
#include <random>
#include <string>
#include <utility>
#include <atomic>

// Forward declare dump_exception for use in mouse event forwarding
static void dump_exception(JSContext* ctx);

// Live window support uses an input subsystem (see input/mac.mm)
@interface CanvasImageView : NSImageView
@end
#import "input/InputImageView.h"

// Pretty HTML formatter using Lexbor. Accepts raw outerHTML of <body> subtree.
static std::string pretty_format_body_html(const char* body_outer_html)
{
   if (!body_outer_html)
      return {};
   std::string wrapped = std::string("<html>") + body_outer_html + "</html>";
   lxb_html_document_t* lxb_doc = lxb_html_document_create();
   if (!lxb_doc)
      return body_outer_html; // fallback
   lxb_status_t st = lxb_html_document_parse(lxb_doc, (const lxb_char_t*)wrapped.c_str(), wrapped.size());
   if (st != LXB_STATUS_OK) {
      lxb_html_document_destroy(lxb_doc);
      return body_outer_html; // fallback
   }
   lxb_dom_node_t* body_node = (lxb_dom_node_t*)lxb_html_document_body_element(lxb_doc);
   std::string pretty;
   if (body_node) {
      struct Buf {
         std::string s;
      };

      Buf buf;
      auto cb = [](const lxb_char_t* data, size_t len, void* ctx) -> lxb_status_t {
         ((Buf*)ctx)->s.append((const char*)data, len);
         return LXB_STATUS_OK;
      };
      if (lxb_html_serialize_pretty_tree_cb(body_node, LXB_HTML_SERIALIZE_OPT_UNDEF, 0, cb, &buf) == LXB_STATUS_OK) {
         pretty = std::move(buf.s);
      }
   }
   lxb_html_document_destroy(lxb_doc);
   if (pretty.empty())
      return body_outer_html; // fallback
   // Strip leading/trailing quotes added around text-only lines.
   std::string cleaned;
   cleaned.reserve(pretty.size());
   size_t start = 0;
   while (start < pretty.size()) {
      size_t end = pretty.find('\n', start);
      if (end == std::string::npos)
         end = pretty.size();
      std::string_view line(pretty.c_str() + start, end - start);
      auto ltrim = [](std::string_view v) {
         size_t i = 0;
         while (i < v.size() && (v[i] == ' ' || v[i] == '\t'))
            ++i;
         return v.substr(i);
      };
      auto rtrim = [](std::string_view v) {
         size_t j = v.size();
         while (j > 0 && (v[j - 1] == ' ' || v[j - 1] == '\t'))
            --j;
         return v.substr(0, j);
      };
      std::string_view core = rtrim(ltrim(line));
      if (core.size() >= 2 && core.front() == '"' && core.back() == '"') {
         // emit unquoted
         std::string_view inner = core.substr(1, core.size() - 2);
         // Reconstruct preserving original indentation
         size_t indent_len = core.data() - line.data(); // distance from line begin to core
         cleaned.append(line.substr(0, indent_len));
         cleaned.append(inner);
      }
      else {
         cleaned.append(line);
      }
      if (end < pretty.size())
         cleaned.push_back('\n');
      start = (end == pretty.size()) ? end : end + 1;
   }
   return cleaned.empty() ? pretty : cleaned;
}

static void diagnostic_atexit()
{
   fprintf(stderr, "[DIAG] atexit handler running (normal process teardown)\n");
   fflush(stderr);
}

struct DiagnosticDtor {
   ~DiagnosticDtor()
   {
      fprintf(stderr, "[DIAG] static destructor executed\n");
   }
} g_diagnostic_dtor;

// Uncomment to enable each test

// #define ENABLE_TEST_1 // disabled
// #define ENABLE_TEST_2 // disabled
#define ENABLE_TEST_3 // new render test

typedef struct {
   double elapsed;
   int success;
} TestResult;

// Deferred environment (used for live render window test)
static JSRuntime* g_deferred_rt = nullptr;
JSContext* g_deferred_ctx = nullptr;
static JSValue g_deferred_global = JS_UNDEFINED;
static JSValue g_deferred_document = JS_UNDEFINED;
static JSValue g_deferred_body = JS_UNDEFINED;
// DomAdapterState is owned per QuickJS runtime.

// Forward declaration for compositing reuse
static Renderer* renderer_from_ctx(JSContext* ctx)
{
   if (!ctx)
      return nullptr;
   JSValue global = JS_GetGlobalObject(ctx);
   JSValue document = JS_GetPropertyStr(ctx, global, "document");
   if (JS_IsException(document) || JS_IsUndefined(document)) {
      JS_FreeValue(ctx, global);
      return nullptr;
   }
   auto* docNode = reinterpret_cast<dom::Node*>(dom_get_cpp_node_opaque(ctx, (JSValueConst)document));
   JS_FreeValue(ctx, document);
   JS_FreeValue(ctx, global);
   if (!docNode)
      return nullptr;
   if (auto d = std::dynamic_pointer_cast<dom::Document>(docNode->shared_from_this())) {
      for (auto* o : d->observers()) {
         if (auto* r = dynamic_cast<Renderer*>(o))
            return r;
      }
   }
   return nullptr;
}

NSImageView* g_canvasImageView = nil;
int g_winW = VIEWPORT_DEFAULT_WIDTH;
int g_winH = VIEWPORT_DEFAULT_HEIGHT;
sk_sp<SkSurface> g_windowSurface;
// GPU runtime globals
static bool g_use_gpu = false; // determined at window creation from SKIA_GPU env
static bool g_run_loop_active = false; // becomes true once [app run] entered
static bool g_gpu_sync_frames = false; // force command buffer sync when automated frame test active
#ifdef __APPLE__
static NSTimer* g_gpuTestTimer = nil; // automated frame test timer
#endif
static bool g_gpu_test_preloop = false; // allow composites before run loop during automated frame test
static sk_sp<GrDirectContext> g_grCtx;
#ifdef __APPLE__
static id<MTLDevice> g_mtlDevice = nil;
static id<MTLCommandQueue> g_mtlQueue = nil;
static CAMetalLayer* g_metalLayer = nil;
#endif
// Retain a few in-flight textures (Metal completion callbacks may fire after Skia flush returns)
static std::vector<GrMtlTextureInfo> g_inflight_textures; // cleared on shutdown
#ifdef __APPLE__
static std::vector<id<CAMetalDrawable>> g_inflight_drawables; // retain drawables asynchronously
#endif

static void gpu_shutdown()
{
   if (!g_use_gpu)
      return;
#ifdef __APPLE__
   // Flush & release Skia GPU resources first
   if (g_grCtx) {
      g_grCtx->flush();
      g_grCtx->submit();
      // Abandon to prevent further callbacks referencing freed objects.
      g_grCtx->releaseResourcesAndAbandonContext();
      g_grCtx.reset();
   }
   g_inflight_textures.clear();
#ifdef __APPLE__
   g_inflight_drawables.clear();
#endif
#if !__has_feature(objc_arc)
   if (g_mtlQueue) { [g_mtlQueue release]; g_mtlQueue = nil; }
   if (g_mtlDevice) { [g_mtlDevice release]; g_mtlDevice = nil; }
#else
   g_mtlQueue = nil;
   g_mtlDevice = nil;
#endif
   g_metalLayer = nil;
#endif
   g_use_gpu = false;
}

// Forward declare CPU composite for GPU wrapper usage
static void composite_into_surface(sk_sp<SkSurface> surface, int W, int H);

static void gpu_composite_and_present(int W, int H)
{
   if (!g_use_gpu) return;
   if (!g_run_loop_active && !g_gpu_test_preloop) return;
#ifdef __APPLE__
   if (!g_metalLayer || !g_grCtx) return;
   @autoreleasepool {
      id<CAMetalDrawable> drawable = [g_metalLayer nextDrawable];
      if (!drawable) return;
      int dw = (int)[drawable.texture width];
      int dh = (int)[drawable.texture height];
      GrMtlTextureInfo texInfo; texInfo.fTexture.reset((__bridge void*)drawable.texture);
      if (!g_gpu_sync_frames) {
         // Async path: retain a few drawables so their textures stay valid until GPU finishes.
         g_inflight_drawables.push_back(drawable); // ARC retain
         if (g_inflight_drawables.size() > 3) {
            g_inflight_drawables.erase(g_inflight_drawables.begin());
         }
         // Track associated texture info ring (parallel size).
         g_inflight_textures.push_back(texInfo);
         if (g_inflight_textures.size() > 3) g_inflight_textures.erase(g_inflight_textures.begin());
         texInfo = g_inflight_textures.back();
      }
      GrBackendRenderTarget backendRT = GrBackendRenderTargets::MakeMtl(dw, dh, texInfo);
      id<MTLCommandBuffer> commandBuffer = [g_mtlQueue commandBuffer];
      if (!commandBuffer) return;
      sk_sp<SkColorSpace> cs = SkColorSpace::MakeSRGB();
      sk_sp<SkSurface> surface = SkSurfaces::WrapBackendRenderTarget(
          g_grCtx.get(), backendRT, kTopLeft_GrSurfaceOrigin, kBGRA_8888_SkColorType, cs, nullptr);
      if (!surface) { [commandBuffer commit]; return; }
      surface->getCanvas()->clear(SkColorSetARGB(255,0x20,0x20,0x20));
      composite_into_surface(surface, W, H);
      if (g_gpu_sync_frames) {
         std::atomic<bool> done(false);
         GrFlushInfo info = {};
         info.fFinishedProc = [](void* ctx){ ((std::atomic<bool>*)ctx)->store(true, std::memory_order_release); };
         info.fFinishedContext = &done;
         g_grCtx->flush(info);
         g_grCtx->submit();
         // Spin-wait briefly (test usage: small frame count) with timeout safeguard
         const int maxWaitMs = 500; // half second safety
         int waited = 0;
         while (!done.load(std::memory_order_acquire) && waited < maxWaitMs) { usleep(1000); waited++; }
      } else {
         g_grCtx->flush();
         g_grCtx->submit();
      }
      [commandBuffer presentDrawable:drawable];
      [commandBuffer commit];
      if (g_gpu_sync_frames) {
         [commandBuffer waitUntilCompleted];
         // Synchronous mode: clear rings (no need to retain beyond frame)
         g_inflight_textures.clear();
         g_inflight_drawables.clear();
      }
   }
#endif
}
static void composite_into_surface(sk_sp<SkSurface> surface, int W, int H)
{
   if (!surface)
      return;
   // Apply layout just-in-time if needed
   if (g_deferred_ctx)
      layout_maybe_run(g_deferred_ctx);
   // Retrieve adapter state bound to the deferred runtime (for canvas id lookups)
   DomAdapterState* st_for_canvas = nullptr;
   GfxStateHandle* gs = nullptr;
   if (g_deferred_ctx) {
      JSRuntime* rt = JS_GetRuntime(g_deferred_ctx);
      st_for_canvas = (DomAdapterState*)JS_GetRuntimeOpaque(rt);
      // Fetch per-context graphics state owned by the adapter
      extern GfxStateHandle* dom_gfx_state(JSContext * ctx);
      gs = dom_gfx_state(g_deferred_ctx);
   }
   // Device scale for device-pixel compositing
   float deviceScale = 1.0f;
   if (g_deferred_ctx) {
      deviceScale = dom_get_display_scale(g_deferred_ctx);
      if (deviceScale <= 0.f) deviceScale = 1.0f;
   }
   SkCanvas* canvas = surface->getCanvas();
   canvas->clear(SkColorSetARGB(255, 0x20, 0x20, 0x20));
   extern bool gfx_get_size(GfxStateHandle * gs, int id, int* w, int* h);
   extern sk_sp<SkImage> gfx_snapshot(GfxStateHandle * gs, int id);
   extern int dom_element_canvas_id(DomAdapterState*, dom::Element * el, bool createIfMissing);
   Renderer* renderer = renderer_from_ctx(g_deferred_ctx);
   if (!renderer)
      return;
   renderer->forEachLayer([&](RenderLayer* rl) {
      if (!rl || !rl->element)
         return;
      int id = st_for_canvas ? dom_element_canvas_id(st_for_canvas, rl->element, false) : 0;
      int x = 0, y = 0, w = 0, h = 0;
      if (!layout_get_box(rl->element, x, y, w, h)) {
         // Fallback for elements without layout box: use lexbor tokenizer helpers
         const std::string& st_fallback = rl->element->styleCssText;
         auto decls = css::parse_inline(st_fallback);
         auto get_px = [&](const char* prop, int defv) {
            auto it = decls.kv.find(prop);
            if (it == decls.kv.end())
               return defv;
            float v = -1.f;
            std::string unit;
            if (css::parse_number_unit(it->second, v, unit) && v >= 0 && (unit.empty() || unit == "px"))
               return (int)std::lround(v);
            return defv;
         };
         int fx = get_px("left", -1);
         int fy = get_px("top", -1);
         int fw = get_px("width", -1);
         int fh = get_px("height", -1);
         if (fx >= 0) x = fx;
         if (fy >= 0) y = fy;
         if (fw > 0) w = fw;
         if (fh > 0) h = fh;
         if (w <= 0) w = 50;
         if (h <= 0) h = 50;
      }
      // Override with absolute positioning if explicitly styled.
      const std::string& style_override = rl->element->styleCssText;
      {
         auto decls2 = css::parse_inline(style_override);
         auto get_px2 = [&](const char* prop, int defv) {
            auto it = decls2.kv.find(prop);
            if (it == decls2.kv.end())
               return defv;
            float v = -1.f;
            std::string unit;
            if (css::parse_number_unit(it->second, v, unit) && v >= 0 && (unit.empty() || unit == "px"))
               return (int)std::lround(v);
            return defv;
         };
         int lx = get_px2("left", -1);
         int ty = get_px2("top", -1);
         if (lx >= 0) x = lx;
         if (ty >= 0) y = ty;
      }
      // Draw background color if present even without canvas surface
      const std::string& st = rl->element->styleCssText;
      SkColor bg = 0;
      auto d3 = css::parse_inline(st);
      auto itBg = d3.kv.find("background-color");
      if (itBg == d3.kv.end()) itBg = d3.kv.find("background");
      if (itBg != d3.kv.end()) {
         int rr = 0, gg = 0, bb = 0;
         if (css::parse_rgb_color(itBg->second, rr, gg, bb)) {
            bg = SkColorSetARGB(255, (uint8_t)rr, (uint8_t)gg, (uint8_t)bb);
         }
      }
      if (bg) {
         SkPaint p;
         p.setStyle(SkPaint::kFill_Style);
         p.setColor(bg);
         canvas->drawRect(SkRect::MakeXYWH((SkScalar)(x * deviceScale), (SkScalar)(y * deviceScale),
                                           (SkScalar)(w * deviceScale), (SkScalar)(h * deviceScale)),
                          p);
      }
      if (id) {
         auto img = gfx_snapshot(gs, id);
         if (img) {
            // Draw snapshot at device pixel position; image size already matches device pixels of the canvas.
            canvas->drawImage(img.get(), (SkScalar)(x * deviceScale), (SkScalar)(y * deviceScale));
            if (getenv("DEBUG_DRAW_BORDER")) {
               SkPaint border;
               border.setStyle(SkPaint::kStroke_Style);
               border.setColor(SK_ColorWHITE);
               border.setStrokeWidth(1);
               canvas->drawRect(SkRect::MakeXYWH((SkScalar)(x * deviceScale), (SkScalar)(y * deviceScale),
                                                 (SkScalar)(w * deviceScale), (SkScalar)(h * deviceScale)),
                                border);
            }
         }
      }
   });
}

static void present_surface(NSImageView* iv, sk_sp<SkSurface> surface, int W, int H)
{
   // Raster-only path (GPU path uses CAMetalLayer directly)
   if (!iv || !surface)
      return;
   SkPixmap pixmap;
   if (!surface->peekPixels(&pixmap))
      return;
   CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
   CGBitmapInfo bitmapInfo = (CGBitmapInfo)kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big;
   CGDataProviderRef provider =
       CGDataProviderCreateWithData(NULL, pixmap.addr(), pixmap.rowBytes() * pixmap.height(), NULL);
   CGImageRef cgImage = CGImageCreate(pixmap.width(), pixmap.height(), 8, 32, pixmap.rowBytes(), colorSpace, bitmapInfo,
                                      provider, NULL, false, kCGRenderingIntentDefault);
   if (cgImage) {
   NSImage* img = [[NSImage alloc] initWithCGImage:cgImage size:NSMakeSize(W, H)];
   [iv setImage:img];
#if !__has_feature(objc_arc)
   [img release];
#endif
   CGImageRelease(cgImage);
   }
   CGDataProviderRelease(provider);
   CGColorSpaceRelease(colorSpace);
}

static JSValue js_requestComposite(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (g_windowSurface || g_use_gpu) {
      // Run internal layout pass before painting (JS unaware)
      layout_maybe_run(ctx);
      if (g_use_gpu) {
         gpu_composite_and_present(g_winW, g_winH);
      } else if (g_windowSurface) {
         composite_into_surface(g_windowSurface, g_winW, g_winH);
         present_surface(g_canvasImageView, g_windowSurface, g_winW, g_winH);
      }
   }
   return JS_UNDEFINED;
}

// Internal native composite trigger used by layout auto-pass
void native_request_composite(JSContext* ctx)
{
   (void)ctx;
   if (g_windowSurface || g_use_gpu) {
      if (g_use_gpu) {
         gpu_composite_and_present(g_winW, g_winH);
      } else if (g_windowSurface) {
         composite_into_surface(g_windowSurface, g_winW, g_winH);
         present_surface(g_canvasImageView, g_windowSurface, g_winW, g_winH);
      }
   }
}

// Minimal native->JS mouse event bridge: calls global onNativeMouseEvent(evt)
@implementation CanvasImageView
@end

// Adapter state is owned per QuickJS runtime.

static void do_runtime_full_cleanup(JSRuntime* rt, JSContext* ctx, JSValue global, JSValue document, JSValue body)
{
   if (!ctx || !rt)
      return;
   fprintf(stderr, "[DEBUG] (deferred) Cleanup: start\n");
   // Delete host state (InputManager) before tearing down the context
   if (void* host = dom_get_host_state(ctx)) {
      auto* im = reinterpret_cast<input::InputManager*>(host);
      delete im;
      dom_set_host_state(ctx, nullptr);
   }
   JS_SetPropertyStr(ctx, global, "document", JS_UNDEFINED);
   if (!JS_IsUndefined(document))
      JS_SetPropertyStr(ctx, document, "body", JS_UNDEFINED);
   if (!JS_IsUndefined(body))
      JS_FreeValue(ctx, body);
   if (!JS_IsUndefined(document))
      JS_FreeValue(ctx, document);
   if (!JS_IsUndefined(global))
      JS_FreeValue(ctx, global);
   JS_RunGC(rt);
   if (DomAdapterState* st_rt = (DomAdapterState*)JS_GetRuntimeOpaque(rt))
      dom_runtime_cleanup(st_rt, ctx);
   JS_RunGC(rt);
   for (int i = 0; i < 3; i++)
      JS_RunGC(rt);
   JS_FreeContext(ctx);
   {
      volatile char* p = (char*)malloc(16);
      if (p) {
         p[0] = 0;
         free((void*)p);
      }
   }
   DomAdapterState* st_rt = (DomAdapterState*)JS_GetRuntimeOpaque(rt);
   if (st_rt)
      dom_adapter_unregister_runtime(st_rt, rt);
   JS_FreeRuntime(rt);
   if (st_rt)
      dom_adapter_destroy(st_rt);
   fprintf(stderr, "[DEBUG] (deferred) Cleanup: end\n");
}

// Forward declarations
static char* load_file(const char* filename, size_t* out_len);
static void dump_exception(JSContext* ctx);
double run_test(JSContext* ctx, const char* filename);

TestResult run_preact_test(const char* test_js, const char* output_html, const char* preact_js_path,
                           const char* hooks_js_path, const char* test_label, const char* benchmark_label,
                           bool defer_cleanup)
{

   TestResult result = {0};
   JSRuntime* rt = JS_NewRuntime();
   JSContext* ctx = JS_NewContext(rt);
   int error = 0;
   size_t preact_js_len = 0, hooks_js_len = 0, htm_js_len = 0;
   char *preact_js = NULL, *hooks_js = NULL, *htm_js = NULL;
   JSValue r = JS_UNDEFINED, html_val = JS_UNDEFINED;
   const char* html = NULL;
   JSValue global = JS_UNDEFINED, document = JS_UNDEFINED, body = JS_UNDEFINED;
   const char* assign_hooks = "if (typeof preactHooks !== 'undefined') preact.hooks = preactHooks;";

   std::unique_ptr<DomAdapterState, void (*)(DomAdapterState*)> st(dom_adapter_create(), dom_adapter_destroy);
   // Bind state to both context and runtime (runtime used by finalizers)
   JS_SetContextOpaque(ctx, st.get());
   JS_SetRuntimeOpaque(rt, st.get());
   // Define WHATWG-like globals after binding adapter state so timer polyfills
   // use context-local storage and do not claim runtime opaque.
   define_whatwg_globals(ctx);
   // Initialize per-context device scale before any canvases are created (crisp rendering on high-DPI)
   {
      float scale = 1.0f;
#ifdef __APPLE__
      @autoreleasepool {
         NSScreen* ms = [NSScreen mainScreen];
         if (ms) {
            CGFloat s = [ms backingScaleFactor];
            if (s > 0) scale = (float)s;
         }
      }
#endif
      dom_set_display_scale(ctx, scale);
   }
   dom_define_node_proto(st.get(), ctx);
   global = JS_GetGlobalObject(ctx);
   document = dom_create_document(st.get(), ctx);
   JS_SetPropertyStr(ctx, global, "document", JS_DupValue(ctx, document));
   // Expose factory methods via adapter helper (internal js_create* now static)
   dom_attach_document_factories(st.get(), ctx, document);
   // Attach a renderer owned by the adapter to the current Document
   dom_attach_renderer(ctx);
   // Create per-context input manager and store in adapter host state
   {
      auto* docNode = reinterpret_cast<dom::Node*>(dom_get_cpp_node_opaque(ctx, (JSValueConst)document));
      std::shared_ptr<dom::Document> cppDoc;
      if (docNode && docNode->nodeType == dom::NodeType::DOCUMENT) {
         cppDoc = std::dynamic_pointer_cast<dom::Document>(docNode->shared_from_this());
      }
      if (cppDoc) {
         auto* im = new input::InputManager(cppDoc);
         dom_set_host_state(ctx, im);
      }
   }
   body = JS_GetPropertyStr(ctx, document, "body");
   JS_SetPropertyStr(ctx, global, "window", JS_DupValue(ctx, global));
   JS_SetPropertyStr(ctx, global, "self", JS_DupValue(ctx, global));
   JS_SetPropertyStr(ctx, global, "globalThis", JS_DupValue(ctx, global));
   // Install gfx (Skia surface) helpers
   gfx_install_js(ctx);
   // Initialize renderer scheduling subsystem
   scheduler_init();

   preact_js = load_file(preact_js_path, &preact_js_len);
   if (!preact_js) {
      fprintf(stderr, "Failed to load %s\n", preact_js_path);
      error = 1;
      goto cleanup;
   }
   r = JS_Eval(ctx, preact_js, preact_js_len, preact_js_path, JS_EVAL_TYPE_GLOBAL);
   free(preact_js);
   if (JS_IsException(r))
      dump_exception(ctx);
   JS_FreeValue(ctx, r);

   hooks_js = load_file(hooks_js_path, &hooks_js_len);
   if (!hooks_js) {
      fprintf(stderr, "Failed to load %s\n", hooks_js_path);
      error = 1;
      goto cleanup;
   }
   r = JS_Eval(ctx, hooks_js, hooks_js_len, hooks_js_path, JS_EVAL_TYPE_GLOBAL);
   free(hooks_js);
   if (JS_IsException(r))
      dump_exception(ctx);
   JS_FreeValue(ctx, r);

   r = JS_Eval(ctx, assign_hooks, strlen(assign_hooks), "<assign_hooks>", JS_EVAL_TYPE_GLOBAL);
   if (JS_IsException(r))
      dump_exception(ctx);
   JS_FreeValue(ctx, r);

   // Load htm UMD (provides global 'htm') and bind template helper as global
   // 'htm'
   htm_js = load_file("build/htm.js", &htm_js_len);
   if (htm_js) {
      r = JS_Eval(ctx, htm_js, htm_js_len, "build/htm.js", JS_EVAL_TYPE_GLOBAL);
      free(htm_js);
      htm_js = NULL;
      if (JS_IsException(r))
         dump_exception(ctx);
      JS_FreeValue(ctx, r);
      const char* bind_html = "if (typeof htm !== 'undefined' && typeof preact !== 'undefined') { "
                              "globalThis.htm = htm.bind(preact.h); }";
      r = JS_Eval(ctx, bind_html, strlen(bind_html), "<bind_htm>", JS_EVAL_TYPE_GLOBAL);
      if (JS_IsException(r))
         dump_exception(ctx);
      JS_FreeValue(ctx, r);
      // Load lightweight runtime helpers (functions.js) if present (provides htmx)
      size_t rt_len = 0;
      char* rt_js = load_file("src/runtime/functions.js", &rt_len);
      if (rt_js) {
         JSValue rr = JS_Eval(ctx, rt_js, rt_len, "src/runtime/functions.js", JS_EVAL_TYPE_GLOBAL);
         free(rt_js);
         if (JS_IsException(rr))
            dump_exception(ctx);
         JS_FreeValue(ctx, rr);
      }
      else {
         fprintf(stderr, "[WARN] runtime/functions.js missing; htmx unavailable\n");
      }
   }
   else {
      fprintf(stderr, "[WARN] build/htm.js not found; html templates unavailable\n");
   }

   // Run test (benchmark only the app/test execution)
   result.elapsed = run_test(ctx, test_js);

   // Ensure output directory exists
   mkdir("output", 0777);

   // Serialize via body.outerHTML (standard property on Element)
   html_val = JS_GetPropertyStr(ctx, body, "outerHTML");
   if (!JS_IsException(html_val)) {
      html = JS_ToCString(ctx, html_val);
      if (html) {
         std::string pretty = pretty_format_body_html(html);
         const char* final_html = pretty.c_str();
         printf("%s\n%s\n", test_label, final_html);
         FILE* f = fopen(output_html, "w");
         if (f) {
            fputs(final_html, f);
            fclose(f);
            result.success = 1;
         }
         else {
            fprintf(stderr, "Failed to open %s for writing\n", output_html);
         }
         JS_FreeCString(ctx, html);
      }
   }
   else {
      dump_exception(ctx);
   }
   JS_FreeValue(ctx, html_val);

cleanup:
   if (defer_cleanup && !error) {
      // Preserve environment for later (window compositing needs DOM + renderer
      // data)
      g_deferred_rt = rt;
      g_deferred_ctx = ctx;
      g_deferred_global = global;
      g_deferred_document = document;
      g_deferred_body = body;
      fprintf(stderr, "[DEBUG] Deferred cleanup (keeping runtime alive for window)\n");
      // Keep DomAdapterState alive; will be destroyed in do_runtime_full_cleanup
      (void)st.release();
   }
   else {
      fprintf(stderr, "[DEBUG] Cleanup: start\n");
      JS_SetPropertyStr(ctx, global, "document", JS_UNDEFINED);
      if (!JS_IsUndefined(document))
         JS_SetPropertyStr(ctx, document, "body", JS_UNDEFINED);
      if (!JS_IsUndefined(body))
         JS_FreeValue(ctx, body);
      if (!JS_IsUndefined(document))
         JS_FreeValue(ctx, document);
      if (!JS_IsUndefined(global))
         JS_FreeValue(ctx, global);
      const char* disableCleanup = getenv("DOM_DISABLE_CLEANUP");
      if (!disableCleanup || !*disableCleanup) {
         JS_RunGC(rt);
         dom_runtime_cleanup(st.get(), ctx);
         JS_RunGC(rt);
      }
      else {
         fprintf(stderr, "[MAIN] DOM cleanup skipped due to DOM_DISABLE_CLEANUP env var\n");
      }
      for (int i = 0; i < 3; ++i)
         JS_RunGC(rt);
      fprintf(stderr, "[DEBUG] Freeing JSContext and JSRuntime\n");
      // Delete host state (InputManager) before freeing context
      if (void* host = dom_get_host_state(ctx)) {
         auto* im = reinterpret_cast<input::InputManager*>(host);
         delete im;
         dom_set_host_state(ctx, nullptr);
      }
      JS_FreeContext(ctx);
      {
         volatile char* p = (char*)malloc(16);
         if (p) {
            p[0] = 0;
            free((void*)p);
         }
      }
      dom_adapter_unregister_runtime(st.get(), rt);
      JS_FreeRuntime(rt);
      // renderer unique_ptr will go out of scope and be destroyed here
   }
   FILE* mf = fopen("output/last_run_ok.marker", "w");
   if (mf) {
      fputs("ok", mf);
      fclose(mf);
   }
   fflush(stdout);
   if (!error && result.elapsed >= 0)
      printf("%s: %.1f ms\n", benchmark_label, result.elapsed);
   return result;
}

void dump_exception(JSContext* ctx)
{
   JSValue ex = JS_GetException(ctx);
   const char* err = JS_ToCString(ctx, ex);
   fprintf(stderr, "Exception: %s\n", err ? err : "(no message)");
   JS_FreeCString(ctx, err);
   JSValue stack = JS_GetPropertyStr(ctx, ex, "stack");
   if (!JS_IsUndefined(stack)) {
      const char* s = JS_ToCString(ctx, stack);
      fprintf(stderr, "%s\n", s);
      JS_FreeCString(ctx, s);
   }
   JS_FreeValue(ctx, stack);
   JS_FreeValue(ctx, ex);
}

static char* load_file(const char* filename, size_t* out_len)
{
   FILE* f = fopen(filename, "rb");
   if (!f)
      return NULL;
   fseek(f, 0, SEEK_END);
   long len = ftell(f);
   fseek(f, 0, SEEK_SET);
   char* buf = (char*)malloc(len + 1);
   if (!buf) {
      fclose(f);
      return NULL;
   }
   fread(buf, 1, len, f);
   buf[len] = '\0';
   fclose(f);
   if (out_len)
      *out_len = len;
   return buf;
}

double run_test(JSContext* ctx, const char* filename)
{
   struct timeval start, end;
   size_t js_len = 0;
   char* js = load_file(filename, &js_len);
   if (!js) {
      fprintf(stderr, "Failed to load %s\n", filename);
      return -1.0;
   }
   gettimeofday(&start, NULL);
   JSValue r = JS_Eval(ctx, js, js_len, filename, JS_EVAL_TYPE_GLOBAL);
   free(js);
   if (JS_IsException(r))
      dump_exception(ctx);
   JS_FreeValue(ctx, r);
   gettimeofday(&end, NULL);
   return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;
}

int main(int argc, char** argv)
{
   atexit(diagnostic_atexit);
// Enable core dumps for segfault debugging
#include <sys/resource.h>
   // No external serializer needed (using body.outerHTML)
   const char* run_only = getenv("RUN_ONLY");
   int which = run_only ? atoi(run_only) : 0; // 0 = both
   const char* stress = getenv("DOM_STRESS_LOOPS");
   int stress_loops = stress ? atoi(stress) : 0;
   if (stress_loops > 0) {
      fprintf(stderr, "[DIAG] Stress mode: %d loops (RUN_ONLY=%d)\n", stress_loops, which);
   }
   auto run_test_suite = [&](int testId) {
      if (testId == 1) {
#ifdef ENABLE_TEST_1
         run_preact_test("src/tests/bruteforce.js", "output/bruteforce.html", "build/preact.js",
                         "build/preact_hooks.js", "[TEST 1 OUTPUT]", "[BENCHMARK] Preact app + DOM brute force test");
#endif
      }
      else if (testId == 2) {
#ifdef ENABLE_TEST_2
         run_preact_test("src/tests/complex.js", "output/complex.html", "build/preact.js", "build/preact_hooks.js",
                         "[TEST 2 OUTPUT]", "[BENCHMARK] Preact app + DOM complexity test");
#endif
      }
      else if (testId == 3) {
#ifdef ENABLE_TEST_3
         run_preact_test("src/tests/render.js", "output/render.html", "build/preact.js", "build/preact_hooks.js",
                         "[TEST 3 OUTPUT]", "[BENCHMARK] Preact app + DOM render test",
                         true /* defer cleanup so DOM stays alive for live window */
         );
#endif
      }
   };

   if (stress_loops > 0) {
      int target = which == 2 ? 2 : 1; // default to test1 if 0 or 1
   for (int i = 0; i < stress_loops; i++) {
         fprintf(stderr, "[DIAG] Stress iteration %d/%d start\n", i + 1, stress_loops);
         run_test_suite(target);
         fprintf(stderr, "[DIAG] Stress iteration %d/%d end\n", i + 1, stress_loops);
      }
   }
   else if (which == 0 || which == 1) {
#ifdef ENABLE_TEST_1
      run_preact_test("src/tests/bruteforce.js", "output/bruteforce.html", "build/preact.js", "build/preact_hooks.js",
                      "[TEST 1 OUTPUT]", "[BENCHMARK] Preact app + DOM brute force test", false);
#endif
   }
   if (stress_loops == 0 && (which == 0 || which == 2)) {
#ifdef ENABLE_TEST_2
      run_preact_test("src/tests/complex.js", "output/complex.html", "build/preact.js", "build/preact_hooks.js",
                      "[TEST 2 OUTPUT]", "[BENCHMARK] Preact app + DOM complexity test", false);
#endif
   }
   if (stress_loops == 0 && (which == 0 || which == 3)) {
#ifdef ENABLE_TEST_3
      run_preact_test("src/tests/render.js", "output/render.html", "build/preact.js", "build/preact_hooks.js",
                      "[TEST 3 OUTPUT]", "[BENCHMARK] Preact app + DOM render test", true);
#endif
   }
   // No serializer buffer to free
   const char* exit_mode = getenv("DOM_EXIT_MODE");
   if (exit_mode) {
      if (!strcmp(exit_mode, "fast")) {
         fprintf(stderr, "[DIAG] Fast _exit(0) requested\n");
         _exit(0);
      }
      else if (!strcmp(exit_mode, "abort")) {
         fprintf(stderr, "[DIAG] abort() requested\n");
         abort();
      }
   }
   // After tests: if RUN_ONLY set, skip interactive window & GPU to avoid async teardown races
   if (run_only && *run_only && !getenv("UI_FORCE_WINDOW")) {
      // Perform deferred cleanup immediately if it was requested (we deferred in test 3)
      if (g_deferred_rt && g_deferred_ctx) {
         gpu_shutdown();
         if (void* host = dom_get_host_state(g_deferred_ctx)) {
            auto* im = reinterpret_cast<input::InputManager*>(host);
            delete im;
            dom_set_host_state(g_deferred_ctx, nullptr);
         }
         do_runtime_full_cleanup(g_deferred_rt, g_deferred_ctx, g_deferred_global, g_deferred_document, g_deferred_body);
         g_deferred_rt = nullptr;
         g_deferred_ctx = nullptr;
         g_deferred_global = g_deferred_document = g_deferred_body = JS_UNDEFINED;
      }
      return 0;
   }
   if (run_only && *run_only && getenv("UI_FORCE_WINDOW")) {
      fprintf(stderr, "[INFO] RUN_ONLY=%s set but UI_FORCE_WINDOW present; proceeding to create window.\n", run_only);
   }
   // Otherwise open a simple window (default viewport size) and render layers
   @autoreleasepool {
      NSApplication* app = [NSApplication sharedApplication];
      [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
      id appDel = [NSObject new];
      NSRect frame = NSMakeRect(0, 0, g_winW, g_winH);
      NSWindow* window = [[NSWindow alloc] initWithContentRect:frame
                                                     styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable)
                                                       backing:NSBackingStoreBuffered
                                                         defer:NO];
      [window setTitle:@"Renderer Layers"];
      [window makeKeyAndOrderFront:nil];
      [NSApp activateIgnoringOtherApps:YES];

      // Create an NSImageView backed by a bitmap we paint into via Skia.
      g_winW = VIEWPORT_DEFAULT_WIDTH;
      g_winH = VIEWPORT_DEFAULT_HEIGHT;
      // Determine device scale from the window for crisp rendering.
      CGFloat scale = [[window screen] backingScaleFactor];
      if (scale <= 0) scale = 1.0;
      // Inform adapter per-context state so all canvases allocate at device pixels.
      if (g_deferred_ctx) {
         extern void dom_set_display_scale(JSContext * ctx, float scale);
         dom_set_display_scale(g_deferred_ctx, (float)scale);
      }
   const char* gpuEnv = getenv("SKIA_GPU");
   const char* runOnlyEnv = getenv("RUN_ONLY");
   // Disable GPU for benchmark/test mode (RUN_ONLY) to avoid async destruction races
   if (runOnlyEnv && *runOnlyEnv) {
      g_use_gpu = false;
      fprintf(stderr, "[GPU] Disabled (RUN_ONLY=%s) for test mode; using raster.\n", runOnlyEnv);
   } else {
      g_use_gpu = (gpuEnv && gpuEnv[0] != '0');
   }
#ifdef __APPLE__
      if (g_use_gpu) {
         g_mtlDevice = MTLCreateSystemDefaultDevice();
         if (!g_mtlDevice) { fprintf(stderr, "[GPU] No Metal device; fallback.\n"); g_use_gpu = false; }
         if (g_use_gpu) { g_mtlQueue = [g_mtlDevice newCommandQueue]; if (!g_mtlQueue) { fprintf(stderr, "[GPU] No queue; fallback.\n"); g_use_gpu=false; } }
         if (g_use_gpu) {
            GrMtlBackendContext backend = {};
            backend.fDevice.reset((__bridge void*)g_mtlDevice);
            backend.fQueue.reset((__bridge void*)g_mtlQueue);
            g_grCtx = GrDirectContexts::MakeMetal(backend);
            if (!g_grCtx) { fprintf(stderr, "[GPU] GrDirectContext failed; fallback.\n"); g_use_gpu=false; }
         }
         if (g_use_gpu) {
            g_metalLayer = [CAMetalLayer layer];
            g_metalLayer.device = g_mtlDevice;
            g_metalLayer.pixelFormat = MTLPixelFormatBGRA8Unorm;
            g_metalLayer.frame = frame;
            g_metalLayer.drawableSize = CGSizeMake(g_winW * scale, g_winH * scale);
            g_metalLayer.contentsScale = scale;
            NSView* gpuView = [[InputImageView alloc] initWithFrame:frame];
            [gpuView setWantsLayer:YES];
            gpuView.layer = g_metalLayer;
            [window setContentView:gpuView];
            fprintf(stderr, "[GPU] Metal path active (SKIA_GPU=%s).\n", gpuEnv ? gpuEnv : "");
            // Optional timer-based redraw (env UI_GPU_TIMER=1 to enable). Keeps layer alive & exercises GPU.
            // Lazy redraw on demand only; explicit timers removed for stability.
         }
      }
#endif
      if (!g_use_gpu) {
         const int pw = (int)llround(g_winW * scale);
         const int ph = (int)llround(g_winH * scale);
         SkImageInfo info = SkImageInfo::Make(pw, ph, kN32_SkColorType, kPremul_SkAlphaType);
         g_windowSurface = SkSurfaces::Raster(info);
         g_canvasImageView = (NSImageView*)[[InputImageView alloc] initWithFrame:frame];
         if ([g_canvasImageView respondsToSelector:@selector(setWantsLayer:)]) {
            [g_canvasImageView setWantsLayer:YES];
            if (g_canvasImageView.layer) g_canvasImageView.layer.contentsScale = scale;
         }
         if (g_windowSurface && g_canvasImageView) {
            composite_into_surface(g_windowSurface, g_winW, g_winH);
            present_surface(g_canvasImageView, g_windowSurface, g_winW, g_winH);
            [window setContentView:g_canvasImageView];
         }
      }
      // Expose requestComposite() to JS for drag updates
      if (g_deferred_ctx) {
         JSValue global = JS_GetGlobalObject(g_deferred_ctx);
         JS_SetPropertyStr(g_deferred_ctx, global, "requestComposite",
                           JS_NewCFunction(g_deferred_ctx, js_requestComposite, "requestComposite", 0));
         // Install native mouse event dispatcher; use dynamic viewport (single
         // source: g_winW/g_winH)
         char viewportDecl[128];
         snprintf(viewportDecl, sizeof(viewportDecl), "globalThis.__viewport={w:%d,h:%d};\n", g_winW, g_winH);
       const char* dispatchBody =
             "if(!globalThis.__dispatchNativeMouseInstalled){\n"
             "  globalThis.__dispatchNativeMouseInstalled=true;\n"
             "  globalThis.__dispatchNativeMouse=function(ev){\n"
             "    if(!ev||!ev.type)return; const t=ev.type; const x=ev.clientX|0; const y=ev.clientY|0;\n"
             "    var target=null; var captured=globalThis.__dragTarget||null;\n"
             "    var nodes=document.getElementsByTagName?Array.from(document.getElementsByTagName('canvas')):[];\n"
             "    if(captured){ target=captured; } else {\n"
             "      for(var i=nodes.length-1;i>=0;i--){\n"
             "        var n=nodes[i]; var st=n.getAttribute? (n.getAttribute('style')||''):'';\n"
             "        var mW=st.match(/width:(\\d+)/); var mH=st.match(/height:(\\d+)/);\n"
             "        var mL=st.match(/left:(\\d+)/); var mT=st.match(/top:(\\d+)/);\n"
             "        var w=mW?+mW[1]:64; var h=mH?+mH[1]:64; var lx=mL?+mL[1]:0; var ty=mT?+mT[1]:0;\n"
             "        if(x>=lx&&x<=lx+w&&y>=ty&&y<=ty+h){ target=n; break; }\n"
             "      }\n"
             "    }\n"
             "    if(!target && t!=='mousemove' && t!=='mouseup') return;\n"
             "    if(t==='mousedown' && target){\n"
             "      var st=target.getAttribute('style')||'';\n"
             "      var mL=/left:(\\d+)/.exec(st); var mT=/top:(\\d+)/.exec(st);\n"
             "      target.__draggingOffset=[x-(mL?+mL[1]:0), y-(mT?+mT[1]:0)];\n"
             "      globalThis.__dragTarget=target; globalThis.__dragActive=true;\n"
             "    }\n"
             "    if(t==='mousemove' && globalThis.__dragTarget){\n"
             "      var target=globalThis.__dragTarget; var off=target.__draggingOffset; var st=target.getAttribute('style')||'';\n"
             "      var mW=st.match(/width:(\\d+)/); var mH=st.match(/height:(\\d+)/);"
             " var w=mW?+mW[1]:64; var h=mH?+mH[1]:64;\n"
             "      var nx=Math.max(0,Math.min(globalThis.__viewport.w-w,x-off[0]));\n"
             "      var ny=Math.max(0,Math.min(globalThis.__viewport.h-h,y-off[1]));\n"
             "      target.setAttribute('style', st.replace(/left:\\d+/, 'left:'+nx)"
             ".replace(/top:\\d+/, 'top:'+ny));\n"
             "      if(typeof requestComposite==='function') requestComposite();\n"
             "    }\n"
             "    if(t==='mouseup'){ if(globalThis.__dragTarget){ delete globalThis.__dragTarget.__draggingOffset; } globalThis.__dragTarget=null; globalThis.__dragActive=false; }\n"
             "    var dispatchEl = globalThis.__dragTarget || target;\n"
             "    var arr = dispatchEl? dispatchEl['__listeners_'+t] : null;\n"
             "    if (Array.isArray(arr)) { for (var i=0;i<arr.length;i++){ try { arr[i].call(dispatchEl, ev); } catch(e) "
             "{} } }\n"
             "  };\n"
             "}\n";
         std::string dispatchSrc = std::string(viewportDecl) + dispatchBody;
         JSValue r = JS_Eval(g_deferred_ctx, dispatchSrc.c_str(), dispatchSrc.size(), "<install_dispatch>",
                             JS_EVAL_TYPE_GLOBAL);
         if (JS_IsException(r))
            dump_exception(g_deferred_ctx);
         JS_FreeValue(g_deferred_ctx, r);
         // Install requestComposite throttling with env-configurable pacing and drag-aware fast path
         double normalDt = 16.0;
         if (const char* s = getenv("UI_TARGET_DT_MS")) { double v = atof(s); if (v >= 0.0) normalDt = v; }
         else if (const char* s2 = getenv("UI_MAX_FPS")) { double f = atof(s2); if (f > 0.0) normalDt = 1000.0 / f; }
         double dragDt = 0.0; // default: immediate during drag
         if (const char* sd = getenv("UI_DRAG_DT_MS")) { double v = atof(sd); if (v >= 0.0) dragDt = v; }
         else if (const char* sf = getenv("UI_DRAG_MAX_FPS")) { double f = atof(sf); if (f > 0.0) dragDt = 1000.0 / f; }
         char throttleBuf[1024];
         snprintf(throttleBuf, sizeof(throttleBuf),
                  "if(typeof requestComposite==='function' && !globalThis.__rcThrottle){"
                  "(function(){var last=0;var pending=false;function fire(){pending=false;last=Date.now();requestCompositeImmediate();}"
                  "if(!globalThis.requestCompositeImmediate){globalThis.requestCompositeImmediate=requestComposite;}"
                  "globalThis.requestComposite=function(){var now=Date.now();var target=(globalThis.__dragActive?%.3f:%.3f);"
                  "if(target<=0){if(!pending && typeof setTimeout==='function'){pending=true;setTimeout(fire,0);}return;}"
                  "var dt=now-last; if(dt>=target){fire();} else if(!pending && typeof setTimeout==='function'){pending=true;setTimeout(fire,Math.max(0, target-dt));}};"
                  "globalThis.__rcThrottle=true;})();}",
                  dragDt, normalDt);
         JSValue tr = JS_Eval(g_deferred_ctx, throttleBuf, strlen(throttleBuf),
                              "<throttle_requestComposite>", JS_EVAL_TYPE_GLOBAL);
         if (JS_IsException(tr))
            dump_exception(g_deferred_ctx);
         JS_FreeValue(g_deferred_ctx, tr);
         JS_FreeValue(g_deferred_ctx, global);
      }
      // Automated GPU frame test (pre-loop synchronous frames): UI_GPU_TEST_FRAMES=N
      if (g_use_gpu) {
         const char* framesEnv = getenv("UI_GPU_TEST_FRAMES");
         if (framesEnv && *framesEnv) {
            int targetFrames = atoi(framesEnv);
            if (targetFrames > 0) {
               if (targetFrames > 1 && !getenv("UI_GPU_MULTI_OK")) {
                  fprintf(stderr, "[GPU] UI_GPU_TEST_FRAMES=%d clamped to 1 (multi-frame Metal pre-loop draws unstable). Set UI_GPU_MULTI_OK=1 to force.\n", targetFrames);
                  targetFrames = 1;
               }
               g_gpu_sync_frames = true;
               g_gpu_test_preloop = true;
               fprintf(stderr, "[GPU] Automated pre-loop frame test: rendering %d frames synchronously.\n", targetFrames);
               for (int i = 0; i < targetFrames; ++i) {
                  gpu_composite_and_present(g_winW, g_winH);
                  // brief delay to let CAMetalLayer present internals settle
                  usleep(2000);
               }
               g_gpu_test_preloop = false;
               // Immediate teardown without entering run loop to avoid async drawable calls.
               fprintf(stderr, "[GPU] Pre-loop frames done; performing immediate teardown.\n");
               for (NSWindow* w in [NSApp windows]) { [w close]; }
               if (g_deferred_rt && g_deferred_ctx) {
                  if (void* host = dom_get_host_state(g_deferred_ctx)) {
                     auto* im = reinterpret_cast<input::InputManager*>(host);
                     delete im;
                     dom_set_host_state(g_deferred_ctx, nullptr);
                  }
                  do_runtime_full_cleanup(g_deferred_rt, g_deferred_ctx, g_deferred_global, g_deferred_document,
                                          g_deferred_body);
                  g_deferred_rt = nullptr;
                  g_deferred_ctx = nullptr;
                  g_deferred_global = g_deferred_document = g_deferred_body = JS_UNDEFINED;
               }
               if (g_grCtx) { g_grCtx->flush(); g_grCtx->submit(); }
               gpu_shutdown();
               g_gpu_sync_frames = false;
               return 0;
            }
         }
      }
   g_run_loop_active = true;
   [app run];
   g_run_loop_active = false;
      // After window loop exits (user closed window), perform deferred cleanup if
      // any
   if (g_deferred_rt && g_deferred_ctx) {
      // Close any lingering windows explicitly (automated test mode does not close them itself)
      for (NSWindow* w in [NSApp windows]) { [w close]; }
      // Delete host state (InputManager) stored per-context prior to teardown
         if (void* host = dom_get_host_state(g_deferred_ctx)) {
            auto* im = reinterpret_cast<input::InputManager*>(host);
            delete im;
            dom_set_host_state(g_deferred_ctx, nullptr);
         }
         do_runtime_full_cleanup(g_deferred_rt, g_deferred_ctx, g_deferred_global, g_deferred_document,
                                 g_deferred_body);
         g_deferred_rt = nullptr;
         g_deferred_ctx = nullptr;
         g_deferred_global = g_deferred_document = g_deferred_body = JS_UNDEFINED;
      // Final Skia flush & queue drain before GPU shutdown
      if (g_use_gpu && g_grCtx) { g_grCtx->flush(); g_grCtx->submit(); }
      // Spin briefly giving Metal a chance to retire (replace with proper fence later)
      if (g_use_gpu) usleep(30000);
      gpu_shutdown();
      }
      else {
         gpu_shutdown(); // safety if no deferred ctx
      }
   g_gpu_sync_frames = false; // reset for subsequent runs (if any)
   }
   return 0;
}