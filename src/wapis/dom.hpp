// dom.hpp - C++ DOM interface (W3C/WHATWG-inspired)
#pragma once
#include <atomic>
#include <memory> // for std::enable_shared_from_this
#include <string>
#include <unordered_map>
#include <vector>

namespace dom {

enum class NodeType { ELEMENT = 1, TEXT = 3, DOCUMENT = 9 };

// Optional engine hooks (per-Document)
class Element; class Node; class Document; class DomObserver;
using AttributeHook = void (*)(Element*, const std::string& name, const std::string& value);
using MutationHook  = void (*)(Node* target, const char* op, Node* related);

class Node : public std::enable_shared_from_this<Node> {
public:
  NodeType nodeType;
  std::string nodeName;
  std::string nodeValue;
  std::vector<std::shared_ptr<Node>> childNodes;
  std::weak_ptr<Node> parentNode;
  std::weak_ptr<Node> ownerDocument;
  uint64_t debugId = 0; // monotonic id for debugging

  // --- Core DOM methods ---
  virtual std::shared_ptr<Node> appendChild(std::shared_ptr<Node> child);
  virtual std::shared_ptr<Node> insertBefore(std::shared_ptr<Node> newChild, std::shared_ptr<Node> refChild);
  virtual std::shared_ptr<Node> removeChild(std::shared_ptr<Node> child);
  virtual std::shared_ptr<Node> replaceChild(std::shared_ptr<Node> newChild, std::shared_ptr<Node> oldChild);
  virtual std::shared_ptr<Node> cloneNode(bool deep = false) const;
  virtual bool contains(std::shared_ptr<Node> other) const;
  virtual bool hasChildNodes() const;

  // --- textContent convenience ---
  virtual std::string textContent() const;           // Concatenate descendant text nodes
  virtual void setTextContent(const std::string& v); // Replace children (or value for Text)

  // innerHTML/outerHTML are defined on Element.

  // --- Event listeners (engine-agnostic bookkeeping) ---
  void addEventListener(const std::string& type);
  void removeEventListener(const std::string& type);
  bool hasEventListener(const std::string& type) const;
  void dispatchEvent(const std::string& type); // Core dispatch (no bubbling yet)
  // --- Properties ---
  std::shared_ptr<Node> firstChild() const;
  std::shared_ptr<Node> lastChild() const;
  std::shared_ptr<Node> nextSibling() const;
  std::shared_ptr<Node> previousSibling() const;
  virtual ~Node() = default; // public for shared_ptr destruction

protected:
  std::unordered_map<std::string, size_t> listenerCounts; // type -> count
};

class Element : public Node {
public:
  std::unordered_map<std::string, std::string> attributes;
  std::string tagName;
  // Simple style store (cssText only for now)
  std::string styleCssText;

  // Optional per-node attachment for rendering/layout/state without bloating base Element.
  void* data = nullptr; // opaque engine attachment (allocated/freed by engine subsystems)

  // --- Element methods ---
  void setAttribute(const std::string& name, const std::string& value);
  std::string getAttribute(const std::string& name) const;
  void removeAttribute(const std::string& name);

#ifndef DOM_STRICT
  // Convenience DOM-style accessors (NON-STANDARD shorthands for get/setAttribute("class"))
  std::string className() const {
    return getAttribute("class");
  } // NON-STANDARD
  void setClassName(const std::string& v) {
    setAttribute("class", v);
  } // NON-STANDARD
#endif
  // innerHTML / outerHTML (basic, minimal) -- NON-STANDARD SIMPLIFIED IMPLEMENTATION
  std::string innerHTML() const;              // Serialize children (very minimal)
  void setInnerHTML(const std::string& html); // Replace children from simple HTML/text (stub)
  std::string outerHTML() const;              // Serialize this element including its tag

#ifndef DOM_EXCLUDE_STYLE_HELPERS
  const std::string& getStyleCssText() const {
    return styleCssText;
  }                                            // NON-STANDARD convenience
  void setStyleCssText(const std::string& v) { // NON-STANDARD convenience; syncs style attribute
    styleCssText = v;
    attributes["style"] = v;
    // Opaque: engine layer may hook style changes; DOM stays generic.
  }
#endif
  std::string serializeOpenTag() const; // INTERNAL NON-STANDARD helper

  // --- Query methods ---
  std::vector<std::shared_ptr<Element>> getElementsByTagName(const std::string& name) const;
  // ...add more as needed...
};

class Text : public Node {
public:
  Text(const std::string& value);
};

class Document : public Node {
public:
  Document();
  std::shared_ptr<Element> createElement(const std::string& tag);
  std::shared_ptr<Text> createTextNode(const std::string& value);
  // Monotonic debug id source (per-document, avoids globals)
  uint64_t nextDebugId();
  // Per-document observer management
  void addObserver(DomObserver*);
  void removeObserver(DomObserver*);
  const std::vector<DomObserver*>& observers() const { return observers_; }
  // Per-document DOM hooks
  void setAttributeHook(AttributeHook cb) { attrHook_ = cb; }
  AttributeHook getAttributeHook() const { return attrHook_; }
  void setMutationHook(MutationHook cb) { mutHook_ = cb; }
  MutationHook getMutationHook() const { return mutHook_; }
private:
  std::atomic<uint64_t> idCounter{1};
  std::vector<DomObserver*> observers_;
  AttributeHook attrHook_ = nullptr;
  MutationHook mutHook_ = nullptr;
};

// Factory helpers
std::shared_ptr<Document> createDocument();

} // namespace dom
