#include "transcribe.h"
#include <libsoup/soup.h>

#include <algorithm>

static constexpr const char* kTranscriptionModel = "gpt-4o-transcribe";

static std::string transcribe_trim_text(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) { return !g_ascii_isspace(c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) { return !g_ascii_isspace(c); }).base(), value.end());
    return value;
}

bool transcribe_with_openai(
    const std::vector<unsigned char>& audio_data,
    const std::string& api_key,
    const std::string& prompt,
    std::string* out_text,
    std::string* out_error
) {
    bool ok = false;
    GError* error = nullptr;
    SoupSession* session = nullptr;
    SoupMessage* message = nullptr;
    GBytes* response = nullptr;
    std::string auth;
    std::string body;
    unsigned status = 0;
    bool http_ok = false;
    std::string text;
    if (audio_data.empty()) {
        if (out_error) *out_error = "audio buffer is empty";
        return false;
    }
    g_print("sending compressed audio size: %zu bytes\n", audio_data.size());
    GBytes* audio_bytes = g_bytes_new(audio_data.data(), audio_data.size());
    SoupMultipart* multipart = soup_multipart_new("multipart/form-data");
    soup_multipart_append_form_string(multipart, "model", kTranscriptionModel);
    soup_multipart_append_form_string(multipart, "response_format", "text");
    if (!prompt.empty()) {
        g_print("transcribe prompt: %s\n", prompt.c_str());
        soup_multipart_append_form_string(multipart, "prompt", prompt.c_str());
    } else {
        g_print("transcribe prompt: <default/no prompt>\n");
    }
    soup_multipart_append_form_file(multipart, "file", "audio.webm", "audio/webm", audio_bytes);
    g_bytes_unref(audio_bytes);
    message = soup_message_new_from_multipart("https://api.openai.com/v1/audio/transcriptions", multipart);
    soup_multipart_free(multipart);
    if (!message) {
        if (out_error) *out_error = "failed to build HTTP request";
        goto cleanup;
    }
    auth = "Bearer " + api_key;
    soup_message_headers_append(soup_message_get_request_headers(message), "Authorization", auth.c_str());
    session = soup_session_new();
    response = soup_session_send_and_read(session, message, nullptr, &error);
    if (response) {
        gsize size = 0;
        const auto* raw = static_cast<const char*>(g_bytes_get_data(response, &size));
        if (raw && size > 0) body.assign(raw, size);
    }
    status = soup_message_get_status(message);
    http_ok = status >= 200 && status < 300;
    if (!response || !http_ok) {
        if (out_error) *out_error = error ? std::string("request failed: ") + error->message : "http " + std::to_string(status) + ": " + transcribe_trim_text(body);
        goto cleanup;
    }
    text = transcribe_trim_text(body);
    if (text.empty()) {
        if (out_error) *out_error = "empty transcription";
        goto cleanup;
    }
    if (out_text) *out_text = text;
    ok = true;
cleanup:
    g_clear_error(&error);
    if (response) g_bytes_unref(response);
    if (session) g_object_unref(session);
    if (message) g_object_unref(message);
    return ok;
}
