#include "render.h"
#include <include/core/SkPaint.h>
#include <include/core/SkRect.h>
#include <string.h>

// Example: extract style info from a fake DOM node (stub)
static void extract_style(struct dom_node *node, float *flex, SkColor *color) {
    // TODO: Replace with real style extraction from your fake DOM
    *flex = 1.0f;
    *color = SK_ColorLTGRAY;
    // Example: if node->style contains "background-color:#FF0000" set color = SK_ColorRED
}

// Example: iterate children (stub)
static struct dom_node **get_children(struct dom_node *node, int *count) {
    // TODO: Replace with real child access from your fake DOM
    *count = 0;
    return NULL;
}

void gfx_render_dom_to_surface(struct dom_node *root, SkSurface *surface) {
    if (!root || !surface) return;
    SkCanvas *canvas = surface->getCanvas();
    canvas->clear(SK_ColorWHITE);

    // Example: recursively render each node (replace with real layout logic)
    float flex = 1.0f;
    SkColor color = SK_ColorLTGRAY;
    extract_style(root, &flex, &color);

    // Example: draw a rectangle for this node (replace with real layout)
    SkPaint paint;
    paint.setColor(color);
    paint.setStyle(SkPaint::kFill_Style);
    canvas->drawRect(SkRect::MakeXYWH(0, 0, 100, 100), paint);

    // Recurse for children
    int child_count = 0;
    struct dom_node **children = get_children(root, &child_count);
    for (int i = 0; i < child_count; ++i) {
        gfx_render_dom_to_surface(children[i], surface);
    }
}
