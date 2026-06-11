#pragma once

#include <expected>
#include <string>

namespace supermoan {

// Uploads the WAV to Groq's Whisper endpoint and returns the transcription
// (leading space already stripped), or an error description.
std::expected<std::string, std::string> transcribe(const std::string& wav_path,
                                                   const std::string& model,
                                                   const std::string& prompt,
                                                   const std::string& api_key);

} // namespace supermoan
