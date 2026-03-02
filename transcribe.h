#pragma once

#include <string>
#include <vector>

bool transcribe_with_openai(
    const std::vector<unsigned char>& audio_data,
    const std::string& api_key,
    const std::string& prompt,
    std::string* out_text,
    std::string* out_error
);
