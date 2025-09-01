#include "renderer.h"
#include <cstdio>
#include "scheduler.h"

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
    auto* ptr = rl.get();
    layers_[el] = std::move(rl);
    return ptr;
}

void Renderer::onElementCreated(dom::Element* el) {
    ensureLayer(el); // allocate layer lazily
}
void Renderer::onElementRemoved(dom::Element* el) {
    layers_.erase(el);
}
void Renderer::onAttributeChanged(dom::Element* el, const std::string& name, const std::string& oldValue, const std::string& newValue) {
    if (name == "style" && oldValue != newValue) {
        if (auto* rl = ensureLayer(el)) rl->dirtyStyle = true;
        scheduleFrame();
    }
}
void Renderer::onChildListChanged(dom::Element* el) {
    if (auto* rl = ensureLayer(el)) { rl->dirtyChildren = true; scheduleFrame(); }
}

void Renderer::frame() {
    // Unoptimized: iterate all layers and “repaint” dirty ones
    for (auto &kv : layers_) {
        RenderLayer* rl = kv.second.get();
        if (rl->dirtyStyle || rl->dirtyChildren) {
            // Placeholder: future Skia paint calls.
            std::fprintf(stderr, "[Renderer] repaint element=%p style=%d children=%d\n", (void*)rl->element, rl->dirtyStyle, rl->dirtyChildren);
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
    for (auto &kv : layers_) cb(kv.second.get());
}

void renderer_for_each_layer(const std::function<void(RenderLayer*)>& cb) {
    if (!g_renderer) return;
    g_renderer->forEachLayer(cb);
}

void renderer_request_frame() { if (g_renderer) g_renderer->scheduleFrame(); }
