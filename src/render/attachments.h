// attachments.h - engine-side per-element attachment management (opaque to DOM)
#pragma once
#include "render/internal_render_data.h"
#include "wapis/dom.hpp"
#include <functional>
#include <unordered_map>

DomElementRenderData* ensure_render_data(dom::Element* el);
DomElementRenderData* get_render_data(dom::Element* el);
void free_render_data(dom::Element* el);
void release_all_render_data();
void mark_style_dirty(dom::Element* el);
void mark_layout_dirty(dom::Element* el);
// Iterate all element -> render data pairs (diagnostics / bulk operations)
void for_each_render_data(const std::function<void(dom::Element*, DomElementRenderData*)>& fn);
