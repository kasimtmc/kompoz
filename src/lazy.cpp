#include <kompoz/animation.hpp>
#include <kompoz/host.hpp>
#include <kompoz/lazy.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace kompoz {

struct LazyListState::Data {
    MutableInteractionSource interactions;
    MutableState<LazyListLayoutInfo> layoutInfo{LazyListLayoutInfo{}};  // from the last layout
    std::vector<size_t> contentTypes;
    MutableState<int> firstIndex{0};
    MutableState<int> window{0};  // bumped when the composed items no longer cover the view
    float firstOffset = 0;
    uint64_t generation = 0;  // moved on by whatever stops an animated scroll (a drag, another one)
    MutableState<bool> userScrolling{false};  // dragged or flung (the host says)
    MutableState<int> animations{0};          // animated scrolls running

    // From the last composition.
    int count = 0;
    std::vector<size_t> keys;
    int composedStart = 0, composedEnd = 0;  // the items around the view, in order...
    std::vector<int> composed;  // ...and the item each child node shows (a pinned header comes last, on top)
    std::vector<int> sticky;    // sticky header indices, ascending
    int pinned = -1;            // the sticky header composed for the top of the view

    // The header the top of the view belongs to: the last sticky one at or before `index`.
    int headerFor(int index) const {
        auto it = std::upper_bound(sticky.begin(), sticky.end(), index);
        return it == sticky.begin() ? -1 : *std::prev(it);
    }

    // animateItem(): where each animated item was shown and at which index, and its running animations.
    struct ItemAnimation {
        int index = -1;
        float shown = 0;  // main-axis position on screen at the last layout
        float from = 0;   // offset the move animation starts at
        int64_t moveStart = -1, appearStart = -1;
    };
    std::unordered_map<size_t, ItemAnimation> itemAnimations;
    std::unordered_set<size_t> previousKeys;  // all keys before the last composition (to spot insertions)
    bool animatesItems = false;
    // Items whose data is gone, fading out where they were shown; their nodes are the children
    // after the composed ones, kept as they were (Composer::keepGroup).
    struct Leaving {
        size_t key;
        int64_t start;
        float shown;  // main-axis position on screen
        bool done = false;
    };
    std::vector<Leaving> leaving;

    // From the last layout.
    float viewport = 0, spacing = 0;
    // Where the items start..start+n were laid out (from the start edge, before reversing).
    int laidStart = 0;
    std::vector<float> laidPos;
    float laidBefore = 0, laidAfter = 0;  // the content padding before the first item and after the last
    std::unordered_map<size_t, float> sizes;  // measured main-axis sizes by item key
    float sizeSum = 0;
    int sizeCount = 0;
    bool atEnd = false;

    float sizeOf(int index) const {
        auto it = sizes.find(keys[size_t(index)]);
        if (it != sizes.end()) return it->second;
        return sizeCount ? sizeSum / float(sizeCount) : 48.f;
    }

    void recordSize(size_t key, float size) {
        auto [it, inserted] = sizes.try_emplace(key, size);
        if (inserted) {
            ++sizeCount;
        } else {
            sizeSum -= it->second;
            it->second = size;
        }
        sizeSum += size;
    }

    int first() const { return count ? std::clamp(firstIndex.peek(), 0, count - 1) : 0; }

    void setFirst(int index, float offset) {
        firstOffset = offset;
        if (firstIndex.peek() != index) firstIndex.set(index);
    }

    // Items that must be composed to fill the view (one extra on each side).
    std::pair<int, int> visibleRange() const {
        if (count == 0) return {0, 0};
        int start = first();
        float view = viewport > 0 ? viewport : 1000.f;
        float covered = -firstOffset;
        int end = start;
        while (end < count && covered < view) covered += sizeOf(end++) + spacing;
        return {std::max(0, start - 1), std::min(count, end + 1)};
    }

    // What to compose: the visible items plus half a view before and after, so that small
    // scrolls don't need a recomposition.
    std::pair<int, int> composeRange() const {
        auto [start, end] = visibleRange();
        float extra = (viewport > 0 ? viewport : 1000.f) * 0.5f;
        for (float d = 0; start > 0 && d < extra;) d += sizeOf(--start) + spacing;
        for (float d = 0; end < count && d < extra;) d += sizeOf(end++) + spacing;
        return {start, end};
    }

    void requestWindowIfNeeded() {
        auto [start, end] = visibleRange();
        if (start < composedStart || end > composedEnd) window.set(window.peek() + 1);
    }

    // The item a snapping fling that would travel `travel` dp (towards the end) settles on, and
    // the offset that lines it up: the one whose snap point is nearest to where it would stop.
    std::pair<int, float> snapTarget(float travel, SnapPosition position) const {
        if (count == 0) return {0, 0};
        float inner = std::max(0.f, viewport - laidBefore - laidAfter);
        auto offsetFor = [&](int i) {
            float room = inner - sizeOf(i);
            return position == SnapPosition::Start ? 0.f : position == SnapPosition::Center ? -room / 2 : -room;
        };
        int best = std::clamp(first(), 0, count - 1);
        float bestGap = kInfinity;
        int step = travel >= 0 ? 1 : -1;
        for (int i = std::clamp(first() - 2 * step, 0, count - 1); i >= 0 && i < count; i += step) {
            float off = offsetFor(i), gap = std::abs(distanceTo(i, off) - travel);
            if (gap < bestGap) best = i, bestGap = gap;
            if (step * (distanceTo(i, off) - travel) > inner) break;  // well past where it would stop
        }
        return {best, offsetFor(best)};
    }

    // How far to scroll (dp towards the end) for item `index` to be first, `offset` into it:
    // exact if it was laid out (*exact), else from the sizes known so far.
    float distanceTo(int index, float offset, bool* exact = nullptr) const {
        if (exact) *exact = index >= laidStart && index < laidStart + int(laidPos.size());
        if (index >= laidStart && index < laidStart + int(laidPos.size())) return laidPos[size_t(index - laidStart)] - laidBefore + offset;
        int from = first();
        float d = offset - firstOffset;
        for (int i = from; i < index; ++i) d += sizeOf(i) + spacing;
        for (int i = index; i < from; ++i) d -= sizeOf(i) + spacing;
        return d;
    }

    // Moves the scroll position; returns the part of `delta` that was used.
    float move(float delta) {
        if (count == 0 || delta == 0) return 0;
        int index = first();
        float offset = firstOffset + delta, used = delta;
        // A hair short of the next item counts as there (float error: 109.99998 of 110 would keep
        // the item before it first).
        constexpr float kHair = 0.01f;
        while (index < count - 1 && offset >= sizeOf(index) + spacing - kHair) offset -= sizeOf(index++) + spacing;
        if (offset < 0 && offset > -kHair) offset = 0;
        while (offset < 0 && index > 0) offset += sizeOf(--index) + spacing;
        if (offset < 0) {  // reached the start
            used -= offset;
            offset = 0;
        }
        setFirst(index, offset);
        return used;
    }
};

LazyListState::LazyListState() : data_(std::make_shared<Data>()) {}

int LazyListState::firstVisibleItemIndex() const { return data_->firstIndex.get(); }
float LazyListState::firstVisibleItemScrollOffset() const { return data_->firstOffset; }
int LazyListState::totalItemsCount() const { return data_->count; }
bool LazyListState::canScrollBackward() const { return data_->first() > 0 || data_->firstOffset > 0; }
bool LazyListState::canScrollForward() const { return !data_->atEnd; }

void LazyListState::scrollToItem(int index, float offset) const {
    Data& d = *data_;
    // Before the first layout the count isn't known yet: the content clamps the index then.
    d.setFirst(d.count > 0 ? std::clamp(index, 0, d.count - 1) : std::max(0, index), std::max(0.f, offset));
    d.atEnd = false;
    d.requestWindowIfNeeded();
    detail::notifyStateChanged(&d);
}

float LazyListState::scrollBy(float delta) const {
    Data& d = *data_;
    if (delta > 0 && d.atEnd) return 0;
    float used = d.move(delta);
    if (delta < 0) d.atEnd = false;
    d.requestWindowIfNeeded();
    if (used != 0) detail::notifyStateChanged(&d);
    return used;
}

void LazyListState::stopScroll() const { ++data_->generation; }
const MutableInteractionSource& LazyListState::interactionSource() const { return data_->interactions; }
LazyListLayoutInfo LazyListState::layoutInfo() const { return data_->layoutInfo.get(); }

bool LazyListState::isScrollInProgress() const { return data_->userScrolling.get() || data_->animations.get() > 0; }

namespace {
// While it lives an animated scroll of `d` runs (isScrollInProgress), until done or stopped.
struct ScrollAnimating {
    std::shared_ptr<LazyListState::Data> d;
    explicit ScrollAnimating(std::shared_ptr<LazyListState::Data> data) : d(std::move(data)) { d->animations.set(d->animations.peek() + 1); }
    ~ScrollAnimating() { d->animations.set(d->animations.peek() - 1); }
};

Task animateToItem(LazyListState state, int index, float offset, uint64_t generation) {
    ScrollAnimating running{state.data_};
    LazyListState::Data& d = *state.data_;  // `state` keeps it alive
    auto interrupted = [&d, generation] { return d.generation != generation; };
    auto scroll = [state](float delta) { return state.scrollBy(delta); };
    co_await awaitFrame();  // laid out with what is there now
    if (interrupted()) co_return;
    if (d.count == 0) co_return state.scrollToItem(index, offset);
    index = std::clamp(index, 0, d.count - 1);
    // Far away: first jump to a view's length short of it (Compose teleports too).
    float view = d.viewport > 0 ? d.viewport : 1000.f;
    if (float distance = d.distanceTo(index, offset); std::abs(distance) > 3 * view) {
        int near = index;
        float covered = 0;
        if (distance > 0)
            while (near > 0 && covered < view) covered += d.sizeOf(--near) + d.spacing;
        else
            while (near < d.count - 1 && covered < view) covered += d.sizeOf(near++) + d.spacing;
        state.scrollToItem(near, 0);
        co_await awaitFrame();
        if (interrupted()) co_return;
    }
    // Each frame it heads for where the item is now thought to be: better known as items get
    // measured, exact once it is laid out.
    auto remaining = [&d, index, offset] { return d.distanceTo(index, offset); };
    for (int pass = 0; pass < 12; ++pass) {  // again if it ended before the item was laid out
        float distance = d.distanceTo(index, offset);
        if (std::abs(distance) < 0.5f) break;
        bool stopped = false;
        co_await detail::animateScroll(scroll, interrupted, distance, spring(), remaining, &stopped);
        if (stopped) break;  // the end of the list (or someone else scrolls it)
        co_await awaitFrame();  // laid out where it got to
        if (interrupted()) break;
    }
}
} // namespace

Task LazyListState::animateScrollToItem(int index, float offset) const {
    stopScroll();
    return animateToItem(*this, index, offset, data_->generation);
}
Task LazyListState::animateScrollBy(float delta) const { return animateScrollBy(delta, spring()); }
namespace {
Task animateListBy(LazyListState state, float delta, AnimationSpec spec) {
    auto d = state.data_;
    ScrollAnimating running{d};
    co_await detail::animateScroll([state](float x) { return state.scrollBy(x); },
                                   [d, generation = d->generation] { return d->generation != generation; }, delta, spec);
}
} // namespace

Task LazyListState::animateScrollBy(float delta, const AnimationSpec& spec) const {
    stopScroll();
    return animateListBy(*this, delta, spec);
}

// Saveable: a list on the back stack comes back where it was.
LazyListState rememberLazyListState(CallSite site) {
    return rememberSaveable([] { return LazyListState(); }, site);
}

LazyListState rememberLazyListState(int initialFirstVisibleItemIndex, float initialFirstVisibleItemScrollOffset, CallSite site) {
    return rememberSaveable(
        [&] {
            LazyListState s;
            s.scrollToItem(initialFirstVisibleItemIndex, initialFirstVisibleItemScrollOffset);
            return s;
        },
        site);
}

// ---- LazyListScope ----------------------------------------------------------------------

size_t LazyListScope::defaultKey(int index) { return detail::mixKey(0xdefa017ULL, uint64_t(index)); }

void LazyListScope::add(int count, std::function<size_t(int)> key, std::function<void(int)> content, bool sticky,
                        std::function<size_t(int)> contentType) {
    if (count > 0) intervals_.push_back({size(), count, std::move(key), std::move(content), sticky, std::move(contentType)});
}

void LazyListScope::stickyHeader(const Content& content) {
    add(1, [k = defaultKey(size())](int) { return k; }, [content](int) { content(); }, true);
}

std::vector<int> LazyListScope::stickyIndices() const {
    std::vector<int> out;
    for (const Interval& iv : intervals_)
        if (iv.sticky)
            for (int i = 0; i < iv.count; ++i) out.push_back(iv.start + i);
    return out;
}

void LazyListScope::item(const Content& content) {
    add(1, [k = defaultKey(size())](int) { return k; }, [content](int) { content(); });
}

void LazyListScope::items(int count, const std::function<void(int)>& content) {
    int base = size();
    add(count, [base](int i) { return defaultKey(base + i); }, content);
}

void LazyListScope::items(int count, const std::function<size_t(int)>& key, const std::function<void(int)>& content) {
    add(count, [key](int i) { return explicitKey(key(i)); }, content);
}

void LazyListScope::items(int count, const std::function<size_t(int)>& key, const std::function<size_t(int)>& contentType,
                          const std::function<void(int)>& content) {
    add(count, [key](int i) { return explicitKey(key(i)); }, content, false, contentType);
}

size_t LazyListScope::contentTypeAt(int index) const {
    auto it = std::upper_bound(intervals_.begin(), intervals_.end(), index,
                               [](int i, const Interval& iv) { return i < iv.start; });
    const Interval& iv = *std::prev(it);
    return iv.contentType ? iv.contentType(index - iv.start) : 0;
}

int LazyListScope::size() const { return intervals_.empty() ? 0 : intervals_.back().start + intervals_.back().count; }

size_t LazyListScope::keyAt(int index) const {
    auto it = std::upper_bound(intervals_.begin(), intervals_.end(), index,
                               [](int i, const Interval& iv) { return i < iv.start; });
    const Interval& iv = *std::prev(it);
    return iv.key(index - iv.start);
}

void LazyListScope::composeAt(int index) const {
    auto it = std::upper_bound(intervals_.begin(), intervals_.end(), index,
                               [](int i, const Interval& iv) { return i < iv.start; });
    const Interval& iv = *std::prev(it);
    iv.content(index - iv.start);
}

// ---- Layout -----------------------------------------------------------------------------

namespace {

// Makes the list scrollable by drag, fling and wheel, and clips the items to its bounds.
struct LazyListElement final : ScrollableElement {
    LazyListState state;
    bool vertical = true;
    bool reverse = false;  // reverseLayout: the screen's direction is the other way round
    std::optional<FlingBehavior> fling;
    std::optional<CoroutineScope> coroutines;
    // A snapping list settles itself: on the item nearest to where the fling would stop.
    bool settle(float velocity) const override {
        if (!fling || !fling->snap || !coroutines) return false;
        float v = reverse ? -velocity : velocity;
        auto [index, offset] = state.data_->snapTarget(fling->decay.calculateTargetValue(0, v), fling->snapPosition);
        coroutines->launch([state = state, index, offset]() -> Task { co_await state.animateScrollToItem(index, offset); });
        return true;
    }
    Orientation orientation() const override { return vertical ? Orientation::Vertical : Orientation::Horizontal; }
    std::optional<MutableInteractionSource> interactionSource() const override { return state.interactionSource(); }
    float scrollBy(float delta) const override {
        state.stopScroll();  // the user takes over
        return reverse ? -state.scrollBy(-delta) : state.scrollBy(delta);
    }
    void scrollInProgress(bool on) const override {
        if (state.data_->userScrolling.peek() != on) state.data_->userScrolling.set(on);
    }
    void draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode&, const std::function<void()>& drawContent) const override {
        canvas.pushClip(clipBounds(bounds));
        drawContent();
        canvas.popClip();
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const LazyListElement*>(&other);
        return o && o->state == state && o->vertical == vertical && o->reverse == reverse && o->userScrollEnabled == userScrollEnabled &&
               o->overscroll == overscroll && o->overscrollEffect == overscrollEffect;
    }
};

// Marks an item for animateItem(); the list's layout animates its placement and alpha.
struct AnimateItemElement final : ModifierElement {
    AnimationSpec placementSpec = spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow);
    AnimationSpec fadeInSpec = tween(220);
    AnimationSpec fadeOutSpec = tween(220);
    mutable float alpha = 1;       // written by the list's layout, before drawing
    mutable bool leaving = false;  // its data is gone: it fades out, untouchable
    bool ignoresInput() const override { return leaving; }
    void draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode&, const std::function<void()>& drawContent) const override {
        if (alpha >= 1) return drawContent();
        if (alpha <= 0 || canvas.quickReject(bounds)) return;
        canvas.pushLayer(bounds, 0, alpha);
        drawContent();
        canvas.popLayer();
    }
};

// (`carrier`: the node it is on, drawn again when its alpha changes.)
const AnimateItemElement* animatedItem(LayoutNode& item, LayoutNode** carrier) {
    for (auto* content : item.children)  // the item's own nodes, inside its container
        if (auto* e = content->modifier.find<AnimateItemElement>()) {
            *carrier = content;
            return e;
        }
    return nullptr;
}

// Every item gets one container node (laid out by detail::stackPolicy), so the list maps items
// to children one to one.

MeasurePolicy listPolicy(LazyListState state, bool vertical, LazyListOptions options) {
    return [state, vertical, options](LayoutNode& node, const Constraints& c) {
        LazyListState::Data& d = *state.data_;
        detail::recordRead(&d);  // scrolled (scrollBy, scrollToItem): measured again
        const PaddingValues& p = options.contentPadding;
        bool reverse = options.reverseLayout;  // laid out from the start edge as usual, then mirrored
        float before = vertical ? p.top : p.start, after = vertical ? p.bottom : p.end;
        if (reverse) std::swap(before, after);
        float crossBefore = vertical ? p.start : p.top, crossAfter = vertical ? p.end : p.bottom;
        float mainMax = vertical ? c.maxHeight : c.maxWidth;
        float crossMax = vertical ? c.maxWidth : c.maxHeight;
        float crossAvailable = crossMax == kInfinity ? kInfinity : std::max(0.f, crossMax - crossBefore - crossAfter);
        auto mainOf = [&](Size s) { return vertical ? s.height : s.width; };
        auto crossOf = [&](Size s) { return vertical ? s.width : s.height; };

        // Child j shows item composed[j]: the items start..start+n in order, maybe a pinned
        // header after them. Positions below are per child.
        int start = d.composedStart;
        size_t children = std::min(node.children.size(), d.composed.size());
        bool hasPinned = d.pinned >= 0 && children > 0 && d.composed[children - 1] == d.pinned;
        int n = int(children) - (hasPinned ? 1 : 0);  // the in-order ones (the range without the pinned header)
        std::vector<float> mains(children), crosses(children);
        float crossSize = 0;
        // fillParentMaxWidth / Height in the items: of the list (its viewport, past the padding).
        auto finite = [](float v) { return v == kInfinity ? NAN : v; };
        float mainAvailable = finite(mainMax - before - after);
        detail::LazyParentScope parent(vertical ? Size{finite(crossAvailable), mainAvailable} : Size{mainAvailable, finite(crossAvailable)});
        for (size_t j = 0; j < children; ++j) {
            Constraints cc = vertical ? Constraints{0, crossAvailable, 0, kInfinity}
                                      : Constraints{0, kInfinity, 0, crossAvailable};
            Size s = node.children[j]->measure(cc);
            mains[j] = mainOf(s);
            crosses[j] = crossOf(s);
            crossSize = std::max(crossSize, crosses[j]);
            d.recordSize(d.keys[size_t(d.composed[j])], mains[j]);
        }
        // A header pinned at the top that also falls in the composed range is the last child,
        // out of order: its place in the range is laid out from its size.
        int pinnedChild = hasPinned ? int(children) - 1 : -1;
        int pinnedIndex = hasPinned ? d.pinned : -1;
        bool pinnedInRange = pinnedIndex >= start && pinnedIndex < d.composedEnd;
        int range = n + (pinnedInRange ? 1 : 0);  // items start..start+range, back to back
        auto childAt = [&](int k) {  // the child showing item start+k
            if (pinnedInRange && start + k == pinnedIndex) return pinnedChild;
            return pinnedInRange && start + k > pinnedIndex ? k - 1 : k;
        };
        d.spacing = options.spacing;
        float content = before + after;
        for (int k = 0; k < range; ++k) content += mains[size_t(childAt(k))] + options.spacing;
        float viewport = mainMax == kInfinity ? content : mainMax;  // unbounded: no virtualization
        d.viewport = viewport;

        // Positions from the first visible item; then don't leave a gap after the last one.
        std::vector<float> pos(static_cast<size_t>(range));  // per item start+k
        auto mainAt = [&](int k) { return mains[size_t(childAt(k))]; };
        auto layoutFrom = [&] {
            int first = std::clamp(d.first(), start, std::max(start, start + range - 1)) - start;
            if (range == 0) return;
            pos[size_t(first)] = before - d.firstOffset;
            for (int k = first + 1; k < range; ++k) pos[size_t(k)] = pos[size_t(k - 1)] + mainAt(k - 1) + options.spacing;
            for (int k = first - 1; k >= 0; --k) pos[size_t(k)] = pos[size_t(k + 1)] - options.spacing - mainAt(k);
        };
        layoutFrom();
        d.atEnd = true;
        if (range > 0 && start + range == d.count) {
            float lastEnd = pos[size_t(range - 1)] + mainAt(range - 1) + after;
            if (lastEnd <= viewport + 0.5f) {  // short of the end, or within half a dp of it
                if (lastEnd != viewport) {
                    d.move(lastEnd - viewport);  // so the end of the content touches the end of the view
                    layoutFrom();
                }
            } else {
                d.atEnd = false;
            }
        } else if (range > 0 || d.count > 0) {
            d.atEnd = false;
        }
        // Arranged (Compose's verticalArrangement / horizontalArrangement): items that all fit sit
        // as it says, on the screen — at the end, in the middle, spread out (a reversed list's
        // Top is its far end).
        if (options.arrangement && range > 0 && start == 0 && range == d.count) {
            float lastEnd = pos[size_t(range - 1)] + mainAt(range - 1) + after;
            if (lastEnd < viewport - 0.5f) {  // (all in view: not scrolled)
                Arrangement arrangement = *options.arrangement;
                using Kind = Arrangement::Kind;
                if (reverse && (arrangement.kind == Kind::Start || arrangement.kind == Kind::End))
                    arrangement.kind = arrangement.kind == Kind::Start ? Kind::End : Kind::Start;
                std::vector<float> sizes, at(static_cast<size_t>(range), 0.f);
                for (int k = 0; k < range; ++k) sizes.push_back(mainAt(k));
                arrangement.arrange(viewport - before - after, sizes, at);
                for (int k = 0; k < range; ++k) pos[size_t(k)] = before + at[size_t(k)];
            }
        }
        if (!detail::isProbeMeasure()) {
            d.laidStart = start;
            d.laidPos = pos;
            d.laidBefore = before;
            d.laidAfter = after;
        }

        // Across the scroll axis the list wraps its widest item, like Compose (fillMaxWidth()
        // or a fixed height make it larger through the constraints).
        Size own = c.constrain(vertical ? Size{crossSize + crossBefore + crossAfter, viewport}
                                        : Size{viewport, crossSize + crossBefore + crossAfter});
        float space = std::max(0.f, (vertical ? own.width : own.height) - crossBefore - crossAfter);
        float extent = vertical ? own.height : own.width;
        auto onScreen = [&](float main, float size) { return reverse ? extent - main - size : main; };
        int64_t now = node.host()->frameTime();
        std::unordered_set<size_t> animated;
        auto placeChild = [&](int j, float main) {
            main = onScreen(main, mains[size_t(j)]);
            float cross = crossBefore + (space - crosses[size_t(j)]) / 2 * (1 + options.crossAxisBias);
            if (vertical)
                node.children[size_t(j)]->place(cross, main);
            else
                node.children[size_t(j)]->place(main, cross);
        };
        for (int k = 0; k < range; ++k) {
            int j = childAt(k);
            float main = pos[size_t(k)];
            LayoutNode* carrier = nullptr;
            if (auto* e = animatedItem(*node.children[size_t(j)], &carrier); e && !detail::isProbeMeasure()) {
                // Indices only change when the data does (scrolling keeps them): then glide from
                // where the item was shown. A key that wasn't in the data before fades in.
                d.animatesItems = true;
                size_t key = d.keys[size_t(start + k)];
                animated.insert(key);
                auto& a = d.itemAnimations[key];
                int index = start + k;
                if (a.index >= 0 && a.index != index) {
                    a.from = a.shown - main;
                    a.moveStart = now;
                }
                if (a.index < 0 && !d.previousKeys.empty() && !d.previousKeys.count(key)) a.appearStart = now;
                float offset = 0;
                bool finished = true;
                if (a.moveStart >= 0) {
                    float p = e->placementSpec.progress(float(now - a.moveStart) / 1e6f, finished);
                    offset = a.from * (1 - p);
                    if (finished) a.moveStart = -1;
                }
                float alpha = 1;
                if (a.appearStart >= 0) {
                    bool done = true;
                    alpha = std::clamp(e->fadeInSpec.progress(float(now - a.appearStart) / 1e6f, done), 0.f, 1.f);
                    if (done) a.appearStart = -1;
                    finished = finished && done;
                }
                if (alpha != e->alpha) e->alpha = alpha, carrier->invalidateDraw();  // (drawn faded)
                if (!finished) node.host()->requestLayoutFrame();
                a.index = index;
                main += offset;
                a.shown = main;
            }
            placeChild(j, main);
        }
        // The section's header stays at the top edge until the next header pushes it up.
        float pinnedTop = 0;
        if (pinnedChild >= 0) {
            float top = pinnedInRange ? std::max(0.f, pos[size_t(pinnedIndex - start)]) : 0.f;
            auto next = std::upper_bound(d.sticky.begin(), d.sticky.end(), pinnedIndex);
            if (next != d.sticky.end() && *next >= start && *next < start + range)
                top = std::min(top, pos[size_t(*next - start)] - mains[size_t(pinnedChild)]);
            placeChild(pinnedChild, pinnedTop = top);
        }
        if (!detail::isProbeMeasure()) {  // what is in view, for layoutInfo()
            LazyListLayoutInfo info;
            info.totalItemsCount = d.count;
            info.viewportStartOffset = -before;
            info.viewportEndOffset = viewport - before;
            info.viewportSize = own;
            info.orientation = vertical ? Orientation::Vertical : Orientation::Horizontal;
            info.reverseLayout = reverse;
            info.beforeContentPadding = before;
            info.afterContentPadding = after;
            info.mainAxisItemSpacing = options.spacing;
            auto add = [&](int index, float main, float size) {
                float offset = main - before;
                if (offset + size > info.viewportStartOffset && offset < info.viewportEndOffset)
                    info.visibleItemsInfo.push_back({index, d.keys[size_t(index)], offset, size,
                                                     size_t(index) < d.contentTypes.size() ? d.contentTypes[size_t(index)] : 0});
            };
            if (pinnedChild >= 0) add(pinnedIndex, pinnedTop, mains[size_t(pinnedChild)]);
            for (int k = 0; k < range; ++k)
                if (start + k != pinnedIndex) add(start + k, pos[size_t(k)], mainAt(k));
            if (!(info == d.layoutInfo.peek())) d.layoutInfo.set(std::move(info));
        }
        // Removed items fade out where they were; the composition lets one go once it is done.
        for (size_t l = 0; l < d.leaving.size() && children + l < node.children.size(); ++l) {
            LayoutNode& child = *node.children[children + l];
            Constraints cc = vertical ? Constraints{0, crossAvailable, 0, kInfinity} : Constraints{0, kInfinity, 0, crossAvailable};
            Size s = child.measure(cc);
            float cross = crossBefore + (space - crossOf(s)) / 2 * (1 + options.crossAxisBias);
            LazyListState::Data::Leaving& gone = d.leaving[l];
            float main = onScreen(gone.shown, mainOf(s));
            if (vertical) child.place(cross, main);
            else child.place(main, cross);
            LayoutNode* carrier = nullptr;
            auto* e = animatedItem(child, &carrier);
            bool done = true;
            float fade = e ? e->fadeOutSpec.progress(float(now - gone.start) / 1e6f, done) : 1.f;
            if (e) {
                float alpha = std::clamp(1 - fade, 0.f, 1.f);
                if (alpha != e->alpha) e->alpha = alpha, carrier->invalidateDraw();  // (drawn faded)
                e->leaving = true;
            }
            if (detail::isProbeMeasure()) continue;
            if (!done) node.host()->requestLayoutFrame();
            else if (!gone.done) gone.done = true, d.window.set(d.window.peek() + 1);
        }
        if (!detail::isProbeMeasure() && d.headerFor(d.first()) != d.pinned) d.window.set(d.window.peek() + 1);
        if (d.animatesItems && !detail::isProbeMeasure())  // forget items that left the view
            std::erase_if(d.itemAnimations, [&](const auto& entry) { return !animated.count(entry.first); });
        d.requestWindowIfNeeded();
        return own;
    };
}

} // namespace

Modifier Modifier::animateItem() const { return then(std::make_shared<AnimateItemElement>()); }
Modifier Modifier::animateItem(const AnimationSpec& placementSpec) const {
    auto e = std::make_shared<AnimateItemElement>();
    e->placementSpec = placementSpec;
    return then(std::move(e));
}
Modifier Modifier::animateItem(const AnimationSpec& fadeInSpec, const AnimationSpec& placementSpec,
                               const AnimationSpec& fadeOutSpec) const {
    auto e = std::make_shared<AnimateItemElement>();
    e->fadeInSpec = fadeInSpec;
    e->placementSpec = placementSpec;
    e->fadeOutSpec = fadeOutSpec;
    return then(std::move(e));
}

namespace {

void lazyList(bool vertical, const Modifier& modifier, const LazyListOptions& given, const LazyListContent& content,
              const CallSite& site) {
    ComposableScope scope(site);
    LazyListOptions options = given;
    if (options.arrangement) options.spacing = options.arrangement->spacing;
    LazyListState state = options.state ? *options.state : rememberLazyListState();
    LazyListState::Data& d = *state.data_;

    LazyListScope list;
    content(list);
    if (d.animatesItems) d.previousKeys = std::unordered_set<size_t>(d.keys.begin(), d.keys.end());
    d.count = list.size();
    d.keys.resize(size_t(d.count));
    d.contentTypes.resize(size_t(d.count));
    for (int i = 0; i < d.count; ++i) d.keys[size_t(i)] = list.keyAt(i), d.contentTypes[size_t(i)] = list.contentTypeAt(i);
    auto& composer = detail::Composer::require();
    if (d.animatesItems) {
        // Animated items shown last time whose data is gone start fading out; one that is back
        // (or done) is no longer leaving.
        std::unordered_set<size_t> now(d.keys.begin(), d.keys.end());
        std::erase_if(d.leaving, [&](const LazyListState::Data::Leaving& l) { return l.done || now.count(l.key); });
        for (const auto& [key, a] : d.itemAnimations)
            if (a.index >= 0 && !now.count(key) &&
                std::none_of(d.leaving.begin(), d.leaving.end(), [&](const LazyListState::Data::Leaving& l) { return l.key == key; }))
                d.leaving.push_back({key, composer.frameTimeNanos(), a.shown});
    }
    if (d.count > 0 && d.firstIndex.peek() >= d.count) d.setFirst(d.count - 1, 0);  // the list shrank

    d.sticky = list.stickyIndices();
    d.window.get();  // recompose when scrolling needs items that aren't composed (or another header)
    auto [start, end] = d.composeRange();
    d.composedStart = start;
    d.composedEnd = end;
    d.pinned = d.headerFor(d.first());
    d.composed.clear();
    for (int i = start; i < end; ++i)
        if (i != d.pinned) d.composed.push_back(i);
    if (d.pinned >= 0) d.composed.push_back(d.pinned);  // last: drawn over the items, first to be hit

    auto element = std::make_shared<LazyListElement>();
    if (options.overscrollEffect)
        element->overscroll = false, element->overscrollEffect = *options.overscrollEffect;
    else
        element->overscroll = detail::overscrollWanted();
    element->bringIntoViewSpec = detail::currentBringIntoViewSpec();
    element->state = state;
    element->vertical = vertical;
    element->reverse = options.reverseLayout;
    element->userScrollEnabled = options.userScrollEnabled;
    element->fling = options.flingBehavior;
    if (options.flingBehavior && options.flingBehavior->snap) element->coroutines = rememberCoroutineScope();
    // Items scrolled away keep their rememberSaveable state (and the scroll position of a list
    // inside them) for when they come back, by key.
    SaveableStateHolder saveable = rememberSaveableStateHolder();
    // One container node per item (by key), its content in a group of its own: composed from
    // the data, or kept as it was for an item fading out after its data went.
    const uint64_t contentGroup = detail::keyOf(CallSite::current());
    auto item = [&](size_t itemKey, int index) {
        key(itemKey, [&] {
            composer.emit(
                detail::keyOf(CallSite::current()),
                [](LayoutNode& node) {
                    node.modifier = {};
                    node.measurePolicy = detail::stackPolicy;
                    node.drawContent = {};
                },
                [&] {
                    if (index < 0) return composer.keepGroup(contentGroup);
                    composer.startGroup(contentGroup);
                    saveable.SaveableStateProvider(std::to_string(itemKey), [&] { list.composeAt(index); });
                    composer.endGroup();
                });
        });
    };
    composer.emit(
        detail::keyOf(CallSite::current()),
        [&](LayoutNode& node) {
            node.modifier = modifier.then(element);
            node.measurePolicy = listPolicy(state, vertical, options);
            node.drawContent = {};
        },
        [&] {
            for (int i : d.composed) item(d.keys[size_t(i)], i);
            for (const LazyListState::Data::Leaving& l : d.leaving) item(l.key, -1);  // after the others (and a pinned header)
        });
}

} // namespace

void LazyColumn(const Modifier& modifier, const LazyListContent& content, CallSite site) {
    lazyList(true, modifier, LazyListOptions{}, content, site);
}
void LazyColumn(const Modifier& modifier, const LazyListOptions& options, const LazyListContent& content, CallSite site) {
    lazyList(true, modifier, options, content, site);
}
void LazyRow(const Modifier& modifier, const LazyListContent& content, CallSite site) {
    lazyList(false, modifier, LazyListOptions{}, content, site);
}
void LazyRow(const Modifier& modifier, const LazyListOptions& options, const LazyListContent& content, CallSite site) {
    lazyList(false, modifier, options, content, site);
}

} // namespace kompoz

namespace kompoz {

int GridCells::count(float width, float spacing) const {
    if (kind == Kind::Fixed) return std::max(1, int(value));
    if (width <= 0 || value <= 0) return 1;
    return std::max(1, int((width + spacing) / (value + spacing)));
}

// The grid's lines underneath, and what maps items to them (from the last composition and
// layout).
struct LazyGridState::Data {
    LazyListState lines;
    std::vector<int> lineStart;  // first item of each line, then the item count
    int columns = 1;
    bool vertical = true;
    std::optional<std::pair<int, float>> pending;  // a scroll to an item before its line was known
    struct Cell {
        size_t key = 0;
        int column = 0;
        float across = 0, acrossSize = 0, alongSize = 0;
    };
    std::unordered_map<int, Cell> cells;  // laid-out items, by index

    int lineOf(int item) const {
        if (lineStart.size() < 2) return 0;
        auto it = std::upper_bound(lineStart.begin(), lineStart.end() - 1, std::max(0, item));
        return std::max(0, int(it - lineStart.begin()) - 1);
    }
};

LazyGridState::LazyGridState() : data_(std::make_shared<Data>()) {}

int LazyGridState::firstVisibleItemIndex() const {
    int line = data_->lines.firstVisibleItemIndex();
    if (data_->pending) return data_->pending->first;
    return size_t(line) + 1 < data_->lineStart.size() ? data_->lineStart[size_t(line)] : 0;
}
float LazyGridState::firstVisibleItemScrollOffset() const {
    return data_->pending ? data_->pending->second : data_->lines.firstVisibleItemScrollOffset();
}
bool LazyGridState::canScrollBackward() const { return data_->lines.canScrollBackward(); }
bool LazyGridState::canScrollForward() const { return data_->lines.canScrollForward(); }
void LazyGridState::scrollToItem(int index, float offset) const {
    if (data_->lineStart.size() < 2) {  // not laid out yet: when it is
        data_->pending = std::pair{index, offset};
        return;
    }
    data_->pending.reset();
    data_->lines.scrollToItem(data_->lineOf(index), offset);
}
float LazyGridState::scrollBy(float delta) const { return data_->lines.scrollBy(delta); }
Task LazyGridState::animateScrollToItem(int index, float offset) const {
    return data_->lines.animateScrollToItem(data_->lineOf(index), offset);
}
Task LazyGridState::animateScrollBy(float delta) const { return data_->lines.animateScrollBy(delta); }
void LazyGridState::stopScroll() const { data_->lines.stopScroll(); }
const MutableInteractionSource& LazyGridState::interactionSource() const { return data_->lines.interactionSource(); }
const LazyListState& LazyGridState::lines() const { return data_->lines; }

LazyGridLayoutInfo LazyGridState::layoutInfo() const {
    const Data& d = *data_;
    LazyListLayoutInfo lines = d.lines.layoutInfo();
    LazyGridLayoutInfo out;
    out.viewportStartOffset = lines.viewportStartOffset;
    out.viewportEndOffset = lines.viewportEndOffset;
    out.totalItemsCount = d.lineStart.empty() ? 0 : d.lineStart.back();
    out.viewportSize = lines.viewportSize;
    out.orientation = lines.orientation;
    out.beforeContentPadding = lines.beforeContentPadding;
    out.afterContentPadding = lines.afterContentPadding;
    out.mainAxisItemSpacing = lines.mainAxisItemSpacing;
    out.maxSpan = d.columns;
    for (const LazyListItemInfo& line : lines.visibleItemsInfo) {
        if (size_t(line.index) + 1 >= d.lineStart.size()) continue;
        for (int i = d.lineStart[size_t(line.index)]; i < d.lineStart[size_t(line.index) + 1]; ++i) {
            auto cell = d.cells.find(i);
            if (cell == d.cells.end()) continue;
            const Data::Cell& c = cell->second;
            LazyGridItemInfo info{i, c.key};
            if (d.vertical) {
                info.row = line.index, info.column = c.column;
                info.offset = {c.across, line.offset};
                info.size = {c.acrossSize, c.alongSize};
            } else {
                info.row = c.column, info.column = line.index;
                info.offset = {line.offset, c.across};
                info.size = {c.alongSize, c.acrossSize};
            }
            out.visibleItemsInfo.push_back(info);
        }
    }
    return out;
}

LazyGridState rememberLazyGridState(int initialFirstVisibleItemIndex, float initialFirstVisibleItemScrollOffset, CallSite site) {
    return rememberSaveable(
        [&] {
            LazyGridState s;
            if (initialFirstVisibleItemIndex != 0 || initialFirstVisibleItemScrollOffset != 0)
                s.scrollToItem(initialFirstVisibleItemIndex, initialFirstVisibleItemScrollOffset);
            return s;
        },
        site);
}

int LazyGridScope::spanAt(int index, int columns) const {
    for (const Span& s : spans_)
        if (index >= s.start && index < s.start + s.count) return std::clamp(s.span, 1, columns);
    return 1;
}

namespace {

// One grid line: a row of cells of equal width (a column of cells of equal height in a
// horizontal grid), plus spacing for spans, aligned to its start.
void gridRow(const LazyGridScope& grid, int from, int to, int columns, float spacing, MutableState<float> width,
             const std::shared_ptr<LazyGridState::Data>& state, bool vertical = true) {
    std::vector<std::pair<int, int>> cells;  // column, span
    for (int i = from, column = 0; i < to; ++i) {
        int span = grid.spanAt(i, columns);
        cells.push_back({column, span});
        column += span;
    }
    detail::Composer::require().emit(
        detail::keyOf(CallSite::current()),
        [&](LayoutNode& node) {
            node.modifier = vertical ? Modifier().fillMaxWidth() : Modifier().fillMaxHeight();
            std::vector<size_t> keys;
            for (int i = from; i < to; ++i) keys.push_back(grid.keyAt(i));
            node.measurePolicy = [cells, columns, spacing, width, vertical, state, from, keys](LayoutNode& n, const Constraints& c) {
                float line = vertical ? (c.hasBoundedWidth() ? c.maxWidth : 0) : (c.hasBoundedHeight() ? c.maxHeight : 0);
                if (!detail::isProbeMeasure()) width.set(line);  // Adaptive cells follow the size (same frame)
                float cell = std::max(0.f, (line - spacing * float(columns - 1)) / float(columns)), across = 0;
                for (size_t k = 0; k < n.children.size() && k < cells.size(); ++k) {
                    auto [column, span] = cells[k];
                    float cw = cell * float(span) + spacing * float(span - 1);
                    Size s = n.children[k]->measure(vertical ? Constraints{cw, cw, 0, kInfinity} : Constraints{0, kInfinity, cw, cw});
                    float at = float(column) * (cell + spacing);
                    n.children[k]->place(vertical ? at : 0, vertical ? 0 : at);
                    across = std::max(across, vertical ? s.height : s.width);
                    if (!detail::isProbeMeasure())  // for the state's layoutInfo
                        state->cells[from + int(k)] = {keys[k], column, at, cw, vertical ? s.height : s.width};
                }
                return c.constrain(vertical ? Size{line, across} : Size{across, line});
            };
            node.drawContent = {};
        },
        [&] {
            for (int i = from; i < to; ++i)
                key(grid.keyAt(i), [&] {
                    // One node per cell, whatever the item emits.
                    detail::Composer::require().emit(
                        detail::keyOf(CallSite::current()),
                        [](LayoutNode& cellNode) {
                            cellNode.modifier = {};
                            cellNode.measurePolicy = {};  // stacks its content
                            cellNode.drawContent = {};
                        },
                        [&] { grid.composeAt(i); });
                });
        });
}

} // namespace

namespace {

// Lines of cells (rows of a vertical grid, columns of a horizontal one) in a lazy list.
void lazyGrid(GridCells cells, const Modifier& modifier, const LazyGridOptions& options,
              const std::function<void(LazyGridScope&)>& content, bool vertical) {
    auto size = remember([] { return mutableStateOf(0.f); });  // across the lines: the grid's width (height)
    int count = cells.count(size.get(), vertical ? options.horizontalSpacing : options.verticalSpacing);
    LazyGridScope grid;
    content(grid);
    std::vector<int> lineStart;  // first item of each line, then the item count
    for (int i = 0, at = 0; i < grid.size(); ++i) {
        int span = grid.spanAt(i, count);
        if (i == 0 || at + span > count) {
            lineStart.push_back(i);
            at = 0;
        }
        at += span;
    }
    lineStart.push_back(grid.size());
    int lines = int(lineStart.size()) - 1;
    float between = vertical ? options.verticalSpacing : options.horizontalSpacing;
    float within = vertical ? options.horizontalSpacing : options.verticalSpacing;
    // The state maps items to the lines underneath; a scroll to an item made before goes now.
    auto own = remember([] { return LazyGridState(); });
    LazyGridState state = options.state ? *options.state : own;
    auto d = state.data_;
    d->lineStart = lineStart;
    d->columns = count;
    d->vertical = vertical;
    if (d->pending && lines > 0) state.scrollToItem(d->pending->first, d->pending->second);
    LazyListOptions list{d->lines, options.contentPadding, between, -1};
    list.flingBehavior = options.flingBehavior;  // a snapping grid snaps its lines
    list.userScrollEnabled = options.userScrollEnabled;
    list.overscrollEffect = options.overscrollEffect;
    auto items = [&](LazyListScope& scope) {
        scope.items(
            lines, std::function<size_t(int)>([&](int r) { return grid.keyAt(lineStart[size_t(r)]); }),
            std::function<void(int)>([&](int r) {
                gridRow(grid, lineStart[size_t(r)], lineStart[size_t(r) + 1], count, within, size, d, vertical);
            }));
    };
    if (vertical)
        LazyColumn(modifier, list, items);
    else
        LazyRow(modifier, list, items);
}

} // namespace

void LazyVerticalGrid(GridCells columns, const Modifier& modifier, const LazyGridOptions& options,
                      const std::function<void(LazyGridScope&)>& content, CallSite site) {
    ComposableScope scope(site);
    lazyGrid(columns, modifier, options, content, true);
}

void LazyHorizontalGrid(GridCells rows, const Modifier& modifier, const LazyGridOptions& options,
                        const std::function<void(LazyGridScope&)>& content, CallSite site) {
    ComposableScope scope(site);
    lazyGrid(rows, modifier, options, content, false);
}

namespace {

// What a staggered grid keeps between compositions and layouts.
} // namespace

// Every item's place from the last layout (unmeasured ones by the average), and what came of it.
struct LazyStaggeredGridState::Data {
    ScrollState scroll;
    struct Place {
        float x = 0, y = 0, width = 0, height = 0;
        int lane = 0;
        bool fullLine = false;
        size_t key = 0;
    };
    std::vector<Place> places;
    float padBefore = 0;      // the content padding before the first items (the top, or the start)
    bool horizontal = false;  // a LazyHorizontalStaggeredGrid's: it scrolls along x
    MutableState<int> first{0};
    float firstOffset = 0;
    MutableState<LazyStaggeredGridLayoutInfo> info{LazyStaggeredGridLayoutInfo{}};
    // The item a scroll went to: kept at the top while the items around it get measured (the
    // places of unmeasured ones were guesses), until something else scrolls.
    std::optional<std::pair<int, float>> pending;
    float settledAt = -1;  // where the last correction left the scroll
    std::function<bool(int)> measured;  // whether an item's height is known

    float targetOf(int index, float offset) const {
        if (places.empty()) return 0;
        const Place& p = places[size_t(std::clamp(index, 0, int(places.size()) - 1))];
        return (horizontal ? p.x : p.y) - padBefore + offset;
    }
};

LazyStaggeredGridState::LazyStaggeredGridState() : data_(std::make_shared<Data>()) {}
int LazyStaggeredGridState::firstVisibleItemIndex() const {
    int first = data_->first.get();
    return data_->pending && data_->places.empty() ? data_->pending->first : first;
}
float LazyStaggeredGridState::firstVisibleItemScrollOffset() const {
    return data_->pending && data_->places.empty() ? data_->pending->second : data_->firstOffset;
}
bool LazyStaggeredGridState::canScrollBackward() const { return data_->scroll.value() > 0; }
bool LazyStaggeredGridState::canScrollForward() const { return data_->scroll.value() < data_->scroll.maxValue(); }
void LazyStaggeredGridState::scrollToItem(int index, float offset) const {
    data_->pending = std::pair{index, offset};  // kept there while the places settle (a layout does it)
    data_->settledAt = -1;
    if (!data_->places.empty()) {
        data_->scroll.scrollTo(data_->targetOf(index, offset));
        data_->settledAt = data_->scroll.value();  // a scroll after this one lets go of it
    }
}
Task LazyStaggeredGridState::animateScrollToItem(int index, float offset) const {
    return data_->scroll.animateScrollTo(data_->targetOf(index, offset));
}
LazyStaggeredGridLayoutInfo LazyStaggeredGridState::layoutInfo() const { return data_->info.get(); }
const ScrollState& LazyStaggeredGridState::scrollState() const { return data_->scroll; }

LazyStaggeredGridState rememberLazyStaggeredGridState(int initialFirstVisibleItemIndex, float initialFirstVisibleItemScrollOffset,
                                                      CallSite site) {
    return rememberSaveable(
        [&] {
            LazyStaggeredGridState s;
            if (initialFirstVisibleItemIndex != 0 || initialFirstVisibleItemScrollOffset != 0)
                s.scrollToItem(initialFirstVisibleItemIndex, initialFirstVisibleItemScrollOffset);
            return s;
        },
        site);
}

namespace {

struct StaggeredData {
    MutableState<int> window{0};  // bumped when the composed items no longer cover the view
    std::pair<int, int> composed{0, 0};  // the items composed, [start, end)
    bool laidOut = false;
    std::vector<size_t> keys;
    std::vector<bool> fullLine;
    std::vector<int> nodes;  // the item each child node shows
    float viewport = 0;
    std::unordered_map<size_t, float> heights;  // measured (widths in a horizontal grid), by key
    float heightSum = 0;
    int heightCount = 0;

    float heightOf(int index) const {
        auto it = heights.find(keys[size_t(index)]);
        if (it != heights.end()) return it->second;
        return heightCount ? heightSum / float(heightCount) : 120.f;  // not measured yet: a guess
    }
    void record(size_t key, float h) {
        auto [it, inserted] = heights.try_emplace(key, h);
        if (inserted)
            ++heightCount;
        else
            heightSum -= it->second;
        it->second = h;
        heightSum += h;
    }
};

} // namespace

namespace {

// Both staggered grids: lanes across the scrolling axis (columns of a vertical grid, rows of a
// horizontal one), items along it.
void staggeredGrid(bool horizontal, StaggeredGridCells lanesOf, const Modifier& modifier, const LazyStaggeredGridOptions& options,
                   const std::function<void(LazyGridScope&)>& content) {
    auto d = remember([] { return std::make_shared<StaggeredData>(); });
    auto own = remember([] { return LazyStaggeredGridState(); });
    LazyStaggeredGridState gridState = options.state ? *options.state : own;
    auto sd = gridState.data_;
    sd->horizontal = horizontal;
    ScrollState scroll = sd->scroll;
    LazyGridScope grid;
    content(grid);
    int count = grid.size();
    d->keys.resize(size_t(count));
    d->fullLine.resize(size_t(count));
    for (int i = 0; i < count; ++i) {
        d->keys[size_t(i)] = grid.keyAt(i);
        d->fullLine[size_t(i)] = grid.spanAt(i, LazyGridScope::FullLine) > 1;
    }
    d->window.get();  // recompose when scrolling needs items that aren't composed
    auto [start, end] = d->laidOut ? d->composed : std::pair{0, 24};  // before the first layout: a screenful, say
    start = std::clamp(start, 0, count);
    end = std::clamp(end, start, count);
    d->composed = {start, end};
    d->nodes.clear();
    for (int i = start; i < end; ++i) d->nodes.push_back(i);

    // Every item's place, in order: after the shortest lane, or across all of them. "Main" is
    // the scrolling axis (y of a vertical grid), "cross" the lanes'.
    auto policy = [d, lanesOf, options, scroll, sd, horizontal](LayoutNode& node, const Constraints& c) {
        const PaddingValues& pad = options.contentPadding;
        float crossPadStart = horizontal ? pad.top : pad.start, crossPadEnd = horizontal ? pad.bottom : pad.end;
        float mainPadStart = horizontal ? pad.start : pad.top, mainPadEnd = horizontal ? pad.end : pad.bottom;
        bool bounded = horizontal ? c.hasBoundedHeight() : c.hasBoundedWidth();
        float across = bounded ? (horizontal ? c.maxHeight : c.maxWidth) : 360.f;
        float inner = std::max(0.f, across - crossPadStart - crossPadEnd);
        float crossGap = horizontal ? options.verticalSpacing : options.horizontalSpacing;
        float mainGap = horizontal ? options.horizontalItemSpacing : options.verticalItemSpacing;
        int lanes = std::max(1, lanesOf.count(inner, crossGap));
        float laneSize = std::max(0.f, (inner - crossGap * float(lanes - 1)) / float(lanes));
        bool probe = detail::isProbeMeasure();
        for (size_t k = 0; k < node.children.size() && k < d->nodes.size(); ++k) {
            int i = d->nodes[k];
            float cs = d->fullLine[size_t(i)] ? inner : laneSize;
            Size s = node.children[k]->measure(horizontal ? Constraints{0, kInfinity, cs, cs} : Constraints{cs, cs, 0, kInfinity});
            if (!probe) d->record(d->keys[size_t(i)], horizontal ? s.width : s.height);
        }
        struct Place {
            float cross, main, length;
            int lane;
        };
        int n = int(d->keys.size());
        std::vector<Place> places(static_cast<size_t>(n));
        std::vector<float> ends(size_t(lanes), mainPadStart);
        for (int i = 0; i < n; ++i) {
            float length = d->heightOf(i);
            if (d->fullLine[size_t(i)]) {
                float at = *std::max_element(ends.begin(), ends.end());
                places[size_t(i)] = {crossPadStart, at, length, 0};
                std::fill(ends.begin(), ends.end(), at + length + mainGap);
            } else {
                size_t lane = size_t(std::min_element(ends.begin(), ends.end()) - ends.begin());
                places[size_t(i)] = {crossPadStart + float(lane) * (laneSize + crossGap), ends[lane], length, int(lane)};
                ends[lane] += length + mainGap;
            }
        }
        float total = (n ? *std::max_element(ends.begin(), ends.end()) - mainGap : mainPadStart) + mainPadEnd;
        for (size_t k = 0; k < node.children.size() && k < d->nodes.size(); ++k) {
            const Place& p = places[size_t(d->nodes[k])];
            if (horizontal) node.children[k]->place(p.main, p.cross);
            else node.children[k]->place(p.cross, p.main);
        }
        if (!probe) {
            // The items in view must be composed; when they aren't, compose them and half a view
            // more on each side, so that small scrolls don't need a recomposition.
            float view = d->viewport > 0 ? d->viewport : 800.f, top = scroll.value();
            auto range = [&](float from, float to) {
                int first = n, last = -1;
                for (int i = 0; i < n; ++i)
                    if (places[size_t(i)].main < to && places[size_t(i)].main + places[size_t(i)].length > from) {
                        first = std::min(first, i);
                        last = std::max(last, i);
                    }
                return first <= last ? std::pair{first, last + 1} : std::pair{0, 0};
            };
            auto visible = range(top, top + view);
            bool covered = visible.first >= d->composed.first && visible.second <= d->composed.second;
            if (!d->laidOut || !covered) {
                d->composed = range(top - view / 2, top + view * 1.5f);
                d->window.set(d->window.peek() + 1);
            }
            d->laidOut = true;
            // The state: every place, the first visible item and what is in view.
            auto size = [&](const Place& p, bool full) {
                float cs = full ? inner : laneSize;
                return horizontal ? Size{p.length, cs} : Size{cs, p.length};
            };
            sd->places.resize(size_t(n));
            for (int i = 0; i < n; ++i) {
                const Place& p = places[size_t(i)];
                bool full = d->fullLine[size_t(i)];
                Size s = size(p, full);
                float x = horizontal ? p.main : p.cross, y = horizontal ? p.cross : p.main;
                sd->places[size_t(i)] = {x, y, s.width, s.height, p.lane, full, d->keys[size_t(i)]};
            }
            sd->padBefore = mainPadStart;
            sd->measured = [d](int i) { return i >= 0 && size_t(i) < d->keys.size() && d->heights.contains(d->keys[size_t(i)]); };
            LazyStaggeredGridLayoutInfo info;
            info.viewportStartOffset = -mainPadStart;
            info.viewportEndOffset = view - mainPadStart;
            info.totalItemsCount = n;
            info.viewportSize = horizontal ? Size{view, across} : Size{across, view};
            info.beforeContentPadding = mainPadStart, info.afterContentPadding = mainPadEnd, info.mainAxisItemSpacing = mainGap;
            info.lanes = lanes;
            for (int i = visible.first; i < visible.second; ++i) {
                const Place& p = places[size_t(i)];
                if (!(p.main < top + view && p.main + p.length > top)) continue;
                bool full = d->fullLine[size_t(i)];
                float along = p.main - top - mainPadStart, lane = p.cross - crossPadStart;
                info.visibleItemsInfo.push_back(
                    {i, d->keys[size_t(i)], p.lane, full, horizontal ? Offset{along, lane} : Offset{lane, along}, size(p, full)});
            }
            int first = info.visibleItemsInfo.empty() ? 0 : info.visibleItemsInfo.front().index;
            sd->firstOffset = info.visibleItemsInfo.empty() ? 0 : top - places[size_t(first)].main + mainPadStart;
            if (first != sd->first.peek()) sd->first.set(first);
            if (!(info == sd->info.peek())) sd->info.set(std::move(info));
        }
        return c.constrain(horizontal ? Size{total, across} : Size{across, total});
    };
    // The outer node learns the view's length for the scrolled one inside it.
    detail::Composer::require().emit(
        detail::keyOf(CallSite::current()),
        [&](LayoutNode& node) {
            node.modifier = modifier;
            node.measurePolicy = [d, sd, horizontal](LayoutNode& n, const Constraints& c) {
                bool bounded = horizontal ? c.hasBoundedWidth() : c.hasBoundedHeight();
                d->viewport = bounded ? (horizontal ? c.maxWidth : c.maxHeight) : 0;
                Size s{c.minWidth, c.minHeight};
                for (auto* child : n.children) {
                    s = child->measure(c);
                    child->place(0, 0);
                }
                // The item scrolled to stays at the start while its neighbours' sizes come in; a
                // scroll by something else lets go of it.
                if (sd->pending && !sd->places.empty() && !detail::isProbeMeasure()) {
                    auto [index, offset] = *sd->pending;
                    float target = sd->targetOf(index, offset), now = sd->scroll.value();
                    if (sd->settledAt >= 0 && std::abs(now - sd->settledAt) > 0.5f) {
                        sd->pending.reset();
                    } else if (std::abs(now - target) > 0.5f) {
                        sd->scroll.scrollTo(target);
                        sd->settledAt = sd->scroll.value();
                    } else if (sd->measured && sd->measured(index)) {
                        sd->pending.reset();
                    } else {
                        sd->settledAt = now;
                    }
                }
                return c.constrain(s);
            };
            node.drawContent = {};
        },
        [&] {
            detail::Composer::require().emit(
                detail::keyOf(CallSite::current()),
                [&](LayoutNode& node) {
                    if (options.overscrollEffect)
                        node.modifier = horizontal ? Modifier().fillMaxHeight().horizontalScroll(scroll, *options.overscrollEffect, options.userScrollEnabled)
                                                   : Modifier().fillMaxWidth().verticalScroll(scroll, *options.overscrollEffect, options.userScrollEnabled);
                    else
                        node.modifier = horizontal ? Modifier().fillMaxHeight().horizontalScroll(scroll, options.userScrollEnabled)
                                                   : Modifier().fillMaxWidth().verticalScroll(scroll, options.userScrollEnabled);
                    node.measurePolicy = policy;
                    node.drawContent = {};
                },
                [&] {
                    for (int i = start; i < end; ++i)
                        key(grid.keyAt(i), [&] {
                            detail::Composer::require().emit(
                                detail::keyOf(CallSite::current()),
                                [](LayoutNode& cell) {
                                    cell.modifier = {};
                                    cell.measurePolicy = {};  // stacks its content
                                    cell.drawContent = {};
                                },
                                [&] { grid.composeAt(i); });
                        });
                });
        });
}

} // namespace

void LazyVerticalStaggeredGrid(StaggeredGridCells columns, const Modifier& modifier, const LazyStaggeredGridOptions& options,
                               const std::function<void(LazyGridScope&)>& content, CallSite site) {
    ComposableScope scope(site);
    staggeredGrid(false, columns, modifier, options, content);
}

void LazyHorizontalStaggeredGrid(StaggeredGridCells rows, const Modifier& modifier, const LazyStaggeredGridOptions& options,
                                 const std::function<void(LazyGridScope&)>& content, CallSite site) {
    ComposableScope scope(site);
    staggeredGrid(true, rows, modifier, options, content);
}
} // namespace kompoz
