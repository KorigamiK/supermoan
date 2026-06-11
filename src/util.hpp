#pragma once

#include <string>

namespace supermoan {

struct Config {
    double long_recording_threshold = 1000; // seconds; above -> large model
    std::string transcription_prompt;       // context words for Whisper
    double silence_threshold = -50;         // dB; peak below -> no sound
};

// ~/.config/supermoan/config, "key : value" lines, '#' comments
Config load_config();

// GROQ_API_KEY from the environment, falling back to ~/.env
std::string groq_api_key();

void log_line(const std::string& msg);
void log_result(const std::string& title, const std::string& result, double seconds);
// Drop the log once it outgrows a sane size; called on startup
void trim_log();

inline constexpr const char* kLogFile = "/tmp/supermoan.log";
inline constexpr const char* kWavFile = "/tmp/supermoan.wav";
inline constexpr const char* kSocketPath = "/tmp/supermoan.sock";

} // namespace supermoan
