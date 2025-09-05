#include "dom_observer.h"
#include <mutex>

namespace {
static std::vector<DomObserver*> g_observers; // simple list; observers manage own lifetime
}

void dom_register_observer(DomObserver* o)
{
   if (!o)
      return;
   if (std::find(g_observers.begin(), g_observers.end(), o) == g_observers.end())
      g_observers.push_back(o);
}

void dom_unregister_observer(DomObserver* o)
{
   g_observers.erase(std::remove(g_observers.begin(), g_observers.end(), o), g_observers.end());
}

void dom_notify_element_created(dom::Element* el)
{
   for (auto* o : g_observers)
      o->onElementCreated(el);
}

void dom_notify_element_removed(dom::Element* el)
{
   for (auto* o : g_observers)
      o->onElementRemoved(el);
}

void dom_notify_attribute_changed(dom::Element* el, const std::string& name, const std::string& oldValue,
                                  const std::string& newValue)
{
   for (auto* o : g_observers)
      o->onAttributeChanged(el, name, oldValue, newValue);
}

void dom_notify_childlist_changed(dom::Element* el)
{
   for (auto* o : g_observers)
      o->onChildListChanged(el);
}
