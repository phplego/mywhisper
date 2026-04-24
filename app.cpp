#include "app_state.h"
#include "audio_pipeline.h"
#include "hotkey_x11.h"
#include "overlay_ui.h"
#include "tray_ui.h"
#include "version.h"
#include <X11/keysym.h>

static constexpr const char* kRunStateNames[] = {"Idle", "Recording", "Transcribing"};

static bool handle_cli_args(int argc, char** argv) {
    if (argc == 2 && g_strcmp0(argv[1], "--version") == 0) {
        g_print("mywhisper-gtk v%s\n", kAppVersion);
        return true;
    }
    return false;
}

static void set_status(AppState* app, RunState next_status, const char* reason) {
    if (!app || app->status == next_status) return;
    const char* from = kRunStateNames[static_cast<int>(app->status)];
    const char* to = kRunStateNames[static_cast<int>(next_status)];
    if (reason && *reason) g_print("status: %s -> %s (%s)\n", from, to, reason);
    else g_print("status: %s -> %s\n", from, to);
    app->status = next_status;
    overlay_ui_set_status(app, next_status);
}

static void on_prompts_changed(AppState* app) {
    tray_ui_rebuild_menu(app);
    tray_ui_update_prompt_label(app);
}

static void on_toggle_recording(AppState* app) {
    audio_pipeline_toggle_recording(app);
}

static void on_cancel_recording(AppState* app) {
    audio_pipeline_stop_recording(app, true);
}

static void on_open_settings(AppState* app) {
    GApplication* gapp = g_application_get_default();
    if (!gapp || !GTK_IS_APPLICATION(gapp)) return;
    app_settings_show_window(GTK_APPLICATION(gapp), app, gtk_get_current_event_time());
}

static void on_quit(AppState* app) {
    audio_pipeline_cancel_processes(app);
    GApplication* gapp = g_application_get_default();
    if (gapp) g_application_quit(gapp);
}

static void on_app_shutdown(GApplication*, gpointer user_data) {
    auto* app = static_cast<AppState*>(user_data);
    if (!app) return;
    app->shutting_down = true;
    if (app->ui.menu) {
        gtk_widget_destroy(app->ui.menu);
        app->ui.menu = nullptr;
    }
    if (app->ui.indicator) {
        g_object_unref(app->ui.indicator);
        app->ui.indicator = nullptr;
    }
    overlay_ui_shutdown(app);
    if (app->hotkey.display) {
        XCloseDisplay(app->hotkey.display);
        app->hotkey.display = nullptr;
    }
    audio_pipeline_shutdown(app);
    while (g_main_context_pending(nullptr)) {
        g_main_context_iteration(nullptr, FALSE);
    }
    g_mutex_clear(&app->audio.audio_mutex);
    while (g_source_remove_by_user_data(app)) {}
    delete app;
}

static void on_activate(GtkApplication* application, gpointer) {
    g_application_hold(G_APPLICATION(application));
    g_signal_connect(
        application,
        "shutdown",
        G_CALLBACK(+[](GApplication* app, gpointer) { g_application_release(app); }),
        nullptr
    );
    g_print("mywhisper-gtk v%s\n", kAppVersion);
    auto* app = new AppState();
    g_mutex_init(&app->audio.audio_mutex);
    GtkSettings* gtk_settings = gtk_settings_get_default();
    if (gtk_settings) g_object_set(gtk_settings, "gtk-menu-images", TRUE, nullptr);
    app->hotkey.display = XOpenDisplay(nullptr);
    if (!app->hotkey.display) g_error("XOpenDisplay failed. This minimal version requires X11 session.");
    app->hotkey.esc = XKeysymToKeycode(app->hotkey.display, XK_Escape);
    app->ui.idle_icon_path = settings_store_find_icon_path("icons/icon_idle.svg");
    app->ui.recording_icon_paths = {
        settings_store_find_icon_path("icons/icon_recording_1.svg"),
        settings_store_find_icon_path("icons/icon_recording_2.svg"),
        settings_store_find_icon_path("icons/icon_recording_3.svg")
    };
    app->ui.transcribing_icon_paths = {
        settings_store_find_icon_path("icons/icon_transcribing_1.svg"),
        settings_store_find_icon_path("icons/icon_transcribing_2.svg"),
        settings_store_find_icon_path("icons/icon_transcribing_3.svg")
    };
    settings_store_set_prompt_change_hook(on_prompts_changed);
    settings_store_load_persisted_state(app);
    hotkey_x11_refresh_trigger_key(app);
    audio_pipeline_set_status_handler(set_status);
    hotkey_x11_set_handlers(on_toggle_recording, on_cancel_recording);
    tray_ui_set_handlers({on_toggle_recording, on_open_settings, on_quit});
    app->ui.indicator = app_indicator_new(
        "mywhisper-gtk-indicator",
        app->ui.idle_icon_path.c_str(),
        APP_INDICATOR_CATEGORY_APPLICATION_STATUS
    );
    if (!app->ui.indicator) g_error("Failed to create AppIndicator.");
    app_indicator_set_status(app->ui.indicator, APP_INDICATOR_STATUS_ACTIVE);
    app_indicator_set_title(app->ui.indicator, "MicRec");
    tray_ui_rebuild_menu(app);
    overlay_ui_init(app);
    g_timeout_add(
        100,
        +[](gpointer user_data) -> gboolean {
            tray_ui_update_prompt_label(static_cast<AppState*>(user_data));
            return G_SOURCE_REMOVE;
        },
        app
    );
    g_signal_connect(application, "shutdown", G_CALLBACK(on_app_shutdown), app);
    g_timeout_add(220, tray_ui_tick, app);
    g_timeout_add(20, hotkey_x11_poll, app);
}

int main(int argc, char** argv) {
    if (handle_cli_args(argc, argv)) return 0;
    GtkApplication* app = gtk_application_new("dev.mywhisper.trayrec", G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(app, "activate", G_CALLBACK(on_activate), nullptr);
    const int status = g_application_run(G_APPLICATION(app), argc, argv);
    g_object_unref(app);
    return status;
}
