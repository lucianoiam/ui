
#include "gfx/render.h"
#include "gfx/display.h"
#include <quickjs.h>
#include <include/core/SkSurface.h>
#include <include/core/SkImage.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkFont.h>
#include <include/core/SkPaint.h>
#include <stdio.h>
#include "../scratch/SkiaDisplay.h"

extern "C" void render_and_display_dom(JSContext *ctx, JSValue document) {
    fprintf(stderr, "[gfx] Entered render_and_display_dom\n");
    JSValue body = JS_GetPropertyStr(ctx, document, "body");
    if (JS_IsUndefined(body) || JS_IsNull(body)) {
        fprintf(stderr, "[gfx] No document.body found for rendering\n");
        JS_FreeValue(ctx, body);
        return;
    }

    int W = 220, H = 40;
    int windowW = 800, windowH = 600;
    const int N = 5;
    sk_sp<SkImage> images[N];
    NSPoint positions[N];
    srand((unsigned)time(NULL));
    for (int i = 0; i < N; ++i) {
    SkImageInfo info = SkImageInfo::MakeN32Premul(W, H);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) continue;
    SkCanvas *canvas = surface->getCanvas();
    // Generate a random background color
    uint8_t r = rand() % 256;
    uint8_t g = rand() % 256;
    uint8_t b = rand() % 256;
    SkColor bg = SkColorSetARGB(255, r, g, b);
    canvas->clear(bg);
        // Render the DOM tree (QuickJS version)
        gfx_render_dom_qjs(ctx, body, canvas, 0, 0, W, H);
    // Draw the address as text
    char addr[64];
    snprintf(addr, sizeof(addr), "Surface: %p", (void*)surface.get());
    SkPaint paint;
    paint.setColor(SK_ColorBLACK);
    SkFont font;
    font.setSize(18);
    canvas->drawString(addr, 8, 28, font, paint);
    // Ensure Skia flushes all drawing before snapshot
    // Random position within the larger window, ensuring images stay fully visible
    positions[i].x = rand() % (windowW - W);
    positions[i].y = rand() % (windowH - H);
    images[i] = surface->makeImageSnapshot();
    }
    fprintf(stderr, "[gfx] Calling display_skia_images with %d images\n", N);
    display_skia_images(images, positions, N, windowW, windowH);
    JS_FreeValue(ctx, body);
}
