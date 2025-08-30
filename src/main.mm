#include <quickjs.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>
#include "wapis/dom.h"
#include "wapis/whatwg.h"


// Uncomment to enable each test

#define ENABLE_TEST_1
#define ENABLE_TEST_2


typedef struct {
    double elapsed;
    int success;
} TestResult;

// Forward declarations
static char *load_file(const char *filename, size_t *out_len);
static void dump_exception(JSContext *ctx);
double run_test(JSContext *ctx, const char *filename);


TestResult run_preact_test(const char *test_js, const char *output_html, const char *serialize_dom_js, size_t serialize_dom_js_len, const char *preact_js_path, const char *hooks_js_path, const char *test_label, const char *benchmark_label) {

    TestResult result = {0};
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    int error = 0;
    size_t preact_js_len = 0, hooks_js_len = 0;
    char *preact_js = NULL, *hooks_js = NULL;
    JSValue r = JS_UNDEFINED, serialize_fn = JS_UNDEFINED, html_val = JS_UNDEFINED;
    const char *html = NULL;
    JSValue global = JS_UNDEFINED, document = JS_UNDEFINED, body = JS_UNDEFINED;
    const char *assign_hooks = "if (typeof preactHooks !== 'undefined') preact.hooks = preactHooks;";

    define_whatwg_globals(ctx);
    dom_define_node_proto(ctx);
    global = JS_GetGlobalObject(ctx);
    document = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, global, "document", document);
    body = dom_make_node(ctx, "BODY", 1, document);
    JS_SetPropertyStr(ctx, document, "body", body);
    JS_SetPropertyStr(ctx, document, "createElement", JS_NewCFunction(ctx, js_createElement, "createElement", 1));
    JS_SetPropertyStr(ctx, document, "createElementNS", JS_NewCFunction(ctx, js_createElementNS, "createElementNS", 2));
    JS_SetPropertyStr(ctx, document, "createTextNode", JS_NewCFunction(ctx, js_createTextNode, "createTextNode", 1));
    JS_SetPropertyStr(ctx, global, "window", JS_DupValue(ctx, global));
    JS_SetPropertyStr(ctx, global, "self", JS_DupValue(ctx, global));
    JS_SetPropertyStr(ctx, global, "globalThis", JS_DupValue(ctx, global));

    preact_js = load_file(preact_js_path, &preact_js_len);
    if (!preact_js) {
        fprintf(stderr, "Failed to load %s\n", preact_js_path);
        error = 1;
        goto cleanup;
    }
    r = JS_Eval(ctx, preact_js, preact_js_len, preact_js_path, JS_EVAL_TYPE_GLOBAL);
    free(preact_js);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);

    hooks_js = load_file(hooks_js_path, &hooks_js_len);
    if (!hooks_js) {
        fprintf(stderr, "Failed to load %s\n", hooks_js_path);
        error = 1;
        goto cleanup;
    }
    r = JS_Eval(ctx, hooks_js, hooks_js_len, hooks_js_path, JS_EVAL_TYPE_GLOBAL);
    free(hooks_js);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);

    r = JS_Eval(ctx, assign_hooks, strlen(assign_hooks), "<assign_hooks>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);

    // Run test
    result.elapsed = run_test(ctx, test_js);
    r = JS_Eval(ctx, serialize_dom_js, serialize_dom_js_len, "src/tests/serialize_dom.js", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);

    serialize_fn = JS_GetPropertyStr(ctx, global, "serialize_dom");
    if (JS_IsFunction(ctx, serialize_fn)) {
        html_val = JS_Call(ctx, serialize_fn, global, 0, NULL);
        if (!JS_IsException(html_val)) {
            html = JS_ToCString(ctx, html_val);
            if (html) {
                printf("%s\n%s\n", test_label, html);
                FILE *f = fopen(output_html, "w");
                if (f) { fputs(html, f); fclose(f); result.success = 1; }
                else { fprintf(stderr, "Failed to open %s for writing\n", output_html); }
                JS_FreeCString(ctx, html);
            }
        } else {
            dump_exception(ctx);
        }
        JS_FreeValue(ctx, html_val);
    } else {
        fprintf(stderr, "serialize_dom is not a function!\n");
    }
    JS_FreeValue(ctx, serialize_fn);

cleanup:
    // Remove document/body properties to break cycles, but do not free JSValues owned by QuickJS
    fprintf(stderr, "[DEBUG] Cleanup: start\n");
    JS_SetPropertyStr(ctx, global, "document", JS_UNDEFINED);
    JS_SetPropertyStr(ctx, document, "body", JS_UNDEFINED);
    // Do NOT JS_FreeValue body, document, or global: QuickJS owns them after set as properties
    for (int i = 0; i < 3; ++i) JS_RunGC(rt);
    // Free per-context prototype
    // No per-context prototype cleanup needed for new dom implementation
    fprintf(stderr, "[DEBUG] Freeing JSContext and JSRuntime\n");
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    fflush(stdout);
    fprintf(stderr, "[DEBUG] Cleanup: end\n");
    if (!error && result.elapsed >= 0)
        printf("%s: %.1f ms\n", benchmark_label, result.elapsed);
    return result;
}

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
    size_t serialize_dom_js_len = 0;
    char *serialize_dom_js = load_file("src/tests/serialize_dom.js", &serialize_dom_js_len);
    if (!serialize_dom_js) {
        fprintf(stderr, "Failed to load src/tests/serialize_dom.js\n");
        return 1;
    }
    #ifdef ENABLE_TEST_1
    run_preact_test(
        "src/tests/bruteforce.js",
        "output/bruteforce.html",
        serialize_dom_js,
        serialize_dom_js_len,
        "build/preact.js",
        "build/preact_hooks.js",
        "[TEST 1 OUTPUT]",
        "[BENCHMARK] Preact app + DOM brute force test"
    );
    #endif
    #ifdef ENABLE_TEST_2
    run_preact_test(
        "src/tests/complex.js",
        "output/complex.html",
        serialize_dom_js,
        serialize_dom_js_len,
        "build/preact.js",
        "build/preact_hooks.js",
        "[TEST 2 OUTPUT]",
        "[BENCHMARK] Preact app + DOM complexity test"
    );
    #endif
    free(serialize_dom_js);
    return 0;
}