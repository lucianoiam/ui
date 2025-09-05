// dom_adapter.h - QuickJS <-> C++ DOM bridge public API (formerly dom_qjs.h)
#ifndef DOM_ADAPTER_H
#define DOM_ADAPTER_H
#include <memory>
#include <quickjs.h>

namespace dom {
class Element;
class Node;
} // namespace dom

// Public adapter API (C++ linkage). Responsibilities:
//  - dom_define_node_proto: registers the shared DOMNode prototype & class with a context
//  - dom_create_document: returns a new Document (with body) bridged to C++ DOM
//  - dom_attach_document_factories: installs createElement* / createTextNode on a document
//  - dom_runtime_cleanup: explicitly release wrapper identity maps (call before freeing context)
//  - dom_adapter_unregister_runtime: clear per-runtime registration state after runtime free
//  - dom_define_core: convenience to ensure prototype installed (currently just calls dom_define_node_proto)
// Notes:
//  * Adapter no longer exposes the raw js_create* functions; they are internal/static.
//  * All linkage is C++; QuickJS only needs function pointers passed during setup.
//  * Call dom_runtime_cleanup before JS_FreeContext/JS_FreeRuntime for deterministic teardown.
// Define DOM_DISABLE_STYLE before including this header (or via compiler -DDOM_DISABLE_STYLE)
// to prevent automatic creation of .style objects on Elements for isolation / perf testing.
JSValue dom_make_node(JSContext* ctx, const char* name, int type, JSValue ownerDoc);
void dom_define_node_proto(JSContext* ctx);
JSValue dom_create_document(JSContext* ctx);
int dom_define_core(JSContext* ctx);
void dom_runtime_cleanup(JSContext* ctx);
// Notify adapter that a JSRuntime will be freed so it can forget registration state
void dom_adapter_unregister_runtime(JSRuntime* rt);
// Attach createElement/createElementNS/createTextNode functions to a document
void dom_attach_document_factories(JSContext* ctx, JSValue document);
// Retrieve canvas id for a given Element* (returns 0 if none and createIfMissing=false)
int dom_element_canvas_id(dom::Element* el, bool createIfMissing = false);
// Internal accessor (layout engine) to map JS wrapper -> C++ node pointer (non-owning)
extern "C" void* dom_get_cpp_node_opaque(JSContext* ctx, JSValueConst v);
#endif // DOM_ADAPTER_H
