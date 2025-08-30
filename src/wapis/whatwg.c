#include "whatwg.h"
#include <quickjs.h>
#include <string.h>
#include <stdlib.h>

// Minimal setTimeout polyfill: calls callback immediately
static JSValue js_setTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
        JSValue ret = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
        JS_FreeValue(ctx, ret);
    }
    // Return a dummy timer id
    return JS_NewInt32(ctx, 1);
}

// Minimal clearTimeout polyfill: no-op
static JSValue js_clearTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

// Define minimal WHATWG/Window globals for JS context
void define_whatwg_globals(JSContext *ctx) {
    JSValue global_obj = JS_GetGlobalObject(ctx);
    // setTimeout
    JS_SetPropertyStr(ctx, global_obj, "setTimeout",
        JS_NewCFunction(ctx, js_setTimeout, "setTimeout", 2));
    // clearTimeout
    JS_SetPropertyStr(ctx, global_obj, "clearTimeout",
        JS_NewCFunction(ctx, js_clearTimeout, "clearTimeout", 1));
    // window alias
    JS_SetPropertyStr(ctx, global_obj, "window", JS_DupValue(ctx, global_obj));
    JS_FreeValue(ctx, global_obj);
}
