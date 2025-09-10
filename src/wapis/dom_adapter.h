// dom_adapter.h - QuickJS <-> C++ DOM bridge public API
#ifndef DOM_ADAPTER_H
#define DOM_ADAPTER_H
#include <memory>
#include <quickjs.h>

struct GfxStateHandle; // from renderer/sk_canvas_view.h

namespace dom {
class Element;
class Node;
} // namespace dom

// Forward-declared opaque state for adapter instance
struct DomAdapterState;
DomAdapterState* dom_adapter_create();
void dom_adapter_destroy(DomAdapterState*);

// Public adapter API (instance-based). Responsibilities:
//  - dom_define_node_proto: register the DOMNode prototype & class with a context
//  - dom_create_document: create a Document (with body) bridged to the C++ DOM
//  - dom_attach_document_factories: install createElement* / createTextNode on a document
//  - dom_runtime_cleanup: release wrapper identity maps (call before freeing the context)
//  - dom_adapter_unregister_runtime: clear per-runtime registration state after runtime free
//  - dom_define_core: ensure prototype installation (currently calls dom_define_node_proto)
// Notes:
//  * Internal js_create* functions are private to the adapter implementation.
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
// Access per-context graphics state (owned by DomAdapterState)
GfxStateHandle* dom_gfx_state(JSContext* ctx);
// Cross-platform display scale storage per-context (used by renderer to allocate in device pixels)
void dom_set_display_scale(JSContext* ctx, float scale);
float dom_get_display_scale(JSContext* ctx);
// Opaque host state pointer storage (per-context); lifetime owned by the host
void dom_set_host_state(JSContext* ctx, void* host);
void* dom_get_host_state(JSContext* ctx);
// Create and attach a Renderer (owned by DomAdapterState) to the current Document
void dom_attach_renderer(JSContext* ctx);
#endif // DOM_ADAPTER_H
