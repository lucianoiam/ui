#ifndef WHATWG_H
#define WHATWG_H
#include <quickjs.h>
#ifdef __cplusplus
extern "C" {
#endif
void define_whatwg_globals(JSContext *ctx);
#ifdef __cplusplus
}
#endif
#endif // WHATWG_H
