#pragma once

// The desktop's accent color on Linux (and other free desktops): the XDG settings portal's
// org.freedesktop.appearance accent-color, which GNOME and KDE fill in. It is read, and followed
// as it changes, on a thread of its own over D-Bus (libdbus, loaded when it is there, as SDL
// does); each value comes to the main loop as an SDL event. On Windows, the one in Settings >
// Personalization > Colors, from the registry (desktop_accent_windows.cpp). The light/dark preference needs none
// of this: SDL follows the portal's color-scheme itself (SDL_EVENT_SYSTEM_THEME_CHANGED).

#include <kompoz/graphics.hpp>

#include <cstdint>
#include <memory>
#include <optional>

namespace kompoz::sdl {

class DesktopAccent {
public:
    DesktopAccent();  // starts listening (nothing happens where there is no session bus or portal)
    ~DesktopAccent();
    DesktopAccent(const DesktopAccent&) = delete;
    DesktopAccent& operator=(const DesktopAccent&) = delete;

    // The SDL event type sent when the accent is learned or changes (0: none will come).
    uint32_t eventType() const;
    // The latest accent; none: the desktop has none (or doesn't say).
    std::optional<Color> accent() const;

    struct State;

private:
    std::shared_ptr<State> state_;
};

} // namespace kompoz::sdl
