#include "input.h"
#include "renderer/css_parser.h"
#include "wapis/dom.hpp"
#include <algorithm>
#include <cstdio>
#include <unordered_map>

namespace input {

static void collectElementsWith(dom::Element* root, std::vector<dom::Element*>& out)
{
   if (!root)
      return;
   out.push_back(root);
   for (auto& c : root->childNodes) {
      if (c && c->nodeType == dom::NodeType::ELEMENT)
         collectElementsWith((dom::Element*)c.get(), out);
   }
}

dom::Element* InputManager::hitTest(int x, int y)
{
   auto doc = doc_;
   if (!doc)
      return nullptr;
   // breadth-first collection of all elements
   std::vector<dom::Element*> els;
   for (auto& c : doc->childNodes)
      if (c && c->nodeType == dom::NodeType::ELEMENT)
         collectElementsWith((dom::Element*)c.get(), els);
   // iterate in insertion order; later elements are on top
   for (auto it = els.rbegin(); it != els.rend(); ++it) {
      dom::Element* el = *it;
      const std::string& cssText = el->styleCssText;
      auto decls = css::parse_inline(cssText);
      auto get_px = [&](const char* prop, int defv) {
         auto it = decls.kv.find(prop);
         if (it == decls.kv.end())
            return defv;
         float v = -1.f;
         std::string unit;
         if (css::parse_number_unit(it->second, v, unit) && v >= 0 && (unit.empty() || unit == "px")) {
            return (int)std::lround(v);
         }
         return defv;
      };
      int left = get_px("left", 0);
      int top = get_px("top", 0);
      int w = get_px("width", 64);
      int h = get_px("height", 64);
      if (x >= left && x <= left + w && y >= top && y <= top + h)
         return el;
   }
   return nullptr;
}

void InputManager::feed(const InputEvent& ev)
{
   // Future: queue, coalesce. For now: no-op (dispatch will happen from platform layer in JS binding)
   (void)ev;
}

} // namespace input
