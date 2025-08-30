
// SkiaDisplay.h - C++ friendly API to show a SkImage in a macOS window.
#pragma once

#include <include/core/SkImage.h>
#include <include/core/SkRefCnt.h>



#ifdef __OBJC__
#import <Cocoa/Cocoa.h>
#else
typedef struct _NSPoint { double x, y; } NSPoint;
#endif
// Displays multiple SkImages at given positions in a macOS window and enters the event loop.
// images: array of sk_sp<SkImage>
// positions: array of NSPoint for each image
// count: number of images
// width, height: window size
extern "C" void display_skia_images(const sk_sp<SkImage>* images, const NSPoint* positions, int count, int width, int height);

// Displays a single SkImage (legacy)
void display_skia_image(sk_sp<SkImage> image, int width, int height);
