#pragma once
#include <functional>

// Portable minimal scheduler abstraction.
// For now executes callbacks immediately while coalescing duplicates.
// Can later be swapped for platform event loop / vsync integration.

void scheduler_init();
void scheduler_request(const std::function<void()>& cb);
