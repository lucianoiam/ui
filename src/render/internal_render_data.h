// internal_render_data.h - engine-specific per-element attachment (opaque to generic DOM)
#pragma once
#include <string>
#include <unordered_map>

struct DomElementRenderData {
   std::unordered_map<std::string, std::string> parsedStyle;
   void* yogaNode = nullptr;
   float layoutX = 0, layoutY = 0, layoutW = 0, layoutH = 0;
   int surfaceId = 0;
   unsigned dirtyFlags = 0; // 1=style,2=layout,4=paint
   uint32_t styleVersion = 0;
   // Flex / layout meta cached after style parse
   bool isFlex = false;
   float flexGrow = 0.f;
   float flexShrink = 1.f;
   float flexBasis = 0.f;
   bool flexBasisPercent = false;
   bool flexBasisAuto = false;
   int flexDirection = 0; // YGFlexDirection* enum value stored as int to avoid header include
};
