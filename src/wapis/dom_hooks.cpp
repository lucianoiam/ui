#include "dom_hooks.h"
namespace dom {
static AttributeHook g_attr_hook = nullptr;
void setAttributeHook(AttributeHook cb){ g_attr_hook = cb; }
AttributeHook getAttributeHook(){ return g_attr_hook; }
}
