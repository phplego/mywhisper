#pragma once

#include "app_state.h"

struct TrayUiHandlers {
    void (*toggle_recording)(AppState*) = nullptr;
    void (*open_settings)(AppState*) = nullptr;
    void (*quit_app)(AppState*) = nullptr;
};

void tray_ui_set_handlers(const TrayUiHandlers& handlers);
gboolean tray_ui_tick(gpointer user_data);
void tray_ui_update_prompt_label(AppState* app);
void tray_ui_rebuild_menu(AppState* app);
