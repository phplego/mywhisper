#include "wake_word.h"

#include "audio_pipeline.h"

#include <dlfcn.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <array>
#include <atomic>
#include <cstdint>
#include <string>

namespace {

constexpr uint32_t kApiVersion = 1;
constexpr const char* kLibraryName = "libmywhisper-wakeword.so";

struct Detector;
struct Api {
    uint32_t api_version;
    uint32_t sample_rate;
    Detector* (*create)();
    void (*destroy)(Detector*);
    int (*process_pcm)(Detector*, const int16_t*, size_t);
};

void* library = nullptr;
const Api* api = nullptr;
Detector* detector = nullptr;
GThread* thread = nullptr;
std::atomic_bool running{false};
std::atomic<GPid> recorder_pid{0};

std::string sibling_path(const char* executable, const char* file_name) {
    if (!executable || !*executable) return {};
    gchar* dir = g_path_get_dirname(executable);
    gchar* path = dir ? g_build_filename(dir, file_name, nullptr) : nullptr;
    std::string result = path ? path : "";
    g_free(path);
    g_free(dir);
    return result;
}

std::string find_library() {
    const char* app_image = g_getenv("APPIMAGE");
    std::string path = sibling_path(app_image, kLibraryName);
    if (!path.empty() && g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR)) return path;

    GError* error = nullptr;
    gchar* executable = g_file_read_link("/proc/self/exe", &error);
    path = sibling_path(executable, kLibraryName);
    g_free(executable);
    g_clear_error(&error);
    if (!path.empty() && g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR)) return path;

    gchar* user_path = g_build_filename(g_get_home_dir(), ".local", "lib", "mywhisper", kLibraryName, nullptr);
    path = user_path ? user_path : "";
    g_free(user_path);
    return !path.empty() && g_file_test(path.c_str(), G_FILE_TEST_IS_REGULAR) ? path : "";
}

gboolean activate_recording(gpointer data) {
    auto* app = static_cast<AppState*>(data);
    if (app && !app->shutting_down && app->settings.wake_word_enabled &&
        (app->status == RunState::Idle || app->status == RunState::Recording)) {
        g_print("wake word detected: Hey Jarvis (%s recording)\n",
                app->status == RunState::Idle ? "starting" : "stopping");
        audio_pipeline_toggle_recording(app);
    }
    return G_SOURCE_REMOVE;
}

gpointer monitor(gpointer data) {
    auto* app = static_cast<AppState*>(data);
    std::array<int16_t, 1600> pcm{};
    const int fd = app->wake_word.audio_fd;
    while (running) {
        const ssize_t bytes = read(fd, pcm.data(), pcm.size() * sizeof(pcm[0]));
        if (bytes <= 0) break;
        const int result = api->process_pcm(detector, pcm.data(), bytes / sizeof(pcm[0]));
        if (result < 0) {
            g_printerr("wake-word inference failed\n");
            break;
        }
        if (result == 1) g_main_context_invoke(nullptr, activate_recording, app);
    }
    close(fd);
    app->wake_word.audio_fd = -1;
    const GPid pid = recorder_pid.exchange(0);
    if (pid > 0) {
        waitpid(pid, nullptr, 0);
        g_spawn_close_pid(pid);
    }
    return nullptr;
}

bool start(AppState* app) {
    if (!app || !api || thread) return thread != nullptr;
    detector = api->create();
    if (!detector) return false;

    gchar rate[16];
    g_snprintf(rate, sizeof(rate), "%u", api->sample_rate);
    gchar* argv[] = {
        const_cast<gchar*>("arecord"), const_cast<gchar*>("-q"), const_cast<gchar*>("-D"), const_cast<gchar*>("default"),
        const_cast<gchar*>("-f"), const_cast<gchar*>("S16_LE"), const_cast<gchar*>("-c"), const_cast<gchar*>("1"),
        const_cast<gchar*>("-r"), rate, const_cast<gchar*>("-t"), const_cast<gchar*>("raw"), nullptr
    };
    GError* error = nullptr;
    GPid pid = 0;
    gint fd = -1;
    const gboolean spawned = g_spawn_async_with_pipes(
        nullptr, argv, nullptr, static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD),
        nullptr, nullptr, &pid, nullptr, &fd, nullptr, &error);
    if (!spawned) {
        g_printerr("failed to start wake-word recorder: %s\n", error ? error->message : "unknown error");
        g_clear_error(&error);
        api->destroy(detector);
        detector = nullptr;
        return false;
    }
    app->wake_word.audio_fd = fd;
    recorder_pid = pid;
    running = true;
    thread = g_thread_new("wake-word", monitor, app);
    if (!thread) {
        running = false;
        kill(pid, SIGINT);
        waitpid(pid, nullptr, 0);
        g_spawn_close_pid(pid);
        recorder_pid = 0;
        close(fd);
        app->wake_word.audio_fd = -1;
        api->destroy(detector);
        detector = nullptr;
        return false;
    }
    g_print("wake-word monitoring started\n");
    return true;
}

void stop() {
    running = false;
    const GPid pid = recorder_pid.load();
    if (pid > 0) kill(pid, SIGINT);
    if (thread) {
        g_thread_join(thread);
        thread = nullptr;
    }
    if (detector) {
        api->destroy(detector);
        detector = nullptr;
    }
}

}  // namespace

void wake_word_init(AppState* app) {
    if (!app) return;
    const std::string path = find_library();
    if (path.empty()) return;
    library = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!library) {
        g_printerr("failed to load wake-word library: %s\n", dlerror());
        return;
    }
    auto get_api = reinterpret_cast<const Api* (*)(uint32_t)>(dlsym(library, "mww_get_api"));
    api = get_api ? get_api(kApiVersion) : nullptr;
    if (!api || api->api_version != kApiVersion || !api->create || !api->destroy || !api->process_pcm) {
        g_printerr("incompatible wake-word library\n");
        dlclose(library);
        library = nullptr;
        api = nullptr;
        return;
    }
    app->wake_word.available = true;
    g_print("wake-word library available: %s\n", path.c_str());
    if (app->settings.wake_word_enabled && !start(app)) {
        g_printerr("failed to enable wake-word monitoring\n");
    }
}

bool wake_word_set_enabled(AppState* app, bool enabled) {
    if (!app || !app->wake_word.available) return false;
    if (enabled && !start(app)) return false;
    if (!enabled) stop();
    return settings_store_set_wake_word_enabled(app, enabled);
}

void wake_word_shutdown() {
    stop();
    api = nullptr;
    if (library) {
        dlclose(library);
        library = nullptr;
    }
}
