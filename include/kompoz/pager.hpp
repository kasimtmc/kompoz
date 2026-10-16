#pragma once

// HorizontalPager / VerticalPager: full-size pages you swipe between; it snaps to the nearest
// page, or the next one after a fling. A scrollable inside a page scrolls first; the pager takes
// over where it ends (nested scrolling).
//
//   auto pager = rememberPagerState(0, [] { return 5; });
//   HorizontalPager(pager, Modifier().fillMaxWidth().height(200), [](int page) { Text(...); });
//   scope.launch([pager]() -> Task { co_await pager.animateScrollToPage(2); });

#include "animation.hpp"
#include "coroutines.hpp"
#include "layout.hpp"
#include "runtime.hpp"

#include <algorithm>
#include <functional>
#include <memory>

namespace kompoz {

class PagerState {
public:
    PagerState(int initialPage, std::function<int()> pageCount);

    // Observable: the page closest to the snap position (PagerOptions::snapPosition).
    int currentPage() const;
    // Observable: the page the pager last came to rest on (not changing while it moves).
    int settledPage() const;
    // Observable: the page a fling or animateScrollToPage is taking it to; currentPage otherwise.
    int targetPage() const;
    // How far (in pages, -0.5..0.5) the scroll position is from currentPage. Not observable.
    float currentPageOffsetFraction() const;
    // How far `page` is from the snap position, in pages (Compose's): 0 there, 1 a page on,
    // -1.5 a page and a half back. Read it while drawing (graphicsLayer's block) for pages
    // that shrink or fade as they go. Not observable.
    float getOffsetDistanceInPages(int page) const;
    int pageCount() const;
    bool isScrollInProgress() const;
    // Whether there is more to scroll that way (from the last layout).
    bool canScrollForward() const;
    bool canScrollBackward() const;

    // To `page`, and `pageOffsetFraction` of a page on from it (where the pager can go).
    void scrollToPage(int page, float pageOffsetFraction = 0) const;
    Task animateScrollToPage(int page, AnimationSpec spec = spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow)) const;
    Task animateScrollToPage(int page, float pageOffsetFraction,
                             AnimationSpec spec = spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow)) const;
    // Hears the user's drags of the pager (collectIsDraggedAsState(): an auto-advancing pager
    // that waits while it is held).
    const MutableInteractionSource& interactionSource() const;

    friend bool operator==(const PagerState& a, const PagerState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};

template <>
struct Saver<PagerState> {
    static SavedValue save(const PagerState& s) { return int64_t(s.currentPage()); }
    static std::optional<PagerState> restore(const SavedValue& v) {
        auto page = v.integer();  // the page count comes with rememberPagerState
        return page ? std::optional(PagerState(int(*page), [] { return 1 << 30; })) : std::nullopt;
    }
};

PagerState rememberPagerState(int initialPage, std::function<int()> pageCount, CallSite site = CallSite::current());

// How long a page is along the pager (Compose's PageSize): all the room there is (Fill: the
// pager less its content padding), a fixed size (dp), or what `calculateMainAxisPageSize` says
// from that room and the spacing between pages:
//   PageSize{[](float room, float spacing) { return (room - spacing) / 2; }}  // two pages a view
struct PageSize {
    std::function<float(float availableSpace, float pageSpacing)> calculateMainAxisPageSize;  // empty: Fill
    static const PageSize Fill;
    static PageSize Fixed(float pageSize) {
        return {[pageSize](float, float) { return pageSize; }};
    }
};
inline const PageSize PageSize::Fill{};

// How many pages a fling may turn (Compose's PagerSnapDistance): at most `pages` from the page
// it starts on, however fast it is.
struct PagerSnapDistance {
    int pages = 1;
    static PagerSnapDistance atMost(int pages) { return {std::max(0, pages)}; }
};
// How a pager settles when let go (Compose's PagerDefaults.flingBehavior): a fling goes on as
// far as its speed carries it (at least to the next page that way, at most pagerSnapDistance
// pages); a slow drag turns the page once it went snapPositionalThreshold of it, else goes back.
struct PagerFlingBehavior {
    PagerSnapDistance pagerSnapDistance = PagerSnapDistance::atMost(1);
    float snapPositionalThreshold = 0.5f;
    AnimationSpec snapAnimationSpec = spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow);
};
namespace PagerDefaults {
inline PagerFlingBehavior flingBehavior(const PagerState&, PagerSnapDistance pagerSnapDistance = PagerSnapDistance::atMost(1),
                                        float snapPositionalThreshold = 0.5f) {
    return {pagerSnapDistance, snapPositionalThreshold};
}
}  // namespace PagerDefaults

struct PagerOptions {
    PaddingValues contentPadding;  // start/end padding lets the neighbors peek in
    PageSize pageSize = PageSize::Fill;
    float pageSpacing = 0;
    // Where the current page sits (smaller pages than the pager): at the start, in the middle,
    // at the end — as far as the pages go (contentPadding lets the first and last ones get there).
    SnapPosition snapPosition = SnapPosition::Start;
    int beyondViewportPageCount = 0;  // extra pages kept composed on each side
    bool userScrollEnabled = true;
    // Pages from the end (Compose's reverseLayout): the first page at the right (a vertical
    // pager's at the bottom), the next ones further left, the finger the other way.
    bool reverseLayout = false;
    // Each page's key (Compose's key): a page keeps its state by it, and the current page stays
    // the same page when pages come or go before it.
    std::function<size_t(int page)> key;
    PagerFlingBehavior flingBehavior;
    // Pages less tall than a HorizontalPager (or less wide than a VerticalPager) sit so; pages
    // get what they need along the pager too, up to the page size (from its start).
    VerticalAlignment verticalAlignment = Alignment::CenterVertically;
    HorizontalAlignment horizontalAlignment = Alignment::CenterHorizontally;
    // Its overscroll effect: unset, the stretch; nullptr, none; or your own (as LazyListOptions').
    std::optional<std::shared_ptr<OverscrollEffect>> overscrollEffect;
};

void HorizontalPager(PagerState state, const Modifier& modifier, const std::function<void(int page)>& content,
                     PagerOptions options = {}, CallSite site = CallSite::current());
// Pages stacked vertically, each as tall as the pager (give it a bounded height).
void VerticalPager(PagerState state, const Modifier& modifier, const std::function<void(int page)>& content,
                   PagerOptions options = {}, CallSite site = CallSite::current());

} // namespace kompoz
