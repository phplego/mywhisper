#pragma once

#include "app_state.h"

struct TrayUiHandlers {
    void (*toggle_recording)(AppState*) = nullptr; // Start/stop command target.
    void (*open_settings)(AppState*) = nullptr; // Settings command target.
    void (*quit_app)(AppState*) = nullptr; // Exit command target.
};

// Installs callbacks invoked by tray menu commands.
void tray_ui_set_handlers(const TrayUiHandlers& handlers);
// Advances tray animation and applies state-dependent controls.
gboolean tray_ui_tick(gpointer user_data);
// Displays the active prompt title beside the tray icon.
void tray_ui_update_prompt_label(AppState* app);
// Recreates the tray menu from current settings and prompts.
void tray_ui_rebuild_menu(AppState* app);
