
// fake_dom.c - Modularized fake DOM for QuickJS/Preact emulation
#include <quickjs.h>
#include "fake_dom.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

// No-op event methods for fake DOM compatibility with Preact
static JSValue fn_addEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}
static JSValue fn_removeEventListener(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    return JS_UNDEFINED;
}



// DOM creation functions for JS
JSValue js_createElement(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *tag = JS_ToCString(ctx, argv[0]);
    JSValue el = fake_dom_make_node(ctx, tag, 1, this_val);
    JS_FreeCString(ctx, tag);
    return el;
}

JSValue js_createElementNS(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *tag = JS_ToCString(ctx, argv[1]);
    JSValue el = fake_dom_make_node(ctx, tag, 1, this_val);
    JS_FreeCString(ctx, tag);
    return el;
}

JSValue js_createTextNode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    const char *txt = JS_ToCString(ctx, argv[0]);
    JSValue t = fake_dom_make_node(ctx, "#text", 3, this_val);
    JS_SetPropertyStr(ctx, t, "_nodeValue", JS_NewString(ctx, txt));
    JS_FreeCString(ctx, txt);
    return t;
}


static int JS_Length(JSContext *ctx, JSValueConst arr) {
    JSValue len_val = JS_GetPropertyStr(ctx, arr, "length");
    int len = 0;
    JS_ToInt32(ctx, &len, len_val);
    JS_FreeValue(ctx, len_val);
    return len;
}

static void fake_dom_object_finalizer(JSRuntime *rt, JSValue val) {
    printf("[DOM] object destroyed\n");
}

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
    if (argc < 2) return JS_ThrowTypeError(ctx, "setAttribute: Expected name and value");
    const char *name = JS_ToCString(ctx, argv[0]);
    JSValue attrs = JS_GetPropertyStr(ctx, this_val, "_attributes");
    JS_SetPropertyStr(ctx, attrs, name, JS_DupValue(ctx, argv[1]));
    JS_FreeCString(ctx, name);
    JS_FreeValue(ctx, attrs);
    return JS_UNDEFINED;
}

static JSValue fn_getAttribute(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
    if (argc < 1) return JS_ThrowTypeError(ctx, "getAttribute: Expected name");
    const char *name = JS_ToCString(ctx, argv[0]);
    JSValue attrs = JS_GetPropertyStr(ctx, this_val, "_attributes");
    JSValue value = JS_GetPropertyStr(ctx, attrs, name);
    JS_FreeCString(ctx, name);
    JS_FreeValue(ctx, attrs);
    return value;
}

static JSValue fn_appendChild(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv) {
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

// Helper for defining properties (C, not C++ lambda)
static void define_prop(JSContext *ctx, JSValue proto, const char *name, JSCFunctionMagic *getter, JSCFunctionMagic *setter) {
    JSAtom atom = JS_NewAtom(ctx, name);
    JS_DefinePropertyGetSet(ctx, proto, atom,
        JS_NewCFunctionMagic(ctx, getter, name, 0, JS_CFUNC_getter, 0),
        setter ? JS_NewCFunctionMagic(ctx, setter, name, 1, JS_CFUNC_setter, 0) : JS_UNDEFINED,
        JS_PROP_CONFIGURABLE | JS_PROP_ENUMERABLE);
    JS_FreeAtom(ctx, atom);
}

void fake_dom_define_node_proto(JSContext *ctx) {
    static JSClassDef dom_class = {
        "DOMNode",
        .finalizer = fake_dom_object_finalizer
    };
    FakeDomClassInfo *info = malloc(sizeof(FakeDomClassInfo));
    JS_NewClassID(JS_GetRuntime(ctx), &info->class_id);
    JS_NewClass(JS_GetRuntime(ctx), info->class_id, &dom_class);
    info->node_proto = JS_NewObject(ctx);
    define_prop(ctx, info->node_proto, "nodeType", (JSCFunctionMagic *)getter_nodeType, NULL);
    define_prop(ctx, info->node_proto, "childNodes", (JSCFunctionMagic *)getter_childNodes, NULL);
    define_prop(ctx, info->node_proto, "firstChild", (JSCFunctionMagic *)getter_firstChild, NULL);
    define_prop(ctx, info->node_proto, "nodeValue", (JSCFunctionMagic *)getter_nodeValue, (JSCFunctionMagic *)setter_nodeValue);
    define_prop(ctx, info->node_proto, "parentNode", (JSCFunctionMagic *)getter_parentNode, NULL);
    define_prop(ctx, info->node_proto, "ownerDocument", (JSCFunctionMagic *)getter_ownerDocument, NULL);
    define_prop(ctx, info->node_proto, "attributes", (JSCFunctionMagic *)getter_attributes, NULL);

    JS_SetPropertyStr(ctx, info->node_proto, "appendChild", JS_NewCFunction(ctx, fn_appendChild, "appendChild", 1));
    JS_SetPropertyStr(ctx, info->node_proto, "insertBefore", JS_NewCFunction(ctx, fn_insertBefore, "insertBefore", 2));
    JS_SetPropertyStr(ctx, info->node_proto, "removeChild", JS_NewCFunction(ctx, fn_removeChild, "removeChild", 1));
    JS_SetPropertyStr(ctx, info->node_proto, "replaceChild", JS_NewCFunction(ctx, fn_replaceChild, "replaceChild", 2));
    JS_SetPropertyStr(ctx, info->node_proto, "setAttribute", JS_NewCFunction(ctx, fn_setAttribute, "setAttribute", 2));
    JS_SetPropertyStr(ctx, info->node_proto, "getAttribute", JS_NewCFunction(ctx, fn_getAttribute, "getAttribute", 1));
    JS_SetPropertyStr(ctx, info->node_proto, "addEventListener", JS_NewCFunction(ctx, fn_addEventListener, "addEventListener", 2));
    JS_SetPropertyStr(ctx, info->node_proto, "removeEventListener", JS_NewCFunction(ctx, fn_removeEventListener, "removeEventListener", 2));

    JS_SetContextOpaque(ctx, info);
}

JSValue fake_dom_make_node(JSContext *ctx, const char *name, int type, JSValue ownerDoc) {
    FakeDomClassInfo *info = (FakeDomClassInfo *)JS_GetContextOpaque(ctx);
    JSValue obj = JS_NewObjectClass(ctx, info->class_id);
    if (!info || JS_IsUndefined(info->node_proto)) {
        fprintf(stderr, "[FATAL] node_proto is undefined!\n");
        abort();
    }
    JS_SetPrototype(ctx, obj, info->node_proto);
    JS_SetPropertyStr(ctx, obj, "_nodeName", JS_NewString(ctx, name));
    JS_SetPropertyStr(ctx, obj, "_nodeType", JS_NewInt32(ctx, type));
    JS_SetPropertyStr(ctx, obj, "_childNodes", JS_NewArray(ctx));
    JS_SetPropertyStr(ctx, obj, "_parentNode", JS_NULL);
    JS_SetPropertyStr(ctx, obj, "_nodeValue", JS_NULL);
    JSValue attrs = JS_NewObject(ctx);
    JS_SetPropertyStr(ctx, obj, "_attributes", JS_DupValue(ctx, attrs));
    JS_SetPropertyStr(ctx, obj, "_ownerDocument", JS_DupValue(ctx, ownerDoc));
    if (type == 1) {
        JSValue style = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, style, "cssText", JS_NewString(ctx, ""));
        JS_SetPropertyStr(ctx, obj, "style", style);
        JS_SetPropertyStr(ctx, obj, "class", JS_NewString(ctx, ""));
    }
    JS_FreeValue(ctx, attrs);
    return obj;
}
