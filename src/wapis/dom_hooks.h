// dom_hooks.h - optional engine hooks for generic DOM (keeps core decoupled)
#pragma once
#include <string>
namespace dom {
class Element; // forward
using AttributeHook = void(*)(Element*, const std::string& name, const std::string& value);
void setAttributeHook(AttributeHook cb);
AttributeHook getAttributeHook();
}
