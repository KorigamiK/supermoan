#pragma once

#include <memory>
#include <string>

namespace supermoan {

// Captures the system-default input device to a 16 kHz mono s16 WAV file
// via AudioQueue, tracking the peak sample for silence detection.
class Recorder {
public:
    struct Stats {
        double duration_s = 0;
        double peak_db = -120; // dBFS; -120 stands in for digital silence
    };

    Recorder();
    ~Recorder(); // stops and finalizes the WAV if still recording

    Recorder(const Recorder&) = delete;
    Recorder& operator=(const Recorder&) = delete;

    // Returns false (with a log entry) if the queue can't start --
    // e.g. microphone permission denied.
    [[nodiscard]] bool start(const std::string& wav_path);
    Stats stop();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace supermoan
