#pragma once

#include "app_state.h"

void hotkey_x11_set_handlers(void (*on_toggle)(AppState*), void (*on_cancel)(AppState*));
void hotkey_x11_refresh_trigger_key(AppState* app);
gboolean hotkey_x11_poll(gpointer user_data);
