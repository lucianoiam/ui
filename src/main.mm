#include <quickjs.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include <stdbool.h>
#include <sys/stat.h>
#include <sys/types.h>
#include "wapis/dom_adapter.h"
#include "wapis/whatwg.h"
#include <stdlib.h>
#include <unistd.h>
// Lexbor for pretty HTML formatting
#include <lexbor/html/html.h>
#include <string>
#include <utility>

// Reusable pretty HTML formatter using Lexbor. Accepts raw outerHTML of <body> subtree.
static std::string pretty_format_body_html(const char* body_outer_html) {
    if (!body_outer_html) return {};
    std::string wrapped = std::string("<html>") + body_outer_html + "</html>";
    lxb_html_document_t* lxb_doc = lxb_html_document_create();
    if (!lxb_doc) return body_outer_html; // fallback
    lxb_status_t st = lxb_html_document_parse(lxb_doc, (const lxb_char_t*)wrapped.c_str(), wrapped.size());
    if (st != LXB_STATUS_OK) {
        lxb_html_document_destroy(lxb_doc);
        return body_outer_html; // fallback
    }
    lxb_dom_node_t* body_node = (lxb_dom_node_t*) lxb_html_document_body_element(lxb_doc);
    std::string pretty;
    if (body_node) {
        struct Buf { std::string s; }; Buf buf;
        auto cb = [](const lxb_char_t* data, size_t len, void* ctx)->lxb_status_t {
            ((Buf*)ctx)->s.append((const char*)data, len); return LXB_STATUS_OK; };
        if (lxb_html_serialize_pretty_tree_cb(body_node, LXB_HTML_SERIALIZE_OPT_UNDEF, 0, cb, &buf) == LXB_STATUS_OK) {
            pretty = std::move(buf.s);
        }
    }
    lxb_html_document_destroy(lxb_doc);
    if (pretty.empty()) return body_outer_html; // fallback
    return pretty;
}

static void diagnostic_atexit() {
    fprintf(stderr, "[DIAG] atexit handler running (normal process teardown)\n");
    fflush(stderr);
}

struct DiagnosticDtor {
    ~DiagnosticDtor() {
        fprintf(stderr, "[DIAG] static destructor executed\n");
    }
} g_diagnostic_dtor;


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


TestResult run_preact_test(const char *test_js, const char *output_html, const char *preact_js_path, const char *hooks_js_path, const char *test_label, const char *benchmark_label) {

    TestResult result = {0};
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);
    int error = 0;
    size_t preact_js_len = 0, hooks_js_len = 0;
    char *preact_js = NULL, *hooks_js = NULL;
    JSValue r = JS_UNDEFINED, html_val = JS_UNDEFINED;
    const char *html = NULL;
    JSValue global = JS_UNDEFINED, document = JS_UNDEFINED, body = JS_UNDEFINED;
    const char *assign_hooks = "if (typeof preactHooks !== 'undefined') preact.hooks = preactHooks;";

    define_whatwg_globals(ctx);
    dom_define_node_proto(ctx);
    global = JS_GetGlobalObject(ctx);
    document = dom_create_document(ctx);
    JS_SetPropertyStr(ctx, global, "document", JS_DupValue(ctx, document));
    // Expose factory methods via adapter helper (internal js_create* now static)
    dom_attach_document_factories(ctx, document);
    body = JS_GetPropertyStr(ctx, document, "body");
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

    // Run test (benchmark only the app/test execution)
    result.elapsed = run_test(ctx, test_js);

    // Ensure output directory exists
    mkdir("output", 0777);

    // Serialize via body.outerHTML (standard property on Element)
    html_val = JS_GetPropertyStr(ctx, body, "outerHTML");
    if (!JS_IsException(html_val)) {
        html = JS_ToCString(ctx, html_val);
        if (html) {
            std::string pretty = pretty_format_body_html(html);
            const char* final_html = pretty.c_str();
            printf("%s\n%s\n", test_label, final_html);
            FILE *f = fopen(output_html, "w");
            if (f) { fputs(final_html, f); fclose(f); result.success = 1; }
            else { fprintf(stderr, "Failed to open %s for writing\n", output_html); }
            JS_FreeCString(ctx, html);
        }
    } else {
        dump_exception(ctx);
    }
    JS_FreeValue(ctx, html_val);

cleanup:
    fprintf(stderr, "[DEBUG] Cleanup: start\n");
    // Break JS reachability first so that freeing our duplicate wrapper refs
    // actually lets finalizers run during dom_runtime_cleanup.
    JS_SetPropertyStr(ctx, global, "document", JS_UNDEFINED);
    if (!JS_IsUndefined(document)) JS_SetPropertyStr(ctx, document, "body", JS_UNDEFINED);
    // Drop local references (they are dup'd when stored on objects)
    if (!JS_IsUndefined(body)) JS_FreeValue(ctx, body);
    if (!JS_IsUndefined(document)) JS_FreeValue(ctx, document);
    if (!JS_IsUndefined(global)) JS_FreeValue(ctx, global);
    // Now run DOM runtime cleanup which frees duplicate refs kept for identity
    const char* disableCleanup = getenv("DOM_DISABLE_CLEANUP");
    if (!disableCleanup || !*disableCleanup) {
        // Run an extra GC before wrapper free to reduce objects finalizing after context destruction
        JS_RunGC(rt);
        dom_runtime_cleanup(ctx);
        JS_RunGC(rt);
    } else {
        fprintf(stderr, "[MAIN] DOM cleanup skipped due to DOM_DISABLE_CLEANUP env var\n");
    }
    for (int i = 0; i < 3; ++i) JS_RunGC(rt);
    fprintf(stderr, "[DEBUG] Freeing JSContext and JSRuntime\n");
    JS_FreeContext(ctx);
    // Fence: allocate & free small block to catch use-after-free earlier
    { volatile char *p = (char*)malloc(16); if (p) { p[0]=0; free((void*)p);} }
    // Clear adapter registration prior to freeing runtime (defensive)
    dom_adapter_unregister_runtime(rt);
    JS_FreeRuntime(rt);
    // Marker file for post-run analysis (best-effort)
    FILE* mf = fopen("output/last_run_ok.marker", "w"); if (mf) { fputs("ok", mf); fclose(mf);} 
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
    atexit(diagnostic_atexit);
    // Enable core dumps for segfault debugging
    #include <sys/resource.h>
    // No external serializer needed (using body.outerHTML)
    const char* run_only = getenv("RUN_ONLY");
    int which = run_only ? atoi(run_only) : 0; // 0 = both
    const char* stress = getenv("DOM_STRESS_LOOPS");
    int stress_loops = stress ? atoi(stress) : 0;
    if (stress_loops > 0) {
        fprintf(stderr, "[DIAG] Stress mode: %d loops (RUN_ONLY=%d)\n", stress_loops, which);
    }
    auto run_test_suite = [&](int testId){
        if (testId == 1) {
            #ifdef ENABLE_TEST_1
            run_preact_test(
                "src/tests/bruteforce.js",
                "output/bruteforce.html",
                "build/preact.js",
                "build/preact_hooks.js",
                "[TEST 1 OUTPUT]",
                "[BENCHMARK] Preact app + DOM brute force test"
            );
            #endif
        } else if (testId == 2) {
            #ifdef ENABLE_TEST_2
            run_preact_test(
                "src/tests/complex.js",
                "output/complex.html",
                "build/preact.js",
                "build/preact_hooks.js",
                "[TEST 2 OUTPUT]",
                "[BENCHMARK] Preact app + DOM complexity test"
            );
            #endif
        }
    };

    if (stress_loops > 0) {
        int target = which == 2 ? 2 : 1; // default to test1 if 0 or 1
        for (int i=0;i<stress_loops;i++) {
            fprintf(stderr, "[DIAG] Stress iteration %d/%d start\n", i+1, stress_loops);
            run_test_suite(target);
            fprintf(stderr, "[DIAG] Stress iteration %d/%d end\n", i+1, stress_loops);
        }
    } else if (which == 0 || which == 1) {
        #ifdef ENABLE_TEST_1
        run_preact_test(
            "src/tests/bruteforce.js",
            "output/bruteforce.html",
            "build/preact.js",
            "build/preact_hooks.js",
            "[TEST 1 OUTPUT]",
            "[BENCHMARK] Preact app + DOM brute force test"
        );
        #endif
    }
    if (stress_loops == 0 && (which == 0 || which == 2)) {
        #ifdef ENABLE_TEST_2
        run_preact_test(
            "src/tests/complex.js",
            "output/complex.html",
            "build/preact.js",
            "build/preact_hooks.js",
            "[TEST 2 OUTPUT]",
            "[BENCHMARK] Preact app + DOM complexity test"
        );
        #endif
    }
    // No serializer buffer to free
    const char* exit_mode = getenv("DOM_EXIT_MODE");
    if (exit_mode) {
        if (!strcmp(exit_mode, "fast")) {
            fprintf(stderr, "[DIAG] Fast _exit(0) requested\n");
            _exit(0);
        } else if (!strcmp(exit_mode, "abort")) {
            fprintf(stderr, "[DIAG] abort() requested\n");
            abort();
        }
    }
    return 0;
}