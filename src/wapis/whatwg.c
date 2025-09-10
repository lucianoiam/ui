#include "whatwg.h"
#include <quickjs.h>
#include <stdlib.h>
#include <string.h>

// Extremely naive timer polyfills (immediate execution). No persistence.
// Keep a tiny per-context state attached to the JS global, not the JSRuntime opaque
// (the runtime opaque is reserved for the DOM adapter's state).
typedef struct DomTinyState {
   int next_timer_id;
} DomTinyState;

static DomTinyState* tiny_from(JSContext* ctx)
{
   if (!ctx)
      return NULL;
   // Keep a context-private hidden property on the global object.
   JSValue g = JS_GetGlobalObject(ctx);
   JSAtom sym = JS_NewAtom(ctx, "__tinyTimers");
   JSValue v = JS_GetProperty(ctx, g, sym);
   DomTinyState* ts = NULL;
   if (JS_IsUndefined(v)) {
      ts = (DomTinyState*)malloc(sizeof(DomTinyState));
      if (ts)
         ts->next_timer_id = 1;
      JS_SetProperty(ctx, g, sym, JS_NewInt64(ctx, (int64_t)(uintptr_t)ts));
   }
   else {
      int64_t pv = 0;
      JS_ToInt64(ctx, &pv, v);
      ts = (DomTinyState*)(uintptr_t)pv;
   }
   JS_FreeValue(ctx, v);
   JS_FreeAtom(ctx, sym);
   JS_FreeValue(ctx, g);
   return ts;
}

static JSValue js_setTimeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
      JSValue ret = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
      JS_FreeValue(ctx, ret);
   }
   DomTinyState* ts = tiny_from(ctx);
   int id = ts ? ts->next_timer_id++ : 1;
   return JS_NewInt32(ctx, id);
}

static JSValue js_clearTimeout(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   return JS_UNDEFINED;
}

static JSValue js_setInterval(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   // Run once immediately, return id (no repeat scheduling implemented)
   if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
      JSValue ret = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
      JS_FreeValue(ctx, ret);
   }
   DomTinyState* ts = tiny_from(ctx);
   int id = ts ? ts->next_timer_id++ : 1;
   return JS_NewInt32(ctx, id);
}

static JSValue js_clearInterval(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   return JS_UNDEFINED;
}

static JSValue js_requestAnimationFrame(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   if (argc > 0 && JS_IsFunction(ctx, argv[0])) {
      JSValue ret = JS_Call(ctx, argv[0], JS_UNDEFINED, 0, NULL);
      JS_FreeValue(ctx, ret);
   }
   DomTinyState* ts = tiny_from(ctx);
   int id = ts ? ts->next_timer_id++ : 1;
   return JS_NewInt32(ctx, id);
}

static JSValue js_cancelAnimationFrame(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv)
{
   return JS_UNDEFINED;
}

// Define minimal WHATWG/Window globals for JS context
void define_whatwg_globals(JSContext* ctx)
{
   JSValue global_obj = JS_GetGlobalObject(ctx);
   // Timers
   JS_SetPropertyStr(ctx, global_obj, "setTimeout", JS_NewCFunction(ctx, js_setTimeout, "setTimeout", 2));
   JS_SetPropertyStr(ctx, global_obj, "clearTimeout", JS_NewCFunction(ctx, js_clearTimeout, "clearTimeout", 1));
   JS_SetPropertyStr(ctx, global_obj, "setInterval", JS_NewCFunction(ctx, js_setInterval, "setInterval", 2));
   JS_SetPropertyStr(ctx, global_obj, "clearInterval", JS_NewCFunction(ctx, js_clearInterval, "clearInterval", 1));
   JS_SetPropertyStr(ctx, global_obj, "requestAnimationFrame",
                     JS_NewCFunction(ctx, js_requestAnimationFrame, "requestAnimationFrame", 1));
   JS_SetPropertyStr(ctx, global_obj, "cancelAnimationFrame",
                     JS_NewCFunction(ctx, js_cancelAnimationFrame, "cancelAnimationFrame", 1));
   // window alias
   JS_SetPropertyStr(ctx, global_obj, "window", JS_DupValue(ctx, global_obj));
   JS_FreeValue(ctx, global_obj);
}
