// Yoga layout integration
#include "layout_yoga.h"
#include "renderer/css_parser.h"
#include "renderer/element_data.h"
#include "wapis/dom.hpp"
#include "wapis/dom_adapter.h"
#include <functional>
#include <lexbor/css/syntax/tokenizer.h>
#include <memory>
#include <string>
#include <unordered_map>
#include <yoga/Yoga.h>

// We hide direct access; adapter provides a helper we expose via a thin accessor.
// Declare accessor (implemented in dom_adapter.cpp via a small addition) that returns C++ node for a JS value.
extern "C" void* dom_get_cpp_node_opaque(JSContext* ctx, JSValueConst v);

static bool g_layout_dirty = true;

void layout_mark_dirty()
{
   g_layout_dirty = true;
}

namespace dom {
void layout_mark_dirty()
{
   ::layout_mark_dirty();
}
} // namespace dom

// Ensure per-document hooks are installed (idempotent)
static void ensure_layout_hooks(dom::Document* doc)
{
   if (!doc)
      return;
   if (!doc->getAttributeHook()) {
      doc->setAttributeHook(+[](dom::Element* el, const std::string& name, const std::string& value) {
         (void)value;
         if (name == "style") {
            mark_style_dirty(el);
         }
      });
   }
   if (!doc->getMutationHook()) {
      doc->setMutationHook(+[](dom::Node* target, const char* op, dom::Node* related) {
         // Any structural change marks layout dirty.
         if (target && target->nodeType == dom::NodeType::ELEMENT) {
            mark_layout_dirty(static_cast<dom::Element*>(target));
         }
         if (op && std::string(op) == "remove") {
            // If an element subtree is being removed, free attachments recursively.
            std::function<void(dom::Node*)> recurse = [&](dom::Node* n) {
               if (!n) {
                  return;
               }
               if (n->nodeType == dom::NodeType::ELEMENT) {
                  free_render_data(static_cast<dom::Element*>(n));
               }
               for (auto& c : n->childNodes) {
                  if (c) {
                     recurse(c.get());
                  }
               }
            };
            if (related) {
               recurse(related);
            }
         }
      });
   }
}

bool layout_get_box(dom::Element* el, int& x, int& y, int& w, int& h)
{
   if (auto* rd = get_render_data(el)) {
      x = (int)rd->layoutX;
      y = (int)rd->layoutY;
      w = (int)rd->layoutW;
      h = (int)rd->layoutH;
      return true;
   }
   return false;
}

struct Box {
   float layoutX, layoutY, layoutW, layoutH;
};

// TempFlexStore removed (flex meta persisted in attachment)

// Ensure style parsed & cached in attachment; apply style to Yoga node; store flex meta back into attachment
static void apply_node_style(dom::Element* el, YGNodeRef node)
{
   if (!el) {
      return;
   }
   auto* rd = ensure_render_data(el);
   if (!rd) {
      return;
   }
   if (rd->dirtyFlags & 1) { // style dirty
      rd->parsedStyle.clear();
      auto pd = css::parse_inline(el->styleCssText);
      rd->parsedStyle = pd.kv;
      rd->dirtyFlags &= ~1u; // clear style dirty
   }
   auto find = [&](const char* k) -> std::string {
      auto it = rd->parsedStyle.find(k);
      return it == rd->parsedStyle.end() ? std::string() : it->second;
   };
   if (std::getenv("LAYOUT_DEBUG") && !el->styleCssText.empty()) {
      fprintf(stderr, "[layout] raw style: '%s'\n", el->styleCssText.c_str());
      for (auto& kv : rd->parsedStyle) {
         fprintf(stderr, "[layout]   decl %s = %s\n", kv.first.c_str(), kv.second.c_str());
      }
   }
   std::string display = find("display");
   if (display == "flex") {
      YGNodeStyleSetDisplay(node, YGDisplayFlex);
   }
   std::string flexdir = find("flex-direction");
   if (!flexdir.empty()) {
      if (flexdir == "row") {
         YGNodeStyleSetFlexDirection(node, YGFlexDirectionRow);
      }
      else if (flexdir == "row-reverse") {
         YGNodeStyleSetFlexDirection(node, YGFlexDirectionRowReverse);
      }
      else if (flexdir == "column-reverse") {
         YGNodeStyleSetFlexDirection(node, YGFlexDirectionColumnReverse);
      }
      else {
         YGNodeStyleSetFlexDirection(node, YGFlexDirectionColumn);
      }
      if (display != "flex" && std::getenv("LAYOUT_DEBUG")) {
         fprintf(stderr,
                 "[layout][warn] flex-direction specified without display:flex (parsed display='%s') raw='%s'\n",
                 display.c_str(), el->styleCssText.c_str());
      }
   }
   else {
      YGNodeStyleSetFlexDirection(node, YGFlexDirectionColumn); // default
   }
   if (std::getenv("LAYOUT_DEBUG") && display == "flex") {
      YGFlexDirection dir = YGNodeStyleGetFlexDirection(node);
      const char* dirStr = dir == YGFlexDirectionRow          ? "row"
                           : dir == YGFlexDirectionRowReverse ? "row-reverse"
                           : dir == YGFlexDirectionColumn     ? "column"
                                                              : "column-reverse";
      fprintf(stderr, "[layout] apply_node_style display:flex dir=%s raw='%s'\n", dirStr, flexdir.c_str());
   }
   std::string flexv = find("flex");
   if (!flexv.empty()) {
      css::FlexShorthand fp = css::parse_flex(flexv);
      if (fp.haveGrow) {
         YGNodeStyleSetFlexGrow(node, fp.grow);
      }
      if (fp.haveShrink) {
         YGNodeStyleSetFlexShrink(node, fp.shrink);
      }
      if (fp.basisPercent) {
         YGNodeStyleSetFlexBasisPercent(node, fp.basisValue);
      }
      else if (fp.basisPoint) {
         YGNodeStyleSetFlexBasis(node, fp.basisValue);
      }
      else if (fp.basisAuto) {
         YGNodeStyleSetFlexBasisAuto(node);
      }
   }
   auto widthv = find("width");
   if (!widthv.empty()) {
      float num;
      std::string unit;
      if (css::parse_number_unit(widthv, num, unit) && (unit == "px" || unit.empty())) {
         if (num >= 0) {
            YGNodeStyleSetWidth(node, num);
         }
      }
   }
   auto heightv = find("height");
   if (!heightv.empty()) {
      float num;
      std::string unit;
      if (css::parse_number_unit(heightv, num, unit) && (unit == "px" || unit.empty())) {
         if (num >= 0) {
            YGNodeStyleSetHeight(node, num);
         }
      }
   }
   rd->isFlex = (display == "flex");
   rd->flexGrow = YGNodeStyleGetFlexGrow(node);
   rd->flexShrink = YGNodeStyleGetFlexShrink(node);
   YGValue basis = YGNodeStyleGetFlexBasis(node);
   rd->flexBasisPercent = (basis.unit == YGUnitPercent);
   rd->flexBasisAuto = (basis.unit == YGUnitAuto);
   if (basis.unit == YGUnitPercent || basis.unit == YGUnitPoint) {
      rd->flexBasis = basis.value;
   }
   else if (basis.unit == YGUnitAuto) {
      rd->flexBasis = 0;
   }
   rd->flexDirection = YGNodeStyleGetFlexDirection(node);
}

static YGNodeRef ensure_yoga_node(dom::Element* el)
{
   auto* rd = ensure_render_data(el);
   if (!rd) {
      return nullptr;
   }
   if (!rd->yogaNode) {
      rd->yogaNode = YGNodeNew();
      rd->dirtyFlags |= 1;
   }
   return (YGNodeRef)rd->yogaNode;
}

static void sync_subtree(dom::Element* el)
{
   if (!el) {
      return;
   }
   YGNodeRef node = ensure_yoga_node(el);
   if (!node) {
      return;
   }
   auto* rd = get_render_data(el);
   if (rd && (rd->dirtyFlags & 1)) {
      apply_node_style(el, node); // style update
   }
   std::vector<dom::Element*> desired;
   desired.reserve(el->childNodes.size());
   for (auto& c : el->childNodes) {
      if (c && c->nodeType == dom::NodeType::ELEMENT) {
         desired.push_back(static_cast<dom::Element*>(c.get()));
      }
   }
   bool mismatch = false;
   uint32_t existing = YGNodeGetChildCount(node);
   uint32_t common = std::min<uint32_t>(existing, desired.size());
   for (uint32_t i = 0; i < common && !mismatch; i++) {
      if (YGNodeGetChild(node, i) != (YGNodeRef)ensure_yoga_node(desired[i])) {
         mismatch = true;
      }
   }
   if (existing != desired.size()) {
      mismatch = true;
   }
   if (mismatch) {
      for (int i = (int)existing - 1; i >= 0; --i) {
         YGNodeRemoveChild(node, YGNodeGetChild(node, (uint32_t)i));
      }
      for (auto* ce : desired) {
         YGNodeInsertChild(node, ensure_yoga_node(ce), YGNodeGetChildCount(node));
      }
   }
   for (auto* ce : desired) {
      sync_subtree(ce);
   }
}

static void apply_layout_recursive(dom::Element* el, YGNodeRef node, float accL = 0, float accT = 0)
{
   if (!el || !node) {
      return;
   }
   float relL = YGNodeLayoutGetLeft(node);
   float relT = YGNodeLayoutGetTop(node);
   float absL = accL + relL;
   float absT = accT + relT;
   float w = YGNodeLayoutGetWidth(node);
   float h = YGNodeLayoutGetHeight(node);
   if (auto* rd = ensure_render_data(el)) {
      rd->layoutX = absL;
      rd->layoutY = absT;
      rd->layoutW = w;
      rd->layoutH = h;
   }
   if (std::getenv("LAYOUT_DEBUG")) {
      fprintf(stderr, "[layout] el=%p tag=%s box=(%.0f,%.0f %.0fx%.0f) rel=(%.0f,%.0f) acc=(%.0f,%.0f)\n", (void*)el,
              el->tagName.c_str(), absL, absT, w, h, relL, relT, accL, accT);
   }
   // recurse with updated accumulated offset
   uint32_t childIdx = 0;
   for (auto& c : el->childNodes) {
      if (c && c->nodeType == dom::NodeType::ELEMENT) {
         apply_layout_recursive(static_cast<dom::Element*>(c.get()), YGNodeGetChild(node, childIdx++), absL, absT);
      }
   }
}

void layout_maybe_run(JSContext* ctx)
{
   if (!ctx) {
      return;
   }
   static bool in_layout = false;
   if (in_layout) {
      return; // avoid reentrant invocation
   }
   in_layout = true;
   if (!g_layout_dirty) {
      in_layout = false;
      return;
   }
   g_layout_dirty = false;
   // flex meta now persisted in attachments, nothing transient to clear
   // Get body element
   JSValue global = JS_GetGlobalObject(ctx);
   JSValue document = JS_GetPropertyStr(ctx, global, "document");
   JSValue body = JS_GetPropertyStr(ctx, document, "body");
   auto bodyNode = reinterpret_cast<dom::Node*>(dom_get_cpp_node_opaque(ctx, (JSValueConst)body));
   // Install hooks on the owning document the first time we see it
   if (bodyNode) {
      if (auto docSP = bodyNode->ownerDocument.lock()) {
         if (auto d = std::dynamic_pointer_cast<dom::Document>(docSP)) {
            ensure_layout_hooks(d.get());
         }
      }
   }
   if (!bodyNode || bodyNode->nodeType != dom::NodeType::ELEMENT) {
      JS_FreeValue(ctx, body);
      JS_FreeValue(ctx, document);
      JS_FreeValue(ctx, global);
      return;
   }
   auto bodyEl = static_cast<dom::Element*>(bodyNode);
   // Treat the first element child of body (if any) as the layout root so its flex styles always map to the viewport.
   dom::Element* layoutRootEl = bodyEl;
   for (auto& c : bodyEl->childNodes) {
      if (c && c->nodeType == dom::NodeType::ELEMENT) {
         layoutRootEl = static_cast<dom::Element*>(c.get());
         break;
      }
   }

   YGNodeRef root = ensure_yoga_node(layoutRootEl);
   apply_node_style(layoutRootEl, root);
   // Force viewport size on root flex container using centralized defaults (will adapt when dynamic resize added)
   extern int g_winW;
   extern int g_winH; // from viewport.h / main.mm
   YGNodeStyleSetWidth(root, (float)g_winW);
   YGNodeStyleSetHeight(root, (float)g_winH);
   // Sync subtree (structure + styles)
   sync_subtree(layoutRootEl);
   YGNodeCalculateLayout(root, YGUndefined, YGUndefined, YGDirectionLTR);
   // Apply to layoutRootEl and descendants (single pass). Root is at (0,0).
   apply_layout_recursive(layoutRootEl, root, 0, 0);
   for_each_render_data([](dom::Element* e, DomElementRenderData* rd) {
      (void)e;
      if (rd) {
         rd->dirtyFlags &= ~2u;
      }
   });
   // If layout root isn't body, set body box to viewport for compositor fallback
   if (layoutRootEl != bodyEl) {
      extern int g_winW;
      extern int g_winH;
      if (auto* rd = ensure_render_data(bodyEl)) {
         rd->layoutX = 0;
         rd->layoutY = 0;
         rd->layoutW = g_winW;
         rd->layoutH = g_winH;
      }
   }
   // Persist Yoga nodes; no freeing here
   // Diagnostic grouped logging
   if (std::getenv("LAYOUT_DEBUG")) {
      fprintf(stderr, "[layout] === BOXES ===\n");
      for_each_render_data([](dom::Element* el, DomElementRenderData* rd) {
         if (!rd) {
            return;
         }
         const char* dirStr = "-";
         if (rd->isFlex) {
            int d = rd->flexDirection;
            dirStr = d == YGFlexDirectionRow          ? "row"
                     : d == YGFlexDirectionRowReverse ? "row-rev"
                     : d == YGFlexDirectionColumn     ? "col"
                                                      : "col-rev";
         }
         fprintf(stderr, "[layout] box el=%p pos=(%.0f,%.0f) size=(%.0f x %.0f) flex=%d grow=%.2f dir=%s\n", (void*)el,
                 rd->layoutX, rd->layoutY, rd->layoutW, rd->layoutH, rd->isFlex ? 1 : 0, rd->flexGrow, dirStr);
      });
      // Row groups
      for_each_render_data([](dom::Element* parent, DomElementRenderData* pRD) {
         if (!pRD || !pRD->isFlex) {
            return;
         }
         if (!(pRD->flexDirection == YGFlexDirectionRow || pRD->flexDirection == YGFlexDirectionRowReverse)) {
            return;
         }
         float pbw = pRD->layoutW;
         std::vector<std::pair<dom::Element*, Box>> children;
         for (auto& c : parent->childNodes) {
            if (c && c->nodeType == dom::NodeType::ELEMENT) {
               auto* ce = static_cast<dom::Element*>(c.get());
               if (auto* crd = get_render_data(ce)) {
                  Box b{crd->layoutX, crd->layoutY, crd->layoutW, crd->layoutH};
                  children.push_back({ce, b});
               }
            }
         }
         if (children.empty()) {
            return;
         }
         fprintf(stderr, "[layout] ROW parent=%p totalW=%.0f children=%zu\n", (void*)parent, pbw, children.size());
         float sumGrow = 0;
         for (auto& ch : children) {
            if (auto* crd = get_render_data(ch.first)) {
               sumGrow += crd->flexGrow;
            }
         }
         for (auto& ch : children) {
            if (auto* crd = get_render_data(ch.first)) {
               float gw = crd->flexGrow;
               fprintf(stderr, "  child=%p w=%.0f grow=%.2f ratio=%.2f%% (expected%%=%.2f)\n", (void*)ch.first,
                       ch.second.layoutW, gw, pbw > 0 ? (ch.second.layoutW / pbw * 100.f) : 0.f,
                       (sumGrow > 0 ? (gw / sumGrow * 100.f) : 0.f));
            }
         }
      });
   }
   JS_FreeValue(ctx, body);
   JS_FreeValue(ctx, document);
   JS_FreeValue(ctx, global);
   extern void native_request_composite(JSContext*);
   native_request_composite(ctx);
   in_layout = false;
}

// (Batching removed for simplicity/robustness)
