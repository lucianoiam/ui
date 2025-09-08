// layout_yoga.h - Yoga layout integration (internal; JS unaware)
#pragma once
#include <quickjs.h>

namespace dom {
class Element;
}

// Mark global layout dirty (call on style mutations)
void layout_mark_dirty();

// Run layout if dirty; builds Yoga tree from DOM starting at document.body
// Applies computed positions/sizes into Element layout* fields (not modifying inline style)
void layout_maybe_run(JSContext* ctx);

// Query computed layout box; returns true if available (values in CSS px units)
bool layout_get_box(dom::Element* el, int& x, int& y, int& w, int& h);
