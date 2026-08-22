#pragma once

#include "app_state.h"

// Loads the optional plug-in and starts monitoring when enabled in settings.
void wake_word_init(AppState* app);
// Enables or disables monitoring and persists the choice.
bool wake_word_set_enabled(AppState* app, bool enabled);
// Stops monitoring and unloads the optional plug-in.
void wake_word_shutdown();
