#include <quickjs.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>
#include "fake_dom.h"
#include "fake_host.h"


// Uncomment to enable each test
//#define ENABLE_TEST_1
#define ENABLE_TEST_2

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
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);
    gettimeofday(&end, NULL);
    return (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;
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

    // Provide setTimeout and clearTimeout as immediate/no-op for Preact Hooks compatibility
    JS_SetPropertyStr(ctx, global, "setTimeout", JS_NewCFunction(ctx, [](JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
            JSValue result = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
            if (JS_IsException(result)) dump_exception(ctx);
            JS_FreeValue(ctx, result);
        }
        return JS_NewInt32(ctx, 0); // fake timer id
    }, "setTimeout", 1));
    JS_SetPropertyStr(ctx, global, "clearTimeout", JS_NewCFunction(ctx, [](JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        return JS_UNDEFINED;
    }, "clearTimeout", 1));
    JS_SetPropertyStr(ctx, global, "cancelAnimationFrame", JS_NewCFunction(ctx, [](JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        return JS_UNDEFINED;
    }, "cancelAnimationFrame", 1));

    // Provide setInterval and clearInterval as immediate/no-op for compatibility
    JS_SetPropertyStr(ctx, global, "setInterval", JS_NewCFunction(ctx, [](JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
            JSValue result = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
            if (JS_IsException(result)) dump_exception(ctx);
            JS_FreeValue(ctx, result);
        }
        return JS_NewInt32(ctx, 0); // fake timer id
    }, "setInterval", 1));
    JS_SetPropertyStr(ctx, global, "clearInterval", JS_NewCFunction(ctx, [](JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        return JS_UNDEFINED;
    }, "clearInterval", 1));
    size_t preact_js_len = 0;
    char *preact_js = load_file("src/preact.js", &preact_js_len);
    if (!preact_js) {
        fprintf(stderr, "Failed to load src/preact.js\n");
        return 1;
    }
    JSValue r = JS_Eval(ctx, preact_js, preact_js_len, "src/preact.js", JS_EVAL_TYPE_GLOBAL);
    free(preact_js);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);
    // Load preact_hooks.js (UMD) after preact.js
    size_t hooks_js_len = 0;
    char *hooks_js = load_file("src/preact_hooks.js", &hooks_js_len);
    if (!hooks_js) {
        fprintf(stderr, "Failed to load src/preact_hooks.js\n");
        return 1;
    }
    r = JS_Eval(ctx, hooks_js, hooks_js_len, "src/preact_hooks.js", JS_EVAL_TYPE_GLOBAL);
    free(hooks_js);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);

    // Assign preactHooks to preact.hooks if defined
    const char *assign_hooks = "if (typeof preactHooks !== 'undefined') preact.hooks = preactHooks;";
    r = JS_Eval(ctx, assign_hooks, strlen(assign_hooks), "<assign_hooks>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);

    double elapsed1 = -1.0, elapsed2 = -1.0;
#ifdef ENABLE_TEST_1
    elapsed1 = run_test(ctx, "src/test_app_1.js");
#endif
#ifdef ENABLE_TEST_2
    elapsed2 = run_test(ctx, "src/test_app_2.js");
#endif

    // Print DOM after both tests (optional, can be commented out)
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

    JS_SetPropertyStr(ctx, global, "document", JS_UNDEFINED);
    JS_SetPropertyStr(ctx, document, "body", JS_UNDEFINED);
    JS_FreeValue(ctx, body);
    JS_FreeValue(ctx, document);
    JS_FreeValue(ctx, global);
    for (int i = 0; i < 3; ++i) JS_RunGC(rt);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    fflush(stdout);
    #ifdef ENABLE_TEST_1
    if (elapsed1 >= 0)
        printf("[BENCHMARK] Preact app + DOM brute force test: %.1f ms\n", elapsed1);
    #endif
    #ifdef ENABLE_TEST_2
    if (elapsed2 >= 0)
        printf("[BENCHMARK] Preact app + DOM complexity test: %.1f ms\n", elapsed2);
    #endif
    return 0;
}