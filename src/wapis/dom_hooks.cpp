#include "dom_hooks.h"
namespace dom {
static AttributeHook g_attr_hook = nullptr;
static MutationHook g_mut_hook = nullptr;
void setAttributeHook(AttributeHook cb) {
  g_attr_hook = cb;
}
AttributeHook getAttributeHook() {
  return g_attr_hook;
}
void setMutationHook(MutationHook cb) {
  g_mut_hook = cb;
}
MutationHook getMutationHook() {
  return g_mut_hook;
}
} // namespace dom
