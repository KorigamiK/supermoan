// supermoan - dictation at cursor for macOS
//
// Toggle model: each launch connects to a unix socket. If a recording
// instance is listening, it is told to stop (and this launch exits);
// otherwise this launch becomes the recorder and waits for that signal.
//
// Launch via `open -gn Supermoan.app` so LaunchServices makes the bundle
// the TCC responsible process; run directly as a child of a hotkey daemon,
// the daemon's (absent) microphone usage declaration applies and TCC kills
// the process without a prompt.

#include "recorder.hpp"
#include "transcriber.hpp"
#include "typist.hpp"
#include "util.hpp"

#include <chrono>
#include <csignal>
#include <cstring>
#include <fstream>
#include <print>
#include <string_view>
#include <thread>

#include <sys/socket.h>
#include <sys/un.h>
#include <unistd.h>

namespace {

using namespace supermoan;
using namespace std::chrono_literals;

constexpr std::string_view kRecordingMsg = "(recording...)";
constexpr std::string_view kTranscribingMsg = "(transcribing...)";
constexpr std::string_view kNoSoundMsg = "(no sound detected)";
constexpr std::string_view kNoKeyMsg = "(no API key)";
constexpr std::string_view kMicErrorMsg = "(mic error)";
constexpr std::string_view kFailedMsg = "(transcription failed)";

class UniqueFd {
public:
    explicit UniqueFd(int fd = -1) : fd_(fd) {}
    ~UniqueFd() { if (fd_ >= 0) close(fd_); }
    UniqueFd(const UniqueFd&) = delete;
    UniqueFd& operator=(const UniqueFd&) = delete;

    int get() const { return fd_; }
    explicit operator bool() const { return fd_ >= 0; }

private:
    int fd_;
};

void cleanup_on_signal(int) {
    // async-signal-safe only; the wav is left for post-mortem
    unlink(kSocketPath);
    _exit(130);
}

sockaddr_un socket_addr() {
    sockaddr_un addr{};
    addr.sun_family = AF_UNIX;
    std::strncpy(addr.sun_path, kSocketPath, sizeof(addr.sun_path) - 1);
    return addr;
}

// Briefly show a status message at the cursor, then erase it.
void flash_message(std::string_view msg) {
    typist::type(msg);
    std::this_thread::sleep_for(700ms);
    typist::backspace(msg.size());
}

// Returns true if a recording instance was signalled to stop.
bool signal_running_recorder() {
    UniqueFd fd{socket(AF_UNIX, SOCK_STREAM, 0)};
    if (!fd) return false;
    auto addr = socket_addr();
    return connect(fd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) == 0
           && write(fd.get(), "s", 1) == 1;
}

void wait_for_stop_signal(const UniqueFd& server) {
    UniqueFd client{accept(server.get(), nullptr, nullptr)};
    if (client) {
        char c;
        std::ignore = read(client.get(), &c, 1);
    }
}

int run_recorder() {
    trim_log();
    const Config cfg = load_config();

    unlink(kSocketPath); // stale leftover, nobody answered on it
    UniqueFd server{socket(AF_UNIX, SOCK_STREAM, 0)};
    auto addr = socket_addr();
    if (!server || bind(server.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0
        || listen(server.get(), 1) < 0) {
        log_line("recorder: cannot bind toggle socket");
        return 1;
    }
    std::signal(SIGINT, cleanup_on_signal);
    std::signal(SIGTERM, cleanup_on_signal);

    if (typist::enabled() && !typist::trusted(/*prompt=*/true)) {
        log_line("Accessibility not granted; enable Supermoan in System Settings > "
                 "Privacy & Security > Accessibility, then retry");
        unlink(kSocketPath);
        return 1;
    }

    const std::string api_key = groq_api_key();
    if (api_key.empty()) {
        log_line("GROQ_API_KEY not set (environment or ~/.env)");
        flash_message(kNoKeyMsg);
        unlink(kSocketPath);
        return 1;
    }

    std::this_thread::sleep_for(200ms); // let the hotkey's own keystrokes settle
    typist::type(kRecordingMsg);

    Recorder rec;
    if (!rec.start(kWavFile)) {
        typist::backspace(kRecordingMsg.size());
        flash_message(kMicErrorMsg);
        unlink(kSocketPath);
        return 1;
    }

    wait_for_stop_signal(server);
    const Recorder::Stats stats = rec.stop();
    unlink(kSocketPath);

    typist::backspace(kRecordingMsg.size());

    if (stats.peak_db < cfg.silence_threshold) {
        flash_message(kNoSoundMsg);
        unlink(kWavFile);
        return 0;
    }

    typist::type(kTranscribingMsg);
    const std::string model = stats.duration_s > cfg.long_recording_threshold
                                  ? "whisper-large-v3"
                                  : "whisper-large-v3-turbo";
    const auto start = std::chrono::steady_clock::now();
    const auto text = transcribe(kWavFile, model, cfg.transcription_prompt, api_key);
    const double seconds = std::chrono::duration<double>(
                               std::chrono::steady_clock::now() - start).count();
    typist::backspace(kTranscribingMsg.size());

    if (text) {
        typist::type(*text);
        log_result("Transcription", *text, seconds);
    } else {
        log_result("Transcription FAILED", text.error(), seconds);
        flash_message(kFailedMsg);
    }
    unlink(kWavFile);
    return text ? 0 : 1;
}

int show_log() {
    std::ifstream in{kLogFile};
    if (!in) {
        std::println(stderr, "No log file found at {}", kLogFile);
        return 1;
    }
    std::print("{}", std::string(std::istreambuf_iterator<char>(in), {}));
    return 0;
}

} // namespace

int main(int argc, char* argv[]) {
    for (int i = 1; i < argc; i++) {
        const std::string_view arg = argv[i];
        if (arg == "--log") return show_log();
        if (arg == "--no-type") {
            typist::set_enabled(false);
        } else {
            std::println(stderr, "Usage: supermoan [--log] [--no-type]");
            std::println(stderr, "Run with no arguments to toggle recording/transcription.");
            return 1;
        }
    }

    if (signal_running_recorder()) return 0;
    return run_recorder();
}
