#pragma once

#include "app_state.h"

void audio_pipeline_set_status_handler(void (*handler)(AppState*, RunState, const char*));
void audio_pipeline_toggle_recording(AppState* app);
void audio_pipeline_stop_recording(AppState* app, bool canceled);
void audio_pipeline_cancel_processes(AppState* app);
void audio_pipeline_shutdown(AppState* app);
