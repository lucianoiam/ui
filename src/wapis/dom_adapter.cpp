// dom_adapter.cpp - QuickJS <-> C++ DOM bridge using dom.hpp backend (renamed from dom_qjs.cpp)
#include "dom_adapter.h"
#include "dom.hpp"
#include <quickjs.h>
#include <memory>
#include <unordered_map>
#include <string>
#include <vector>
#include <cstdlib>
#include <cstdio>

using dom::Node;
using dom::Element;
using dom::Text;
using dom::Document;

namespace {

// Registry to keep C++ DOM nodes alive as long as JS objects exist
static std::unordered_map<void*, std::shared_ptr<Node>> g_node_registry;
// Stable JS wrapper per C++ node
static std::unordered_map<void*, JSValue> g_node_wrappers;
static JSClassID dom_node_class_id = 0;
static JSRuntime* g_class_runtime = nullptr;
// Track context for cleanup (single-context usage assumption)
static JSContext* g_ctx_for_cleanup = nullptr;
// Guard for explicit cleanup phase
static bool g_in_dom_cleanup = false;
static bool g_dom_debug = false;
static size_t g_wrap_count = 0;
static size_t g_finalize_count = 0;

static void ensure_dom_debug_init() {
    static bool inited = false; if (!inited) { inited = true; const char* e = std::getenv("DOM_DEBUG_LOG"); if (e && *e) g_dom_debug = true; }
}

static std::shared_ptr<Node> get_cpp_node(JSContext* ctx, JSValueConst val) {
    void* ptr = JS_GetOpaque2(ctx, val, dom_node_class_id);
    if (!ptr) return nullptr;
    auto it = g_node_registry.find(ptr);
    if (it != g_node_registry.end()) {
        if (g_dom_debug) {
            JSValue idv = JS_GetPropertyStr(ctx, (JSValue)val, "__id");
            if (!JS_IsException(idv)) {
                int64_t jsid=0; JS_ToInt64(ctx, &jsid, idv);
                JS_FreeValue(ctx, idv);
                if ((uint64_t)jsid != it->second->debugId) {
                    fprintf(stderr, "[DOM] ID MISMATCH ptr=%p jsid=%lld cppid=%llu\n", ptr, (long long)jsid, (unsigned long long)it->second->debugId);
                }
            }
        }
        return it->second;
    }
    if (g_dom_debug) fprintf(stderr, "[DOM] get_cpp_node: stale opaque=%p (wrapper missing in registry)\n", ptr);
    return nullptr;
}

static void js_dom_node_finalizer(JSRuntime* rt, JSValue val) {
    void* ptr = JS_GetOpaque(val, dom_node_class_id);
    if (ptr) {
        if (!g_in_dom_cleanup) {
            g_node_wrappers.erase(ptr);
            g_node_registry.erase(ptr);
        }
        ++g_finalize_count;
        if (g_dom_debug && (g_finalize_count < 50 || (g_finalize_count % 500)==0)) {
            fprintf(stderr, "[DOM] finalizer ptr=%p finalize_count=%zu wrappers=%zu nodes=%zu in_cleanup=%d\n", ptr, g_finalize_count, g_node_wrappers.size(), g_node_registry.size(), (int)g_in_dom_cleanup);
        }
    }
}

static JSClassDef dom_node_class = {
    "DOMNode",
    .finalizer = js_dom_node_finalizer,
};

// Wrap a C++ DOM node (stable identity)
JSValue wrap_node_js(JSContext* ctx, std::shared_ptr<Node> node) {
    ensure_dom_debug_init();
    if (!node) return JS_NULL;
    if (!g_ctx_for_cleanup) g_ctx_for_cleanup = ctx;
    void* key = node.get();
    if (auto it = g_node_wrappers.find(key); it != g_node_wrappers.end())
        return JS_DupValue(ctx, it->second);
    JSValue obj = JS_NewObjectClass(ctx, dom_node_class_id);
    JS_SetOpaque(obj, key);
    g_node_registry[key] = node;
    g_node_wrappers[key] = JS_DupValue(ctx, obj);
    // Hidden debug id property
    JS_DefinePropertyValueStr(ctx, obj, "__id", JS_NewInt64(ctx, (int64_t)node->debugId), JS_PROP_WRITABLE);
#ifndef DOM_DISABLE_STYLE
    if (node->nodeType == dom::NodeType::ELEMENT) {
        JSValue style = JS_NewObject(ctx);
        JS_DefinePropertyValueStr(ctx, style, "cssText", JS_NewString(ctx, ""), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
        JS_SetPropertyStr(ctx, obj, "style", style);
    }
#endif
    ++g_wrap_count;
    if (g_dom_debug && (g_wrap_count < 50 || (g_wrap_count % 500)==0)) {
        fprintf(stderr, "[DOM] wrap ptr=%p id=%llu wrap_count=%zu wrappers=%zu nodes=%zu\n", key, (unsigned long long)node->debugId, g_wrap_count, g_node_wrappers.size(), g_node_registry.size());
    }
    return obj;
}

// JS createElement
extern "C" JSValue js_createElement(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;
    auto doc = std::dynamic_pointer_cast<Document>(get_cpp_node(ctx, this_val));
    if (!doc) return JS_UNDEFINED;
    const char* tag = JS_ToCString(ctx, argv[0]);
    auto el = doc->createElement(tag);
    JS_FreeCString(ctx, tag);
    return wrap_node_js(ctx, el);
}
// JS createElementNS (namespace ignored)
extern "C" JSValue js_createElementNS(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;
    auto doc = std::dynamic_pointer_cast<Document>(get_cpp_node(ctx, this_val));
    if (!doc) return JS_UNDEFINED;
    const char* tag = JS_ToCString(ctx, argv[1]);
    auto el = doc->createElement(tag);
    JS_FreeCString(ctx, tag);
    return wrap_node_js(ctx, el);
}
// JS createTextNode
extern "C" JSValue js_createTextNode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;
    auto doc = std::dynamic_pointer_cast<Document>(get_cpp_node(ctx, this_val));
    if (!doc) return JS_UNDEFINED;
    const char* txt = JS_ToCString(ctx, argv[0]);
    auto t = doc->createTextNode(txt);
    JS_FreeCString(ctx, txt);
    return wrap_node_js(ctx, t);
}

// Property getters
static JSValue js_get_nodeType(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto node = get_cpp_node(ctx, this_val); if (!node) return JS_UNDEFINED; return JS_NewInt32(ctx, static_cast<int>(node->nodeType)); }
static JSValue js_get_nodeName(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto node = get_cpp_node(ctx, this_val); if (!node) return JS_UNDEFINED; return JS_NewString(ctx, node->nodeName.c_str()); }
static JSValue js_get_nodeValue(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto node = get_cpp_node(ctx, this_val); if (!node) return JS_UNDEFINED; return JS_NewString(ctx, node->nodeValue.c_str()); }
static JSValue js_get__nodeName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { return js_get_nodeName(ctx, this_val, argc, argv); }
static JSValue js_get_ownerDocument(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto node = get_cpp_node(ctx, this_val); if (!node) return JS_UNDEFINED; if (auto doc = node->ownerDocument.lock()) return wrap_node_js(ctx, doc); return wrap_node_js(ctx, node); }
static JSValue js_get_childNodes(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto node = get_cpp_node(ctx, this_val); if (!node) return JS_UNDEFINED; JSValue arr = JS_NewArray(ctx); uint32_t i=0; for (auto &c : node->childNodes) JS_SetPropertyUint32(ctx, arr, i++, wrap_node_js(ctx, c)); return arr; }
static JSValue js_get_firstChild(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto n = get_cpp_node(ctx, this_val); if(!n) return JS_NULL; return wrap_node_js(ctx, n->firstChild()); }
static JSValue js_get_lastChild(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto n = get_cpp_node(ctx, this_val); if(!n) return JS_NULL; return wrap_node_js(ctx, n->lastChild()); }
static JSValue js_get_parentNode(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto n=get_cpp_node(ctx,this_val); if(!n) return JS_NULL; return wrap_node_js(ctx, n->parentNode.lock()); }
static JSValue js_get_nextSibling(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto n = get_cpp_node(ctx,this_val); if(!n) return JS_NULL; return wrap_node_js(ctx, n->nextSibling()); }
static JSValue js_get_previousSibling(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto n = get_cpp_node(ctx,this_val); if(!n) return JS_NULL; return wrap_node_js(ctx, n->previousSibling()); }
static JSValue js_get_className(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto node = get_cpp_node(ctx, this_val); if (!node || node->nodeType != dom::NodeType::ELEMENT) return JS_NewString(ctx, ""); auto el = std::static_pointer_cast<Element>(node); return JS_NewString(ctx, el->getAttribute("class").c_str()); }
static JSValue js_set_className(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 1) return JS_UNDEFINED; size_t len; const char* str = JS_ToCStringLen(ctx, &len, argv[0]); if (str) { auto node = get_cpp_node(ctx, this_val); if (node && node->nodeType == dom::NodeType::ELEMENT) { auto el = std::static_pointer_cast<Element>(node); el->setAttribute("class", str); } JS_FreeCString(ctx, str); } return JS_UNDEFINED; }
static JSValue js_get_textContent(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto n = get_cpp_node(ctx, this_val); if (!n) return JS_UNDEFINED; if (n->nodeType == dom::NodeType::TEXT) return JS_NewString(ctx, n->nodeValue.c_str()); std::string acc; acc.reserve(64); for (auto &c : n->childNodes) { if (c->nodeType == dom::NodeType::TEXT) acc += c->nodeValue; else { for (auto &gc : c->childNodes) if (gc->nodeType == dom::NodeType::TEXT) acc += gc->nodeValue; } } return JS_NewString(ctx, acc.c_str()); }
static JSValue js_set_textContent(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 1) return JS_UNDEFINED; size_t len; const char* str = JS_ToCStringLen(ctx, &len, argv[0]); auto n = get_cpp_node(ctx, this_val); if (n) { for (auto &c : n->childNodes) if (c) c->parentNode.reset(); n->childNodes.clear(); if (str && n->nodeType != dom::NodeType::TEXT) { if (auto doc = std::dynamic_pointer_cast<Document>(n->ownerDocument.lock())) n->appendChild(doc->createTextNode(str)); } else if (str && n->nodeType == dom::NodeType::TEXT) n->nodeValue = str; } if (str) JS_FreeCString(ctx, str); return JS_UNDEFINED; }
static JSValue js_get_innerHTML(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { return js_get_textContent(ctx, this_val, argc, argv); }
static JSValue js_set_innerHTML(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { return js_set_textContent(ctx, this_val, argc, argv); }
static JSValue js_addEventListener(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 2) return JS_UNDEFINED; JSValue listeners = JS_GetPropertyStr(ctx, this_val, "__listeners"); if (JS_IsUndefined(listeners)) { listeners = JS_NewObject(ctx); JS_SetPropertyStr(ctx, (JSValue)this_val, "__listeners", listeners); } const char* name = JS_ToCString(ctx, argv[0]); JS_SetPropertyStr(ctx, listeners, name, JS_DupValue(ctx, argv[1])); JS_FreeCString(ctx, name); return JS_UNDEFINED; }
static JSValue js_removeEventListener(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 2) return JS_UNDEFINED; JSValue listeners = JS_GetPropertyStr(ctx, this_val, "__listeners"); if (JS_IsObject(listeners)) { const char* name = JS_ToCString(ctx, argv[0]); if (name) { JSAtom at = JS_NewAtom(ctx, name); JS_DeleteProperty(ctx, listeners, at, 0); JS_FreeAtom(ctx, at); JS_FreeCString(ctx, name); } } JS_FreeValue(ctx, listeners); return JS_UNDEFINED; }
static JSValue js_setAttribute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 2) return JS_UNDEFINED; auto node = get_cpp_node(ctx, this_val); if (!node || node->nodeType != dom::NodeType::ELEMENT) return JS_UNDEFINED; auto el = std::static_pointer_cast<Element>(node); const char* name = JS_ToCString(ctx, argv[0]); const char* value = JS_ToCString(ctx, argv[1]); el->setAttribute(name, value); JS_FreeCString(ctx, name); JS_FreeCString(ctx, value); return JS_UNDEFINED; }
static JSValue js_getAttribute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 1) return JS_UNDEFINED; auto node = get_cpp_node(ctx, this_val); if (!node || node->nodeType != dom::NodeType::ELEMENT) return JS_UNDEFINED; auto el = std::static_pointer_cast<Element>(node); const char* name = JS_ToCString(ctx, argv[0]); std::string v = el->getAttribute(name); JS_FreeCString(ctx, name); return JS_NewString(ctx, v.c_str()); }
static JSValue js_removeAttribute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 1) return JS_UNDEFINED; auto node = get_cpp_node(ctx, this_val); if (!node || node->nodeType != dom::NodeType::ELEMENT) return JS_UNDEFINED; auto el = std::static_pointer_cast<Element>(node); const char* name = JS_ToCString(ctx, argv[0]); el->removeAttribute(name); JS_FreeCString(ctx, name); return JS_UNDEFINED; }
static JSValue js_appendChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { auto n = get_cpp_node(ctx, this_val); if (!n || argc < 1) return JS_UNDEFINED; auto c = get_cpp_node(ctx, argv[0]); if (!c) return JS_UNDEFINED; n->appendChild(c); return JS_DupValue(ctx, argv[0]); }
static JSValue js_insertBefore(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { auto n = get_cpp_node(ctx, this_val); if (!n || argc < 2) return JS_UNDEFINED; auto nc = get_cpp_node(ctx, argv[0]); auto rc = get_cpp_node(ctx, argv[1]); if (!nc) return JS_UNDEFINED; n->insertBefore(nc, rc); return JS_DupValue(ctx, argv[0]); }
static JSValue js_removeChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 1) return JS_UNDEFINED; auto n = get_cpp_node(ctx, this_val); auto c = get_cpp_node(ctx, argv[0]); if (!n || !c) return JS_UNDEFINED; n->removeChild(c); return JS_DupValue(ctx, argv[0]); }
static JSValue js_replaceChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 2) return JS_UNDEFINED; auto n = get_cpp_node(ctx, this_val); auto nc = get_cpp_node(ctx, argv[0]); auto oc = get_cpp_node(ctx, argv[1]); if (!n || !nc || !oc) return JS_UNDEFINED; n->replaceChild(nc, oc); return JS_DupValue(ctx, argv[1]); }

} // namespace

extern "C" {

void dom_define_node_proto(JSContext* ctx) {
    JSRuntime* rt = JS_GetRuntime(ctx);
    if (dom_node_class_id == 0) {
        JS_NewClassID(rt, &dom_node_class_id);
    }
    if (g_class_runtime != rt) {
        JS_NewClass(rt, dom_node_class_id, &dom_node_class);
        g_class_runtime = rt;
    }
    JSValue proto = JS_NewObject(ctx);
    auto addGetter=[&](const char* name, JSCFunction* fn){ JSAtom at=JS_NewAtom(ctx,name); JS_DefinePropertyGetSet(ctx, proto, at, JS_NewCFunction(ctx, fn, name, 0), JS_UNDEFINED, JS_PROP_ENUMERABLE); JS_FreeAtom(ctx, at); };
    addGetter("nodeType", js_get_nodeType);
    addGetter("nodeName", js_get_nodeName);
    addGetter("nodeValue", js_get_nodeValue);
    addGetter("childNodes", js_get_childNodes);
    addGetter("firstChild", js_get_firstChild);
    addGetter("lastChild", js_get_lastChild);
    addGetter("parentNode", js_get_parentNode);
    addGetter("nextSibling", js_get_nextSibling);
    addGetter("previousSibling", js_get_previousSibling);
    addGetter("ownerDocument", js_get_ownerDocument);
    addGetter("_nodeName", js_get__nodeName);
    JS_DefinePropertyGetSet(ctx, proto, JS_NewAtom(ctx, "className"), JS_NewCFunction(ctx, js_get_className, "get className", 0), JS_NewCFunction(ctx, js_set_className, "set className", 1), JS_PROP_ENUMERABLE);
    JS_DefinePropertyGetSet(ctx, proto, JS_NewAtom(ctx, "textContent"), JS_NewCFunction(ctx, js_get_textContent, "get textContent", 0), JS_NewCFunction(ctx, js_set_textContent, "set textContent", 1), JS_PROP_ENUMERABLE);
    JS_DefinePropertyGetSet(ctx, proto, JS_NewAtom(ctx, "innerHTML"), JS_NewCFunction(ctx, js_get_innerHTML, "get innerHTML", 0), JS_NewCFunction(ctx, js_set_innerHTML, "set innerHTML", 1), JS_PROP_ENUMERABLE);
    JS_DefinePropertyValueStr(ctx, proto, "appendChild", JS_NewCFunction(ctx, js_appendChild, "appendChild", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, proto, "insertBefore", JS_NewCFunction(ctx, js_insertBefore, "insertBefore", 2), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, proto, "removeChild", JS_NewCFunction(ctx, js_removeChild, "removeChild", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, proto, "replaceChild", JS_NewCFunction(ctx, js_replaceChild, "replaceChild", 2), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, proto, "setAttribute", JS_NewCFunction(ctx, js_setAttribute, "setAttribute", 2), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, proto, "getAttribute", JS_NewCFunction(ctx, js_getAttribute, "getAttribute", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, proto, "removeAttribute", JS_NewCFunction(ctx, js_removeAttribute, "removeAttribute", 1), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, proto, "addEventListener", JS_NewCFunction(ctx, js_addEventListener, "addEventListener", 2), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_DefinePropertyValueStr(ctx, proto, "removeEventListener", JS_NewCFunction(ctx, js_removeEventListener, "removeEventListener", 2), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    JS_SetClassProto(ctx, dom_node_class_id, proto);
}

JSValue dom_make_node(JSContext* ctx, const char*, int, JSValue) { return JS_NULL; }

JSValue dom_create_document(JSContext* ctx) {
    if (g_ctx_for_cleanup && g_ctx_for_cleanup != ctx) {
        g_in_dom_cleanup = true;
        g_node_wrappers.clear();
        g_node_registry.clear();
        g_in_dom_cleanup = false;
    }
    auto doc = dom::createDocument();
    JSValue js_doc = wrap_node_js(ctx, doc);
    auto body = doc->createElement("body");
    doc->appendChild(body);
    JS_SetPropertyStr(ctx, js_doc, "body", wrap_node_js(ctx, body));
    return js_doc;
}

void dom_runtime_cleanup(JSContext* ctx) {
    fprintf(stderr, "[DOM_CLEANUP] wrappers=%zu nodes=%zu\n", g_node_wrappers.size(), g_node_registry.size());
    g_in_dom_cleanup = true;
    std::vector<JSValue> to_free; to_free.reserve(g_node_wrappers.size());
    for (auto &p : g_node_wrappers) to_free.push_back(p.second);
    for (auto &v : to_free) JS_FreeValue(ctx, v);
    g_node_wrappers.clear();
    g_node_registry.clear();
    g_in_dom_cleanup = false;
    fprintf(stderr, "[DOM_CLEANUP] after clear wrappers=%zu nodes=%zu\n", g_node_wrappers.size(), g_node_registry.size());
    g_ctx_for_cleanup = nullptr;
    // Extra GC passes to flush any pending finalizers referencing cleared maps
    JSRuntime* rt = JS_GetRuntime(ctx);
    for (int i=0;i<3;i++) JS_RunGC(rt);
    if (g_dom_debug) fprintf(stderr, "[DOM] totals wrap=%zu finalize=%zu (post-GC)\n", g_wrap_count, g_finalize_count);
    if (g_dom_debug && !g_node_wrappers.empty()) {
        fprintf(stderr, "[DOM][WARN] wrappers not empty after cleanup: %zu\n", g_node_wrappers.size());
    }
}

// Explicitly clear global registration state when a runtime is going away.
// This helps in stress tests that create/destroy multiple runtimes sequentially
// in a single process. Without clearing, we might accidentally skip class
// re-definition for a new runtime that happens to have a different address
// (or worse, reuse a stale pointer leading to UAF if some delayed finalizer
// touches g_class_runtime). Call this after JS_FreeContext/JS_FreeRuntime.
void dom_adapter_unregister_runtime(JSRuntime* rt) {
    if (g_class_runtime == rt) {
        if (getenv("DOM_DEBUG_LOG")) {
            fprintf(stderr, "[DEBUG] dom_adapter: unregister runtime %p (clearing class registration)\n", (void*)rt);
        }
        g_class_runtime = nullptr;
    }
}

int dom_define_core(JSContext* ctx) { dom_define_node_proto(ctx); return 0; }

} // extern "C"
