 #ifndef DOM_QJS_H
 #define DOM_QJS_H
#include <quickjs.h>
#ifdef __cplusplus
extern "C" {
#endif
JSValue dom_make_node(JSContext *ctx, const char *name, int type, JSValue ownerDoc);
void dom_define_node_proto(JSContext *ctx);
JSValue js_createElement(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_createElementNS(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_createTextNode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue dom_create_document(JSContext *ctx);
int dom_define_core(JSContext *ctx);
#ifdef __cplusplus
}
#endif
#endif // DOM_QJS_H
