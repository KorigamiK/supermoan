#include "util.hpp"

#include <charconv>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <memory>
#include <print>

namespace supermoan {

namespace {

struct FileCloser {
    void operator()(FILE* f) const { std::fclose(f); }
};
using unique_file = std::unique_ptr<FILE, FileCloser>;

std::string trim(const std::string& s) {
    auto b = s.find_first_not_of(" \t");
    auto e = s.find_last_not_of(" \t");
    return b == std::string::npos ? "" : s.substr(b, e - b + 1);
}

std::string unquote(std::string v) {
    if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
        return v.substr(1, v.size() - 2);
    return v;
}

// std::stod throws; exceptions are disabled project-wide
void parse_double(const std::string& value, double& out, const std::string& key) {
    double parsed{};
    auto [ptr, ec] = std::from_chars(value.data(), value.data() + value.size(), parsed);
    if (ec == std::errc{} && ptr == value.data() + value.size())
        out = parsed;
    else
        log_line(std::format("config: ignoring invalid value for {}: [{}]", key, value));
}

} // namespace

Config load_config() {
    Config cfg;
    const char* home = std::getenv("HOME");
    if (!home) return cfg;

    std::string dir = std::getenv("XDG_CONFIG_HOME") ? std::getenv("XDG_CONFIG_HOME")
                                                     : std::string(home) + "/.config";
    std::ifstream in(dir + "/supermoan/config");
    std::string line;
    while (std::getline(in, line)) {
        auto hash = line.find('#');
        if (hash != std::string::npos) line.erase(hash);
        auto colon = line.find(':');
        if (colon == std::string::npos) continue;

        std::string key = trim(line.substr(0, colon));
        std::string value = unquote(trim(line.substr(colon + 1)));
        if (key.empty()) continue;

        if (key == "long-recording-threshold") parse_double(value, cfg.long_recording_threshold, key);
        else if (key == "transcription-prompt") cfg.transcription_prompt = value;
        else if (key == "silence-threshold") parse_double(value, cfg.silence_threshold, key);
    }
    return cfg;
}

std::string groq_api_key() {
    if (const char* env = std::getenv("GROQ_API_KEY")) return env;

    const char* home = std::getenv("HOME");
    if (!home) return "";

    std::ifstream in(std::string(home) + "/.env");
    std::string line;
    while (std::getline(in, line)) {
        std::string s = trim(line);
        if (s.starts_with("export ")) s = trim(s.substr(7));
        if (s.starts_with("GROQ_API_KEY="))
            return unquote(trim(s.substr(13)));
    }
    return "";
}

// Apple's libc++ ships std::println for FILE* but not yet for ostreams,
// hence stdio here instead of std::ofstream.
void log_line(const std::string& msg) {
    if (unique_file out{std::fopen(kLogFile, "a")})
        std::println(out.get(), "{}", msg);
}

void log_result(const std::string& title, const std::string& result, double seconds) {
    if (unique_file out{std::fopen(kLogFile, "a")})
        std::println(out.get(), "=== {} ===\nResult: [{}]\nTime: {:.3f}s",
                     title, result, seconds);
}

void trim_log() {
    constexpr std::uintmax_t kMaxLogBytes = 512 * 1024;
    std::error_code ec;
    if (std::filesystem::file_size(kLogFile, ec) > kMaxLogBytes && !ec)
        std::filesystem::remove(kLogFile, ec);
}

} // namespace supermoan
