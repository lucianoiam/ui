#include "input.h"
#include "wapis/dom.hpp"
#include <algorithm>
#include <cstdio>
#include <unordered_map>

namespace input {
static std::weak_ptr<dom::Document> g_doc;

void init(std::shared_ptr<dom::Document> doc)
{
   g_doc = doc;
}

void shutdown()
{
   g_doc.reset();
}

// Naive style parser helpers
static int parseDecl(const std::string& style, const char* key)
{
   size_t p = style.find(key);
   if (p == std::string::npos)
      return -1;
   size_t c = style.find(':', p);
   size_t sc = style.find(';', c);
   if (c == std::string::npos)
      return -1;
   std::string num = style.substr(c + 1, sc == std::string::npos ? std::string::npos : sc - (c + 1));
   try {
      return std::stoi(num);
   }
   catch (...) {
      return -1;
   }
}

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

dom::Element* hitTest(int x, int y)
{
   auto doc = g_doc.lock();
   if (!doc)
      return nullptr;
   // breadth-first collection of all elements
   std::vector<dom::Element*> els;
   for (auto& c : doc->childNodes)
      if (c && c->nodeType == dom::NodeType::ELEMENT)
         collectElementsWith((dom::Element*)c.get(), els);
   // iterate in insertion order treat later elements as on top
   for (auto it = els.rbegin(); it != els.rend(); ++it) {
      dom::Element* el = *it;
      auto itAttr = el->attributes.find("style");
      if (itAttr == el->attributes.end())
         continue;
      const std::string& s = itAttr->second;
      int left = parseDecl(s, "left");
      int top = parseDecl(s, "top");
      int w = parseDecl(s, "width");
      if (w < 0)
         w = 64;
      int h = parseDecl(s, "height");
      if (h < 0)
         h = 64;
      if (x >= left && x <= left + w && y >= top && y <= top + h)
         return el;
   }
   return nullptr;
}

void feed(const InputEvent& ev)
{
   // Future: queue, coalesce. For now: no-op (dispatch will happen from platform layer in JS binding)
   (void)ev;
}

} // namespace input
