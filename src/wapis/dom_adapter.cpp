// dom_adapter.cpp - QuickJS <-> C++ DOM bridge using dom.hpp backend (renamed from dom_qjs.cpp)
#include "dom_adapter.h"
#include "dom.hpp"
#include "renderer/sk_canvas_view.h" // moved
#include "renderer/dom_observer.h"
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <quickjs.h>
#include <string>
#include <unordered_map>
#include <vector>

using dom::Document;
using dom::Element;
using dom::Node;
using dom::Text;

// Instance state (no global mutable data) -------------------------------------------------
struct DomAdapterState {
   std::unordered_map<void*, std::shared_ptr<Node>> node_registry;
   std::unordered_map<void*, JSValue> node_wrappers;
   std::unordered_map<Element*, int> element_canvas_ids;
   JSClassID dom_node_class_id = 0;
   JSRuntime* class_runtime = nullptr;
   JSContext* ctx_for_cleanup = nullptr;
   bool in_dom_cleanup = false;
   bool dom_debug = false;
      bool debug_checked = false;
   size_t wrap_count = 0;
   size_t finalize_count = 0;
      // Class definitions/ids stored per-instance (no static globals)
      JSClassDef dom_node_class_def{};
      bool dom_class_def_init = false;
      JSClassID canvas_ctx2d_class_id = 0;
};

DomAdapterState* dom_adapter_create() { return new DomAdapterState(); }
void dom_adapter_destroy(DomAdapterState* s) { delete s; }

static inline DomAdapterState* state_from(JSContext* ctx) {
   return (DomAdapterState*)JS_GetContextOpaque(ctx);
}

static void ensure_dom_debug_init(DomAdapterState* st)
{
   if (!st->debug_checked) {
      st->debug_checked = true;
      const char* e = std::getenv("DOM_DEBUG_LOG");
      if (e && *e)
         st->dom_debug = true;
   }
}

static std::shared_ptr<Node> get_cpp_node(DomAdapterState* st, JSContext* ctx, JSValueConst val)
{
   void* ptr = JS_GetOpaque2(ctx, val, st->dom_node_class_id);
   if (!ptr)
      return nullptr;
   auto it = st->node_registry.find(ptr);
   if (it != st->node_registry.end()) {
      if (st->dom_debug) {
         JSValue idv = JS_GetPropertyStr(ctx, (JSValue)val, "__id");
         if (!JS_IsException(idv)) {
            int64_t jsid = 0;
            JS_ToInt64(ctx, &jsid, idv);
            JS_FreeValue(ctx, idv);
            if ((uint64_t)jsid != it->second->debugId) {
               fprintf(stderr, "[DOM] ID MISMATCH ptr=%p jsid=%lld cppid=%llu\n", ptr, (long long)jsid,
                       (unsigned long long)it->second->debugId);
            }
         }
      }
      return it->second;
   }
   if (st->dom_debug)
      fprintf(stderr, "[DOM] get_cpp_node: stale opaque=%p (wrapper missing in registry)\n", ptr);
   return nullptr;
}

// Expose minimal accessor for internal subsystems (layout, etc.) without leaking other internals
extern "C" void* dom_get_cpp_node_opaque(JSContext* ctx, JSValueConst v)
{
   auto* st = state_from(ctx);
   auto sp = get_cpp_node(st, ctx, v);
   return sp.get();
}

// Backward compatibility wrapper: allow existing code using get_cpp_node(ctx, val)
static inline std::shared_ptr<Node> get_cpp_node(JSContext* ctx, JSValueConst val)
{
   auto* st = state_from(ctx);
   if (!st) return nullptr;
   return get_cpp_node(st, ctx, val);
}

static void js_dom_node_finalizer(JSRuntime* rt, JSValue val)
{
   auto* st = (DomAdapterState*)JS_GetRuntimeOpaque(rt);
   if (!st) return;
   void* ptr = JS_GetOpaque(val, st->dom_node_class_id);
   if (!ptr) return;
   if (!st->in_dom_cleanup) {
      st->node_wrappers.erase(ptr);
      st->node_registry.erase(ptr);
   }
   ++st->finalize_count;
   if (st->dom_debug && (st->finalize_count < 50 || (st->finalize_count % 500) == 0)) {
      fprintf(stderr, "[DOM] finalizer ptr=%p finalize_count=%zu wrappers=%zu nodes=%zu in_cleanup=%d\n", ptr,
              st->finalize_count, st->node_wrappers.size(), st->node_registry.size(), (int)st->in_dom_cleanup);
   }
}

// Per-instance class def lives inside DomAdapterState; finalized when registering

// Wrap a C++ DOM node (stable identity)
JSValue wrap_node_js(JSContext* ctx, std::shared_ptr<Node> node)
{
   auto* st = state_from(ctx);
   if (!st) return JS_NULL; // state not bound yet
   ensure_dom_debug_init(st);
   if (!node)
      return JS_NULL;
   if (!st->ctx_for_cleanup)
      st->ctx_for_cleanup = ctx;
   void* key = node.get();
   {
   auto it = st->node_wrappers.find(key);
   if (it != st->node_wrappers.end()) {
         return JS_DupValue(ctx, it->second);
      }
   }
   JSValue obj = JS_NewObjectClass(ctx, st->dom_node_class_id);
   JS_SetOpaque(obj, key);
   st->node_registry[key] = node;
   st->node_wrappers[key] = JS_DupValue(ctx, obj);
   // Hidden debug id property
   JS_DefinePropertyValueStr(ctx, obj, "__id", JS_NewInt64(ctx, (int64_t)node->debugId), JS_PROP_WRITABLE);
   if (node->nodeType == dom::NodeType::ELEMENT) {
      JSValue style = JS_NewObject(ctx);
      // Back-reference to element wrapper for dynamic getter/setter
      JS_DefinePropertyValueStr(ctx, style, "__node", JS_DupValue(ctx, obj), JS_PROP_CONFIGURABLE);
      JSAtom cssAt = JS_NewAtom(ctx, "cssText");
      JS_DefinePropertyGetSet(ctx, style, cssAt,
                              JS_NewCFunction(
                                  ctx,
                                  [](JSContext* c, JSValueConst this_val, int, JSValueConst*) {
                                     JSValue elv = JS_GetPropertyStr(c, this_val, "__node");
                                     if (JS_IsUndefined(elv)) {
                                        JS_FreeValue(c, elv);
                                        return JS_NewString(c, "");
                                     }
                                     auto n = get_cpp_node(c, elv);
                                     JS_FreeValue(c, elv);
                                     if (!n || n->nodeType != dom::NodeType::ELEMENT)
                                        return JS_NewString(c, "");
                                     return JS_NewString(
                                         c, std::static_pointer_cast<Element>(n)->getStyleCssText().c_str());
                                  },
                                  "cssText", 0),
                              JS_NewCFunction(
                                  ctx,
                                  [](JSContext* c, JSValueConst this_val, int argc, JSValueConst* argv) {
                                     if (argc < 1)
                                        return JS_UNDEFINED;
                                     size_t len;
                                     const char* str = JS_ToCStringLen(c, &len, argv[0]);
                                     JSValue elv = JS_GetPropertyStr(c, this_val, "__node");
                                     auto n = get_cpp_node(c, elv);
                                     JS_FreeValue(c, elv);
                                     if (n && n->nodeType == dom::NodeType::ELEMENT && str) {
                                        std::static_pointer_cast<Element>(n)->setStyleCssText(str);
                                     }
                                     if (str)
                                        JS_FreeCString(c, str);
                                     return JS_UNDEFINED;
                                  },
                                  "cssText", 1),
                              JS_PROP_ENUMERABLE);
      JS_FreeAtom(ctx, cssAt);
      JS_SetPropertyStr(ctx, obj, "style", style);
   }
   ++st->wrap_count;
   if (st->dom_debug && (st->wrap_count < 50 || (st->wrap_count % 500) == 0)) {
   fprintf(stderr, "[DOM] wrap ptr=%p id=%llu wrap_count=%zu wrappers=%zu nodes=%zu\n", key,
        (unsigned long long)node->debugId, st->wrap_count, st->node_wrappers.size(), st->node_registry.size());
   }
   return obj;
}

static JSValue js_createElement(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 1)
      return JS_UNDEFINED;
   auto doc = std::dynamic_pointer_cast<Document>(get_cpp_node(ctx, this_val));
   if (!doc)
      return JS_UNDEFINED;
   const char* tag = JS_ToCString(ctx, argv[0]);
   auto el = doc->createElement(tag);
   dom_notify_element_created(el.get());
   JS_FreeCString(ctx, tag);
   return wrap_node_js(ctx, el);
}

static JSValue js_createElementNS(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 2)
      return JS_UNDEFINED;
   auto doc = std::dynamic_pointer_cast<Document>(get_cpp_node(ctx, this_val));
   if (!doc)
      return JS_UNDEFINED;
   const char* tag = JS_ToCString(ctx, argv[1]);
   auto el = doc->createElement(tag);
   dom_notify_element_created(el.get());
   JS_FreeCString(ctx, tag);
   return wrap_node_js(ctx, el);
}

static JSValue js_createTextNode(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 1)
      return JS_UNDEFINED;
   auto doc = std::dynamic_pointer_cast<Document>(get_cpp_node(ctx, this_val));
   if (!doc)
      return JS_UNDEFINED;
   const char* txt = JS_ToCString(ctx, argv[0]);
   auto t = doc->createTextNode(txt);
   JS_FreeCString(ctx, txt);
   return wrap_node_js(ctx, t);
}

// Property getters
static JSValue js_get_nodeType(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
{
   auto node = get_cpp_node(ctx, this_val);
   if (!node)
      return JS_UNDEFINED;
   return JS_NewInt32(ctx, static_cast<int>(node->nodeType));
}

static JSValue js_get_nodeName(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
{
   auto node = get_cpp_node(ctx, this_val);
   if (!node)
      return JS_UNDEFINED;
   return JS_NewString(ctx, node->nodeName.c_str());
}

static JSValue js_get_nodeValue(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
{
   auto node = get_cpp_node(ctx, this_val);
   if (!node)
      return JS_UNDEFINED;
   return JS_NewString(ctx, node->nodeValue.c_str());
}

static JSValue js_get__nodeName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   return js_get_nodeName(ctx, this_val, argc, argv);
}

static JSValue js_get_ownerDocument(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
{
   auto node = get_cpp_node(ctx, this_val);
   if (!node)
      return JS_UNDEFINED;
   if (auto doc = node->ownerDocument.lock())
      return wrap_node_js(ctx, doc);
   return wrap_node_js(ctx, node);
}

static JSValue js_get_childNodes(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
{
   auto node = get_cpp_node(ctx, this_val);
   if (!node)
      return JS_UNDEFINED;
   JSValue arr = JS_NewArray(ctx);
   uint32_t i = 0;
   for (auto& c : node->childNodes)
      JS_SetPropertyUint32(ctx, arr, i++, wrap_node_js(ctx, c));
   return arr;
}

static JSValue js_get_firstChild(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
{
   auto n = get_cpp_node(ctx, this_val);
   if (!n)
      return JS_NULL;
   return wrap_node_js(ctx, n->firstChild());
}

static JSValue js_get_lastChild(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
{
   auto n = get_cpp_node(ctx, this_val);
   if (!n)
      return JS_NULL;
   return wrap_node_js(ctx, n->lastChild());
}

static JSValue js_get_parentNode(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
{
   auto n = get_cpp_node(ctx, this_val);
   if (!n)
      return JS_NULL;
   return wrap_node_js(ctx, n->parentNode.lock());
}

static JSValue js_get_nextSibling(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
{
   auto n = get_cpp_node(ctx, this_val);
   if (!n)
      return JS_NULL;
   return wrap_node_js(ctx, n->nextSibling());
}

static JSValue js_get_previousSibling(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
{
   auto n = get_cpp_node(ctx, this_val);
   if (!n)
      return JS_NULL;
   return wrap_node_js(ctx, n->previousSibling());
}
#ifndef DOM_STRICT
static JSValue js_get_className(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
{
   auto node = get_cpp_node(ctx, this_val);
   if (!node || node->nodeType != dom::NodeType::ELEMENT)
      return JS_NewString(ctx, "");
   auto el = std::static_pointer_cast<Element>(node);
   return JS_NewString(ctx, el->className().c_str());
}

static JSValue js_set_className(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 1)
      return JS_UNDEFINED;
   size_t len;
   const char* str = JS_ToCStringLen(ctx, &len, argv[0]);
   if (str) {
      auto node = get_cpp_node(ctx, this_val);
      if (node && node->nodeType == dom::NodeType::ELEMENT) {
         std::static_pointer_cast<Element>(node)->setClassName(str);
      }
      JS_FreeCString(ctx, str);
   }
   return JS_UNDEFINED;
}
#endif
static JSValue js_get_textContent(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
{
   auto n = get_cpp_node(ctx, this_val);
   if (!n)
      return JS_UNDEFINED;
   auto s = n->textContent();
   return JS_NewString(ctx, s.c_str());
}

static JSValue js_set_textContent(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 1)
      return JS_UNDEFINED;
   size_t len;
   const char* str = JS_ToCStringLen(ctx, &len, argv[0]);
   auto n = get_cpp_node(ctx, this_val);
   if (n && str)
      n->setTextContent(str);
   if (str)
      JS_FreeCString(ctx, str);
   return JS_UNDEFINED;
}

static JSValue js_get_innerHTML(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
{
   auto n = get_cpp_node(ctx, this_val);
   if (!n || n->nodeType != dom::NodeType::ELEMENT)
      return JS_UNDEFINED;
   auto s = std::static_pointer_cast<Element>(n)->innerHTML();
   return JS_NewString(ctx, s.c_str());
}

static JSValue js_set_innerHTML(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 1)
      return JS_UNDEFINED;
   size_t len;
   const char* str = JS_ToCStringLen(ctx, &len, argv[0]);
   auto n = get_cpp_node(ctx, this_val);
   if (n && n->nodeType == dom::NodeType::ELEMENT && str)
      std::static_pointer_cast<Element>(n)->setInnerHTML(str);
   if (str)
      JS_FreeCString(ctx, str);
   return JS_UNDEFINED;
}

static JSValue js_get_outerHTML(JSContext* ctx, JSValueConst this_val, int, JSValueConst*)
{
   auto n = get_cpp_node(ctx, this_val);
   if (!n || n->nodeType != dom::NodeType::ELEMENT)
      return JS_UNDEFINED;
   auto s = std::static_pointer_cast<Element>(n)->outerHTML();
   return JS_NewString(ctx, s.c_str());
}

static JSValue js_addEventListener(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 2)
      return JS_UNDEFINED;
   const char* name = JS_ToCString(ctx, argv[0]);
   if (!name)
      return JS_UNDEFINED;
   auto n = get_cpp_node(ctx, this_val);
   if (n)
      n->addEventListener(name);
   if (JS_IsFunction(ctx, argv[1])) {
      std::string prop = std::string("__listeners_") + name;
      JSValue arr = JS_GetPropertyStr(ctx, this_val, prop.c_str());
      if (!JS_IsArray(arr)) {
         if (!JS_IsUndefined(arr))
            JS_FreeValue(ctx, arr);
         arr = JS_NewArray(ctx);
         JS_SetPropertyStr(ctx, (JSValue)this_val, prop.c_str(), JS_DupValue(ctx, arr));
      }
      int64_t len64 = 0;
      JS_GetLength(ctx, arr, &len64);
      uint32_t len = (uint32_t)len64;
      JS_SetPropertyUint32(ctx, arr, len, JS_DupValue(ctx, argv[1]));
      JS_FreeValue(ctx, arr);
   }
   JS_FreeCString(ctx, name);
   return JS_UNDEFINED;
}

static JSValue js_removeEventListener(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 2)
      return JS_UNDEFINED;
   const char* name = JS_ToCString(ctx, argv[0]);
   if (!name)
      return JS_UNDEFINED;
   auto n = get_cpp_node(ctx, this_val);
   if (n)
      n->removeEventListener(name);
   if (JS_IsFunction(ctx, argv[1])) {
      std::string prop = std::string("__listeners_") + name;
      JSValue arr = JS_GetPropertyStr(ctx, this_val, prop.c_str());
      if (JS_IsArray(arr)) {
         int64_t len64 = 0;
         JS_GetLength(ctx, arr, &len64);
         uint32_t len = (uint32_t)len64;
         for (uint32_t i = 0; i < len; i++) {
            JSValue fn = JS_GetPropertyUint32(ctx, arr, i);
            bool match = JS_IsFunction(ctx, fn) && JS_IsFunction(ctx, argv[1]);
            if (match) {
               for (uint32_t j = i + 1; j < len; j++) {
                  JSValue tmp = JS_GetPropertyUint32(ctx, arr, j);
                  JS_SetPropertyUint32(ctx, arr, j - 1, tmp);
               }
               JS_SetPropertyUint32(ctx, arr, len - 1, JS_UNDEFINED);
               break;
            }
            JS_FreeValue(ctx, fn);
         }
      }
      JS_FreeValue(ctx, arr);
   }
   JS_FreeCString(ctx, name);
   return JS_UNDEFINED;
}

static JSValue js_setAttribute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 2)
      return JS_UNDEFINED;
   auto node = get_cpp_node(ctx, this_val);
   if (!node || node->nodeType != dom::NodeType::ELEMENT)
      return JS_UNDEFINED;
   auto el = std::static_pointer_cast<Element>(node);
   const char* name = JS_ToCString(ctx, argv[0]);
   const char* value = JS_ToCString(ctx, argv[1]);
   el->setAttribute(name, value);
   JS_FreeCString(ctx, name);
   JS_FreeCString(ctx, value);
   return JS_UNDEFINED;
}

static JSValue js_setAttribute_with_notify(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 2)
      return JS_UNDEFINED;
   auto node = get_cpp_node(ctx, this_val);
   if (!node || node->nodeType != dom::NodeType::ELEMENT)
      return JS_UNDEFINED;
   auto el = std::static_pointer_cast<Element>(node);
   const char* name = JS_ToCString(ctx, argv[0]);
   const char* value = JS_ToCString(ctx, argv[1]);
   std::string oldv = el->getAttribute(name);
   el->setAttribute(name, value);
   std::string newv = el->getAttribute(name);
   if (name) {
      dom_notify_attribute_changed(el.get(), name, oldv, newv);
   }
   JS_FreeCString(ctx, name);
   JS_FreeCString(ctx, value);
   return JS_UNDEFINED;
}

static JSValue js_getAttribute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 1)
      return JS_UNDEFINED;
   auto node = get_cpp_node(ctx, this_val);
   if (!node || node->nodeType != dom::NodeType::ELEMENT)
      return JS_UNDEFINED;
   auto el = std::static_pointer_cast<Element>(node);
   const char* name = JS_ToCString(ctx, argv[0]);
   std::string v = el->getAttribute(name);
   JS_FreeCString(ctx, name);
   return JS_NewString(ctx, v.c_str());
}

static JSValue js_removeAttribute(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 1)
      return JS_UNDEFINED;
   auto node = get_cpp_node(ctx, this_val);
   if (!node || node->nodeType != dom::NodeType::ELEMENT)
      return JS_UNDEFINED;
   auto el = std::static_pointer_cast<Element>(node);
   const char* name = JS_ToCString(ctx, argv[0]);
   el->removeAttribute(name);
   JS_FreeCString(ctx, name);
   return JS_UNDEFINED;
}

static JSValue js_appendChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   auto n = get_cpp_node(ctx, this_val);
   if (!n || argc < 1)
      return JS_UNDEFINED;
   auto c = get_cpp_node(ctx, argv[0]);
   if (!c)
      return JS_UNDEFINED;
   n->appendChild(c);
   if (n->nodeType == dom::NodeType::ELEMENT)
      dom_notify_childlist_changed(std::static_pointer_cast<Element>(n).get());
   return JS_DupValue(ctx, argv[0]);
}

static JSValue js_insertBefore(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   auto n = get_cpp_node(ctx, this_val);
   if (!n || argc < 2)
      return JS_UNDEFINED;
   auto nc = get_cpp_node(ctx, argv[0]);
   auto rc = get_cpp_node(ctx, argv[1]);
   if (!nc)
      return JS_UNDEFINED;
   n->insertBefore(nc, rc);
   if (n->nodeType == dom::NodeType::ELEMENT)
      dom_notify_childlist_changed(std::static_pointer_cast<Element>(n).get());
   return JS_DupValue(ctx, argv[0]);
}

static JSValue js_removeChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 1)
      return JS_UNDEFINED;
   auto n = get_cpp_node(ctx, this_val);
   auto c = get_cpp_node(ctx, argv[0]);
   if (!n || !c)
      return JS_UNDEFINED;
   n->removeChild(c);
   if (n->nodeType == dom::NodeType::ELEMENT)
      dom_notify_childlist_changed(std::static_pointer_cast<Element>(n).get());
   if (c->nodeType == dom::NodeType::ELEMENT)
      dom_notify_element_removed(std::static_pointer_cast<Element>(c).get());
   return JS_DupValue(ctx, argv[0]);
}

static JSValue js_replaceChild(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 2)
      return JS_UNDEFINED;
   auto n = get_cpp_node(ctx, this_val);
   auto nc = get_cpp_node(ctx, argv[0]);
   auto oc = get_cpp_node(ctx, argv[1]);
   if (!n || !nc || !oc)
      return JS_UNDEFINED;
   n->replaceChild(nc, oc);
   if (n->nodeType == dom::NodeType::ELEMENT)
      dom_notify_childlist_changed(std::static_pointer_cast<Element>(n).get());
   if (oc->nodeType == dom::NodeType::ELEMENT)
      dom_notify_element_removed(std::static_pointer_cast<Element>(oc).get());
   if (nc->nodeType == dom::NodeType::ELEMENT)
      dom_notify_element_created(std::static_pointer_cast<Element>(nc).get());
   return JS_DupValue(ctx, argv[1]);
}

// getElementsByTagName (simple recursive traversal implemented here rather than relying on C++ helper)
static JSValue js_getElementsByTagName(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 1)
      return JS_NewArray(ctx);
   const char* tag = JS_ToCString(ctx, argv[0]);
   if (!tag)
      return JS_NewArray(ctx);
   std::string wanted = tag;
   JS_FreeCString(ctx, tag);
   auto root = get_cpp_node(ctx, this_val);
   JSValue arr = JS_NewArray(ctx);
   if (!root)
      return arr;
   uint32_t idx = 0;

   struct Walker {
      JSContext* ctx;
      JSValue arr;
      uint32_t* pidx;
      const std::string* wanted;

      void operator()(std::shared_ptr<Node> n)
      {
         if (!n)
            return;
         if (n->nodeType == dom::NodeType::ELEMENT) {
            auto el = std::static_pointer_cast<Element>(n);
            if (el->tagName == *wanted) {
               JS_SetPropertyUint32(ctx, arr, (*pidx)++, wrap_node_js(ctx, el));
            }
         }
         for (auto& c : n->childNodes)
            (*this)(c);
      }
   } walker{ctx, arr, &idx, &wanted};

   walker(root);
   return arr;
}

// Descriptor tables (properties & methods) for prototype definition
struct PropDesc {
   const char* name;
   JSCFunction* getter;
   JSCFunction* setter;
};

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
#ifndef DOM_STRICT
    {"className", js_get_className, js_set_className},
#endif
    {"textContent", js_get_textContent, js_set_textContent},
    {"innerHTML", js_get_innerHTML, js_set_innerHTML},
    {"outerHTML", js_get_outerHTML, nullptr},
};
struct MethodDesc {
   const char* name;
   JSCFunction* fn;
   int length;
};

// forward declare to allow inclusion in table after definition
static JSValue js_element_getContext(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

// Forward decls for method table (some indexers require explicit prototypes even if defined earlier)
static JSValue js_appendChild(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue js_insertBefore(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue js_removeChild(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue js_replaceChild(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue js_getElementsByTagName(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue js_setAttribute_with_notify(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue js_getAttribute(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue js_removeAttribute(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue js_addEventListener(JSContext*, JSValueConst, int, JSValueConst*);
static JSValue js_removeEventListener(JSContext*, JSValueConst, int, JSValueConst*);

// Canvas-like 2D context object per element (very small subset)
struct JSCanvasContext2D {
   int canvasId;
};

static JSValue js_ctx_fillRect(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 5)
      return JS_UNDEFINED;
   int32_t x = 0, y = 0, w = 0, h = 0;
   int64_t color = 0;
   JS_ToInt32(ctx, &x, argv[0]);
   JS_ToInt32(ctx, &y, argv[1]);
   JS_ToInt32(ctx, &w, argv[2]);
   JS_ToInt32(ctx, &h, argv[3]);
   JS_ToInt64(ctx, &color, argv[4]);
   auto* stc = state_from(ctx);
   JSCanvasContext2D* c2d = (JSCanvasContext2D*)JS_GetOpaque2(ctx, this_val, stc ? stc->canvas_ctx2d_class_id : 0);
   if (c2d)
      gfx_fill_rect(c2d->canvasId, x, y, w, h, (uint32_t)color);
   return JS_UNDEFINED;
}

static JSValue js_ctx_fillCircle(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc < 4)
      return JS_UNDEFINED;
   int32_t cx = 0, cy = 0, r = 0;
   int64_t color = 0xFFFFFFFF;
   JS_ToInt32(ctx, &cx, argv[0]);
   JS_ToInt32(ctx, &cy, argv[1]);
   JS_ToInt32(ctx, &r, argv[2]);
   if (argc >= 4)
      JS_ToInt64(ctx, &color, argv[3]);
   auto* stc = state_from(ctx);
   JSCanvasContext2D* c2d = (JSCanvasContext2D*)JS_GetOpaque2(ctx, this_val, stc ? stc->canvas_ctx2d_class_id : 0);
   if (c2d)
      gfx_fill_circle(c2d->canvasId, cx, cy, r, (uint32_t)color);
   return JS_UNDEFINED;
}

static JSValue js_element_getContext(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   auto node = get_cpp_node(ctx, this_val);
   if (!node || node->nodeType != dom::NodeType::ELEMENT)
      return JS_NULL;
   auto el = std::static_pointer_cast<Element>(node);
   // Only allow on <canvas>
   std::string tag = el->tagName;
   for (auto& c : tag)
      c = (char)tolower(c);
   if (tag != "canvas")
      return JS_NULL;
   // Expect first argument '2d'
   if (argc > 0) {
      const char* arg0 = JS_IsString(argv[0]) ? JS_ToCString(ctx, argv[0]) : nullptr;
      bool ok = arg0 && std::string(arg0) == "2d";
      if (arg0)
         JS_FreeCString(ctx, arg0);
      if (!ok)
         return JS_NULL;
   }
   int width = 64, height = 64;
   std::string style = el->getStyleCssText();
   auto findDecl = [&](const char* key) {
      size_t p = style.find(key);
      if (p != std::string::npos) {
         size_t c = style.find(':', p);
         size_t sc = style.find(';', c);
         if (c != std::string::npos) {
            std::string num = style.substr(c + 1, sc == std::string::npos ? std::string::npos : sc - (c + 1));
            try {
               return std::stoi(num);
            }
            catch (...) {
            }
         }
      }
      return -1;
   };
   int wDecl = findDecl("width");
   if (wDecl > 0)
      width = wDecl;
   int hDecl = findDecl("height");
   if (hDecl > 0)
      height = hDecl;
   int id;
   auto* st = state_from(ctx);
   auto it = st->element_canvas_ids.find(el.get());
   if (it == st->element_canvas_ids.end()) {
      id = gfx_create_canvas(width, height);
      st->element_canvas_ids[el.get()] = id;
   } else {
      id = it->second;
   }
   if (!id)
      return JS_NULL;
   if (st->canvas_ctx2d_class_id == 0) {
      JS_NewClassID(JS_GetRuntime(ctx), &st->canvas_ctx2d_class_id);
      JSClassDef def{};
      def.class_name = "CanvasRenderingContext2D";
      JS_NewClass(JS_GetRuntime(ctx), st->canvas_ctx2d_class_id, &def);
   }
   JSValue ctxObj = JS_NewObjectClass(ctx, st->canvas_ctx2d_class_id);
   auto* c2d = (JSCanvasContext2D*)js_mallocz(ctx, sizeof(JSCanvasContext2D));
   c2d->canvasId = id;
   JS_SetOpaque(ctxObj, c2d);
   JS_SetPropertyStr(ctx, ctxObj, "fillRect", JS_NewCFunction(ctx, js_ctx_fillRect, "fillRect", 5));
   JS_SetPropertyStr(ctx, ctxObj, "fillCircle", JS_NewCFunction(ctx, js_ctx_fillCircle, "fillCircle", 4));
   return ctxObj;
}

static const MethodDesc kMethods[] = {
   {"appendChild", js_appendChild, 1},
   {"insertBefore", js_insertBefore, 2},
   {"removeChild", js_removeChild, 1},
   {"replaceChild", js_replaceChild, 2},
   {"getElementsByTagName", js_getElementsByTagName, 1},
   {"setAttribute", js_setAttribute_with_notify, 2},
   {"getAttribute", js_getAttribute, 1},
   {"removeAttribute", js_removeAttribute, 1},
   {"addEventListener", js_addEventListener, 2},
   {"removeEventListener", js_removeEventListener, 2},
   {"getContext", js_element_getContext, 1},
};

// --- Editor/Indexer Suppression -------------------------------------------------
// Some lightweight indexers incorrectly flag the anonymous namespace close as an
// error. Provide a harmless anchor symbol under common IDE macros.
#if defined(__INTELLISENSE__) || defined(__clang_analyzer__) || defined(__GNUC_ANALYZER__)
static void __dom_adapter_indexer_sentinel__() {}
#endif
// -------------------------------------------------------------------------------

// (removed dummy anchor to avoid any global/static data)

// Public accessor for element-associated canvas id
int dom_element_canvas_id(DomAdapterState* st, dom::Element* el, bool createIfMissing)
{
   if (!el)
      return 0;
   auto& map = st->element_canvas_ids;
   auto it = map.find(el);
   if (it != map.end())
      return it->second;
   if (!createIfMissing)
      return 0;
   int id = gfx_create_canvas(64, 64);
   if (id > 0)
      map[el] = id;
   return id;
}

void dom_define_node_proto(DomAdapterState* st, JSContext* ctx)
{
   JSRuntime* rt = JS_GetRuntime(ctx);
   if (st->dom_node_class_id == 0) {
      JS_NewClassID(rt, &st->dom_node_class_id);
   }
   if (st->class_runtime != rt) {
         if (!st->dom_class_def_init) {
            st->dom_node_class_def = JSClassDef{};
            st->dom_node_class_def.class_name = "DOMNode";
            st->dom_node_class_def.finalizer = js_dom_node_finalizer;
            st->dom_class_def_init = true;
         }
         JS_NewClass(rt, st->dom_node_class_id, &st->dom_node_class_def);
      st->class_runtime = rt;
   }
   JSValue proto = JS_NewObject(ctx);
   for (const auto& pd : kPropGetSet) {
      JSAtom at = JS_NewAtom(ctx, pd.name);
      JSValue getv = pd.getter ? JS_NewCFunction(ctx, pd.getter, pd.name, 0) : JS_UNDEFINED;
      JSValue setv = pd.setter ? JS_NewCFunction(ctx, pd.setter, pd.name, 1) : JS_UNDEFINED;
      JS_DefinePropertyGetSet(ctx, proto, at, getv, setv, JS_PROP_ENUMERABLE);
      JS_FreeAtom(ctx, at);
   }
   for (const auto& md : kMethods) {
      JS_DefinePropertyValueStr(ctx, proto, md.name, JS_NewCFunction(ctx, md.fn, md.name, md.length),
                                JS_PROP_WRITABLE | JS_PROP_CONFIGURABLE);
   }
   JS_SetClassProto(ctx, st->dom_node_class_id, proto);
}

JSValue dom_make_node(DomAdapterState*, JSContext* ctx, const char*, int, JSValue)
{
   return JS_NULL;
}

JSValue dom_create_document(DomAdapterState* st, JSContext* ctx)
{
   if (st->ctx_for_cleanup && st->ctx_for_cleanup != ctx) {
      st->in_dom_cleanup = true;
      st->node_wrappers.clear();
      st->node_registry.clear();
      st->in_dom_cleanup = false;
   }
   auto doc = dom::createDocument();
   JSValue js_doc = wrap_node_js(ctx, doc);
   auto body = doc->createElement("body");
   doc->appendChild(body);
   JS_SetPropertyStr(ctx, js_doc, "body", wrap_node_js(ctx, body));
   return js_doc;
}

void dom_runtime_cleanup(DomAdapterState* st, JSContext* ctx)
{
   fprintf(stderr, "[DOM_CLEANUP] wrappers=%zu nodes=%zu\n", st->node_wrappers.size(), st->node_registry.size());
   st->in_dom_cleanup = true;
   std::vector<JSValue> to_free;
   to_free.reserve(st->node_wrappers.size());
   for (auto& p : st->node_wrappers)
      to_free.push_back(p.second);
   for (auto& v : to_free)
      JS_FreeValue(ctx, v);
   st->node_wrappers.clear();
   st->node_registry.clear();
   st->in_dom_cleanup = false;
   fprintf(stderr, "[DOM_CLEANUP] after clear wrappers=%zu nodes=%zu\n", st->node_wrappers.size(),
      st->node_registry.size());
   st->ctx_for_cleanup = nullptr;
   // Extra GC passes to flush any pending finalizers referencing cleared maps
   JSRuntime* rt = JS_GetRuntime(ctx);
   for (int i = 0; i < 3; i++)
      JS_RunGC(rt);
   if (st->dom_debug)
      fprintf(stderr, "[DOM] totals wrap=%zu finalize=%zu (post-GC)\n", st->wrap_count, st->finalize_count);
   if (st->dom_debug && !st->node_wrappers.empty()) {
      fprintf(stderr, "[DOM][WARN] wrappers not empty after cleanup: %zu\n", st->node_wrappers.size());
   }
}

// Explicitly clear global registration state when a runtime is going away.
// This helps in stress tests that create/destroy multiple runtimes sequentially
// in a single process. Without clearing, we might accidentally skip class
// re-definition for a new runtime that happens to have a different address
// (or worse, reuse a stale pointer leading to UAF if some delayed finalizer
// touches g_class_runtime). Call this after JS_FreeContext/JS_FreeRuntime.
void dom_adapter_unregister_runtime(DomAdapterState* st, JSRuntime* rt)
{
   if (st->class_runtime == rt) {
      if (getenv("DOM_DEBUG_LOG")) {
         fprintf(stderr, "[DEBUG] dom_adapter: unregister runtime %p (clearing class registration)\n", (void*)rt);
      }
   st->class_runtime = nullptr;
   }
}

int dom_define_core(DomAdapterState* st, JSContext* ctx)
{
   dom_define_node_proto(st, ctx);
   return 0;
}

// Attach document factory methods (internal API now that js_create* are static)
void dom_attach_document_factories(DomAdapterState*, JSContext* ctx, JSValue document)
{
   JS_SetPropertyStr(ctx, document, "createElement", JS_NewCFunction(ctx, js_createElement, "createElement", 1));
   JS_SetPropertyStr(ctx, document, "createElementNS", JS_NewCFunction(ctx, js_createElementNS, "createElementNS", 2));
   JS_SetPropertyStr(ctx, document, "createTextNode", JS_NewCFunction(ctx, js_createTextNode, "createTextNode", 1));
}
