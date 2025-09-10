#include "sk_canvas_view.h"
#include <include/core/SkColor.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkPaint.h>
#include <include/core/SkSurface.h>
#include <mutex>

struct GfxStateHandle {
   std::mutex mtx;
   int next_id = 1;
   std::unordered_map<int, SkCanvasView> views;
   float device_scale = 1.0f;
};

GfxStateHandle* gfx_state_create()
{
   return new GfxStateHandle();
}

void gfx_state_destroy(GfxStateHandle* g)
{
   delete g;
}

static SkCanvas* canvas_for(GfxStateHandle* gs, int id)
{
   auto it = gs->views.find(id);
   if (it == gs->views.end())
      return nullptr;
   return it->second.surface ? it->second.surface->getCanvas() : nullptr;
}

int gfx_create_canvas(GfxStateHandle* gs, int width, int height)
{
   if (!gs || width <= 0 || height <= 0)
      return -1;
   std::lock_guard<std::mutex> lock(gs->mtx);
   // Allocate backing surface in device pixels, but draw in logical units by scaling the canvas.
   const float s = gs->device_scale <= 0.f ? 1.f : gs->device_scale;
   const int pw = (int)std::lround(width * s);
   const int ph = (int)std::lround(height * s);
   SkImageInfo info = SkImageInfo::Make(pw, ph, kN32_SkColorType, kPremul_SkAlphaType);
   sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
   if (!surface)
      return -1;
   int id = gs->next_id++;
   gs->views[id] = {id, width, height, surface};
   // Ensure drawing commands use logical coordinates (points)
   if (SkCanvas* c = surface->getCanvas()) {
      if (s != 1.f) c->scale(s, s);
   }
   return id;
}

void gfx_fill_rect(GfxStateHandle* gs, int id, int x, int y, int w, int h, uint32_t rgba)
{
   if (!gs)
      return;
   std::lock_guard<std::mutex> lock(gs->mtx);
   SkCanvas* c = canvas_for(gs, id);
   if (!c)
      return;
   SkPaint p;
   p.setStyle(SkPaint::kFill_Style);
   uint8_t r = (rgba >> 24) & 0xFF, g = (rgba >> 16) & 0xFF, b = (rgba >> 8) & 0xFF, a = (rgba) & 0xFF;
   p.setColor(SkColorSetARGB(a, r, g, b));
   c->drawRect(SkRect::MakeXYWH(x, y, w, h), p);
}

void gfx_fill_circle(GfxStateHandle* gs, int id, int cx, int cy, int radius, uint32_t rgba)
{
   if (!gs)
      return;
   std::lock_guard<std::mutex> lock(gs->mtx);
   SkCanvas* c = canvas_for(gs, id);
   if (!c)
      return;
   SkPaint p;
   p.setStyle(SkPaint::kFill_Style);
   uint8_t r = (rgba >> 24) & 0xFF, g = (rgba >> 16) & 0xFF, b = (rgba >> 8) & 0xFF, a = (rgba) & 0xFF;
   p.setAntiAlias(true);
   p.setColor(SkColorSetARGB(a, r, g, b));
   c->drawCircle((SkScalar)cx, (SkScalar)cy, (SkScalar)radius, p);
}

sk_sp<SkImage> gfx_snapshot(GfxStateHandle* gs, int id)
{
   if (!gs)
      return nullptr;
   std::lock_guard<std::mutex> lock(gs->mtx);
   auto it = gs->views.find(id);
   if (it == gs->views.end())
      return nullptr;
   return it->second.surface ? it->second.surface->makeImageSnapshot() : nullptr;
}

bool gfx_get_size(GfxStateHandle* gs, int id, int* outW, int* outH)
{
   if (!gs)
      return false;
   std::lock_guard<std::mutex> lock(gs->mtx);
   auto it = gs->views.find(id);
   if (it == gs->views.end())
      return false;
   if (outW)
      *outW = it->second.width;
   if (outH)
      *outH = it->second.height;
   return true;
}

void gfx_set_device_scale(GfxStateHandle* gs, float scale)
{
   if (!gs)
      return;
   std::lock_guard<std::mutex> lock(gs->mtx);
   float s = (scale > 0.f) ? scale : 1.f;
   if (std::abs(gs->device_scale - s) < 1e-6f) {
      gs->device_scale = s;
      return;
   }
   // Update scale for future draws without discarding existing content.
   // Existing pixels remain at their previous resolution until redrawn.
   gs->device_scale = s;
   for (auto& kv : gs->views) {
      SkCanvasView& v = kv.second;
      if (v.surface) {
         if (SkCanvas* c = v.surface->getCanvas()) {
            c->resetMatrix();
            if (s != 1.f)
               c->scale(s, s);
         }
      }
   }
}

float gfx_get_device_scale(GfxStateHandle* gs)
{
   if (!gs)
      return 1.f;
   std::lock_guard<std::mutex> lock(gs->mtx);
   return gs->device_scale;
}

// JS binding helpers: fetch per-context state from DomAdapterState
extern GfxStateHandle* dom_gfx_state(JSContext* ctx);

static inline GfxStateHandle* gfx_state_from(JSContext* ctx)
{
   return dom_gfx_state(ctx);
}

static JSValue js_gfx_create(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
   if (argc < 2)
      return JS_NewInt32(ctx, -1);
   int32_t w = 0, h = 0;
   JS_ToInt32(ctx, &w, argv[0]);
   JS_ToInt32(ctx, &h, argv[1]);
   int id = gfx_create_canvas(gfx_state_from(ctx), w, h);
   return JS_NewInt32(ctx, id);
}

static JSValue js_gfx_fill_rect(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
   if (argc < 6)
      return JS_UNDEFINED;
   int32_t id = 0, x = 0, y = 0, w = 0, h = 0;
   int64_t color = 0;
   JS_ToInt32(ctx, &id, argv[0]);
   JS_ToInt32(ctx, &x, argv[1]);
   JS_ToInt32(ctx, &y, argv[2]);
   JS_ToInt32(ctx, &w, argv[3]);
   JS_ToInt32(ctx, &h, argv[4]);
   JS_ToInt64(ctx, &color, argv[5]);
   gfx_fill_rect(gfx_state_from(ctx), id, x, y, w, h, (uint32_t)color);
   return JS_UNDEFINED;
}

static JSValue js_gfx_fill_circle(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
   if (argc < 4)
      return JS_UNDEFINED;
   int32_t id = 0, cx = 0, cy = 0, r = 0;
   int64_t color = 0;
   JS_ToInt32(ctx, &id, argv[0]);
   JS_ToInt32(ctx, &cx, argv[1]);
   JS_ToInt32(ctx, &cy, argv[2]);
   JS_ToInt32(ctx, &r, argv[3]);
   if (argc >= 5)
      JS_ToInt64(ctx, &color, argv[4]);
   else
      color = 0xFFFFFFFF;
   gfx_fill_circle(gfx_state_from(ctx), id, cx, cy, r, (uint32_t)color);
   return JS_UNDEFINED;
}

static JSValue js_gfx_snapshot(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv)
{
   if (argc < 1)
      return JS_UNDEFINED;
   int32_t id = 0;
   JS_ToInt32(ctx, &id, argv[0]);
   auto img = gfx_snapshot(gfx_state_from(ctx), id);
   if (!img)
      return JS_NULL;
   return JS_NewInt32(ctx, id);
}

void gfx_install_js(JSContext* ctx)
{
   JSValue global = JS_GetGlobalObject(ctx);
   JS_SetPropertyStr(ctx, global, "gfxCreateCanvas", JS_NewCFunction(ctx, js_gfx_create, "gfxCreateCanvas", 2));
   JS_SetPropertyStr(ctx, global, "gfxFillRect", JS_NewCFunction(ctx, js_gfx_fill_rect, "gfxFillRect", 6));
   JS_SetPropertyStr(ctx, global, "gfxFillCircle", JS_NewCFunction(ctx, js_gfx_fill_circle, "gfxFillCircle", 5));
   JS_SetPropertyStr(ctx, global, "gfxSnapshot", JS_NewCFunction(ctx, js_gfx_snapshot, "gfxSnapshot", 1));
   JS_FreeValue(ctx, global);
}
