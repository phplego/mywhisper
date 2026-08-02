#pragma once

#include "app_state.h"

// Creates the transparent status border on the active monitor.
void overlay_ui_init(AppState* app);
// Shows, hides, or recolors the status border for a run state.
void overlay_ui_set_status(AppState* app, RunState status);
// Destroys the status overlay associated with an application state.
void overlay_ui_shutdown(AppState* app);
