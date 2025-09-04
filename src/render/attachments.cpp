#include "attachments.h"
#include <cassert>
#include <yoga/Yoga.h>

static std::unordered_map<dom::Element*, std::unique_ptr<DomElementRenderData>> g_renderData;

DomElementRenderData* ensure_render_data(dom::Element* el) {
  if (!el)
    return nullptr;
  auto it = g_renderData.find(el);
  if (it == g_renderData.end()) {
    auto ptr = std::make_unique<DomElementRenderData>();
    auto raw = ptr.get();
    // New attachments start with style dirty so first layout parses style into cache
    raw->dirtyFlags |= 1; // style dirty
    g_renderData[el] = std::move(ptr);
    el->data = raw; // store opaque pointer
    return raw;
  }
  el->data = it->second.get();
  return it->second.get();
}
DomElementRenderData* get_render_data(dom::Element* el) {
  if (!el)
    return nullptr;
  auto it = g_renderData.find(el);
  if (it == g_renderData.end())
    return nullptr;
  return it->second.get();
}
void free_render_data(dom::Element* el) {
  if (!el)
    return;
  auto it = g_renderData.find(el);
  if (it != g_renderData.end()) {
    if (it->second->yogaNode) {
      YGNodeRef n = (YGNodeRef)it->second->yogaNode;
      YGNodeFreeRecursive(n);
      it->second->yogaNode = nullptr;
    }
    g_renderData.erase(it);
  }
  el->data = nullptr;
}
void release_all_render_data() {
  for (auto& kv : g_renderData) {
    if (kv.second->yogaNode) {
      YGNodeRef n = (YGNodeRef)kv.second->yogaNode;
      YGNodeFreeRecursive(n);
      kv.second->yogaNode = nullptr;
    }
  }
  g_renderData.clear();
}
extern void layout_mark_dirty();
void mark_style_dirty(dom::Element* el) {
  if (auto* rd = ensure_render_data(el)) {
    rd->dirtyFlags |= 1;
    rd->dirtyFlags |= 2;
    rd->styleVersion++;
    layout_mark_dirty();
  }
}
void mark_layout_dirty(dom::Element* el) {
  if (auto* rd = ensure_render_data(el)) {
    rd->dirtyFlags |= 2;
    layout_mark_dirty();
  }
}
void for_each_render_data(const std::function<void(dom::Element*, DomElementRenderData*)>& fn) {
  for (auto& kv : g_renderData)
    fn(kv.first, kv.second.get());
}
