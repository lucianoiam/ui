#include "whatwg.h"
#include <quickjs.h>
#include <string.h>
#include <stdlib.h>

// Extremely naive timer polyfills (immediate execution). No persistence.
static int next_timer_id = 1;
static JSValue js_setTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
        JSValue ret = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
        JS_FreeValue(ctx, ret);
    }
    return JS_NewInt32(ctx, next_timer_id++);
}
static JSValue js_clearTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) { return JS_UNDEFINED; }
static JSValue js_setInterval(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    // Run once immediately, return id (no repeat scheduling implemented)
    if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
        JSValue ret = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
        JS_FreeValue(ctx, ret);
    }
    return JS_NewInt32(ctx, next_timer_id++);
}
static JSValue js_clearInterval(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) { return JS_UNDEFINED; }
static JSValue js_requestAnimationFrame(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
        JSValue ret = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
        JS_FreeValue(ctx, ret);
    }
    return JS_NewInt32(ctx, next_timer_id++);
}
static JSValue js_cancelAnimationFrame(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) { return JS_UNDEFINED; }

// Define minimal WHATWG/Window globals for JS context
void define_whatwg_globals(JSContext *ctx) {
    JSValue global_obj = JS_GetGlobalObject(ctx);
    // Timers
    JS_SetPropertyStr(ctx, global_obj, "setTimeout", JS_NewCFunction(ctx, js_setTimeout, "setTimeout", 2));
    JS_SetPropertyStr(ctx, global_obj, "clearTimeout", JS_NewCFunction(ctx, js_clearTimeout, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, global_obj, "setInterval", JS_NewCFunction(ctx, js_setInterval, "setInterval", 2));
    JS_SetPropertyStr(ctx, global_obj, "clearInterval", JS_NewCFunction(ctx, js_clearInterval, "clearInterval", 1));
    JS_SetPropertyStr(ctx, global_obj, "requestAnimationFrame", JS_NewCFunction(ctx, js_requestAnimationFrame, "requestAnimationFrame", 1));
    JS_SetPropertyStr(ctx, global_obj, "cancelAnimationFrame", JS_NewCFunction(ctx, js_cancelAnimationFrame, "cancelAnimationFrame", 1));
    // window alias
    JS_SetPropertyStr(ctx, global_obj, "window", JS_DupValue(ctx, global_obj));
    JS_FreeValue(ctx, global_obj);
}
