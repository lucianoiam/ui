// dom_hooks.h - optional engine hooks for generic DOM (keeps core decoupled)
#pragma once
#include <string>

namespace dom {
class Element; // forward
using AttributeHook = void (*)(Element*, const std::string& name, const std::string& value);
void setAttributeHook(AttributeHook cb);
AttributeHook getAttributeHook();

class Node; // forward
// Mutation hook: op types: append, insert, remove, replace
using MutationHook = void (*)(Node* target, const char* op, Node* related);
void setMutationHook(MutationHook cb);
MutationHook getMutationHook();
} // namespace dom
