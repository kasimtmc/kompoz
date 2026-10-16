#pragma once

// Interactions, as Compose's InteractionSource: what the user is doing to a component —
// pressing it, hovering it with the mouse, focusing it, dragging it. A component given a
// MutableInteractionSource tells it each start and end; read whether one is going on
// (collectIsPressedAsState()...), or hear each one (interactions(), a SharedFlow).
//
//   auto source = remember([] { return MutableInteractionSource(); });
//   Button(onClick, Modifier(), {.interactionSource = source}, [&] { Text(source.collectIsPressedAsState() ? "Basılı" : "Bas"); });
//
//   // A read-only field that opens a picker when tapped:
//   LaunchedEffect(source, [source, open]() -> Task {
//       co_await source.interactions().collect([open](const Interaction& i) {
//           if (i.type == Interaction::Type::Release) open.set(true);
//       });
//   });
//
// Scrollables' states have one too (ScrollState, LazyListState, PagerState::interactionSource()):
// it hears the user's drags (collectIsDraggedAsState(): a carousel that pauses while held).

#include "graphics.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

namespace kompoz {

template <class T>
class SharedFlow;

struct Interaction {
    enum class Type { Press, Release, Cancel, HoverEnter, HoverExit, Focus, Unfocus, DragStart, DragStop, DragCancel };
    Type type = Type::Press;
    uint64_t id = 0;       // a start's own; what ends it has the same
    Offset pressPosition;  // a press's (and its release's or cancel's), in the component

    // Starts (each with a new id) and their ends, for emitting your own (Compose:
    // PressInteraction.Press(offset), PressInteraction.Release(press)...).
    static Interaction press(Offset position = {});
    static Interaction hoverEnter();
    static Interaction focus();
    static Interaction dragStart();
    Interaction release() const { return {Type::Release, id, pressPosition}; }
    Interaction cancel() const { return {type == Type::DragStart ? Type::DragCancel : Type::Cancel, id, pressPosition}; }
    Interaction exit() const { return {Type::HoverExit, id, {}}; }
    Interaction unfocus() const { return {Type::Unfocus, id, {}}; }
    Interaction stop() const { return {Type::DragStop, id, {}}; }
    bool isStart() const { return type == Type::Press || type == Type::HoverEnter || type == Type::Focus || type == Type::DragStart; }

    friend bool operator==(const Interaction& a, const Interaction& b) {
        return a.type == b.type && a.id == b.id && a.pressPosition.x == b.pressPosition.x && a.pressPosition.y == b.pressPosition.y;
    }
};

namespace detail {
struct InteractionData;
// A source's presses as a node drawing them (Modifier::indication) follows them in frame time:
// one seen for the first time starts now, one gone is let go now; `done` says which to forget.
struct PressTrack {
    uint64_t id = 0;
    Offset origin;
    int64_t start = 0, released = -1;
};
}
class MutableInteractionSource;
namespace detail {
std::vector<PressTrack> followPresses(const MutableInteractionSource& source, int64_t now,
                                      const std::function<bool(const PressTrack&)>& done);
}

// Copies share it (remember one, pass it by value). On the UI thread.
class MutableInteractionSource {
public:
    MutableInteractionSource();
    // Every interaction from now on (none is kept for later collectors).
    SharedFlow<Interaction> interactions() const;
    void tryEmit(const Interaction& interaction) const;
    // Observable (read while composing, it recomposes when it changes): whether a press, a
    // hover, a focus or a drag has started and not ended yet.
    bool collectIsPressedAsState() const;
    bool collectIsHoveredAsState() const;
    bool collectIsFocusedAsState() const;
    bool collectIsDraggedAsState() const;
    // Where the latest press still going on started (Compose's indications draw from it).
    Offset pressPosition() const;

    friend bool operator==(const MutableInteractionSource& a, const MutableInteractionSource& b) { return a.data_ == b.data_; }
    std::shared_ptr<detail::InteractionData> data_;
};

} // namespace kompoz

template <>
struct std::hash<kompoz::MutableInteractionSource> {
    size_t operator()(const kompoz::MutableInteractionSource& s) const noexcept { return std::hash<const void*>{}(s.data_.get()); }
};
