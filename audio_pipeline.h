#pragma once

#include "app_state.h"

// Installs the callback used to publish audio-pipeline state transitions.
void audio_pipeline_set_status_handler(void (*handler)(AppState*, RunState, const char*));
// Starts recording when idle or stops it when already recording.
void audio_pipeline_toggle_recording(AppState* app);
// Stops recording and optionally discards the captured audio.
void audio_pipeline_stop_recording(AppState* app, bool canceled);
// Requests termination of active recorder and encoder processes.
void audio_pipeline_cancel_processes(AppState* app);
// Stops child processes and joins all audio worker threads.
void audio_pipeline_shutdown(AppState* app);
