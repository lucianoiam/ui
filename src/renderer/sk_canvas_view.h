#pragma once
#include <include/core/SkCanvas.h>
#include <include/core/SkImage.h>
#include <include/core/SkSurface.h>
#include <memory>
#include <quickjs.h>
#include <unordered_map>

struct SkCanvasView {
   int id;
   int width;
   int height;
   sk_sp<SkSurface> surface;
};
// Per-runtime graphics state handle (opaque to callers)
struct GfxStateHandle;
struct DomAdapterState; // from dom_adapter.h (forward decl)

// Low-level drawing API bound to a given GfxStateHandle (no globals)
int gfx_create_canvas(GfxStateHandle* gs, int width, int height);
void gfx_fill_rect(GfxStateHandle* gs, int id, int x, int y, int w, int h, uint32_t rgba);
void gfx_fill_circle(GfxStateHandle* gs, int id, int cx, int cy, int radius, uint32_t rgba);
sk_sp<SkImage> gfx_snapshot(GfxStateHandle* gs, int id);
bool gfx_get_size(GfxStateHandle* gs, int id, int* outW, int* outH);

// JS bindings installer; resolved per-context, routes through DomAdapterState's GfxStateHandle.
void gfx_install_js(JSContext* ctx);

// Lifecycle helpers for the host to manage the per-runtime graphics state.
GfxStateHandle* gfx_state_create();
void gfx_state_destroy(GfxStateHandle*);
