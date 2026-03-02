#pragma once

#include "app_state.h"

void hotkey_x11_set_handlers(void (*on_toggle)(AppState*), void (*on_cancel)(AppState*));
gboolean hotkey_x11_poll(gpointer user_data);
