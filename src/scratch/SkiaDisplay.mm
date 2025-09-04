
#include "SkiaDisplay.h"
#import <Cocoa/Cocoa.h>
#include <cstdint>
#include <include/core/SkPixmap.h>
#include <vector>

// Internal NSView subclass
@interface SkiaViewInternal : NSView {
@private
  sk_sp<SkImage> _image;
}
- (instancetype)initWithImage:(sk_sp<SkImage>)img;
@end

@implementation SkiaViewInternal
- (instancetype)initWithImage:(sk_sp<SkImage>)img {
  if ((self = [super initWithFrame:NSZeroRect])) {
    _image = std::move(img);
  }
  return self;
}
- (BOOL)isFlipped {
  return YES;
}
- (void)drawRect:(NSRect)dirtyRect {
  [super drawRect:dirtyRect];
  if (!_image)
    return;
  SkPixmap pixmap;
  if (!_image->peekPixels(&pixmap))
    return;
  size_t width = pixmap.width();
  size_t height = pixmap.height();
  size_t rowBytes = pixmap.rowBytes();
  CGColorSpaceRef colorSpace = CGColorSpaceCreateDeviceRGB();
  CGBitmapInfo bitmapInfo =
      kCGImageAlphaPremultipliedLast | kCGBitmapByteOrder32Big; // RGBA premul
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

@interface AppDelegate : NSObject <NSApplicationDelegate>
@end

@implementation AppDelegate
- (BOOL)applicationShouldTerminateAfterLastWindowClosed:
    (NSApplication *)sender {
  return YES;
}
@end

void display_skia_image(sk_sp<SkImage> image, int width, int height) {
  @autoreleasepool {
    NSApplication *app = [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    static AppDelegate *delegate = nil;
    if (!delegate) {
      delegate = [AppDelegate new];
      [app setDelegate:delegate];
    }
    if (![NSApp mainMenu]) {
      NSMenu *menubar = [[NSMenu alloc] initWithTitle:@""];
      NSMenuItem *appItem = [[NSMenuItem alloc] initWithTitle:@""
                                                       action:NULL
                                                keyEquivalent:@""];
      [menubar addItem:appItem];
      NSMenu *appMenu = [[NSMenu alloc] initWithTitle:@"Application"];
      NSString *appName = [[NSProcessInfo processInfo] processName];
      NSString *quitTitle = [NSString stringWithFormat:@"Quit %@", appName];
      NSMenuItem *quitItem =
          [[NSMenuItem alloc] initWithTitle:quitTitle
                                     action:@selector(terminate:)
                              keyEquivalent:@"q"];
      [quitItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
      [appMenu addItem:quitItem];
      [appItem setSubmenu:appMenu];
      NSMenuItem *windowItem = [[NSMenuItem alloc] initWithTitle:@""
                                                          action:NULL
                                                   keyEquivalent:@""];
      [menubar addItem:windowItem];
      NSMenu *windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
      NSMenuItem *closeItem =
          [[NSMenuItem alloc] initWithTitle:@"Close Window"
                                     action:@selector(performClose:)
                              keyEquivalent:@"w"];
      [closeItem setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
      [windowMenu addItem:closeItem];
      [windowItem setSubmenu:windowMenu];
      [NSApp setMainMenu:menubar];
    }
    NSRect frame = NSMakeRect(0, 0, width, height);
    NSWindow *window =
        [[NSWindow alloc] initWithContentRect:frame
                                    styleMask:(NSWindowStyleMaskTitled |
                                               NSWindowStyleMaskClosable |
                                               NSWindowStyleMaskResizable)
                                      backing:NSBackingStoreBuffered
                                        defer:NO];
    [window setTitle:@"Skia Output"];
    SkiaViewInternal *view = [[SkiaViewInternal alloc] initWithImage:image];
    [view setFrame:frame];
    [window setContentView:view];
    [window makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
    [app run];
  }
}
