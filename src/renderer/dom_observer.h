#pragma once
#include <algorithm>
#include <string>
#include <vector>

namespace dom {
class Element;
}

class DomObserver {
public:
  virtual void onElementCreated(dom::Element*) {}
  virtual void onElementRemoved(dom::Element*) {}
  virtual void onAttributeChanged(dom::Element*, const std::string& name, const std::string& oldValue,
                                  const std::string& newValue) {}
  virtual void onChildListChanged(dom::Element*) {}
  virtual ~DomObserver() = default;
};

// Registration API (no ownership transfer). Not thread-safe (single-threaded JS assumption).
void dom_register_observer(DomObserver*);
void dom_unregister_observer(DomObserver*);

// Internal dispatch helpers (to be called by dom_adapter after mutations)
void dom_notify_element_created(dom::Element*);
void dom_notify_element_removed(dom::Element*);
void dom_notify_attribute_changed(dom::Element*, const std::string& name, const std::string& oldValue,
                                  const std::string& newValue);
void dom_notify_childlist_changed(dom::Element*);
