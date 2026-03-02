#include "app_state.h"

#include <algorithm>
#include <errno.h>
#include <unistd.h>

static void (*kStorePromptChangeHook)(AppState*) = nullptr;

static std::string settings_store_get_path_near_exe(const char* file_name, GFileTest test) {
    if (!file_name || *file_name == '\0') return "";
    GError* read_link_error = nullptr;
    gchar* exe_path = g_file_read_link("/proc/self/exe", &read_link_error);
    if (!exe_path) {
        g_clear_error(&read_link_error);
        return "";
    }
    gchar* exe_dir = g_path_get_dirname(exe_path);
    gchar* abs_path = exe_dir ? g_build_filename(exe_dir, file_name, nullptr) : nullptr;
    std::string out = (abs_path && g_file_test(abs_path, test)) ? std::string(abs_path) : "";
    g_free(abs_path);
    g_free(exe_dir);
    g_free(exe_path);
    g_clear_error(&read_link_error);
    return out;
}

std::string settings_store_find_icon_path(const char* file_name) {
    return settings_store_get_path_near_exe(file_name, G_FILE_TEST_EXISTS);
}

static std::string settings_store_get_home_config_path(const char* dir_name, const char* file_name) {
    if (!dir_name || !file_name) return "";
    const char* home = g_get_home_dir();
    if (!home || *home == '\0') return "";
    gchar* path = g_build_filename(home, ".config", dir_name, file_name, nullptr);
    std::string out = path ? std::string(path) : "";
    g_free(path);
    return out;
}

static std::string settings_store_get_autostart_desktop_path() {
    return settings_store_get_home_config_path("autostart", "mywhisper.desktop");
}

static std::string settings_store_get_custom_prompts_store_path() {
    return settings_store_get_home_config_path("mywhisper", "custom_prompts.ini");
}

static std::string settings_store_get_settings_store_path() {
    return settings_store_get_home_config_path("mywhisper", "settings.ini");
}

std::string settings_store_trim_text(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) { return !g_ascii_isspace(c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) { return !g_ascii_isspace(c); }).base(), value.end());
    return value;
}

bool settings_store_is_autostart_enabled() {
    const std::string path = settings_store_get_autostart_desktop_path();
    return !path.empty() && g_file_test(path.c_str(), G_FILE_TEST_EXISTS);
}

bool settings_store_disable_autostart() {
    const std::string path = settings_store_get_autostart_desktop_path();
    return !path.empty() && (unlink(path.c_str()) == 0 || errno == ENOENT);
}

bool settings_store_enable_autostart() {
    const std::string path = settings_store_get_autostart_desktop_path();
    if (path.empty()) return false;
    gchar* dir = g_path_get_dirname(path.c_str());
    if (!dir) return false;
    const bool dir_ok = g_mkdir_with_parents(dir, 0700) == 0;
    g_free(dir);
    if (!dir_ok) return false;
    const std::string app_path = settings_store_get_path_near_exe("app.out", static_cast<GFileTest>(G_FILE_TEST_EXISTS | G_FILE_TEST_IS_EXECUTABLE));
    if (app_path.empty()) return false;
    const std::string content =
        "[Desktop Entry]\nType=Application\nName=mywhisper\nExec=" + app_path +
        "\nX-GNOME-Autostart-enabled=true\nX-GNOME-Autostart-Delay=10\nTerminal=false\n";
    GError* write_error = nullptr;
    const gboolean ok = g_file_set_contents(path.c_str(), content.c_str(), -1, &write_error);
    if (!ok) g_clear_error(&write_error);
    return ok == TRUE;
}

static void settings_store_save_app_settings(const AppState* app) {
    if (!app) return;
    const std::string path = settings_store_get_settings_store_path();
    if (path.empty()) return;
    gchar* dir = g_path_get_dirname(path.c_str());
    if (!dir) return;
    const int mk_ok = g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    if (mk_ok != 0) return;
    GKeyFile* key_file = g_key_file_new();
    g_key_file_set_string(key_file, "settings", "openai_api_key", app->settings.openai_api_key.c_str());
    gsize data_size = 0;
    GError* serialize_error = nullptr;
    gchar* data = g_key_file_to_data(key_file, &data_size, &serialize_error);
    if (!data) {
        g_clear_error(&serialize_error);
        g_key_file_free(key_file);
        return;
    }
    GError* write_error = nullptr;
    const gboolean write_ok = g_file_set_contents(path.c_str(), data, data_size, &write_error);
    if (!write_ok) g_clear_error(&write_error);
    g_free(data);
    g_key_file_free(key_file);
}

static void settings_store_load_app_settings(AppState* app) {
    if (!app) return;
    app->settings.openai_api_key.clear();
    const std::string path = settings_store_get_settings_store_path();
    if (path.empty() || !g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) return;
    GError* load_error = nullptr;
    GKeyFile* key_file = g_key_file_new();
    const gboolean ok = g_key_file_load_from_file(key_file, path.c_str(), G_KEY_FILE_NONE, &load_error);
    if (!ok) {
        g_clear_error(&load_error);
        g_key_file_free(key_file);
        return;
    }
    GError* key_error = nullptr;
    gchar* key_raw = g_key_file_get_string(key_file, "settings", "openai_api_key", &key_error);
    if (!key_error && key_raw) app->settings.openai_api_key = settings_store_trim_text(key_raw);
    g_clear_error(&key_error);
    g_free(key_raw);
    g_key_file_free(key_file);
}

static void settings_store_save_custom_prompts(const AppState* app) {
    if (!app) return;
    const std::string path = settings_store_get_custom_prompts_store_path();
    if (path.empty()) return;
    gchar* dir = g_path_get_dirname(path.c_str());
    if (!dir) return;
    const int mk_ok = g_mkdir_with_parents(dir, 0700);
    g_free(dir);
    if (mk_ok != 0) return;
    GKeyFile* key_file = g_key_file_new();
    g_key_file_set_integer(key_file, "prompts", "count", static_cast<gint>(app->settings.custom_prompts.size()));
    g_key_file_set_integer(key_file, "prompts", "active_index", app->settings.active_prompt_index);
    for (size_t i = 0; i < app->settings.custom_prompts.size(); ++i) {
        gchar* title_key = g_strdup_printf("title_%zu", i);
        gchar* text_key = g_strdup_printf("text_%zu", i);
        g_key_file_set_string(key_file, "prompts", title_key, app->settings.custom_prompts[i].title.c_str());
        g_key_file_set_string(key_file, "prompts", text_key, app->settings.custom_prompts[i].text.c_str());
        g_free(title_key);
        g_free(text_key);
    }
    gsize data_size = 0;
    GError* serialize_error = nullptr;
    gchar* data = g_key_file_to_data(key_file, &data_size, &serialize_error);
    if (!data) {
        g_clear_error(&serialize_error);
        g_key_file_free(key_file);
        return;
    }
    GError* write_error = nullptr;
    const gboolean write_ok = g_file_set_contents(path.c_str(), data, data_size, &write_error);
    if (!write_ok) g_clear_error(&write_error);
    g_free(data);
    g_key_file_free(key_file);
}

static void settings_store_load_custom_prompts(AppState* app) {
    if (!app) return;
    app->settings.custom_prompts.clear();
    app->settings.active_prompt_index = -1;
    const std::string path = settings_store_get_custom_prompts_store_path();
    if (path.empty() || !g_file_test(path.c_str(), G_FILE_TEST_EXISTS)) return;
    GError* load_error = nullptr;
    GKeyFile* key_file = g_key_file_new();
    const gboolean ok = g_key_file_load_from_file(key_file, path.c_str(), G_KEY_FILE_NONE, &load_error);
    if (!ok) {
        g_clear_error(&load_error);
        g_key_file_free(key_file);
        return;
    }
    GError* count_error = nullptr;
    const gint count = g_key_file_get_integer(key_file, "prompts", "count", &count_error);
    if (count_error) {
        g_clear_error(&count_error);
    } else if (count > 0) {
        for (gint i = 0; i < count; ++i) {
            gchar* title_key = g_strdup_printf("title_%d", i);
            gchar* text_key = g_strdup_printf("text_%d", i);
            GError* title_error = nullptr;
            GError* text_error = nullptr;
            gchar* title_raw = g_key_file_get_string(key_file, "prompts", title_key, &title_error);
            gchar* text_raw = g_key_file_get_string(key_file, "prompts", text_key, &text_error);
            if (!title_error && !text_error && title_raw && text_raw) {
                const std::string title = settings_store_trim_text(title_raw);
                const std::string text = settings_store_trim_text(text_raw);
                if (!title.empty() && !text.empty()) app->settings.custom_prompts.push_back({title, text});
            }
            g_clear_error(&title_error);
            g_clear_error(&text_error);
            g_free(title_raw);
            g_free(text_raw);
            g_free(title_key);
            g_free(text_key);
        }
    }
    GError* active_error = nullptr;
    const gint active = g_key_file_get_integer(key_file, "prompts", "active_index", &active_error);
    if (active_error) {
        g_clear_error(&active_error);
    } else {
        app->settings.active_prompt_index = active;
    }
    if (app->settings.active_prompt_index < 0 || static_cast<size_t>(app->settings.active_prompt_index) >= app->settings.custom_prompts.size()) {
        app->settings.active_prompt_index = -1;
    }
    g_key_file_free(key_file);
}

void settings_store_load_persisted_state(AppState* app) {
    settings_store_load_app_settings(app);
    settings_store_load_custom_prompts(app);
}

void settings_store_set_prompt_change_hook(void (*hook)(AppState*)) {
    kStorePromptChangeHook = hook;
}

static void settings_store_notify_prompts_changed(AppState* app) {
    settings_store_save_custom_prompts(app);
    if (kStorePromptChangeHook) kStorePromptChangeHook(app);
}

static bool settings_store_parse_custom_prompt(const char* title, const char* text, std::string* clean_title, std::string* clean_text) {
    if (!title || !text || !clean_title || !clean_text) return false;
    *clean_title = settings_store_trim_text(title);
    *clean_text = settings_store_trim_text(text);
    return !clean_title->empty() && !clean_text->empty();
}

size_t settings_store_get_custom_prompt_count(const AppState* app) {
    return app ? app->settings.custom_prompts.size() : 0;
}

const char* settings_store_get_custom_prompt_title(const AppState* app, size_t index) {
    return (!app || index >= app->settings.custom_prompts.size()) ? nullptr : app->settings.custom_prompts[index].title.c_str();
}

const char* settings_store_get_custom_prompt_text(const AppState* app, size_t index) {
    return (!app || index >= app->settings.custom_prompts.size()) ? nullptr : app->settings.custom_prompts[index].text.c_str();
}

bool settings_store_set_active_prompt(AppState* app, int index) {
    if (!app || index < -1 || (index >= 0 && static_cast<size_t>(index) >= app->settings.custom_prompts.size())) return false;
    if (index == app->settings.active_prompt_index) return true;
    app->settings.active_prompt_index = index;
    settings_store_notify_prompts_changed(app);
    return true;
}

bool settings_store_add_custom_prompt(AppState* app, const char* title, const char* text) {
    if (!app) return false;
    std::string clean_title;
    std::string clean_text;
    if (!settings_store_parse_custom_prompt(title, text, &clean_title, &clean_text)) return false;
    app->settings.custom_prompts.push_back({clean_title, clean_text});
    settings_store_notify_prompts_changed(app);
    return true;
}

bool settings_store_update_custom_prompt(AppState* app, size_t index, const char* title, const char* text) {
    if (!app || index >= app->settings.custom_prompts.size()) return false;
    std::string clean_title;
    std::string clean_text;
    if (!settings_store_parse_custom_prompt(title, text, &clean_title, &clean_text)) return false;
    app->settings.custom_prompts[index].title = clean_title;
    app->settings.custom_prompts[index].text = clean_text;
    settings_store_notify_prompts_changed(app);
    return true;
}

bool settings_store_remove_custom_prompt(AppState* app, size_t index) {
    if (!app || index >= app->settings.custom_prompts.size()) return false;
    app->settings.custom_prompts.erase(app->settings.custom_prompts.begin() + static_cast<std::vector<CustomPrompt>::difference_type>(index));
    if (app->settings.custom_prompts.empty() || app->settings.active_prompt_index == static_cast<int>(index)) {
        app->settings.active_prompt_index = -1;
    } else if (app->settings.active_prompt_index > static_cast<int>(index)) {
        app->settings.active_prompt_index -= 1;
    }
    settings_store_notify_prompts_changed(app);
    return true;
}

const char* settings_store_get_openai_api_key(const AppState* app) {
    return app ? app->settings.openai_api_key.c_str() : nullptr;
}

bool settings_store_set_openai_api_key(AppState* app, const char* api_key) {
    if (!app || !api_key) return false;
    const std::string clean_key = settings_store_trim_text(api_key);
    if (clean_key == app->settings.openai_api_key) return true;
    app->settings.openai_api_key = clean_key;
    settings_store_save_app_settings(app);
    return true;
}
