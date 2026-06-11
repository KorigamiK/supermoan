#pragma once

#include <cstddef>
#include <string_view>

namespace supermoan::typist {

// CGEventPost requires Accessibility for the TCC responsible process.
// `prompt` shows the system dialog directing to System Settings.
bool trusted(bool prompt);

// Skip event posting and log instead (--no-type, for headless testing)
void set_enabled(bool enabled);
bool enabled();

// Types arbitrary UTF-8 independent of keyboard layout
// (CGEventKeyboardSetUnicodeString).
void type(std::string_view utf8);
void backspace(std::size_t count);

} // namespace supermoan::typist
