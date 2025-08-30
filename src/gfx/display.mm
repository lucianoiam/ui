#include "../scratch/SkiaDisplay.h"

// C++ wrapper for multi-image display
#ifdef __cplusplus
extern "C" {
#endif
void gfx_display_skia_images(const sk_sp<SkImage>* images, const NSPoint* positions, int count, int width, int height) {
    display_skia_images(images, positions, count, width, height);
}
#ifdef __cplusplus
}
#endif
#include "display.h"
#include <include/core/SkImage.h>
#include "../scratch/SkiaDisplay.h"
#include <Cocoa/Cocoa.h>

void gfx_display_skia_image(sk_sp<SkImage> image, int width, int height) {
    // Use your existing SkiaDisplay or platform-specific code
    display_skia_image(image, width, height);
}
