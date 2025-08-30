#include "render.h"
#include <quickjs.h>
#include <include/core/SkPaint.h>
#include <include/core/SkRect.h>
#include <string.h>


// C-callable wrapper for C code
extern "C" void gfx_render_dom_qjs(JSContext *ctx, JSValue node, void *canvas, float x, float y, float w, float h);
extern "C" void gfx_render_dom_qjs_c(JSContext *ctx, JSValue node, void *canvas, float x, float y, float w, float h);

#ifdef __cplusplus
#include <include/core/SkCanvas.h>
#include <include/core/SkColor.h>

// Helper: get string property from JSValue
static int get_js_string(JSContext *ctx, JSValue obj, const char *prop, char *out, size_t outlen) {
    JSValue val = JS_GetPropertyStr(ctx, obj, prop);
    if (JS_IsString(val)) {
        const char *str = JS_ToCString(ctx, val);
        if (str) {
            strncpy(out, str, outlen-1);
            out[outlen-1] = 0;
            JS_FreeCString(ctx, str);
            JS_FreeValue(ctx, val);
            return 1;
        }
    }
    JS_FreeValue(ctx, val);
    return 0;
}

static int get_js_style(JSContext *ctx, JSValue obj, char *out, size_t outlen) {
    JSValue style = JS_GetPropertyStr(ctx, obj, "style");
    if (JS_IsObject(style)) {
        JSValue css = JS_GetPropertyStr(ctx, style, "cssText");
        if (JS_IsString(css)) {
            const char *str = JS_ToCString(ctx, css);
            if (str) {
                strncpy(out, str, outlen-1);
                out[outlen-1] = 0;
                JS_FreeCString(ctx, str);
                JS_FreeValue(ctx, css);
                JS_FreeValue(ctx, style);
                return 1;
            }
        }
        JS_FreeValue(ctx, css);
    }
    JS_FreeValue(ctx, style);
    return 0;
}

static SkColor parse_bg_color(const char *css) {
    if (!css) return SK_ColorLTGRAY;
    const char *bg = strstr(css, "background-color:");
    if (bg) {
        bg += strlen("background-color:");
        if (*bg == '#') {
            unsigned r=0,g=0,b=0;
            if (sscanf(bg+1, "%02x%02x%02x", &r, &g, &b) == 3) return SkColorSetRGB(r,g,b);
        }
    }
    return SK_ColorLTGRAY;
}

// Actual C++ implementation
static void gfx_render_dom_qjs_impl(JSContext *ctx, JSValue node, SkCanvas *canvas, float x, float y, float w, float h) {
    char nodeName[32] = {0};
    get_js_string(ctx, node, "_nodeName", nodeName, sizeof(nodeName));
    char css[128] = {0};
    get_js_style(ctx, node, css, sizeof(css));
    SkColor color = parse_bg_color(css);
    if (strcmp(nodeName, "DIV") == 0) {
        SkPaint paint;
        paint.setColor(color);
        paint.setStyle(SkPaint::kFill_Style);
        canvas->drawRect(SkRect::MakeXYWH(x, y, w, h), paint);
    }
    JSValue arr = JS_GetPropertyStr(ctx, node, "_childNodes");
    int len = 0;
    if (JS_IsArray(arr)) {
        JSValue len_val = JS_GetPropertyStr(ctx, arr, "length");
        JS_ToInt32(ctx, &len, len_val);
        JS_FreeValue(ctx, len_val);
        for (int i = 0; i < len; ++i) {
            JSValue child = JS_GetPropertyUint32(ctx, arr, i);
            gfx_render_dom_qjs_impl(ctx, child, canvas, x+10, y+10+i*40, w-20, 30);
            JS_FreeValue(ctx, child);
        }
    }
    JS_FreeValue(ctx, arr);
}

extern "C" void gfx_render_dom_qjs_c(JSContext *ctx, JSValue node, void *canvas, float x, float y, float w, float h) {
    gfx_render_dom_qjs_impl(ctx, node, static_cast<SkCanvas*>(canvas), x, y, w, h);
}

extern "C" void gfx_render_dom_qjs(JSContext *ctx, JSValue node, void *canvas, float x, float y, float w, float h) {
    gfx_render_dom_qjs_impl(ctx, node, static_cast<SkCanvas*>(canvas), x, y, w, h);
}
#endif
