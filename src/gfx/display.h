#ifndef GFX_DISPLAY_H
#define GFX_DISPLAY_H

#include <include/core/SkImage.h>
#ifdef __cplusplus
extern "C" {
#endif

// Display a Skia image on screen (platform-specific implementation)
void gfx_display_skia_image(sk_sp<SkImage> image, int width, int height);

#ifdef __cplusplus
}
#endif

#endif // GFX_DISPLAY_H
