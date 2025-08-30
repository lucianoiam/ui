
#ifndef FAKE_DOM_H
#define FAKE_DOM_H
#include <quickjs.h>
#ifdef __cplusplus
extern "C" {
#endif
typedef struct {
	JSClassID class_id;
	JSValue node_proto;
} FakeDomClassInfo;
void fake_dom_define_node_proto(JSContext *ctx);
JSValue fake_dom_make_node(JSContext *ctx, const char *name, int type, JSValue ownerDoc);
// DOM creation functions for JS
JSValue js_createElement(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_createElementNS(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
JSValue js_createTextNode(JSContext *ctx, JSValueConst this_val, int argc, JSValueConst *argv);
#ifdef __cplusplus
}
#endif
#endif // FAKE_DOM_H
