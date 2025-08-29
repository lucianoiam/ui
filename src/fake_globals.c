#include "fake_globals.h"
#include <stdio.h>
#include <quickjs.h>
#include <stdbool.h>

static JSValue fake_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    for (int i = 0; i < argc; i++) {
        const char *s = JS_ToCString(ctx, argv[i]);
        printf("%s", s ? s : "(invalid)");
        JS_FreeCString(ctx, s);
        if (i + 1 < argc) printf(" ");
    }
    printf("\n");
    return JS_UNDEFINED;
}

static JSValue fake_requestAnimationFrame(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
        JSValue result = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
        if (JS_IsException(result)) JS_FreeValue(ctx, result);
        else JS_FreeValue(ctx, result);
    }
    return JS_NewInt32(ctx, 0);
}
static JSValue fake_setTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
        JSValue result = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
        if (JS_IsException(result)) JS_FreeValue(ctx, result);
        else JS_FreeValue(ctx, result);
    }
    return JS_NewInt32(ctx, 0);
}
static JSValue fake_clearTimeout(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}
static JSValue fake_cancelAnimationFrame(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}
static JSValue fake_setInterval(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
        JSValue result = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
        if (JS_IsException(result)) JS_FreeValue(ctx, result);
        else JS_FreeValue(ctx, result);
    }
    return JS_NewInt32(ctx, 0);
}
static JSValue fake_clearInterval(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}

void define_fake_globals(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    // console
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, fake_console_log, "log", 1));
    JS_SetPropertyStr(ctx, global, "console", console);
    // timers/animation
    JS_SetPropertyStr(ctx, global, "requestAnimationFrame", JS_NewCFunction(ctx, fake_requestAnimationFrame, "requestAnimationFrame", 1));
    JS_SetPropertyStr(ctx, global, "setTimeout", JS_NewCFunction(ctx, fake_setTimeout, "setTimeout", 1));
    JS_SetPropertyStr(ctx, global, "clearTimeout", JS_NewCFunction(ctx, fake_clearTimeout, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, global, "cancelAnimationFrame", JS_NewCFunction(ctx, fake_cancelAnimationFrame, "cancelAnimationFrame", 1));
    JS_SetPropertyStr(ctx, global, "setInterval", JS_NewCFunction(ctx, fake_setInterval, "setInterval", 1));
    JS_SetPropertyStr(ctx, global, "clearInterval", JS_NewCFunction(ctx, fake_clearInterval, "clearInterval", 1));
    JS_FreeValue(ctx, global);
}
