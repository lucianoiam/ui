
// SkiaDisplay.h - C++ friendly API to show a SkImage in a macOS window.
#pragma once

#include <include/core/SkImage.h>
#include <include/core/SkRefCnt.h>

// Displays an SkImage in a macOS window and enters the event loop.
void display_skia_image(sk_sp<SkImage> image, int width, int height);
