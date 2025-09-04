// internal_render_data.h - engine-specific per-element attachment (opaque to generic DOM)
#pragma once
#include <unordered_map>
#include <string>
struct DomElementRenderData {
    std::unordered_map<std::string,std::string> parsedStyle;
    void* yogaNode = nullptr;
    float layoutX=0, layoutY=0, layoutW=0, layoutH=0;
    int surfaceId = 0;
    unsigned dirtyFlags = 0; // 1=style,2=layout,4=paint
    uint32_t styleVersion = 0;
};
