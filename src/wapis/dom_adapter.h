// dom_adapter.h - QuickJS <-> C++ DOM bridge public API (formerly dom_qjs.h)
#ifndef DOM_ADAPTER_H
#define DOM_ADAPTER_H
#include <memory>
#include <quickjs.h>

namespace dom {
class Element;
class Node;
} // namespace dom

// Forward-declared opaque state for adapter instance (eliminates global/static data)
struct DomAdapterState;
DomAdapterState* dom_adapter_create();
void dom_adapter_destroy(DomAdapterState*);

// Public adapter API (instance-based). Responsibilities:
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
JSValue dom_make_node(DomAdapterState*, JSContext* ctx, const char* name, int type, JSValue ownerDoc);
void dom_define_node_proto(DomAdapterState*, JSContext* ctx);
JSValue dom_create_document(DomAdapterState*, JSContext* ctx);
int dom_define_core(DomAdapterState*, JSContext* ctx);
void dom_runtime_cleanup(DomAdapterState*, JSContext* ctx);
void dom_adapter_unregister_runtime(DomAdapterState*, JSRuntime* rt);
void dom_attach_document_factories(DomAdapterState*, JSContext* ctx, JSValue document);
int dom_element_canvas_id(DomAdapterState*, dom::Element* el, bool createIfMissing = false);
// Internal accessor (layout engine) to map JS wrapper -> C++ node pointer (non-owning)
extern "C" void* dom_get_cpp_node_opaque(JSContext* ctx, JSValueConst v);
#endif // DOM_ADAPTER_H
