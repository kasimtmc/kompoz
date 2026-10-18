#pragma once

// Screen readers (Narrator, NVDA) and other UI Automation clients on Windows read the app through
// providers of its semantics tree (ComposeHost::semanticsTree): the window answers WM_GETOBJECT
// with the root, its nodes are the fragments under it. UI Automation calls them on threads of its
// own; they answer from the tree last published. What a client asks the app to do (invoke, toggle,
// select, set a value, focus, scroll) comes to the main loop as an SDL event; what changes in the
// tree (focus, names, states, values, children, live regions) is raised as UI Automation events.

#include <kompoz/layout.hpp>

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace kompoz::sdl {

class UiaBridge {
public:
    explicit UiaBridge(void* hwnd);  // the window's HWND
    ~UiaBridge();
    UiaBridge(const UiaBridge&) = delete;
    UiaBridge& operator=(const UiaBridge&) = delete;

    // A client asked for the window's tree: it is worth publishing.
    bool active() const;
    // Whether the bridge wants the tree now (it was just asked for), even if nothing changed.
    bool takeTreeRequest();

    // The tree as it is now (UI thread), with the window's pixels per dp. What changed since the
    // last one is raised as events.
    void publish(std::vector<SemanticsNode> nodes, float dpToPixels);

    // What clients asked for; each one also comes as an SDL event of eventType().
    struct Request {
        int id = 0;
        SemanticsAction action = SemanticsAction::Click;
        float argument = 0;
        std::string text;  // SetText's
    };
    uint32_t eventType() const;  // 0: none will come
    std::vector<Request> takeRequests();

    // What UI Automation gets for the window: its root, an IRawElementProviderSimple (a new
    // reference). For tests, which ask the providers themselves.
    void* rootProvider();

    struct State;

private:
    std::shared_ptr<State> state_;
};

} // namespace kompoz::sdl
