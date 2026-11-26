#pragma once

#include "runtime.hpp"

#include <cstdint>
#include <functional>
#include <optional>
#include <string>

namespace kompoz {

// A window shown as a tray icon's popup (Window's popup, as the panels' own applets are): no title
// bar, above the other windows, placed next to `anchor` (screen coordinates as the platform has
// them: Tray's onAction gives them; none: where the platform puts it), its onCloseRequest called as
// it loses the keyboard's focus (a click elsewhere) or Escape goes unused in it. Wayland: a layer
// surface (wlr-layer-shell; a plain window where the compositor has none); X11, Windows: an
// undecorated window kept on top, out of the taskbar. Elsewhere a plain window.
struct WindowPopup {
    std::optional<IntOffset> anchor;
    bool operator==(const WindowPopup&) const = default;
};

struct Application {
    std::string title = "Kompoz";
    int width = 480, height = 800;  // initial window size in dp (desktop only)
    Content content;
    std::string fontPath, boldFontPath, mediumFontPath;  // optional; otherwise bundled/system fonts are used
    // Images AsyncImage downloaded are kept in the app's cache directory up to this size (the
    // least recently used go first), for the next time and the next run; 0: not kept. Not on
    // the web, where the browser caches.
    uint64_t imageDiskCacheBytes = uint64_t(64) << 20;
    // The user closing the app's window (its title bar's ×, Alt+F4): none, the app ends; else this,
    // which may hide the window instead (setAppWindowVisible(false), a Tray to show it again) or end
    // it (exitApplication). A signal (Ctrl+C, SIGTERM) ends the app either way. Desktops only.
    std::function<void()> onCloseRequest;
    // The window opened as a popup (WindowPopup): Window's popup, for which the app core sets it.
    std::optional<WindowPopup> popup;
};

// Implemented by the application. The platform backend owns the real entry point
// (main on the desktops and the web, the activity's native methods on Android) and calls this once.
Application createApplication(int argc, char** argv);

} // namespace kompoz
