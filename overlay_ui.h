#pragma once

#include "app_state.h"

void overlay_ui_init(AppState* app);
void overlay_ui_set_status(AppState* app, RunState status);
void overlay_ui_shutdown(AppState* app);
