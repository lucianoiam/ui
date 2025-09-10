#include "../renderer/viewport.h"
#import "InputImageView.h"
#include "input.h"
#include "renderer/renderer.h"
#include "wapis/dom.hpp"
#include "wapis/dom_adapter.h"
#import <Cocoa/Cocoa.h>
#include <include/core/SkSurface.h>
#include <quickjs.h>

extern JSContext* g_deferred_ctx; // declared in main.mm
extern JSValue g_deferred_global; // main.mm

static void dump_exception_local(JSContext* ctx)
{
   JSValue ex = JS_GetException(ctx);
   const char* err = JS_ToCString(ctx, ex);
   fprintf(stderr, "[input] exception: %s\n", err ? err : "(no message)");
   if (err)
      JS_FreeCString(ctx, err);
   JS_FreeValue(ctx, ex);
}

extern void composite_into_surface(sk_sp<SkSurface> surface, int W, int H);
extern void present_surface(NSImageView* iv, sk_sp<SkSurface> surface, int W, int H);
extern NSImageView* g_canvasImageView;
extern sk_sp<SkSurface> g_windowSurface;
extern int g_winW;
extern int g_winH;

@implementation InputImageView

- (void)dispatchMouseEventType:(const char*)type x:(int)x y:(int)y
{
   InputEvent ev{type, x, y};
   // Forward to per-context input manager if available (no globals)
   if (g_deferred_ctx) {
      void* host = dom_get_host_state(g_deferred_ctx);
      auto* im = reinterpret_cast<input::InputManager*>(host);
      if (im) {
         im->feed(ev);
      }
   }
   if (!g_deferred_ctx)
      return;
   JSValue global = JS_GetGlobalObject(g_deferred_ctx);
   JSValue fn = JS_GetPropertyStr(g_deferred_ctx, global, "__dispatchNativeMouse");
   if (JS_IsFunction(g_deferred_ctx, fn)) {
      JSValue obj = JS_NewObject(g_deferred_ctx);
      JS_SetPropertyStr(g_deferred_ctx, obj, "type", JS_NewString(g_deferred_ctx, type));
      JS_SetPropertyStr(g_deferred_ctx, obj, "clientX", JS_NewInt32(g_deferred_ctx, x));
      JS_SetPropertyStr(g_deferred_ctx, obj, "clientY", JS_NewInt32(g_deferred_ctx, y));
      JSValue args[1] = {obj};
      JSValue r = JS_Call(g_deferred_ctx, fn, global, 1, args);
      if (JS_IsException(r)) {
         dump_exception_local(g_deferred_ctx);
      }
      JS_FreeValue(g_deferred_ctx, r);
   }
   JS_FreeValue(g_deferred_ctx, fn);
   JS_FreeValue(g_deferred_ctx, global);
}

- (NSPoint)translatePoint:(NSEvent*)event
{
   // Convert Cocoa (bottom-left origin) window point to top-left origin used by style 'top'.
   NSPoint p = [event locationInWindow];
   return NSMakePoint(p.x, g_winH - p.y);
}

- (void)mouseDown:(NSEvent*)event
{
   NSPoint tp = [self translatePoint:event];
   [self dispatchMouseEventType:"mousedown" x:(int)tp.x y:(int)tp.y];
}

- (void)mouseDragged:(NSEvent*)event
{
   NSPoint tp = [self translatePoint:event];
   [self dispatchMouseEventType:"mousemove" x:(int)tp.x y:(int)tp.y];
}

- (void)mouseUp:(NSEvent*)event
{
   NSPoint tp = [self translatePoint:event];
   [self dispatchMouseEventType:"mouseup" x:(int)tp.x y:(int)tp.y];
}

@end
