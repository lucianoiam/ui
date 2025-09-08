#import "SkiaView.h"
#import <include/core/SkImage.h>
#import <include/core/SkSurface.h>

@implementation SkiaView
- (BOOL)isFlipped {
  return YES;
}
- (void)drawRect:(NSRect)dirtyRect {
  [super drawRect:dirtyRect];
  if (!image)
    return;
  SkPixmap pixmap;
  if (!image->peekPixels(&pixmap))
    return;
  size_t width = pixmap.width();
  size_t height = pixmap.height();
  size_t rowBytes = pixmap.rowBytes();
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
  CGBitmapInfo bitmapInfo =
      kCGBitmapByteOrder32Host | kCGImageAlphaPremultipliedFirst;
  CGDataProviderRef provider = CGDataProviderCreateWithData(
      NULL, pixmap.addr(), rowBytes * height, NULL);
  CGImageRef cgImage =
      CGImageCreate(width, height, 8, 32, rowBytes, colorSpace, bitmapInfo,
                    provider, NULL, false, kCGRenderingIntentDefault);
  if (cgImage) {
    CGContextRef ctx = [[NSGraphicsContext currentContext] CGContext];
    CGContextDrawImage(ctx, CGRectMake(0, 0, width, height), cgImage);
    CGImageRelease(cgImage);
  }
  CGDataProviderRelease(provider);
  CGColorSpaceRelease(colorSpace);
}
@end
