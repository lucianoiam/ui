#ifndef FAKE_GLOBALS_H
#define FAKE_GLOBALS_H
#include <quickjs.h>
#ifdef __cplusplus
extern "C" {
#endif
void define_fake_globals(JSContext *ctx);
#ifdef __cplusplus
}
#endif
#endif // FAKE_GLOBALS_H
