#include "app_state.h"
#include "version.h"
#include <cstddef>
#include <string>

static GtkWidget* kSettingsWindowRef = nullptr;
static GtkWidget* kSettingsAutostartCheck = nullptr;
static GtkWidget* kSettingsOpenAiApiKeyEntry = nullptr;
static GtkWidget* kSettingsCustomPromptsSection = nullptr;
static GtkWidget* kSettingsCustomPromptsList = nullptr;
static GtkWidget* kSettingsEditPromptButton = nullptr;
static GtkWidget* kSettingsDeletePromptButton = nullptr;
static AppState* kSettingsAppState = nullptr;
static bool kSettingsAutostartCheckSyncing = false;

static void app_settings_sync_autostart_check() {
    if (!kSettingsAutostartCheck) {
        return;
    }
    kSettingsAutostartCheckSyncing = true;
    gtk_toggle_button_set_active(
        GTK_TOGGLE_BUTTON(kSettingsAutostartCheck),
        settings_store_is_autostart_enabled() ? TRUE : FALSE
    );
    kSettingsAutostartCheckSyncing = false;
}

static int app_settings_get_selected_prompt_index() {
    if (!kSettingsCustomPromptsList || !kSettingsAppState) {
        return -1;
    }
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(kSettingsCustomPromptsList));
    if (!row) {
        return -1;
    }
    return gtk_list_box_row_get_index(row);
}

static void app_settings_refresh_prompt_action_buttons() {
    if (!kSettingsEditPromptButton || !kSettingsDeletePromptButton || !kSettingsAppState) {
        return;
    }
    const bool has_selected = app_settings_get_selected_prompt_index() >= 0;
    gtk_widget_set_sensitive(kSettingsEditPromptButton, has_selected);
    gtk_widget_set_sensitive(kSettingsDeletePromptButton, has_selected);
}

static bool app_settings_run_prompt_dialog(
    const char* dialog_title,
    const char* accept_label,
    std::string* title_value,
    std::string* text_value
) {
    if (!kSettingsWindowRef || !title_value || !text_value) {
        return false;
    }
    GtkWidget* dialog = gtk_dialog_new_with_buttons(
        dialog_title,
        GTK_WINDOW(kSettingsWindowRef),
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        "_Cancel",
        GTK_RESPONSE_CANCEL,
        accept_label,
        GTK_RESPONSE_ACCEPT,
        nullptr
    );
    gtk_window_set_default_size(GTK_WINDOW(dialog), 560, 360);
    GtkWidget* content = gtk_dialog_get_content_area(GTK_DIALOG(dialog));
    GtkWidget* body = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_container_set_border_width(GTK_CONTAINER(body), 18);
    gtk_box_set_spacing(GTK_BOX(content), 0);
    gtk_box_pack_start(GTK_BOX(content), body, TRUE, TRUE, 0);
    GtkWidget* accept_button = gtk_dialog_get_widget_for_response(GTK_DIALOG(dialog), GTK_RESPONSE_ACCEPT);
    GtkWidget* action_area = accept_button ? gtk_widget_get_parent(accept_button) : nullptr;
    if (action_area) {
        gtk_widget_set_margin_start(action_area, 18);
        gtk_widget_set_margin_end(action_area, 18);
        gtk_widget_set_margin_bottom(action_area, 18);
    }
    GtkWidget* title_label = gtk_label_new("Title");
    GtkWidget* title_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(title_entry), title_value->c_str());
    gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
    gtk_box_pack_start(GTK_BOX(body), title_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(body), title_entry, FALSE, FALSE, 0);
    GtkWidget* prompt_label = gtk_label_new("Prompt");
    GtkWidget* prompt_view = gtk_text_view_new();
    GtkTextBuffer* buffer = gtk_text_view_get_buffer(GTK_TEXT_VIEW(prompt_view));
    gtk_text_buffer_set_text(buffer, text_value->c_str(), -1);
    GtkWidget* prompt_scroll = gtk_scrolled_window_new(nullptr, nullptr);
    gtk_label_set_xalign(GTK_LABEL(prompt_label), 0.0f);
    gtk_scrolled_window_set_policy(GTK_SCROLLED_WINDOW(prompt_scroll), GTK_POLICY_AUTOMATIC, GTK_POLICY_AUTOMATIC);
    gtk_widget_set_size_request(prompt_scroll, 0, 180);
    gtk_container_add(GTK_CONTAINER(prompt_scroll), prompt_view);
    gtk_box_pack_start(GTK_BOX(body), prompt_label, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(body), prompt_scroll, TRUE, TRUE, 0);
    gtk_widget_show_all(dialog);
    const gint response = gtk_dialog_run(GTK_DIALOG(dialog));
    if (response != GTK_RESPONSE_ACCEPT) {
        gtk_widget_destroy(dialog);
        return false;
    }
    const gchar* title_raw = gtk_entry_get_text(GTK_ENTRY(title_entry));
    GtkTextIter start;
    GtkTextIter end;
    gtk_text_buffer_get_bounds(buffer, &start, &end);
    gchar* text_raw = gtk_text_buffer_get_text(buffer, &start, &end, FALSE);
    gchar* title_clean = g_strdup(title_raw ? title_raw : "");
    g_strstrip(title_clean);
    g_strstrip(text_raw);
    const bool valid = (*title_clean != '\0' && *text_raw != '\0');
    if (valid) {
        *title_value = title_clean;
        *text_value = text_raw;
    }
    g_free(title_clean);
    g_free(text_raw);
    gtk_widget_destroy(dialog);
    return valid;
}

static void app_settings_refresh_custom_prompts_ui() {
    if (!kSettingsCustomPromptsSection || !kSettingsCustomPromptsList || !kSettingsAppState) {
        return;
    }
    GList* children = gtk_container_get_children(GTK_CONTAINER(kSettingsCustomPromptsList));
    for (GList* it = children; it != nullptr; it = it->next) {
        gtk_widget_destroy(GTK_WIDGET(it->data));
    }
    g_list_free(children);
    const size_t count = settings_store_get_custom_prompt_count(kSettingsAppState);
    if (count == 0) {
        gtk_widget_hide(kSettingsCustomPromptsSection);
        app_settings_refresh_prompt_action_buttons();
        return;
    }
    for (size_t i = 0; i < count; ++i) {
        const char* title = settings_store_get_custom_prompt_title(kSettingsAppState, i);
        const char* text = settings_store_get_custom_prompt_text(kSettingsAppState, i);
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* row_body = gtk_grid_new();
        GtkWidget* title_label = gtk_label_new(title);
        GtkWidget* text_label = gtk_label_new(text);
        gtk_grid_set_column_spacing(GTK_GRID(row_body), 12);
        gtk_container_set_border_width(GTK_CONTAINER(row_body), 8);
        gtk_label_set_xalign(GTK_LABEL(title_label), 0.0f);
        gtk_label_set_single_line_mode(GTK_LABEL(title_label), TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(title_label), PANGO_ELLIPSIZE_END);
        gtk_label_set_width_chars(GTK_LABEL(title_label), 14);
        gtk_widget_set_hexpand(title_label, FALSE);
        gtk_label_set_xalign(GTK_LABEL(text_label), 0.0f);
        gtk_label_set_single_line_mode(GTK_LABEL(text_label), TRUE);
        gtk_label_set_ellipsize(GTK_LABEL(text_label), PANGO_ELLIPSIZE_END);
        gtk_style_context_add_class(gtk_widget_get_style_context(text_label), "dim-label");
        gtk_widget_set_hexpand(text_label, TRUE);
        gtk_widget_set_halign(text_label, GTK_ALIGN_FILL);
        gtk_grid_attach(GTK_GRID(row_body), title_label, 0, 0, 1, 1);
        gtk_grid_attach(GTK_GRID(row_body), text_label, 1, 0, 1, 1);
        gtk_container_add(GTK_CONTAINER(row), row_body);
        gtk_list_box_insert(GTK_LIST_BOX(kSettingsCustomPromptsList), row, -1);
    }
    gtk_widget_show_all(kSettingsCustomPromptsSection);
    if (!gtk_list_box_get_selected_row(GTK_LIST_BOX(kSettingsCustomPromptsList))) {
        GtkListBoxRow* first = gtk_list_box_get_row_at_index(GTK_LIST_BOX(kSettingsCustomPromptsList), 0);
        if (first) {
            gtk_list_box_select_row(GTK_LIST_BOX(kSettingsCustomPromptsList), first);
        }
    }
    app_settings_refresh_prompt_action_buttons();
}

static void app_settings_on_autostart_toggled(GtkToggleButton* button, gpointer) {
    if (!button || kSettingsAutostartCheckSyncing) {
        return;
    }
    const bool current = settings_store_is_autostart_enabled();
    const bool desired = gtk_toggle_button_get_active(button);
    if (desired == current) {
        return;
    }
    const bool ok = desired ? settings_store_enable_autostart() : settings_store_disable_autostart();
    if (!ok) {
        g_printerr("failed to update launch-at-login setting\n");
    }
    app_settings_sync_autostart_check();
}

static void app_settings_on_prompt_row_selected(GtkListBox*, GtkListBoxRow*, gpointer) {
    app_settings_refresh_prompt_action_buttons();
}

static void app_settings_on_openai_api_key_changed(GtkEditable*, gpointer) {
    if (!kSettingsAppState || !kSettingsOpenAiApiKeyEntry) {
        return;
    }
    const gchar* key = gtk_entry_get_text(GTK_ENTRY(kSettingsOpenAiApiKeyEntry));
    if (!settings_store_set_openai_api_key(kSettingsAppState, key ? key : "")) {
        g_printerr("failed to save OpenAI API Key\n");
    }
}

static void app_settings_on_add_custom_prompt_clicked(GtkButton*, gpointer) {
    if (!kSettingsAppState) {
        return;
    }
    std::string title;
    std::string text;
    if (!app_settings_run_prompt_dialog("Add custom prompt", "_Add", &title, &text)) {
        return;
    }
    if (!settings_store_add_custom_prompt(kSettingsAppState, title.c_str(), text.c_str())) {
        g_printerr("failed to add custom prompt\n");
    }
    app_settings_refresh_custom_prompts_ui();
}

static void app_settings_on_edit_custom_prompt_clicked(GtkButton*, gpointer) {
    if (!kSettingsAppState) {
        return;
    }
    const int index = app_settings_get_selected_prompt_index();
    if (index < 0) {
        return;
    }
    const char* current_title = settings_store_get_custom_prompt_title(kSettingsAppState, static_cast<size_t>(index));
    const char* current_text = settings_store_get_custom_prompt_text(kSettingsAppState, static_cast<size_t>(index));
    if (!current_title || !current_text) {
        return;
    }
    std::string title = current_title;
    std::string text = current_text;
    if (!app_settings_run_prompt_dialog("Edit custom prompt", "_Save", &title, &text)) {
        return;
    }
    if (!settings_store_update_custom_prompt(kSettingsAppState, static_cast<size_t>(index), title.c_str(), text.c_str())) {
        g_printerr("failed to edit custom prompt\n");
    }
    app_settings_refresh_custom_prompts_ui();
}

static void app_settings_on_delete_custom_prompt_clicked(GtkButton*, gpointer) {
    if (!kSettingsAppState) {
        return;
    }
    const int index = app_settings_get_selected_prompt_index();
    if (index < 0) {
        return;
    }
    const char* title = settings_store_get_custom_prompt_title(kSettingsAppState, static_cast<size_t>(index));
    std::string caption = title && *title ? std::string("Delete this prompt?\n\n") + title : "Delete this prompt?";
    GtkWidget* confirm = gtk_message_dialog_new(
        kSettingsWindowRef ? GTK_WINDOW(kSettingsWindowRef) : nullptr,
        static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
        GTK_MESSAGE_WARNING,
        GTK_BUTTONS_NONE,
        "%s",
        caption.c_str()
    );
    gtk_dialog_add_button(GTK_DIALOG(confirm), "_Cancel", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(confirm), "_Delete", GTK_RESPONSE_ACCEPT);
    gtk_dialog_set_default_response(GTK_DIALOG(confirm), GTK_RESPONSE_CANCEL);
    const gint response = gtk_dialog_run(GTK_DIALOG(confirm));
    gtk_widget_destroy(confirm);
    if (response != GTK_RESPONSE_ACCEPT) {
        return;
    }
    if (!settings_store_remove_custom_prompt(kSettingsAppState, static_cast<size_t>(index))) {
        g_printerr("failed to delete custom prompt\n");
    }
    app_settings_refresh_custom_prompts_ui();
}

void app_settings_show_window(GtkApplication* application, AppState* app, guint32 user_event_time) {
    if (!application) {
        return;
    }
    kSettingsAppState = app;
    if (!kSettingsWindowRef) {
        kSettingsWindowRef = gtk_application_window_new(application);
        gtk_window_set_title(GTK_WINDOW(kSettingsWindowRef), "MyWhisper Settings");
        gtk_window_set_default_size(GTK_WINDOW(kSettingsWindowRef), 600, 400);
        gtk_window_set_position(GTK_WINDOW(kSettingsWindowRef), GTK_WIN_POS_CENTER);
        GtkWidget* overlay = gtk_overlay_new();
        GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 12);
        gtk_container_set_border_width(GTK_CONTAINER(content), 16);
        gtk_container_add(GTK_CONTAINER(kSettingsWindowRef), overlay);
        gtk_container_add(GTK_CONTAINER(overlay), content);
        GtkWidget* version_label = gtk_label_new(nullptr);
        const std::string version_markup = std::string("<small><span alpha='55%'>v") + kAppVersion + "</span></small>";
        gtk_label_set_markup(GTK_LABEL(version_label), version_markup.c_str());
        gtk_label_set_xalign(GTK_LABEL(version_label), 1.0f);
        gtk_label_set_yalign(GTK_LABEL(version_label), 0.0f);
        gtk_style_context_add_class(gtk_widget_get_style_context(version_label), "dim-label");
        gtk_widget_set_halign(version_label, GTK_ALIGN_END);
        gtk_widget_set_valign(version_label, GTK_ALIGN_START);
        gtk_widget_set_margin_top(version_label, 8);
        gtk_widget_set_margin_end(version_label, 10);
        gtk_overlay_add_overlay(GTK_OVERLAY(overlay), version_label);
        kSettingsAutostartCheck = gtk_check_button_new();
        gtk_widget_set_valign(kSettingsAutostartCheck, GTK_ALIGN_START);
        GtkWidget* autostart_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget* autostart_copy_click = gtk_event_box_new();
        GtkWidget* autostart_copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget* autostart_title = gtk_label_new("Launch at login");
        GtkWidget* autostart_hint = gtk_label_new(nullptr);
        gtk_label_set_markup(
            GTK_LABEL(autostart_hint),
            "<small>it creates ~/.config/autostart/mywhisper.desktop</small>"
        );
        gtk_label_set_xalign(GTK_LABEL(autostart_title), 0.0f);
        gtk_label_set_xalign(GTK_LABEL(autostart_hint), 0.0f);
        gtk_label_set_line_wrap(GTK_LABEL(autostart_hint), TRUE);
        gtk_style_context_add_class(gtk_widget_get_style_context(autostart_hint), "dim-label");
        gtk_box_pack_start(GTK_BOX(autostart_copy), autostart_title, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(autostart_copy), autostart_hint, FALSE, FALSE, 0);
        gtk_event_box_set_visible_window(GTK_EVENT_BOX(autostart_copy_click), FALSE);
        gtk_container_add(GTK_CONTAINER(autostart_copy_click), autostart_copy);
        g_signal_connect(
            autostart_copy_click,
            "button-press-event",
            G_CALLBACK(+[](GtkWidget*, GdkEventButton* event, gpointer) -> gboolean {
                if (!event || event->button != 1 || !kSettingsAutostartCheck) {
                    return FALSE;
                }
                gtk_toggle_button_set_active(
                    GTK_TOGGLE_BUTTON(kSettingsAutostartCheck),
                    !gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(kSettingsAutostartCheck))
                );
                return TRUE;
            }),
            nullptr
        );
        gtk_box_pack_start(GTK_BOX(autostart_row), kSettingsAutostartCheck, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(autostart_row), autostart_copy_click, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(content), autostart_row, FALSE, FALSE, 0);
        g_signal_connect(kSettingsAutostartCheck, "toggled", G_CALLBACK(app_settings_on_autostart_toggled), nullptr);
        GtkWidget* api_key_row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget* api_key_copy = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
        GtkWidget* api_key_title = gtk_label_new("OpenAI API Key");
        kSettingsOpenAiApiKeyEntry = gtk_entry_new();
        gtk_label_set_xalign(GTK_LABEL(api_key_title), 0.0f);
        gtk_entry_set_visibility(GTK_ENTRY(kSettingsOpenAiApiKeyEntry), FALSE);
        gtk_entry_set_invisible_char(GTK_ENTRY(kSettingsOpenAiApiKeyEntry), 0x2022);
        gtk_entry_set_placeholder_text(GTK_ENTRY(kSettingsOpenAiApiKeyEntry), "sk-...");
        gtk_box_pack_start(GTK_BOX(api_key_copy), api_key_title, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(api_key_copy), kSettingsOpenAiApiKeyEntry, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(api_key_row), api_key_copy, TRUE, TRUE, 0);
        gtk_box_pack_start(GTK_BOX(content), api_key_row, FALSE, FALSE, 0);
        g_signal_connect(kSettingsOpenAiApiKeyEntry, "changed", G_CALLBACK(app_settings_on_openai_api_key_changed), nullptr);
        kSettingsCustomPromptsSection = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        kSettingsCustomPromptsList = gtk_list_box_new();
        GtkWidget* custom_prompts_scroll = gtk_scrolled_window_new(nullptr, nullptr);
        gtk_scrolled_window_set_policy(
            GTK_SCROLLED_WINDOW(custom_prompts_scroll),
            GTK_POLICY_AUTOMATIC,
            GTK_POLICY_AUTOMATIC
        );
        gtk_list_box_set_selection_mode(GTK_LIST_BOX(kSettingsCustomPromptsList), GTK_SELECTION_SINGLE);
        gtk_widget_set_size_request(custom_prompts_scroll, 0, 170);
        gtk_widget_set_vexpand(custom_prompts_scroll, TRUE);
        gtk_widget_set_hexpand(custom_prompts_scroll, TRUE);
        gtk_container_add(GTK_CONTAINER(custom_prompts_scroll), kSettingsCustomPromptsList);
        gtk_box_pack_start(GTK_BOX(kSettingsCustomPromptsSection), custom_prompts_scroll, TRUE, TRUE, 0);
        gtk_widget_set_vexpand(kSettingsCustomPromptsSection, TRUE);
        gtk_widget_set_hexpand(kSettingsCustomPromptsSection, TRUE);
        GtkWidget* actions = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
        kSettingsEditPromptButton = gtk_button_new_with_label("Edit");
        kSettingsDeletePromptButton = gtk_button_new_with_label("Delete");
        gtk_box_pack_start(GTK_BOX(actions), kSettingsEditPromptButton, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(actions), kSettingsDeletePromptButton, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(kSettingsCustomPromptsSection), actions, FALSE, FALSE, 0);
        gtk_box_pack_start(GTK_BOX(content), kSettingsCustomPromptsSection, TRUE, TRUE, 0);
        GtkWidget* add_prompt_button = gtk_button_new_with_label("Add custom prompt");
        gtk_button_set_image(
            GTK_BUTTON(add_prompt_button),
            gtk_image_new_from_icon_name("list-add-symbolic", GTK_ICON_SIZE_BUTTON)
        );
        gtk_button_set_always_show_image(GTK_BUTTON(add_prompt_button), TRUE);
        gtk_box_pack_start(GTK_BOX(content), add_prompt_button, FALSE, FALSE, 0);
        g_signal_connect(add_prompt_button, "clicked", G_CALLBACK(app_settings_on_add_custom_prompt_clicked), nullptr);
        g_signal_connect(kSettingsEditPromptButton, "clicked", G_CALLBACK(app_settings_on_edit_custom_prompt_clicked), nullptr);
        g_signal_connect(kSettingsDeletePromptButton, "clicked", G_CALLBACK(app_settings_on_delete_custom_prompt_clicked), nullptr);
        g_signal_connect(kSettingsCustomPromptsList, "row-selected", G_CALLBACK(app_settings_on_prompt_row_selected), nullptr);
        g_signal_connect(
            kSettingsWindowRef,
            "destroy",
            G_CALLBACK(+[](GtkWidget*, gpointer) {
                kSettingsWindowRef = nullptr;
                kSettingsAutostartCheck = nullptr;
                kSettingsOpenAiApiKeyEntry = nullptr;
                kSettingsCustomPromptsSection = nullptr;
                kSettingsCustomPromptsList = nullptr;
                kSettingsEditPromptButton = nullptr;
                kSettingsDeletePromptButton = nullptr;
                kSettingsAppState = nullptr;
            }),
            nullptr
        );
    }
    app_settings_sync_autostart_check();
    if (kSettingsOpenAiApiKeyEntry) {
        const char* api_key = settings_store_get_openai_api_key(kSettingsAppState);
        gtk_entry_set_text(GTK_ENTRY(kSettingsOpenAiApiKeyEntry), api_key ? api_key : "");
    }
    gtk_widget_show_all(kSettingsWindowRef);
    app_settings_refresh_custom_prompts_ui();
    gtk_window_set_urgency_hint(GTK_WINDOW(kSettingsWindowRef), FALSE);
    gtk_window_deiconify(GTK_WINDOW(kSettingsWindowRef));
    gtk_window_present_with_time(GTK_WINDOW(kSettingsWindowRef), user_event_time);
}
