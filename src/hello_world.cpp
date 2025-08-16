#include <cstdio>
#include <cstring>
#include <unordered_map>
#include <string>
#include <functional>

#include <lexbor/dom/interfaces/document.h>
#include <lexbor/dom/interfaces/element.h>
#include <lexbor/dom/interfaces/node.h>
#include <lexbor/html/html.h>
#include <lexbor/html/interfaces/document.h>

#include <yoga/Yoga.h>

#include <include/core/SkSurface.h>
#include <include/core/SkCanvas.h>
#include <include/core/SkPaint.h>
#include <include/core/SkImageInfo.h>
#include <include/core/SkImage.h>
#include <include/core/SkData.h>
#include <include/core/SkStream.h>
#include <include/encode/SkPngEncoder.h>

extern "C" {
#include "quickjs/quickjs.h"
}

// HTML content as before
static const char* html_content = R"HTML(
<div id="container" style="display:flex; flex-direction: column; width: 800px; height: 600px;">
  <div id="box1" style="background-color:#FF0000; flex:1"></div>
  <div id="box2" style="background-color:#00FF00; flex:1"></div>
  <div id="box3" style="background-color:#0000FF; flex:1"></div>
  <div id="canvasContainer" style="flex:2">
    <canvas id="mycanvas" width="640" height="480"></canvas>
  </div>
</div>
)HTML";

// Global pointer to SkCanvas for JS functions to draw on
static SkCanvas* g_skiaCanvas = nullptr;

// Forward declarations of JS native functions
static JSValue js_arc(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
static JSValue js_fill(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
static JSValue js_getContext(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);
static JSValue js_getCanvasById(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv);

static JSValue js_arc(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 3) return JS_EXCEPTION;
    double x, y, r;
    if (JS_ToFloat64(ctx, &x, argv[0]) ||
        JS_ToFloat64(ctx, &y, argv[1]) ||
        JS_ToFloat64(ctx, &r, argv[2]))
        return JS_EXCEPTION;

    if (!g_skiaCanvas) return JS_EXCEPTION;

    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SK_ColorCYAN);
    paint.setStyle(SkPaint::kFill_Style);

    g_skiaCanvas->drawCircle((float)x, (float)y, (float)r, paint);

    return JS_UNDEFINED;
}

static JSValue js_fill(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    // no-op placeholder for compatibility
    return JS_UNDEFINED;
}

static JSValue js_getContext(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NULL;
    const char* ctxType = JS_ToCString(ctx, argv[0]);
    if (!ctxType) return JS_NULL;

    JSValue ctx2d = JS_NULL;
    if (strcmp(ctxType, "2d") == 0) {
        ctx2d = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, ctx2d, "arc", JS_NewCFunction(ctx, js_arc, "arc", 3));
        JS_SetPropertyStr(ctx, ctx2d, "fill", JS_NewCFunction(ctx, js_fill, "fill", 0));
    }
    JS_FreeCString(ctx, ctxType);
    return ctx2d;
}

static JSValue js_getCanvasById(JSContext* ctx, JSValueConst this_val, int argc, JSValueConst* argv) {
    if (argc < 1) return JS_NULL;
    const char* id = JS_ToCString(ctx, argv[0]);
    if (!id) return JS_NULL;

    JSValue canvasObj = JS_NULL;
    if (strcmp(id, "mycanvas") == 0) {
        canvasObj = JS_NewObject(ctx);
        JS_SetPropertyStr(ctx, canvasObj, "getContext", JS_NewCFunction(ctx, js_getContext, "getContext", 1));
    }

    JS_FreeCString(ctx, id);
    return canvasObj;
}

int main() {
    // Create Lexbor HTML document
    lxb_html_document_t* document = lxb_html_document_create();
    if (!document) {
        fprintf(stderr, "Failed to create Lexbor document\n");
        return 1;
    }

    // Parse HTML content
    lxb_status_t status = lxb_html_document_parse(document, (const lxb_char_t*)html_content, strlen(html_content));
    if (status != LXB_STATUS_OK) {
        fprintf(stderr, "Failed to parse HTML\n");
        lxb_html_document_destroy(document);
        return 1;
    }

    // Get the DOM document
    lxb_dom_document_t* dom_doc = (lxb_dom_document_t*)document;
    if (!dom_doc) {
        fprintf(stderr, "Failed to get DOM document\n");
        lxb_html_document_destroy(document);
        return 1;
    }

    // Helper function to find element by ID
    std::function<lxb_dom_element_t*(lxb_dom_node_t*, const char*)> find_element_by_id = [&](lxb_dom_node_t* node, const char* id) -> lxb_dom_element_t* {
        if (node->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            lxb_dom_element_t* el = (lxb_dom_element_t*)node;
            size_t attr_len = 0;
            const lxb_char_t* attr_val = lxb_dom_element_get_attribute(el, (const lxb_char_t*)"id", 2, &attr_len);
            if (attr_val != nullptr) {
                if (attr_len == strlen(id) && strncmp((const char*)attr_val, id, attr_len) == 0) {
                    return el;
                }
            }
        }

        for (lxb_dom_node_t* child = node->first_child; child; child = lxb_dom_node_next(child)) {
            lxb_dom_element_t* found = find_element_by_id(child, id);
            if (found) return found;
        }
        return nullptr;
    };

    // Get container element by ID
    lxb_dom_element_t* container = find_element_by_id((lxb_dom_node_t*)lxb_html_document_body_element(document), "container");
    if (!container) {
        fprintf(stderr, "Container div not found\n");
        lxb_html_document_destroy(document);
        return 1;
    }

    // Map from DOM node pointer to Yoga node
    std::unordered_map<lxb_dom_node_t*, YGNodeRef> yogaNodes;

    // Create Yoga root node
    YGNodeRef yoga_root = YGNodeNew();
    YGNodeStyleSetFlexDirection(yoga_root, YGFlexDirectionColumn);
    YGNodeStyleSetWidth(yoga_root, 800);
    YGNodeStyleSetHeight(yoga_root, 600);

    yogaNodes[(lxb_dom_node_t*)container] = yoga_root;

    // Iterate children of container
    for (lxb_dom_node_t* child = ((lxb_dom_node_t*)container)->first_child; child; child = lxb_dom_node_next(child)) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            YGNodeRef childYoga = YGNodeNew();

            lxb_dom_element_t* el = (lxb_dom_element_t*)child;

            // Get style attribute string
            size_t style_len = 0;
            const lxb_char_t* style_val = lxb_dom_element_get_attribute(el, (const lxb_char_t*)"style", 5, &style_len);
            if (style_val != nullptr) {
                std::string styleStr((const char*)style_val, style_len);

                if (styleStr.find("flex:2") != std::string::npos) {
                    YGNodeStyleSetFlex(childYoga, 2.0f);
                } else if (styleStr.find("flex:1") != std::string::npos) {
                    YGNodeStyleSetFlex(childYoga, 1.0f);
                }
            } else {
                YGNodeStyleSetFlex(childYoga, 1.0f);
            }

            YGNodeInsertChild(yoga_root, childYoga, YGNodeGetChildCount(yoga_root));
            yogaNodes[child] = childYoga;
        }
    }

    // Calculate Yoga layout
    YGNodeCalculateLayout(yoga_root, 800, 600, YGDirectionLTR);

    // Setup Skia surface
    int W = 800, H = 600;
    SkImageInfo info = SkImageInfo::MakeN32Premul(W, H);
    sk_sp<SkSurface> surface = SkSurfaces::Raster(info);
    if (!surface) {
        fprintf(stderr, "Failed to create Skia surface\n");
        YGNodeFree(yoga_root);
        lxb_html_document_destroy(document);
        return 1;
    }
    SkCanvas* canvas = surface->getCanvas();
    canvas->clear(SK_ColorWHITE);

    // Store global pointer for JS drawing functions
    g_skiaCanvas = canvas;

    // Draw boxes with Yoga layout
    for (lxb_dom_node_t* child = ((lxb_dom_node_t*)container)->first_child; child; child = lxb_dom_node_next(child)) {
        if (child->type == LXB_DOM_NODE_TYPE_ELEMENT) {
            YGNodeRef yoga_node = yogaNodes[child];
            float left = YGNodeLayoutGetLeft(yoga_node);
            float top = YGNodeLayoutGetTop(yoga_node);
            float width = YGNodeLayoutGetWidth(yoga_node);
            float height = YGNodeLayoutGetHeight(yoga_node);

            lxb_dom_element_t* el = (lxb_dom_element_t*)child;

            // Get background-color from style attribute
            size_t style_len = 0;
            const lxb_char_t* style_val = lxb_dom_element_get_attribute(el, (const lxb_char_t*)"style", 5, &style_len);
            SkColor color = SK_ColorGRAY; // Default color

            if (style_val != nullptr) {
                std::string styleStr((const char*)style_val, style_len);

                if (styleStr.find("background-color:#FF0000") != std::string::npos)
                    color = SK_ColorRED;
                else if (styleStr.find("background-color:#00FF00") != std::string::npos)
                    color = SK_ColorGREEN;
                else if (styleStr.find("background-color:#0000FF") != std::string::npos)
                    color = SK_ColorBLUE;
                else
                    color = SK_ColorLTGRAY;
            }

            SkPaint paint;
            paint.setColor(color);
            paint.setStyle(SkPaint::kFill_Style);

            canvas->drawRect(SkRect::MakeXYWH(left, top, width, height), paint);
        }
    }

    // Setup QuickJS runtime and context
    JSRuntime* rt = JS_NewRuntime();
    JSContext* ctx = JS_NewContext(rt);
    JSValue global_obj = JS_GetGlobalObject(ctx);

    // Register getCanvasById global function
    JSValue getCanvasById = JS_NewCFunction(ctx, js_getCanvasById, "getCanvasById", 1);
    JS_SetPropertyStr(ctx, global_obj, "getCanvasById", getCanvasById);

    // Example JS code that draws a circle on the canvas:
    const char* js_code = R"JS(
      let canvas = getCanvasById("mycanvas");
      let ctx = canvas.getContext("2d");
      ctx.arc(200, 100, 50);
      ctx.fill();
    )JS";

    JSValue result = JS_Eval(ctx, js_code, strlen(js_code), "<input>", JS_EVAL_TYPE_GLOBAL);
    if (JS_IsException(result)) {
        fprintf(stderr, "JS evaluation failed\n");
        JSValue exception = JS_GetException(ctx);
        const char* err_str = JS_ToCString(ctx, exception);
        fprintf(stderr, "Exception: %s\n", err_str);
        JS_FreeCString(ctx, err_str);
        JS_FreeValue(ctx, exception);
    }
    JS_FreeValue(ctx, result);

    // Cleanup QuickJS
    JS_FreeValue(ctx, getCanvasById);
    JS_FreeValue(ctx, global_obj);
    JS_FreeContext(ctx);
    JS_FreeRuntime(rt);

    // Save output to PNG
    sk_sp<SkImage> image = surface->makeImageSnapshot();
    if (!image) {
        fprintf(stderr, "Failed to create image snapshot\n");
        YGNodeFree(yoga_root);
        lxb_html_document_destroy(document);
        return 1;
    }
    SkPngEncoder::Options options;
    options.fZLibLevel = 9;
    SkFILEWStream stream("output.png");
    SkPixmap pixmap;
    if (!image->peekPixels(&pixmap)) {
        fprintf(stderr, "Failed to peek pixels from image\n");
        YGNodeFree(yoga_root);
        lxb_html_document_destroy(document);
        return 1;
    }
    if (!SkPngEncoder::Encode(&stream, pixmap, options)) {
        fprintf(stderr, "Failed to encode image to PNG\n");
        YGNodeFree(yoga_root);
        lxb_html_document_destroy(document);
        return 1;
    }

    // Cleanup Yoga nodes
    for (auto& kv : yogaNodes) {
        YGNodeFree(kv.second);
    }

    // Cleanup Lexbor document
    lxb_html_document_destroy(document);

    return 0;
}
