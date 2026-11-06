#pragma once

// Material 3 carousels: a row of items that scroll sideways.
//
// HorizontalMultiBrowseCarousel shows a few large items, then a medium and a small one at the
// end; scrolled, each item grows and shrinks as it moves along (and is masked to that size —
// its content keeps the large size, cut around its middle), and a release settles on an item.
// HorizontalUncontainedCarousel's items all keep one width; it scrolls freely.
// HorizontalCenteredHeroCarousel shows one large item in the middle and a small one on either
// side; the first item is large at the start of the list and the last at its end, and a release
// settles with an item large.
//
//   auto state = rememberCarouselState(0, [&] { return int(photos.size()); });
//   HorizontalMultiBrowseCarousel(state, 186, Modifier().fillMaxWidth().height(221), CarouselOptions{.itemSpacing = 8},
//                                 [&](int i) { Image(photos[i], "", Modifier().fillMaxSize(), ContentScale::Crop); });

#include "layout.hpp"
#include "named.hpp"
#include "runtime.hpp"

#include <functional>
#include <memory>

namespace kompoz {

class CarouselState {
public:
    CarouselState(int initialItem, std::function<int()> itemCount);
    int currentItem() const;  // observable: the item at the start (the first large one)
    int itemCount() const;
    bool isScrollInProgress() const;
    void scrollToItem(int item) const;
    Task animateScrollToItem(int item) const;

    friend bool operator==(const CarouselState& a, const CarouselState& b) { return a.data_ == b.data_; }
    struct Data;
    std::shared_ptr<Data> data_;
};
template <>
struct Saver<CarouselState> {
    static SavedValue save(const CarouselState& s) { return int64_t(s.currentItem()); }
    static std::optional<CarouselState> restore(const SavedValue& v) {
        auto item = v.integer();
        return item ? std::optional(CarouselState(int(*item), nullptr)) : std::nullopt;
    }
};
// Saveable; the latest itemCount is used.
namespace detail {
CarouselState rememberCarouselState(int initialItem, std::function<int()> itemCount, CallSite site);
// rememberCarouselState(initialItem = 0) { itemCount }, as Compose's.
struct RememberCarouselStateImpl {
    template <class... A>
    static CarouselState call(CallSite site, A&&... args) {
        return rememberWithCount<names::InitialItem, names::ItemCount, CarouselState>(
            [](int first, std::function<int()> count, CallSite s) { return rememberCarouselState(first, std::move(count), s); }, site,
            std::forward<A>(args)...);
    }
};
} // namespace detail
inline constexpr Composable<detail::RememberCarouselStateImpl> rememberCarouselState;

namespace CarouselDefaults {
inline constexpr float MinSmallItemSize = 40, MaxSmallItemSize = 56;
} // namespace CarouselDefaults

struct CarouselOptions {
    float itemSpacing = 0;
    PaddingValues contentPadding;  // the items scroll on into it
    Shape itemShape = RoundedCornerShape(28);  // each item's mask (MaterialTheme.shapes.extraLarge; see maskClip)
    float minSmallItemWidth = CarouselDefaults::MinSmallItemSize, maxSmallItemWidth = CarouselDefaults::MaxSmallItemSize;
    bool userScrollEnabled = true;
};

// What an item knows of itself (Compose's CarouselItemScope.carouselItemInfo): how wide its mask
// is now, and the smallest and largest it gets. It changes with every step of a scroll without
// recomposing: read it while drawing (drawBehind, graphicsLayer), e.g. to fade a label in as the
// item grows.
class CarouselItemInfo {
public:
    struct Data {
        float size = 0, minSize = 0, maxSize = 0;
    };
    explicit CarouselItemInfo(std::shared_ptr<Data> data) : data_(std::move(data)) {}
    float size() const { return data_->size; }
    float minSize() const { return data_->minSize; }
    float maxSize() const { return data_->maxSize; }
    friend bool operator==(const CarouselItemInfo& a, const CarouselItemInfo& b) { return a.data_ == b.data_; }

private:
    std::shared_ptr<Data> data_;
};
struct CarouselItemScope {
    int index;
    CarouselItemInfo carouselItemInfo;
    // The content kept to the part of it its item shows (Compose's maskClip / maskBorder): a mask
    // as wide as the item is now, in the middle of the content, in `shape` (with options.itemShape
    // RectangleShape, the item's own mask is just its bounds); a border along it.
    Modifier maskClip(Shape shape) const;
    Modifier maskBorder(const BorderStroke& border, Shape shape) const;
};

namespace detail {
void HorizontalMultiBrowseCarousel(const CarouselState& state, float preferredItemWidth, const Modifier& modifier, const CarouselOptions& options,
                                   const std::function<void(int item)>& content, CallSite site);
void HorizontalMultiBrowseCarousel(const CarouselState& state, float preferredItemWidth, const Modifier& modifier, const CarouselOptions& options,
                                   const std::function<void(const CarouselItemScope& item)>& content, CallSite site);
void HorizontalUncontainedCarousel(const CarouselState& state, float itemWidth, const Modifier& modifier, const CarouselOptions& options,
                                   const std::function<void(int item)>& content, CallSite site);
void HorizontalUncontainedCarousel(const CarouselState& state, float itemWidth, const Modifier& modifier, const CarouselOptions& options,
                                   const std::function<void(const CarouselItemScope& item)>& content, CallSite site);
// maxItemWidth caps the large item (0: as wide as the small ones leave); what it leaves widens them.
void HorizontalCenteredHeroCarousel(const CarouselState& state, float maxItemWidth, const Modifier& modifier, const CarouselOptions& options,
                                    const std::function<void(int item)>& content, CallSite site);
void HorizontalCenteredHeroCarousel(const CarouselState& state, float maxItemWidth, const Modifier& modifier, const CarouselOptions& options,
                                    const std::function<void(const CarouselItemScope& item)>& content, CallSite site);
// The carousels as Compose's: (state, size, modifier, itemSpacing = …, contentPadding = …,
// minSmallItemWidth = …, maxSmallItemWidth = …, userScrollEnabled = …) { index -> … }, the content
// taking the item's index, its CarouselItemScope, or both (Compose's CarouselItemScope.(Int)); or
// with a CarouselOptions after the modifier.
template <int Kind, class SizeTag>
struct CarouselImpl {
    using P = Params<names::State, SizeTag, names::Modifier, names::Options, names::ItemSpacing, names::ContentPadding,
                     names::MinSmallItemWidth, names::MaxSmallItemWidth, names::UserScrollEnabled>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        splitLast(
            [&](auto&& content, auto&&... rest) {
                static_assert(P::template check<decltype(rest)...>());
                CarouselOptions options = P::template get<names::Options>(CarouselOptions{}, rest...);
                P::template set<names::ItemSpacing>(options.itemSpacing, rest...);
                P::template set<names::ContentPadding>(options.contentPadding, rest...);
                P::template set<names::MinSmallItemWidth>(options.minSmallItemWidth, rest...);
                P::template set<names::MaxSmallItemWidth>(options.maxSmallItemWidth, rest...);
                P::template set<names::UserScrollEnabled>(options.userScrollEnabled, rest...);
                const CarouselState& state = P::template arg<names::State>(rest...);
                float size = P::template get<SizeTag>(0.f, rest...);
                Modifier modifier = P::template get<names::Modifier>(Modifier{}, rest...);
                auto run = [&](const auto& f) {
                    if constexpr (Kind == 0) HorizontalMultiBrowseCarousel(state, size, modifier, options, f, site);
                    else if constexpr (Kind == 1) HorizontalUncontainedCarousel(state, size, modifier, options, f, site);
                    else HorizontalCenteredHeroCarousel(state, size, modifier, options, f, site);
                };
                using C = std::remove_cvref_t<decltype(content)>;
                if constexpr (std::is_invocable_v<C&, const CarouselItemScope&, int>)
                    run(std::function<void(const CarouselItemScope&)>([content](const CarouselItemScope& item) mutable { content(item, item.index); }));
                else if constexpr (std::is_invocable_v<C&, const CarouselItemScope&>)
                    run(std::function<void(const CarouselItemScope&)>(content));
                else
                    run(std::function<void(int)>(content));
            },
            std::forward<A>(args)...);
    }
};
} // namespace detail
// preferredItemWidth: the large items'; the small ones take what is left, between their min and max.
inline constexpr Composable<detail::CarouselImpl<0, detail::names::PreferredItemWidth>> HorizontalMultiBrowseCarousel;
inline constexpr Composable<detail::CarouselImpl<1, detail::names::ItemWidth>> HorizontalUncontainedCarousel;
// maxItemWidth caps the large item (0: as wide as the small ones leave); what it leaves widens them.
inline constexpr Composable<detail::CarouselImpl<2, detail::names::MaxItemWidth>> HorizontalCenteredHeroCarousel;

} // namespace kompoz
