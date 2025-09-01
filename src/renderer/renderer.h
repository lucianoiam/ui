#pragma once
#include "dom_observer.h"
#include <unordered_map>
#include <memory>
#include <functional>

namespace dom { class Element; }

struct RenderLayer {
    dom::Element* element = nullptr; // non-owning
    bool dirtyStyle = true;
    bool dirtyChildren = true;
};

class Renderer : public DomObserver {
public:
    Renderer() = default;
    ~Renderer() override = default;

    void onElementCreated(dom::Element* el) override;
    void onElementRemoved(dom::Element* el) override;
    void onAttributeChanged(dom::Element* el, const std::string& name, const std::string& oldValue, const std::string& newValue) override;
    void onChildListChanged(dom::Element* el) override;

    void frame(); // naive full pass over dirty layers
    void scheduleFrame(); // request an async frame (coalesced)
    void forEachLayer(const std::function<void(RenderLayer*)>& cb); // safe iteration

private:
    RenderLayer* ensureLayer(dom::Element* el);
    std::unordered_map<dom::Element*, std::unique_ptr<RenderLayer>> layers_;
    bool framePending_ = false;
};

// Global singleton accessor (simple for now)
Renderer* renderer_get();
void renderer_init();
// Iterate over all layers (no guarantees on order)
void renderer_for_each_layer(const std::function<void(RenderLayer*)>& cb);
// Convenience external request (safe if renderer not inited)
void renderer_request_frame();
