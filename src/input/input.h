#pragma once
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace dom {
class Element;
class Document;
} // namespace dom

struct InputEvent {
   std::string type; // mousedown, mousemove, mouseup
   int x = 0;
   int y = 0;
};

// Platform-agnostic dispatcher API
namespace input {
using Listener = std::function<void(const InputEvent&)>;
void init(std::shared_ptr<dom::Document> doc); // store weak ref
void shutdown();
void feed(const InputEvent& ev); // platform layer calls this
// Simple hit test over elements that registered listeners of matching type
dom::Element* hitTest(int x, int y);
} // namespace input
