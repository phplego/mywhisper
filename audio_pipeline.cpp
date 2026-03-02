#include "audio_pipeline.h"
#include "transcribe.h"

#include <X11/keysym.h>
#include <X11/extensions/XTest.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>
#include <sys/wait.h>

static void (*kAudioSetStatusHandler)(AppState*, RunState, const char*) = nullptr;

static void audio_pipeline_join_thread(GThread** thread) {
    if (!thread || !*thread) return;
    g_thread_join(*thread);
    *thread = nullptr;
}

static void audio_pipeline_request_stop_pid(GPid* pid) {
    if (!pid || *pid <= 0) return;
    kill(*pid, SIGINT);
}

static void audio_pipeline_set_status(AppState* app, RunState status, const char* reason) {
    if (kAudioSetStatusHandler) {
        kAudioSetStatusHandler(app, status, reason);
    } else if (app) {
        app->status = status;
    }
}

static bool audio_pipeline_copy_to_clipboard(const std::string& text) {
    GtkClipboard* clipboard = gtk_clipboard_get(GDK_SELECTION_CLIPBOARD);
    GtkClipboard* primary = gtk_clipboard_get(GDK_SELECTION_PRIMARY);
    if (!clipboard && !primary) return false;
    if (clipboard) {
        gtk_clipboard_set_text(clipboard, text.c_str(), -1);
        gtk_clipboard_store(clipboard);
    }
    if (primary) {
        gtk_clipboard_set_text(primary, text.c_str(), -1);
        gtk_clipboard_store(primary);
    }
    return true;
}

static bool audio_pipeline_trigger_paste(AppState* app) {
    if (!app || !app->hotkey.display) return false;
    const KeyCode shift = XKeysymToKeycode(app->hotkey.display, XK_Shift_L);
    const KeyCode insert = XKeysymToKeycode(app->hotkey.display, XK_Insert);
    if (shift == 0 || insert == 0) return false;
    constexpr gulong kStepDelayUs = 12000;
    XTestFakeKeyEvent(app->hotkey.display, shift, True, CurrentTime);
    XSync(app->hotkey.display, False);
    g_usleep(kStepDelayUs);
    XTestFakeKeyEvent(app->hotkey.display, insert, True, CurrentTime);
    XSync(app->hotkey.display, False);
    g_usleep(kStepDelayUs);
    XTestFakeKeyEvent(app->hotkey.display, insert, False, CurrentTime);
    XSync(app->hotkey.display, False);
    g_usleep(kStepDelayUs);
    XTestFakeKeyEvent(app->hotkey.display, shift, False, CurrentTime);
    XSync(app->hotkey.display, False);
    return true;
}

struct TranscriptionResult {
    AppState* app = nullptr;
    bool ok = false;
    std::string text;
    std::string error;
};

static gboolean audio_pipeline_on_transcription_ready(gpointer user_data) {
    auto* result = static_cast<TranscriptionResult*>(user_data);
    if (!result || !result->app) {
        delete result;
        return G_SOURCE_REMOVE;
    }
    auto* app = result->app;
    app->audio.transcription_thread = nullptr;
    if (app->shutting_down) {
        delete result;
        return G_SOURCE_REMOVE;
    }
    audio_pipeline_set_status(app, RunState::Idle, "transcription finished");
    if (result->ok) {
        const bool copied = audio_pipeline_copy_to_clipboard(result->text);
        if (copied) {
            constexpr guint kPasteDelayMs = 160;
            g_timeout_add_full(
                G_PRIORITY_DEFAULT,
                kPasteDelayMs,
                +[](gpointer data) -> gboolean {
                    auto* delayed_app = static_cast<AppState*>(data);
                    const bool pasted = audio_pipeline_trigger_paste(delayed_app);
                    g_print("autopaste: %s\n", pasted ? "ok" : "failed");
                    return G_SOURCE_REMOVE;
                },
                app,
                nullptr
            );
        }
        g_print("transcript: %s\n", result->text.c_str());
        g_print("clipboard: %s\n", copied ? "ok" : "failed");
    } else {
        g_printerr("transcription failed: %s\n", result->error.c_str());
    }
    delete result;
    return G_SOURCE_REMOVE;
}

struct TranscriptionJob {
    AppState* app = nullptr;
    std::vector<unsigned char> audio_data;
    std::string api_key;
    std::string prompt;
};

static gpointer audio_pipeline_transcription_thread(gpointer data) {
    auto* job = static_cast<TranscriptionJob*>(data);
    auto* result = new TranscriptionResult();
    result->app = job ? job->app : nullptr;
    if (!job || !job->app) {
        result->ok = false;
        result->error = "invalid transcription job";
    } else {
        result->ok = transcribe_with_openai(job->audio_data, job->api_key, job->prompt, &result->text, &result->error);
    }
    delete job;
    g_main_context_invoke(nullptr, audio_pipeline_on_transcription_ready, result);
    return nullptr;
}

static bool audio_pipeline_start_transcription_async(AppState* app, std::vector<unsigned char> audio_data) {
    if (!app || audio_data.empty()) return false;
    if (app->status != RunState::Idle) {
        g_printerr("transcription skipped: already running\n");
        return false;
    }
    if (app->audio.transcription_thread) {
        g_printerr("transcription skipped: previous transcription thread is still active\n");
        return false;
    }
    if (app->settings.openai_api_key.empty()) {
        GtkWidget* dialog = gtk_message_dialog_new(
            nullptr,
            static_cast<GtkDialogFlags>(GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT),
            GTK_MESSAGE_INFO,
            GTK_BUTTONS_OK,
            "%s",
            "OpenAI API Key is not set."
        );
        gtk_message_dialog_format_secondary_text(GTK_MESSAGE_DIALOG(dialog), "%s", "Open Settings and enter your API key.");
        gtk_window_set_title(GTK_WINDOW(dialog), "MyWhisper");
        gtk_window_set_position(GTK_WINDOW(dialog), GTK_WIN_POS_CENTER);
        gtk_dialog_run(GTK_DIALOG(dialog));
        gtk_widget_destroy(dialog);
        return false;
    }
    std::string prompt;
    if (app->settings.active_prompt_index >= 0 && static_cast<size_t>(app->settings.active_prompt_index) < app->settings.custom_prompts.size()) {
        prompt = app->settings.custom_prompts[app->settings.active_prompt_index].text;
    }
    audio_pipeline_set_status(app, RunState::Transcribing, "transcription started");
    auto* job = new TranscriptionJob();
    job->app = app;
    job->audio_data = std::move(audio_data);
    job->api_key = app->settings.openai_api_key;
    job->prompt = std::move(prompt);
    app->audio.transcription_thread = g_thread_new("openai-transcribe", audio_pipeline_transcription_thread, job);
    if (!app->audio.transcription_thread) {
        delete job;
        audio_pipeline_set_status(app, RunState::Idle, "failed to start transcription thread");
        g_printerr("failed to create transcription thread\n");
        return false;
    }
    return true;
}

struct RecorderReadJob {
    AppState* app = nullptr;
    gint fd = -1;
};

static gpointer audio_pipeline_recorder_reader_thread(gpointer data) {
    auto* job = static_cast<RecorderReadJob*>(data);
    if (!job || !job->app || job->fd < 0) {
        delete job;
        return nullptr;
    }
    unsigned char chunk[8192];
    while (true) {
        const ssize_t n = read(job->fd, chunk, sizeof(chunk));
        if (n > 0) {
            g_mutex_lock(&job->app->audio.audio_mutex);
            job->app->audio.audio_buffer.insert(job->app->audio.audio_buffer.end(), chunk, chunk + n);
            g_mutex_unlock(&job->app->audio.audio_mutex);
            continue;
        }
        if (n == 0) break;
        if (errno == EINTR) continue;
        break;
    }
    close(job->fd);
    delete job;
    return nullptr;
}

static void audio_pipeline_on_recorder_exit(GPid pid, gint, gpointer user_data) {
    auto* app = static_cast<AppState*>(user_data);
    if (!app) return;
    if (pid == app->audio.recorder_pid) {
        app->audio.recorder_pid = 0;
        g_spawn_close_pid(pid);
        return;
    }
    if (pid != app->audio.encoder_pid) {
        g_spawn_close_pid(pid);
        return;
    }
    audio_pipeline_set_status(app, RunState::Idle, "recording pipeline finished");
    app->audio.encoder_pid = 0;
    if (app->audio.recorder_reader_thread) {
        g_thread_join(app->audio.recorder_reader_thread);
        app->audio.recorder_reader_thread = nullptr;
    }
    std::vector<unsigned char> audio_data;
    g_mutex_lock(&app->audio.audio_mutex);
    audio_data = app->audio.audio_buffer;
    app->audio.audio_buffer.clear();
    g_mutex_unlock(&app->audio.audio_mutex);
    const bool canceled = app->audio.cancel_requested;
    app->audio.cancel_requested = false;
    if (canceled) {
        g_print("recording canceled\n");
    } else {
        audio_pipeline_start_transcription_async(app, std::move(audio_data));
    }
    g_spawn_close_pid(pid);
}

void audio_pipeline_stop_recording(AppState* app, bool canceled) {
    if (!app || app->status != RunState::Recording || app->audio.recorder_pid <= 0) return;
    app->audio.cancel_requested = canceled;
    audio_pipeline_request_stop_pid(&app->audio.recorder_pid);
}

static void audio_pipeline_start_recording(AppState* app) {
    if (!app || app->status != RunState::Idle) return;
    gchar* argv[] = {
        const_cast<gchar*>("arecord"), const_cast<gchar*>("-q"), const_cast<gchar*>("-f"), const_cast<gchar*>("S16_LE"),
        const_cast<gchar*>("-c"), const_cast<gchar*>("1"), const_cast<gchar*>("-r"), const_cast<gchar*>("16000"),
        const_cast<gchar*>("-t"), const_cast<gchar*>("raw"), const_cast<gchar*>("-d"), const_cast<gchar*>("600"), nullptr
    };
    GError* error = nullptr;
    GPid recorder_pid = 0;
    gint pcm_stdout_fd = -1;
    const gboolean recorder_ok = g_spawn_async_with_pipes(
        nullptr, argv, nullptr,
        static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD),
        nullptr, nullptr, &recorder_pid, nullptr, &pcm_stdout_fd, nullptr, &error
    );
    if (!recorder_ok) {
        g_printerr("failed to start arecord: %s\n", error ? error->message : "unknown error");
        g_clear_error(&error);
        return;
    }
    gchar* ffmpeg_argv[] = {
        const_cast<gchar*>("ffmpeg"), const_cast<gchar*>("-hide_banner"), const_cast<gchar*>("-loglevel"), const_cast<gchar*>("error"),
        const_cast<gchar*>("-f"), const_cast<gchar*>("s16le"), const_cast<gchar*>("-ar"), const_cast<gchar*>("16000"),
        const_cast<gchar*>("-ac"), const_cast<gchar*>("1"), const_cast<gchar*>("-i"), const_cast<gchar*>("pipe:0"),
        const_cast<gchar*>("-c:a"), const_cast<gchar*>("libopus"), const_cast<gchar*>("-b:a"), const_cast<gchar*>("24k"),
        const_cast<gchar*>("-vbr"), const_cast<gchar*>("on"), const_cast<gchar*>("-application"), const_cast<gchar*>("voip"),
        const_cast<gchar*>("-f"), const_cast<gchar*>("webm"), const_cast<gchar*>("pipe:1"), nullptr
    };
    GPid encoder_pid = 0;
    gint encoded_stdout_fd = -1;
    const gboolean encoder_ok = g_spawn_async_with_pipes(
        nullptr, ffmpeg_argv, nullptr,
        static_cast<GSpawnFlags>(G_SPAWN_SEARCH_PATH | G_SPAWN_DO_NOT_REAP_CHILD),
        +[](gpointer data) {
            const int pcm_fd = *static_cast<int*>(data);
            if (dup2(pcm_fd, STDIN_FILENO) < 0) _exit(127);
            if (pcm_fd != STDIN_FILENO) close(pcm_fd);
        },
        &pcm_stdout_fd, &encoder_pid, nullptr, &encoded_stdout_fd, nullptr, &error
    );
    close(pcm_stdout_fd);
    if (!encoder_ok) {
        g_printerr("failed to start ffmpeg: %s\n", error ? error->message : "unknown error");
        g_clear_error(&error);
        kill(recorder_pid, SIGINT);
        waitpid(recorder_pid, nullptr, 0);
        g_spawn_close_pid(recorder_pid);
        return;
    }
    audio_pipeline_set_status(app, RunState::Recording, "recording started");
    app->audio.cancel_requested = false;
    app->audio.recorder_pid = recorder_pid;
    app->audio.encoder_pid = encoder_pid;
    g_mutex_lock(&app->audio.audio_mutex);
    app->audio.audio_buffer.clear();
    g_mutex_unlock(&app->audio.audio_mutex);
    auto* read_job = new RecorderReadJob();
    read_job->app = app;
    read_job->fd = encoded_stdout_fd;
    app->audio.recorder_reader_thread = g_thread_new("audio-read", audio_pipeline_recorder_reader_thread, read_job);
    g_child_watch_add(recorder_pid, audio_pipeline_on_recorder_exit, app);
    g_child_watch_add(encoder_pid, audio_pipeline_on_recorder_exit, app);
    g_print("recording started (in-memory webm/opus buffer)\n");
}

void audio_pipeline_toggle_recording(AppState* app) {
    if (!app) return;
    if (app->status == RunState::Recording) {
        audio_pipeline_stop_recording(app, false);
    } else if (app->status == RunState::Idle) {
        audio_pipeline_start_recording(app);
    }
}

void audio_pipeline_cancel_processes(AppState* app) {
    if (!app) return;
    audio_pipeline_request_stop_pid(&app->audio.recorder_pid);
    audio_pipeline_request_stop_pid(&app->audio.encoder_pid);
}

void audio_pipeline_shutdown(AppState* app) {
    if (!app) return;
    app->shutting_down = true;
    audio_pipeline_join_thread(&app->audio.recorder_reader_thread);
    audio_pipeline_join_thread(&app->audio.transcription_thread);
}

void audio_pipeline_set_status_handler(void (*handler)(AppState*, RunState, const char*)) {
    kAudioSetStatusHandler = handler;
}
