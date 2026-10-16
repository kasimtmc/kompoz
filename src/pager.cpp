#include <kompoz/nested_scroll.hpp>
#include <kompoz/pager.hpp>

#include <cmath>

namespace kompoz {

struct PagerState::Data {
    MutableInteractionSource interactions;
    MutableState<int> current{0};
    MutableState<int> settled{0};
    MutableState<int> target{-1};   // where a fling or animateScrollToPage takes it (-1: nowhere)
    MutableState<int> window{0};    // bumped when the composed pages no longer cover the view
    Animatable<float> position{0};  // in pages: k puts page k at the snap position; read during layout only
    std::function<int()> count;
    PagerFlingBehavior fling;       // the pager's, from the last composition
    float pageStride = 0;           // page size + spacing, from the last layout
    float minPosition = 0, maxPosition = kInfinity;  // where the pages end, from the last layout
    int firstVisible = 0, lastVisible = 0;           // the pages in view, from the last layout
    float lastMove = 0;  // the last drag's direction (> 0: on)
    bool dragging = false;
    std::optional<size_t> currentKey;  // the current page's key, from the last composition (PagerOptions::key)

    int pages() const { return count ? std::max(0, count()) : 0; }
    float clampPosition(float p) const {
        float last = std::max(minPosition, std::min(maxPosition, float(std::max(0, pages() - 1))));
        return std::clamp(p, minPosition, last);
    }
    // The page nearest the snap position at `p`.
    int pageAt(float p) const { return std::clamp(int(std::lround(p)), 0, std::max(0, pages() - 1)); }
};

PagerState::PagerState(int initialPage, std::function<int()> pageCount) : data_(std::make_shared<Data>()) {
    data_->count = std::move(pageCount);
    float p = data_->clampPosition(float(initialPage));
    data_->position.snapTo(p);
    data_->current.set(data_->pageAt(p));
    data_->settled.set(data_->pageAt(p));
    data_->firstVisible = data_->lastVisible = data_->pageAt(p);
}

int PagerState::currentPage() const { return data_->current.get(); }
int PagerState::settledPage() const { return data_->settled.get(); }
int PagerState::targetPage() const {
    int target = data_->target.get();
    return target >= 0 ? target : currentPage();
}
float PagerState::currentPageOffsetFraction() const { return data_->position.value() - float(data_->current.peek()); }
float PagerState::getOffsetDistanceInPages(int page) const { return float(page) - data_->position.value(); }
int PagerState::pageCount() const { return data_->pages(); }
const MutableInteractionSource& PagerState::interactionSource() const { return data_->interactions; }
bool PagerState::isScrollInProgress() const { return data_->dragging || data_->position.isRunning(); }
bool PagerState::canScrollForward() const { return data_->position.value() < data_->clampPosition(kInfinity) - 1e-3f; }
bool PagerState::canScrollBackward() const { return data_->position.value() > data_->minPosition + 1e-3f; }

void PagerState::scrollToPage(int page, float pageOffsetFraction) const {
    float p = data_->clampPosition(float(page) + pageOffsetFraction);
    data_->position.snapTo(p);
    data_->target.set(-1);
    data_->settled.set(data_->pageAt(p));
}

namespace {

// Moves the pager to `to` (in pages) as `spec` says, heading for `page` until it gets there
// (then that's where it settled) or a drag takes over; another animation takes over its target.
Task animatePager(std::shared_ptr<PagerState::Data> d, float to, int page, AnimationSpec spec) {
    d->target.set(page);
    co_await d->position.animateTo(to, spec);
    if (d->position.isRunning()) co_return;
    d->target.set(-1);
    if (!d->dragging) d->settled.set(d->pageAt(d->position.value()));
}

} // namespace

Task PagerState::animateScrollToPage(int page, AnimationSpec spec) const { return animateScrollToPage(page, 0, spec); }

Task PagerState::animateScrollToPage(int page, float pageOffsetFraction, AnimationSpec spec) const {
    float to = data_->clampPosition(float(page) + pageOffsetFraction);
    return animatePager(data_, to, data_->pageAt(to), spec);
}

PagerState rememberPagerState(int initialPage, std::function<int()> pageCount, CallSite site) {
    auto& state = rememberSaveable([&] { return PagerState(initialPage, pageCount); }, site);
    state.data_->count = std::move(pageCount);  // the latest count function
    return state;
}

namespace {

// Where a drag that moved the pager leaves it (`velocity` towards the end): a fling as far as
// its speed would carry it — at least the next page its way, at most pagerSnapDistance pages
// from the one it leaves; a slow drag on to the next page its way past snapPositionalThreshold
// of it, else back. A pager no drag moved stays as it is.
void settlePager(const std::shared_ptr<PagerState::Data>& d, const CoroutineScope& coroutines, float velocity) {
    float pos = d->position.value();
    if (!d->dragging && (d->position.isRunning() || std::round(pos) == pos)) return;  // not ours to move
    d->dragging = false;
    const PagerFlingBehavior& fling = d->fling;
    float target;
    if (std::abs(velocity) > 300) {
        float stride = std::max(1.f, d->pageStride);
        float carried = std::round(splineBasedDecay().calculateTargetValue(pos * stride, velocity) / stride);
        float left = velocity > 0 ? std::floor(pos) : std::ceil(pos), most = float(fling.pagerSnapDistance.pages);
        target = velocity > 0 ? std::max(std::ceil(pos), carried) : std::min(std::floor(pos), carried);
        target = std::clamp(target, left - most, left + most);
    } else {
        float base = std::floor(pos), past = pos - base;
        float threshold = std::clamp(fling.snapPositionalThreshold, 0.f, 1.f);
        target = base + (d->lastMove >= 0 ? (past > threshold ? 1 : 0) : (past < 1 - threshold ? 0 : 1));
    }
    int page = std::clamp(int(target), 0, std::max(0, d->pages() - 1));
    float to = d->clampPosition(float(page));
    coroutines.launch([d, to, page, spec = fling.snapAnimationSpec]() -> Task { co_await animatePager(d, to, page, spec); });
}

// The pager scrolls like any scrollable (so a list inside a page goes first) and snaps to a page
// when the drag ends.
struct PagerElement final : ScrollableElement {
    PagerElement(std::shared_ptr<PagerState::Data> data, CoroutineScope scope, bool v, bool r)
        : d(std::move(data)), coroutines(std::move(scope)), vertical(v), reversed(r) {}
    std::shared_ptr<PagerState::Data> d;
    CoroutineScope coroutines;
    bool vertical, reversed;  // reversed: reverseLayout, the pages on towards the start
    Orientation orientation() const override { return vertical ? Orientation::Vertical : Orientation::Horizontal; }
    std::optional<MutableInteractionSource> interactionSource() const override { return d->interactions; }
    float scrollBy(float delta) const override {
        if (reversed) return -scrollBy(-delta, true);
        return scrollBy(delta, true);
    }
    float scrollBy(float delta, bool) const {
        float stride = std::max(1.f, d->pageStride);
        float before = d->position.value();
        d->position.snapTo(d->clampPosition(before + delta / stride));
        d->dragging = true;
        if (delta != 0) d->lastMove = delta;
        return (d->position.value() - before) * stride;
    }
    // A list inside a page hands over what it couldn't scroll while dragged, not while flinging.
    float scrollByNested(float delta, NestedScrollSource source) const override {
        return source == NestedScrollSource::UserInput ? scrollBy(delta) : 0;
    }
    bool settle(float velocity) const override {
        settlePager(d, coroutines, reversed ? -velocity : velocity);
        return true;
    }
    void draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode&, const std::function<void()>& drawContent) const override {
        canvas.pushClip(clipBounds(bounds));
        drawContent();
        canvas.popClip();
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const PagerElement*>(&other);
        return o && o->d == d && o->vertical == vertical && o->reversed == reversed && o->overscroll == overscroll &&
               o->overscrollEffect == overscrollEffect;
    }
};

// On every page: while the pager sits between two pages, a drag on a list inside a page moves
// the pager back to a page first; what is left of the list's fling settles the pager and goes no
// further.
class PageConnection final : public NestedScrollConnection {
public:
    PageConnection(std::shared_ptr<PagerState::Data> data, CoroutineScope scope, bool vertical)
        : d(std::move(data)), coroutines(std::move(scope)), vertical(vertical) {}
    Offset onPreScroll(Offset available, NestedScrollSource source) override {
        float delta = (vertical ? available.y : available.x) * (reversed ? -1.f : 1.f);  // reversed: the finger the other way
        float pos = d->position.value();
        if (source != NestedScrollSource::UserInput || delta == 0 || std::abs(pos - std::round(pos)) < 1e-4f) return {};
        float stride = std::max(1.f, d->pageStride);
        float next = std::clamp(pos - delta / stride, std::floor(pos), std::ceil(pos));
        d->position.snapTo(next);
        d->dragging = true;
        d->lastMove = -delta;
        float used = (pos - next) * stride * (reversed ? -1.f : 1.f);
        return vertical ? Offset{0, used} : Offset{used, 0};
    }
    // The pager settles here, with the rest of the list's speed: a list flicked on past its end
    // turns the page. (The pager's own settle further up the chain then gets no velocity and
    // finds it already moving.)
    Velocity onPostFling(Velocity, Velocity available) override {
        float v = vertical ? available.y : available.x;  // the finger's direction
        settlePager(d, coroutines, reversed ? v : -v);
        return vertical ? Velocity{0, available.y} : Velocity{available.x, 0};
    }
    std::shared_ptr<PagerState::Data> d;
    CoroutineScope coroutines;
    bool vertical;
    bool reversed = false;  // the finger the other way: a horizontal pager right to left, or reverseLayout (not both)
};

void pager(PagerState state, const Modifier& modifier, const std::function<void(int page)>& content,
           const PagerOptions& options, bool vertical) {
    auto d = state.data_;
    d->fling = options.flingBehavior;
    d->window.get();  // recompose when the pages in view aren't all composed
    int count = d->pages();
    // Keyed pages: the current page stays the page it was (its key) when pages come or go before it.
    if (options.key && count > 0) {
        int was = d->current.peek();
        if (d->currentKey && !d->dragging && !d->position.isRunning() && (was >= count || options.key(was) != *d->currentKey))
            for (int page = 0; page < count; ++page)
                if (options.key(page) == *d->currentKey) {
                    float p = d->clampPosition(d->position.value() + float(page - was));
                    d->position.snapTo(p);
                    d->current.set(was = d->pageAt(p));  // (settled there as the layout finds it at rest)
                    break;
                }
        d->currentKey = options.key(std::clamp(was, 0, count - 1));
    }
    int current = count ? std::clamp(state.currentPage(), 0, count - 1) : 0;
    // The pages in view and one more each way (the next one a drag brings in), and as many
    // more as beyondViewportPageCount says.
    int around = 1 + std::max(0, options.beyondViewportPageCount);
    int first = std::max(0, std::min(current, d->firstVisible) - around), last = std::min(count - 1, std::max(current, d->lastVisible) + around);
    auto coroutines = rememberCoroutineScope();
    auto pageConnection = remember([&] { return std::make_shared<PageConnection>(d, coroutines, vertical); });
    pageConnection->reversed = (!vertical && LocalLayoutDirection.current() == LayoutDirection::Rtl) != options.reverseLayout;
    pageConnection->d = d;
    pageConnection->vertical = vertical;

    Modifier m = modifier;
    if (options.userScrollEnabled) {
        auto element = std::make_shared<PagerElement>(d, coroutines, vertical, options.reverseLayout);
        if (options.overscrollEffect)
            element->overscroll = false, element->overscrollEffect = *options.overscrollEffect;
        else
            element->overscroll = detail::overscrollWanted();
        m = m.then(std::move(element));
    } else {
        m = m.clipToBounds();
    }

    PaddingValues padding = options.contentPadding;
    float spacing = options.pageSpacing;
    float crossBias = vertical ? options.horizontalAlignment.bias : options.verticalAlignment.bias;
    detail::Composer::require().emit(
        detail::keyOf(CallSite::current()),
        [&](LayoutNode& node) {
            node.modifier = m;
            node.measurePolicy = [d, first, last, padding, spacing, vertical, crossBias, pageSize = options.pageSize,
                                  snap = options.snapPosition, reverse = options.reverseLayout](LayoutNode& n, const Constraints& c) {
                // Main axis: the pages' direction; cross axis: the other one. Reversed: laid out as
                // from the end (its padding first), then mirrored.
                float mainPadStart = vertical ? padding.top : padding.start, mainPadEnd = vertical ? padding.bottom : padding.end;
                if (reverse) std::swap(mainPadStart, mainPadEnd);
                float crossPadStart = vertical ? padding.start : padding.top, crossPadEnd = vertical ? padding.end : padding.bottom;
                bool mainBounded = vertical ? c.hasBoundedHeight() : c.hasBoundedWidth();
                float main = mainBounded ? (vertical ? c.maxHeight : c.maxWidth) : 0;
                float room = std::max(0.f, main - mainPadStart - mainPadEnd);
                float page = std::max(0.f, pageSize.calculateMainAxisPageSize ? pageSize.calculateMainAxisPageSize(room, spacing) : room);
                d->pageStride = page + spacing;
                float stride = std::max(1.f, d->pageStride);
                // Where the current page sits, and how far the pages go: the first one's start at the
                // start padding, the last one's end at the end padding (pages that fit don't scroll).
                float snapOffset = snap == SnapPosition::Start ? 0 : snap == SnapPosition::Center ? (room - page) / 2 : room - page;
                int count = d->pages();
                float extent = count > 0 ? float(count) * d->pageStride - spacing : 0;
                d->minPosition = snapOffset / stride;
                d->maxPosition = (std::max(0.f, extent - room) + snapOffset) / stride;
                float pos = d->position.value();
                if (!detail::isProbeMeasure()) {
                    if (float kept = d->clampPosition(pos); kept != pos && !d->position.isRunning()) d->position.snapTo(pos = kept);
                    int nearest = d->pageAt(pos);
                    if (d->current.peek() != nearest) d->current.set(nearest);
                    // At rest, it settled where it is (the pages moved under it: kept within their ends, resized).
                    if (!d->dragging && !d->position.isRunning() && d->settled.peek() != nearest) d->settled.set(nearest);
                    // The pages in view: from the one at the pager's start to the one at its end.
                    float origin = mainPadStart + snapOffset;
                    // (as a page number: what is out of int's range — an endless padding, no stride — at the ends)
                    auto page = [count](float v) { return std::isnan(v) || v <= -1 ? -1 : v >= float(count) ? count : int(v); };
                    d->firstVisible = std::clamp(page(std::floor(pos - origin / stride)), 0, std::max(0, count - 1));
                    d->lastVisible = std::clamp(page(std::ceil(pos + (main - origin) / stride)) - 1, 0, std::max(0, count - 1));
                    if (count > 0 && (d->firstVisible < first || d->lastVisible > last)) d->window.set(d->window.peek() + 1);
                }
                float crossMax = vertical ? c.maxWidth : c.maxHeight;
                float crossPad = crossPadStart + crossPadEnd;
                float maxCross = crossMax != kInfinity ? std::max(0.f, crossMax - crossPad) : kInfinity;
                // Pages take what they need, up to the page size along the pager (Compose's).
                std::vector<Size> sizes;
                float cross = 0;
                for (auto* child : n.children) {
                    sizes.push_back(vertical ? child->measure({0, maxCross, 0, page}) : child->measure({0, page, 0, maxCross}));
                    cross = std::max(cross, vertical ? sizes.back().width : sizes.back().height);
                }
                Size size = vertical ? c.constrain({cross + crossPad, main}) : c.constrain({main, cross + crossPad});
                float crossRoom = std::max(0.f, (vertical ? size.width : size.height) - crossPad);
                for (size_t k = 0; k < n.children.size(); ++k) {
                    float at = mainPadStart + snapOffset + (float(first + int(k)) - pos) * d->pageStride;
                    if (reverse) at = main - at - (vertical ? sizes[k].height : sizes[k].width);
                    float across = crossPadStart + (crossRoom - (vertical ? sizes[k].width : sizes[k].height)) * (1 + crossBias) / 2;
                    if (vertical)
                        n.children[k]->place(across, at);
                    else
                        n.children[k]->place(at, across);
                }
                return size;
            };
            node.drawContent = {};
        },
        [&] {
            for (int page = first; page <= last; ++page)
                key(options.key ? options.key(page) : size_t(page), [&] {
                    detail::Composer::require().emit(
                        detail::keyOf(CallSite::current()),
                        [&](LayoutNode& pageNode) {
                            pageNode.modifier = Modifier().nestedScroll(pageConnection);
                            pageNode.measurePolicy = detail::stackPolicy;
                            pageNode.drawContent = {};
                        },
                        [&] { content(page); });
                });
        });
}

} // namespace

void HorizontalPager(PagerState state, const Modifier& modifier, const std::function<void(int page)>& content,
                     PagerOptions options, CallSite site) {
    ComposableScope scope(site);
    pager(std::move(state), modifier, content, options, false);
}

void VerticalPager(PagerState state, const Modifier& modifier, const std::function<void(int page)>& content,
                   PagerOptions options, CallSite site) {
    ComposableScope scope(site);
    pager(std::move(state), modifier, content, options, true);
}

} // namespace kompoz
