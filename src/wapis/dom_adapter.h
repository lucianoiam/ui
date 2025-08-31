// dom_adapter.h - QuickJS <-> C++ DOM bridge public API (formerly dom_qjs.h)
#ifndef DOM_ADAPTER_H
#define DOM_ADAPTER_H
#include <quickjs.h>
#ifdef __cplusplus
extern "C" {
#endif
// Define DOM_DISABLE_STYLE before including this header (or via compiler -DDOM_DISABLE_STYLE)
// to prevent automatic creation of .style objects on Elements for isolation / perf testing.
JSValue dom_make_node(JSContext *ctx, const char *name, int type, JSValue ownerDoc);
void dom_define_node_proto(JSContext *ctx);
JSValue js_createElement(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_createElementNS(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_createTextNode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue dom_create_document(JSContext *ctx);
int dom_define_core(JSContext *ctx);
void dom_runtime_cleanup(JSContext *ctx);
// Notify adapter that a JSRuntime will be freed so it can forget registration state
void dom_adapter_unregister_runtime(JSRuntime* rt);
#ifdef __cplusplus
}
#endif
#endif // DOM_ADAPTER_H
