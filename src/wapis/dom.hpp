// dom.hpp - C++ DOM interface (W3C/WHATWG-inspired)
#pragma once
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

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

    // --- Core DOM methods ---
    virtual std::shared_ptr<Node> appendChild(std::shared_ptr<Node> child);
    virtual std::shared_ptr<Node> insertBefore(std::shared_ptr<Node> newChild, std::shared_ptr<Node> refChild);
    virtual std::shared_ptr<Node> removeChild(std::shared_ptr<Node> child);
    virtual std::shared_ptr<Node> replaceChild(std::shared_ptr<Node> newChild, std::shared_ptr<Node> oldChild);
    virtual std::shared_ptr<Node> cloneNode(bool deep = false) const;
    virtual bool contains(std::shared_ptr<Node> other) const;
    virtual bool hasChildNodes() const;

    // --- Properties ---
    std::shared_ptr<Node> firstChild() const;
    std::shared_ptr<Node> lastChild() const;
    std::shared_ptr<Node> nextSibling() const;
    std::shared_ptr<Node> previousSibling() const;

    virtual ~Node() = default;
};

class Element : public Node {
public:
    std::unordered_map<std::string, std::string> attributes;
    std::string tagName;

    // --- Element methods ---
    void setAttribute(const std::string& name, const std::string& value);
    std::string getAttribute(const std::string& name) const;
    void removeAttribute(const std::string& name);

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
