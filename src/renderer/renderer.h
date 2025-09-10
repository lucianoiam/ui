#pragma once
#include "dom_observer.h"
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace dom {
class Element;
}

struct RenderLayer {
   dom::Element* element = nullptr; // non-owning
   bool dirtyStyle = true;
   bool dirtyChildren = true;
   int depth = 0;
   uint64_t createOrder = 0; // monotonic for stable ordering among siblings
};

class Renderer : public dom::DomObserver {
 public:
   Renderer() = default;
   ~Renderer() override = default;

   void onElementCreated(dom::Element* el) override;
   void onElementRemoved(dom::Element* el) override;
   void onAttributeChanged(dom::Element* el, const std::string& name, const std::string& oldValue,
                           const std::string& newValue) override;
   void onChildListChanged(dom::Element* el) override;

   void frame();                                                   // naive full pass over dirty layers
   void scheduleFrame();                                           // request an async frame (coalesced)
   void forEachLayer(const std::function<void(RenderLayer*)>& cb); // safe iteration

 private:
   RenderLayer* ensureLayer(dom::Element* el);
   std::unordered_map<dom::Element*, std::unique_ptr<RenderLayer>> layers_; // primary lookup
   std::vector<RenderLayer*> ordered_;                                      // cached ordered list
   bool orderDirty_ = true;
   uint64_t nextCreateOrder_ = 1;
   bool framePending_ = false;
};

// Instantiate and manage a Renderer in the embedding app.
