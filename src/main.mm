#include "wapis/dom.hpp" // for dom::Element attribute access
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
// Lexbor for pretty HTML formatting
#include "gfx/sk_canvas_view.h"
#include "layout/layout_yoga.h"
#include "renderer/renderer.h"
#include "renderer/scheduler.h"
#include "viewport.h" // default viewport size
#import <Cocoa/Cocoa.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkImage.h>
#include <include/core/SkSurface.h>
#include <lexbor/html/html.h>
#include <random>
#include <string>
#include <utility>

// Forward declare dump_exception for use in mouse event forwarding
static void dump_exception(JSContext *ctx);

// Globals from deferred runtime (declared later in file). We'll extern them
// here if needed. g_deferred_ctx defined later (deferred runtime support)

// Old direct mouse bridge removed; handled via input subsystem (see
// input/mac.mm)
@interface CanvasImageView : NSImageView
@end // legacy (no events)
#import "input/InputImageView.h"

// Reusable pretty HTML formatter using Lexbor. Accepts raw outerHTML of <body>
// subtree.
static std::string pretty_format_body_html(const char *body_outer_html) {
  if (!body_outer_html)
    return {};
  std::string wrapped = std::string("<html>") + body_outer_html + "</html>";
  lxb_html_document_t *lxb_doc = lxb_html_document_create();
  if (!lxb_doc)
    return body_outer_html; // fallback
  lxb_status_t st = lxb_html_document_parse(
      lxb_doc, (const lxb_char_t *)wrapped.c_str(), wrapped.size());
  if (st != LXB_STATUS_OK) {
    lxb_html_document_destroy(lxb_doc);
    return body_outer_html; // fallback
  }
  lxb_dom_node_t *body_node =
      (lxb_dom_node_t *)lxb_html_document_body_element(lxb_doc);
  std::string pretty;
  if (body_node) {
    struct Buf {
      std::string s;
    };
    Buf buf;
    auto cb = [](const lxb_char_t *data, size_t len,
                 void *ctx) -> lxb_status_t {
      ((Buf *)ctx)->s.append((const char *)data, len);
      return LXB_STATUS_OK;
    };
    if (lxb_html_serialize_pretty_tree_cb(body_node,
                                          LXB_HTML_SERIALIZE_OPT_UNDEF, 0, cb,
                                          &buf) == LXB_STATUS_OK) {
      pretty = std::move(buf.s);
    }
  }
  lxb_html_document_destroy(lxb_doc);
  if (pretty.empty())
    return body_outer_html; // fallback
  // Lexbor currently quotes text node data; strip leading/trailing quotes on
  // lines consisting solely of quoted text.
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
      size_t indent_len =
          core.data() - line.data(); // distance from line begin to core
      cleaned.append(line.substr(0, indent_len));
      cleaned.append(inner);
    } else {
      cleaned.append(line);
    }
    if (end < pretty.size())
      cleaned.push_back('\n');
    start = (end == pretty.size()) ? end : end + 1;
  }
  return cleaned.empty() ? pretty : cleaned;
}

static void diagnostic_atexit() {
  fprintf(stderr, "[DIAG] atexit handler running (normal process teardown)\n");
  fflush(stderr);
}

struct DiagnosticDtor {
  ~DiagnosticDtor() { fprintf(stderr, "[DIAG] static destructor executed\n"); }
} g_diagnostic_dtor;

// Uncomment to enable each test

// #define ENABLE_TEST_1 // disabled (kept for reference)
// #define ENABLE_TEST_2 // disabled (kept for reference)
#define ENABLE_TEST_3 // new render test

typedef struct {
  double elapsed;
  int success;
} TestResult;

// Deferred environment (only used for live render window test)
static JSRuntime *g_deferred_rt = nullptr;
JSContext *g_deferred_ctx = nullptr;
static JSValue g_deferred_global = JS_UNDEFINED;
static JSValue g_deferred_document = JS_UNDEFINED;
static JSValue g_deferred_body = JS_UNDEFINED;

// Forward declaration for compositing reuse
static void perform_composite_and_present(NSImageView *iv,
                                          sk_sp<SkSurface> surface, int W,
                                          int H, NSWindow *window);
static NSImageView *g_canvasImageView = nil;
static sk_sp<SkSurface> g_windowSurface;
int g_winW = VIEWPORT_DEFAULT_WIDTH,
    g_winH = VIEWPORT_DEFAULT_HEIGHT; // single initialization point

static void composite_into_surface(sk_sp<SkSurface> surface, int W, int H) {
  if (!surface)
    return;
  // Make sure layout applied just-in-time (in case no explicit requestComposite
  // call)
  if (g_deferred_ctx)
    layout_maybe_run(g_deferred_ctx);
  SkCanvas *canvas = surface->getCanvas();
  canvas->clear(SkColorSetARGB(255, 0x20, 0x20, 0x20));
  extern bool gfx_get_size(int id, int *w, int *h);
  extern sk_sp<SkImage> gfx_snapshot(int id);
  extern int dom_element_canvas_id(dom::Element * el, bool createIfMissing);
  renderer_for_each_layer([&](RenderLayer *rl) {
    if (!rl || !rl->element)
      return;
    int id = dom_element_canvas_id(rl->element, false);
    int x = 0, y = 0, w = 0, h = 0;
    if (!layout_get_box(rl->element, x, y, w, h)) {
      // fallback if layout omitted this element
      w = 50;
      h = 50;
    }
    // Draw background color if present even without canvas surface
    const std::string &st = rl->element->styleCssText;
    auto trim = [&](std::string s) {
      size_t a = s.find_first_not_of(" \t");
      size_t b = s.find_last_not_of(" \t");
      if (a == std::string::npos)
        return std::string();
      return s.substr(a, b - a + 1);
    };
    auto parseRgb = [&](const std::string &val) {
      int r = 0, gc = 0, b = 0;
      if (sscanf(val.c_str(), "rgb(%d,%d,%d)", &r, &gc, &b) == 3) {
        return SkColorSetARGB(255, (uint8_t)r, (uint8_t)gc, (uint8_t)b);
      }
      return (SkColor)0;
    };
    SkColor bg = 0;
    // background-color:
    size_t bcp = st.find("background-color:");
    if (bcp != std::string::npos) {
      size_t vs = bcp + 17;
      size_t sc = st.find(';', vs);
      std::string val = trim(
          st.substr(vs, sc == std::string::npos ? std::string::npos : sc - vs));
      bg = parseRgb(val);
    }
    if (!bg) {
      size_t bp = st.find("background:");
      if (bp != std::string::npos) {
        size_t vs = bp + 11;
        size_t sc = st.find(';', vs);
        std::string val = trim(st.substr(
            vs, sc == std::string::npos ? std::string::npos : sc - vs));
        bg = parseRgb(val);
      }
    }
    if (bg) {
      SkPaint p;
      p.setStyle(SkPaint::kFill_Style);
      p.setColor(bg);
      canvas->drawRect(SkRect::MakeXYWH(x, y, w, h), p);
    }
    if (id) {
      auto img = gfx_snapshot(id);
      if (img) {
        canvas->drawImage(img.get(), (SkScalar)x, (SkScalar)y);
        if (getenv("DEBUG_DRAW_BORDER")) {
          SkPaint border;
          border.setStyle(SkPaint::kStroke_Style);
          border.setColor(SK_ColorWHITE);
          border.setStrokeWidth(1);
          canvas->drawRect(SkRect::MakeXYWH(x, y, w, h), border);
        }
      }
    }
  });
}

static void present_surface(NSImageView *iv, sk_sp<SkSurface> surface, int W,
                            int H) {
  if (!iv || !surface)
    return;
  SkPixmap pixmap;
  if (!surface->peekPixels(&pixmap))
    return;
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
  CGBitmapInfo bitmapInfo =
      kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big;
  CGDataProviderRef provider = CGDataProviderCreateWithData(
      NULL, pixmap.addr(), pixmap.rowBytes() * pixmap.height(), NULL);
  CGImageRef cgImage = CGImageCreate(
      pixmap.width(), pixmap.height(), 8, 32, pixmap.rowBytes(), colorSpace,
      bitmapInfo, provider, NULL, false, kCGRenderingIntentDefault);
  if (cgImage) {
    NSImage *img = [[NSImage alloc] initWithCGImage:cgImage
                                               size:NSMakeSize(W, H)];
    [iv setImage:img];
    CGImageRelease(cgImage);
  }
  CGDataProviderRelease(provider);
  CGColorSpaceRelease(colorSpace);
}

static JSValue js_requestComposite(JSContext *ctx, JSValueConst this_val,
                                   int argc, JSValueConst *argv) {
  if (g_windowSurface) {
    // Run internal layout pass before painting (JS unaware)
    layout_maybe_run(ctx);
    composite_into_surface(g_windowSurface, g_winW, g_winH);
    present_surface(g_canvasImageView, g_windowSurface, g_winW, g_winH);
  }
  return JS_UNDEFINED;
}

// Internal native composite trigger used by layout auto-pass
void native_request_composite(JSContext *ctx) {
  (void)ctx;
  if (g_windowSurface) {
    composite_into_surface(g_windowSurface, g_winW, g_winH);
    present_surface(g_canvasImageView, g_windowSurface, g_winW, g_winH);
  }
}

// Minimal native->JS mouse event bridge (no architecture): calls global
// onNativeMouseEvent(evt)
@implementation CanvasImageView
@end

static void do_runtime_full_cleanup(JSRuntime *rt, JSContext *ctx,
                                    JSValue global, JSValue document,
                                    JSValue body) {
  if (!ctx || !rt)
    return;
  fprintf(stderr, "[DEBUG] (deferred) Cleanup: start\n");
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
  dom_runtime_cleanup(ctx);
  JS_RunGC(rt);
  for (int i = 0; i < 3; i++)
    JS_RunGC(rt);
  JS_FreeContext(ctx);
  {
    volatile char *p = (char *)malloc(16);
    if (p) {
      p[0] = 0;
      free((void *)p);
    }
  }
  dom_adapter_unregister_runtime(rt);
  JS_FreeRuntime(rt);
  fprintf(stderr, "[DEBUG] (deferred) Cleanup: end\n");
}

// Forward declarations
static char *load_file(const char *filename, size_t *out_len);
static void dump_exception(JSContext *ctx);
double run_test(JSContext *ctx, const char *filename);

TestResult run_preact_test(const char *test_js, const char *output_html,
                           const char *preact_js_path,
                           const char *hooks_js_path, const char *test_label,
                           const char *benchmark_label, bool defer_cleanup) {

  TestResult result = {0};
  JSRuntime *rt = JS_NewRuntime();
  JSContext *ctx = JS_NewContext(rt);
  int error = 0;
  size_t preact_js_len = 0, hooks_js_len = 0, htm_js_len = 0;
  char *preact_js = NULL, *hooks_js = NULL, *htm_js = NULL;
  JSValue r = JS_UNDEFINED, html_val = JS_UNDEFINED;
  const char *html = NULL;
  JSValue global = JS_UNDEFINED, document = JS_UNDEFINED, body = JS_UNDEFINED;
  const char *assign_hooks =
      "if (typeof preactHooks !== 'undefined') preact.hooks = preactHooks;";

  define_whatwg_globals(ctx);
  dom_define_node_proto(ctx);
  global = JS_GetGlobalObject(ctx);
  document = dom_create_document(ctx);
  JS_SetPropertyStr(ctx, global, "document", JS_DupValue(ctx, document));
  // Expose factory methods via adapter helper (internal js_create* now static)
  dom_attach_document_factories(ctx, document);
  body = JS_GetPropertyStr(ctx, document, "body");
  JS_SetPropertyStr(ctx, global, "window", JS_DupValue(ctx, global));
  JS_SetPropertyStr(ctx, global, "self", JS_DupValue(ctx, global));
  JS_SetPropertyStr(ctx, global, "globalThis", JS_DupValue(ctx, global));
  // Install gfx (Skia surface) helpers
  gfx_install_js(ctx);
  // Initialize renderer (DOM observer)
  scheduler_init();
  renderer_init();

  preact_js = load_file(preact_js_path, &preact_js_len);
  if (!preact_js) {
    fprintf(stderr, "Failed to load %s\n", preact_js_path);
    error = 1;
    goto cleanup;
  }
  r = JS_Eval(ctx, preact_js, preact_js_len, preact_js_path,
              JS_EVAL_TYPE_GLOBAL);
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

  r = JS_Eval(ctx, assign_hooks, strlen(assign_hooks), "<assign_hooks>",
              JS_EVAL_TYPE_GLOBAL);
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
    const char *bind_html =
        "if (typeof htm !== 'undefined' && typeof preact !== 'undefined') { "
        "globalThis.htm = htm.bind(preact.h); }";
    r = JS_Eval(ctx, bind_html, strlen(bind_html), "<bind_htm>",
                JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r))
      dump_exception(ctx);
    JS_FreeValue(ctx, r);
  } else {
    fprintf(stderr,
            "[WARN] build/htm.js not found; html templates unavailable\n");
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
      const char *final_html = pretty.c_str();
      printf("%s\n%s\n", test_label, final_html);
      FILE *f = fopen(output_html, "w");
      if (f) {
        fputs(final_html, f);
        fclose(f);
        result.success = 1;
      } else {
        fprintf(stderr, "Failed to open %s for writing\n", output_html);
      }
      JS_FreeCString(ctx, html);
    }
  } else {
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
    fprintf(stderr,
            "[DEBUG] Deferred cleanup (keeping runtime alive for window)\n");
  } else {
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
    const char *disableCleanup = getenv("DOM_DISABLE_CLEANUP");
    if (!disableCleanup || !*disableCleanup) {
      JS_RunGC(rt);
      dom_runtime_cleanup(ctx);
      JS_RunGC(rt);
    } else {
      fprintf(
          stderr,
          "[MAIN] DOM cleanup skipped due to DOM_DISABLE_CLEANUP env var\n");
    }
    for (int i = 0; i < 3; ++i)
      JS_RunGC(rt);
    fprintf(stderr, "[DEBUG] Freeing JSContext and JSRuntime\n");
    JS_FreeContext(ctx);
    {
      volatile char *p = (char *)malloc(16);
      if (p) {
        p[0] = 0;
        free((void *)p);
      }
    }
    dom_adapter_unregister_runtime(rt);
    JS_FreeRuntime(rt);
  }
  FILE *mf = fopen("output/last_run_ok.marker", "w");
  if (mf) {
    fputs("ok", mf);
    fclose(mf);
  }
  fflush(stdout);
  if (!error && result.elapsed >= 0)
    printf("%s: %.1f ms\n", benchmark_label, result.elapsed);
  return result;
}

void dump_exception(JSContext *ctx) {
  JSValue ex = JS_GetException(ctx);
  const char *err = JS_ToCString(ctx, ex);
  fprintf(stderr, "Exception: %s\n", err ? err : "(no message)");
  JS_FreeCString(ctx, err);
  JSValue stack = JS_GetPropertyStr(ctx, ex, "stack");
  if (!JS_IsUndefined(stack)) {
    const char *s = JS_ToCString(ctx, stack);
    fprintf(stderr, "%s\n", s);
    JS_FreeCString(ctx, s);
  }
  JS_FreeValue(ctx, stack);
  JS_FreeValue(ctx, ex);
}

static char *load_file(const char *filename, size_t *out_len) {
  FILE *f = fopen(filename, "rb");
  if (!f)
    return NULL;
  fseek(f, 0, SEEK_END);
  long len = ftell(f);
  fseek(f, 0, SEEK_SET);
  char *buf = (char *)malloc(len + 1);
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

double run_test(JSContext *ctx, const char *filename) {
  struct timeval start, end;
  size_t js_len = 0;
  char *js = load_file(filename, &js_len);
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
  return (end.tv_sec - start.tv_sec) * 1000.0 +
         (end.tv_usec - start.tv_usec) / 1000.0;
}

int main(int argc, char **argv) {
  atexit(diagnostic_atexit);
// Enable core dumps for segfault debugging
#include <sys/resource.h>
  // No external serializer needed (using body.outerHTML)
  const char *run_only = getenv("RUN_ONLY");
  int which = run_only ? atoi(run_only) : 0; // 0 = both
  const char *stress = getenv("DOM_STRESS_LOOPS");
  int stress_loops = stress ? atoi(stress) : 0;
  if (stress_loops > 0) {
    fprintf(stderr, "[DIAG] Stress mode: %d loops (RUN_ONLY=%d)\n",
            stress_loops, which);
  }
  auto run_test_suite = [&](int testId) {
    if (testId == 1) {
#ifdef ENABLE_TEST_1
      run_preact_test("src/tests/bruteforce.js", "output/bruteforce.html",
                      "build/preact.js", "build/preact_hooks.js",
                      "[TEST 1 OUTPUT]",
                      "[BENCHMARK] Preact app + DOM brute force test");
#endif
    } else if (testId == 2) {
#ifdef ENABLE_TEST_2
      run_preact_test("src/tests/complex.js", "output/complex.html",
                      "build/preact.js", "build/preact_hooks.js",
                      "[TEST 2 OUTPUT]",
                      "[BENCHMARK] Preact app + DOM complexity test");
#endif
    } else if (testId == 3) {
#ifdef ENABLE_TEST_3
      run_preact_test(
          // "src/tests/render.js", // disabled in favor of layout.js
          "src/tests/layout.js", "output/render.html", "build/preact.js",
          "build/preact_hooks.js", "[TEST 3 OUTPUT]",
          "[BENCHMARK] Preact app + DOM render test",
          true /* defer cleanup so DOM stays alive for live window */
      );
#endif
    }
  };

  if (stress_loops > 0) {
    int target = which == 2 ? 2 : 1; // default to test1 if 0 or 1
    for (int i = 0; i < stress_loops; i++) {
      fprintf(stderr, "[DIAG] Stress iteration %d/%d start\n", i + 1,
              stress_loops);
      run_test_suite(target);
      fprintf(stderr, "[DIAG] Stress iteration %d/%d end\n", i + 1,
              stress_loops);
    }
  } else if (which == 0 || which == 1) {
#ifdef ENABLE_TEST_1
    run_preact_test("src/tests/bruteforce.js", "output/bruteforce.html",
                    "build/preact.js", "build/preact_hooks.js",
                    "[TEST 1 OUTPUT]",
                    "[BENCHMARK] Preact app + DOM brute force test", false);
#endif
  }
  if (stress_loops == 0 && (which == 0 || which == 2)) {
#ifdef ENABLE_TEST_2
    run_preact_test("src/tests/complex.js", "output/complex.html",
                    "build/preact.js", "build/preact_hooks.js",
                    "[TEST 2 OUTPUT]",
                    "[BENCHMARK] Preact app + DOM complexity test", false);
#endif
  }
  if (stress_loops == 0 && (which == 0 || which == 3)) {
#ifdef ENABLE_TEST_3
    run_preact_test(
        // "src/tests/render.js", // disabled in favor of layout.js
        "src/tests/layout.js", "output/render.html", "build/preact.js",
        "build/preact_hooks.js", "[TEST 3 OUTPUT]",
        "[BENCHMARK] Preact app + DOM render test", true);
#endif
  }
  // No serializer buffer to free
  const char *exit_mode = getenv("DOM_EXIT_MODE");
  if (exit_mode) {
    if (!strcmp(exit_mode, "fast")) {
      fprintf(stderr, "[DIAG] Fast _exit(0) requested\n");
      _exit(0);
    } else if (!strcmp(exit_mode, "abort")) {
      fprintf(stderr, "[DIAG] abort() requested\n");
      abort();
    }
  }
  // After running tests, open a simple window (default viewport size) and
  // render layers
  @autoreleasepool {
    NSApplication *app = [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    static id appDel = nil;
    if (!appDel)
      appDel = [NSObject new];
    NSRect frame = NSMakeRect(0, 0, g_winW, g_winH);
    NSWindow *window =
        [[NSWindow alloc] initWithContentRect:frame
                                    styleMask:(NSWindowStyleMaskTitled |
                                               NSWindowStyleMaskClosable)
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    [window setTitle:@"Renderer Layers"];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];

    // Create an NSImageView backed by a bitmap we paint into via Skia (now
    // storing globals for incremental recomposite)
    g_winW = VIEWPORT_DEFAULT_WIDTH;
    g_winH = VIEWPORT_DEFAULT_HEIGHT;
    SkImageInfo info = SkImageInfo::Make(g_winW, g_winH, kN32_SkColorType,
                                         kPremul_SkAlphaType);
    g_windowSurface = SkSurfaces::Raster(info);
    g_canvasImageView =
        (NSImageView *)[[InputImageView alloc] initWithFrame:frame];
    if (g_windowSurface && g_canvasImageView) {
      composite_into_surface(g_windowSurface, g_winW, g_winH);
      present_surface(g_canvasImageView, g_windowSurface, g_winW, g_winH);
      [window setContentView:g_canvasImageView];
    }
    // Expose requestComposite() to JS for drag updates
    if (g_deferred_ctx) {
      JSValue global = JS_GetGlobalObject(g_deferred_ctx);
      JS_SetPropertyStr(g_deferred_ctx, global, "requestComposite",
                        JS_NewCFunction(g_deferred_ctx, js_requestComposite,
                                        "requestComposite", 0));
      // Install native mouse event dispatcher; use dynamic viewport (single
      // source: g_winW/g_winH)
      char viewportDecl[128];
      snprintf(viewportDecl, sizeof(viewportDecl),
               "globalThis.__viewport={w:%d,h:%d};\n", g_winW, g_winH);
      const char *dispatchBody = R"JS(
if(!globalThis.__dispatchNativeMouseInstalled){
    globalThis.__dispatchNativeMouseInstalled=true;
    globalThis.__dispatchNativeMouse=function(ev){
        if(!ev||!ev.type)return; const t=ev.type; const x=ev.clientX|0; const y=ev.clientY|0;
        var target=null; var nodes=document.getElementsByTagName?Array.from(document.getElementsByTagName('canvas')):[];
        for(var i=nodes.length-1;i>=0;i--){
            var n=nodes[i]; var st=n.getAttribute? (n.getAttribute('style')||''):'';
            var mW=st.match(/width:(\d+)/); var mH=st.match(/height:(\d+)/); var mL=st.match(/left:(\d+)/); var mT=st.match(/top:(\d+)/);
            var w=mW?+mW[1]:64; var h=mH?+mH[1]:64; var lx=mL?+mL[1]:0; var ty=mT?+mT[1]:0;
            if(x>=lx&&x<=lx+w&&y>=ty&&y<=ty+h){ target=n; break; }
        }
        if(!target) return;
        if(t==='mousedown') {
            var st=target.getAttribute('style')||'';
            var mL=/left:(\d+)/.exec(st); var mT=/top:(\d+)/.exec(st);
            target.__draggingOffset=[x-(mL?+mL[1]:0), y-(mT?+mT[1]:0)];
        }
        if(t==='mousemove' && target.__draggingOffset){
            var off=target.__draggingOffset; var st=target.getAttribute('style')||'';
            var mW=st.match(/width:(\d+)/); var mH=st.match(/height:(\d+)/); var w=mW?+mW[1]:64; var h=mH?+mH[1]:64;
            var nx=Math.max(0,Math.min(globalThis.__viewport.w-w,x-off[0]));
            var ny=Math.max(0,Math.min(globalThis.__viewport.h-h,y-off[1]));
            target.setAttribute('style', st.replace(/left:\d+/, 'left:'+nx).replace(/top:\d+/, 'top:'+ny));
            if(typeof requestComposite==='function') requestComposite();
        }
        if(t==='mouseup') { if(target.__draggingOffset) delete target.__draggingOffset; }
        var arr = target['__listeners_'+t];
        if (Array.isArray(arr)) { for (var i=0;i<arr.length;i++) { try { arr[i].call(target, ev); } catch(e) {} } }
    };
}
)JS";
      std::string dispatchSrc = std::string(viewportDecl) + dispatchBody;
      JSValue r =
          JS_Eval(g_deferred_ctx, dispatchSrc.c_str(), dispatchSrc.size(),
                  "<install_dispatch>", JS_EVAL_TYPE_GLOBAL);
      if (JS_IsException(r))
        dump_exception(g_deferred_ctx);
      JS_FreeValue(g_deferred_ctx, r);
      JS_FreeValue(g_deferred_ctx, global);
    }
    [app run];
    // After window loop exits (user closed window), perform deferred cleanup if
    // any
    if (g_deferred_rt && g_deferred_ctx) {
      do_runtime_full_cleanup(g_deferred_rt, g_deferred_ctx, g_deferred_global,
                              g_deferred_document, g_deferred_body);
      g_deferred_rt = nullptr;
      g_deferred_ctx = nullptr;
      g_deferred_global = g_deferred_document = g_deferred_body = JS_UNDEFINED;
    }
  }
  return 0;
}