#include "hotkey_x11.h"

static constexpr gint64 kTriggerPressWindowMs = 500;

static void (*kHotkeyToggleHandler)(AppState*) = nullptr;
static void (*kHotkeyCancelHandler)(AppState*) = nullptr;

void hotkey_x11_set_handlers(void (*on_toggle)(AppState*), void (*on_cancel)(AppState*)) {
    kHotkeyToggleHandler = on_toggle;
    kHotkeyCancelHandler = on_cancel;
}

static gboolean is_key_down(const char keymap[32], KeyCode keycode) {
    if (keycode == 0) return FALSE;
    const int idx = keycode / 8;
    const int bit = keycode % 8;
    return (keymap[idx] & (1 << bit)) != 0;
}

gboolean hotkey_x11_poll(gpointer user_data) {
    auto* app = static_cast<AppState*>(user_data);
    if (!app || !app->hotkey.display) return G_SOURCE_CONTINUE;
    char keymap[32] = {0};
    XQueryKeymap(app->hotkey.display, keymap);

    const bool trigger_now = is_key_down(keymap, app->hotkey.trigger_key);
    const bool new_press = trigger_now && !app->hotkey.trigger_key_down;
    app->hotkey.trigger_key_down = trigger_now;

    const bool esc_now = is_key_down(keymap, app->hotkey.esc);
    const bool esc_press = esc_now && !app->hotkey.esc_down;
    app->hotkey.esc_down = esc_now;
    if (esc_press && app->status == RunState::Recording && kHotkeyCancelHandler) kHotkeyCancelHandler(app);
    if (!new_press) return G_SOURCE_CONTINUE;

    const gint64 now_ms = g_get_monotonic_time() / 1000;
    const bool is_double_press = app->hotkey.last_trigger_press_ms > 0 && (now_ms - app->hotkey.last_trigger_press_ms) <= kTriggerPressWindowMs;
    app->hotkey.last_trigger_press_ms = now_ms;
    if (is_double_press) {
        app->hotkey.last_trigger_press_ms = 0;
        if (kHotkeyToggleHandler) kHotkeyToggleHandler(app);
    }
    return G_SOURCE_CONTINUE;
}
