// Includes must come first
#include <quickjs.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "fake_dom.h"

// Static C functions for JS_NewCFunction (must be at file scope)
static JSValue js_createElement(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *tag = JS_ToCString(ctx, argv[0]);
    JSValue el = fake_dom_make_node(ctx, tag, 1, this_val);
    JS_FreeCString(ctx, tag);
    return el;
}
static JSValue js_createElementNS(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *tag = JS_ToCString(ctx, argv[1]);
    JSValue el = fake_dom_make_node(ctx, tag, 1, this_val);
    JS_FreeCString(ctx, tag);
    return el;
}
static JSValue js_createTextNode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *txt = JS_ToCString(ctx, argv[0]);
    JSValue t = fake_dom_make_node(ctx, "#text", 3, this_val);
    JS_SetPropertyStr(ctx, t, "_nodeValue", JS_NewString(ctx, txt));
    JS_FreeCString(ctx, txt);
    return t;
}

// Forward declaration for Preact source (assumed defined elsewhere)
extern const char preact_js[];
extern const unsigned int preact_js_len;

// Utility to get array length

// Utility to dump exceptions
static void dump_exception(JSContext *ctx) {
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


#include "fake_host.h"

// Helper to load a JS file from disk
static char *load_file(const char *filename, size_t *out_len) {
    FILE *f = fopen(filename, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = (char *)malloc(len + 1);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, len, f);
    buf[len] = '\0';
    fclose(f);
    if (out_len) *out_len = len;
    return buf;
}

int main() {
    struct timeval start, end;
    double elapsed;

    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);

    fake_define_console(ctx);
    fake_dom_define_node_proto(ctx);

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue document = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, global, "document", document);

    JSValue body = fake_dom_make_node(ctx, "BODY", 1, document);
    JS_SetPropertyStr(ctx, document, "body", body);



    JS_SetPropertyStr(ctx, document, "createElement", JS_NewCFunction(ctx, js_createElement, "createElement", 1));
    JS_SetPropertyStr(ctx, document, "createElementNS", JS_NewCFunction(ctx, js_createElementNS, "createElementNS", 2));
    JS_SetPropertyStr(ctx, document, "createTextNode", JS_NewCFunction(ctx, js_createTextNode, "createTextNode", 1));

    JS_SetPropertyStr(ctx, global, "window", JS_DupValue(ctx, global));
    JS_SetPropertyStr(ctx, global, "self", JS_DupValue(ctx, global));
    JS_SetPropertyStr(ctx, global, "globalThis", JS_DupValue(ctx, global));

    JS_SetPropertyStr(ctx, global, "requestAnimationFrame", JS_NewCFunction(ctx, [](JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
            JSValue result = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
            if (JS_IsException(result)) dump_exception(ctx);
            JS_FreeValue(ctx, result);
        }
        return JS_NewInt32(ctx, 0);
    }, "requestAnimationFrame", 1));

    // Evaluate Preact
    JSValue r = JS_Eval(ctx, preact_js, preact_js_len, "<preact>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);

    // Load and run app.js from disk
    size_t react_app_js_len = 0;
    char *react_app_js = load_file("src/react_app.js", &react_app_js_len);
    if (!react_app_js) {
        fprintf(stderr, "Failed to load src/react_app.js\n");
        return 1;
    }
    gettimeofday(&start, NULL);
    r = JS_Eval(ctx, react_app_js, react_app_js_len, "src/react_app.js", JS_EVAL_TYPE_GLOBAL);
    free(react_app_js);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);
    gettimeofday(&end, NULL);
    elapsed = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;

    // Serialize DOM tree using print_dom.js
    size_t print_dom_js_len = 0;
    char *print_dom_js = load_file("src/print_dom.js", &print_dom_js_len);
    if (!print_dom_js) {
        fprintf(stderr, "Failed to load src/print_dom.js\n");
        return 1;
    }
    r = JS_Eval(ctx, print_dom_js, print_dom_js_len, "src/print_dom.js", JS_EVAL_TYPE_GLOBAL);
    free(print_dom_js);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);

    // Remove document and body from global object to break references
    JS_SetPropertyStr(ctx, global, "document", JS_UNDEFINED);
    JS_SetPropertyStr(ctx, document, "body", JS_UNDEFINED);

    // Explicitly free all global JSValue references
    JS_FreeValue(ctx, body);
    JS_FreeValue(ctx, document);
    JS_FreeValue(ctx, global);

    // Run GC multiple times to ensure collection
    for (int i = 0; i < 3; ++i) {
        JS_RunGC(rt);
    }
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    fflush(stdout); // Ensure all logs are printed
    // On a Mac M4 running a typical web browser, this test would take approximately 2-3ms. Observed value here: 24ms.
    printf("[BENCHMARK] Preact app + DOM stress test: %.1f ms\n", elapsed);
    return 0;
}