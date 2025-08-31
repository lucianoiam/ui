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
    {
        auto it = g_node_wrappers.find(key);
        if (it != g_node_wrappers.end()) {
            return JS_DupValue(ctx, it->second);
        }
    }
    JSValue obj = JS_NewObjectClass(ctx, dom_node_class_id);
    JS_SetOpaque(obj, key);
    g_node_registry[key] = node;
    g_node_wrappers[key] = JS_DupValue(ctx, obj);
    // Hidden debug id property
    JS_DefinePropertyValueStr(ctx, obj, "__id", JS_NewInt64(ctx, (int64_t)node->debugId), JS_PROP_WRITABLE);
    if (node->nodeType == dom::NodeType::ELEMENT) {
        JSValue style = JS_NewObject(ctx);
        // Back-reference to element wrapper for dynamic getter/setter
        JS_DefinePropertyValueStr(ctx, style, "__node", JS_DupValue(ctx, obj), JS_PROP_CONFIGURABLE);
        JSAtom cssAt = JS_NewAtom(ctx, "cssText");
        JS_DefinePropertyGetSet(ctx, style, cssAt,
            JS_NewCFunction(ctx, [](JSContext* c, JSValueConst this_val, int, JSValueConst*){
                JSValue elv = JS_GetPropertyStr(c, this_val, "__node");
                if (JS_IsUndefined(elv)) { JS_FreeValue(c, elv); return JS_NewString(c, ""); }
                auto n = get_cpp_node(c, elv);
                JS_FreeValue(c, elv);
                if (!n || n->nodeType != dom::NodeType::ELEMENT) return JS_NewString(c, "");
                return JS_NewString(c, std::static_pointer_cast<Element>(n)->getStyleCssText().c_str());
            }, "cssText", 0),
            JS_NewCFunction(ctx, [](JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv){
                if (argc < 1) return JS_UNDEFINED;
                size_t len; const char* str = JS_ToCStringLen(c, &len, argv[0]);
                JSValue elv = JS_GetPropertyStr(c, this_val, "__node");
                auto n = get_cpp_node(c, elv);
                JS_FreeValue(c, elv);
                if (n && n->nodeType == dom::NodeType::ELEMENT && str) {
                    std::static_pointer_cast<Element>(n)->setStyleCssText(str);
                }
                if (str) JS_FreeCString(c, str);
                return JS_UNDEFINED;
            }, "cssText", 1),
            JS_PROP_ENUMERABLE);
        JS_FreeAtom(ctx, cssAt);
        JS_SetPropertyStr(ctx, obj, "style", style);
    }
    ++g_wrap_count;
    if (g_dom_debug && (g_wrap_count < 50 || (g_wrap_count % 500)==0)) {
        fprintf(stderr, "[DOM] wrap ptr=%p id=%llu wrap_count=%zu wrappers=%zu nodes=%zu\n", key, (unsigned long long)node->debugId, g_wrap_count, g_node_wrappers.size(), g_node_registry.size());
    }
    return obj;
}

static JSValue js_createElement(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_UNDEFINED;
    auto doc = std::dynamic_pointer_cast<Document>(get_cpp_node(ctx, this_val));
    if (!doc) return JS_UNDEFINED;
    const char* tag = JS_ToCString(ctx, argv[0]);
    auto el = doc->createElement(tag);
    JS_FreeCString(ctx, tag);
    return wrap_node_js(ctx, el);
}
static JSValue js_createElementNS(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 2) return JS_UNDEFINED;
    auto doc = std::dynamic_pointer_cast<Document>(get_cpp_node(ctx, this_val));
    if (!doc) return JS_UNDEFINED;
    const char* tag = JS_ToCString(ctx, argv[1]);
    auto el = doc->createElement(tag);
    JS_FreeCString(ctx, tag);
    return wrap_node_js(ctx, el);
}
static JSValue js_createTextNode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
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
static JSValue js_get_className(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto node = get_cpp_node(ctx, this_val); if (!node || node->nodeType != dom::NodeType::ELEMENT) return JS_NewString(ctx, ""); auto el = std::static_pointer_cast<Element>(node); return JS_NewString(ctx, el->className().c_str()); }
static JSValue js_set_className(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 1) return JS_UNDEFINED; size_t len; const char* str = JS_ToCStringLen(ctx, &len, argv[0]); if (str) { auto node = get_cpp_node(ctx, this_val); if (node && node->nodeType == dom::NodeType::ELEMENT) { std::static_pointer_cast<Element>(node)->setClassName(str); } JS_FreeCString(ctx, str); } return JS_UNDEFINED; }
static JSValue js_get_textContent(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto n = get_cpp_node(ctx, this_val); if (!n) return JS_UNDEFINED; auto s = n->textContent(); return JS_NewString(ctx, s.c_str()); }
static JSValue js_set_textContent(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 1) return JS_UNDEFINED; size_t len; const char* str = JS_ToCStringLen(ctx, &len, argv[0]); auto n = get_cpp_node(ctx, this_val); if (n && str) n->setTextContent(str); if (str) JS_FreeCString(ctx, str); return JS_UNDEFINED; }
static JSValue js_get_innerHTML(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto n=get_cpp_node(ctx,this_val); if(!n) return JS_UNDEFINED; auto s=n->innerHTML(); return JS_NewString(ctx, s.c_str()); }
static JSValue js_set_innerHTML(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if(argc<1) return JS_UNDEFINED; size_t len; const char* str=JS_ToCStringLen(ctx,&len,argv[0]); auto n=get_cpp_node(ctx,this_val); if(n && str) n->setInnerHTML(str); if(str) JS_FreeCString(ctx,str); return JS_UNDEFINED; }
static JSValue js_get_outerHTML(JSContext* ctx, JSValueConst this_val, int, JSValueConst*) { auto n=get_cpp_node(ctx,this_val); if(!n) return JS_UNDEFINED; auto s=n->outerHTML(); return JS_NewString(ctx, s.c_str()); }
static JSValue js_addEventListener(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if(argc<2) return JS_UNDEFINED; const char* name=JS_ToCString(ctx, argv[0]); auto n=get_cpp_node(ctx,this_val); if(n && name) n->addEventListener(name); if(name) JS_FreeCString(ctx,name); return JS_UNDEFINED; }
static JSValue js_removeEventListener(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if(argc<2) return JS_UNDEFINED; const char* name=JS_ToCString(ctx, argv[0]); auto n=get_cpp_node(ctx,this_val); if(n && name) n->removeEventListener(name); if(name) JS_FreeCString(ctx,name); return JS_UNDEFINED; }
static JSValue js_setAttribute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 2) return JS_UNDEFINED; auto node = get_cpp_node(ctx, this_val); if (!node || node->nodeType != dom::NodeType::ELEMENT) return JS_UNDEFINED; auto el = std::static_pointer_cast<Element>(node); const char* name = JS_ToCString(ctx, argv[0]); const char* value = JS_ToCString(ctx, argv[1]); el->setAttribute(name, value); JS_FreeCString(ctx, name); JS_FreeCString(ctx, value); return JS_UNDEFINED; }
static JSValue js_getAttribute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 1) return JS_UNDEFINED; auto node = get_cpp_node(ctx, this_val); if (!node || node->nodeType != dom::NodeType::ELEMENT) return JS_UNDEFINED; auto el = std::static_pointer_cast<Element>(node); const char* name = JS_ToCString(ctx, argv[0]); std::string v = el->getAttribute(name); JS_FreeCString(ctx, name); return JS_NewString(ctx, v.c_str()); }
static JSValue js_removeAttribute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 1) return JS_UNDEFINED; auto node = get_cpp_node(ctx, this_val); if (!node || node->nodeType != dom::NodeType::ELEMENT) return JS_UNDEFINED; auto el = std::static_pointer_cast<Element>(node); const char* name = JS_ToCString(ctx, argv[0]); el->removeAttribute(name); JS_FreeCString(ctx, name); return JS_UNDEFINED; }
static JSValue js_appendChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { auto n = get_cpp_node(ctx, this_val); if (!n || argc < 1) return JS_UNDEFINED; auto c = get_cpp_node(ctx, argv[0]); if (!c) return JS_UNDEFINED; n->appendChild(c); return JS_DupValue(ctx, argv[0]); }
static JSValue js_insertBefore(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { auto n = get_cpp_node(ctx, this_val); if (!n || argc < 2) return JS_UNDEFINED; auto nc = get_cpp_node(ctx, argv[0]); auto rc = get_cpp_node(ctx, argv[1]); if (!nc) return JS_UNDEFINED; n->insertBefore(nc, rc); return JS_DupValue(ctx, argv[0]); }
static JSValue js_removeChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 1) return JS_UNDEFINED; auto n = get_cpp_node(ctx, this_val); auto c = get_cpp_node(ctx, argv[0]); if (!n || !c) return JS_UNDEFINED; n->removeChild(c); return JS_DupValue(ctx, argv[0]); }
static JSValue js_replaceChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) { if (argc < 2) return JS_UNDEFINED; auto n = get_cpp_node(ctx, this_val); auto nc = get_cpp_node(ctx, argv[0]); auto oc = get_cpp_node(ctx, argv[1]); if (!n || !nc || !oc) return JS_UNDEFINED; n->replaceChild(nc, oc); return JS_DupValue(ctx, argv[1]); }

// Descriptor tables (properties & methods) for prototype definition
struct PropDesc { const char* name; JSCFunction* getter; JSCFunction* setter; };
static const PropDesc kPropGetSet[] = {
    {"nodeType", js_get_nodeType, nullptr},
    {"nodeName", js_get_nodeName, nullptr},
    {"nodeValue", js_get_nodeValue, nullptr},
    {"childNodes", js_get_childNodes, nullptr},
    {"firstChild", js_get_firstChild, nullptr},
    {"lastChild", js_get_lastChild, nullptr},
    {"parentNode", js_get_parentNode, nullptr},
    {"nextSibling", js_get_nextSibling, nullptr},
    {"previousSibling", js_get_previousSibling, nullptr},
    {"ownerDocument", js_get_ownerDocument, nullptr},
    {"_nodeName", js_get__nodeName, nullptr},
    {"className", js_get_className, js_set_className},
    {"textContent", js_get_textContent, js_set_textContent},
    {"innerHTML", js_get_innerHTML, js_set_innerHTML},
    {"outerHTML", js_get_outerHTML, nullptr},
};
struct MethodDesc { const char* name; JSCFunction* fn; int length; };
static const MethodDesc kMethods[] = {
    {"appendChild", js_appendChild, 1},
    {"insertBefore", js_insertBefore, 2},
    {"removeChild", js_removeChild, 1},
    {"replaceChild", js_replaceChild, 2},
    {"setAttribute", js_setAttribute, 2},
    {"getAttribute", js_getAttribute, 1},
    {"removeAttribute", js_removeAttribute, 1},
    {"addEventListener", js_addEventListener, 2},
    {"removeEventListener", js_removeEventListener, 2},
};

} // namespace

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
    for (const auto &pd : kPropGetSet) {
        JSAtom at = JS_NewAtom(ctx, pd.name);
        JSValue getv = pd.getter ? JS_NewCFunction(ctx, pd.getter, pd.name, 0) : JS_UNDEFINED;
        JSValue setv = pd.setter ? JS_NewCFunction(ctx, pd.setter, pd.name, 1) : JS_UNDEFINED;
        JS_DefinePropertyGetSet(ctx, proto, at, getv, setv, JS_PROP_ENUMERABLE);
        JS_FreeAtom(ctx, at);
    }
    for (const auto &md : kMethods) {
        JS_DefinePropertyValueStr(ctx, proto, md.name, JS_NewCFunction(ctx, md.fn, md.name, md.length), JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
    }
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

// Attach document factory methods (internal API now that js_create* are static)
void dom_attach_document_factories(JSContext* ctx, JSValue document) {
    JS_SetPropertyStr(ctx, document, "createElement", JS_NewCFunction(ctx, js_createElement, "createElement", 1));
    JS_SetPropertyStr(ctx, document, "createElementNS", JS_NewCFunction(ctx, js_createElementNS, "createElementNS", 2));
    JS_SetPropertyStr(ctx, document, "createTextNode", JS_NewCFunction(ctx, js_createTextNode, "createTextNode", 1));
}
