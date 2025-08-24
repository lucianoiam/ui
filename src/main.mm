#include <quickjs.h>
#include <stdio.h>
#include <string.h>

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

// Node prototype and methods
static JSValue node_proto;
JSClassID dom_class_id = 0;

// Finalizer for DOM objects
static void dom_object_finalizer(JSRuntime *rt, JSValue val) {
    printf("[DOM] object destroyed\n");
}

// Remove from parent function (defined before usage)
static void remove_from_parent(JSContext *ctx, JSValue node) {
    JSValue parent = JS_GetPropertyStr(ctx, node, "_parentNode");
    if (!JS_IsNull(parent) && !JS_IsUndefined(parent)) {
        JSValue arr = JS_GetPropertyStr(ctx, parent, "_childNodes");
        int len = JS_Length(ctx, arr);
        for (int i = 0; i < len; i++) {
            JSValue child = JS_GetPropertyUint32(ctx, arr, i);
            if (JS_IsStrictEqual(ctx, child, node)) {
                for (int j = i; j < len - 1; j++) {
                    JSValue next = JS_GetPropertyUint32(ctx, arr, j + 1);
                    JS_SetPropertyUint32(ctx, arr, j, JS_DupValue(ctx, next));
                    JS_FreeValue(ctx, next);
                }
                JS_SetPropertyUint32(ctx, arr, len - 1, JS_UNDEFINED);
                JS_SetPropertyStr(ctx, node, "_parentNode", JS_NULL);
                JS_FreeValue(ctx, child);
                break;
            }
            JS_FreeValue(ctx, child);
        }
        JS_FreeValue(ctx, arr);
    }
    JS_FreeValue(ctx, parent);
}

static JSValue getter_nodeType(JSContext *ctx, JSValueConst this_val) {
    return JS_DupValue(ctx, JS_GetPropertyStr(ctx, this_val, "_nodeType"));
}

static JSValue getter_childNodes(JSContext *ctx, JSValueConst this_val) {
    return JS_DupValue(ctx, JS_GetPropertyStr(ctx, this_val, "_childNodes"));
}

static JSValue getter_firstChild(JSContext *ctx, JSValueConst this_val) {
    JSValue arr = JS_GetPropertyStr(ctx, this_val, "_childNodes");
    JSValue first = JS_GetPropertyUint32(ctx, arr, 0);
    JS_FreeValue(ctx, arr);
    return first;
}

static JSValue getter_nodeValue(JSContext *ctx, JSValueConst this_val) {
    return JS_DupValue(ctx, JS_GetPropertyStr(ctx, this_val, "_nodeValue"));
}

static JSValue setter_nodeValue(JSContext *ctx, JSValueConst this_val, JSValueConst value) {
    JS_SetPropertyStr(ctx, (JSValue)this_val, "_nodeValue", JS_DupValue(ctx, value));
    return JS_UNDEFINED;
}

static JSValue getter_parentNode(JSContext *ctx, JSValueConst this_val) {
    return JS_DupValue(ctx, JS_GetPropertyStr(ctx, this_val, "_parentNode"));
}

static JSValue getter_ownerDocument(JSContext *ctx, JSValueConst this_val) {
    return JS_DupValue(ctx, JS_GetPropertyStr(ctx, this_val, "_ownerDocument"));
}

static JSValue getter_attributes(JSContext *ctx, JSValueConst this_val) {
    return JS_DupValue(ctx, JS_GetPropertyStr(ctx, this_val, "_attributes"));
}

static JSValue fn_setAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    printf("[DOM] setAttribute called\n");
    if (argc < 2) return JS_ThrowTypeError(ctx, "setAttribute: Expected name and value");
    const char *name = JS_ToCString(ctx, argv[0]);
    JSValue attrs = JS_GetPropertyStr(ctx, this_val, "_attributes");
    JS_SetPropertyStr(ctx, attrs, name, JS_DupValue(ctx, argv[1]));
    JS_FreeCString(ctx, name);
    JS_FreeValue(ctx, attrs);
    return JS_UNDEFINED;
}

static JSValue fn_getAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    printf("[DOM] getAttribute called\n");
    if (argc < 1) return JS_ThrowTypeError(ctx, "getAttribute: Expected name");
    const char *name = JS_ToCString(ctx, argv[0]);
    JSValue attrs = JS_GetPropertyStr(ctx, this_val, "_attributes");
    JSValue value = JS_GetPropertyStr(ctx, attrs, name);
    JS_FreeCString(ctx, name);
    JS_FreeValue(ctx, attrs);
    return value;
}

static JSValue fn_appendChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    printf("[DOM] appendChild called\n");
    if (argc < 1 || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) {
        return JS_ThrowTypeError(ctx, "appendChild: Invalid node");
    }
    remove_from_parent(ctx, (JSValue)argv[0]);
    JSValue arr = JS_GetPropertyStr(ctx, this_val, "_childNodes");
    JS_SetPropertyUint32(ctx, arr, JS_Length(ctx, arr), JS_DupValue(ctx, argv[0]));
    JS_SetPropertyStr(ctx, (JSValue)argv[0], "_parentNode", JS_DupValue(ctx, this_val));
    JS_FreeValue(ctx, arr);
    return JS_DupValue(ctx, argv[0]);
}

static JSValue fn_insertBefore(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    printf("[DOM] insertBefore called\n");
    if (argc < 1 || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) {
        return JS_ThrowTypeError(ctx, "insertBefore: Invalid node");
    }
    remove_from_parent(ctx, (JSValue)argv[0]);
    JSValue arr = JS_GetPropertyStr(ctx, this_val, "_childNodes");
    int len = JS_Length(ctx, arr);
    int index = len;
    if (argc > 1 && !JS_IsNull(argv[1]) && !JS_IsUndefined(argv[1])) {
        for (int i = 0; i < len; i++) {
            JSValue child = JS_GetPropertyUint32(ctx, arr, i);
            if (JS_IsStrictEqual(ctx, child, argv[1])) {
                index = i;
                JS_FreeValue(ctx, child);
                break;
            }
            JS_FreeValue(ctx, child);
        }
    }
    for (int i = len; i > index; i--) {
        JSValue prev = JS_GetPropertyUint32(ctx, arr, i - 1);
        JS_SetPropertyUint32(ctx, arr, i, JS_DupValue(ctx, prev));
        JS_FreeValue(ctx, prev);
    }
    JS_SetPropertyUint32(ctx, arr, index, JS_DupValue(ctx, argv[0]));
    JS_SetPropertyStr(ctx, (JSValue)argv[0], "_parentNode", JS_DupValue(ctx, this_val));
    JS_FreeValue(ctx, arr);
    return JS_DupValue(ctx, argv[0]);
}

static JSValue fn_removeChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    printf("[DOM] removeChild called\n");
    if (argc < 1 || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0])) {
        return JS_ThrowTypeError(ctx, "removeChild: Invalid node");
    }
    JSValue arr = JS_GetPropertyStr(ctx, this_val, "_childNodes");
    int len = JS_Length(ctx, arr);
    for (int i = 0; i < len; i++) {
        JSValue child = JS_GetPropertyUint32(ctx, arr, i);
    if (JS_IsStrictEqual(ctx, child, argv[0])) {
            for (int j = i; j < len - 1; j++) {
                JSValue next = JS_GetPropertyUint32(ctx, arr, j + 1);
                JS_SetPropertyUint32(ctx, arr, j, JS_DupValue(ctx, next));
                JS_FreeValue(ctx, next);
            }
            JS_SetPropertyUint32(ctx, arr, len - 1, JS_UNDEFINED);
            JS_SetPropertyStr(ctx, (JSValue)argv[0], "_parentNode", JS_NULL);
            JS_FreeValue(ctx, child);
            JS_FreeValue(ctx, arr);
            return JS_DupValue(ctx, argv[0]);
        }
        JS_FreeValue(ctx, child);
    }
    JS_FreeValue(ctx, arr);
    return JS_ThrowTypeError(ctx, "removeChild: Node not found");
}

static JSValue fn_replaceChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    printf("[DOM] replaceChild called\n");
    if (argc < 2 || JS_IsNull(argv[0]) || JS_IsUndefined(argv[0]) || JS_IsNull(argv[1]) || JS_IsUndefined(argv[1])) {
        return JS_ThrowTypeError(ctx, "replaceChild: Invalid nodes");
    }
    JSValue arr = JS_GetPropertyStr(ctx, this_val, "_childNodes");
    int len = JS_Length(ctx, arr);
    for (int i = 0; i < len; i++) {
        JSValue child = JS_GetPropertyUint32(ctx, arr, i);
    if (JS_IsStrictEqual(ctx, child, argv[1])) {
            remove_from_parent(ctx, (JSValue)argv[0]);
            JS_SetPropertyUint32(ctx, arr, i, JS_DupValue(ctx, argv[0]));
            JS_SetPropertyStr(ctx, (JSValue)argv[0], "_parentNode", JS_DupValue(ctx, this_val));
            JS_SetPropertyStr(ctx, (JSValue)argv[1], "_parentNode", JS_NULL);
            JS_FreeValue(ctx, child);
            JS_FreeValue(ctx, arr);
            return JS_DupValue(ctx, argv[1]);
        }
        JS_FreeValue(ctx, child);
    }
    JS_FreeValue(ctx, arr);
    return JS_ThrowTypeError(ctx, "replaceChild: Node not found");
}

static void define_node_proto(JSContext *ctx) {
    // Define a JSClass with a finalizer for DOM nodes
    static JSClassDef dom_class = {
        "DOMNode",
        .finalizer = dom_object_finalizer
    };
    if (dom_class_id == 0) {
        JS_NewClassID(JS_GetRuntime(ctx), &dom_class_id);
        JS_NewClass(JS_GetRuntime(ctx), dom_class_id, &dom_class);
    }
    node_proto = JS_NewObject(ctx);
    JS_SetClassProto(ctx, dom_class_id, node_proto);

    auto define_prop = [&](const char *name, JSCFunctionMagic *getter, JSCFunctionMagic *setter = nullptr) {
        JSAtom atom = JS_NewAtom(ctx, name);
        JS_DefinePropertyGetSet(ctx, node_proto, atom,
                                JS_NewCFunctionMagic(ctx, getter, name, 0, JS_CFUNC_getter, 0),
                                setter ? JS_NewCFunctionMagic(ctx, setter, name, 1, JS_CFUNC_setter, 0) : JS_UNDEFINED,
                                JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
        JS_FreeAtom(ctx, atom);
    };

    define_prop("nodeType", (JSCFunctionMagic *)getter_nodeType);
    define_prop("childNodes", (JSCFunctionMagic *)getter_childNodes);
    define_prop("firstChild", (JSCFunctionMagic *)getter_firstChild);
    define_prop("nodeValue", (JSCFunctionMagic *)getter_nodeValue, (JSCFunctionMagic *)setter_nodeValue);
    define_prop("parentNode", (JSCFunctionMagic *)getter_parentNode);
    define_prop("ownerDocument", (JSCFunctionMagic *)getter_ownerDocument);
    define_prop("attributes", (JSCFunctionMagic *)getter_attributes);

    JS_SetPropertyStr(ctx, node_proto, "appendChild", JS_NewCFunction(ctx, fn_appendChild, "appendChild", 1));
    JS_SetPropertyStr(ctx, node_proto, "insertBefore", JS_NewCFunction(ctx, fn_insertBefore, "insertBefore", 2));
    JS_SetPropertyStr(ctx, node_proto, "removeChild", JS_NewCFunction(ctx, fn_removeChild, "removeChild", 1));
    JS_SetPropertyStr(ctx, node_proto, "replaceChild", JS_NewCFunction(ctx, fn_replaceChild, "replaceChild", 2));
    JS_SetPropertyStr(ctx, node_proto, "setAttribute", JS_NewCFunction(ctx, fn_setAttribute, "setAttribute", 2));
    JS_SetPropertyStr(ctx, node_proto, "getAttribute", JS_NewCFunction(ctx, fn_getAttribute, "getAttribute", 1));
}

static JSValue make_node(JSContext *ctx, const char *name, int type, JSValue ownerDoc) {
    JSValue obj = JS_NewObjectClass(ctx, dom_class_id);
    JS_SetPrototype(ctx, obj, node_proto);
    JS_SetPropertyStr(ctx, obj, "_nodeName", JS_NewString(ctx, name));
    JS_SetPropertyStr(ctx, obj, "_nodeType", JS_NewInt32(ctx, type));
    JS_SetPropertyStr(ctx, obj, "_childNodes", JS_NewArray(ctx));
    JS_SetPropertyStr(ctx, obj, "_parentNode", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "_nodeValue", JS_NULL);
    JSValue attrs = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "_attributes", JS_DupValue(ctx, attrs));
    JS_SetPropertyStr(ctx, obj, "_ownerDocument", JS_DupValue(ctx, ownerDoc));
    // Add style property for element nodes (nodeType 1)
    if (type == 1) {
        JSValue style = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, style, "cssText", JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, obj, "style", style);
        // Add class property for element nodes
        JS_SetPropertyStr(ctx, obj, "class", JS_NewString(ctx, ""));
    }
    JS_FreeValue(ctx, attrs);
    return obj;
}

int main() {
    JSRuntime *rt = JS_NewRuntime();
    JSContext *ctx = JS_NewContext(rt);

    define_console(ctx);
    define_node_proto(ctx);

    JSValue global = JS_GetGlobalObject(ctx);
    JSValue document = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, global, "document", document);

    JSValue body = make_node(ctx, "BODY", 1, document);
    JS_SetPropertyStr(ctx, document, "body", body);

    JS_SetPropertyStr(ctx, document, "createElement", JS_NewCFunction(ctx, [](JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        const char *tag = JS_ToCString(ctx, argv[0]);
        JSValue el = make_node(ctx, tag, 1, this_val);
        JS_FreeCString(ctx, tag);
        return el;
    }, "createElement", 1));

    JS_SetPropertyStr(ctx, document, "createElementNS", JS_NewCFunction(ctx, [](JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        const char *tag = JS_ToCString(ctx, argv[1]);
        JSValue el = make_node(ctx, tag, 1, this_val);
        JS_FreeCString(ctx, tag);
        return el;
    }, "createElementNS", 2));

    JS_SetPropertyStr(ctx, document, "createTextNode", JS_NewCFunction(ctx, [](JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
        const char *txt = JS_ToCString(ctx, argv[0]);
        JSValue t = make_node(ctx, "#text", 3, this_val);
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

    // Render Preact app
        const char *script = R"JS(
const { h, render } = preact;
function App() {
    return h('div', { class: 'container' }, [
        h('h1', null, 'Hello World!'),
        h('p', { style: 'color: blue; font-weight: bold;' }, 'This is a Preact app in QuickJS')
    ]);
}
render(h(App), document.body);
)JS";
    r = JS_Eval(ctx, script, strlen(script), "<app>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);

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
    return 0;
}