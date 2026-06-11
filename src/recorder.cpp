#include "recorder.hpp"

#include "util.hpp"

#include <AudioToolbox/AudioToolbox.h>

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <format>

namespace supermoan {

namespace {

constexpr double kSampleRate = 16000;
constexpr UInt32 kBufferBytes = 6400; // 0.2 s of 16 kHz mono s16
constexpr int kBufferCount = 3;

#pragma pack(push, 1)
struct WavHeader {
    char riff[4] = {'R', 'I', 'F', 'F'};
    uint32_t riff_size = 0; // patched on stop: 36 + data_size
    char wave[4] = {'W', 'A', 'V', 'E'};
    char fmt[4] = {'f', 'm', 't', ' '};
    uint32_t fmt_size = 16;
    uint16_t format = 1; // PCM
    uint16_t channels = 1;
    uint32_t sample_rate = kSampleRate;
    uint32_t byte_rate = kSampleRate * 2;
    uint16_t block_align = 2;
    uint16_t bits = 16;
    char data[4] = {'d', 'a', 't', 'a'};
    uint32_t data_size = 0; // patched on stop
};
#pragma pack(pop)
static_assert(sizeof(WavHeader) == 44);

struct FileCloser {
    void operator()(FILE* f) const { std::fclose(f); }
};
using unique_file = std::unique_ptr<FILE, FileCloser>;

} // namespace

struct Recorder::Impl {
    AudioQueueRef queue = nullptr;
    unique_file file;
    uint64_t data_bytes = 0;
    int16_t peak = 0;

    ~Impl() {
        if (queue) {
            AudioQueueStop(queue, true);
            AudioQueueDispose(queue, true);
        }
    }

    static void callback(void* user, AudioQueueRef q, AudioQueueBufferRef buf,
                         const AudioTimeStamp*, UInt32, const AudioStreamPacketDescription*) {
        auto& self = *static_cast<Impl*>(user);
        UInt32 bytes = buf->mAudioDataByteSize;
        if (bytes > 0) {
            fwrite(buf->mAudioData, 1, bytes, self.file.get());
            self.data_bytes += bytes;

            const auto* samples = static_cast<const int16_t*>(buf->mAudioData);
            for (UInt32 i = 0; i < bytes / 2; i++) {
                int16_t v = samples[i] == INT16_MIN ? INT16_MAX : std::abs(samples[i]);
                self.peak = std::max(self.peak, v);
            }
        }
        AudioQueueEnqueueBuffer(q, buf, 0, nullptr); // no-op error once stopped
    }
};

Recorder::Recorder() = default;
Recorder::~Recorder() = default;

bool Recorder::start(const std::string& wav_path) {
    impl_ = std::make_unique<Impl>();
    impl_->file.reset(std::fopen(wav_path.c_str(), "wb"));
    if (!impl_->file) {
        log_line(std::format("recorder: cannot open {}", wav_path));
        return false;
    }
    WavHeader header;
    fwrite(&header, sizeof(header), 1, impl_->file.get());

    AudioStreamBasicDescription fmt{};
    fmt.mSampleRate = kSampleRate;
    fmt.mFormatID = kAudioFormatLinearPCM;
    fmt.mFormatFlags = kLinearPCMFormatFlagIsSignedInteger | kLinearPCMFormatFlagIsPacked;
    fmt.mBytesPerPacket = 2;
    fmt.mFramesPerPacket = 1;
    fmt.mBytesPerFrame = 2;
    fmt.mChannelsPerFrame = 1;
    fmt.mBitsPerChannel = 16;

    OSStatus err = AudioQueueNewInput(&fmt, Impl::callback, impl_.get(), nullptr, nullptr, 0,
                                      &impl_->queue);
    for (int i = 0; i < kBufferCount && err == noErr; i++) {
        AudioQueueBufferRef buf = nullptr;
        err = AudioQueueAllocateBuffer(impl_->queue, kBufferBytes, &buf);
        if (err == noErr) err = AudioQueueEnqueueBuffer(impl_->queue, buf, 0, nullptr);
    }
    if (err == noErr) err = AudioQueueStart(impl_->queue, nullptr);

    if (err != noErr) {
        log_line(std::format("recorder: AudioQueue failed (OSStatus {}); "
                             "microphone permission denied?", err));
        impl_.reset();
        return false;
    }
    return true;
}

Recorder::Stats Recorder::stop() {
    Stats stats;
    if (!impl_) return stats;

    AudioQueueStop(impl_->queue, true); // synchronous: drains pending buffers
    AudioQueueDispose(impl_->queue, true);
    impl_->queue = nullptr;

    WavHeader header;
    header.data_size = static_cast<uint32_t>(impl_->data_bytes);
    header.riff_size = 36 + header.data_size;
    std::fseek(impl_->file.get(), 0, SEEK_SET);
    fwrite(&header, sizeof(header), 1, impl_->file.get());

    stats.duration_s = static_cast<double>(impl_->data_bytes) / 2 / kSampleRate;
    if (impl_->peak > 0)
        stats.peak_db = 20 * std::log10(impl_->peak / 32768.0);

    impl_.reset();
    return stats;
}

} // namespace supermoan
