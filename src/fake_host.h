#ifndef FAKE_HOST_H
#define FAKE_HOST_H
#include <quickjs.h>
#ifdef __cplusplus
extern "C" {
#endif
void fake_define_console(JSContext *ctx);
#ifdef __cplusplus
}
#endif
#endif // FAKE_HOST_H
