// dom.hpp - C++ DOM interface (W3C/WHATWG-inspired)
#pragma once
#include <string>
#include <vector>
#include <memory> // for std::enable_shared_from_this
#include <unordered_map>
#include <atomic>

namespace dom {

enum class NodeType {
    ELEMENT = 1,
    TEXT = 3,
    DOCUMENT = 9
};

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

    // --- textContent convenience (moved from adapter) ---
    virtual std::string textContent() const;            // Concatenate descendant text nodes
    virtual void setTextContent(const std::string& v);  // Replace children (or value for Text)

    // --- Inner HTML (basic serialization / parsing stub) ---
    virtual std::string innerHTML() const;              // Serialize children (very minimal)
    virtual void setInnerHTML(const std::string& html); // Replace children from simple HTML/text (stub)
    virtual std::string outerHTML() const;              // Serialize this node including its tag if Element

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

    // --- Element methods ---
    void setAttribute(const std::string& name, const std::string& value);
    std::string getAttribute(const std::string& name) const;
    void removeAttribute(const std::string& name);

    // Convenience DOM-style accessors
    std::string className() const { return getAttribute("class"); }
    void setClassName(const std::string& v) { setAttribute("class", v); }
    const std::string& getStyleCssText() const { return styleCssText; }
    void setStyleCssText(const std::string& v) { styleCssText = v; }
    std::string serializeOpenTag() const; // helper for HTML

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
};

// Factory helpers
std::shared_ptr<Document> createDocument();

} // namespace dom
