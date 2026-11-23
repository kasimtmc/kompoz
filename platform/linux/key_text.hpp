#pragma once

// What a key press types where no input method is asked to (no text field takes typing: KeyEventType::
// Unknown's text), on X11 and Wayland: its keysym (the layout's, at the level Shift and AltGr pick),
// through the desktop's compose table, as libxkbcommon reads it for the locale (dead keys: ´ then e
// is é; the Compose key's sequences). libxkbcommon is loaded at run time; without it, a keysym types
// what xkbcommon would make of it and nothing composes.

#include <cstdint>
#include <memory>
#include <string>

namespace kompoz::platform {

class KeyText {
public:
    KeyText();
    ~KeyText();
    // A pressed key's keysym: the text it types (UTF-8), empty while a sequence is under way or for a
    // key that types nothing (a control character, a modifier). `fallback`: its text where
    // libxkbcommon isn't there.
    std::string typed(uint32_t keysym, std::string_view fallback = {});
    void reset();  // a sequence under way given up (the window lost focus)

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace kompoz::platform
