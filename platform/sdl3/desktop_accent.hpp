#pragma once

// The desktop's accent color on Linux (and other free desktops): the XDG settings portal's
// org.freedesktop.appearance accent-color, which GNOME and KDE fill in. It is read, and followed
// as it changes, on a thread of its own over D-Bus (libdbus, loaded when it is there, as SDL
// does); it notifies (Notifier) as each value comes. On Windows, the one in Settings >
// Personalization > Colors, from the registry (desktop_accent_windows.cpp). The light/dark preference needs none
// of this: SDL follows the portal's color-scheme itself (SDL_EVENT_SYSTEM_THEME_CHANGED).

#include "../common/system.hpp"

#include <kompoz/graphics.hpp>

#include <memory>
#include <optional>

namespace kompoz::sdl {

class DesktopAccent {
public:
    explicit DesktopAccent(platform::Notifier notifier);  // starts listening (nothing happens where there is no session bus or portal)
    ~DesktopAccent();
    DesktopAccent(const DesktopAccent&) = delete;
    DesktopAccent& operator=(const DesktopAccent&) = delete;

    // Whether it was started: it notifies when the accent is learned or changes.
    bool listening() const;
    // The latest accent; none: the desktop has none (or doesn't say).
    std::optional<Color> accent() const;

    struct State;

private:
    std::shared_ptr<State> state_;
};

} // namespace kompoz::sdl
