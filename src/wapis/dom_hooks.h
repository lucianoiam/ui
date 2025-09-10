// dom_hooks.h - compatibility shim; prefer per-Document hooks in dom.hpp
#pragma once
#include "dom.hpp"
#include <string>

namespace dom {
inline void setAttributeHook(AttributeHook cb)
{
   // Compatibility shim for legacy include sites.
   (void)cb; // No-op; per-Document install occurs in layout when a document exists.
}

inline AttributeHook getAttributeHook()
{
   return nullptr;
}

inline void setMutationHook(MutationHook cb)
{
   (void)cb;
}

inline MutationHook getMutationHook()
{
   return nullptr;
}
} // namespace dom
