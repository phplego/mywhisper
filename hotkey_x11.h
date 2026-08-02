#pragma once

#include "app_state.h"

// Installs callbacks for trigger-key toggles and Escape cancellation.
void hotkey_x11_set_handlers(void (*on_toggle)(AppState*), void (*on_cancel)(AppState*));
// Recomputes the X11 keycode after the configured modifier changes.
void hotkey_x11_refresh_trigger_key(AppState* app);
// Grabs or releases Escape at the X11 root window.
void hotkey_x11_capture_escape(AppState* app, bool capture);
// Polls X11 key state and recognizes the configured double press.
gboolean hotkey_x11_poll(gpointer user_data);
