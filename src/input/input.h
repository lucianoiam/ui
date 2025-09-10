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

// Platform-agnostic dispatcher API (instance-based)
namespace input {
using Listener = std::function<void(const InputEvent&)>;

class InputManager {
 public:
   explicit InputManager(std::shared_ptr<dom::Document> doc) : doc_(std::move(doc))
   {
   }

   void setDocument(std::shared_ptr<dom::Document> doc)
   {
      doc_ = std::move(doc);
   }

   void feed(const InputEvent& ev); // platform layer calls this
   dom::Element* hitTest(int x, int y);

 private:
   std::shared_ptr<dom::Document> doc_;
};

} // namespace input
