#pragma once
#include <include/core/SkCanvas.h>
#include <include/core/SkImage.h>
#include <include/core/SkSurface.h>
#include <memory>
#include <quickjs.h>
#include <unordered_map>

// SkCanvasView: backs a logical <div> with a Skia surface for drawing from JS.
// Lifecycle:
//  - Created from JS via gfxCreateCanvas(width,height)
//  - Returns a numeric handle (int) referencing an internal surface record
//  - JS can issue drawing ops via gfxFillRect(handle,x,y,w,h,color)
//  - Later we can map this to DOM elements by attribute (data-gfx-id) or direct pointer wiring.
struct SkCanvasView {
   int id;
   int width;
   int height;
   sk_sp<SkSurface> surface;
};

// Create and register a new surface, returning its id (or -1 on failure)
int gfx_create_canvas(int width, int height);
// Fill rectangle with RGBA color (0xRRGGBBAA)
void gfx_fill_rect(int id, int x, int y, int w, int h, uint32_t rgba);
// Filled circle (center x,y, radius, RGBA 0xRRGGBBAA)
void gfx_fill_circle(int id, int cx, int cy, int radius, uint32_t rgba);
// Snapshot to SkImage (for future compositing / window display)
sk_sp<SkImage> gfx_snapshot(int id);
// Query size (returns true if id valid)
bool gfx_get_size(int id, int* outW, int* outH);

// QuickJS binding install (defines global gfx* functions)
void gfx_install_js(JSContext* ctx);
