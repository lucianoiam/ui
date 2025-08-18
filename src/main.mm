#include <cstdio>
#include <cstring>
#include <quickjs/quickjs.h>

// Forward declaration for remove_from_parent
static void remove_from_parent(JSContext* ctx, JSValue node);

// You must define these with your actual Preact code
extern const char preact_js[];
extern const unsigned int preact_js_len;

static void dump_exception(JSContext* ctx) {
    JSValue ex = JS_GetException(ctx);
    JSValue msg = JS_ToString(ctx, ex);
    const char *err = JS_ToCString(ctx, msg);
    fprintf(stderr, "Exception: %s\n", err ? err : "(no message)");
    JS_FreeCString(ctx, err);
    JS_FreeValue(ctx, msg);
    JSValue stk = JS_GetPropertyStr(ctx, ex, "stack");
    if (!JS_IsUndefined(stk)) {
        const char *s = JS_ToCString(ctx, stk);
        fprintf(stderr, "%s\n", s);
        JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, stk);
    }
    JS_FreeValue(ctx, ex);
}

static JSValue js_console_log(JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) {
    for (int i = 0; i < argc; ++i) {
        JSValue val = JS_ToString(ctx, argv[i]);
        const char* s = JS_ToCString(ctx, val);
        printf("[JS] %s", s ? s : "(invalid)");
        JS_FreeCString(ctx, s);
        JS_FreeValue(ctx, val);
        if (i + 1 < argc) printf(" ");
    }
    printf("\n");
    return JS_UNDEFINED;
}

void define_console(JSContext* ctx) {
    JSValue global = JS_GetGlobalObject(ctx);
    JSValue console = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, console, "log", JS_NewCFunction(ctx, js_console_log, "log", 1));
    JS_SetPropertyStr(ctx, global, "console", console);
    JS_FreeValue(ctx, global);
}

static JSValue node_proto;

static JSValue getter_nodeType(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    printf("[DOM] get nodeType on %s\n", JS_ToCString(ctx, JS_GetPropertyStr(ctx, this_val, "_nodeName")));
    return JS_GetPropertyStr(ctx, this_val, "_nodeType");
}
static JSValue getter_childNodes(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    printf("[DOM] get childNodes on %s\n", JS_ToCString(ctx, JS_GetPropertyStr(ctx, this_val, "_nodeName")));
    return JS_GetPropertyStr(ctx, this_val, "_childNodes");
}
static JSValue getter_firstChild(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    printf("[DOM] get firstChild on %s\n", JS_ToCString(ctx, JS_GetPropertyStr(ctx, this_val, "_nodeName")));
    JSValue arr = JS_GetPropertyStr(ctx, this_val, "_childNodes");
    JSValue first = JS_GetPropertyUint32(ctx, arr, 0);
    JS_FreeValue(ctx, arr);
    return first;
}
static JSValue getter_nodeValue(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    printf("[DOM] get nodeValue on %s\n", JS_ToCString(ctx, JS_GetPropertyStr(ctx, this_val, "_nodeName")));
    return JS_GetPropertyStr(ctx, this_val, "_nodeValue");
}
static JSValue setter_nodeValue(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    printf("[DOM] set nodeValue on %s\n", JS_ToCString(ctx, JS_GetPropertyStr(ctx, this_val, "_nodeName")));
    JS_SetPropertyStr(ctx, (JSValue)this_val, "_nodeValue", JS_DupValue(ctx, argv[0]));
    return JS_UNDEFINED;
}
static JSValue getter_parentNode(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    printf("[DOM] get parentNode on %s\n", JS_ToCString(ctx, JS_GetPropertyStr(ctx, this_val, "_nodeName")));
    return JS_GetPropertyStr(ctx, this_val, "_parentNode");
}
static JSValue getter_ownerDocument(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) {
    printf("[DOM] get ownerDocument on %s\n", JS_ToCString(ctx, JS_GetPropertyStr(ctx, this_val, "_nodeName")));
    return JS_GetPropertyStr(ctx, this_val, "_ownerDocument");
}
static JSValue fn_appendChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    printf("[DOM] appendChild on %s with node %s\n", JS_ToCString(ctx, JS_GetPropertyStr(ctx, this_val, "_nodeName")), JS_ToCString(ctx, JS_GetPropertyStr(ctx, argv[0], "_nodeName")));
    JSValue parentCheck = JS_GetPropertyStr(ctx, argv[0], "_parentNode");
    printf("[DEBUG] appendChild: before, node %p _parentNode=%p\n", (void*)(uintptr_t)((JSValue)argv[0]).u.ptr, (void*)(uintptr_t)parentCheck.u.ptr);
    JS_FreeValue(ctx, parentCheck);
    remove_from_parent(ctx, (JSValue)argv[0]);
    JSValue parentCheckAfter = JS_GetPropertyStr(ctx, argv[0], "_parentNode");
    printf("[DEBUG] appendChild: after remove, node %p _parentNode=%p\n", (void*)(uintptr_t)((JSValue)argv[0]).u.ptr, (void*)(uintptr_t)parentCheckAfter.u.ptr);
    JS_FreeValue(ctx, parentCheckAfter);

    JSValue arr = JS_GetPropertyStr(ctx, this_val, "_childNodes");
    JSValue push = JS_GetPropertyStr(ctx, arr, "push");
    JSValue child = JS_DupValue(ctx, argv[0]);
    printf("[TRACE] JS_DupValue child=%p\n", (void*)(uintptr_t)child.u.ptr);
    JS_Call(ctx, push, arr, 1, &child);
    JSValue parentDup = JS_DupValue(ctx, this_val);
    printf("[TRACE] JS_DupValue parent=%p\n", (void*)(uintptr_t)parentDup.u.ptr);
    JS_SetPropertyStr(ctx, (JSValue)argv[0], "_parentNode", parentDup);
    printf("[TRACE] JS_FreeValue child=%p\n", (void*)(uintptr_t)child.u.ptr);
    JS_FreeValue(ctx, child);
    JS_FreeValue(ctx, push);
    JS_FreeValue(ctx, arr);
    return JS_UNDEFINED;
}
static JSValue fn_insertBefore(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    printf("[DOM] insertBefore on %s with node %s before %s\n", JS_ToCString(ctx, JS_GetPropertyStr(ctx, this_val, "_nodeName")), JS_ToCString(ctx, JS_GetPropertyStr(ctx, argv[0], "_nodeName")), argc > 1 && !JS_IsNull(argv[1]) ? JS_ToCString(ctx, JS_GetPropertyStr(ctx, argv[1], "_nodeName")) : "null");
    // Debug parent node
    JSValue parentCheck = JS_GetPropertyStr(ctx, argv[0], "_parentNode");
    printf("[DEBUG] insertBefore: before, node %p _parentNode=%p (isNull=%d)\n",
           (void*)(uintptr_t)((JSValue)argv[0]).u.ptr, (void*)(uintptr_t)parentCheck.u.ptr, JS_IsNull(parentCheck));
    JS_FreeValue(ctx, parentCheck);
    remove_from_parent(ctx, (JSValue)argv[0]);
    JSValue parentCheckAfter = JS_GetPropertyStr(ctx, argv[0], "_parentNode");
    printf("[DEBUG] insertBefore: after remove, node %p _parentNode=%p (isNull=%d)\n",
           (void*)(uintptr_t)((JSValue)argv[0]).u.ptr, (void*)(uintptr_t)parentCheckAfter.u.ptr, JS_IsNull(parentCheckAfter));
    JS_FreeValue(ctx, parentCheckAfter);

    JSValue arr = JS_GetPropertyStr(ctx, this_val, "_childNodes");
    JSValue lenVal = JS_GetPropertyStr(ctx, arr, "length");
    int len = 0;
    JS_ToInt32(ctx, &len, lenVal);
    JS_FreeValue(ctx, lenVal);

    // Debug _childNodes contents
    printf("[DEBUG] _childNodes contents:\n");
    for (int i = 0; i < len; i++) {
        JSValue c = JS_GetPropertyUint32(ctx, arr, i);
        printf("[DEBUG] _childNodes[%d] = %p (isUndefined=%d, isNull=%d)\n",
               i, (void*)(uintptr_t)c.u.ptr, JS_IsUndefined(c), JS_IsNull(c));
        JS_FreeValue(ctx, c);
    }

    int index = len;
    if (argc > 1 && !JS_IsNull(argv[1])) {
        for (int i = 0; i < len; i++) {
            JSValue c = JS_GetPropertyUint32(ctx, arr, i);
            if (JS_StrictEq(ctx, c, argv[1])) {
                index = i;
                JS_FreeValue(ctx, c);
                break;
            }
            JS_FreeValue(ctx, c);
        }
    }

    for (int i = len; i > index; i--) {
        JSValue prev = JS_GetPropertyUint32(ctx, arr, i - 1);
        JSValue prevDup = JS_DupValue(ctx, prev);
        printf("[TRACE] JS_DupValue shift prev=%p\n", (void*)(uintptr_t)prevDup.u.ptr);
        JS_SetPropertyUint32(ctx, arr, i, prevDup);
        JS_FreeValue(ctx, prev);
    }
    JSValue old = JS_GetPropertyUint32(ctx, arr, index);
    JSValue childDup = JS_DupValue(ctx, argv[0]);
    printf("[TRACE] JS_DupValue insert child=%p\n", (void*)(uintptr_t)childDup.u.ptr);
    JS_SetPropertyUint32(ctx, arr, index, childDup);
    if (!JS_IsUndefined(old) && !JS_IsNull(old)) {
        printf("[TRACE] JS_FreeValue insert old=%p\n", (void*)(uintptr_t)old.u.ptr);
        JS_FreeValue(ctx, old);
    }
    JSValue parentDup = JS_DupValue(ctx, this_val);
    printf("[TRACE] JS_DupValue parent=%p\n", (void*)(uintptr_t)parentDup.u.ptr);
    JS_SetPropertyStr(ctx, (JSValue)argv[0], "_parentNode", parentDup);
    JS_FreeValue(ctx, arr);
    return JS_UNDEFINED;
}

void define_node_proto(JSContext* ctx) {
    node_proto = JS_NewObject(ctx);

    auto define_prop = [&](const char* name, JSCFunction* getter, JSCFunction* setter = nullptr) {
        JSAtom atom = JS_NewAtom(ctx, name);
        JS_DefinePropertyGetSet(ctx, node_proto, atom,
            JS_NewCFunction(ctx, getter, name, 0),
            setter ? JS_NewCFunction(ctx, setter, name, setter == getter ? 1 : 0) : JS_UNDEFINED,
            JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
        JS_FreeAtom(ctx, atom);
    };

    define_prop("nodeType", getter_nodeType);
    define_prop("childNodes", getter_childNodes);
    define_prop("firstChild", getter_firstChild);
    define_prop("nodeValue", getter_nodeValue, setter_nodeValue);
    define_prop("parentNode", getter_parentNode);
    define_prop("ownerDocument", getter_ownerDocument);

    JS_SetPropertyStr(ctx, node_proto, "appendChild", JS_NewCFunction(ctx, fn_appendChild, "appendChild", 1));
    JS_SetPropertyStr(ctx, node_proto, "insertBefore", JS_NewCFunction(ctx, fn_insertBefore, "insertBefore", 2));
}

JSValue make_node(JSContext* ctx, const char* name, int type, JSValue ownerDoc) {
    JSValue obj = JS_NewObject(ctx);
    JS_SetPrototype(ctx, obj, node_proto);
    JSValue nodeName = JS_NewString(ctx, name);
    printf("[DOM] constructor: %s (%d)\n", name, type);
    JS_SetPropertyStr(ctx, obj, "_nodeName", nodeName);
    JSValue nodeType = JS_NewInt32(ctx, type);
    JS_SetPropertyStr(ctx, obj, "_nodeType", nodeType);
    JSValue arr = JS_NewArray(ctx);
    JS_SetPropertyStr(ctx, obj, "_childNodes", arr);
    JS_SetPropertyStr(ctx, obj, "_parentNode", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "_nodeValue", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "_ownerDocument", ownerDoc);
    return obj;
}

static void remove_from_parent(JSContext* ctx, JSValue node) {
    JSValue parent = JS_GetPropertyStr(ctx, node, "_parentNode");
    printf("[DEBUG] Before remove_from_parent: node %p _parentNode=%p (isNull=%d)\n",
           (void*)(uintptr_t)node.u.ptr, (void*)(uintptr_t)parent.u.ptr, JS_IsNull(parent));
    if (!JS_IsNull(parent) && !JS_IsUndefined(parent)) {
        printf("[DEBUG] Removing node %p from parent %p\n", (void*)(uintptr_t)node.u.ptr, (void*)(uintptr_t)parent.u.ptr);
        JSValue arr = JS_GetPropertyStr(ctx, parent, "_childNodes");
        JSValue lenVal = JS_GetPropertyStr(ctx, arr, "length");
        int len = 0;
        JS_ToInt32(ctx, &len, lenVal);
        JS_FreeValue(ctx, lenVal);
        for (int i = 0; i < len; ++i) {
            JSValue c = JS_GetPropertyUint32(ctx, arr, i);
            if (JS_StrictEq(ctx, c, node)) {
                for (int j = i; j < len - 1; ++j) {
                    JSValue next = JS_GetPropertyUint32(ctx, arr, j + 1);
                    JS_SetPropertyUint32(ctx, arr, j, JS_DupValue(ctx, next));
                    JS_FreeValue(ctx, next);
                }
                JSValue last = JS_GetPropertyUint32(ctx, arr, len - 1);
                JS_SetPropertyUint32(ctx, arr, len - 1, JS_UNDEFINED);
                if (!JS_IsUndefined(last)) JS_FreeValue(ctx, last);
                printf("[TRACE] Set _parentNode to JS_NULL for node %p\n", (void*)(uintptr_t)node.u.ptr);
                JS_SetPropertyStr(ctx, node, "_parentNode", JS_NULL);
                break;
            }
            JS_FreeValue(ctx, c);
        }
        JS_FreeValue(ctx, arr);
    }
    JS_FreeValue(ctx, parent);
    JSValue parent_after = JS_GetPropertyStr(ctx, node, "_parentNode");
    printf("[DEBUG] After remove_from_parent: node %p _parentNode=%p (isNull=%d)\n",
           (void*)(uintptr_t)node.u.ptr, (void*)(uintptr_t)parent_after.u.ptr, JS_IsNull(parent_after));
    JS_FreeValue(ctx, parent_after);
}

int main() {
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);

    define_node_proto(ctx);
    define_console(ctx);

    JSValue global = JS_GetGlobalObject(ctx);

    // new Document
    JSValue document = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, global, "document", document);

    // new Element(BODY)
    JSValue body = make_node(ctx, "BODY", 1, document);
    JS_SetPropertyStr(ctx, document, "body", body);

    JS_SetPropertyStr(ctx, document, "createElement",
        JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
            const char* tag = JS_ToCString(ctx, argv[0]);
            JSValue el = make_node(ctx, tag, 1, this_val);
            JS_FreeCString(ctx, tag);
            return el;
        }, "createElement", 1));

    JS_SetPropertyStr(ctx, document, "createElementNS",
        JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
            // createElementNS(ns, tag)
            const char* tag = JS_ToCString(ctx, argv[1]);
            JSValue el = make_node(ctx, tag, 1, this_val);
            JS_FreeCString(ctx, tag);
            return el;
        }, "createElementNS", 2));

    JS_SetPropertyStr(ctx, document, "createTextNode",
        JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) -> JSValue {
            const char* txt = JS_ToCString(ctx, argv[0]);
            printf("[DOM] createTextNode(%s)\n", txt);
            JSValue t = make_node(ctx, "#text", 3, this_val);
            JS_SetPropertyStr(ctx, t, "_nodeValue", JS_NewString(ctx, txt));
            JS_FreeCString(ctx, txt);
            return t;
        }, "createTextNode", 1));

    JS_SetPropertyStr(ctx, global, "window", global);
    JS_SetPropertyStr(ctx, global, "self", global);
    JS_SetPropertyStr(ctx, global, "globalThis", global);

    JS_SetPropertyStr(ctx, global, "requestAnimationFrame",
        JS_NewCFunction(ctx, [](JSContext* ctx, JSValueConst, int argc, JSValueConst* argv) -> JSValue {
            if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
                JS_Call(ctx, argv[0], JS_UNDEFINED, 0, nullptr);
            }
            return JS_UNDEFINED;
        }, "requestAnimationFrame", 1));

    JSValue r = JS_Eval(ctx, preact_js, preact_js_len, "<preact>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);


    // Render Hello World!
    const char* script = "const { h, render } = preact; render(h('div', null, 'Hello World!'), document.body);";
    r = JS_Eval(ctx, script, strlen(script), "<test>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);

    // Serialize and print the DOM tree after render
    const char* print_dom =
        "function printNode(node, indent) {\n"
        "  let s = '';\n"
        "  if (node._nodeType === 1) {\n"
        "    s += indent + '<' + node._nodeName.toLowerCase() + '>' + '\\n';\n"
        "    let children = node.childNodes;\n"
        "    for (let i = 0; i < children.length; ++i) {\n"
        "      s += printNode(children[i], indent + '  ');\n"
        "    }\n"
        "    s += indent + '</' + node._nodeName.toLowerCase() + '>' + '\\n';\n"
        "  } else if (node._nodeType === 3) {\n"
        "    s += indent + node._nodeValue + '\\n';\n"
        "  }\n"
        "  return s;\n"
        "}\n"
        "console.log(printNode(document.body, ''))";
    r = JS_Eval(ctx, print_dom, strlen(print_dom), "<print_dom>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(r)) dump_exception(ctx);
    JS_FreeValue(ctx, r);

    JS_FreeValue(ctx, global);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);
    return 0;
}