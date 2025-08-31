// dom.cpp - C++ DOM implementation (W3C/WHATWG-inspired)
#include "dom.hpp"
#include <algorithm>
#include <atomic>

static std::atomic<uint64_t> g_node_id_counter{1};

namespace dom {

// --- Node ---
std::shared_ptr<Node> Node::appendChild(std::shared_ptr<Node> child) {
    if (!child) return nullptr;
    child->parentNode = shared_from_this();
    childNodes.push_back(child);
    return child;
}

std::shared_ptr<Node> Node::insertBefore(std::shared_ptr<Node> newChild, std::shared_ptr<Node> refChild) {
    if (!newChild) return nullptr;
    auto it = std::find(childNodes.begin(), childNodes.end(), refChild);
    if (it == childNodes.end()) return appendChild(newChild);
    newChild->parentNode = shared_from_this();
    childNodes.insert(it, newChild);
    return newChild;
}

std::shared_ptr<Node> Node::removeChild(std::shared_ptr<Node> child) {
    auto it = std::find(childNodes.begin(), childNodes.end(), child);
    if (it != childNodes.end()) {
        (*it)->parentNode.reset();
        childNodes.erase(it);
        return child;
    }
    return nullptr;
}

std::shared_ptr<Node> Node::replaceChild(std::shared_ptr<Node> newChild, std::shared_ptr<Node> oldChild) {
    auto it = std::find(childNodes.begin(), childNodes.end(), oldChild);
    if (it != childNodes.end()) {
        newChild->parentNode = shared_from_this();
        (*it)->parentNode.reset();
        *it = newChild;
        return oldChild;
    }
    return nullptr;
}

std::shared_ptr<Node> Node::cloneNode(bool deep) const {
    auto clone = std::make_shared<Node>(*this);
    clone->childNodes.clear();
    if (deep) {
        for (const auto& child : childNodes) {
            clone->appendChild(child->cloneNode(true));
        }
    }
    return clone;
}

bool Node::contains(std::shared_ptr<Node> other) const {
    if (!other) return false;
    for (const auto& child : childNodes) {
        if (child == other || child->contains(other)) return true;
    }
    return false;
}

bool Node::hasChildNodes() const {
    return !childNodes.empty();
}

std::string Node::textContent() const {
    if (nodeType == NodeType::TEXT) return nodeValue;
    std::string acc;
    acc.reserve(64);
    for (auto &c : childNodes) {
        if (!c) continue;
        if (c->nodeType == NodeType::TEXT) acc += c->nodeValue; else acc += c->textContent();
    }
    return acc;
}

void Node::setTextContent(const std::string& v) {
    if (nodeType == NodeType::TEXT) { nodeValue = v; return; }
    for (auto &c : childNodes) if (c) c->parentNode.reset();
    childNodes.clear();
    if (!v.empty()) {
        if (auto doc = std::dynamic_pointer_cast<Document>(ownerDocument.lock())) {
            appendChild(doc->createTextNode(v));
        }
    }
}

// Removed Node::innerHTML / outerHTML to align closer with spec (they belong to Element)


void Node::addEventListener(const std::string& type) {
    listenerCounts[type]++;
}
void Node::removeEventListener(const std::string& type) {
    auto it = listenerCounts.find(type);
    if (it != listenerCounts.end()) {
        if (it->second > 1) --it->second; else listenerCounts.erase(it);
    }
}
bool Node::hasEventListener(const std::string& type) const {
    return listenerCounts.find(type) != listenerCounts.end();
}

void Node::dispatchEvent(const std::string& type) {
    // Placeholder: just check if any listener registered; real impl would queue tasks.
    (void)type; // no-op; adapter may integrate later.
}

std::shared_ptr<Node> Node::firstChild() const {
    return childNodes.empty() ? nullptr : childNodes.front();
}

std::shared_ptr<Node> Node::lastChild() const {
    return childNodes.empty() ? nullptr : childNodes.back();
}

std::shared_ptr<Node> Node::nextSibling() const {
    if (auto p = parentNode.lock()) {
        auto& siblings = p->childNodes;
        auto it = std::find(siblings.begin(), siblings.end(), shared_from_this());
        if (it != siblings.end() && ++it != siblings.end()) return *it;
    }
    return nullptr;
}

std::shared_ptr<Node> Node::previousSibling() const {
    if (auto p = parentNode.lock()) {
        auto& siblings = p->childNodes;
        auto it = std::find(siblings.begin(), siblings.end(), shared_from_this());
        if (it != siblings.begin() && it != siblings.end()) return *(--it);
    }
    return nullptr;
}

// --- Element ---
void Element::setAttribute(const std::string& name, const std::string& value) {
    attributes[name] = value;
    if (name == "style") {
        // Keep styleCssText mirror in sync
        const_cast<Element*>(this)->styleCssText = value;
    }
}

std::string Element::getAttribute(const std::string& name) const {
    auto it = attributes.find(name);
    return it != attributes.end() ? it->second : std::string();
}

void Element::removeAttribute(const std::string& name) {
    attributes.erase(name);
}

std::vector<std::shared_ptr<Element>> Element::getElementsByTagName(const std::string& name) const {
    std::vector<std::shared_ptr<Element>> result;
    if (tagName == name) result.push_back(std::static_pointer_cast<Element>(const_cast<Element*>(this)->shared_from_this()));
    for (const auto& child : childNodes) {
        if (auto el = std::dynamic_pointer_cast<Element>(child)) {
            auto sub = el->getElementsByTagName(name);
            result.insert(result.end(), sub.begin(), sub.end());
        }
    }
    return result;
}

// --- Text ---
Text::Text(const std::string& value) {
    nodeType = NodeType::TEXT;
    nodeName = "#text";
    nodeValue = value;
}

// --- Document ---
Document::Document() {
    nodeType = NodeType::DOCUMENT;
    nodeName = "#document";
    nodeValue = "";
    ownerDocument.reset();
}

std::shared_ptr<Element> Document::createElement(const std::string& tag) {
    auto el = std::make_shared<Element>();
    el->nodeType = NodeType::ELEMENT;
    el->nodeName = tag;
    el->tagName = tag;
    el->ownerDocument = shared_from_this();
    el->debugId = g_node_id_counter.fetch_add(1, std::memory_order_relaxed);
    return el;
}

std::shared_ptr<Text> Document::createTextNode(const std::string& value) {
    auto t = std::make_shared<Text>(value);
    t->ownerDocument = shared_from_this();
    t->debugId = g_node_id_counter.fetch_add(1, std::memory_order_relaxed);
    return t;
}

std::string Element::serializeOpenTag() const {
    std::string s;
    s += "<" + tagName;
    for (auto &kv : attributes) {
        s += " " + kv.first + "=\"" + kv.second + "\"";
    }
    s += ">";
    return s;
}

// Element innerHTML / outerHTML (minimal implementations)
std::string Element::innerHTML() const {
    std::string s;
    for (auto &c : childNodes) if (c) {
        if (c->nodeType == NodeType::TEXT) s += c->nodeValue;
        else if (c->nodeType == NodeType::ELEMENT) s += std::static_pointer_cast<Element>(c)->outerHTML();
        else s += c->textContent();
    }
    return s;
}
void Element::setInnerHTML(const std::string& html) {
    for (auto &c : childNodes) if (c) c->parentNode.reset();
    childNodes.clear();
    if (!html.empty()) {
        if (auto doc = std::dynamic_pointer_cast<Document>(ownerDocument.lock())) appendChild(doc->createTextNode(html));
    }
}
std::string Element::outerHTML() const {
    std::string s = serializeOpenTag();
    for (auto &c : childNodes) if (c) {
        if (c->nodeType == NodeType::TEXT) s += c->nodeValue;
        else if (c->nodeType == NodeType::ELEMENT) s += std::static_pointer_cast<Element>(c)->outerHTML();
        else s += c->textContent();
    }
    s += "</" + tagName + ">";
    return s;
}

// --- Factory ---
std::shared_ptr<Document> createDocument() {
    auto d = std::make_shared<Document>();
    d->debugId = g_node_id_counter.fetch_add(1, std::memory_order_relaxed);
    return d;
}

} // namespace dom
