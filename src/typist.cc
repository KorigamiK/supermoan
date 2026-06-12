#include "typist.hh"

#include "util.hh"

#include <ApplicationServices/ApplicationServices.h>

#include <algorithm>
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

std::optional<KeyStroke> ascii_keystroke(char c) {
    constexpr CGEventFlags shift = kCGEventFlagMaskShift;
    switch (c) {
    case 'a': return KeyStroke{0};
    case 's': return KeyStroke{1};
    case 'd': return KeyStroke{2};
    case 'f': return KeyStroke{3};
    case 'h': return KeyStroke{4};
    case 'g': return KeyStroke{5};
    case 'z': return KeyStroke{6};
    case 'x': return KeyStroke{7};
    case 'c': return KeyStroke{8};
    case 'v': return KeyStroke{9};
    case 'b': return KeyStroke{11};
    case 'q': return KeyStroke{12};
    case 'w': return KeyStroke{13};
    case 'e': return KeyStroke{14};
    case 'r': return KeyStroke{15};
    case 'y': return KeyStroke{16};
    case 't': return KeyStroke{17};
    case '1': return KeyStroke{18};
    case '2': return KeyStroke{19};
    case '3': return KeyStroke{20};
    case '4': return KeyStroke{21};
    case '6': return KeyStroke{22};
    case '5': return KeyStroke{23};
    case '=': return KeyStroke{24};
    case '9': return KeyStroke{25};
    case '7': return KeyStroke{26};
    case '-': return KeyStroke{27};
    case '8': return KeyStroke{28};
    case '0': return KeyStroke{29};
    case ']': return KeyStroke{30};
    case 'o': return KeyStroke{31};
    case 'u': return KeyStroke{32};
    case '[': return KeyStroke{33};
    case 'i': return KeyStroke{34};
    case 'p': return KeyStroke{35};
    case 'l': return KeyStroke{37};
    case 'j': return KeyStroke{38};
    case '\'': return KeyStroke{39};
    case 'k': return KeyStroke{40};
    case ';': return KeyStroke{41};
    case '\\': return KeyStroke{42};
    case ',': return KeyStroke{43};
    case '/': return KeyStroke{44};
    case 'n': return KeyStroke{45};
    case 'm': return KeyStroke{46};
    case '.': return KeyStroke{47};
    case ' ': return KeyStroke{kVKSpace};
    case '\t': return KeyStroke{kVKTab};
    case '\n': return KeyStroke{kVKReturn};

    case 'A': return KeyStroke{0, shift};
    case 'S': return KeyStroke{1, shift};
    case 'D': return KeyStroke{2, shift};
    case 'F': return KeyStroke{3, shift};
    case 'H': return KeyStroke{4, shift};
    case 'G': return KeyStroke{5, shift};
    case 'Z': return KeyStroke{6, shift};
    case 'X': return KeyStroke{7, shift};
    case 'C': return KeyStroke{8, shift};
    case 'V': return KeyStroke{9, shift};
    case 'B': return KeyStroke{11, shift};
    case 'Q': return KeyStroke{12, shift};
    case 'W': return KeyStroke{13, shift};
    case 'E': return KeyStroke{14, shift};
    case 'R': return KeyStroke{15, shift};
    case 'Y': return KeyStroke{16, shift};
    case 'T': return KeyStroke{17, shift};
    case '!': return KeyStroke{18, shift};
    case '@': return KeyStroke{19, shift};
    case '#': return KeyStroke{20, shift};
    case '$': return KeyStroke{21, shift};
    case '^': return KeyStroke{22, shift};
    case '%': return KeyStroke{23, shift};
    case '+': return KeyStroke{24, shift};
    case '(': return KeyStroke{25, shift};
    case '&': return KeyStroke{26, shift};
    case '_': return KeyStroke{27, shift};
    case '*': return KeyStroke{28, shift};
    case ')': return KeyStroke{29, shift};
    case '}': return KeyStroke{30, shift};
    case 'O': return KeyStroke{31, shift};
    case 'U': return KeyStroke{32, shift};
    case '{': return KeyStroke{33, shift};
    case 'I': return KeyStroke{34, shift};
    case 'P': return KeyStroke{35, shift};
    case 'L': return KeyStroke{37, shift};
    case 'J': return KeyStroke{38, shift};
    case '"': return KeyStroke{39, shift};
    case 'K': return KeyStroke{40, shift};
    case ':': return KeyStroke{41, shift};
    case '|': return KeyStroke{42, shift};
    case '<': return KeyStroke{43, shift};
    case '?': return KeyStroke{44, shift};
    case 'N': return KeyStroke{45, shift};
    case 'M': return KeyStroke{46, shift};
    case '>': return KeyStroke{47, shift};
    default: return std::nullopt;
    }
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
