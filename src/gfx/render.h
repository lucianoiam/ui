#ifndef GFX_RENDER_H
#define GFX_RENDER_H

#include <quickjs.h>

// Only for C++
#ifdef __cplusplus
#include <include/core/SkSurface.h>
#include <include/core/SkCanvas.h>
struct dom_node;
void gfx_render_dom_to_surface(struct dom_node *root, SkSurface *surface);
#endif

#ifdef __cplusplus
extern "C" {
#endif

void gfx_render_dom_qjs(JSContext *ctx, JSValue root, void *canvas, float x, float y, float w, float h);
void render_and_display_dom(JSContext *ctx, JSValue document);

#ifdef __cplusplus
}
#endif

#endif // GFX_RENDER_H
