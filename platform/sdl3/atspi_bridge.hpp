#pragma once

// Screen readers (Orca) and other assistive technologies on Linux read the app through AT-SPI: its
// semantics tree (ComposeHost::semanticsTree), served on the accessibility bus over D-Bus (libdbus,
// loaded at run time) on a thread of its own, the app registered with the AT-SPI registry. It
// starts when the desktop's accessibility is on (org.a11y.Status IsEnabled or ScreenReaderEnabled,
// also once it is turned on later); KOMPOZ_ATSPI=1 starts it anyway, KOMPOZ_ATSPI=0 never. What an assistive technology asks the
// app to do (click, focus, set a slider) comes to the main loop as an SDL event; what changes in the
// tree (focus, states, names, children, live regions) is told to it as AT-SPI events.

#include <kompoz/layout.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kompoz::sdl {

class AtspiBridge {
public:
    explicit AtspiBridge(std::string appName);
    ~AtspiBridge();
    AtspiBridge(const AtspiBridge&) = delete;
    AtspiBridge& operator=(const AtspiBridge&) = delete;

    // Connected to the accessibility bus: the tree is worth publishing.
    bool active() const;
    // Whether the bridge wants the tree now (it just connected), even if nothing changed.
    bool takeTreeRequest();

    // The window the tree is in: where it is on the screen and its size, in the platform's points
    // (the units AT-SPI's extents are in), and dp to points.
    struct Window {
        float x = 0, y = 0, width = 0, height = 0;
        float dpToPoints = 1;
        bool focused = true;
        std::string title;
    };
    // The tree as it is now (UI thread). What changed since the last one is told to listeners.
    void publish(std::vector<SemanticsNode> nodes, const Window& window);

    // What assistive technologies asked for; each one also comes as an SDL event of eventType().
    struct Request {
        int id = 0;
        SemanticsAction action = SemanticsAction::Click;
        float argument = 0;
        std::string text;  // SetText's
    };
    uint32_t eventType() const;  // 0: none will come
    std::vector<Request> takeRequests();

    struct State;

private:
    std::shared_ptr<State> state_;
};

} // namespace kompoz::sdl
