#include "sk_canvas_view.h"
#include <include/core/SkColor.h>
#include <include/core/SkPaint.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkSurface.h>
#include <mutex>

namespace {
static std::mutex g_mutex;
static int g_next_id = 1;
static std::unordered_map<int, SkCanvasView> g_views;

SkCanvas* canvas_for(int id) {
    auto it = g_views.find(id);
    if (it == g_views.end()) return nullptr;
    return it->second.surface ? it->second.surface->getCanvas() : nullptr;
} // anonymous
}

int gfx_create_canvas(int width, int height) {
    if (width <= 0 || height <= 0) return -1;
    std::lock_guard<std::mutex> lock(g_mutex);
    SkImageInfo info = SkImageInfo::Make(width, height, kN32_SkColorType, kPremul_SkAlphaType);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) return -1;
    int id = g_next_id++;
    g_views[id] = {id, width, height, surface};
    return id;
}

void gfx_fill_rect(int id, int x, int y, int w, int h, uint32_t rgba) {
    std::lock_guard<std::mutex> lock(g_mutex);
    SkCanvas* c = canvas_for(id);
    if (!c) return;
    SkPaint p; p.setStyle(SkPaint::kFill_Style);
    uint8_t r = (rgba >> 24) & 0xFF;
    uint8_t g = (rgba >> 16) & 0xFF;
    uint8_t b = (rgba >> 8) & 0xFF;
    uint8_t a = (rgba) & 0xFF;
    p.setColor(SkColorSetARGB(a, r, g, b));
    c->drawRect(SkRect::MakeXYWH(x, y, w, h), p);
}

void gfx_fill_circle(int id, int cx, int cy, int radius, uint32_t rgba) {
    std::lock_guard<std::mutex> lock(g_mutex);
    SkCanvas* c = canvas_for(id);
    if (!c) return;
    SkPaint p; p.setStyle(SkPaint::kFill_Style);
    uint8_t r = (rgba >> 24) & 0xFF;
    uint8_t g = (rgba >> 16) & 0xFF;
    uint8_t b = (rgba >> 8) & 0xFF;
    uint8_t a = (rgba) & 0xFF;
    p.setAntiAlias(true);
    p.setColor(SkColorSetARGB(a, r, g, b));
    c->drawCircle((SkScalar)cx, (SkScalar)cy, (SkScalar)radius, p);
}

sk_sp<SkImage> gfx_snapshot(int id) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_views.find(id);
    if (it == g_views.end()) return nullptr;
    return it->second.surface ? it->second.surface->makeImageSnapshot() : nullptr;
}

bool gfx_get_size(int id, int* outW, int* outH) {
    std::lock_guard<std::mutex> lock(g_mutex);
    auto it = g_views.find(id);
    if (it == g_views.end()) return false;
    if (outW) *outW = it->second.width;
    if (outH) *outH = it->second.height;
    return true;
}

// ---- QuickJS bindings ----
static JSValue js_gfx_create(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_NewInt32(ctx, -1);
    int32_t w=0,h=0; JS_ToInt32(ctx, &w, argv[0]); JS_ToInt32(ctx, &h, argv[1]);
    int id = gfx_create_canvas(w,h);
    return JS_NewInt32(ctx, id);
}
static JSValue js_gfx_fill_rect(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 6) return JS_UNDEFINED;
    int32_t id=0,x=0,y=0,w=0,h=0; int64_t color=0;
    JS_ToInt32(ctx, &id, argv[0]); JS_ToInt32(ctx, &x, argv[1]); JS_ToInt32(ctx, &y, argv[2]);
    JS_ToInt32(ctx, &w, argv[3]); JS_ToInt32(ctx, &h, argv[4]); JS_ToInt64(ctx, &color, argv[5]);
    gfx_fill_rect(id,x,y,w,h,(uint32_t)color);
    return JS_UNDEFINED;
}
static JSValue js_gfx_fill_circle(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 4) return JS_UNDEFINED;
    int32_t id=0,cx=0,cy=0,r=0; int64_t color=0;
    JS_ToInt32(ctx, &id, argv[0]); JS_ToInt32(ctx, &cx, argv[1]); JS_ToInt32(ctx, &cy, argv[2]); JS_ToInt32(ctx, &r, argv[3]);
    if (argc >=5) JS_ToInt64(ctx, &color, argv[4]); else color = 0xFFFFFFFF;
    gfx_fill_circle(id,cx,cy,r,(uint32_t)color);
    return JS_UNDEFINED;
}
static JSValue js_gfx_snapshot(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;
    int32_t id=0; JS_ToInt32(ctx, &id, argv[0]);
    auto img = gfx_snapshot(id);
    if (!img) return JS_NULL;
    // For now just return a JS number as a placeholder (could be pointer/int handle for future blitting)
    return JS_NewInt32(ctx, id);
}

void gfx_install_js(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JS_SetPropertyStr(ctx, global, "gfxCreateCanvas", JS_NewCFunction(ctx, js_gfx_create, "gfxCreateCanvas", 2));
    JS_SetPropertyStr(ctx, global, "gfxFillRect", JS_NewCFunction(ctx, js_gfx_fill_rect, "gfxFillRect", 6));
    JS_SetPropertyStr(ctx, global, "gfxFillCircle", JS_NewCFunction(ctx, js_gfx_fill_circle, "gfxFillCircle", 5));
    JS_SetPropertyStr(ctx, global, "gfxSnapshot", JS_NewCFunction(ctx, js_gfx_snapshot, "gfxSnapshot", 1));
    JS_FreeValue(ctx, global);
}
