#include "render.h"
#include <quickjs.h>

// C-callable wrapper that forwards to C++ implementation
// Ensure C linkage for the symbol
#ifdef __cplusplus
extern "C" {
#endif
void gfx_render_dom_qjs(JSContext *ctx, JSValue node, void *canvas, float x, float y, float w, float h) {
    // Implemented in render_dom_qjs.cpp
    extern void gfx_render_dom_qjs_c(JSContext *, JSValue, void *, float, float, float, float);
    gfx_render_dom_qjs_c(ctx, node, canvas, x, y, w, h);
#ifdef __cplusplus
}
#endif
}
