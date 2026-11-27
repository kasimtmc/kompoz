#pragma once

// Two panes side by side (or one over the other) with a splitter between them that the user drags
// to share the room: a file tree and an editor, a list and its details. Compose has none; this is
// the API of Compose Multiplatform's SplitPane component (org.jetbrains.compose.splitpane):
//
//   val state = rememberSplitPaneState(initialPositionPercentage = 0.3f)
//   HorizontalSplitPane(splitPaneState = state) {
//       first(minSize = 200.dp) { FileTree() }
//       second(minSize = 300.dp) { Editor() }
//       splitter {
//           visiblePart { Box(Modifier.width(1.dp).fillMaxHeight().background(MaterialTheme.colorScheme.outlineVariant)) }
//           handle { Box(Modifier.markAsHandle().cursorForHorizontalResize().width(9.dp).fillMaxHeight()) }
//       }
//   }
//
// The splitter's place is positionPercentage of the way between where the first pane is at its
// minSize and where the second one is at its own (0: the first at its least). Without splitter { }
// the splitter is a 1 dp line with a handle over it as wide as the pointer needs (8 dp for a mouse,
// 24 for a finger). The handle (Modifier.markAsHandle()) is dragged; Kompoz's own, it also takes
// the keyboard focus (the arrow keys move it, Shift more, Home and End to the ends) and is a
// slider to accessibility services (its place, set from them). Right to left, a horizontal one
// has its first pane at the right.

#include "layout.hpp"
#include "named.hpp"
#include "runtime.hpp"
#include "saved_value.hpp"
#include "state.hpp"

#include <functional>
#include <memory>
#include <optional>

namespace kompoz {

class SplitPaneState {
public:
    explicit SplitPaneState(float initialPositionPercentage = 0, bool moveEnabled = true);

    // Where the splitter is, from 0 (the first pane at its minSize) to 1 (the second at its own).
    // Observable; setting it moves the splitter.
    float positionPercentage() const;
    void setPositionPercentage(float positionPercentage) const;
    // false: the user can't move it (code still can).
    bool moveEnabled() const;
    void setMoveEnabled(bool moveEnabled) const;
    // Moves the splitter `delta` dp towards the second pane (back with a negative one), within
    // its room (Compose Multiplatform's dispatchRawMovement).
    void dispatchRawMovement(float delta) const;

    friend bool operator==(const SplitPaneState& a, const SplitPaneState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};

template <>
struct Saver<SplitPaneState> {
    static SavedValue save(const SplitPaneState& s) { return SavedValue::List{double(s.positionPercentage()), s.moveEnabled()}; }
    static std::optional<SplitPaneState> restore(const SavedValue& v) {
        if (!v.list() || v.list()->size() != 2) return std::nullopt;
        auto at = (*v.list())[0].number();
        auto enabled = (*v.list())[1].boolean();
        if (!at || !enabled) return std::nullopt;
        return SplitPaneState(float(*at), *enabled);
    }
};

// Where the handle is put against the splitter's visible part (Compose Multiplatform's):
// before it, over it (centered), or after it.
enum class SplitterHandleAlignment { BEFORE, ABOVE, AFTER };

namespace detail {

SplitPaneState rememberSplitPaneState(float initialPositionPercentage, bool moveEnabled, CallSite site);
struct RememberSplitPaneStateImpl {
    using P = Params<names::InitialPositionPercentage, names::MoveEnabled>;
    template <class... A>
    static SplitPaneState call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        return rememberSplitPaneState(float(P::get<names::InitialPositionPercentage>(0.f, args...)), P::get<names::MoveEnabled>(true, args...),
                                      site);
    }
};

// What a split pane's block gives (SplitPaneScope's first, second and splitter; SplitterScope's
// visiblePart and handle).
struct SplitPaneReceiver {
    struct Pane {
        float minSize = 0;
        Content content;
    };
    std::optional<Pane> first, second;
    Content visiblePart, handle;
    SplitterHandleAlignment handleAlignment = SplitterHandleAlignment::ABOVE;
    bool inSplitter = false;
};
SplitPaneReceiver*& splitPaneReceiver();
[[noreturn]] void outsideSplitPane(const char* function);

void splitPane(bool vertical, const Modifier& modifier, const std::optional<SplitPaneState>& state, const SplitPaneReceiver& panes,
               CallSite site);

template <bool Vertical>
struct SplitPaneImpl {
    using P = Params<names::Modifier, names::SplitPaneState, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::template check<A...>());
        static_assert(P::template blockGiven<A...>(), "kompoz: a split pane takes a block: first { … }, second { … }, splitter { … }");
        SplitPaneReceiver panes;
        SplitPaneReceiver* saved = std::exchange(splitPaneReceiver(), &panes);
        std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...))();  // (the block: what goes where)
        splitPaneReceiver() = saved;
        std::optional<SplitPaneState> state;
        if constexpr (P::template given<names::SplitPaneState, A...>()) state = P::template arg<names::SplitPaneState>(args...);
        splitPane(Vertical, P::template get<names::Modifier>(Modifier{}, args...), state, panes, site);
    }
};

// first(minSize = …) { … } and second(…) { … }: a pane, its least size along the split (dp).
template <class... A>
SplitPaneReceiver::Pane paneOf(const char* function, A&&... args) {
    using P = Params<names::MinSize, Block>;
    static_assert(P::check<A...>());
    static_assert(P::blockGiven<A...>(), "kompoz: first and second take a block: the pane's content");
    if (!splitPaneReceiver()) outsideSplitPane(function);
    return {float(P::get<names::MinSize>(0.f, args...)), Content(std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...)))};
}

} // namespace detail

inline constexpr Composable<detail::RememberSplitPaneStateImpl> rememberSplitPaneState;
inline constexpr Composable<detail::SplitPaneImpl<false>> HorizontalSplitPane;  // first at the start, second at the end
inline constexpr Composable<detail::SplitPaneImpl<true>> VerticalSplitPane;     // first above, second below

// In a split pane's block.
template <class... A>
void first(A&&... args) {
    detail::splitPaneReceiver() && (detail::splitPaneReceiver()->first = detail::paneOf("first", std::forward<A>(args)...), true);
}
template <class... A>
void second(A&&... args) {
    detail::splitPaneReceiver() && (detail::splitPaneReceiver()->second = detail::paneOf("second", std::forward<A>(args)...), true);
}
// splitter { visiblePart { … }; handle { … } }: the splitter's own look.
template <class F>
void splitter(F&& block) {
    detail::SplitPaneReceiver* r = detail::splitPaneReceiver();
    if (!r) detail::outsideSplitPane("splitter");
    r->inSplitter = true;
    block();
    r->inSplitter = false;
}
// In splitter { }: what is drawn between the panes (taking its room)...
template <class F>
void visiblePart(F&& content) {
    detail::SplitPaneReceiver* r = detail::splitPaneReceiver();
    if (!r || !r->inSplitter) detail::outsideSplitPane("visiblePart");
    r->visiblePart = Content(std::forward<F>(content));
}
// ...and what is dragged, over it (taking no room): give it Modifier.markAsHandle().
template <class... A>
void handle(A&&... args) {
    using P = detail::Params<detail::names::Alignment, detail::Block>;
    static_assert(P::check<A...>());
    detail::SplitPaneReceiver* r = detail::splitPaneReceiver();
    if (!r || !r->inSplitter) detail::outsideSplitPane("handle");
    r->handleAlignment = P::get<detail::names::Alignment>(SplitterHandleAlignment::ABOVE, args...);
    r->handle = Content(std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...)));
}

} // namespace kompoz
