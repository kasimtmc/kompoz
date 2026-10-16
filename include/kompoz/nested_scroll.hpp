#pragma once

// Nested scrolling (Compose's Modifier.nestedScroll). A scrollable offers every scroll step to
// the nestedScroll() connections and scrollables around it: first (pre-scroll, outermost first),
// then uses what is left itself, then offers the rest (post-scroll, innermost first). That is how
// a collapsing app bar takes the scroll before the list, a bottom sheet moves with what a list
// leaves at its top, and an outer list carries on where an inner one ends. Flings go the same way.
//
// Offsets and velocities follow the finger: dragging up gives a negative y (the content moves up,
// towards its end). Every callback returns the part of `available` it used.

#include "layout.hpp"
#include "runtime.hpp"

#include <functional>
#include <memory>

namespace kompoz {

class NestedScrollConnection {
public:
    virtual ~NestedScrollConnection() = default;
    virtual Offset onPreScroll(Offset available, NestedScrollSource source) {
        (void)available, (void)source;
        return {};
    }
    virtual Offset onPostScroll(Offset consumed, Offset available, NestedScrollSource source) {
        (void)consumed, (void)available, (void)source;
        return {};
    }
    // When the finger lifts, before the scrollable flings, and once its fling is over (with the
    // velocity that was left). Unlike Compose these don't suspend: launch a coroutine to animate.
    virtual Velocity onPreFling(Velocity available) {
        (void)available;
        return {};
    }
    virtual Velocity onPostFling(Velocity consumed, Velocity available) {
        (void)consumed, (void)available;
        return {};
    }
};

// A connection made of lambdas (missing ones use nothing).
struct NestedScrollHandlers {
    std::function<Offset(Offset available, NestedScrollSource source)> onPreScroll;
    std::function<Offset(Offset consumed, Offset available, NestedScrollSource source)> onPostScroll;
    std::function<Velocity(Velocity available)> onPreFling;
    std::function<Velocity(Velocity consumed, Velocity available)> onPostFling;
};
std::shared_ptr<NestedScrollConnection> nestedScrollConnection(NestedScrollHandlers handlers);
// The same connection on every recomposition, running the latest handlers.
std::shared_ptr<NestedScrollConnection> rememberNestedScrollConnection(NestedScrollHandlers handlers,
                                                                       CallSite site = CallSite::current());

// Lets a custom gesture (a pointerInput drag) take part like a scrollable: pass it to
// nestedScroll() and it dispatches to the parents of that modifier.
class NestedScrollDispatcher {
public:
    NestedScrollDispatcher();
    Offset dispatchPreScroll(Offset available, NestedScrollSource source) const;
    Offset dispatchPostScroll(Offset consumed, Offset available, NestedScrollSource source) const;
    Velocity dispatchPreFling(Velocity available) const;
    Velocity dispatchPostFling(Velocity consumed, Velocity available) const;
    friend bool operator==(const NestedScrollDispatcher& a, const NestedScrollDispatcher& b) { return a.data_ == b.data_; }

    struct Data;

private:
    friend class Modifier;
    std::shared_ptr<Data> data_;
};

struct NestedScrollElement final : ModifierElement {
    std::shared_ptr<NestedScrollConnection> connection;
    std::shared_ptr<NestedScrollDispatcher::Data> dispatcher;  // may be null
    void attach(LayoutNode& node) const override;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const NestedScrollElement*>(&other);
        return o && o->connection == connection && o->dispatcher == dispatcher;
    }
};

namespace detail {

// The nested scroll parents of the element at `index` on `node`: the connections and scrollables
// outside it on the same node, then on its ancestors, up to the window's or popup's root.
class NestedScrollChain {
public:
    NestedScrollChain(const LayoutNode& node, size_t index);
    Offset preScroll(Offset available, NestedScrollSource source) const;
    Offset postScroll(Offset consumed, Offset available, NestedScrollSource source) const;
    Velocity preFling(Velocity available) const;
    Velocity postFling(Velocity consumed, Velocity available) const;

private:
    struct Link {
        std::shared_ptr<const ModifierElement> element;  // keeps it alive while its callbacks run
        NestedScrollConnection* connection = nullptr;
        const ScrollableElement* scrollable = nullptr;
        bool reversed = false;  // a horizontal one laid out right to left: the finger the other way
    };
    std::vector<Link> links_;  // innermost first
};

} // namespace detail

} // namespace kompoz
