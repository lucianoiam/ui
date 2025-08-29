#ifndef FAKE_DOM_H
#define FAKE_DOM_H
#include <quickjs.h>
#ifdef __cplusplus
extern "C" {
#endif
void fake_dom_define_node_proto(JSContext *ctx);
JSValue fake_dom_make_node(JSContext *ctx, const char *name, int type, JSValue ownerDoc);
#ifdef __cplusplus
}
#endif
#endif // FAKE_DOM_H
