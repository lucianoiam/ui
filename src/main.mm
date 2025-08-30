#include <quickjs.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>
#include "fake_dom.h"
#include "fake_globals.h"


// Uncomment to enable each test
#define ENABLE_TEST_1
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

int main(int argc, char **argv) {
    // Enable core dumps for segfault debugging
    #include <sys/resource.h>
    struct rlimit rl;
    if (getrlimit(RLIMIT_CORE, &rl) == 0) {
        rl.rlim_cur = rl.rlim_max;
        setrlimit(RLIMIT_CORE, &rl);
    }
    fprintf(stderr, "[INFO] Core dumps enabled. If a segfault occurs, run: lldb %s core or gdb %s core\n", argv[0], argv[0]);
    struct timeval start, end;
    double elapsed;
    JSRuntime *rt = JS_NewRuntime();
    double elapsed1 = -1.0, elapsed2 = -1.0;
    size_t serialize_dom_js_len = 0;
    char *serialize_dom_js = load_file("src/serialize_dom.js", &serialize_dom_js_len);
    if (!serialize_dom_js) {
        fprintf(stderr, "Failed to load src/serialize_dom.js\n");
        return 1;
    }

#ifdef ENABLE_TEST_1
    {
        JSRuntime *rt = JS_NewRuntime();
        JSContext *ctx = JS_NewContext(rt);
        define_fake_globals(ctx);
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
        const char *assign_hooks = "if (typeof preactHooks !== 'undefined') preact.hooks = preactHooks;";
        r = JS_Eval(ctx, assign_hooks, strlen(assign_hooks), "<assign_hooks>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(r)) dump_exception(ctx);
        JS_FreeValue(ctx, r);
        // Run test 1
        elapsed1 = run_test(ctx, "src/test_app_1.js");
        r = JS_Eval(ctx, serialize_dom_js, serialize_dom_js_len, "src/serialize_dom.js", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(r)) dump_exception(ctx);
        JS_FreeValue(ctx, r);
        JSValue serialize_fn1 = JS_GetPropertyStr(ctx, global, "serialize_dom");
        if (JS_IsFunction(ctx, serialize_fn1)) {
            JSValue html_val1 = JS_Call(ctx, serialize_fn1, global, 0, NULL);
            if (!JS_IsException(html_val1)) {
                const char *html1 = JS_ToCString(ctx, html_val1);
                if (html1) {
                    printf("[TEST 1 OUTPUT]\n%s\n", html1);
                    FILE *f1 = fopen("output/test_app_1.html", "w");
                    if (f1) { fputs(html1, f1); fclose(f1); }
                    else { fprintf(stderr, "Failed to open output/test_app_1.html for writing\n"); }
                    JS_FreeCString(ctx, html1);
                }
            } else {
                dump_exception(ctx);
            }
            JS_FreeValue(ctx, html_val1);
        } else {
            fprintf(stderr, "serialize_dom is not a function!\n");
        }
        JS_FreeValue(ctx, serialize_fn1);
    // Do not free body, document, or global; QuickJS owns them after setting as properties
        for (int i = 0; i < 3; ++i) JS_RunGC(rt);
        // Free FakeDomClassInfo and node_proto before freeing context
        FakeDomClassInfo *info = (FakeDomClassInfo *)JS_GetContextOpaque(ctx);
        if (info) {
            free(info);
            JS_SetContextOpaque(ctx, NULL);
        }
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        fflush(stdout);
        if (elapsed1 >= 0)
            printf("[BENCHMARK] Preact app + DOM brute force test: %.1f ms\n", elapsed1);
    }
#endif
#ifdef ENABLE_TEST_2
    {
        JSRuntime *rt = JS_NewRuntime();
        JSContext *ctx = JS_NewContext(rt);
        define_fake_globals(ctx);
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
        const char *assign_hooks = "if (typeof preactHooks !== 'undefined') preact.hooks = preactHooks;";
        r = JS_Eval(ctx, assign_hooks, strlen(assign_hooks), "<assign_hooks>", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(r)) dump_exception(ctx);
        JS_FreeValue(ctx, r);
        // Run test 2
        elapsed2 = run_test(ctx, "src/test_app_2.js");
        r = JS_Eval(ctx, serialize_dom_js, serialize_dom_js_len, "src/serialize_dom.js", JS_EVAL_TYPE_GLOBAL);
        if (JS_IsException(r)) dump_exception(ctx);
        JS_FreeValue(ctx, r);
        JSValue serialize_fn2 = JS_GetPropertyStr(ctx, global, "serialize_dom");
        if (JS_IsFunction(ctx, serialize_fn2)) {
            JSValue html_val2 = JS_Call(ctx, serialize_fn2, global, 0, NULL);
            if (!JS_IsException(html_val2)) {
                const char *html2 = JS_ToCString(ctx, html_val2);
                if (html2) {
                    printf("[TEST 2 OUTPUT]\n%s\n", html2);
                    FILE *f2 = fopen("output/test_app_2.html", "w");
                    if (f2) { fputs(html2, f2); fclose(f2); }
                    else { fprintf(stderr, "Failed to open output/test_app_2.html for writing\n"); }
                    JS_FreeCString(ctx, html2);
                }
            } else {
                dump_exception(ctx);
            }
            JS_FreeValue(ctx, html_val2);
        } else {
            fprintf(stderr, "serialize_dom is not a function!\n");
        }
        JS_FreeValue(ctx, serialize_fn2);
        JS_SetPropertyStr(ctx, global, "document", JS_UNDEFINED);
        JS_SetPropertyStr(ctx, document, "body", JS_UNDEFINED);
        JS_FreeValue(ctx, body);
        JS_FreeValue(ctx, document);
        JS_FreeValue(ctx, global);
        for (int i = 0; i < 3; ++i) JS_RunGC(rt);
        // Free FakeDomClassInfo and node_proto before freeing context
        FakeDomClassInfo *info = (FakeDomClassInfo *)JS_GetContextOpaque(ctx);
        if (info) {
            free(info);
            JS_SetContextOpaque(ctx, NULL);
        }
        JS_FreeContext(ctx);
        JS_FreeRuntime(rt);
        fflush(stdout);
        if (elapsed2 >= 0)
            printf("[BENCHMARK] Preact app + DOM complexity test: %.1f ms\n", elapsed2);
    }
#endif
    free(serialize_dom_js);
    return 0;
}