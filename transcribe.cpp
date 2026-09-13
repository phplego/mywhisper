#include "transcribe.h"
#include <libsoup/soup.h>
#include <json-glib/json-glib.h>

#include <algorithm>
#include <cstring>
#include <utility>

static constexpr const char* kTranscriptionModel = "gpt-transcribe";

// Removes surrounding whitespace from API text and error payloads.
static std::string transcribe_trim_text(std::string value) {
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char c) { return !g_ascii_isspace(c); }));
    value.erase(std::find_if(value.rbegin(), value.rend(), [](unsigned char c) { return !g_ascii_isspace(c); }).base(), value.end());
    return value;
}

// Sends either API request using the same authentication and error handling.
static bool transcribe_send(SoupMessage* message, const std::string& api_key, const char* stage,
                            std::string* body, std::string* out_error) {
    if (!message) {
        if (out_error) *out_error = "failed to build HTTP request";
        return false;
    }
    const std::string auth = "Bearer " + api_key;
    soup_message_headers_append(soup_message_get_request_headers(message), "Authorization", auth.c_str());
    g_autoptr(SoupSession) session = soup_session_new();
    g_autoptr(GError) error = nullptr;
    const gint64 started = g_get_monotonic_time();
    g_autoptr(GBytes) response = soup_session_send_and_read(session, message, nullptr, &error);
    g_print("%s request time: %.3f s\n", stage,
            (g_get_monotonic_time() - started) / static_cast<double>(G_USEC_PER_SEC));
    if (response) {
        gsize size = 0;
        const auto* raw = static_cast<const char*>(g_bytes_get_data(response, &size));
        if (raw && size) body->assign(raw, size);
    }
    const unsigned status = soup_message_get_status(message);
    const bool ok = response && status >= 200 && status < 300;
    if (!ok && out_error) {
        *out_error = error ? std::string("request failed: ") + error->message
                           : "http " + std::to_string(status) + ": " + transcribe_trim_text(*body);
    }
    g_object_unref(message);
    return ok;
}

// Prints the API's usage object unchanged, including any token breakdowns.
static void transcribe_log_usage(JsonParser* parser, const char* stage) {
    JsonNode* root = json_parser_get_root(parser);
    JsonNode* usage = JSON_NODE_HOLDS_OBJECT(root)
        ? json_object_get_member(json_node_get_object(root), "usage") : nullptr;
    gchar* value = usage ? json_to_string(usage, FALSE) : nullptr;
    g_print("%s usage: %s\n", stage, value ? value : "unavailable");
    g_free(value);
}

// Applies editing instructions to the completed transcript, never to the audio.
static bool transcribe_apply_prompt(const std::string& transcript, const std::string& api_key,
                                    const std::string& prompt, std::string* text, std::string* error) {
    g_autoptr(JsonBuilder) builder = json_builder_new();
    json_builder_begin_object(builder);
    json_builder_set_member_name(builder, "model");
    json_builder_add_string_value(builder, "gpt-5-nano");
    json_builder_set_member_name(builder, "reasoning_effort");
    json_builder_add_string_value(builder, "minimal");
    json_builder_set_member_name(builder, "messages");
    json_builder_begin_array(builder);
    const std::string instructions = "Apply the following instructions to the user's transcript. "
        "Return only the resulting text, without commentary.\n\n" + prompt;
    for (const auto& entry : {std::make_pair("developer", instructions.c_str()),
                              std::make_pair("user", transcript.c_str())}) {
        json_builder_begin_object(builder);
        json_builder_set_member_name(builder, "role");
        json_builder_add_string_value(builder, entry.first);
        json_builder_set_member_name(builder, "content");
        json_builder_add_string_value(builder, entry.second);
        json_builder_end_object(builder);
    }
    json_builder_end_array(builder);
    json_builder_end_object(builder);
    JsonNode* root = json_builder_get_root(builder);
    gchar* payload = json_to_string(root, FALSE);
    GBytes* bytes = g_bytes_new_take(payload, strlen(payload));
    SoupMessage* message = soup_message_new("POST", "https://api.openai.com/v1/chat/completions");
    soup_message_set_request_body_from_bytes(message, "application/json", bytes);
    g_bytes_unref(bytes);
    json_node_free(root);
    std::string body;
    if (!transcribe_send(message, api_key, "post-processing", &body, error)) return false;

    g_autoptr(JsonParser) parser = json_parser_new();
    g_autoptr(JsonReader) reader = json_reader_new(nullptr);
    bool ok = json_parser_load_from_data(parser, body.data(), body.size(), nullptr);
    if (ok) {
        transcribe_log_usage(parser, "post-processing");
        json_reader_set_root(reader, json_parser_get_root(parser));
        ok = json_reader_read_member(reader, "choices") && json_reader_read_element(reader, 0)
            && json_reader_read_member(reader, "finish_reason");
        const char* reason = ok ? json_reader_get_string_value(reader) : nullptr;
        ok = reason && std::string(reason) == "stop";
        if (ok) {
            json_reader_end_member(reader);
            ok = json_reader_read_member(reader, "message") && json_reader_read_member(reader, "content");
            const char* content = ok ? json_reader_get_string_value(reader) : nullptr;
            *text = content ? transcribe_trim_text(content) : "";
            ok = !text->empty();
        }
    }
    if (!ok && error) *error = "invalid, incomplete or empty text response";
    return ok;
}

// Transcribes audio first, then optionally processes the text with a custom prompt.
bool transcribe_with_openai(
    const std::vector<unsigned char>& audio_data,
    const std::string& api_key,
    const std::string& prompt,
    std::string* out_text,
    std::string* out_error
) {
    if (audio_data.empty()) {
        if (out_error) *out_error = "audio buffer is empty";
        return false;
    }
    g_print("sending compressed audio size: %zu bytes\n", audio_data.size());
    GBytes* audio_bytes = g_bytes_new(audio_data.data(), audio_data.size());
    SoupMultipart* multipart = soup_multipart_new("multipart/form-data");
    soup_multipart_append_form_string(multipart, "model", kTranscriptionModel);
    soup_multipart_append_form_string(multipart, "response_format", "json");
    soup_multipart_append_form_file(multipart, "file", "audio.ogg", "audio/ogg", audio_bytes);
    g_bytes_unref(audio_bytes);
    SoupMessage* message = soup_message_new_from_multipart("https://api.openai.com/v1/audio/transcriptions", multipart);
    soup_multipart_free(multipart);
    std::string body;
    if (!transcribe_send(message, api_key, "transcription", &body, out_error)) return false;
    std::string text;
    g_autoptr(JsonParser) parser = json_parser_new();
    if (json_parser_load_from_data(parser, body.data(), body.size(), nullptr)) {
        transcribe_log_usage(parser, "transcription");
        g_autoptr(JsonReader) reader = json_reader_new(json_parser_get_root(parser));
        if (json_reader_read_member(reader, "text")) {
            const char* content = json_reader_get_string_value(reader);
            if (content) text = transcribe_trim_text(content);
        }
    }
    if (text.empty()) {
        if (out_error) *out_error = "invalid or empty transcription response";
        return false;
    }
    g_print("transcript: %s\n", text.c_str());
    if (!prompt.empty()) {
        std::string processed;
        std::string error;
        if (!transcribe_apply_prompt(text, api_key, prompt, &processed, &error)) {
            if (out_error) *out_error = "post-processing failed: " + error;
            return false;
        }
        text = std::move(processed);
        g_print("post-processed text: %s\n", text.c_str());
    }
    if (out_text) *out_text = std::move(text);
    return true;
}
