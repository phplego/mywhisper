#pragma once

#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
#include <X11/Xlib.h>

#include <string>
#include <vector>

enum class RunState { Idle = 0, Recording = 1, Transcribing = 2 };

struct CustomPrompt {
    std::string title;
    std::string text;
};

struct UiState {
    AppIndicator* indicator = nullptr;
    GtkWidget* toggle_item = nullptr;
    GtkWidget* menu = nullptr;
    RunState rendered_status = RunState::Idle;
    gint animation_frame = 0;
    std::string idle_icon_path;
    std::vector<std::string> recording_icon_paths;
    std::vector<std::string> transcribing_icon_paths;
};

struct HotkeyState {
    Display* display = nullptr;
    KeyCode trigger_key = 0;
    KeyCode esc = 0;
    bool trigger_key_down = false;
    bool esc_down = false;
    gint64 last_trigger_press_ms = 0;
};

struct AudioState {
    GPid recorder_pid = 0;
    GPid encoder_pid = 0;
    GThread* recorder_reader_thread = nullptr;
    GThread* transcription_thread = nullptr;
    GMutex audio_mutex;
    std::vector<unsigned char> audio_buffer;
    bool cancel_requested = false;
};

struct SettingsState {
    std::string openai_api_key;
    std::string trigger_modifier = "ctrl";
    int trigger_press_window_ms = 500;
    std::vector<CustomPrompt> custom_prompts;
    int active_prompt_index = -1;
};

struct AppState {
    UiState ui;
    HotkeyState hotkey;
    AudioState audio;
    SettingsState settings;
    RunState status = RunState::Idle;
    bool shutting_down = false;
};

void app_settings_show_window(GtkApplication* application, AppState* app, guint32 user_event_time);

std::string settings_store_find_icon_path(const char* file_name);
std::string settings_store_trim_text(std::string value);
void settings_store_load_persisted_state(AppState* app);
void settings_store_set_prompt_change_hook(void (*hook)(AppState*));
bool settings_store_set_active_prompt(AppState* app, int index);

bool settings_store_is_autostart_enabled();
bool settings_store_enable_autostart();
bool settings_store_disable_autostart();

size_t settings_store_get_custom_prompt_count(const AppState* app);
const char* settings_store_get_custom_prompt_title(const AppState* app, size_t index);
const char* settings_store_get_custom_prompt_text(const AppState* app, size_t index);
bool settings_store_add_custom_prompt(AppState* app, const char* title, const char* text);
bool settings_store_update_custom_prompt(AppState* app, size_t index, const char* title, const char* text);
bool settings_store_remove_custom_prompt(AppState* app, size_t index);
const char* settings_store_get_openai_api_key(const AppState* app);
bool settings_store_set_openai_api_key(AppState* app, const char* api_key);
const char* settings_store_get_trigger_modifier(const AppState* app);
bool settings_store_set_trigger_modifier(AppState* app, const char* modifier);
int settings_store_get_trigger_press_window_ms(const AppState* app);
bool settings_store_set_trigger_press_window_ms(AppState* app, int window_ms);
