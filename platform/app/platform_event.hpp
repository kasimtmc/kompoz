// What a platform tells a Kompoz app: its window, input and lifecycle events, in Kompoz's own
// terms. Each platform turns its own events into these; the app core handles only these.
#pragma once

#include <kompoz/layout.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kompoz::platform {

enum class EventType {
    Quit,
    // The window.
    FocusGained, FocusLost, Shown, Hidden, Minimized, Maximized, Restored,
    Exposed,         // what it shows is gone: shown again
    Resized,         // its size (in pixels, or points) changed
    MetricsChanged,  // its scale, display, safe area, the system's theme or language changed
    DevicesChanged,  // a mouse or keyboard came or went (what mediaQuery tells)
    SurfaceLost,     // the pixels kept for it are gone
    // The app's lifecycle (phones).
    WillEnterBackground, DidEnterBackground, WillEnterForeground, DidEnterForeground, LowMemory,
    // Input. Positions in the window's points, times on the platform's clock (ns).
    PointerLeft,                  // the mouse left the window
    MouseMove, MouseDown, MouseUp,
    Wheel,                        // dx, dy: notches (as the user turns them: flipped back if the system flips)
    PinchBegin, Pinch,            // a touchpad's: `scale` and `rotation` since the last, around (x, y)
    TouchDown, TouchMove, TouchUp,  // a finger on the screen (`pointer` its id)
    KeyDown, KeyUp,
    Text,                         // text typed (`text`)
    Back,                         // the system's back (Android's button)
    // A drag from another app: begun (`mimeTypes`: what it may carry, where told), moved over the
    // window, what it carries (`clip`: its text, HTML, image, files' URIs), dropped (alone: it left).
    DropBegin, DropMove, DropData, DropComplete,
    // Things learned on other threads: the desktop's accent color, the wallpaper's colors, an
    // assistive technology's requests.
    AccentChanged, WallpaperChanged, AccessibilityRequest,
    Wake,  // asked for by KompozHost's wake (work for the next frame)
};

enum class MouseButton { Left, Right, Middle, Back, Forward };

struct Event {
    EventType type = EventType::Quit;
    int64_t time = -1;  // ns, on the platform's clock; -1: now
    float x = 0, y = 0;
    float dx = 0, dy = 0;  // Wheel
    float scale = 1;       // Pinch
    float rotation = 0;    // Pinch: degrees clockwise
    int64_t pointer = 0;   // a finger's
    MouseButton button = MouseButton::Left;
    Key key = Key::Unknown;
    KeyModifiers modifiers;  // held down (pointer and key events)
    int nativeKeyCode = 0;   // KeyDown, KeyUp: the platform's code of the key (KeyEvent's)
    std::string text;        // Text: typed, whether a text field takes it or not (KeyEventType::Unknown then); Quit: "signal" when a signal asked
    std::vector<std::string> mimeTypes;      // DropBegin
    std::shared_ptr<const ClipEntry> clip;  // DropData
};

} // namespace kompoz::platform
