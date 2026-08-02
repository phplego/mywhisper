#pragma once

#include <gtk/gtk.h>
#include <libayatana-appindicator/app-indicator.h>
#include <X11/Xlib.h>

#include <string>
#include <vector>

enum class RunState { Idle = 0, Recording = 1, Transcribing = 2 }; // Drives tray and overlay presentation.

struct CustomPrompt {
    std::string title;
    std::string text;
};

struct UiState {
    AppIndicator* indicator = nullptr; // Desktop tray indicator owned by the application.
    GtkWidget* toggle_item = nullptr; // Menu command whose label follows the run state.
    GtkWidget* menu = nullptr; // Rebuilt whenever the prompt collection changes.
    RunState rendered_status = RunState::Idle; // Last state applied by the tray timer.
    guint idle_refresh_source = 0; // Delayed idle-icon refresh source, or zero.
    gint animation_frame = 0; // Current recording/transcription icon frame.
    std::string idle_icon_path;
    std::vector<std::string> recording_icon_paths;
    std::vector<std::string> transcribing_icon_paths;
};

struct HotkeyState {
    Display* display = nullptr; // Dedicated X11 connection used for polling and synthetic input.
    KeyCode trigger_key = 0; // Configured Ctrl, Shift, or Alt keycode.
    KeyCode esc = 0; // Escape keycode captured only while recording.
    bool trigger_key_down = false;
    bool esc_down = false;
    bool esc_captured = false;
    gint64 last_trigger_press_ms = 0; // Monotonic timestamp used for double-press detection.
};

struct AudioState {
    GPid recorder_pid = 0; // arecord child process.
    GPid encoder_pid = 0; // opusenc child process.
    GThread* recorder_reader_thread = nullptr;
    GThread* transcription_thread = nullptr;
    GMutex audio_mutex;
    std::vector<unsigned char> audio_buffer; // In-memory Ogg/Opus payload sent to OpenAI.
    bool cancel_requested = false; // Suppresses transcription after cancellation.
};

struct SettingsState {
    std::string openai_api_key;
    std::string trigger_modifier = "ctrl";
    int trigger_press_window_ms = 500;
    std::vector<CustomPrompt> custom_prompts;
    int active_prompt_index = -1; // -1 selects the API's default prompt.
};

struct AppState {
    UiState ui;
    HotkeyState hotkey;
    AudioState audio;
    SettingsState settings;
    RunState status = RunState::Idle;
    bool shutting_down = false; // Prevents worker completion from touching torn-down UI.
};

// Opens or raises the application settings window.
void app_settings_show_window(GtkApplication* application, AppState* app, guint32 user_event_time);

// Resolves an icon bundled next to the executable.
std::string settings_store_find_icon_path(const char* file_name);
// Removes leading and trailing ASCII whitespace.
std::string settings_store_trim_text(std::string value);
// Loads application preferences and custom prompts into memory.
void settings_store_load_persisted_state(AppState* app);
// Installs the callback used after prompt state changes.
void settings_store_set_prompt_change_hook(void (*hook)(AppState*));
// Selects a custom prompt, or -1 for no prompt.
bool settings_store_set_active_prompt(AppState* app, int index);

// Reports whether the XDG autostart desktop entry exists.
bool settings_store_is_autostart_enabled();
// Creates the XDG autostart desktop entry.
bool settings_store_enable_autostart();
// Removes the XDG autostart desktop entry.
bool settings_store_disable_autostart();

// Returns the number of configured custom prompts.
size_t settings_store_get_custom_prompt_count(const AppState* app);
// Returns a prompt title by index without transferring ownership.
const char* settings_store_get_custom_prompt_title(const AppState* app, size_t index);
// Returns prompt text by index without transferring ownership.
const char* settings_store_get_custom_prompt_text(const AppState* app, size_t index);
// Validates and appends a custom prompt.
bool settings_store_add_custom_prompt(AppState* app, const char* title, const char* text);
// Validates and replaces a custom prompt.
bool settings_store_update_custom_prompt(AppState* app, size_t index, const char* title, const char* text);
// Removes a custom prompt and adjusts the active selection.
bool settings_store_remove_custom_prompt(AppState* app, size_t index);
// Returns the in-memory OpenAI API key without transferring ownership.
const char* settings_store_get_openai_api_key(const AppState* app);
// Trims, stores, and persists the OpenAI API key.
bool settings_store_set_openai_api_key(AppState* app, const char* api_key);
// Returns the normalized global trigger modifier.
const char* settings_store_get_trigger_modifier(const AppState* app);
// Normalizes and persists the global trigger modifier.
bool settings_store_set_trigger_modifier(AppState* app, const char* modifier);
// Returns the configured double-press interval in milliseconds.
int settings_store_get_trigger_press_window_ms(const AppState* app);
// Clamps and persists the double-press interval.
bool settings_store_set_trigger_press_window_ms(AppState* app, int window_ms);
