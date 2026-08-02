#include "hotkey_x11.h"

#include <X11/keysym.h>

static void (*kHotkeyToggleHandler)(AppState*) = nullptr;
static void (*kHotkeyCancelHandler)(AppState*) = nullptr;
static bool kGrabFailed = false;

// Records asynchronous X11 failures while attempting to grab Escape.
static int capture_error_handler(Display*, XErrorEvent*) {
    kGrabFailed = true;
    return 0;
}

// Installs application callbacks for recognized hotkey actions.
void hotkey_x11_set_handlers(void (*on_toggle)(AppState*), void (*on_cancel)(AppState*)) {
    kHotkeyToggleHandler = on_toggle;
    kHotkeyCancelHandler = on_cancel;
}

// Tests one keycode bit in the 256-key bitmap returned by XQueryKeymap.
static gboolean is_key_down(const char keymap[32], KeyCode keycode) {
    if (keycode == 0) return FALSE;
    const int idx = keycode / 8;
    const int bit = keycode % 8;
    return (keymap[idx] & (1 << bit)) != 0;
}

// Maps the persisted modifier name to its left-hand X11 keysym.
static KeySym get_trigger_keysym(const AppState* app) {
    const char* modifier = settings_store_get_trigger_modifier(app);
    if (g_strcmp0(modifier, "shift") == 0) return XK_Shift_L;
    if (g_strcmp0(modifier, "alt") == 0) return XK_Alt_L;
    return XK_Control_L;
}

// Updates the trigger keycode and resets partial double-press state.
void hotkey_x11_refresh_trigger_key(AppState* app) {
    if (!app || !app->hotkey.display) return;
    app->hotkey.trigger_key = XKeysymToKeycode(app->hotkey.display, get_trigger_keysym(app));
    app->hotkey.trigger_key_down = false;
    app->hotkey.last_trigger_press_ms = 0;
}

// Grabs Escape during recording so cancellation does not reach the focused app.
void hotkey_x11_capture_escape(AppState* app, bool capture) {
    if (!app || !app->hotkey.display || app->hotkey.esc == 0 || app->hotkey.esc_captured == capture) return;
    const Window root = DefaultRootWindow(app->hotkey.display);
    if (!capture) {
        XUngrabKey(app->hotkey.display, app->hotkey.esc, AnyModifier, root);
        XSync(app->hotkey.display, False);
        app->hotkey.esc_captured = false;
        return;
    }

    XSync(app->hotkey.display, False);
    kGrabFailed = false;
    auto previous_handler = XSetErrorHandler(capture_error_handler);
    XGrabKey(
        app->hotkey.display,
        app->hotkey.esc,
        AnyModifier,
        root,
        False,
        GrabModeAsync,
        GrabModeAsync
    );
    XSync(app->hotkey.display, False);
    XSetErrorHandler(previous_handler);
    app->hotkey.esc_captured = !kGrabFailed;
    if (kGrabFailed) {
        XUngrabKey(app->hotkey.display, app->hotkey.esc, AnyModifier, root);
        XSync(app->hotkey.display, False);
        g_printerr("failed to capture Escape; key will be passed to the active application\n");
    }
}

// Polls X11 key state and dispatches Escape or double-modifier actions.
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
    const bool is_double_press = app->hotkey.last_trigger_press_ms > 0 &&
        (now_ms - app->hotkey.last_trigger_press_ms) <= settings_store_get_trigger_press_window_ms(app);
    app->hotkey.last_trigger_press_ms = now_ms;
    if (is_double_press) {
        app->hotkey.last_trigger_press_ms = 0;
        if (kHotkeyToggleHandler) kHotkeyToggleHandler(app);
    }
    return G_SOURCE_CONTINUE;
}
