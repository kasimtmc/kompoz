// Scrollbars, as Compose Multiplatform has them on the desktop: a thumb as long as the share of
// the content in view, where the content is; drag it to scroll, click the track to page there.
//
//   auto scroll = rememberScrollState();
//   Box(Modifier().fillMaxSize(), [&] {
//       Column(Modifier().fillMaxSize().verticalScroll(scroll), ...);
//       VerticalScrollbar(rememberScrollbarAdapter(scroll), Modifier().align(Alignment::CenterEnd).fillMaxHeight());
//   });
#pragma once

#include "interaction.hpp"
#include "lazy.hpp"
#include "layout.hpp"
#include "runtime.hpp"

#include <memory>
#include <optional>

namespace kompoz {

// What a scrollbar shows and moves (Compose's ScrollbarAdapter): read while drawing, so a scroll
// redraws it without recomposing anything.
class ScrollbarAdapter {
public:
    virtual ~ScrollbarAdapter() = default;
    virtual float scrollOffset() const = 0;  // dp scrolled from the start
    virtual float contentSize() const = 0;   // dp, all of it
    virtual float viewportSize() const = 0;  // dp, the part in view
    virtual void scrollTo(float offset) const = 0;
};

// For verticalScroll/horizontalScroll's state, and a lazy list's (its content size estimated from
// the items in view, as Compose does).
std::shared_ptr<ScrollbarAdapter> rememberScrollbarAdapter(const ScrollState& state, CallSite site = CallSite::current());
std::shared_ptr<ScrollbarAdapter> rememberScrollbarAdapter(const LazyListState& state, CallSite site = CallSite::current());

struct ScrollbarStyle {
    float minimalHeight = 16;  // the thumb's least length
    float thickness = 8;
    Shape shape = RoundedCornerShape(4);
    int hoverDurationMillis = 300;  // the color's change when the mouse comes and goes
    Color unhoverColor{0, 0, 0, 0.12f}, hoverColor{0, 0, 0, 0.5f};
    friend bool operator==(const ScrollbarStyle&, const ScrollbarStyle&) = default;
};
inline const CompositionLocal<ScrollbarStyle> LocalScrollbarStyle{ScrollbarStyle{}};

struct ScrollbarOptions {
    bool reverseLayout = false;  // a reversed list's: the start at the end
    std::optional<ScrollbarStyle> style;  // LocalScrollbarStyle's unless given
    std::optional<MutableInteractionSource> interactionSource;  // told of hovers and drags
};
// The scrollbar fills the length its modifier gives it (fillMaxHeight beside the content) and is
// style.thickness thick; without anything to scroll it shows no thumb.
void VerticalScrollbar(std::shared_ptr<ScrollbarAdapter> adapter, const Modifier& modifier = {}, const ScrollbarOptions& options = {},
                       CallSite site = CallSite::current());
void HorizontalScrollbar(std::shared_ptr<ScrollbarAdapter> adapter, const Modifier& modifier = {}, const ScrollbarOptions& options = {},
                         CallSite site = CallSite::current());

} // namespace kompoz
