#include "tray_ui.h"

static TrayUiHandlers kTrayHandlers;

void tray_ui_set_handlers(const TrayUiHandlers& handlers) {
    kTrayHandlers = handlers;
}

gboolean tray_ui_tick(gpointer user_data) {
    auto* app = static_cast<AppState*>(user_data);
    if (!app || !app->ui.indicator) return G_SOURCE_REMOVE;

    const bool status_changed = app->status != app->ui.rendered_status;
    if (status_changed) {
        app->ui.rendered_status = app->status;
        app->ui.animation_frame = 0;
        const bool is_recording = app->status == RunState::Recording;
        const bool is_transcribing = app->status == RunState::Transcribing;
        if (app->ui.toggle_item) {
            gtk_menu_item_set_label(GTK_MENU_ITEM(app->ui.toggle_item), is_recording ? "Stop recording" : "Start recording");
            gtk_widget_set_sensitive(app->ui.toggle_item, is_transcribing ? FALSE : TRUE);
        }
    }

    const std::vector<std::string>* frames = nullptr;
    if (app->status == RunState::Recording) frames = &app->ui.recording_icon_paths;
    else if (app->status == RunState::Transcribing) frames = &app->ui.transcribing_icon_paths;

    const char* icon_path = app->ui.idle_icon_path.c_str();
    if (frames && !frames->empty()) {
        if (!status_changed) app->ui.animation_frame = (app->ui.animation_frame + 1) % static_cast<gint>(frames->size());
        icon_path = (*frames)[app->ui.animation_frame].c_str();
    }
    app_indicator_set_icon_full(app->ui.indicator, icon_path, "MicRec state");
    return G_SOURCE_CONTINUE;
}

void tray_ui_update_prompt_label(AppState* app) {
    if (!app || !app->ui.indicator) return;
    if (app->settings.active_prompt_index >= 0 && static_cast<size_t>(app->settings.active_prompt_index) < app->settings.custom_prompts.size()) {
        const char* title = app->settings.custom_prompts[app->settings.active_prompt_index].title.c_str();
        app_indicator_set_label(app->ui.indicator, title, title);
        return;
    }
    app_indicator_set_label(app->ui.indicator, "", "");
}

static void on_menu_prompt_selected(GtkCheckMenuItem* item, gpointer user_data) {
    auto* app = static_cast<AppState*>(user_data);
    if (!app || !item || gtk_check_menu_item_get_active(item) != TRUE) return;
    const int selected = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(item), "prompt-index"));
    settings_store_set_active_prompt(app, selected);
}

static void on_menu_record_now(GtkMenuItem*, gpointer user_data) {
    if (kTrayHandlers.toggle_recording) kTrayHandlers.toggle_recording(static_cast<AppState*>(user_data));
}

static void on_menu_settings(GtkMenuItem*, gpointer user_data) {
    if (kTrayHandlers.open_settings) kTrayHandlers.open_settings(static_cast<AppState*>(user_data));
}

static void on_menu_quit(GtkMenuItem*, gpointer user_data) {
    if (kTrayHandlers.quit_app) kTrayHandlers.quit_app(static_cast<AppState*>(user_data));
}

void tray_ui_rebuild_menu(AppState* app) {
    if (!app || !app->ui.indicator) return;
    if (app->ui.menu) {
        gtk_widget_destroy(app->ui.menu);
        app->ui.menu = nullptr;
    }
    GtkWidget* menu = gtk_menu_new();
    auto make_item_with_icon = [](const char* icon_name, const char* text) -> GtkWidget* {
        GtkWidget* item = gtk_menu_item_new();
        GtkWidget* row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget* icon = gtk_image_new_from_icon_name(icon_name, GTK_ICON_SIZE_MENU);
        GtkWidget* label = gtk_label_new(text);
        gtk_label_set_xalign(GTK_LABEL(label), 0.0f);
        gtk_box_pack_start(GTK_BOX(row), icon, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(row), label, TRUE, TRUE, 0);
        gtk_container_add(GTK_CONTAINER(item), row);
        return item;
    };

    GtkWidget* record_now_item = make_item_with_icon("media-record-symbolic", "Start recording");
    GtkWidget* settings_item = make_item_with_icon("preferences-system-symbolic", "Settings");
    GtkWidget* quit_item = make_item_with_icon("system-shutdown-symbolic", "Exit");
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), record_now_item);

    if (!app->settings.custom_prompts.empty()) {
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
        GSList* radio_group = nullptr;
        GtkWidget* default_item = gtk_radio_menu_item_new_with_label(radio_group, "Default (no prompt)");
        radio_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(default_item));
        g_object_set_data(G_OBJECT(default_item), "prompt-index", GINT_TO_POINTER(-1));
        gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(default_item), app->settings.active_prompt_index == -1 ? TRUE : FALSE);
        g_signal_connect(default_item, "toggled", G_CALLBACK(on_menu_prompt_selected), app);
        gtk_menu_shell_append(GTK_MENU_SHELL(menu), default_item);

        for (size_t i = 0; i < app->settings.custom_prompts.size(); ++i) {
            GtkWidget* item = gtk_radio_menu_item_new_with_label(radio_group, app->settings.custom_prompts[i].title.c_str());
            radio_group = gtk_radio_menu_item_get_group(GTK_RADIO_MENU_ITEM(item));
            g_object_set_data(G_OBJECT(item), "prompt-index", GINT_TO_POINTER(static_cast<int>(i)));
            gtk_check_menu_item_set_active(GTK_CHECK_MENU_ITEM(item), app->settings.active_prompt_index == static_cast<int>(i) ? TRUE : FALSE);
            g_signal_connect(item, "toggled", G_CALLBACK(on_menu_prompt_selected), app);
            gtk_menu_shell_append(GTK_MENU_SHELL(menu), item);
        }
    }

    gtk_menu_shell_append(GTK_MENU_SHELL(menu), gtk_separator_menu_item_new());
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), settings_item);
    gtk_menu_shell_append(GTK_MENU_SHELL(menu), quit_item);
    gtk_widget_show_all(menu);
    app->ui.menu = menu;
    app->ui.toggle_item = record_now_item;
    app_indicator_set_menu(app->ui.indicator, GTK_MENU(menu));
    g_signal_connect(record_now_item, "activate", G_CALLBACK(on_menu_record_now), app);
    g_signal_connect(settings_item, "activate", G_CALLBACK(on_menu_settings), app);
    g_signal_connect(quit_item, "activate", G_CALLBACK(on_menu_quit), app);
}
