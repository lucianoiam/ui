// Centralized default viewport dimensions (single source of truth)
#pragma once

constexpr int VIEWPORT_DEFAULT_WIDTH = 800;
constexpr int VIEWPORT_DEFAULT_HEIGHT = 600;

// Current runtime viewport size (defined in main.mm)
extern int g_winW;
extern int g_winH;

// (Future) If dynamic resizing is added, update the global g_winW/g_winH
// variables (declared in main.mm) and re-run layout/composite.
