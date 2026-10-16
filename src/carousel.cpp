#include <kompoz/animation.hpp>
#include <kompoz/carousel.hpp>
#include <kompoz/coroutines.hpp>
#include <kompoz/host.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace kompoz {

struct CarouselState::Data {
    MutableState<int> current{0};
    Animatable<float> position{0.f};  // in items: item i is at the start when it is i
    std::function<int()> count;
    float stride = 1;       // dp a drag moves it by an item (a large item and the spacing): the last layout's
    float maxPosition = 0;  // the last layout's
    float large = 0;        // the size items are measured at (masked to their own)
    float smallest = 0;     // the smallest an item gets
    int shown = 4;          // items the window holds, to compose around the position
    bool dragging = false, snaps = true, laidOut = false;

    int items() const { return count ? std::max(0, count()) : 0; }
    float clamp(float p) const { return std::clamp(p, 0.f, std::max(0.f, maxPosition)); }
};

CarouselState::CarouselState(int initialItem, std::function<int()> itemCount) : data_(std::make_shared<Data>()) {
    data_->count = std::move(itemCount);
    data_->position.snapTo(float(std::max(0, initialItem)));  // kept in range by the first layout
    data_->current.set(std::max(0, initialItem));
}
int CarouselState::currentItem() const { return data_->current.get(); }
int CarouselState::itemCount() const { return data_->items(); }
bool CarouselState::isScrollInProgress() const { return data_->dragging || data_->position.isRunning(); }
void CarouselState::scrollToItem(int item) const {
    data_->position.snapTo(data_->laidOut ? data_->clamp(float(item)) : float(std::max(0, item)));
    detail::requestLayout();
}
Task CarouselState::animateScrollToItem(int item) const {
    return data_->position.animateTo(data_->clamp(float(item)), spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow));
}

CarouselState rememberCarouselState(int initialItem, std::function<int()> itemCount, CallSite site) {
    auto& state = rememberSaveable([&] { return CarouselState(initialItem, itemCount); }, site);
    state.data_->count = std::move(itemCount);  // the latest
    return state;
}

namespace {

// Scrolls sideways by items; a release settles on one (multi-browse) or flings freely.
struct CarouselElement final : ScrollableElement {
    CarouselElement(std::shared_ptr<CarouselState::Data> data, CoroutineScope scope) : d(std::move(data)), coroutines(std::move(scope)) {}
    std::shared_ptr<CarouselState::Data> d;
    CoroutineScope coroutines;
    Orientation orientation() const override { return Orientation::Horizontal; }
    float scrollBy(float delta) const override {
        float stride = std::max(1.f, d->stride);
        float before = d->position.peek();
        d->position.snapTo(d->clamp(before + delta / stride));
        d->dragging = true;
        return (d->position.peek() - before) * stride;
    }
    bool settle(float velocity) const override {
        if (!d->snaps) {  // uncontained: the usual fling
            d->dragging = false;
            return false;
        }
        float pos = d->position.peek();
        float target = std::round(pos);
        if (!d->dragging && (d->position.isRunning() || target == pos)) return true;
        d->dragging = false;
        if (std::abs(velocity) > 300) target = velocity > 0 ? std::ceil(pos) : std::floor(pos);  // flung: on to the next
        target = d->clamp(target);
        auto data = d;
        coroutines.launch([data, target]() -> Task {
            co_await data->position.animateTo(target, spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow));
        });
        return true;
    }
    void draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode&, const std::function<void()>& drawContent) const override {
        canvas.pushClip(clipBounds(bounds));
        drawContent();
        canvas.popClip();
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const CarouselElement*>(&other);
        return o && o->d == d;
    }
};

// The multi-browse sizes for a window `width` wide: as many large items near the preferred
// width as fit with a medium one (between the small and the large) and a small one after.
std::vector<float> arrange(float width, float preferred, float spacing, float minSmall, float maxSmall) {
    float small = std::max(minSmall, maxSmall);
    if (width <= 0) return {};
    int large = std::max(1, int(std::floor((width - 2 * small - 2 * spacing) / (preferred + spacing))));
    float l = preferred, medium = 0;
    while (true) {
        medium = width - float(large) * l - small - float(large + 1) * spacing;
        if (medium >= small || large == 1) break;
        --large;
    }
    if (medium > l) medium = (l + small) / 2;  // room to spare: the large ones grow
    if (medium < small) small = medium = std::max(minSmall, std::min(small, (width - 3 * spacing) / 3));  // narrow: they shrink
    l = (width - small - medium - float(large + 1) * spacing) / float(large);
    if (l <= small) return {width};  // only room for one
    std::vector<float> sizes(size_t(large), l);
    sizes.push_back(medium);
    sizes.push_back(small);
    return sizes;
}

enum class Kind { MultiBrowse, Uncontained, Hero };

// The centered hero layout: a large item and a small one on either side. Item `first + idx` is
// at slot k = item - position (0: the large one); at the start of the list the large one is at
// the start ([large, small, small]), in the middle in the middle, at the end at the end.
Size heroLayout(LayoutNode& n, const Constraints& c, CarouselState::Data& d, int first, PaddingValues padding, float spacing, float maxItem,
                float maxSmall) {
    float w = c.hasBoundedWidth() ? c.maxWidth : c.minWidth;
    float window = std::max(0.f, w - padding.start - padding.end);
    int count = d.items();
    float pos = d.position.peek();
    float small = maxSmall, large = window - 2 * small - 2 * spacing;
    if (maxItem > 0 && large > maxItem) {  // capped: the small ones take what is left
        large = maxItem;
        small = (window - large - 2 * spacing) / 2;
    }
    if (large <= small) small = 0, large = window;  // only room for one
    float maxPosition = float(std::max(0, count - 1));
    if (!detail::isProbeMeasure()) {
        d.stride = std::max(1.f, (large + small) / 2 + spacing);
        d.maxPosition = maxPosition;
        d.large = large;
        d.smallest = small > 0 ? small : large;
        d.shown = 3;
        d.laidOut = true;
        if (pos > maxPosition && !d.position.isRunning()) d.position.snapTo(pos = maxPosition);
        if (int nearest = int(std::lround(pos)); d.current.peek() != nearest) d.current.set(nearest);
    }
    // A slot's x and size with the large one at `focus` among the three that show.
    auto slot = [&](int k, int focus) {
        int j = k + focus;
        auto size = [&](int s) { return s == focus ? large : small; };
        if (j < 0) return std::pair{float(j) * (small + spacing), small};
        float x = 0;
        for (int s = 0; s < std::min(j, 3); ++s) x += size(s) + spacing;
        if (j >= 3) return std::pair{x + float(j - 3) * (small + spacing), small};
        return std::pair{x, size(j)};
    };
    // Which arrangement, and how far into the next one.
    int from = 1, to = 1;
    float t = 0;
    if (count <= 1) from = to = 0;
    else if (count == 2) from = 0, to = 2, t = std::clamp(pos, 0.f, 1.f);
    else if (pos < 1) from = 0, t = std::max(0.f, pos);
    else if (pos > float(count - 2)) to = 2, t = std::min(1.f, pos - float(count - 2));
    auto at = [&](int k) {
        auto [x0, s0] = slot(k, from);
        auto [x1, s1] = slot(k, to);
        return std::pair{lerp(x0, x1, t), lerp(s0, s1, t)};
    };
    float h = c.hasBoundedHeight() ? std::max(0.f, c.maxHeight - padding.top - padding.bottom) : -1;
    float tallest = 0;
    for (size_t idx = 0; idx < n.children.size(); ++idx) {
        float k = float(first + int(idx)) - pos;
        int j = int(std::floor(k));
        float f = k - float(j);
        auto [xa, sa] = at(j);
        auto [xb, sb] = at(j + 1);
        float size = lerp(sa, sb, f), x = lerp(xa, xb, f);
        Size s = n.children[idx]->measure(h >= 0 ? Constraints::fixed(size, h) : Constraints{size, size, 0, kInfinity});
        n.children[idx]->place(padding.start + x, padding.top);
        tallest = std::max(tallest, s.height);
    }
    return c.constrain({w, (h >= 0 ? h : tallest) + padding.top + padding.bottom});
}

using ItemContent = std::function<void(const CarouselItemScope&)>;

// maskClip / maskBorder: a clip or a border drawn over the item's mask in the content, not its bounds.
struct MaskElement final : ModifierElement {
    CarouselItemInfo info;
    std::shared_ptr<const ModifierElement> drawn;  // clip(shape) or border(stroke, shape)'s element
    bool clips = true;
    explicit MaskElement(CarouselItemInfo i) : info(std::move(i)) {}
    Rect mask(const Rect& b) const {
        float w = info.size() > 0 ? std::min(b.width, info.size()) : b.width;
        return {b.x + (b.width - w) / 2, b.y, w, b.height};
    }
    void draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode& node, const std::function<void()>& drawContent) const override {
        drawn->draw(canvas, mask(bounds), node, drawContent);
    }
    bool clipsContent() const override { return clips; }
    Rect clipBounds(const Rect& bounds) const override { return mask(bounds); }
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const MaskElement*>(&other);
        return o && o->info == info && o->clips == clips && o->drawn->equals(*drawn);
    }
};

void carousel(const CarouselState& state, float itemWidth, Kind kind, const Modifier& modifier, const CarouselOptions& options,
              const ItemContent& content) {
    bool multiBrowse = kind == Kind::MultiBrowse;
    auto d = state.data_;
    d->snaps = kind != Kind::Uncontained;
    int count = d->items();
    float pos = d->position.value();  // laid out again as it moves
    int behind = kind == Kind::Hero ? 2 : 1;  // the hero layout shows two before the large one at the end of the list
    int first = std::max(0, int(std::floor(pos)) - behind), last = std::min(count - 1, int(std::floor(pos)) + d->shown + 1);
    auto coroutines = rememberCoroutineScope();
    Modifier m = options.userScrollEnabled ? modifier.then(std::make_shared<CarouselElement>(d, coroutines)) : modifier.clipToBounds();
    PaddingValues padding = options.contentPadding;
    float spacing = options.itemSpacing, minSmall = options.minSmallItemWidth, maxSmall = options.maxSmallItemWidth;

    auto policy = [d, first, padding, spacing, multiBrowse, kind, itemWidth, minSmall, maxSmall](LayoutNode& n, const Constraints& c) {
        if (kind == Kind::Hero) return heroLayout(n, c, *d, first, padding, spacing, itemWidth, std::max(minSmall, maxSmall));
        float w = c.hasBoundedWidth() ? c.maxWidth : c.minWidth;
        float window = std::max(0.f, w - padding.start - padding.end);
        int count = d->items();
        float pos = d->position.peek();
        // Slot sizes and positions (from the window's start): item i sits at slot i - position,
        // between two slots it takes a mix of both — so the items always fill the window.
        std::vector<float> sizes, ends;  // the start arrangement; and the one at the end of the list
        if (multiBrowse) {
            sizes = arrange(window, itemWidth, spacing, minSmall, maxSmall);
            ends.assign(sizes.rbegin(), sizes.rend());
        } else {
            int fit = std::max(1, int(std::ceil((window + spacing) / (itemWidth + spacing))));
            sizes.assign(size_t(fit), itemWidth);
            ends = sizes;
        }
        int slots = int(sizes.size());
        float large = sizes.empty() ? 0 : *std::max_element(sizes.begin(), sizes.end());
        float stride = (multiBrowse ? large : itemWidth) + spacing;
        float maxPosition = multiBrowse ? float(std::max(0, count - slots))
                                        : std::max(0.f, (float(count) * stride - spacing - window) / std::max(1.f, stride));
        if (!detail::isProbeMeasure()) {
            d->stride = std::max(1.f, stride);
            d->maxPosition = maxPosition;
            d->large = multiBrowse ? large : itemWidth;
            d->smallest = multiBrowse ? std::max(minSmall, std::min(sizes.empty() ? 0.f : *std::min_element(sizes.begin(), sizes.end()), maxSmall)) : itemWidth;
            d->shown = slots;
            d->laidOut = true;
            if (pos > maxPosition && !d->position.isRunning()) d->position.snapTo(pos = maxPosition);
            if (int nearest = int(std::lround(pos)); d->current.peek() != nearest) d->current.set(nearest);
        }
        // Near the end of the list the arrangement turns around (small, medium, large...), so the
        // last items can be large too.
        int largeCount = multiBrowse ? std::max(1, slots - 2) : slots;
        float turn = float(slots - largeCount);
        float e = multiBrowse && turn > 0 ? std::clamp((pos - (maxPosition - turn)) / turn, 0.f, 1.f) : 0.f;
        std::vector<float> slotSize, slotX;
        for (int k = 0; k < slots; ++k) {
            slotX.push_back(k == 0 ? 0.f : slotX.back() + slotSize.back() + spacing);
            slotSize.push_back(lerp(sizes[size_t(k)], ends[size_t(k)], e));
        }
        float edge = multiBrowse ? std::max(minSmall, std::min(slotSize.empty() ? 0.f : slotSize.back(), maxSmall)) : itemWidth;
        auto sizeAt = [&](int k) { return k >= 0 && k < slots ? slotSize[size_t(k)] : edge; };
        auto xAt = [&](int k) {
            if (k < 0) return float(k) * (edge + spacing);  // off the start
            if (k >= slots) return (slots ? slotX.back() + slotSize.back() + spacing : 0.f) + float(k - slots) * (edge + spacing);
            return slotX[size_t(k)];
        };
        float h = c.hasBoundedHeight() ? std::max(0.f, c.maxHeight - padding.top - padding.bottom) : -1;
        float tallest = 0;
        for (size_t idx = 0; idx < n.children.size(); ++idx) {
            float k = float(first + int(idx)) - pos;
            int j = int(std::floor(k));
            float t = k - float(j);
            float size = lerp(sizeAt(j), sizeAt(j + 1), t), x = lerp(xAt(j), xAt(j + 1), t);
            Size s = n.children[idx]->measure(h >= 0 ? Constraints::fixed(size, h) : Constraints{size, size, 0, kInfinity});
            n.children[idx]->place(padding.start + x, padding.top);
            tallest = std::max(tallest, s.height);
        }
        return c.constrain({w, (h >= 0 ? h : tallest) + padding.top + padding.bottom});
    };
    Shape shape = options.itemShape;
    detail::Composer::require().emit(
        detail::keyOf(CallSite::current()),
        [&](LayoutNode& node) {
            node.modifier = m;
            node.measurePolicy = policy;
            node.drawContent = {};
        },
        [&] {
            for (int i = first; i <= last; ++i)
                key(i, [&] {
                    auto info = remember([] { return std::make_shared<CarouselItemInfo::Data>(); });
                    // The item: masked to its size, its content at the large size around its middle.
                    detail::Composer::require().emit(
                        detail::keyOf(CallSite::current()),
                        [&](LayoutNode& item) {
                            item.modifier = Modifier().clip(shape);
                            item.measurePolicy = [d, info](LayoutNode& in, const Constraints& c) {
                                float full = std::max(d->large, c.maxWidth);
                                if (!detail::isProbeMeasure()) *info = {c.maxWidth, std::min(d->smallest, d->large), d->large};
                                Size size{c.maxWidth, c.minHeight};
                                for (auto* child : in.children) {
                                    Size s = child->measure({full, full, c.minHeight, c.maxHeight});
                                    child->place((c.maxWidth - full) / 2, 0);
                                    size.height = std::max(size.height, s.height);
                                }
                                return c.constrain(size);
                            };
                            item.drawContent = {};
                        },
                        [&] { Box(Modifier(), [&] { content(CarouselItemScope{i, CarouselItemInfo(info)}); }); });
                });
        });
}

} // namespace

Modifier CarouselItemScope::maskClip(Shape shape) const {
    auto e = std::make_shared<MaskElement>(carouselItemInfo);
    e->drawn = Modifier().clip(shape).elements().front();
    return Modifier().then(e);
}
Modifier CarouselItemScope::maskBorder(const BorderStroke& border, Shape shape) const {
    auto e = std::make_shared<MaskElement>(carouselItemInfo);
    e->drawn = Modifier().border(border, shape).elements().front();
    e->clips = false;
    return Modifier().then(e);
}

void HorizontalMultiBrowseCarousel(const CarouselState& state, float preferredItemWidth, const Modifier& modifier, const CarouselOptions& options,
                                   const std::function<void(int item)>& content, CallSite site) {
    ComposableScope scope(site);
    carousel(state, preferredItemWidth, Kind::MultiBrowse, modifier, options, [&](const CarouselItemScope& item) { content(item.index); });
}

void HorizontalMultiBrowseCarousel(const CarouselState& state, float preferredItemWidth, const Modifier& modifier, const CarouselOptions& options,
                                   const std::function<void(const CarouselItemScope& item)>& content, CallSite site) {
    ComposableScope scope(site);
    carousel(state, preferredItemWidth, Kind::MultiBrowse, modifier, options, content);
}

void HorizontalUncontainedCarousel(const CarouselState& state, float itemWidth, const Modifier& modifier, const CarouselOptions& options,
                                   const std::function<void(int item)>& content, CallSite site) {
    ComposableScope scope(site);
    carousel(state, itemWidth, Kind::Uncontained, modifier, options, [&](const CarouselItemScope& item) { content(item.index); });
}

void HorizontalUncontainedCarousel(const CarouselState& state, float itemWidth, const Modifier& modifier, const CarouselOptions& options,
                                   const std::function<void(const CarouselItemScope& item)>& content, CallSite site) {
    ComposableScope scope(site);
    carousel(state, itemWidth, Kind::Uncontained, modifier, options, content);
}

void HorizontalCenteredHeroCarousel(const CarouselState& state, float maxItemWidth, const Modifier& modifier, const CarouselOptions& options,
                                    const std::function<void(int item)>& content, CallSite site) {
    ComposableScope scope(site);
    carousel(state, maxItemWidth, Kind::Hero, modifier, options, [&](const CarouselItemScope& item) { content(item.index); });
}

void HorizontalCenteredHeroCarousel(const CarouselState& state, float maxItemWidth, const Modifier& modifier, const CarouselOptions& options,
                                    const std::function<void(const CarouselItemScope& item)>& content, CallSite site) {
    ComposableScope scope(site);
    carousel(state, maxItemWidth, Kind::Hero, modifier, options, content);
}

} // namespace kompoz
