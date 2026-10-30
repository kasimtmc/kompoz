#pragma once

// The desktop's accent color on Linux (and other free desktops): the XDG settings portal's
// org.freedesktop.appearance accent-color, which GNOME and KDE fill in. It is read, and followed
// as it changes, on a thread of its own over D-Bus (libdbus, loaded when it is there, as SDL
// does); it notifies (Notifier) as each value comes (linux/desktop_accent.cpp). On Windows, the
// one in Settings > Personalization > Colors, from the registry (windows/desktop_accent.cpp). On
// Linux the light/dark preference too, the portal's color-scheme (for Kompoz's own platforms: SDL
// follows it itself, SDL_EVENT_SYSTEM_THEME_CHANGED).

#include "system.hpp"

#include <kompoz/graphics.hpp>

#include <memory>
#include <optional>

namespace kompoz::platform {

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
    // Whether the desktop prefers dark (the portal's color-scheme, read even with
    // KOMPOZ_NO_DESKTOP_ACCENT); none: it doesn't say (nor on Windows, where the platform asks).
    std::optional<bool> darkScheme() const;

    struct State;

private:
    std::shared_ptr<State> state_;
};

} // namespace kompoz::platform
