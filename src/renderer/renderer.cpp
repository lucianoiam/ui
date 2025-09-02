#include "renderer.h"
#include <cstdio>
#include "scheduler.h"
#include "wapis/dom.hpp"
extern void layout_mark_dirty();

namespace {
static Renderer* g_renderer = nullptr;
}

Renderer* renderer_get() { return g_renderer; }

void renderer_init() {
    if (!g_renderer) {
        g_renderer = new Renderer();
        dom_register_observer(g_renderer);
    }
}

RenderLayer* Renderer::ensureLayer(dom::Element* el) {
    if (!el) return nullptr;
    auto it = layers_.find(el);
    if (it != layers_.end()) return it->second.get();
    auto rl = std::make_unique<RenderLayer>();
    rl->element = el;
    rl->dirtyStyle = rl->dirtyChildren = true;
    rl->createOrder = nextCreateOrder_++;
    auto* ptr = rl.get();
    layers_[el] = std::move(rl);
    orderDirty_ = true;
    return ptr;
}

void Renderer::onElementCreated(dom::Element* el) {
    ensureLayer(el); // allocate layer lazily
    layout_mark_dirty();
}
void Renderer::onElementRemoved(dom::Element* el) {
    layers_.erase(el);
    orderDirty_ = true;
    layout_mark_dirty();
}
void Renderer::onAttributeChanged(dom::Element* el, const std::string& name, const std::string& oldValue, const std::string& newValue) {
    if (name == "style" && oldValue != newValue) {
        if (auto* rl = ensureLayer(el)) rl->dirtyStyle = true;
        scheduleFrame();
        layout_mark_dirty();
    }
}
void Renderer::onChildListChanged(dom::Element* el) {
    if (auto* rl = ensureLayer(el)) { rl->dirtyChildren = true; scheduleFrame(); }
    layout_mark_dirty();
    orderDirty_ = true;
}

void Renderer::frame() {
    // Rebuild ordering if dirty
    if (orderDirty_) {
        ordered_.clear(); ordered_.reserve(layers_.size());
        for (auto &kv : layers_) ordered_.push_back(kv.second.get());
        // Assign depth by walking parent chain (simple heuristic: distance to root)
        for (auto *rl : ordered_) {
            int d=0; auto *n = rl->element; while (n && n->parentNode.lock()) { d++; n = static_cast<dom::Element*>(n->parentNode.lock().get()); }
            rl->depth = d;
        }
        std::sort(ordered_.begin(), ordered_.end(), [](RenderLayer* a, RenderLayer* b){
            if (a->depth != b->depth) return a->depth < b->depth; // parents first
            return a->createOrder < b->createOrder; // stable
        });
        orderDirty_ = false;
    }
    for (auto *rl : ordered_) {
        if (rl->dirtyStyle || rl->dirtyChildren) {
            std::fprintf(stderr, "[Renderer] repaint element=%p depth=%d style=%d children=%d\n", (void*)rl->element, rl->depth, rl->dirtyStyle, rl->dirtyChildren);
            rl->dirtyStyle = rl->dirtyChildren = false;
        }
    }
    framePending_ = false;
}

void Renderer::scheduleFrame() {
    if (framePending_) return; // coalesce
    framePending_ = true;
    scheduler_request([this]{ this->frame(); });
}

void Renderer::forEachLayer(const std::function<void(RenderLayer*)>& cb) {
    if (orderDirty_) { // ensure an order before iterating
        ordered_.clear(); ordered_.reserve(layers_.size());
        for (auto &kv : layers_) ordered_.push_back(kv.second.get());
        for (auto *rl : ordered_) {
            int d=0; auto *n = rl->element; while (n && n->parentNode.lock()) { d++; n = static_cast<dom::Element*>(n->parentNode.lock().get()); }
            rl->depth = d;
        }
        std::sort(ordered_.begin(), ordered_.end(), [](RenderLayer* a, RenderLayer* b){
            if (a->depth != b->depth) return a->depth < b->depth; return a->createOrder < b->createOrder; });
        orderDirty_ = false;
    }
    for (auto *rl : ordered_) cb(rl);
}

void renderer_for_each_layer(const std::function<void(RenderLayer*)>& cb) {
    if (!g_renderer) return;
    g_renderer->forEachLayer(cb);
}

void renderer_request_frame() { if (g_renderer) g_renderer->scheduleFrame(); }
