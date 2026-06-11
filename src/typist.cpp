#include "typist.hpp"

#include "util.hpp"

#include <ApplicationServices/ApplicationServices.h>

#include <algorithm>
#include <chrono>
#include <format>
#include <memory>
#include <thread>
#include <vector>

namespace supermoan::typist {

namespace {

using namespace std::chrono_literals;

constexpr CGKeyCode kVKDelete = 51;
constexpr auto kKeyDelay = 8ms;
// Max UTF-16 units CGEventKeyboardSetUnicodeString accepts per event
constexpr CFIndex kChunk = 20;

bool g_enabled = true;

struct CFDeleter {
    void operator()(CFTypeRef ref) const { CFRelease(ref); }
};
template <typename T>
using cf_ptr = std::unique_ptr<std::remove_pointer_t<T>, CFDeleter>;

void post_key_event(const cf_ptr<CGEventRef>& event) {
    // Without explicit flags the event inherits the keyboard state captured
    // at creation -- physically held modifiers (the hotkey's cmd-shift) would
    // turn our keystrokes into app shortcuts.
    CGEventSetFlags(event.get(), static_cast<CGEventFlags>(0));
    CGEventPost(kCGSessionEventTap, event.get());
    std::this_thread::sleep_for(kKeyDelay);
}

// Some apps consult the hardware modifier state directly, so zeroed event
// flags aren't enough: hold off until the hotkey chord is fully released.
void wait_for_modifier_release() {
    constexpr CGEventFlags kModifiers = kCGEventFlagMaskCommand | kCGEventFlagMaskShift |
                                        kCGEventFlagMaskAlternate | kCGEventFlagMaskControl;
    constexpr auto kTimeout = 3s;
    const auto deadline = std::chrono::steady_clock::now() + kTimeout;
    while (CGEventSourceFlagsState(kCGEventSourceStateCombinedSessionState) & kModifiers) {
        if (std::chrono::steady_clock::now() > deadline) {
            log_line("typist: modifiers still held after 3s, typing anyway");
            return;
        }
        std::this_thread::sleep_for(10ms);
    }
}

} // namespace

bool trusted(bool prompt) {
    if (!prompt) return AXIsProcessTrusted();
    const void* keys[] = {kAXTrustedCheckOptionPrompt};
    const void* values[] = {kCFBooleanTrue};
    cf_ptr<CFDictionaryRef> opts{CFDictionaryCreate(nullptr, keys, values, 1,
                                                    &kCFTypeDictionaryKeyCallBacks,
                                                    &kCFTypeDictionaryValueCallBacks)};
    return AXIsProcessTrustedWithOptions(opts.get());
}

void set_enabled(bool enabled) {
    g_enabled = enabled;
}

bool enabled() {
    return g_enabled;
}

void type(std::string_view utf8) {
    if (!g_enabled) {
        log_line(std::format("typist (disabled): type [{}]", utf8));
        return;
    }

    wait_for_modifier_release();

    cf_ptr<CFStringRef> str{CFStringCreateWithBytes(
        nullptr, reinterpret_cast<const UInt8*>(utf8.data()),
        static_cast<CFIndex>(utf8.size()), kCFStringEncodingUTF8, false)};
    if (!str) {
        log_line("typist: invalid UTF-8, nothing typed");
        return;
    }

    const CFIndex len = CFStringGetLength(str.get());
    std::vector<UniChar> buf(kChunk);

    for (CFIndex off = 0; off < len;) {
        CFIndex chunk = std::min(kChunk, len - off);

        // Don't split a surrogate pair across events
        if (off + chunk < len) {
            UniChar last = CFStringGetCharacterAtIndex(str.get(), off + chunk - 1);
            if (last >= 0xD800 && last <= 0xDBFF) chunk--;
        }

        CFStringGetCharacters(str.get(), CFRangeMake(off, chunk), buf.data());

        for (bool down : {true, false}) {
            cf_ptr<CGEventRef> event{CGEventCreateKeyboardEvent(nullptr, 0, down)};
            CGEventKeyboardSetUnicodeString(event.get(), chunk, buf.data());
            post_key_event(event);
        }

        off += chunk;
    }
}

void backspace(std::size_t count) {
    if (!g_enabled) {
        log_line(std::format("typist (disabled): backspace {}", count));
        return;
    }
    wait_for_modifier_release();
    for (std::size_t i = 0; i < count; i++) {
        for (bool down : {true, false}) {
            cf_ptr<CGEventRef> event{CGEventCreateKeyboardEvent(nullptr, kVKDelete, down)};
            post_key_event(event);
        }
    }
}

} // namespace supermoan::typist
