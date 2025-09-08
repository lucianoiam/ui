#pragma once
#include <include/core/SkCanvas.h>
#include <include/core/SkImage.h>
#include <include/core/SkSurface.h>
#include <memory>
#include <quickjs.h>
#include <unordered_map>

struct SkCanvasView { int id; int width; int height; sk_sp<SkSurface> surface; };
int gfx_create_canvas(int width, int height);
void gfx_fill_rect(int id, int x, int y, int w, int h, uint32_t rgba);
void gfx_fill_circle(int id, int cx, int cy, int radius, uint32_t rgba);
sk_sp<SkImage> gfx_snapshot(int id);
bool gfx_get_size(int id, int* outW, int* outH);
void gfx_install_js(JSContext* ctx);
