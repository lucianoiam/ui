// element_data.h - per-element rendering metadata (opaque to DOM)
#pragma once
#include "wapis/dom.hpp"
#include <functional>
#include <unordered_map>

struct DomElementRenderData {
   std::unordered_map<std::string, std::string> parsedStyle;
   void* yogaNode = nullptr;
   float layoutX = 0, layoutY = 0, layoutW = 0, layoutH = 0;
   int surfaceId = 0;
   unsigned dirtyFlags = 0; // 1=style,2=layout,4=paint
   uint32_t styleVersion = 0;
   bool isFlex = false;
   float flexGrow = 0.f;
   float flexShrink = 1.f;
   float flexBasis = 0.f;
   bool flexBasisPercent = false;
   bool flexBasisAuto = false;
   int flexDirection = 0; // YGFlexDirection* enum value stored as int to avoid header include
};

DomElementRenderData* ensure_render_data(dom::Element* el);
DomElementRenderData* get_render_data(dom::Element* el);
void free_render_data(dom::Element* el);
void release_all_render_data();
void mark_style_dirty(dom::Element* el);
void mark_layout_dirty(dom::Element* el);
// Iterate all element -> render data pairs (diagnostics / bulk operations)
void for_each_render_data(const std::function<void(dom::Element*, DomElementRenderData*)>& fn);
