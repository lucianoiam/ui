#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace dom {
class Element;

class DomObserver {
 public:
  virtual void onElementCreated(dom::Element*) {}
  virtual void onElementRemoved(dom::Element*) {}
  virtual void onAttributeChanged(dom::Element*, const std::string& name, const std::string& oldValue,
                                  const std::string& newValue) {}
  virtual void onChildListChanged(dom::Element*) {}
  virtual ~DomObserver() = default;
};
} // namespace dom

// Observers are attached per Document (see dom::Document).
inline void dom_register_observer(dom::DomObserver*) {}
inline void dom_unregister_observer(dom::DomObserver*) {}
inline void dom_notify_element_created(dom::Element*) {}
inline void dom_notify_element_removed(dom::Element*) {}
inline void dom_notify_attribute_changed(dom::Element*, const std::string&, const std::string&, const std::string&) {}
inline void dom_notify_childlist_changed(dom::Element*) {}
