#include "typist.hh"

#include "util.hh"

#include <ApplicationServices/ApplicationServices.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <format>
#include <memory>
#include <optional>
#include <thread>
#include <vector>

namespace supermoan::typist {

namespace {

using namespace std::chrono_literals;

constexpr CGKeyCode kVKDelete = 51;
constexpr CGKeyCode kVKReturn = 36;
constexpr CGKeyCode kVKTab = 48;
constexpr CGKeyCode kVKSpace = 49;
constexpr auto kKeyDelay = 8ms;
// Max UTF-16 units CGEventKeyboardSetUnicodeString accepts per event
constexpr CFIndex kChunk = 20;

bool g_enabled = true;

struct CFDeleter {
    void operator()(CFTypeRef ref) const { CFRelease(ref); }
};
template <typename T>
using cf_ptr = std::unique_ptr<std::remove_pointer_t<T>, CFDeleter>;

struct KeyStroke {
    CGKeyCode code;
    CGEventFlags flags = 0;
};

struct AsciiKey {
    char normal;
    char shifted;
    CGKeyCode code;
};

void post_key_event(const cf_ptr<CGEventRef>& event, CGEventFlags flags = 0) {
    // Without explicit flags the event inherits the keyboard state captured
    // at creation -- physically held modifiers (the hotkey's cmd-shift) would
    // turn our keystrokes into app shortcuts.
    CGEventSetFlags(event.get(), flags);
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

constexpr auto make_ascii_keys() {
    constexpr std::array keys{
        AsciiKey{'a', 'A', 0},  AsciiKey{'s', 'S', 1},  AsciiKey{'d', 'D', 2},
        AsciiKey{'f', 'F', 3},  AsciiKey{'h', 'H', 4},  AsciiKey{'g', 'G', 5},
        AsciiKey{'z', 'Z', 6},  AsciiKey{'x', 'X', 7},  AsciiKey{'c', 'C', 8},
        AsciiKey{'v', 'V', 9},  AsciiKey{'b', 'B', 11}, AsciiKey{'q', 'Q', 12},
        AsciiKey{'w', 'W', 13}, AsciiKey{'e', 'E', 14}, AsciiKey{'r', 'R', 15},
        AsciiKey{'y', 'Y', 16}, AsciiKey{'t', 'T', 17}, AsciiKey{'1', '!', 18},
        AsciiKey{'2', '@', 19}, AsciiKey{'3', '#', 20}, AsciiKey{'4', '$', 21},
        AsciiKey{'6', '^', 22}, AsciiKey{'5', '%', 23}, AsciiKey{'=', '+', 24},
        AsciiKey{'9', '(', 25}, AsciiKey{'7', '&', 26}, AsciiKey{'-', '_', 27},
        AsciiKey{'8', '*', 28}, AsciiKey{'0', ')', 29}, AsciiKey{']', '}', 30},
        AsciiKey{'o', 'O', 31}, AsciiKey{'u', 'U', 32}, AsciiKey{'[', '{', 33},
        AsciiKey{'i', 'I', 34}, AsciiKey{'p', 'P', 35}, AsciiKey{'l', 'L', 37},
        AsciiKey{'j', 'J', 38}, AsciiKey{'\'', '"', 39}, AsciiKey{'k', 'K', 40},
        AsciiKey{';', ':', 41}, AsciiKey{'\\', '|', 42}, AsciiKey{',', '<', 43},
        AsciiKey{'/', '?', 44}, AsciiKey{'n', 'N', 45}, AsciiKey{'m', 'M', 46},
        AsciiKey{'.', '>', 47}, AsciiKey{' ', '\0', kVKSpace},
        AsciiKey{'\t', '\0', kVKTab}, AsciiKey{'\n', '\0', kVKReturn},
    };

    std::array<std::optional<KeyStroke>, 128> table{};
    constexpr CGEventFlags shift = kCGEventFlagMaskShift;
    for (const AsciiKey& key : keys) {
        table[static_cast<unsigned char>(key.normal)] = KeyStroke{key.code};
        if (key.shifted != '\0')
            table[static_cast<unsigned char>(key.shifted)] = KeyStroke{key.code, shift};
    }
    return table;
}

std::optional<KeyStroke> ascii_keystroke(char c) {
    static constexpr auto table = make_ascii_keys();
    const auto index = static_cast<unsigned char>(c);
    return index < table.size() ? table[index] : std::nullopt;
}

bool type_ascii(std::string_view text) {
    std::vector<KeyStroke> keys;
    keys.reserve(text.size());
    for (char c : text) {
        if (auto key = ascii_keystroke(c))
            keys.push_back(*key);
        else
            return false;
    }

    for (const KeyStroke& key : keys) {
        for (bool down : {true, false}) {
            cf_ptr<CGEventRef> event{CGEventCreateKeyboardEvent(nullptr, key.code, down)};
            post_key_event(event, key.flags);
        }
    }
    return true;
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

    // RDP clients can ignore CGEventKeyboardSetUnicodeString. If that happens,
    // a Unicode event with keycode 0 is delivered as the physical A key.
    if (type_ascii(utf8)) return;

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
