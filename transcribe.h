#pragma once

#include <string>
#include <vector>

// Transcribes encoded audio, optionally applies a prompt to the text, or returns an error.
bool transcribe_with_openai(
    const std::vector<unsigned char>& audio_data,
    const std::string& api_key,
    const std::string& prompt,
    std::string* out_text,
    std::string* out_error
);
