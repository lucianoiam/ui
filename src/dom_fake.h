// dom_fake.h
#ifndef DOM_FAKE_H
#define DOM_FAKE_H
#include <quickjs.h>
#ifdef __cplusplus
extern "C" {
#endif
void dom_define_node_proto(JSContext *ctx);
JSValue dom_make_node(JSContext *ctx, const char *name, int type, JSValue ownerDoc);
#ifdef __cplusplus
}
#endif
#endif // DOM_FAKE_H
