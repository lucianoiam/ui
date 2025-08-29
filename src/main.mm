#include <quickjs.h>
#include <stdio.h>
#include <string.h>
#include <sys/time.h>
#include "dom_fake.h"

// Forward declaration for Preact source (assumed defined elsewhere)
extern const char preact_js[];
extern const unsigned int preact_js_len;

// Utility to get array length
static int JS_Length(JSContext *ctx, JSValueConst arr) {
    JSValue len_val = JS_GetPropertyStr(ctx, arr, "length");
    int len = 0;
    JS_ToInt32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);
    return len;
}

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

// Console.log implementation
static JSValue js_console_log(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    for (int i = 0; i < argc; i++) {
        const char *s = JS_ToCString(ctx, argv[i]);
        printf("%s", s ? s : "(invalid)");
        JS_FreeCString(ctx, s);
        if (i + 1 < argc) printf(" ");
    }
    printf("\n");
    return JS_UNDEFINED;
}

static void define_console(JSContext *ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_console_log, "log", 1));
    JS_SetPropertyStr(ctx, global, "console", console);
    JS_FreeValue(ctx, global);
}

int main() {
    struct timeval start, end;
    double elapsed;

    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);

    define_console(ctx);
    dom_define_node_proto(ctx);

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue document = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, global, "document", document);

    JSValue body = dom_make_node(ctx, "BODY", 1, document);
    JS_SetPropertyStr(ctx, document, "body", body);

    JS_SetPropertyStr(ctx, document, "createElement", JS_NewCFunction(ctx, [](JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        const char *tag = JS_ToCString(ctx, argv[0]);
        JSValue el = dom_make_node(ctx, tag, 1, this_val);
        JS_FreeCString(ctx, tag);
        return el;
    }, "createElement", 1));

    JS_SetPropertyStr(ctx, document, "createElementNS", JS_NewCFunction(ctx, [](JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        const char *tag = JS_ToCString(ctx, argv[1]);
        JSValue el = dom_make_node(ctx, tag, 1, this_val);
        JS_FreeCString(ctx, tag);
        return el;
    }, "createElementNS", 2));

    JS_SetPropertyStr(ctx, document, "createTextNode", JS_NewCFunction(ctx, [](JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        const char *txt = JS_ToCString(ctx, argv[0]);
        JSValue t = dom_make_node(ctx, "#text", 3, this_val);
        JS_SetPropertyStr(ctx, t, "_nodeValue", JS_NewString(ctx, txt));
        JS_FreeCString(ctx, txt);
        return t;
    }, "createTextNode", 1));

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

    // Render a more complex Preact app to stress test the fake DOM
    gettimeofday(&start, NULL);
    const char *script = R"JS(
const { h, render } = preact;

function ListItem({ value }) {
    return h('li', null, [
        h('span', { style: 'color: green;' }, 'Item: '),
        h('b', null, value)
    ]);
}

function List({ count }) {
    let items = [];
    for (let i = 0; i < count; ++i) {
        items.push(h(ListItem, { value: 'Value ' + i }));
    }
    return h('ul', { class: 'big-list' }, items);
}

function Nested({ depth }) {
    if (depth <= 0) return h('span', null, 'Leaf');
    return h('div', { class: 'nested' }, [
        h('span', null, 'Depth: ' + depth),
        h(Nested, { depth: depth - 1 })
    ]);
}

function App() {
    return h('div', { class: 'container' }, [
        h('h1', null, 'DOM Stress Test'),
        h('p', { style: 'color: blue; font-weight: bold;' }, 'Rendering 500 list items and 10 levels of nesting'),
        h(List, { count: 500 }),
        h(Nested, { depth: 10 })
    ]);
}

render(h(App), document.body);
)JS";
    r = JS_Eval(ctx, script, strlen(script), "<app>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);
    gettimeofday(&end, NULL);
    elapsed = (end.tv_sec - start.tv_sec) * 1000.0 + (end.tv_usec - start.tv_usec) / 1000.0;

    // Serialize DOM tree
    const char *print_dom = R"JS(
function printNode(node, indent = '') {
    let s = '';
    if (node.nodeType === 1) {
        let style = node.style && node.style.cssText ? node.style.cssText : '';
        let styleAttr = style.length > 0 ? " style=\"" + style + "\"" : "";
        let className = node.class && node.class.length > 0 ? " class=\"" + node.class + "\"" : "";
        s += indent + '<' + node._nodeName.toLowerCase() + className + styleAttr + '>' + '\n';
        for (let child of node.childNodes) {
            s += printNode(child, indent + '  ');
        }
        s += indent + '</' + node._nodeName.toLowerCase() + '>' + '\n';
    } else if (node.nodeType === 3) {
        s += indent + node.nodeValue + '\n';
    }
    return s;
}
console.log(printNode(document.body));
)JS";
    r = JS_Eval(ctx, print_dom, strlen(print_dom), "<print_dom>", JS_EVAL_TYPE_GLOBAL);
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
    printf("[BENCHMARK] Preact app + DOM stress test: %.1f ms\n", elapsed);
    return 0;
}