#pragma once

// App structure from Material 3: Scaffold, top app bars (with scroll behaviors), NavigationBar,
// floating action buttons and snackbars.
//
//   Scaffold(Modifier().fillMaxSize(),
//            topBar = [&] { TopAppBar(title = [] { Text("Kompoz"); }); },
//            floatingActionButton = [&] { FloatingActionButton(onAdd, [] { Icon(Icons::Filled::Add, "Ekle"); }); },
//            snackbarHost = [&] { SnackbarHost(snackbar); },
//            [=](PaddingValues padding) {  // (with <kompoz/blocks.hpp>: Scaffold(…) [=](PaddingValues padding) { … };)
//       LazyColumn(Modifier().fillMaxSize(), contentPadding = padding, ...);
//   });

#include "coroutines.hpp"
#include "graphics.hpp"
#include "layout.hpp"
#include "nested_scroll.hpp"
#include "runtime.hpp"
#include "strings.hpp"
#include "widgets.hpp"

#include <array>
#include <chrono>
#include <deque>
#include <functional>
#include <optional>
#include <string>

namespace kompoz {

// Where the Scaffold puts its floating action button: at the end (Compose's default), the
// start or the middle above the bottom bar, or at the end over it.
enum class FabPosition { End, Start, Center, EndOverlay };

struct ScaffoldSlots {
    Content topBar, bottomBar, floatingActionButton, snackbarHost;
    FabPosition floatingActionButtonPosition = FabPosition::End;
    std::optional<Color> containerColor;
    // The insets the content's padding keeps clear of where there is no bar: by default the
    // system bars and the display cutout, not the keyboard (add .imePadding() inside).
    std::optional<WindowInsets> contentWindowInsets;
};

// `content` gets the padding that keeps it clear of the bars and the system insets, live (as
// they change size, a top bar collapsing as a list scrolls, what reads it as it lays out
// follows without the content recomposing; PaddingValues::calculate*Padding); it is
// laid out full size so lists can scroll behind the bars. With a text field, pad it as in
// Compose: Modifier().padding(padding).consumeWindowInsets(padding).imePadding().
namespace detail {
void Scaffold(const Modifier& modifier, const ScaffoldSlots& slots, const std::function<void(PaddingValues)>& content,
              CallSite site = CallSite::current());
struct ScaffoldImpl {
    using P = Params<names::Modifier, names::TopBar, names::BottomBar, names::SnackbarHost, names::FloatingActionButton, names::FloatingActionButtonPosition, names::ContainerColor, names::ContentWindowInsets, BlockWith<PaddingValues>>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: Scaffold takes a block, [=](PaddingValues padding) { … }: its content");
        ScaffoldSlots slots;
        P::set<names::TopBar>(slots.topBar, args...);
        P::set<names::BottomBar>(slots.bottomBar, args...);
        P::set<names::SnackbarHost>(slots.snackbarHost, args...);
        P::set<names::FloatingActionButton>(slots.floatingActionButton, args...);
        P::set<names::FloatingActionButtonPosition>(slots.floatingActionButtonPosition, args...);
        P::set<names::ContainerColor>(slots.containerColor, args...);
        P::set<names::ContentWindowInsets>(slots.contentWindowInsets, args...);
        Scaffold(P::get<names::Modifier>(Modifier{}, args...), slots, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::ScaffoldImpl> Scaffold;

// How far a top app bar is collapsed and how far the content under it has scrolled, in dp
// (negative: collapsed / scrolled up). Observable.
class TopAppBarState {
public:
    explicit TopAppBarState(float initialHeightOffsetLimit = -kInfinity, float initialHeightOffset = 0,
                            float initialContentOffset = 0);
    float heightOffsetLimit() const;  // minus the collapsible height; the app bar sets it
    void setHeightOffsetLimit(float limit) const;
    float heightOffset() const;  // limit .. 0
    void setHeightOffset(float offset) const;
    float contentOffset() const;
    void setContentOffset(float offset) const;
    float collapsedFraction() const;   // 0 expanded .. 1 collapsed
    float overlappedFraction() const;  // > 0 once content scrolled under the bar
    friend bool operator==(const TopAppBarState& a, const TopAppBarState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};

// Saved (rememberSaveable): how far it is collapsed and scrolled, as Compose's TopAppBarState.Saver.
template <>
struct Saver<TopAppBarState> {
    static SavedValue save(const TopAppBarState& s);
    static std::optional<TopAppBarState> restore(const SavedValue& v);
};
TopAppBarState rememberTopAppBarState(float initialHeightOffsetLimit = -kInfinity, float initialHeightOffset = 0, float initialContentOffset = 0,
                                      CallSite site = CallSite::current());

// Put the connection on a parent of the scrolling content (usually the Scaffold):
//   auto scroll = TopAppBarDefaults::enterAlwaysScrollBehavior();
//   Scaffold(Modifier().nestedScroll(scroll.nestedScrollConnection), topBar = [&] {
//       TopAppBar(title = ..., scrollBehavior = scroll); }, ...);
struct TopAppBarScrollBehavior {
    TopAppBarState state;
    std::shared_ptr<NestedScrollConnection> nestedScrollConnection;
    bool isPinned = true;
    friend bool operator==(const TopAppBarScrollBehavior&, const TopAppBarScrollBehavior&) = default;
};

// A top app bar's colors (Compose's TopAppBarColors: TopAppBarDefaults.topAppBarColors(containerColor = …)),
// those left unset the theme's.
struct TopAppBarColors {
    std::optional<Color> containerColor, scrolledContainerColor, navigationIconContentColor, titleContentColor, actionIconContentColor,
        subtitleContentColor;
    friend bool operator==(const TopAppBarColors&, const TopAppBarColors&) = default;
};
namespace TopAppBarDefaults {
using topAppBarColors = TopAppBarColors;
using centerAlignedTopAppBarColors = TopAppBarColors;
using mediumTopAppBarColors = TopAppBarColors;
using largeTopAppBarColors = TopAppBarColors;
// Never moves; takes the scrolled container color once content is under it.
TopAppBarScrollBehavior pinnedScrollBehavior(CallSite site = CallSite::current());
TopAppBarScrollBehavior pinnedScrollBehavior(const TopAppBarState& state, CallSite site = CallSite::current());
// Scrolls away with the content and comes back as soon as it scrolls the other way.
TopAppBarScrollBehavior enterAlwaysScrollBehavior(CallSite site = CallSite::current());
TopAppBarScrollBehavior enterAlwaysScrollBehavior(const TopAppBarState& state, CallSite site = CallSite::current());
// Collapses as the content scrolls up and expands only once the content is back at its top
// (for MediumTopAppBar / LargeTopAppBar).
TopAppBarScrollBehavior exitUntilCollapsedScrollBehavior(CallSite site = CallSite::current());
TopAppBarScrollBehavior exitUntilCollapsedScrollBehavior(const TopAppBarState& state, CallSite site = CallSite::current());
// Heights (dp) of the bars.
inline constexpr float TopAppBarExpandedHeight = 64;
inline constexpr float MediumAppBarCollapsedHeight = 64, MediumAppBarExpandedHeight = 112;
inline constexpr float LargeAppBarCollapsedHeight = 64, LargeAppBarExpandedHeight = 152;
// The sides of the window a bar keeps its content clear of: the safe drawing area's top and sides.
WindowInsets windowInsets();
} // namespace TopAppBarDefaults

struct TopAppBarSlots {
    Content title;
    Content subtitle;  // under the title, smaller (M3 Expressive); a medium or large bar grows by 24 dp for it
    Content navigationIcon, actions;
    // Compose's TopAppBarColors: unset ones are the theme's.
    std::optional<Color> containerColor;              // surface
    std::optional<Color> scrolledContainerColor;      // surfaceContainer, once content scrolled under it
    std::optional<Color> navigationIconContentColor;  // onSurface
    std::optional<Color> titleContentColor;           // onSurface
    std::optional<Color> actionIconContentColor;      // onSurfaceVariant
    std::optional<Color> subtitleContentColor;        // onSurfaceVariant
    // dp; unset: TopAppBarDefaults' (a medium or large one 24 more with a subtitle). The single
    // row bars only have an expanded height; a two row one collapses to its collapsed height.
    std::optional<float> expandedHeight, collapsedHeight;
    std::optional<WindowInsets> windowInsets;  // unset: TopAppBarDefaults::windowInsets()
    std::optional<TopAppBarScrollBehavior> scrollBehavior;
    // A two row bar's title, large and collapsed: at the start, in the middle or at the end of
    // the large row (Compose's titleHorizontalAlignment; the collapsed one is centered or at the start).
    HorizontalAlignment titleHorizontalAlignment = Alignment::Start;
};
namespace detail {
inline void applyColors(TopAppBarSlots& slots, const TopAppBarColors& colors) {
    for (auto [field, color] : {std::pair{&TopAppBarSlots::containerColor, colors.containerColor},
                                std::pair{&TopAppBarSlots::scrolledContainerColor, colors.scrolledContainerColor},
                                std::pair{&TopAppBarSlots::navigationIconContentColor, colors.navigationIconContentColor},
                                std::pair{&TopAppBarSlots::titleContentColor, colors.titleContentColor},
                                std::pair{&TopAppBarSlots::actionIconContentColor, colors.actionIconContentColor},
                                std::pair{&TopAppBarSlots::subtitleContentColor, colors.subtitleContentColor}})
        if (color) slots.*field = color;
}
} // namespace detail

// Includes the status bar inset.
namespace detail {
void TopAppBar(const TopAppBarSlots& slots, const Modifier& modifier = {}, CallSite site = CallSite::current());
struct TopAppBarImpl {
    using P = Params<names::Title, names::Modifier, names::Subtitle, names::NavigationIcon, names::Actions, names::ExpandedHeight, names::CollapsedHeight, names::WindowInsets, names::ContainerColor, names::ScrolledContainerColor, names::NavigationIconContentColor, names::TitleContentColor, names::ActionIconContentColor, names::SubtitleContentColor, names::ScrollBehavior, names::TitleHorizontalAlignment, names::Colors>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        TopAppBarSlots slots;
        P::set<names::Title>(slots.title, args...);
        P::set<names::Subtitle>(slots.subtitle, args...);
        P::set<names::NavigationIcon>(slots.navigationIcon, args...);
        P::set<names::Actions>(slots.actions, args...);
        P::set<names::ExpandedHeight>(slots.expandedHeight, args...);
        P::set<names::CollapsedHeight>(slots.collapsedHeight, args...);
        P::set<names::WindowInsets>(slots.windowInsets, args...);
        P::set<names::ContainerColor>(slots.containerColor, args...);
        P::set<names::ScrolledContainerColor>(slots.scrolledContainerColor, args...);
        P::set<names::NavigationIconContentColor>(slots.navigationIconContentColor, args...);
        P::set<names::TitleContentColor>(slots.titleContentColor, args...);
        if constexpr (P::given<names::Colors, A...>()) detail::applyColors(slots, P::get<names::Colors>(TopAppBarColors{}, args...));
        P::set<names::ActionIconContentColor>(slots.actionIconContentColor, args...);
        P::set<names::SubtitleContentColor>(slots.subtitleContentColor, args...);
        P::set<names::ScrollBehavior>(slots.scrollBehavior, args...);
        P::set<names::TitleHorizontalAlignment>(slots.titleHorizontalAlignment, args...);
        TopAppBar(slots, P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::TopAppBarImpl> TopAppBar;
// Two rows: the title starts large below the icons (112 / 152 dp) and moves up into the top row
// as the bar collapses to 64 dp with exitUntilCollapsedScrollBehavior.
namespace detail {
void MediumTopAppBar(const TopAppBarSlots& slots, const Modifier& modifier = {}, CallSite site = CallSite::current());
struct MediumTopAppBarImpl {
    using P = Params<names::Title, names::Modifier, names::Subtitle, names::NavigationIcon, names::Actions, names::ExpandedHeight, names::CollapsedHeight, names::WindowInsets, names::ContainerColor, names::ScrolledContainerColor, names::NavigationIconContentColor, names::TitleContentColor, names::ActionIconContentColor, names::SubtitleContentColor, names::ScrollBehavior, names::TitleHorizontalAlignment, names::Colors>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        TopAppBarSlots slots;
        P::set<names::Title>(slots.title, args...);
        P::set<names::Subtitle>(slots.subtitle, args...);
        P::set<names::NavigationIcon>(slots.navigationIcon, args...);
        P::set<names::Actions>(slots.actions, args...);
        P::set<names::ExpandedHeight>(slots.expandedHeight, args...);
        P::set<names::CollapsedHeight>(slots.collapsedHeight, args...);
        P::set<names::WindowInsets>(slots.windowInsets, args...);
        P::set<names::ContainerColor>(slots.containerColor, args...);
        P::set<names::ScrolledContainerColor>(slots.scrolledContainerColor, args...);
        P::set<names::NavigationIconContentColor>(slots.navigationIconContentColor, args...);
        P::set<names::TitleContentColor>(slots.titleContentColor, args...);
        if constexpr (P::given<names::Colors, A...>()) detail::applyColors(slots, P::get<names::Colors>(TopAppBarColors{}, args...));
        P::set<names::ActionIconContentColor>(slots.actionIconContentColor, args...);
        P::set<names::SubtitleContentColor>(slots.subtitleContentColor, args...);
        P::set<names::ScrollBehavior>(slots.scrollBehavior, args...);
        P::set<names::TitleHorizontalAlignment>(slots.titleHorizontalAlignment, args...);
        MediumTopAppBar(slots, P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::MediumTopAppBarImpl> MediumTopAppBar;
namespace detail {
void LargeTopAppBar(const TopAppBarSlots& slots, const Modifier& modifier = {}, CallSite site = CallSite::current());
struct LargeTopAppBarImpl {
    using P = Params<names::Title, names::Modifier, names::Subtitle, names::NavigationIcon, names::Actions, names::ExpandedHeight, names::CollapsedHeight, names::WindowInsets, names::ContainerColor, names::ScrolledContainerColor, names::NavigationIconContentColor, names::TitleContentColor, names::ActionIconContentColor, names::SubtitleContentColor, names::ScrollBehavior, names::TitleHorizontalAlignment, names::Colors>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        TopAppBarSlots slots;
        P::set<names::Title>(slots.title, args...);
        P::set<names::Subtitle>(slots.subtitle, args...);
        P::set<names::NavigationIcon>(slots.navigationIcon, args...);
        P::set<names::Actions>(slots.actions, args...);
        P::set<names::ExpandedHeight>(slots.expandedHeight, args...);
        P::set<names::CollapsedHeight>(slots.collapsedHeight, args...);
        P::set<names::WindowInsets>(slots.windowInsets, args...);
        P::set<names::ContainerColor>(slots.containerColor, args...);
        P::set<names::ScrolledContainerColor>(slots.scrolledContainerColor, args...);
        P::set<names::NavigationIconContentColor>(slots.navigationIconContentColor, args...);
        P::set<names::TitleContentColor>(slots.titleContentColor, args...);
        if constexpr (P::given<names::Colors, A...>()) detail::applyColors(slots, P::get<names::Colors>(TopAppBarColors{}, args...));
        P::set<names::ActionIconContentColor>(slots.actionIconContentColor, args...);
        P::set<names::SubtitleContentColor>(slots.subtitleContentColor, args...);
        P::set<names::ScrollBehavior>(slots.scrollBehavior, args...);
        P::set<names::TitleHorizontalAlignment>(slots.titleHorizontalAlignment, args...);
        LargeTopAppBar(slots, P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::LargeTopAppBarImpl> LargeTopAppBar;
// M3 Expressive's flexible ones (Compose's MediumFlexibleTopAppBar, LargeFlexibleTopAppBar):
// bigger titles (headlineMedium / displaySmall, subtitles labelLarge / titleMedium), 112 / 120 dp
// tall (136 / 152 with a subtitle), the title anywhere across (titleHorizontalAlignment).
namespace detail {
void MediumFlexibleTopAppBar(const TopAppBarSlots& slots, const Modifier& modifier = {}, CallSite site = CallSite::current());
struct MediumFlexibleTopAppBarImpl {
    using P = Params<names::Title, names::Modifier, names::Subtitle, names::NavigationIcon, names::Actions, names::ExpandedHeight, names::CollapsedHeight, names::WindowInsets, names::ContainerColor, names::ScrolledContainerColor, names::NavigationIconContentColor, names::TitleContentColor, names::ActionIconContentColor, names::SubtitleContentColor, names::ScrollBehavior, names::TitleHorizontalAlignment, names::Colors>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        TopAppBarSlots slots;
        P::set<names::Title>(slots.title, args...);
        P::set<names::Subtitle>(slots.subtitle, args...);
        P::set<names::NavigationIcon>(slots.navigationIcon, args...);
        P::set<names::Actions>(slots.actions, args...);
        P::set<names::ExpandedHeight>(slots.expandedHeight, args...);
        P::set<names::CollapsedHeight>(slots.collapsedHeight, args...);
        P::set<names::WindowInsets>(slots.windowInsets, args...);
        P::set<names::ContainerColor>(slots.containerColor, args...);
        P::set<names::ScrolledContainerColor>(slots.scrolledContainerColor, args...);
        P::set<names::NavigationIconContentColor>(slots.navigationIconContentColor, args...);
        P::set<names::TitleContentColor>(slots.titleContentColor, args...);
        if constexpr (P::given<names::Colors, A...>()) detail::applyColors(slots, P::get<names::Colors>(TopAppBarColors{}, args...));
        P::set<names::ActionIconContentColor>(slots.actionIconContentColor, args...);
        P::set<names::SubtitleContentColor>(slots.subtitleContentColor, args...);
        P::set<names::ScrollBehavior>(slots.scrollBehavior, args...);
        P::set<names::TitleHorizontalAlignment>(slots.titleHorizontalAlignment, args...);
        MediumFlexibleTopAppBar(slots, P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::MediumFlexibleTopAppBarImpl> MediumFlexibleTopAppBar;
namespace detail {
void LargeFlexibleTopAppBar(const TopAppBarSlots& slots, const Modifier& modifier = {}, CallSite site = CallSite::current());
struct LargeFlexibleTopAppBarImpl {
    using P = Params<names::Title, names::Modifier, names::Subtitle, names::NavigationIcon, names::Actions, names::ExpandedHeight, names::CollapsedHeight, names::WindowInsets, names::ContainerColor, names::ScrolledContainerColor, names::NavigationIconContentColor, names::TitleContentColor, names::ActionIconContentColor, names::SubtitleContentColor, names::ScrollBehavior, names::TitleHorizontalAlignment, names::Colors>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        TopAppBarSlots slots;
        P::set<names::Title>(slots.title, args...);
        P::set<names::Subtitle>(slots.subtitle, args...);
        P::set<names::NavigationIcon>(slots.navigationIcon, args...);
        P::set<names::Actions>(slots.actions, args...);
        P::set<names::ExpandedHeight>(slots.expandedHeight, args...);
        P::set<names::CollapsedHeight>(slots.collapsedHeight, args...);
        P::set<names::WindowInsets>(slots.windowInsets, args...);
        P::set<names::ContainerColor>(slots.containerColor, args...);
        P::set<names::ScrolledContainerColor>(slots.scrolledContainerColor, args...);
        P::set<names::NavigationIconContentColor>(slots.navigationIconContentColor, args...);
        P::set<names::TitleContentColor>(slots.titleContentColor, args...);
        if constexpr (P::given<names::Colors, A...>()) detail::applyColors(slots, P::get<names::Colors>(TopAppBarColors{}, args...));
        P::set<names::ActionIconContentColor>(slots.actionIconContentColor, args...);
        P::set<names::SubtitleContentColor>(slots.subtitleContentColor, args...);
        P::set<names::ScrollBehavior>(slots.scrollBehavior, args...);
        P::set<names::TitleHorizontalAlignment>(slots.titleHorizontalAlignment, args...);
        LargeFlexibleTopAppBar(slots, P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::LargeFlexibleTopAppBarImpl> LargeFlexibleTopAppBar;
// A two row bar whose titles style themselves (Compose's TwoRowsTopAppBar, M3 Expressive):
// title(expanded) and subtitle(expanded) are called for the large row (true) and for the
// collapsed top row (false), in the text style around the bar, so each can be its own size or
// say something else. The rest as the other bars take it: icons, colors, heights (unset: 112 dp,
// 136 with a subtitle; 64 collapsed), titleHorizontalAlignment, scrollBehavior.
//   TwoRowsTopAppBar([](bool expanded) { Text("Gelen kutusu", style = TextStyle{.font = expanded ? typography().displaySmall : typography().titleLarge}); },
//                    navigationIcon = ..., scrollBehavior = behavior);
namespace detail {
void TwoRowsTopAppBar(const std::function<void(bool expanded)>& title, const TopAppBarSlots& slots, const Modifier& modifier = {},
                      const std::function<void(bool expanded)>& subtitle = {}, CallSite site = CallSite::current());
struct TwoRowsTopAppBarImpl {
    using P = Params<names::Title, names::Modifier, names::Subtitle, names::NavigationIcon, names::Actions, names::ExpandedHeight, names::CollapsedHeight, names::WindowInsets, names::ContainerColor, names::ScrolledContainerColor, names::NavigationIconContentColor, names::TitleContentColor, names::ActionIconContentColor, names::SubtitleContentColor, names::ScrollBehavior, names::TitleHorizontalAlignment, names::Colors>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::given<names::Title, A...>(), "kompoz: TwoRowsTopAppBar takes a title, [](bool expanded) { … }");
        TopAppBarSlots slots;
        P::set<names::NavigationIcon>(slots.navigationIcon, args...);
        P::set<names::Actions>(slots.actions, args...);
        P::set<names::ExpandedHeight>(slots.expandedHeight, args...);
        P::set<names::CollapsedHeight>(slots.collapsedHeight, args...);
        P::set<names::WindowInsets>(slots.windowInsets, args...);
        P::set<names::ContainerColor>(slots.containerColor, args...);
        P::set<names::ScrolledContainerColor>(slots.scrolledContainerColor, args...);
        P::set<names::NavigationIconContentColor>(slots.navigationIconContentColor, args...);
        P::set<names::TitleContentColor>(slots.titleContentColor, args...);
        if constexpr (P::given<names::Colors, A...>()) detail::applyColors(slots, P::get<names::Colors>(TopAppBarColors{}, args...));
        P::set<names::ActionIconContentColor>(slots.actionIconContentColor, args...);
        P::set<names::SubtitleContentColor>(slots.subtitleContentColor, args...);
        P::set<names::ScrollBehavior>(slots.scrollBehavior, args...);
        P::set<names::TitleHorizontalAlignment>(slots.titleHorizontalAlignment, args...);
        TwoRowsTopAppBar(P::get<names::Title>(std::function<void(bool)>{}, args...), slots, P::get<names::Modifier>(Modifier{}, args...),
                         P::get<names::Subtitle>(std::function<void(bool)>{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::TwoRowsTopAppBarImpl> TwoRowsTopAppBar;
// The title in the middle of the bar (not past the icons on either side).
namespace detail {
void CenterAlignedTopAppBar(const TopAppBarSlots& slots, const Modifier& modifier = {}, CallSite site = CallSite::current());
struct CenterAlignedTopAppBarImpl {
    using P = Params<names::Title, names::Modifier, names::Subtitle, names::NavigationIcon, names::Actions, names::ExpandedHeight, names::CollapsedHeight, names::WindowInsets, names::ContainerColor, names::ScrolledContainerColor, names::NavigationIconContentColor, names::TitleContentColor, names::ActionIconContentColor, names::SubtitleContentColor, names::ScrollBehavior, names::TitleHorizontalAlignment, names::Colors>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        TopAppBarSlots slots;
        P::set<names::Title>(slots.title, args...);
        P::set<names::Subtitle>(slots.subtitle, args...);
        P::set<names::NavigationIcon>(slots.navigationIcon, args...);
        P::set<names::Actions>(slots.actions, args...);
        P::set<names::ExpandedHeight>(slots.expandedHeight, args...);
        P::set<names::CollapsedHeight>(slots.collapsedHeight, args...);
        P::set<names::WindowInsets>(slots.windowInsets, args...);
        P::set<names::ContainerColor>(slots.containerColor, args...);
        P::set<names::ScrolledContainerColor>(slots.scrolledContainerColor, args...);
        P::set<names::NavigationIconContentColor>(slots.navigationIconContentColor, args...);
        P::set<names::TitleContentColor>(slots.titleContentColor, args...);
        if constexpr (P::given<names::Colors, A...>()) detail::applyColors(slots, P::get<names::Colors>(TopAppBarColors{}, args...));
        P::set<names::ActionIconContentColor>(slots.actionIconContentColor, args...);
        P::set<names::SubtitleContentColor>(slots.subtitleContentColor, args...);
        P::set<names::ScrollBehavior>(slots.scrollBehavior, args...);
        P::set<names::TitleHorizontalAlignment>(slots.titleHorizontalAlignment, args...);
        CenterAlignedTopAppBar(slots, P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::CenterAlignedTopAppBarImpl> CenterAlignedTopAppBar;

// The menu an AppBarRow opens for the items that didn't fit (Compose's AppBarMenuState).
class AppBarMenuState {
public:
    bool isShowing() const { return showing_.get(); }  // observable
    void show() const { showing_.set(true); }
    void dismiss() const { showing_.set(false); }
    friend bool operator==(const AppBarMenuState& a, const AppBarMenuState& b) { return a.showing_ == b.showing_; }

private:
    MutableState<bool> showing_{false};
};
// An AppBarRow's items (Compose's AppBarScope): each an icon button in the row, or a line in
// the overflow menu when the row has no room for it.
class AppBarScope {
public:
    void clickableItem(std::function<void()> onClick, Content icon, std::string label, bool enabled = true);
    void toggleableItem(bool checked, std::function<void(bool)> onCheckedChange, Content icon, std::string label, bool enabled = true);
    // Your own: how it shows in the row, and in the menu (dismiss the menu from it when done).
    void customItem(Content appBarContent, std::function<void(const AppBarMenuState&)> menuContent);

    struct Item {
        Content appBar;
        std::function<void(const AppBarMenuState&)> menu;
    };
    std::vector<Item> items;
};
struct AppBarRowOptions {
    // What opens the menu (Compose's overflowIndicator): unset, a ⋮ icon button.
    std::function<void(const AppBarMenuState&)> overflowIndicator;
    // At most this many slots in the row, the menu's button taking the last one when it is needed.
    int maxItemCount = std::numeric_limits<int>::max();
};
// App bar actions that fit (Compose's AppBarRow): as many items as there is room for, the rest
// in a menu behind the overflow button (only when something is left out).
//   TopAppBar(title = ..., actions = [] {
//       AppBarRow([](AppBarScope& s) {
//           s.clickableItem(share, [] { Icon(Icons::Filled::Share, "Paylaş"); }, "Paylaş");
//           s.toggleableItem(liked, setLiked, [] { Icon(Icons::Filled::Favorite, "Beğen"); }, "Beğen");
//       });
//   });
void AppBarRow(std::function<void(AppBarScope&)> content, const Modifier& modifier = {}, AppBarRowOptions options = {},
               CallSite site = CallSite::current());
// The same down a column (Compose's AppBarColumn): for a rail or a vertical toolbar, the menu's
// button at the bottom.
void AppBarColumn(std::function<void(AppBarScope&)> content, const Modifier& modifier = {}, AppBarRowOptions options = {},
                  CallSite site = CallSite::current());

// ---- Date picker ------------------------------------------------------------------------------

// The names and formats a date picker shows: DatePickerStrings, in strings.hpp.


// How a date picker takes a date (Compose's DisplayMode): from the calendar, or typed in a field.
enum class DisplayMode { Picker, Input };

// The selected date and the month on show, observable. Dates are std::chrono calendar dates.
class DatePickerState {
public:
    explicit DatePickerState(std::optional<std::chrono::year_month_day> initialSelectedDate = {},
                             std::optional<std::chrono::year_month> initialDisplayedMonth = {},
                             std::pair<int, int> yearRange = {1900, 2100}, DisplayMode initialDisplayMode = DisplayMode::Picker);
    DisplayMode displayMode() const;  // observable
    void setDisplayMode(DisplayMode mode) const;
    std::optional<std::chrono::year_month_day> selectedDate() const;
    void setSelectedDate(std::optional<std::chrono::year_month_day> date) const;
    std::chrono::year_month displayedMonth() const;
    void setDisplayedMonth(std::chrono::year_month month) const;  // kept within the year range
    std::pair<int, int> yearRange() const;
    friend bool operator==(const DatePickerState& a, const DatePickerState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};
template <>
struct Saver<DatePickerState> {
    static SavedValue save(const DatePickerState& s);
    static std::optional<DatePickerState> restore(const SavedValue& v);
};
DatePickerState rememberDatePickerState(std::optional<std::chrono::year_month_day> initialSelectedDate = {},
                                        std::optional<std::chrono::year_month> initialDisplayedMonth = {},
                                        std::pair<int, int> yearRange = {1900, 2100},
                                        DisplayMode initialDisplayMode = DisplayMode::Picker, CallSite site = CallSite::current());

struct DatePickerOptions {
    std::optional<DatePickerStrings> strings;  // unset: LocalStrings's (the system's language)
    std::optional<std::chrono::year_month_day> today;  // outlined in the grid; by default the device's date
    bool showModeToggle = true;  // a button by the headline between the calendar and typing the date
};

// Material 3 date picker: the selected date on top, a month's days to pick from (swipe or the
// arrows for other months, the month name for a year grid). Going to another month takes a
// picked date along (same day, or the month's last), so the day, month and year can be
// picked in any order; setDisplayedMonth only shows a month. At most 360 dp wide.
// DisplayMode::Input (the pencil by the headline, or the state's mode): the date typed in an
// outlined field, digits only (the separators show by themselves, MM/DD/YYYY or the strings'
// pattern), picked once it is whole and valid; a date that isn't, or is out of the year range,
// says so under the field.
void DatePicker(const DatePickerState& state, const Modifier& modifier = {}, DatePickerOptions options = {},
                CallSite site = CallSite::current());
// A date picker in a dialog with its buttons (confirm and dismiss), as Compose's DatePickerDialog.
void DatePickerDialog(std::function<void()> onDismissRequest, const Content& confirmButton, const Content& dismissButton,
                      const Content& content, CallSite site = CallSite::current());

// The first and last day of a range and the month on show, observable. A range is complete
// when it has both; the end is never before the start.
class DateRangePickerState {
public:
    explicit DateRangePickerState(std::optional<std::chrono::year_month_day> initialSelectedStartDate = {},
                                  std::optional<std::chrono::year_month_day> initialSelectedEndDate = {},
                                  std::optional<std::chrono::year_month> initialDisplayedMonth = {},
                                  std::pair<int, int> yearRange = {1900, 2100}, DisplayMode initialDisplayMode = DisplayMode::Picker);
    DisplayMode displayMode() const;  // observable
    void setDisplayMode(DisplayMode mode) const;
    std::optional<std::chrono::year_month_day> selectedStartDate() const;
    std::optional<std::chrono::year_month_day> selectedEndDate() const;
    // An end before the start is dropped.
    void setSelection(std::optional<std::chrono::year_month_day> start, std::optional<std::chrono::year_month_day> end) const;
    std::chrono::year_month displayedMonth() const;
    void setDisplayedMonth(std::chrono::year_month month) const;  // kept within the year range
    std::pair<int, int> yearRange() const;
    friend bool operator==(const DateRangePickerState& a, const DateRangePickerState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};
template <>
struct Saver<DateRangePickerState> {
    static SavedValue save(const DateRangePickerState& s);
    static std::optional<DateRangePickerState> restore(const SavedValue& v);
};
DateRangePickerState rememberDateRangePickerState(std::optional<std::chrono::year_month_day> initialSelectedStartDate = {},
                                                  std::optional<std::chrono::year_month_day> initialSelectedEndDate = {},
                                                  std::optional<std::chrono::year_month> initialDisplayedMonth = {},
                                                  std::pair<int, int> yearRange = {1900, 2100},
                                                  DisplayMode initialDisplayMode = DisplayMode::Picker,
                                                  CallSite site = CallSite::current());

// A date range picker: the start and the end on top as two tabs, below them DatePicker's
// calendar (arrows, swipe, a year grid) for the one being picked, which works like a
// DatePicker; the end can't go before the start (a start moved past it drops it). The days
// between are banded. (Compose's lists the months vertically, without tabs.) DisplayMode::Input:
// the start and the end typed side by side, as DatePicker's field; a start after the end (or
// an end before the start) says so instead of picking it.
void DateRangePicker(const DateRangePickerState& state, const Modifier& modifier = {}, DatePickerOptions options = {},
                     CallSite site = CallSite::current());

// ---- Time picker -------------------------------------------------------------------------------

enum class TimePickerSelectionMode { Hour, Minute };
enum class TimePickerLayoutType { Vertical, Horizontal };

// The picked time (hour 0..23, minute 0..59) and which of them the dial sets, observable.
// Kompoz has no locale data: the clock is 24-hour unless asked otherwise.
class TimePickerState {
public:
    explicit TimePickerState(int initialHour = 0, int initialMinute = 0, bool is24Hour = true);
    int hour() const;
    void setHour(int hour) const;  // wraps into 0..23
    int minute() const;
    void setMinute(int minute) const;  // wraps into 0..59
    bool is24hour() const;
    bool isAfternoon() const { return hour() >= 12; }
    TimePickerSelectionMode selection() const;
    void setSelection(TimePickerSelectionMode mode) const;
    friend bool operator==(const TimePickerState& a, const TimePickerState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};
template <>
struct Saver<TimePickerState> {
    static SavedValue save(const TimePickerState& s);
    static std::optional<TimePickerState> restore(const SavedValue& v);
};
TimePickerState rememberTimePickerState(int initialHour = 0, int initialMinute = 0, bool is24Hour = true,
                                        CallSite site = CallSite::current());

struct TimePickerOptions {
    std::optional<std::string> am, pm;  // the 12-hour clock's period toggle; unset: LocalStrings's
    std::optional<std::string> hourLabel, minuteLabel;  // under TimeInput's fields, and for accessibility; unset: LocalStrings's
    std::optional<TimePickerLayoutType> layoutType;  // by default side by side in a landscape window
};

// Material 3 time picker: the hour and minute (tap one to set it) above a clock dial; tap or
// drag on the dial, and a picked hour moves on to the minutes. The 24-hour dial has the
// hours 13..23 and 00 on an inner ring.
void TimePicker(const TimePickerState& state, const Modifier& modifier = {}, TimePickerOptions options = {},
                CallSite site = CallSite::current());
// The same time typed into two number fields (Compose's TimeInput).
void TimeInput(const TimePickerState& state, const Modifier& modifier = {}, TimePickerOptions options = {},
               CallSite site = CallSite::current());

struct TimePickerDialogSlots {
    Content title, confirmButton, dismissButton;
    Content modeToggleButton;  // bottom left: switches between TimePicker and TimeInput
};
// A time picker in a dialog with its title and buttons, as Compose's TimePickerDialog.
void TimePickerDialog(std::function<void()> onDismissRequest, const TimePickerDialogSlots& slots, const Content& content,
                      CallSite site = CallSite::current());

// ---- Search bar ------------------------------------------------------------------------------

namespace SearchBarDefaults {
struct InputFieldOptions {
    std::string query;
    std::function<void(std::string)> onQueryChange;
    std::function<void(std::string)> onSearch;  // the keyboard's search key
    bool expanded = false;
    std::function<void(bool)> onExpandedChange;
    std::string placeholder;
    Content leadingIcon, trailingIcon;
};
// The search bar's field: the query (or placeholder) that expands the bar when tapped, and an
// editable, focused field while expanded.
void InputField(const InputFieldOptions& options, const Modifier& modifier = {}, CallSite site = CallSite::current());
} // namespace SearchBarDefaults

// Material 3 search bar: a pill in the layout that expands into a search view over the whole
// window (the field on top, `content` such as suggestions below); back closes it.
//   SearchBar([&] { SearchBarDefaults::InputField({.query = q.get(), .onQueryChange = ..., .onSearch = ...,
//                                                  .expanded = open.get(), .onExpandedChange = ...}); },
//             open.get(), [open](bool v) { open.set(v); }, Modifier().fillMaxWidth(), [&] { ...suggestions... });
void SearchBar(const Content& inputField, bool expanded, std::function<void(bool)> onExpandedChange,
               const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
// A search bar that opens in place (Compose's DockedSearchBar): the field stays where it is and
// `content` opens under it at the bar's width, over the page (240 dp to two thirds of the
// window tall); a tap outside or back closes it. Same arguments as SearchBar.
void DockedSearchBar(const Content& inputField, bool expanded, std::function<void(bool)> onExpandedChange,
                     const Modifier& modifier, const Content& content, CallSite site = CallSite::current());

// The search bar as a state (Compose's SearchBarState): collapsed (the bar in the layout) or
// expanded (ExpandedFullScreenSearchBar / ExpandedDockedSearchBar over the page), animated
// between; the query lives in a TextFieldState.
//   auto state = rememberSearchBarState();
//   auto text = rememberTextFieldState();
//   auto scope = rememberCoroutineScope();
//   auto field = [&] {
//       SearchBarDefaults::InputField(state, text, {.onSearch = [state, scope](std::string) {
//           scope.launch([state]() -> Task { co_await state.animateToCollapsed(); });
//       }, .placeholder = "Ara"});
//   };
//   SearchBar(state, field, Modifier().fillMaxWidth());
//   ExpandedFullScreenSearchBar(state, field, [&] { ...results... });
enum class SearchBarValue { Collapsed, Expanded };
class SearchBarState {
public:
    explicit SearchBarState(SearchBarValue initialValue = SearchBarValue::Collapsed);
    SearchBarValue currentValue() const;  // where it last settled (observable)
    SearchBarValue targetValue() const;   // where it is going (observable)
    float progress() const;               // 0 collapsed .. 1 expanded (observable)
    Task animateToExpanded() const;
    Task animateToCollapsed() const;
    void snapTo(SearchBarValue value) const;

    friend bool operator==(const SearchBarState& a, const SearchBarState& b) { return a.data_ == b.data_; }
    struct Data;
    std::shared_ptr<Data> data_;
};
template <>
struct Saver<SearchBarState> {
    static SavedValue save(const SearchBarState& s);
    static std::optional<SearchBarState> restore(const SavedValue& v);
};
SearchBarState rememberSearchBarState(SearchBarValue initialValue = SearchBarValue::Collapsed, CallSite site = CallSite::current());
namespace SearchBarDefaults {
// The field of a SearchBarState: collapsed, the query (or placeholder), expanding the bar when
// tapped; expanded, the focused field editing `textFieldState` (the keyboard's search key calls
// onSearch with it). Of `options`, onSearch, placeholder and the icons count.
void InputField(const SearchBarState& state, const TextFieldState& textFieldState, const InputFieldOptions& options = {},
                const Modifier& modifier = {}, CallSite site = CallSite::current());
} // namespace SearchBarDefaults
// The collapsed bar of a SearchBarState: the pill in the layout (empty while the bar is expanded).
void SearchBar(const SearchBarState& state, const Content& inputField, const Modifier& modifier = {}, CallSite site = CallSite::current());
// The expanded search over the whole window (the field on top, `content` below), fading in and
// out with the state's progress; back collapses it.
void ExpandedFullScreenSearchBar(const SearchBarState& state, const Content& inputField, const Content& content,
                                 CallSite site = CallSite::current());
// The expanded search in place: over the collapsed bar at its width, `content` under the field
// (240 dp to two thirds of the window tall); a tap outside or back collapses it.
void ExpandedDockedSearchBar(const SearchBarState& state, const Content& inputField, const Content& content,
                             CallSite site = CallSite::current());

// A search bar as the top app bar (Compose's AppBarWithSearch): the collapsed SearchBar between a
// navigation icon and actions, under the status bar, 72 dp tall. With a scroll behavior on the
// scrolling content's parent it slides away as the content scrolls on and comes back first as it
// scrolls back (enterAlwaysSearchBarScrollBehavior).
//   auto scroll = SearchBarDefaults::enterAlwaysSearchBarScrollBehavior();
//   Scaffold(Modifier().nestedScroll(scroll.nestedScrollConnection), topBar = [&] {
//       AppBarWithSearch(state, field, Modifier(), {.scrollBehavior = scroll});
//   }, ...);
//   ExpandedFullScreenSearchBar(state, field, results);
struct AppBarWithSearchSlots {
    Content navigationIcon, actions;
    std::optional<TopAppBarScrollBehavior> scrollBehavior;
};
void AppBarWithSearch(const SearchBarState& state, const Content& inputField, const Modifier& modifier = {},
                      const AppBarWithSearchSlots& slots = {}, CallSite site = CallSite::current());
namespace SearchBarDefaults {
TopAppBarScrollBehavior enterAlwaysSearchBarScrollBehavior(CallSite site = CallSite::current());
} // namespace SearchBarDefaults
// The search bar alone as the top bar (M3's TopSearchBar): the collapsed SearchBar under the
// status bar, 8 dp in from the window's edges, at most 720 dp wide in the middle, nothing
// behind it but the content; with a scroll behavior it slides away and back as AppBarWithSearch.
void TopSearchBar(const SearchBarState& state, const Content& inputField, const Modifier& modifier = {},
                  std::optional<TopAppBarScrollBehavior> scrollBehavior = {}, CallSite site = CallSite::current());

// ---- Menu bar ----------------------------------------------------------------------------------
//
// A window's menus (Compose Multiplatform's MenuBar), drawn as a strip at the top of the window's
// content: a click opens a menu under its title; while one is open, the mouse moving to another
// title opens that one; a click outside or Escape closes it. Items show their shortcuts, which work
// whenever the window has the keys (after the focused text field and the handlers around it).
//   MenuBar({{"Dosya", {{.text = "Aç", .onClick = open, .shortcut = KeyShortcut{Key::O, true}},
//                       MenuBarItem::Separator(),
//                       {.text = "Satır numaraları", .onClick = toggle, .checked = numbers.get()},
//                       {.text = "Son açılanlar", .submenu = {{.text = "notlar.txt", .onClick = ...}}}}}});
struct KeyShortcut {
    Key key = Key::Unknown;
    bool ctrl = false, shift = false, alt = false;
    bool matches(const KeyEvent& event) const;
    std::string text() const;  // "Ctrl+Shift+S"
    friend bool operator==(const KeyShortcut&, const KeyShortcut&) = default;
};
struct MenuBarItem {
    std::string text;
    std::function<void()> onClick;
    std::optional<KeyShortcut> shortcut;
    bool enabled = true;
    std::optional<bool> checked;       // a check box item: ticked when true
    std::vector<MenuBarItem> submenu;  // not empty: opens beside the item
    bool separator = false;
    static MenuBarItem Separator() {
        MenuBarItem item;
        item.separator = true;
        return item;
    }
};
struct MenuBarMenu {
    std::string text;
    std::vector<MenuBarItem> items;
};
void MenuBar(const std::vector<MenuBarMenu>& menus, const Modifier& modifier = {}, CallSite site = CallSite::current());

// ---- Tooltips ----------------------------------------------------------------------------------

// Whether a tooltip shows. A persistent one (a rich tooltip with an action) stays until a tap
// outside; the others go away by themselves after 1.5 s, or when the mouse leaves. One shows at a
// time: showing one dismisses another that shows (Compose's global MutatorMutex).
class TooltipState {
public:
    explicit TooltipState(bool initialIsVisible = false, bool isPersistent = false);
    bool isVisible() const;  // observable
    bool isPersistent() const;
    void show() const;
    void dismiss() const;
    friend bool operator==(const TooltipState& a, const TooltipState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};
TooltipState rememberTooltipState(bool initialIsVisible = false, bool isPersistent = false, CallSite site = CallSite::current());

// Shows `tooltip` over `content` (above it, or below when there is no room) on a long press,
// or while a mouse rests on it; a long press doesn't click what is under it.
//   TooltipBox([] { PlainTooltip([] { Text("Favorilere ekle"); }); }, rememberTooltipState(), Modifier(),
//              [&] { IconButton(add, [] { Icon(Icons::Filled::Favorite, "Favori"); }); });
void TooltipBox(const Content& tooltip, const TooltipState& state, const Modifier& modifier, const Content& content,
                CallSite site = CallSite::current());
// Which side of its anchor a tooltip goes (M3's TooltipAnchorPosition): to the other one when
// there is no room; Start and End follow the layout direction.
enum class TooltipAnchorPosition { Above, Below, Left, Right, Start, End };
// Where a TooltipBox puts its tooltip (M3's position provider from
// TooltipDefaults::rememberTooltipPositionProvider): on that side, this far from the anchor
// (and a caret's height more), centered on it across.
struct TooltipPositionProvider {
    TooltipAnchorPosition positioning = TooltipAnchorPosition::Above;
    float spacingBetweenTooltipAndAnchor = 4;
};
void TooltipBox(const TooltipPositionProvider& positionProvider, const Content& tooltip, const TooltipState& state,
                const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
// Foundation's tooltip box (Compose's BasicTooltipBox): `tooltip` in a popup at `position` (given
// the anchor, the tooltip's size and the window's, in dp) while `state` shows it, at once — no
// animation, no caret. `enableUserInput`: a long press or a resting mouse shows it; `focusable`: it
// takes the input while it shows (a tap outside dismisses it).
using BasicTooltipState = TooltipState;
using TooltipPosition = std::function<Offset(const Rect& anchor, Size tooltip, Size window)>;
// Foundation's default state is persistent (Material's isn't).
BasicTooltipState rememberBasicTooltipState(bool initialIsVisible = false, bool isPersistent = true, CallSite site = CallSite::current());
void BasicTooltipBox(const TooltipPosition& position, const Content& tooltip, const BasicTooltipState& state, const Modifier& modifier,
                     const Content& content, bool focusable = true, bool enableUserInput = true, CallSite site = CallSite::current());
// A short label on inverseSurface, at most 200 dp wide.
void PlainTooltip(const Content& content, const Modifier& modifier = {}, CallSite site = CallSite::current());
// A label over `content` (Compose's Label): `label` (a PlainTooltip, say) shown above it, where a
// tooltip would be, while the interaction source tells of a press, a drag or the mouse over it —
// or all the time, isPersistent. It takes no input.
//   Label([&] { PlainTooltip([&] { Text(std::format("{:.0f}", v)); }); }, Modifier(), source, false, [&] { ... });
namespace detail {
void Label(const Content& label, const Modifier& modifier, std::optional<MutableInteractionSource> interactionSource, bool isPersistent,
           const Content& content, CallSite site = CallSite::current());
// Label(label = { PlainTooltip { … } }, interactionSource = …, isPersistent = …) { … } as Compose writes it (and by place as above).
struct LabelImpl {
    using P = Params<names::Label, names::Modifier, names::InteractionSource, names::IsPersistent, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: Label takes a block: what it labels");
        Label(Content(P::arg<names::Label>(args...)), P::get<names::Modifier>(Modifier{}, args...),
              P::get<names::InteractionSource>(std::optional<MutableInteractionSource>(), args...), P::get<names::IsPersistent>(false, args...),
              P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::LabelImpl> Label;
struct RichTooltipSlots {
    Content title, text, action;  // action: e.g. a TextButton (make the state persistent)
};
// A card with an optional title, the text and an optional action, at most 320 dp wide.
void RichTooltip(const RichTooltipSlots& slots, const Modifier& modifier = {}, CallSite site = CallSite::current());
// With a caret (Compose's caretSize): a small triangle on the side towards the anchor, pointing
// at its middle (in a TooltipBox; the tooltip moves away from the anchor by its height).
namespace TooltipDefaults {
inline constexpr Size caretSize{16, 8};
inline TooltipPositionProvider rememberTooltipPositionProvider(TooltipAnchorPosition positioning = TooltipAnchorPosition::Above,
                                                               float spacingBetweenTooltipAndAnchor = 4) {
    return {positioning, spacingBetweenTooltipAndAnchor};
}
inline TooltipPositionProvider rememberPlainTooltipPositionProvider(float spacingBetweenTooltipAndAnchor = 4) {
    return {TooltipAnchorPosition::Above, spacingBetweenTooltipAndAnchor};
}
inline TooltipPositionProvider rememberRichTooltipPositionProvider(float spacingBetweenTooltipAndAnchor = 4) {
    return {TooltipAnchorPosition::Above, spacingBetweenTooltipAndAnchor};
}
} // namespace TooltipDefaults
void PlainTooltip(const Content& content, const Modifier& modifier, Size caretSize, CallSite site = CallSite::current());
void RichTooltip(const RichTooltipSlots& slots, const Modifier& modifier, Size caretSize, CallSite site = CallSite::current());

// ---- Pull to refresh -------------------------------------------------------------------------

namespace PullToRefreshDefaults {
inline constexpr float PositionalThreshold = 80;                    // dp of indicator travel that refreshes
inline constexpr float IndicatorMaxDistance = PositionalThreshold;  // where the indicator stands at the threshold
inline constexpr float Elevation = 3;                               // Material level 2
inline const Shape indicatorShape = CircleShape;
Color indicatorContainerColor();  // surfaceContainerHigh
Color indicatorColor();           // onSurfaceVariant
} // namespace PullToRefreshDefaults

// How far a pull to refresh has brought its indicator down: 0 hidden, 1 at the threshold (up to
// 2, pulled further). Modifier::pullToRefresh drives it; an indicator follows it.
class PullToRefreshState {
public:
    PullToRefreshState();
    float distanceFraction() const;  // observable
    bool isAnimating() const;
    Task animateToThreshold() const;
    Task animateToHidden() const;
    void snapTo(float fraction) const;
    friend bool operator==(const PullToRefreshState& a, const PullToRefreshState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};
PullToRefreshState rememberPullToRefreshState(CallSite site = CallSite::current());

struct PullToRefreshOptions {
    std::optional<PullToRefreshState> state;
    float threshold = PullToRefreshDefaults::PositionalThreshold;
    bool enabled = true;
    Alignment contentAlignment = Alignment::TopStart;
    // Drawn over the content; by default PullToRefreshDefaults::Indicator at the top center.
    std::function<void(const PullToRefreshState& state, bool isRefreshing)> indicator;
};

// Pull the scrollable content down at its top and let go past the threshold: onRefresh runs
// (set isRefreshing until done; the indicator spins meanwhile). A Box with Modifier::pullToRefresh.
//   PullToRefreshBox(refreshing.get(), [=] { reload(); }, Modifier().fillMaxSize(), [&] {
//       LazyColumn(Modifier().fillMaxSize(), ...);
//   });
namespace detail {
void PullToRefreshBox(bool isRefreshing, std::function<void()> onRefresh, const Modifier& modifier, const Content& content,
                      PullToRefreshOptions options, CallSite site);
// PullToRefreshBox(isRefreshing, onRefresh, modifier, state = …, contentAlignment = …, enabled = …) { … },
// as Compose's (indicator: Kompoz's, given the state and whether it is refreshing).
struct PullToRefreshBoxImpl {
    using P = Params<names::IsRefreshing, names::OnRefresh, names::Modifier, names::State, names::ContentAlignment, names::Enabled,
                     names::Indicator, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        PullToRefreshOptions options;
        P::set<names::State>(options.state, args...);
        P::set<names::ContentAlignment>(options.contentAlignment, args...);
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::Indicator>(options.indicator, args...);
        PullToRefreshBox(bool(P::arg<names::IsRefreshing>(args...)), std::function<void()>(P::arg<names::OnRefresh>(args...)),
                         P::get<names::Modifier>(Modifier{}, args...), P::plainBlock(std::forward<A>(args)...), options, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::PullToRefreshBoxImpl> PullToRefreshBox;

namespace PullToRefreshDefaults {
// The circle an indicator sits in: it comes out from under the top edge of where it is placed
// (nothing of it shows above that) as the state goes from 0 to 1, to `maxDistance` dp below it,
// with a shadow while out. No container color by default.
void IndicatorBox(const PullToRefreshState& state, bool isRefreshing, const Modifier& modifier, const Content& content,
                  float maxDistance = IndicatorMaxDistance, Shape shape = indicatorShape,
                  std::optional<Color> containerColor = {}, float elevation = Elevation, CallSite site = CallSite::current());
// An arc that grows with the pull, with an arrowhead, faint until the threshold; a spinner while
// refreshing (cross-fading between them).
void Indicator(const PullToRefreshState& state, bool isRefreshing, const Modifier& modifier = {},
               std::optional<Color> containerColor = {}, std::optional<Color> color = {},
               float maxDistance = IndicatorMaxDistance, CallSite site = CallSite::current());
} // namespace PullToRefreshDefaults

// Includes the navigation bar inset; put NavigationBarItems in it.
namespace detail {
void NavigationBar(const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
struct NavigationBarImpl {
    using P = Params<names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: NavigationBar takes a block: its content");
        NavigationBar(P::get<names::Modifier>(Modifier{}, args...), P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::NavigationBarImpl> NavigationBar;
namespace detail {
void NavigationBarItem(bool selected, std::function<void()> onClick, const ImageVector& icon, std::string label,
                       CallSite site = CallSite::current());
// Compose's: icon = { Icon(…) }, label = { Text("…") } (its own content), enabled.
void NavigationBarItem(bool selected, std::function<void()> onClick, const Content& icon, const Content& label, bool enabled,
                       CallSite site = CallSite::current());
struct NavigationBarItemImpl {
    template <class... A>
    static void call(CallSite site, A&&... args) {
        using P = Params<names::Selected, names::OnClick, names::Icon, names::Label, names::Enabled>;
        static_assert(P::check<A...>());
        bool selected = P::arg<names::Selected>(args...);
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        auto&& icon = P::arg<names::Icon>(args...);
        if constexpr (std::is_convertible_v<decltype(icon), const ImageVector&>)
            NavigationBarItem(selected, std::move(onClick), icon, P::get<names::Label>(std::string(), args...), site);
        else
            NavigationBarItem(selected, std::move(onClick), Content(icon), P::get<names::Label>(Content{}, args...),
                              P::get<names::Enabled>(true, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::NavigationBarItemImpl> NavigationBarItem;

// M3 Expressive's short navigation bar (Compose's ShortNavigationBar and ShortNavigationBarItem):
// 64 dp tall. Its items share the width, the icon above the label (EqualWeight, a compact window's
// default), or sit together in the middle, as wide as the widest, the label after the icon
// (Centered, from a medium window on). The indicator grows from the middle behind the icon (round
// icon and label side by side); the ripple keeps to it.
enum class ShortNavigationBarArrangement { EqualWeight, Centered };
enum class NavigationItemIconPosition { Top, Start };
struct ShortNavigationBarOptions {
    std::optional<ShortNavigationBarArrangement> arrangement;  // by the window's width class
    std::optional<Color> containerColor;                       // surfaceContainer
    std::optional<WindowInsets> windowInsets;                  // the system bars' and cutout's sides and bottom
};
namespace detail {
void ShortNavigationBar(const Modifier& modifier, const Content& content, const ShortNavigationBarOptions& options = {},
                        CallSite site = CallSite::current());
struct ShortNavigationBarImpl {
    using P = Params<names::Modifier, names::ContainerColor, names::WindowInsets, names::Arrangement, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: ShortNavigationBar takes a block: its content");
        ShortNavigationBarOptions options;
        P::set<names::ContainerColor>(options.containerColor, args...);
        P::set<names::WindowInsets>(options.windowInsets, args...);
        P::set<names::Arrangement>(options.arrangement, args...);
        ShortNavigationBar(P::get<names::Modifier>(Modifier{}, args...), P::block(std::forward<A>(args)...), options, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::ShortNavigationBarImpl> ShortNavigationBar;
struct ShortNavigationBarItemOptions {
    std::optional<NavigationItemIconPosition> iconPosition;  // Top in EqualWeight bars, Start in Centered ones
    bool enabled = true;
    std::optional<MutableInteractionSource> interactionSource;
};
namespace detail {
void ShortNavigationBarItem(bool selected, std::function<void()> onClick, const Content& icon, const Content& label,
                            const Modifier& modifier = {}, const ShortNavigationBarItemOptions& options = {},
                            CallSite site = CallSite::current());
struct ShortNavigationBarItemImpl {
    template <class... A>
    static void call(CallSite site, A&&... args) {
        using P = Params<names::Selected, names::OnClick, names::Icon, names::Label, names::Modifier, names::Enabled, names::IconPosition, names::InteractionSource>;
        static_assert(P::check<A...>());
        bool selected = P::arg<names::Selected>(args...);
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::given<names::Icon, A...>(), "kompoz: ShortNavigationBarItem takes an icon");
        ShortNavigationBarItemOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::IconPosition>(options.iconPosition, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        ShortNavigationBarItem(selected, std::move(onClick), P::get<names::Icon>(Content{}, args...), P::get<names::Label>(Content{}, args...),
            P::get<names::Modifier>(Modifier{}, args...), options, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::ShortNavigationBarItemImpl> ShortNavigationBarItem;

// M3 Expressive's wide navigation rail (Compose's WideNavigationRail): collapsed, 96 dp wide with
// each icon above its label; expanded (its state's expand(), often from a menu button in its
// header), it widens and the labels go after the icons. Its items follow its state on their own.
//   auto rail = rememberWideNavigationRailState();
//   WideNavigationRail(Modifier(), state = rail, header = [rail] { IconButton([rail] { rail.toggle(); }, ...); },
//                      [=] { WideNavigationRailItem(...); ... });
class WideNavigationRailState {
public:
    explicit WideNavigationRailState(bool expanded = false);
    bool isExpanded() const;  // observable: where it is going
    void expand() const;
    void collapse() const;
    void toggle() const;
    friend bool operator==(const WideNavigationRailState& a, const WideNavigationRailState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};
template <>
struct Saver<WideNavigationRailState> {
    static SavedValue save(const WideNavigationRailState& s);
    static std::optional<WideNavigationRailState> restore(const SavedValue& v);
};
WideNavigationRailState rememberWideNavigationRailState(bool initiallyExpanded = false, CallSite site = CallSite::current());
struct WideNavigationRailOptions {
    std::optional<WideNavigationRailState> state;
    Content header;                       // above the items, 40 dp apart
    std::optional<Color> containerColor;  // surface
    float collapsedWidth = 96, expandedWidth = 220;
    std::optional<WindowInsets> windowInsets;  // the system bars' and cutout's start, top and bottom
    Arrangement::Kind arrangement = Arrangement::Kind::Start;  // where the items go: Start (top), Center, End (bottom)
};
namespace detail {
void WideNavigationRail(const Modifier& modifier, const Content& content, const WideNavigationRailOptions& options = {},
                        CallSite site = CallSite::current());
struct WideNavigationRailImpl {
    using P = Params<names::Modifier, names::State, names::Header, names::ContainerColor, names::CollapsedWidth, names::ExpandedWidth, names::WindowInsets, names::Arrangement, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: WideNavigationRail takes a block: its content");
        WideNavigationRailOptions options;
        P::set<names::State>(options.state, args...);
        P::set<names::Header>(options.header, args...);
        P::set<names::ContainerColor>(options.containerColor, args...);
        P::set<names::CollapsedWidth>(options.collapsedWidth, args...);
        P::set<names::ExpandedWidth>(options.expandedWidth, args...);
        P::set<names::WindowInsets>(options.windowInsets, args...);
        P::set<names::Arrangement>(options.arrangement, args...);
        WideNavigationRail(P::get<names::Modifier>(Modifier{}, args...), P::block(std::forward<A>(args)...), options, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::WideNavigationRailImpl> WideNavigationRail;
struct WideNavigationRailItemOptions {
    std::optional<bool> railExpanded;  // the rail's state
    bool enabled = true;
    std::optional<MutableInteractionSource> interactionSource;
};
namespace detail {
void WideNavigationRailItem(bool selected, std::function<void()> onClick, const Content& icon, const Content& label,
                            const Modifier& modifier = {}, const WideNavigationRailItemOptions& options = {},
                            CallSite site = CallSite::current());
struct WideNavigationRailItemImpl {
    template <class... A>
    static void call(CallSite site, A&&... args) {
        using P = Params<names::Selected, names::OnClick, names::Icon, names::Label, names::Modifier, names::Enabled, names::RailExpanded, names::InteractionSource>;
        static_assert(P::check<A...>());
        bool selected = P::arg<names::Selected>(args...);
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::given<names::Icon, A...>(), "kompoz: WideNavigationRailItem takes an icon");
        WideNavigationRailItemOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::RailExpanded>(options.railExpanded, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        WideNavigationRailItem(selected, std::move(onClick), P::get<names::Icon>(Content{}, args...), P::get<names::Label>(Content{}, args...),
            P::get<names::Modifier>(Modifier{}, args...), options, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::WideNavigationRailItemImpl> WideNavigationRailItem;
// The modal one (Compose's ModalWideNavigationRail): collapsed, a rail in place (or nothing,
// hideOnCollapse); expanded, it slides in from the start over everything (surfaceContainer, its
// end corners round) above a scrim, and a tap on the scrim or Back collapses it. The content
// leaves room only for the collapsed rail.
struct ModalWideNavigationRailOptions {
    std::optional<WideNavigationRailState> state;
    Content header;
    bool hideOnCollapse = false;
    float collapsedWidth = 96, expandedWidth = 220;
    std::optional<WindowInsets> windowInsets;
};
namespace detail {
void ModalWideNavigationRail(const Modifier& modifier, const Content& content, const ModalWideNavigationRailOptions& options = {},
                             CallSite site = CallSite::current());
struct ModalWideNavigationRailImpl {
    using P = Params<names::Modifier, names::State, names::HideOnCollapse, names::CollapsedWidth, names::ExpandedWidth, names::Header, names::WindowInsets, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: ModalWideNavigationRail takes a block: its content");
        ModalWideNavigationRailOptions options;
        P::set<names::State>(options.state, args...);
        P::set<names::HideOnCollapse>(options.hideOnCollapse, args...);
        P::set<names::CollapsedWidth>(options.collapsedWidth, args...);
        P::set<names::ExpandedWidth>(options.expandedWidth, args...);
        P::set<names::Header>(options.header, args...);
        P::set<names::WindowInsets>(options.windowInsets, args...);
        ModalWideNavigationRail(P::get<names::Modifier>(Modifier{}, args...), P::block(std::forward<A>(args)...), options, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::ModalWideNavigationRailImpl> ModalWideNavigationRail;

namespace detail {
void FloatingActionButton(std::function<void()> onClick, const Content& content, const Modifier& modifier = {},
                          CallSite site = CallSite::current());
struct FloatingActionButtonImpl {
    using P = Params<names::OnClick, names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: FloatingActionButton takes a block: its content");
        FloatingActionButton(std::move(onClick), P::block(std::forward<A>(args)...), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::FloatingActionButtonImpl> FloatingActionButton;
namespace detail {
void SmallFloatingActionButton(std::function<void()> onClick, const Content& content, const Modifier& modifier = {},
                               CallSite site = CallSite::current());  // 40 dp
struct SmallFloatingActionButtonImpl {
    using P = Params<names::OnClick, names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: SmallFloatingActionButton takes a block: its content");
        SmallFloatingActionButton(std::move(onClick), P::block(std::forward<A>(args)...), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::SmallFloatingActionButtonImpl> SmallFloatingActionButton;
namespace detail {
void LargeFloatingActionButton(std::function<void()> onClick, const Content& content, const Modifier& modifier = {},
                               CallSite site = CallSite::current());  // 96 dp (its icon 36)
struct LargeFloatingActionButtonImpl {
    using P = Params<names::OnClick, names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: LargeFloatingActionButton takes a block: its content");
        LargeFloatingActionButton(std::move(onClick), P::block(std::forward<A>(args)...), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::LargeFloatingActionButtonImpl> LargeFloatingActionButton;
namespace detail {
void ExtendedFloatingActionButton(std::function<void()> onClick, const ImageVector& icon, std::string text,
                                  const Modifier& modifier = {}, CallSite site = CallSite::current());
struct ExtendedFloatingActionButtonImpl {
    using P = Params<names::OnClick, names::Icon, names::Text, names::Modifier>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        const ImageVector& icon = P::arg<names::Icon>(args...);
        std::string text = P::arg<names::Text>(args...);
        ExtendedFloatingActionButton(std::move(onClick), icon, std::move(text), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::ExtendedFloatingActionButtonImpl> ExtendedFloatingActionButton;
// M3 Expressive's FAB sizes: medium is 80 dp (its icon 28, corners 20).
namespace detail {
void MediumFloatingActionButton(std::function<void()> onClick, const Content& content, const Modifier& modifier = {},
                                CallSite site = CallSite::current());
struct MediumFloatingActionButtonImpl {
    using P = Params<names::OnClick, names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: MediumFloatingActionButton takes a block: its content");
        MediumFloatingActionButton(std::move(onClick), P::block(std::forward<A>(args)...), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::MediumFloatingActionButtonImpl> MediumFloatingActionButton;
// M3 Expressive's extended FABs, 56 / 80 / 96 dp tall (icons 24 / 28 / 36, labels titleMedium /
// titleLarge / headlineSmall). Not expanded, the label fades out and the button narrows to a
// square FAB around its icon (scrolled content often collapses it).
namespace detail {
void SmallExtendedFloatingActionButton(std::function<void()> onClick, const ImageVector& icon, std::string text, bool expanded = true,
                                       const Modifier& modifier = {}, CallSite site = CallSite::current());
struct SmallExtendedFloatingActionButtonImpl {
    using P = Params<names::OnClick, names::Icon, names::Text, names::Modifier, names::Expanded>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        const ImageVector& icon = P::arg<names::Icon>(args...);
        std::string text = P::arg<names::Text>(args...);
        SmallExtendedFloatingActionButton(std::move(onClick), icon, std::move(text), P::get<names::Expanded>(true, args...), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::SmallExtendedFloatingActionButtonImpl> SmallExtendedFloatingActionButton;
namespace detail {
void MediumExtendedFloatingActionButton(std::function<void()> onClick, const ImageVector& icon, std::string text, bool expanded = true,
                                        const Modifier& modifier = {}, CallSite site = CallSite::current());
struct MediumExtendedFloatingActionButtonImpl {
    using P = Params<names::OnClick, names::Icon, names::Text, names::Modifier, names::Expanded>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        const ImageVector& icon = P::arg<names::Icon>(args...);
        std::string text = P::arg<names::Text>(args...);
        MediumExtendedFloatingActionButton(std::move(onClick), icon, std::move(text), P::get<names::Expanded>(true, args...), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::MediumExtendedFloatingActionButtonImpl> MediumExtendedFloatingActionButton;
namespace detail {
void LargeExtendedFloatingActionButton(std::function<void()> onClick, const ImageVector& icon, std::string text, bool expanded = true,
                                       const Modifier& modifier = {}, CallSite site = CallSite::current());
struct LargeExtendedFloatingActionButtonImpl {
    using P = Params<names::OnClick, names::Icon, names::Text, names::Modifier, names::Expanded>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        const ImageVector& icon = P::arg<names::Icon>(args...);
        std::string text = P::arg<names::Text>(args...);
        LargeExtendedFloatingActionButton(std::move(onClick), icon, std::move(text), P::get<names::Expanded>(true, args...), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::LargeExtendedFloatingActionButtonImpl> LargeExtendedFloatingActionButton;

// M3 Expressive's FAB that stays checked (Compose's ToggleFloatingActionButton), the button of a
// FloatingActionButtonMenu: checked, it turns from primaryContainer to primary and from its
// rounded square to a circle (and to checkedContainerSize, shrinking towards contentAlignment).
// The content reads checkedProgress() (0..1, animated: read while drawing, as in a graphicsLayer,
// only that is drawn again; in a composition, it is composed again); its content color follows too.
class ToggleFloatingActionButtonScope {
public:
    explicit ToggleFloatingActionButtonScope(AnimatedState<float> progress) : progress_(std::move(progress)) {}
    float checkedProgress() const { return progress_.value(); }

private:
    AnimatedState<float> progress_;
};
struct ToggleFloatingActionButtonOptions {
    float containerSize = 56, checkedContainerSize = 56;  // dp (a medium one: 80 to 56)
    float cornerRadius = 16;                              // round once checked
    std::optional<Color> containerColor, checkedContainerColor;  // primaryContainer, primary
    Alignment contentAlignment = Alignment::TopEnd;
};
void ToggleFloatingActionButton(bool checked, std::function<void(bool)> onCheckedChange,
                                const std::function<void(const ToggleFloatingActionButtonScope&)>& content, const Modifier& modifier = {},
                                const ToggleFloatingActionButtonOptions& options = {}, CallSite site = CallSite::current());

// M3 Expressive's FAB menu (Compose's FloatingActionButtonMenu): its actions open in a column
// above the button, one after another from the bottom, and close from the top.
//   FloatingActionButtonMenu(open.get(), [&] {
//       ToggleFloatingActionButton(open.get(), [open](bool on) { open.set(on); }, [](const ToggleFloatingActionButtonScope& s) {
//           // The plus turns into a cross, as it is drawn.
//           Icon(Icons::Filled::Add, "Menü", Modifier().graphicsLayer([s](GraphicsLayerScope& l) { l.rotationZ = 45 * s.checkedProgress(); }));
//       });
//   }, [&](const FloatingActionButtonMenuScope& menu) {
//       FloatingActionButtonMenuItem(menu, [] { ... }, [] { Text("Belge"); }, [] { Icon(Icons::Filled::Edit, ""); });
//   });
class FloatingActionButtonMenuScope {
public:
    struct Data;
    explicit FloatingActionButtonMenuScope(std::shared_ptr<Data> data) : data_(std::move(data)) {}
    const std::shared_ptr<Data>& data() const { return data_; }

private:
    std::shared_ptr<Data> data_;
};
void FloatingActionButtonMenu(bool expanded, const Content& button, const std::function<void(const FloatingActionButtonMenuScope&)>& content,
                              const Modifier& modifier = {}, HorizontalAlignment horizontalAlignment = Alignment::End,
                              CallSite site = CallSite::current());
// One action: a pill with an icon and a label (primaryContainer).
void FloatingActionButtonMenuItem(const FloatingActionButtonMenuScope& menu, std::function<void()> onClick, const Content& text,
                                  const Content& icon, const Modifier& modifier = {}, CallSite site = CallSite::current());

// M3 Expressive's floating toolbar (Compose's HorizontalFloatingToolbar / VerticalFloatingToolbar):
// a pill of actions over the content, usually near an edge; its leading and trailing actions
// show only while expanded. With a scroll behavior it moves out of view as the content scrolls
// forwards and comes back as it scrolls back.
//   Box(Modifier().fillMaxSize().nestedScroll(toolbar.nestedScrollConnection), [=] {
//       LazyColumn(...);
//       HorizontalFloatingToolbar(expanded, Modifier().align(Alignment::BottomCenter).offset(0, -FloatingToolbarDefaults::ScreenOffset),
//                                 scrollBehavior = toolbar, trailingContent = [&] { IconButton(...); },
//                                 [=] { IconButton(...); ... });
//   });
// How far it has moved out of view: 0 shown .. offsetLimit (its size and its gap to the edge).
class FloatingToolbarState {
public:
    FloatingToolbarState();
    float offset() const;  // observable
    float offsetLimit() const;
    void setOffset(float offset) const;  // kept in 0 .. offsetLimit
    void setOffsetLimit(float limit) const;
    friend bool operator==(const FloatingToolbarState& a, const FloatingToolbarState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};
template <>
struct Saver<FloatingToolbarState> {
    static SavedValue save(const FloatingToolbarState& s);
    static std::optional<FloatingToolbarState> restore(const SavedValue& v);
};
FloatingToolbarState rememberFloatingToolbarState(CallSite site = CallSite::current());
enum class FloatingToolbarExitDirection { Bottom, Top, Start, End };
struct FloatingToolbarScrollBehavior {
    FloatingToolbarState state;
    FloatingToolbarExitDirection exitDirection = FloatingToolbarExitDirection::Bottom;
    std::shared_ptr<NestedScrollConnection> nestedScrollConnection;  // on a parent of the scrolling content
    friend bool operator==(const FloatingToolbarScrollBehavior&, const FloatingToolbarScrollBehavior&) = default;
};
struct FloatingToolbarColors {
    Color toolbarContainerColor, toolbarContentColor, fabContainerColor, fabContentColor;
};
namespace detail {
void StandardFloatingActionButton(std::function<void()> onClick, const Content& content, const Modifier& modifier = {},
                                  CallSite site = CallSite::current());
void VibrantFloatingActionButton(std::function<void()> onClick, const Content& content, const Modifier& modifier = {},
                                 CallSite site = CallSite::current());
struct StandardFloatingActionButtonImpl {
    using P = Params<names::OnClick, names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: StandardFloatingActionButton takes a block: its content");
        StandardFloatingActionButton(std::move(onClick), P::block(std::forward<A>(args)...), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
struct VibrantFloatingActionButtonImpl {
    using P = Params<names::OnClick, names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: VibrantFloatingActionButton takes a block: its content");
        VibrantFloatingActionButton(std::move(onClick), P::block(std::forward<A>(args)...), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
namespace FloatingToolbarDefaults {
inline constexpr float ContainerSize = 64, ContentPadding = 8, ScreenOffset = 16, FabSpacing = 8;  // dp
FloatingToolbarColors standardFloatingToolbarColors();  // surfaceContainer; its FAB secondaryContainer
FloatingToolbarColors vibrantFloatingToolbarColors();   // primaryContainer; its FAB tertiaryContainer
// Out of view as the content scrolls forwards (vertically), back as it scrolls back; a flung
// half way finishes the move.
FloatingToolbarScrollBehavior exitAlwaysScrollBehavior(FloatingToolbarExitDirection exitDirection, CallSite site = CallSite::current());
// The FAB beside a toolbar (in the toolbar colors' FAB colors).
inline constexpr Composable<detail::StandardFloatingActionButtonImpl> StandardFloatingActionButton;
inline constexpr Composable<detail::VibrantFloatingActionButtonImpl> VibrantFloatingActionButton;
} // namespace FloatingToolbarDefaults
struct FloatingToolbarOptions {
    std::optional<FloatingToolbarColors> colors;  // standard
    std::optional<FloatingToolbarScrollBehavior> scrollBehavior;
    Content leadingContent, trailingContent;  // while expanded
    Shape shape = CircleShape;
    float shadowElevation = 6;
};
// With a FAB at its end (below a vertical one): collapsed, only the FAB is left.
namespace detail {
void HorizontalFloatingToolbar(bool expanded, const Content& content, const Modifier& modifier = {}, const FloatingToolbarOptions& options = {},
                               CallSite site = CallSite::current());
void HorizontalFloatingToolbar(bool expanded, const Content& floatingActionButton, const Content& content, const Modifier& modifier = {},
                               const FloatingToolbarOptions& options = {}, CallSite site = CallSite::current());
struct HorizontalFloatingToolbarImpl {
    // Its FAB (Compose's floatingActionButton), named or second without a name.
    using P = Params<names::Expanded, names::Modifier, names::FloatingToolbarColors, names::ScrollBehavior, names::Shape, names::LeadingContent, names::TrailingContent, names::ShadowElevation, names::FloatingActionButton, Block>;
    using FabSecond = Params<names::Expanded, names::FloatingActionButton, names::Modifier, names::FloatingToolbarColors, names::ScrollBehavior, names::Shape, names::LeadingContent, names::TrailingContent, names::ShadowElevation, Block>;
    template <class Q, class... A>
    static FloatingToolbarOptions options(A&... args) {
        FloatingToolbarOptions options;
        Q::template set<names::FloatingToolbarColors>(options.colors, args...);
        Q::template set<names::ScrollBehavior>(options.scrollBehavior, args...);
        Q::template set<names::Shape>(options.shape, args...);
        Q::template set<names::LeadingContent>(options.leadingContent, args...);
        Q::template set<names::TrailingContent>(options.trailingContent, args...);
        Q::template set<names::ShadowElevation>(options.shadowElevation, args...);
        return options;
    }
    // (expanded, fab, …, block): the FAB second without a name.
    template <class... A>
    static constexpr bool fabSecond() {
        if constexpr (sizeof...(A) < 3) return false;
        else return !isNamed<std::tuple_element_t<0, std::tuple<A...>>> && isBlock<std::tuple_element_t<1, std::tuple<A...>>> && lastIsBlock<A...>();
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (fabSecond<A...>()) {
            using Q = FabSecond;
            static_assert(Q::check<A...>());
            bool expanded = Q::arg<names::Expanded>(args...);
            HorizontalFloatingToolbar(expanded, Content(Q::arg<names::FloatingActionButton>(args...)), Q::block(std::forward<A>(args)...),
                Q::get<names::Modifier>(Modifier{}, args...), options<Q>(args...), site);
        } else {
            static_assert(P::check<A...>());
            static_assert(P::blockGiven<A...>(), "kompoz: HorizontalFloatingToolbar takes a block: its content");
            bool expanded = P::arg<names::Expanded>(args...);
            Modifier modifier = P::get<names::Modifier>(Modifier{}, args...);
            if constexpr (P::given<names::FloatingActionButton, A...>())
                HorizontalFloatingToolbar(expanded, P::get<names::FloatingActionButton>(Content{}, args...), P::block(std::forward<A>(args)...), modifier,
                    options<P>(args...), site);
            else
                HorizontalFloatingToolbar(expanded, P::block(std::forward<A>(args)...), modifier, options<P>(args...), site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::HorizontalFloatingToolbarImpl> HorizontalFloatingToolbar;
namespace detail {
void VerticalFloatingToolbar(bool expanded, const Content& content, const Modifier& modifier = {}, const FloatingToolbarOptions& options = {},
                             CallSite site = CallSite::current());
void VerticalFloatingToolbar(bool expanded, const Content& floatingActionButton, const Content& content, const Modifier& modifier = {},
                             const FloatingToolbarOptions& options = {}, CallSite site = CallSite::current());
struct VerticalFloatingToolbarImpl {
    // Its FAB (Compose's floatingActionButton), named or second without a name.
    using P = Params<names::Expanded, names::Modifier, names::FloatingToolbarColors, names::ScrollBehavior, names::Shape, names::LeadingContent, names::TrailingContent, names::ShadowElevation, names::FloatingActionButton, Block>;
    using FabSecond = Params<names::Expanded, names::FloatingActionButton, names::Modifier, names::FloatingToolbarColors, names::ScrollBehavior, names::Shape, names::LeadingContent, names::TrailingContent, names::ShadowElevation, Block>;
    template <class Q, class... A>
    static FloatingToolbarOptions options(A&... args) {
        FloatingToolbarOptions options;
        Q::template set<names::FloatingToolbarColors>(options.colors, args...);
        Q::template set<names::ScrollBehavior>(options.scrollBehavior, args...);
        Q::template set<names::Shape>(options.shape, args...);
        Q::template set<names::LeadingContent>(options.leadingContent, args...);
        Q::template set<names::TrailingContent>(options.trailingContent, args...);
        Q::template set<names::ShadowElevation>(options.shadowElevation, args...);
        return options;
    }
    // (expanded, fab, …, block): the FAB second without a name.
    template <class... A>
    static constexpr bool fabSecond() {
        if constexpr (sizeof...(A) < 3) return false;
        else return !isNamed<std::tuple_element_t<0, std::tuple<A...>>> && isBlock<std::tuple_element_t<1, std::tuple<A...>>> && lastIsBlock<A...>();
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (fabSecond<A...>()) {
            using Q = FabSecond;
            static_assert(Q::check<A...>());
            bool expanded = Q::arg<names::Expanded>(args...);
            VerticalFloatingToolbar(expanded, Content(Q::arg<names::FloatingActionButton>(args...)), Q::block(std::forward<A>(args)...),
                Q::get<names::Modifier>(Modifier{}, args...), options<Q>(args...), site);
        } else {
            static_assert(P::check<A...>());
            static_assert(P::blockGiven<A...>(), "kompoz: VerticalFloatingToolbar takes a block: its content");
            bool expanded = P::arg<names::Expanded>(args...);
            Modifier modifier = P::get<names::Modifier>(Modifier{}, args...);
            if constexpr (P::given<names::FloatingActionButton, A...>())
                VerticalFloatingToolbar(expanded, P::get<names::FloatingActionButton>(Content{}, args...), P::block(std::forward<A>(args)...), modifier,
                    options<P>(args...), site);
            else
                VerticalFloatingToolbar(expanded, P::block(std::forward<A>(args)...), modifier, options<P>(args...), site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::VerticalFloatingToolbarImpl> VerticalFloatingToolbar;

enum class SnackbarDuration { Short, Long, Indefinite };

enum class SnackbarResult { Dismissed, ActionPerformed };

struct SnackbarData {
    uint64_t id = 0;
    std::string message, actionLabel;
    std::function<void()> onAction;
    SnackbarDuration duration = SnackbarDuration::Short;
    bool withDismissAction = false;  // a close button
    std::function<void(SnackbarResult)> onResult;  // how it went (showSnackbarAsync)
    // What a snackbar of your own calls (Compose's SnackbarData.performAction / dismiss): its
    // action runs and it goes, or it just goes. Set up by the SnackbarHost showing it.
    void performAction() const {
        if (finish) finish(SnackbarResult::ActionPerformed);
    }
    void dismiss() const {
        if (finish) finish(SnackbarResult::Dismissed);
    }
    std::function<void(SnackbarResult)> finish;
};

class SnackbarHostState;
// co_await state.showSnackbarAsync(...): resumes with how the snackbar went. Cancelled while it
// waits (the coroutine's scope went), the snackbar goes too.
class SnackbarAwaiter {
public:
    SnackbarAwaiter(std::shared_ptr<void> state, SnackbarData data);
    SnackbarAwaiter(const SnackbarAwaiter&) = delete;
    SnackbarAwaiter& operator=(const SnackbarAwaiter&) = delete;
    ~SnackbarAwaiter();
    bool await_ready() const noexcept { return false; }
    template <class P>
    void await_suspend(std::coroutine_handle<P> handle) {
        suspend(handle, handle.promise().job);
    }
    void suspend(std::coroutine_handle<> handle, detail::JobState* job);
    SnackbarResult await_resume() const { return result_->value_or(SnackbarResult::Dismissed); }

private:
    std::shared_ptr<void> state_;  // the SnackbarHostState's data
    SnackbarData data_;
    std::shared_ptr<std::optional<SnackbarResult>> result_ = std::make_shared<std::optional<SnackbarResult>>();
    bool queued_ = false;
};

// Queue of snackbars; SnackbarHost shows them one at a time.
class SnackbarHostState {
public:
    SnackbarHostState();
    void showSnackbar(std::string message, std::string actionLabel = {}, std::function<void()> onAction = {},
                      SnackbarDuration duration = SnackbarDuration::Short) const;
    // In a coroutine, as Compose's suspending showSnackbar: shown after those before it, it
    // resumes once it is gone, with whether its action was taken.
    //   if (co_await snackbars.showSnackbarAsync("Silindi", "Geri al") == SnackbarResult::ActionPerformed) undo();
    // Without an action it stays Short; with one, until it is answered (Compose's defaults).
    [[nodiscard]] SnackbarAwaiter showSnackbarAsync(std::string message, std::string actionLabel = {},
                                                    std::optional<SnackbarDuration> duration = std::nullopt,
                                                    bool withDismissAction = false) const;
    // Compose's named forms: showSnackbar(message, actionLabel = "…", withDismissAction = …, duration = …):
    // in a coroutine (kompozc's showSnackbarAsync) awaited; else shown and forgotten.
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    [[nodiscard]] SnackbarAwaiter showSnackbarAsync(std::string message, A&&... args) const {
        namespace n = detail::names;
        using P = detail::Params<n::ActionLabel, n::WithDismissAction, n::Duration>;
        static_assert(P::check<A...>());
        return showSnackbarAsync(std::move(message), P::get<n::ActionLabel>(std::string(), args...),
                                 P::get<n::Duration>(std::optional<SnackbarDuration>(), args...), P::get<n::WithDismissAction>(false, args...));
    }
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    void showSnackbar(std::string message, A&&... args) const {
        namespace n = detail::names;
        using P = detail::Params<n::ActionLabel, n::WithDismissAction, n::Duration>;
        static_assert(P::check<A...>());
        showSnackbar(std::move(message), P::get<n::ActionLabel>(std::string(), args...), {}, P::get<n::Duration>(SnackbarDuration::Short, args...));
    }
    std::optional<SnackbarData> current() const;  // observable
    void dismiss(uint64_t id, SnackbarResult result = SnackbarResult::Dismissed) const;
    friend bool operator==(const SnackbarHostState& a, const SnackbarHostState& b) { return a.data_ == b.data_; }

    struct Data;

private:
    friend class SnackbarAwaiter;
    void enqueue(SnackbarData data) const;
    void remove(uint64_t id) const;  // gone without telling anyone
    std::shared_ptr<Data> data_;
};

inline SnackbarHostState rememberSnackbarHostState(CallSite site = CallSite::current()) {
    return remember([] { return SnackbarHostState(); }, site);
}

// With snackbars of your own (Compose's SnackbarHost(state, modifier, snackbar)): `snackbar`
// draws the one showing, by default Snackbar(data); it calls data.performAction() / dismiss().
//   SnackbarHost(state, Modifier(), [](const SnackbarData& d) { Snackbar(d, Modifier(), containerColor = cs.errorContainer); });
namespace detail {
void SnackbarHost(SnackbarHostState state, const Modifier& modifier = {}, CallSite site = CallSite::current());
void SnackbarHost(SnackbarHostState state, const Modifier& modifier, std::function<void(const SnackbarData&)> snackbar,
                  CallSite site = CallSite::current());
struct SnackbarHostImpl {
    using P = Params<names::HostState, names::Modifier, names::Snackbar, BlockWith<const SnackbarData&>>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        SnackbarHostState state = P::arg<names::HostState>(args...);
        if constexpr (P::given<names::Snackbar, A...>()) {  // snackbar = { data -> Snackbar(data) }
            SnackbarHost(std::move(state), P::get<names::Modifier>(Modifier{}, args...),
                         std::function<void(const SnackbarData&)>(P::arg<names::Snackbar>(args...)), site);
            return;
        }
        Modifier modifier = P::get<names::Modifier>(Modifier{}, args...);
        if constexpr (P::blockGiven<A...>()) SnackbarHost(std::move(state), modifier, P::block(std::forward<A>(args)...), site);
        else SnackbarHost(std::move(state), modifier, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::SnackbarHostImpl> SnackbarHost;

// A snackbar's options (Compose's Snackbar parameters): its slots and colors.
struct SnackbarOptions {
    Content action;         // the slot one's action, a TextButton (labelLarge in actionContentColor)
    Content dismissAction;  // and its close button, an IconButton (dismissActionContentColor)
    bool actionOnNewLine = false;  // the action under the text (a long one)
    std::optional<Shape> shape;    // unset: 4 dp corners
    std::optional<Color> containerColor, contentColor;  // inverseSurface, inverseOnSurface
    std::optional<Color> actionColor;  // a data snackbar's action button: inversePrimary
    std::optional<Color> actionContentColor, dismissActionContentColor;  // inversePrimary, inverseOnSurface
};
// The snackbar itself (Compose's Snackbar): the text (bodyMedium) and, at its end or under it,
// the action and the close button, on inverseSurface; at most 600 dp wide, 48 dp tall (68 for
// two lines, the first line's baseline 30 dp down).
// One of a SnackbarData, as SnackbarHost shows it: its message, its action as a text button,
// a close button if it has one, 12 dp around.
namespace detail {
void Snackbar(const Content& content, const Modifier& modifier = {}, const SnackbarOptions& options = {},
              CallSite site = CallSite::current());
void Snackbar(const SnackbarData& data, const Modifier& modifier = {}, SnackbarOptions options = {},
              CallSite site = CallSite::current());
struct SnackbarImpl {
    template <class... A>
    static void dataForm(CallSite site, A&&... args) {  // a SnackbarData's, as SnackbarHost shows it
        using P = Params<names::Data, names::Modifier, names::ActionOnNewLine, names::Shape, names::ContainerColor, names::ContentColor, names::ActionColor, names::ActionContentColor, names::DismissActionContentColor>;
        static_assert(P::check<A...>());
        SnackbarOptions options;
        P::set<names::ActionOnNewLine>(options.actionOnNewLine, args...);
        P::set<names::Shape>(options.shape, args...);
        P::set<names::ContainerColor>(options.containerColor, args...);
        P::set<names::ContentColor>(options.contentColor, args...);
        P::set<names::ActionColor>(options.actionColor, args...);
        P::set<names::ActionContentColor>(options.actionContentColor, args...);
        P::set<names::DismissActionContentColor>(options.dismissActionContentColor, args...);
        Snackbar(P::arg<names::Data>(args...), P::get<names::Modifier>(Modifier{}, args...), std::move(options), site);
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (countOf<names::Data, A...> > 0 || (std::is_convertible_v<A, const SnackbarData&> || ...)) dataForm(site, std::forward<A>(args)...);
        else slotForm(site, std::forward<A>(args)...);
    }
    template <class... A>
    static void slotForm(CallSite site, A&&... args) {  // with slots, the block its text
        using P = Params<names::Modifier, names::Action, names::DismissAction, names::ActionOnNewLine, names::Shape, names::ContainerColor, names::ContentColor, names::ActionContentColor, names::DismissActionContentColor, Block>;
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: Snackbar takes a block (its text), or a SnackbarData");
        SnackbarOptions options;
        P::set<names::Action>(options.action, args...);
        P::set<names::DismissAction>(options.dismissAction, args...);
        P::set<names::ActionOnNewLine>(options.actionOnNewLine, args...);
        P::set<names::Shape>(options.shape, args...);
        P::set<names::ContainerColor>(options.containerColor, args...);
        P::set<names::ContentColor>(options.contentColor, args...);
        P::set<names::ActionContentColor>(options.actionContentColor, args...);
        P::set<names::DismissActionContentColor>(options.dismissActionContentColor, args...);
        Snackbar(P::block(std::forward<A>(args)...), P::get<names::Modifier>(Modifier{}, args...), options, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::SnackbarImpl> Snackbar;

// ---- Lists, chips, selection controls, tabs, badges ------------------------------------

// One-, two- or three-line list row (the height follows the slots that are set).
struct ListItemSlots {
    Content headline;
    Content overline, supporting, leading, trailing;
    std::optional<Color> containerColor;
};
namespace detail {
void ListItem(const ListItemSlots& slots, const Modifier& modifier = {}, CallSite site = CallSite::current());
} // namespace detail

// An interactive list item's shapes in each state (Compose's ListItemShapes, M3 Expressive): it
// morphs between them — a little rounded, rounder when hovered, pressed, focused or selected.
struct ListItemShapes {
    Shape shape = RoundedCornerShape(4);
    Shape selectedShape = RoundedCornerShape(16), pressedShape = RoundedCornerShape(16), focusedShape = RoundedCornerShape(16);
    Shape hoveredShape = RoundedCornerShape(12);
};
namespace ListItemDefaults {
inline ListItemShapes shapes() { return {}; }
// A SegmentedListItem's at `index` of `count` (Compose's segmentedShapes): the first one rounder
// (16 dp) at the top, the last one at the bottom, so the items read as one card.
ListItemShapes segmentedShapes(int index, int count, ListItemShapes base = {});
inline constexpr float SegmentedGap = 2;  // the space between segmented items (Arrangement::spacedBy)
}  // namespace ListItemDefaults
// Colors of an interactive list item (Compose's ListItemColors); unset: surface and onSurface
// (leading, trailing and supporting onSurfaceVariant), selected secondaryContainer and
// onSecondaryContainer, disabled onSurface at 38%.
struct ListItemColors {
    std::optional<Color> containerColor, contentColor, leadingContentColor, trailingContentColor, supportingContentColor;
    std::optional<Color> selectedContainerColor, selectedContentColor;
};
namespace ListItemDefaults {
// Compose's ListItemDefaults.colors(containerColor = …): a ListItemColors (a plain ListItem takes its containerColor).
using colors = ListItemColors;
}  // namespace ListItemDefaults
struct InteractiveListItemOptions {
    bool enabled = true;
    std::function<void()> onLongClick;
    std::optional<ListItemShapes> shapes;  // unset: ListItemDefaults::shapes()
    ListItemColors colors;
    std::optional<MutableInteractionSource> interactionSource;
};
// M3 Expressive's interactive list items (Compose's ListItem(onClick), ListItem(selected,
// onClick), ListItem(checked, onCheckedChange)): the whole row takes the tap, a ripple in its
// shape, which morphs as it is pressed or selected; selected (or checked) on secondaryContainer.
// The slots as ListItem's (the headline bodyLarge, supporting bodyMedium, overline labelSmall),
// 16 dp at the sides, 10 above and below, 12 between, at least 56 dp tall.
namespace detail {
void ListItem(std::function<void()> onClick, const ListItemSlots& slots, const Modifier& modifier = {},
              const InteractiveListItemOptions& options = {}, CallSite site = CallSite::current());
void ListItem(bool selected, std::function<void()> onClick, const ListItemSlots& slots, const Modifier& modifier = {},
              const InteractiveListItemOptions& options = {}, CallSite site = CallSite::current());
void ListItem(bool checked, std::function<void(bool)> onCheckedChange, const ListItemSlots& slots, const Modifier& modifier = {},
              const InteractiveListItemOptions& options = {}, CallSite site = CallSite::current());

// ListItem as Compose writes it: ListItem(headlineContent = { … }, supportingContent = { … }, …),
// or an interactive one, its headline the block (ListItem(onClick = …) { Text("…") }). The C++
// forms with a ListItemSlots ({.headline = …}) take their arguments by place, as before.
struct ListItemImpl {
    using Plain = Params<names::HeadlineContent, names::Modifier, names::OverlineContent, names::SupportingContent, names::LeadingContent,
                         names::TrailingContent, names::Colors>;
    template <class Head>
    using Interactive = Params<Head, names::Modifier, names::Enabled, names::LeadingContent, names::TrailingContent, names::OverlineContent,
                               names::SupportingContent, names::OnLongClick, names::Shapes, names::Colors, names::InteractionSource, Block>;
    template <class Head1, class Head2>
    using Interactive2 = Params<Head1, Head2, names::Modifier, names::Enabled, names::LeadingContent, names::TrailingContent,
                                names::OverlineContent, names::SupportingContent, names::OnLongClick, names::Shapes, names::Colors,
                                names::InteractionSource, Block>;
    template <class... A>
    static constexpr bool slotsForm() { return (std::is_same_v<std::remove_cvref_t<A>, ListItemSlots> || ...); }
    template <class Q, class... A>
    static ListItemSlots slots(A&&... args) {
        ListItemSlots s;
        Q::template set<names::OverlineContent>(s.overline, args...);
        Q::template set<names::SupportingContent>(s.supporting, args...);
        Q::template set<names::LeadingContent>(s.leading, args...);
        Q::template set<names::TrailingContent>(s.trailing, args...);
        if constexpr (oneOfParams<names::HeadlineContent, Q>) Q::template set<names::HeadlineContent>(s.headline, args...);
        else s.headline = Q::block(std::forward<A>(args)...);
        if constexpr (std::is_same_v<Q, Plain> && Q::template given<names::Colors, A...>())  // colors = ListItemDefaults.colors(containerColor = …)
            s.containerColor = ListItemColors(Q::template arg<names::Colors>(args...)).containerColor;
        return s;
    }
    template <class Q, class... A>
    static InteractiveListItemOptions options(A&... args) {
        InteractiveListItemOptions o;
        Q::template set<names::Enabled>(o.enabled, args...);
        Q::template set<names::OnLongClick>(o.onLongClick, args...);
        Q::template set<names::Shapes>(o.shapes, args...);
        Q::template set<names::Colors>(o.colors, args...);
        Q::template set<names::InteractionSource>(o.interactionSource, args...);
        return o;
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (slotsForm<A...>()) {
            static_assert(!(isNamed<A> || ...), "kompoz: ListItem's ListItemSlots form takes its arguments by place");
            detail::ListItem(std::forward<A>(args)..., site);
        } else if constexpr (countOf<names::Checked, A...> + countOf<names::OnCheckedChange, A...> > 0) {
            using Q = Interactive2<names::Checked, names::OnCheckedChange>;
            static_assert(Q::check<A...>());
            detail::ListItem(bool(Q::arg<names::Checked>(args...)), std::function<void(bool)>(Q::arg<names::OnCheckedChange>(args...)),
                             slots<Q>(args...), Q::get<names::Modifier>(Modifier{}, args...), options<Q>(args...), site);
        } else if constexpr (countOf<names::Selected, A...> > 0) {
            using Q = Interactive2<names::Selected, names::OnClick>;
            static_assert(Q::check<A...>());
            detail::ListItem(bool(Q::arg<names::Selected>(args...)), std::function<void()>(Q::arg<names::OnClick>(args...)), slots<Q>(args...),
                             Q::get<names::Modifier>(Modifier{}, args...), options<Q>(args...), site);
        } else if constexpr (countOf<names::OnClick, A...> > 0 || startsWithClick<A...>()) {
            using Q = Interactive<names::OnClick>;
            static_assert(Q::check<A...>());
            detail::ListItem(std::function<void()>(Q::arg<names::OnClick>(args...)), slots<Q>(args...), Q::get<names::Modifier>(Modifier{}, args...),
                             options<Q>(args...), site);
        } else {
            using Q = Plain;
            static_assert(Q::check<A...>());
            static_assert(Q::given<names::HeadlineContent, A...>(), "kompoz: ListItem needs headlineContent");
            detail::ListItem(slots<Q>(args...), Q::get<names::Modifier>(Modifier{}, args...), site);
        }
    }
};
// The C++ forms whose ListItemSlots is made in place (ListItem({.headline = …})): a braced
// list goes to a parameter of its type, not through the template.
struct ListItemCall : Composable<ListItemImpl> {
    using Composable<ListItemImpl>::operator();
    void operator()(const ListItemSlots& slots, const Modifier& modifier = {}, CallSite site = CallSite::current()) const {
        ListItem(slots, modifier, site);
    }
    void operator()(std::function<void()> onClick, const ListItemSlots& slots, const Modifier& modifier = {},
                    const InteractiveListItemOptions& options = {}, CallSite site = CallSite::current()) const {
        ListItem(std::move(onClick), slots, modifier, options, site);
    }
    void operator()(bool selected, std::function<void()> onClick, const ListItemSlots& slots, const Modifier& modifier = {},
                    const InteractiveListItemOptions& options = {}, CallSite site = CallSite::current()) const {
        ListItem(selected, std::move(onClick), slots, modifier, options, site);
    }
    void operator()(bool checked, std::function<void(bool)> onCheckedChange, const ListItemSlots& slots, const Modifier& modifier = {},
                    const InteractiveListItemOptions& options = {}, CallSite site = CallSite::current()) const {
        ListItem(checked, std::move(onCheckedChange), slots, modifier, options, site);
    }
};
} // namespace detail
inline constexpr detail::ListItemCall ListItem;
// Items that make one card together (Compose's SegmentedListItem): give each its segmented
// shapes and put them ListItemDefaults::SegmentedGap apart.
//   Column(Modifier(), verticalArrangement = Arrangement::spacedBy(ListItemDefaults::SegmentedGap), [=] {
//       for (int i = 0; i < 3; ++i) SegmentedListItem(open(i), ListItemDefaults::segmentedShapes(i, 3), {...});
//   });
void SegmentedListItem(std::function<void()> onClick, const ListItemShapes& shapes, const ListItemSlots& slots,
                       const Modifier& modifier = {}, InteractiveListItemOptions options = {}, CallSite site = CallSite::current());
void SegmentedListItem(bool selected, std::function<void()> onClick, const ListItemShapes& shapes, const ListItemSlots& slots,
                       const Modifier& modifier = {}, InteractiveListItemOptions options = {}, CallSite site = CallSite::current());

// Icons in chips are usually Modifier().size(18). A filter or input chip's icons come and go
// growing in and fading (the check of a selected filter chip). An input chip's avatar (usually
// 24 dp) is cut to a circle and takes the leading icon's place.
struct ChipOptions {
    Content leadingIcon, trailingIcon;
    bool enabled = true;
    Content avatar;  // input chips
};
namespace detail {
void AssistChip(std::function<void()> onClick, const Content& label, const ChipOptions& options, const Modifier& modifier, CallSite site);
void SuggestionChip(std::function<void()> onClick, const Content& label, const ChipOptions& options, const Modifier& modifier, CallSite site);
void FilterChip(bool selected, std::function<void()> onClick, const Content& label, const ChipOptions& options, const Modifier& modifier, CallSite site);
void InputChip(bool selected, std::function<void()> onClick, const Content& label, const ChipOptions& options, const Modifier& modifier, CallSite site);
// Elevated chips: on surfaceContainerLow with a shadow instead of a border (none when disabled),
// for chips over images or busy content.
void ElevatedAssistChip(std::function<void()> onClick, const Content& label, const ChipOptions& options, const Modifier& modifier, CallSite site);
void ElevatedSuggestionChip(std::function<void()> onClick, const Content& label, const ChipOptions& options, const Modifier& modifier, CallSite site);
void ElevatedFilterChip(bool selected, std::function<void()> onClick, const Content& label, const ChipOptions& options, const Modifier& modifier, CallSite site);


// The chips as Compose's: AssistChip(onClick, label = { Text("…") }, modifier, enabled = …, leadingIcon = …,
// trailingIcon = …), FilterChip(selected, onClick, label, …), InputChip(selected, onClick, label, …, avatar = …);
// or with a ChipOptions after the label, as before.
template <int Kind, bool Selectable>
struct ChipImpl {
    using P = std::conditional_t<Selectable,
                                 Params<names::Selected, names::OnClick, names::Label, names::Options, names::Modifier, names::Enabled,
                                        names::LeadingIcon, names::TrailingIcon, names::Avatar>,
                                 Params<names::OnClick, names::Label, names::Options, names::Modifier, names::Enabled, names::LeadingIcon,
                                        names::TrailingIcon, names::Avatar>>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::template check<A...>());
        ChipOptions options = P::template get<names::Options>(ChipOptions{}, args...);
        P::template set<names::Enabled>(options.enabled, args...);
        P::template set<names::LeadingIcon>(options.leadingIcon, args...);
        P::template set<names::TrailingIcon>(options.trailingIcon, args...);
        P::template set<names::Avatar>(options.avatar, args...);
        std::function<void()> onClick = P::template arg<names::OnClick>(args...);
        Content label = P::template arg<names::Label>(args...);
        Modifier modifier = P::template get<names::Modifier>(Modifier{}, args...);
        if constexpr (Selectable) {
            bool selected = P::template arg<names::Selected>(args...);
            if constexpr (Kind == 0) FilterChip(selected, std::move(onClick), label, options, modifier, site);
            else if constexpr (Kind == 1) InputChip(selected, std::move(onClick), label, options, modifier, site);
            else ElevatedFilterChip(selected, std::move(onClick), label, options, modifier, site);
        } else {
            if constexpr (Kind == 0) AssistChip(std::move(onClick), label, options, modifier, site);
            else if constexpr (Kind == 1) SuggestionChip(std::move(onClick), label, options, modifier, site);
            else if constexpr (Kind == 2) ElevatedAssistChip(std::move(onClick), label, options, modifier, site);
            else ElevatedSuggestionChip(std::move(onClick), label, options, modifier, site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::ChipImpl<0, false>> AssistChip;
inline constexpr Composable<detail::ChipImpl<1, false>> SuggestionChip;
inline constexpr Composable<detail::ChipImpl<0, true>> FilterChip;
inline constexpr Composable<detail::ChipImpl<1, true>> InputChip;
// Elevated chips: on surfaceContainerLow with a shadow instead of a border (none when disabled),
// for chips over images or busy content.
inline constexpr Composable<detail::ChipImpl<2, false>> ElevatedAssistChip;
inline constexpr Composable<detail::ChipImpl<3, false>> ElevatedSuggestionChip;
inline constexpr Composable<detail::ChipImpl<2, true>> ElevatedFilterChip;

namespace detail {
void RadioButton(bool selected, std::function<void()> onClick, const Modifier& modifier = {}, bool enabled = true,
                 std::optional<MutableInteractionSource> interactionSource = {}, CallSite site = CallSite::current());
struct RadioButtonImpl {
    using P = Params<names::Selected, names::OnClick, names::Modifier, names::Enabled, names::InteractionSource>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        bool selected = P::arg<names::Selected>(args...);
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        detail::RadioButton(selected, std::move(onClick), P::get<names::Modifier>(Modifier{}, args...), P::get<names::Enabled>(true, args...),
            P::get<names::InteractionSource>(std::optional<MutableInteractionSource>{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::RadioButtonImpl> RadioButton;

// A slider's colors (Compose's SliderColors): its thumb, its track and the step ticks on either
// side of the value, enabled and not.
struct SliderColors {
    Color thumbColor, activeTrackColor, activeTickColor, inactiveTrackColor, inactiveTickColor;
    Color disabledThumbColor, disabledActiveTrackColor, disabledActiveTickColor, disabledInactiveTrackColor, disabledInactiveTickColor;
    Color thumb(bool enabled) const { return enabled ? thumbColor : disabledThumbColor; }
    Color track(bool enabled, bool active) const {
        return enabled ? (active ? activeTrackColor : inactiveTrackColor) : (active ? disabledActiveTrackColor : disabledInactiveTrackColor);
    }
    Color tick(bool enabled, bool active) const {
        return enabled ? (active ? activeTickColor : inactiveTickColor) : (active ? disabledActiveTickColor : disabledInactiveTickColor);
    }
    friend bool operator==(const SliderColors&, const SliderColors&) = default;
};

// What a slider's thumb and track are drawn from (Compose's SliderState): its value and range,
// whether it is held, and what it was given. rememberSliderState keeps one with a value of its
// own; Slider(value, onValueChange) makes one each time it is composed.
class SliderState {
public:
    float value() const;
    void setValue(float value) const;  // in range, on a step: told to onValueChange (or kept)
    float valueFrom() const;
    float valueTo() const;
    int steps() const;
    float valueAsFraction() const;      // 0 at valueFrom, 1 at valueTo
    std::vector<float> tickFractions() const;  // the steps' (and both ends'), none without steps
    bool isPressed() const;   // held: pressed or dragged
    bool isDragging() const;  // moved since it was pressed
    bool enabled() const;
    bool vertical() const;
    const std::optional<SliderColors>& colors() const;  // the options' (each thumb and track has its default)

    struct Data;
    std::shared_ptr<Data> data_;
};

class RangeSliderState;

struct SliderOptions {
    float valueFrom = 0, valueTo = 1;
    int steps = 0;  // discrete values between the ends
    std::function<void()> onValueChangeFinished;
    bool enabled = true;
    // Told of what the user does to it (Compose's interactionSource): pressed from the touch
    // to the release, dragged once the touch moves it sideways, hovered while the mouse is
    // over a Slider — a value label shown while held reads collectIsPressedAsState() ||
    // collectIsDraggedAsState(). A RangeSlider tells this one of its start thumb's presses
    // and drags, and endInteractionSource of its end thumb's.
    std::optional<MutableInteractionSource> interactionSource;
    std::optional<MutableInteractionSource> endInteractionSource;
    std::optional<SliderColors> colors;
    // The thumb and the track (Slider and VerticalSlider), each composed from the slider's state:
    // unset, the classic round thumb and thin track; SliderDefaults has Material 3's bar thumb and
    // thick track, and thumbs of any shape, with an icon or showing the value.
    //   {.thumb = [](const SliderState& s) { SliderDefaults::Thumb(s); }, .track = [](const SliderState& s) { SliderDefaults::Track(s); }}
    std::function<void(const SliderState&)> thumb, track;
    // A RangeSlider's: each thumb from a state of its own (its value, held or not; unset, `thumb`,
    // else the classic circle), and the track from the range's state (unset, the classic).
    std::function<void(const SliderState&)> startThumb, endThumb;
    std::function<void(const RangeSliderState&)> rangeTrack;
};

// A RangeSlider's state (Compose's RangeSliderState): both ends of the range, which thumb is
// held, and each thumb as a SliderState (for SliderDefaults' thumbs).
class RangeSliderState {
public:
    float activeRangeStart() const;
    float activeRangeEnd() const;
    void setActiveRange(float start, float end) const;  // in range, on steps, start <= end
    float valueFrom() const;
    float valueTo() const;
    int steps() const;
    float startFraction() const;
    float endFraction() const;
    std::vector<float> tickFractions() const;
    int held() const;  // the thumb being moved: 0 the start, 1 the end, -1 none
    bool enabled() const;
    const std::optional<SliderColors>& colors() const;
    SliderState startThumb() const;
    SliderState endThumb() const;

    struct Data;
    std::shared_ptr<Data> data_;
};
// Tap or drag to set the value (dragging takes over from scrolling once it moves sideways).
// With a state of its own (its value, range and steps; the options' are not used).
namespace detail {
void Slider(float value, std::function<void(float)> onValueChange, const Modifier& modifier = {},
            SliderOptions options = {}, CallSite site = CallSite::current());
void Slider(const SliderState& state, const Modifier& modifier = {}, SliderOptions options = {}, CallSite site = CallSite::current());
// Compose's valueRange = 0f..100f (a Kotlin range: its first and last) or a pair: the two ends.
template <class R>
std::pair<float, float> rangeEnds(const R& r) {
    if constexpr (requires { r.first; r.last; }) return {float(r.first), float(r.last)};
    else return {float(r.first), float(r.second)};
}
struct SliderImpl {
    template <class... A>
    static void valueForm(CallSite site, A&&... args) {
        using P = Params<names::Value, names::OnValueChange, names::Modifier, names::Enabled, names::OnValueChangeFinished, names::SliderColors, names::Colors, names::InteractionSource, names::Steps, names::Thumb, names::Track, names::ValueFrom, names::ValueTo, names::ValueRange>;
        static_assert(P::check<A...>());
        float value = P::arg<names::Value>(args...);
        std::function<void(float)> onValueChange = P::arg<names::OnValueChange>(args...);
        SliderOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::OnValueChangeFinished>(options.onValueChangeFinished, args...);
        P::set<names::SliderColors>(options.colors, args...);
        P::set<names::Colors>(options.colors, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::Steps>(options.steps, args...);
        P::set<names::Thumb>(options.thumb, args...);
        P::set<names::Track>(options.track, args...);
        P::set<names::ValueFrom>(options.valueFrom, args...);
        P::set<names::ValueTo>(options.valueTo, args...);
        if constexpr (P::given<names::ValueRange, A...>()) std::tie(options.valueFrom, options.valueTo) = rangeEnds(P::arg<names::ValueRange>(args...));
        detail::Slider(value, std::move(onValueChange), P::get<names::Modifier>(Modifier{}, args...), options, site);
    }
    template <class... A>
    static void stateForm(CallSite site, A&&... args) {  // (its range, steps and onValueChangeFinished: the state's)
        using P = Params<names::State, names::Modifier, names::Enabled, names::SliderColors, names::Colors, names::InteractionSource, names::Thumb, names::Track>;
        static_assert(P::check<A...>());
        const SliderState& state = P::arg<names::State>(args...);
        SliderOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::SliderColors>(options.colors, args...);
        P::set<names::Colors>(options.colors, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::Thumb>(options.thumb, args...);
        P::set<names::Track>(options.track, args...);
        detail::Slider(state, P::get<names::Modifier>(Modifier{}, args...), options, site);
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {  // the value's form, or the state's
        if constexpr (countOf<names::State, A...> > 0 || startsWith<SliderState, A...>()) stateForm(site, std::forward<A>(args)...);
        else valueForm(site, std::forward<A>(args)...);
    }
};
SliderState rememberSliderState(float value = 0, int steps = 0, float valueFrom = 0, float valueTo = 1,
                                std::function<void()> onValueChangeFinished = {}, CallSite site = CallSite::current());
// rememberSliderState(value = …, steps = …, valueRange = 1f..5f) as Compose writes it (and by place as above).
struct RememberSliderStateCall {
    SliderState operator()(float value = 0, int steps = 0, float valueFrom = 0, float valueTo = 1, std::function<void()> onValueChangeFinished = {},
                           CallSite site = CallSite::current()) const {
        return rememberSliderState(value, steps, valueFrom, valueTo, std::move(onValueChangeFinished), site);
    }
    template <class... A>
    static SliderState named(CallSite site, A&&... args) {
        using P = Params<names::Value, names::Steps, names::OnValueChangeFinished, names::ValueRange>;
        static_assert(P::check<A...>());
        std::pair<float, float> range{0, 1};
        if constexpr (P::given<names::ValueRange, A...>()) range = rangeEnds(P::arg<names::ValueRange>(args...));
        return rememberSliderState(P::get<names::Value>(0.f, args...), P::get<names::Steps>(0, args...), range.first, range.second,
                                   P::get<names::OnValueChangeFinished>(std::function<void()>(), args...), site);
    }
    template <class A1> requires isNamed<A1>
    SliderState operator()(A1&& a1, CallSite site = CallSite::current()) const { return named(site, std::forward<A1>(a1)); }
    template <class A1, class A2> requires isNamed<A1>
    SliderState operator()(A1&& a1, A2&& a2, CallSite site = CallSite::current()) const { return named(site, std::forward<A1>(a1), std::forward<A2>(a2)); }
    template <class A1, class A2, class A3> requires isNamed<A1>
    SliderState operator()(A1&& a1, A2&& a2, A3&& a3, CallSite site = CallSite::current()) const { return named(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3)); }
    template <class A1, class A2, class A3, class A4> requires isNamed<A1>
    SliderState operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, CallSite site = CallSite::current()) const { return named(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4)); }
};
} // namespace detail
inline constexpr Composable<detail::SliderImpl> Slider;
inline constexpr detail::RememberSliderStateCall rememberSliderState;

namespace SliderDefaults {
SliderColors colors();         // Material 3's (the bar thumb and the thick track)
SliderColors classicColors();  // the classic slider's (the round thumb and the thin track)
inline constexpr float TrackHeight = 16, ThumbTrackGapSize = 6, TrackInsideCornerSize = 2, TrackStopIndicatorSize = 4, TickSize = 4;
inline constexpr Size ThumbSize{4, 44};
// Thumbs. The classic: a circle, a halo around it while held.
void CircleThumb(const SliderState& state, float radius = 10, CallSite site = CallSite::current());
// Material 3's: a bar (lying down on a vertical slider), half as thick while held.
void Thumb(const SliderState& state, Size size = ThumbSize, CallSite site = CallSite::current());
// Any shape (RoundedCornerShape, OutlineShape(MaterialShapes::Diamond()), ...), a little bigger while held.
void ShapeThumb(const SliderState& state, Shape shape, Size size = {24, 24}, CallSite site = CallSite::current());
// A circle with an icon in it.
void IconThumb(const SliderState& state, const ImageVector& icon, float size = 28, CallSite site = CallSite::current());
// A pill showing the value (label: its text; unset, the value rounded).
void ValueThumb(const SliderState& state, std::function<std::string(float)> label = {}, CallSite site = CallSite::current());
// A thumb (`content`) with a label above it while it is held, or all the time (isPersistent):
//   .thumb = [](const SliderState& s) {
//       SliderDefaults::Label(s, [&] { PlainTooltip([&] { Text(std::format("{:.0f}", s.value())); }); },
//                             [&] { SliderDefaults::Thumb(s); });
//   }
void Label(const SliderState& state, const Content& label, const Content& content, bool isPersistent = false,
           CallSite site = CallSite::current());
// Tracks. The classic: 4 dp, a dot for each step.
void ClassicTrack(const SliderState& state, CallSite site = CallSite::current());
// Material 3's: 16 dp, rounded, apart from the thumb by thumbTrackGapSize (its inner corners
// trackInsideCornerSize), a stop dot at its end and a dot for each step.
void Track(const SliderState& state, bool drawStopIndicator = true, float thumbTrackGapSize = ThumbTrackGapSize,
           float trackInsideCornerSize = TrackInsideCornerSize, CallSite site = CallSite::current());
// A RangeSlider's: the classic, and Material 3's (apart from both thumbs, a stop dot at each end).
void ClassicTrack(const RangeSliderState& state, CallSite site = CallSite::current());
void Track(const RangeSliderState& state, bool drawStopIndicator = true, float thumbTrackGapSize = ThumbTrackGapSize,
           float trackInsideCornerSize = TrackInsideCornerSize, CallSite site = CallSite::current());
} // namespace SliderDefaults
// Two thumbs for a range (start <= end): a press takes the nearer one (with both together,
// the way the drag goes decides), and they don't pass each other.
// A slider standing up (M3 Expressive's VerticalSlider), as tall as it may be and 44 dp wide:
// the value grows upwards, or downwards with reverseDirection; a vertical drag moves it.
namespace detail {
void VerticalSlider(float value, std::function<void(float)> onValueChange, const Modifier& modifier = {}, SliderOptions options = {},
                    bool reverseDirection = false, CallSite site = CallSite::current());
struct VerticalSliderImpl {
    template <class... A>
    static void call(CallSite site, A&&... args) {
        using P = Params<names::Value, names::OnValueChange, names::Modifier, names::Enabled, names::OnValueChangeFinished, names::SliderColors, names::InteractionSource, names::Steps, names::Thumb, names::Track, names::ValueFrom, names::ValueTo, names::ValueRange, names::ReverseDirection>;
        static_assert(P::check<A...>());
        float value = P::arg<names::Value>(args...);
        std::function<void(float)> onValueChange = P::arg<names::OnValueChange>(args...);
        SliderOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::OnValueChangeFinished>(options.onValueChangeFinished, args...);
        P::set<names::SliderColors>(options.colors, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::Steps>(options.steps, args...);
        P::set<names::Thumb>(options.thumb, args...);
        P::set<names::Track>(options.track, args...);
        P::set<names::ValueFrom>(options.valueFrom, args...);
        P::set<names::ValueTo>(options.valueTo, args...);
        if constexpr (P::given<names::ValueRange, A...>()) std::tie(options.valueFrom, options.valueTo) = rangeEnds(P::arg<names::ValueRange>(args...));
        detail::VerticalSlider(value, std::move(onValueChange), P::get<names::Modifier>(Modifier{}, args...), options,
                       P::get<names::ReverseDirection>(false, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::VerticalSliderImpl> VerticalSlider;
// With a state of its own (its range, ends and steps; the options' are not used).
namespace detail {
void RangeSlider(std::pair<float, float> value, std::function<void(std::pair<float, float>)> onValueChange,
                 const Modifier& modifier = {}, SliderOptions options = {}, CallSite site = CallSite::current());
void RangeSlider(const RangeSliderState& state, const Modifier& modifier = {}, SliderOptions options = {},
                 CallSite site = CallSite::current());
struct RangeSliderImpl {
    template <class... A>
    static void valueForm(CallSite site, A&&... args) {
        using P = Params<names::Value, names::OnValueChange, names::Modifier, names::Enabled, names::ValueFrom, names::ValueTo, names::Steps, names::OnValueChangeFinished, names::SliderColors, names::Colors, names::InteractionSource, names::EndInteractionSource, names::Thumb, names::StartThumb, names::EndThumb, names::RangeTrack, names::Track, names::ValueRange>;
        static_assert(P::check<A...>());
        // The value as a pair of ends or Compose's range (200f..800f): told back as what it was given.
        auto&& given = P::arg<names::Value>(args...);
        using V = std::remove_cvref_t<decltype(given)>;
        std::pair<float, float> value = rangeEnds(given);
        auto change = P::arg<names::OnValueChange>(args...);
        std::function<void(std::pair<float, float>)> onValueChange = [change](std::pair<float, float> v) mutable { change(V{v.first, v.second}); };
        SliderOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::ValueFrom>(options.valueFrom, args...);
        P::set<names::ValueTo>(options.valueTo, args...);
        P::set<names::Steps>(options.steps, args...);
        P::set<names::OnValueChangeFinished>(options.onValueChangeFinished, args...);
        P::set<names::SliderColors>(options.colors, args...);
        P::set<names::Colors>(options.colors, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::EndInteractionSource>(options.endInteractionSource, args...);
        P::set<names::Thumb>(options.thumb, args...);
        P::set<names::StartThumb>(options.startThumb, args...);
        P::set<names::EndThumb>(options.endThumb, args...);
        P::set<names::RangeTrack>(options.rangeTrack, args...);
        P::set<names::Track>(options.rangeTrack, args...);  // (Compose's name for a RangeSlider's track)
        if constexpr (P::given<names::ValueRange, A...>()) std::tie(options.valueFrom, options.valueTo) = rangeEnds(P::arg<names::ValueRange>(args...));
        detail::RangeSlider(value, std::move(onValueChange), P::get<names::Modifier>(Modifier{}, args...), options, site);
    }
    template <class... A>
    static void stateForm(CallSite site, A&&... args) {
        using P = Params<names::State, names::Modifier, names::Enabled, names::SliderColors, names::Colors, names::InteractionSource, names::EndInteractionSource, names::Thumb, names::StartThumb, names::EndThumb, names::RangeTrack, names::Track>;
        static_assert(P::check<A...>());
        const RangeSliderState& state = P::arg<names::State>(args...);
        SliderOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::SliderColors>(options.colors, args...);
        P::set<names::Colors>(options.colors, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::EndInteractionSource>(options.endInteractionSource, args...);
        P::set<names::Thumb>(options.thumb, args...);
        P::set<names::StartThumb>(options.startThumb, args...);
        P::set<names::EndThumb>(options.endThumb, args...);
        P::set<names::RangeTrack>(options.rangeTrack, args...);
        detail::RangeSlider(state, P::get<names::Modifier>(Modifier{}, args...), options, site);
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {  // the value's form, or the state's
        if constexpr (countOf<names::State, A...> > 0 || startsWith<RangeSliderState, A...>()) stateForm(site, std::forward<A>(args)...);
        else valueForm(site, std::forward<A>(args)...);
    }
};
RangeSliderState rememberRangeSliderState(float activeRangeStart = 0, float activeRangeEnd = 1, int steps = 0, float valueFrom = 0,
                                          float valueTo = 1, std::function<void()> onValueChangeFinished = {},
                                          CallSite site = CallSite::current());
// rememberRangeSliderState(activeRangeStart = …, activeRangeEnd = …, steps = …, valueRange = 0f..24f) as Compose writes it.
struct RememberRangeSliderStateCall {
    RangeSliderState operator()(float activeRangeStart = 0, float activeRangeEnd = 1, int steps = 0, float valueFrom = 0, float valueTo = 1,
                                std::function<void()> onValueChangeFinished = {}, CallSite site = CallSite::current()) const {
        return rememberRangeSliderState(activeRangeStart, activeRangeEnd, steps, valueFrom, valueTo, std::move(onValueChangeFinished), site);
    }
    template <class... A>
    static RangeSliderState named(CallSite site, A&&... args) {
        using P = Params<names::ActiveRangeStart, names::ActiveRangeEnd, names::Steps, names::OnValueChangeFinished, names::ValueRange>;
        static_assert(P::check<A...>());
        std::pair<float, float> range{0, 1};
        if constexpr (P::given<names::ValueRange, A...>()) range = rangeEnds(P::arg<names::ValueRange>(args...));
        return rememberRangeSliderState(P::get<names::ActiveRangeStart>(0.f, args...), P::get<names::ActiveRangeEnd>(1.f, args...),
                                        P::get<names::Steps>(0, args...), range.first, range.second,
                                        P::get<names::OnValueChangeFinished>(std::function<void()>(), args...), site);
    }
    template <class A1> requires isNamed<A1>
    RangeSliderState operator()(A1&& a1, CallSite site = CallSite::current()) const { return named(site, std::forward<A1>(a1)); }
    template <class A1, class A2> requires isNamed<A1>
    RangeSliderState operator()(A1&& a1, A2&& a2, CallSite site = CallSite::current()) const { return named(site, std::forward<A1>(a1), std::forward<A2>(a2)); }
    template <class A1, class A2, class A3> requires isNamed<A1>
    RangeSliderState operator()(A1&& a1, A2&& a2, A3&& a3, CallSite site = CallSite::current()) const { return named(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3)); }
    template <class A1, class A2, class A3, class A4> requires isNamed<A1>
    RangeSliderState operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, CallSite site = CallSite::current()) const { return named(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4)); }
    template <class A1, class A2, class A3, class A4, class A5> requires isNamed<A1>
    RangeSliderState operator()(A1&& a1, A2&& a2, A3&& a3, A4&& a4, A5&& a5, CallSite site = CallSite::current()) const { return named(site, std::forward<A1>(a1), std::forward<A2>(a2), std::forward<A3>(a3), std::forward<A4>(a4), std::forward<A5>(a5)); }
};
} // namespace detail
inline constexpr Composable<detail::RangeSliderImpl> RangeSlider;
inline constexpr detail::RememberRangeSliderStateCall rememberRangeSliderState;

// Connected toggle buttons: SegmentedButtons inside a SingleChoiceSegmentedButtonRow (one of
// them selected: radio buttons to accessibility services) or a MultiChoiceSegmentedButtonRow
// (each on or off: checkboxes).
//   MultiChoiceSegmentedButtonRow(Modifier(), [&] {
//       SegmentedButton(walk.get(), [walk](bool on) { walk.set(on); }, [] { Text("Yürüyüş"); });
//       ...
//   });
struct SegmentedButtonOptions {
    bool enabled = true;
    Content icon;  // shown instead of the check mark a selected one has
    std::optional<MutableInteractionSource> interactionSource;
    std::optional<Shape> shape;  // the segment's own (SegmentedButtonDefaults::itemShape): clipped to it
};
namespace SegmentedButtonDefaults {
// Compose's itemShape(index, count): the first segment round at its start, the last at its end, the
// rest square (the row clips them all to its pill anyway).
Shape itemShape(int index, int count);
}  // namespace SegmentedButtonDefaults
namespace detail {
void SingleChoiceSegmentedButtonRow(const Modifier& modifier, const Content& content,
                                    CallSite site = CallSite::current());
void MultiChoiceSegmentedButtonRow(const Modifier& modifier, const Content& content,
                                   CallSite site = CallSite::current());
void SegmentedButton(bool selected, std::function<void()> onClick, const Content& label,
                     const Modifier& modifier = {}, CallSite site = CallSite::current());
void SegmentedButton(bool selected, std::function<void()> onClick, const Content& label, const Modifier& modifier,
                     const SegmentedButtonOptions& options, CallSite site = CallSite::current());
// In a MultiChoiceSegmentedButtonRow: onCheckedChange(!checked) on a tap.
void SegmentedButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& label,
                     const Modifier& modifier = {}, const SegmentedButtonOptions& options = {}, CallSite site = CallSite::current());
struct SingleChoiceSegmentedButtonRowImpl {
    using P = Params<names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: SingleChoiceSegmentedButtonRow takes a block: its SegmentedButtons");
        SingleChoiceSegmentedButtonRow(P::get<names::Modifier>(Modifier{}, args...), P::block(std::forward<A>(args)...), site);
    }
};
struct MultiChoiceSegmentedButtonRowImpl {
    using P = Params<names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: MultiChoiceSegmentedButtonRow takes a block: its SegmentedButtons");
        MultiChoiceSegmentedButtonRow(P::get<names::Modifier>(Modifier{}, args...), P::block(std::forward<A>(args)...), site);
    }
};
// SegmentedButton as Compose writes it: SegmentedButton(selected = …, onClick = …, shape =
// SegmentedButtonDefaults.itemShape(i, n), icon = { … }) { Text("…") } in a single-choice row,
// SegmentedButton(checked = …, onCheckedChange = …) { … } in a multi-choice one; the label is the
// block or label = { … }. The C++ forms (selected, onClick, label, modifier, options) go by place.
struct SegmentedButtonImpl {
    using Single = Params<names::Selected, names::OnClick, names::Shape, names::Modifier, names::Enabled, names::InteractionSource, names::Icon,
                          names::Label, Block>;
    using Multi = Params<names::Checked, names::OnCheckedChange, names::Shape, names::Modifier, names::Enabled, names::InteractionSource,
                         names::Icon, names::Label, Block>;
    template <class... A>
    static constexpr bool byPlace() {  // (selected, onClick, label, …): the third argument its label
        if constexpr (sizeof...(A) < 3 || (isNamed<A> || ...)) return false;
        else return std::is_convertible_v<std::tuple_element_t<2, std::tuple<A...>>, Content>;
    }
    template <class Q, class... A>
    static Content labelOf(A&&... args) {
        if constexpr (Q::template given<names::Label, A...>()) return Content(Q::template arg<names::Label>(args...));
        else {
            static_assert(Q::template blockGiven<A...>(), "kompoz: SegmentedButton takes a block: its label");
            return Q::block(std::forward<A>(args)...);
        }
    }
    template <class Q, class... A>
    static SegmentedButtonOptions optionsOf(A&... args) {
        SegmentedButtonOptions o;
        Q::template set<names::Enabled>(o.enabled, args...);
        Q::template set<names::Icon>(o.icon, args...);
        Q::template set<names::InteractionSource>(o.interactionSource, args...);
        if constexpr (Q::template given<names::Shape, A...>()) o.shape = Shape(Q::template arg<names::Shape>(args...));
        return o;
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (byPlace<A...>()) {
            auto all = std::forward_as_tuple(std::forward<A>(args)...);
            Modifier modifier;
            SegmentedButtonOptions options;
            if constexpr (sizeof...(A) >= 4) modifier = std::get<3>(all);
            if constexpr (sizeof...(A) >= 5) options = std::get<4>(all);
            if constexpr (std::is_invocable_v<std::remove_cvref_t<std::tuple_element_t<1, std::tuple<A...>>>&, bool>)
                detail::SegmentedButton(bool(std::get<0>(all)), std::function<void(bool)>(std::get<1>(all)), Content(std::get<2>(all)), modifier, options, site);
            else
                detail::SegmentedButton(bool(std::get<0>(all)), std::function<void()>(std::get<1>(all)), Content(std::get<2>(all)), modifier, options, site);
        } else if constexpr (countOf<names::Checked, A...> + countOf<names::OnCheckedChange, A...> > 0) {
            using Q = Multi;
            static_assert(Q::check<A...>());
            detail::SegmentedButton(bool(Q::arg<names::Checked>(args...)), std::function<void(bool)>(Q::arg<names::OnCheckedChange>(args...)),
                                    labelOf<Q>(std::forward<A>(args)...), Q::get<names::Modifier>(Modifier{}, args...), optionsOf<Q>(args...), site);
        } else {
            using Q = Single;
            static_assert(Q::check<A...>());
            detail::SegmentedButton(bool(Q::arg<names::Selected>(args...)), std::function<void()>(Q::arg<names::OnClick>(args...)),
                                    labelOf<Q>(std::forward<A>(args)...), Q::get<names::Modifier>(Modifier{}, args...), optionsOf<Q>(args...), site);
        }
    }
};
// The C++ forms whose options are made in place (SegmentedButton(on, onClick, label, modifier,
// {.enabled = false})): a braced list goes to a parameter of its type, not through the template.
struct SegmentedButtonCall : Composable<SegmentedButtonImpl> {
    using Composable<SegmentedButtonImpl>::operator();
    void operator()(bool selected, std::function<void()> onClick, const Content& label, const Modifier& modifier,
                    const SegmentedButtonOptions& options, CallSite site = CallSite::current()) const {
        SegmentedButton(selected, std::move(onClick), label, modifier, options, site);
    }
    void operator()(bool checked, std::function<void(bool)> onCheckedChange, const Content& label, const Modifier& modifier,
                    const SegmentedButtonOptions& options, CallSite site = CallSite::current()) const {
        SegmentedButton(checked, std::move(onCheckedChange), label, modifier, options, site);
    }
};
}  // namespace detail
inline constexpr Composable<detail::SingleChoiceSegmentedButtonRowImpl> SingleChoiceSegmentedButtonRow;
inline constexpr Composable<detail::MultiChoiceSegmentedButtonRowImpl> MultiChoiceSegmentedButtonRow;
inline constexpr detail::SegmentedButtonCall SegmentedButton;

// M3 Expressive's button group (Compose's ButtonGroup): buttons in a row, spaced apart. The
// one held grows by expandedRatio of its width and squeezes its neighbours (half each, or
// all of it from the only one), the row keeping its width. A button takes part through
// group.animateWidth(source), with the interaction source it was given:
//   ButtonGroup([&](const ButtonGroupScope& group) {
//       auto source = remember([] { return MutableInteractionSource(); });
//       Button(onClick, group.animateWidth(source), interactionSource = source, [] { Text("Oynat"); });
//       ...
//   });
// Compose's ButtonGroupMenuState, what the items form's overflowIndicator lambda gets: whether the
// menu of the items that didn't fit is open, show() and dismiss().
class ButtonGroupMenuState {
public:
    explicit ButtonGroupMenuState(MutableState<bool> open) : open_(std::move(open)) {}
    bool isExpanded() const { return open_.get(); }
    void show() const { open_.set(true); }
    void dismiss() const { open_.set(false); }

private:
    MutableState<bool> open_;
};
struct ButtonGroupOptions {
    float expandedRatio = 0.15f;
    float spacing = 12;  // dp between the buttons
    // The items form's menu button, composed in place of its ⋮ IconButton (Compose's overflowIndicator):
    // it shows or dismisses the menu through the state.
    std::function<void(const ButtonGroupMenuState&)> overflowIndicator;
};
// A button group from items (Compose's ButtonGroup with an overflowIndicator): buttons while they
// fit — toggle buttons for items with a checked state — and the rest in a ⋮ menu at its end.
struct ButtonGroupItem {
    std::string label;
    Content icon;                             // before the label (Compose's icon = { Icon(…) })
    std::function<void()> onClick;            // a button
    std::optional<bool> checked;              // set: a toggle button (in the menu, a check mark)
    std::function<void(bool)> onCheckedChange;
    bool enabled = true;
};
class ButtonGroupScope {
public:
    // Chain more with .then() or after it: group.animateWidth(source).testTag("oynat").
    Modifier animateWidth(const MutableInteractionSource& source, CallSite site = CallSite::current()) const;
    // Compose's ButtonGroupMenuScope, in a ButtonGroup(overflowIndicator = …) { … } block: the items it
    // declares, clickableItem(onClick = …, label = "…", icon = { … }, enabled = …) and
    // toggleableItem(checked = …, onCheckedChange = …, label = …, icon = …, enabled = …).
    template <class... A>
    void clickableItem(A&&... args) {
        using P = detail::Params<detail::names::OnClick, detail::names::Label, detail::names::Icon, detail::names::Enabled>;
        static_assert(P::check<A...>());
        ButtonGroupItem item;
        item.onClick = std::function<void()>(P::arg<detail::names::OnClick>(args...));
        item.label = std::string(P::arg<detail::names::Label>(args...));
        P::set<detail::names::Icon>(item.icon, args...);
        P::set<detail::names::Enabled>(item.enabled, args...);
        declare(std::move(item));
    }
    template <class... A>
    void toggleableItem(A&&... args) {
        using P = detail::Params<detail::names::Checked, detail::names::OnCheckedChange, detail::names::Label, detail::names::Icon, detail::names::Enabled>;
        static_assert(P::check<A...>());
        ButtonGroupItem item;
        item.checked = bool(P::arg<detail::names::Checked>(args...));
        item.onCheckedChange = std::function<void(bool)>(P::arg<detail::names::OnCheckedChange>(args...));
        item.label = std::string(P::arg<detail::names::Label>(args...));
        P::set<detail::names::Icon>(item.icon, args...);
        P::set<detail::names::Enabled>(item.enabled, args...);
        declare(std::move(item));
    }
    std::vector<ButtonGroupItem>* items = nullptr;  // (the items form's, as its block declares them)

private:
    void declare(ButtonGroupItem item);
};
namespace detail {
void ButtonGroup(const std::function<void(ButtonGroupScope&)>& content, CallSite site = CallSite::current());
void ButtonGroup(const Modifier& modifier, const ButtonGroupOptions& options, const std::function<void(ButtonGroupScope&)>& content,
                 CallSite site = CallSite::current());
void ButtonGroup(const std::vector<ButtonGroupItem>& items, const Modifier& modifier = {}, const ButtonGroupOptions& options = {},
                 CallSite site = CallSite::current());
// ButtonGroup as Compose writes it: ButtonGroup(modifier = …, horizontalArrangement = Arrangement.spacedBy(…),
// expandedRatio = …) { Button(modifier = Modifier.animateWidth(source), …) { } } for the connected
// group, and with overflowIndicator = { menuState -> … } the group from items, its block declaring
// them (clickableItem, toggleableItem). The C++ forms with a vector of items or a ButtonGroupOptions
// go by place.
struct ButtonGroupImpl {
    using P = Params<names::OverflowIndicator, names::Modifier, names::HorizontalArrangement, names::ExpandedRatio, BlockWith<ButtonGroupScope&>>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr ((std::is_same_v<std::remove_cvref_t<A>, std::vector<ButtonGroupItem>> || ...) ||
                      (std::is_same_v<std::remove_cvref_t<A>, ButtonGroupOptions> || ...)) {
            static_assert(!(isNamed<A> || ...), "kompoz: ButtonGroup's forms with items or options take their arguments by place");
            detail::ButtonGroup(std::forward<A>(args)..., site);
        } else {
            static_assert(P::check<A...>());
            static_assert(P::blockGiven<A...>(), "kompoz: ButtonGroup takes a block: its buttons, or with an overflowIndicator its items");
            ButtonGroupOptions options;
            P::set<names::ExpandedRatio>(options.expandedRatio, args...);
            P::set<names::OverflowIndicator>(options.overflowIndicator, args...);
            if constexpr (P::given<names::HorizontalArrangement, A...>()) options.spacing = P::get<names::HorizontalArrangement>(Arrangement{}, args...).spacing;
            Modifier modifier = P::get<names::Modifier>(Modifier{}, args...);
            auto&& block = std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...));
            if (options.overflowIndicator) {  // the items form: the block declares them
                std::vector<ButtonGroupItem> items;
                ButtonGroupScope scope;
                scope.items = &items;
                block(scope);
                detail::ButtonGroup(items, modifier, options, site);
            } else {
                detail::ButtonGroup(modifier, options, std::function<void(ButtonGroupScope&)>(block), site);
            }
        }
    }
};
// The C++ forms whose options or items are made in place (ButtonGroup(modifier, {.expandedRatio = …}, [&](const
// ButtonGroupScope& g) { … }), ButtonGroup({{.label = …}, …})): a braced list goes to a parameter of its type.
struct ButtonGroupCall : Composable<ButtonGroupImpl> {
    using Composable<ButtonGroupImpl>::operator();
    void operator()(const Modifier& modifier, const ButtonGroupOptions& options, const std::function<void(ButtonGroupScope&)>& content,
                    CallSite site = CallSite::current()) const {
        ButtonGroup(modifier, options, content, site);
    }
    void operator()(const std::vector<ButtonGroupItem>& items, const Modifier& modifier = {}, const ButtonGroupOptions& options = {},
                    CallSite site = CallSite::current()) const {
        ButtonGroup(items, modifier, options, site);
    }
};
} // namespace detail
inline constexpr detail::ButtonGroupCall ButtonGroup;

// A connected button group (M3 Expressive's successor to segmented buttons): ToggleButtons
// 2 dp apart in a Row, the first and last rounded on their outer side, the others squarish;
// a checked one turns fully round.
//   Row(Modifier(), horizontalArrangement = Arrangement::spacedBy(ButtonGroupDefaults::ConnectedSpaceBetween), [=] {
//       for (int i = 0; i < 3; ++i)
//           ToggleButton(picked.get() == i, [picked, i](bool) { picked.set(i); }, Modifier().weight(1),
//                        {.shapes = i == 0 ? ButtonGroupDefaults::connectedLeadingButtonShapes()
//                                   : i == 2 ? ButtonGroupDefaults::connectedTrailingButtonShapes()
//                                            : ButtonGroupDefaults::connectedMiddleButtonShapes()}, ...);
//   });
namespace ButtonGroupDefaults {
inline constexpr float ConnectedSpaceBetween = 2;  // dp
inline constexpr float ConnectedInnerCorner = 8, ConnectedPressedInnerCorner = 4;
inline ToggleButtonShapes connectedLeadingButtonShapes() {
    return {RoundedCornerShape(CornerPercent(50), {ConnectedInnerCorner}, {ConnectedInnerCorner}, CornerPercent(50)),
            RoundedCornerShape(CornerPercent(50), {ConnectedPressedInnerCorner}, {ConnectedPressedInnerCorner}, CornerPercent(50)),
            CircleShape};
}
inline ToggleButtonShapes connectedMiddleButtonShapes() {
    return {RoundedCornerShape(ConnectedInnerCorner), RoundedCornerShape(ConnectedPressedInnerCorner), CircleShape};
}
inline ToggleButtonShapes connectedTrailingButtonShapes() {
    return {RoundedCornerShape({ConnectedInnerCorner}, CornerPercent(50), CornerPercent(50), {ConnectedInnerCorner}),
            RoundedCornerShape({ConnectedPressedInnerCorner}, CornerPercent(50), CornerPercent(50), {ConnectedPressedInnerCorner}),
            CircleShape};
}
} // namespace ButtonGroupDefaults

// Tabs of equal width; the indicator slides to the selected one, as wide as its content.
void PrimaryTabRow(int selectedTabIndex, const Modifier& modifier, const Content& tabs,
                   CallSite site = CallSite::current());
// The same for a second level of tabs: a thin indicator under the whole tab, the selected one
// in onSurface rather than primary.
void SecondaryTabRow(int selectedTabIndex, const Modifier& modifier, const Content& tabs,
                     CallSite site = CallSite::current());
struct ScrollableTabRowOptions {
    std::optional<ScrollState> scrollState;  // by default one of its own
    float edgePadding = 52;                  // before the first tab and after the last
};
// Tabs as wide as their content (at least 90 dp) in a row that scrolls sideways; a newly
// selected tab scrolls into view, centered where it can be (Compose's ScrollableTabRow).
void PrimaryScrollableTabRow(int selectedTabIndex, const Modifier& modifier, const Content& tabs,
                             ScrollableTabRowOptions options = {}, CallSite site = CallSite::current());
void SecondaryScrollableTabRow(int selectedTabIndex, const Modifier& modifier, const Content& tabs,
                               ScrollableTabRowOptions options = {}, CallSite site = CallSite::current());
// A tab's colors (Compose's selectedContentColor and unselectedContentColor): by default the
// row's content color (primary, onSurface in a secondary row) selected or not, as in Compose —
// pass unselectedContentColor (colorScheme().onSurfaceVariant) to tell them apart.
struct TabOptions {
    bool enabled = true;
    std::optional<Color> selectedContentColor, unselectedContentColor;
    std::optional<MutableInteractionSource> interactionSource;
};
// 48 dp, 72 with an icon above the text (lined up by the text's baselines, as Compose's).
void Tab(bool selected, std::function<void()> onClick, const Content& text, const Content& icon = {},
         const Modifier& modifier = {}, CallSite site = CallSite::current());
void Tab(bool selected, std::function<void()> onClick, const Content& text, const Content& icon, const Modifier& modifier,
         const TabOptions& options, CallSite site = CallSite::current());
// A tab with its icon before its text, 48 dp tall (Compose's LeadingIconTab); a primary row's
// indicator spans both.
void LeadingIconTab(bool selected, std::function<void()> onClick, const Content& text, const Content& icon,
                    const Modifier& modifier = {}, CallSite site = CallSite::current());
void LeadingIconTab(bool selected, std::function<void()> onClick, const Content& text, const Content& icon, const Modifier& modifier,
                    const TabOptions& options, CallSite site = CallSite::current());

// A pill to drag (Compose's VerticalDragHandle): 4 × 48 dp in outline, 12 × 52 dp in onSurface
// while pressed or dragged, centered in the room it is given (paneExpansionDraggable).
void VerticalDragHandle(const Modifier& modifier = {}, std::optional<MutableInteractionSource> interactionSource = {},
                        CallSite site = CallSite::current());

// A dot, or a small pill with a count; BadgedBox puts it on the top-end corner of its content.
void Badge(const Modifier& modifier = {}, const Content& content = {}, CallSite site = CallSite::current());
void BadgedBox(const Content& badge, const Modifier& modifier, const Content& content,
               CallSite site = CallSite::current());

// A bottom app bar that leaves as the content scrolls forwards and comes back as it scrolls
// back (Compose's BottomAppBarScrollBehavior): the bar loses as much of its height, sliding down,
// and a half way release finishes the move. Its state's offset is how much is out of view.
using BottomAppBarState = FloatingToolbarState;
struct BottomAppBarScrollBehavior {
    BottomAppBarState state;
    std::shared_ptr<NestedScrollConnection> nestedScrollConnection;  // on a parent of the scrolling content (the Scaffold)
    friend bool operator==(const BottomAppBarScrollBehavior&, const BottomAppBarScrollBehavior&) = default;
};
inline BottomAppBarState rememberBottomAppBarState(CallSite site = CallSite::current()) { return rememberFloatingToolbarState(site); }
namespace BottomAppBarDefaults {
BottomAppBarScrollBehavior exitAlwaysScrollBehavior(CallSite site = CallSite::current());
// ...with a state of the caller's (rememberBottomAppBarState: read elsewhere, or saved).
BottomAppBarScrollBehavior exitAlwaysScrollBehavior(BottomAppBarState state, CallSite site = CallSite::current());
} // namespace BottomAppBarDefaults

struct BottomAppBarSlots {
    Content actions, floatingActionButton;
    // Compose's BottomAppBar(…) { content }: a row of its own instead of the actions and the FAB.
    Content content;
    std::optional<Color> containerColor;
    std::optional<Color> contentColor;  // unset: onSurfaceVariant (the content's: its container's own)
    bool windowInsets = true;  // pad by the navigation bar
    std::optional<BottomAppBarScrollBehavior> scrollBehavior;
};
namespace detail {
void BottomAppBar(const BottomAppBarSlots& slots, const Modifier& modifier = {}, CallSite site = CallSite::current());
struct BottomAppBarImpl {
    using P = Params<names::Actions, names::Modifier, names::FloatingActionButton, names::ContainerColor, names::ContentColor, names::WindowInsets, names::ScrollBehavior>;
    using WithContent = Params<names::Modifier, names::ContainerColor, names::ContentColor, names::WindowInsets, names::ScrollBehavior, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (countOf<names::Actions, A...> == 0 && WithContent::blockGiven<A...>()) {  // BottomAppBar(…) { content }
            using Q = WithContent;
            static_assert(Q::check<A...>());
            BottomAppBarSlots slots;
            slots.content = Q::block(std::forward<A>(args)...);
            Q::set<names::ContainerColor>(slots.containerColor, args...);
            Q::set<names::ContentColor>(slots.contentColor, args...);
            Q::set<names::WindowInsets>(slots.windowInsets, args...);
            Q::set<names::ScrollBehavior>(slots.scrollBehavior, args...);
            BottomAppBar(slots, Q::get<names::Modifier>(Modifier{}, args...), site);
        } else {
            static_assert(P::check<A...>());
            BottomAppBarSlots slots;
            P::set<names::Actions>(slots.actions, args...);
            P::set<names::FloatingActionButton>(slots.floatingActionButton, args...);
            P::set<names::ContainerColor>(slots.containerColor, args...);
            P::set<names::ContentColor>(slots.contentColor, args...);
            P::set<names::WindowInsets>(slots.windowInsets, args...);
            P::set<names::ScrollBehavior>(slots.scrollBehavior, args...);
            BottomAppBar(slots, P::get<names::Modifier>(Modifier{}, args...), site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::BottomAppBarImpl> BottomAppBar;

// M3 Expressive's flexible bottom app bar (Compose's FlexibleBottomAppBar): 64 dp, its content
// spread out across it (SpaceBetween, 16 dp in from the sides) by default.
struct FlexibleBottomAppBarOptions {
    std::optional<Color> containerColor;  // surfaceContainer
    float expandedHeight = 64;
    Arrangement horizontalArrangement = Arrangement::SpaceBetween;
    PaddingValues contentPadding{16, 0, 16, 0};
    std::optional<BottomAppBarScrollBehavior> scrollBehavior;
    bool windowInsets = true;  // pad by the navigation bar
};
namespace detail {
void FlexibleBottomAppBar(const Modifier& modifier, const Content& content, const FlexibleBottomAppBarOptions& options = {},
                          CallSite site = CallSite::current());
struct FlexibleBottomAppBarImpl {
    using P = Params<names::Modifier, names::ContainerColor, names::ContentPadding, names::HorizontalArrangement, names::ExpandedHeight, names::WindowInsets, names::ScrollBehavior, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: FlexibleBottomAppBar takes a block: its content");
        FlexibleBottomAppBarOptions options;
        P::set<names::ContainerColor>(options.containerColor, args...);
        P::set<names::ContentPadding>(options.contentPadding, args...);
        P::set<names::HorizontalArrangement>(options.horizontalArrangement, args...);
        P::set<names::ExpandedHeight>(options.expandedHeight, args...);
        P::set<names::WindowInsets>(options.windowInsets, args...);
        P::set<names::ScrollBehavior>(options.scrollBehavior, args...);
        FlexibleBottomAppBar(P::get<names::Modifier>(Modifier{}, args...), P::block(std::forward<A>(args)...), options, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::FlexibleBottomAppBarImpl> FlexibleBottomAppBar;

// ---- Dialogs, menus and sheets (popups above the screen) ------------------------------

struct DialogProperties {
    bool dismissOnBackPress = true, dismissOnClickOutside = true;
    // Kept the platform's way from the window's edges (48 dp at the sides, 24 at the top and
    // bottom); false: as big as the window lets it be (a full screen dialog).
    bool usePlatformDefaultWidth = true;
};
// Centered over a scrim. Compose it while it should show: if (open) Dialog(...).
namespace detail {
void Dialog(std::function<void()> onDismissRequest, const Content& content, DialogProperties properties = {},
            CallSite site = CallSite::current());
struct DialogImpl {
    using P = Params<names::OnDismissRequest, names::Properties, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onDismissRequest = P::arg<names::OnDismissRequest>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: Dialog takes a block: its content");
        Dialog(std::move(onDismissRequest), P::block(std::forward<A>(args)...), P::get<names::Properties>(DialogProperties{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::DialogImpl> Dialog;

namespace AlertDialogDefaults {
inline constexpr float cornerRadius = 28;  // its shape: RoundedCornerShape(28)
Color containerColor();     // surfaceContainerHigh
Color iconContentColor();   // secondary
Color titleContentColor();  // onSurface
Color textContentColor();   // onSurfaceVariant
} // namespace AlertDialogDefaults

struct AlertDialogSlots {
    Content confirmButton, dismissButton, icon, title, text;
    // AlertDialogDefaults' when unset.
    std::optional<Shape> shape;
    std::optional<Color> containerColor, iconContentColor, titleContentColor, textContentColor;
};
// Material 3's dialog: the icon (centered, with the title under it), title, text (giving way
// when the window is short) and the buttons at the end, in rows that wrap with the confirming
// one above. 280 to 560 dp wide.
namespace detail {
void AlertDialog(std::function<void()> onDismissRequest, const AlertDialogSlots& slots, const Modifier& modifier = {},
                 DialogProperties properties = {}, CallSite site = CallSite::current());
struct AlertDialogImpl {
    using P = Params<names::OnDismissRequest, names::ConfirmButton, names::Modifier, names::DismissButton, names::Icon, names::Title, names::Text, names::Shape, names::ContainerColor, names::IconContentColor, names::TitleContentColor, names::TextContentColor, names::Properties>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onDismissRequest = P::arg<names::OnDismissRequest>(args...);
        AlertDialogSlots slots;
        P::set<names::ConfirmButton>(slots.confirmButton, args...);
        P::set<names::DismissButton>(slots.dismissButton, args...);
        P::set<names::Icon>(slots.icon, args...);
        P::set<names::Title>(slots.title, args...);
        P::set<names::Text>(slots.text, args...);
        P::set<names::Shape>(slots.shape, args...);
        P::set<names::ContainerColor>(slots.containerColor, args...);
        P::set<names::IconContentColor>(slots.iconContentColor, args...);
        P::set<names::TitleContentColor>(slots.titleContentColor, args...);
        P::set<names::TextContentColor>(slots.textContentColor, args...);
        AlertDialog(std::move(onDismissRequest), slots, P::get<names::Modifier>(Modifier{}, args...), P::get<names::Properties>(DialogProperties{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::AlertDialogImpl> AlertDialog;
// A dialog with content of your own (Compose's BasicAlertDialog): 280 to 560 dp wide, the
// content at least as wide as that (give it a Surface or card to sit on).
namespace detail {
void BasicAlertDialog(std::function<void()> onDismissRequest, const Modifier& modifier, const Content& content,
                      DialogProperties properties = {}, CallSite site = CallSite::current());
struct BasicAlertDialogImpl {
    using P = Params<names::OnDismissRequest, names::Modifier, names::Properties, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onDismissRequest = P::arg<names::OnDismissRequest>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: BasicAlertDialog takes a block: its content");
        BasicAlertDialog(std::move(onDismissRequest), P::get<names::Modifier>(Modifier{}, args...), P::block(std::forward<A>(args)...), P::get<names::Properties>(DialogProperties{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::BasicAlertDialogImpl> BasicAlertDialog;

// Opens below the node it is composed in (or above it, near the bottom of the window):
//   Box([=] { IconButton(open, ...); DropdownMenu(expanded.get(), close, [=] { DropdownMenuItem(...); }); });
namespace detail {
void DropdownMenu(bool expanded, std::function<void()> onDismissRequest, const Content& content,
                  const Modifier& modifier = {}, CallSite site = CallSite::current());
struct DropdownMenuImpl {
    using P = Params<names::Expanded, names::OnDismissRequest, names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        bool expanded = P::arg<names::Expanded>(args...);
        std::function<void()> onDismissRequest = P::arg<names::OnDismissRequest>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: DropdownMenu takes a block: its content");
        DropdownMenu(expanded, std::move(onDismissRequest), P::block(std::forward<A>(args)...), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::DropdownMenuImpl> DropdownMenu;
struct MenuItemOptions {
    Content leadingIcon, trailingIcon;
    bool enabled = true;
    Content supportingText;  // a line under the text
    // In a DropdownMenuGroup: the item's shape (MenuDefaults::itemShape), and what stands before
    // a checked or selected item's text (a check by default).
    std::optional<Shape> shape;
    Content checkedLeadingIcon;
};

// M3 Expressive menus (Compose's DropdownMenuPopup and DropdownMenuGroup): the popup has no card
// of its own, each group is one, MenuDefaults::GroupSpacing apart, rounder at the menu's ends.
//   DropdownMenuPopup(open.get(), close, [=] {
//       DropdownMenuGroup(MenuDefaults::groupShape(0, 2), [=] { DropdownMenuItem(bold, setBold, [] { Text("Kalın"); }); });
//       Spacer(Modifier().height(MenuDefaults::GroupSpacing));
//       DropdownMenuGroup(MenuDefaults::groupShape(1, 2), [=] { ... });
//   });
namespace detail {
void DropdownMenuPopup(bool expanded, std::function<void()> onDismissRequest, const Content& content, const Modifier& modifier = {},
                       CallSite site = CallSite::current());
struct DropdownMenuPopupImpl {
    using P = Params<names::Expanded, names::OnDismissRequest, names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        bool expanded = P::arg<names::Expanded>(args...);
        std::function<void()> onDismissRequest = P::arg<names::OnDismissRequest>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: DropdownMenuPopup takes a block: its content");
        DropdownMenuPopup(expanded, std::move(onDismissRequest), P::block(std::forward<A>(args)...), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::DropdownMenuPopupImpl> DropdownMenuPopup;
namespace detail {
void DropdownMenuGroup(const Shape& shape, const Content& content, const Modifier& modifier = {}, CallSite site = CallSite::current());
struct DropdownMenuGroupImpl {
    using P = Params<names::Shape, names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        const Shape& shape = P::arg<names::Shape>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: DropdownMenuGroup takes a block: its content");
        DropdownMenuGroup(shape, P::block(std::forward<A>(args)...), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::DropdownMenuGroupImpl> DropdownMenuGroup;
namespace MenuDefaults {
inline constexpr float GroupSpacing = 2;
Shape groupShape(int index, int count);  // 16 dp at the menu's ends, 4 dp where groups meet
Shape itemShape(int index, int count);   // 12 dp at a group's ends, 4 dp between items
} // namespace MenuDefaults
// Items that are on or off (a check before the text when on) or one of several to pick
// (Compose's DropdownMenuItem(checked, onCheckedChange) and (selected, onClick)): checked or
// selected, they sit in tertiaryContainer, 12 dp round.
namespace detail {
void DropdownMenuItem(const Content& text, std::function<void()> onClick, const MenuItemOptions& options = {},
                      CallSite site = CallSite::current());
void DropdownMenuItem(bool checked, std::function<void(bool)> onCheckedChange, const Content& text, const MenuItemOptions& options = {},
                      CallSite site = CallSite::current());
void DropdownMenuItem(bool selected, std::function<void()> onClick, const Content& text, const MenuItemOptions& options = {},
                      CallSite site = CallSite::current());
struct DropdownMenuItemImpl {
    template <class P, class... A>  // (P: the call's own parameters, for those given by their place)
    static MenuItemOptions options(A&... args) {
        MenuItemOptions options;
        P::template set<names::LeadingIcon>(options.leadingIcon, args...);
        P::template set<names::TrailingIcon>(options.trailingIcon, args...);
        P::template set<names::Enabled>(options.enabled, args...);
        P::template set<names::SupportingText>(options.supportingText, args...);
        P::template set<names::Shape>(options.shape, args...);
        P::template set<names::CheckedLeadingIcon>(options.checkedLeadingIcon, args...);
        return options;
    }
    // Which form: (text, onClick), on or off (checked, onCheckedChange, text), one of several
    // (selected, onClick, text); by the names given, or by the first two arguments' types.
    template <class... A>
    static constexpr int form() {
        if constexpr (countOf<names::Checked, A...> + countOf<names::OnCheckedChange, A...> > 0) return 1;
        else if constexpr (countOf<names::Selected, A...> > 0) return 2;
        else if constexpr (sizeof...(A) >= 2 && startsWith<bool, A...>()) {
            using F = std::remove_cvref_t<std::tuple_element_t<1, std::tuple<A...>>>;
            return std::is_invocable_v<F&, bool> ? 1 : 2;
        } else return 0;
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (form<A...>() == 0) {
            using P = Params<names::Text, names::OnClick, names::LeadingIcon, names::TrailingIcon, names::Enabled, names::SupportingText, names::Shape, names::CheckedLeadingIcon>;
            static_assert(P::check<A...>());
            static_assert(P::given<names::Text, A...>() && P::given<names::OnClick, A...>(), "kompoz: DropdownMenuItem takes a text and an onClick");
            DropdownMenuItem(P::get<names::Text>(Content{}, args...), P::get<names::OnClick>(std::function<void()>{}, args...), options<P>(args...), site);
        } else if constexpr (form<A...>() == 1) {
            using P = Params<names::Checked, names::OnCheckedChange, names::Text, names::LeadingIcon, names::TrailingIcon, names::Enabled, names::SupportingText, names::Shape, names::CheckedLeadingIcon>;
            static_assert(P::check<A...>());
            static_assert(P::given<names::Text, A...>(), "kompoz: DropdownMenuItem takes a text");
            DropdownMenuItem(bool(P::arg<names::Checked>(args...)), std::function<void(bool)>(P::arg<names::OnCheckedChange>(args...)),
                             P::get<names::Text>(Content{}, args...), options<P>(args...), site);
        } else {
            using P = Params<names::Selected, names::OnClick, names::Text, names::LeadingIcon, names::TrailingIcon, names::Enabled, names::SupportingText, names::Shape, names::CheckedLeadingIcon>;
            static_assert(P::check<A...>());
            static_assert(P::given<names::Text, A...>(), "kompoz: DropdownMenuItem takes a text");
            DropdownMenuItem(bool(P::arg<names::Selected>(args...)), std::function<void()>(P::arg<names::OnClick>(args...)), P::get<names::Text>(Content{}, args...),
                             options<P>(args...), site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::DropdownMenuItemImpl> DropdownMenuItem;

// A menu that belongs to a text field (Compose's ExposedDropdownMenuBox): a tap on the field
// opens and closes it, and the menu opens under the field (or above it) at its width.
//   ExposedDropdownMenuBox(open.get(), [open](bool v) { open.set(v); }, Modifier(), [=] {
//       TextField(choice, nullptr, Modifier().fillMaxWidth(),
//                 label = "Şehir", trailingIcon = [&] { ExposedDropdownMenuDefaults::TrailingIcon(open.get()); },
//                 readOnly = true);
//       ExposedDropdownMenu(open.get(), [open] { open.set(false); }, [=] { DropdownMenuItem(...); });
//   });
// An editable field works too: it filters as the user types, keeping the menu open.
namespace detail {
void ExposedDropdownMenuBox(bool expanded, std::function<void(bool)> onExpandedChange, const Modifier& modifier,
                            const Content& content, CallSite site = CallSite::current());
struct ExposedDropdownMenuBoxImpl {
    using P = Params<names::Expanded, names::OnExpandedChange, names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        bool expanded = P::arg<names::Expanded>(args...);
        std::function<void(bool)> onExpandedChange = P::arg<names::OnExpandedChange>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: ExposedDropdownMenuBox takes a block: its content");
        ExposedDropdownMenuBox(expanded, std::move(onExpandedChange), P::get<names::Modifier>(Modifier{}, args...), P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::ExposedDropdownMenuBoxImpl> ExposedDropdownMenuBox;
// The box's menu: scrolls when its items don't fit in about half the window.
namespace detail {
void ExposedDropdownMenu(bool expanded, std::function<void()> onDismissRequest, const Content& content,
                         const Modifier& modifier = {}, CallSite site = CallSite::current());
struct ExposedDropdownMenuImpl {
    using P = Params<names::Expanded, names::OnDismissRequest, names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        bool expanded = P::arg<names::Expanded>(args...);
        std::function<void()> onDismissRequest = P::arg<names::OnDismissRequest>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: ExposedDropdownMenu takes a block: its content");
        ExposedDropdownMenu(expanded, std::move(onDismissRequest), P::block(std::forward<A>(args)...), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::ExposedDropdownMenuImpl> ExposedDropdownMenu;
namespace ExposedDropdownMenuDefaults {
// The arrow at the end of the field, turned up while the menu is open.
void TrailingIcon(bool expanded, CallSite site = CallSite::current());
} // namespace ExposedDropdownMenuDefaults

enum class SheetValue { Hidden, Expanded, PartiallyExpanded };

// Where a modal bottom sheet rests and where it is heading (Compose's rememberModalBottomSheetState):
// a sheet taller than half the screen opens half way (PartiallyExpanded) unless
// skipPartiallyExpanded; dragged up it expands, down it hides. Its coroutines move it:
//   scope.launch([state, show]() -> Task { co_await state.hide(); show.set(false); });  // then remove it
class ModalBottomSheetState {
public:
    explicit ModalBottomSheetState(bool skipPartiallyExpanded = false);
    bool isVisible() const;           // observable: not hidden
    SheetValue currentValue() const;  // observable
    SheetValue targetValue() const;   // observable
    bool hasPartiallyExpandedState() const;
    Task show() const;  // to where it opens (half way when it can), from hidden
    Task hide() const;  // slides out (onDismissRequest isn't called: remove the sheet after)
    Task expand() const;
    Task partialExpand() const;  // where there is a half way
    float requireOffset() const;  // dp down from where it is all the way up (its height when hidden)

    friend bool operator==(const ModalBottomSheetState& a, const ModalBottomSheetState& b) { return a.data_ == b.data_; }
    struct Data;
    std::shared_ptr<Data> data_;
};
template <>
struct Saver<ModalBottomSheetState> {
    static SavedValue save(const ModalBottomSheetState& s);
    static std::optional<ModalBottomSheetState> restore(const SavedValue& v);
};
ModalBottomSheetState rememberModalBottomSheetState(bool skipPartiallyExpanded = false, CallSite site = CallSite::current());

// Slides up from the bottom over a scrim; tap the scrim, drag it down or press back to close
// (it slides out, then calls onDismissRequest). A list in it scrolls first; pulled down at its
// top it moves the sheet, and a sheet open half way expands before the list scrolls.
namespace detail {
void ModalBottomSheet(std::function<void()> onDismissRequest, const Content& content, CallSite site = CallSite::current());
void ModalBottomSheet(std::function<void()> onDismissRequest, const ModalBottomSheetState& sheetState, const Content& content,
                      CallSite site = CallSite::current());
struct ModalBottomSheetImpl {
    using P = Params<names::OnDismissRequest, names::SheetState, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onDismissRequest = P::arg<names::OnDismissRequest>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: ModalBottomSheet takes a block: its content");
        if constexpr (P::given<names::SheetState, A...>())
            ModalBottomSheet(std::move(onDismissRequest), P::get<names::SheetState>(ModalBottomSheetState{}, args...), P::block(std::forward<A>(args)...), site);
        else
            ModalBottomSheet(std::move(onDismissRequest), P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::ModalBottomSheetImpl> ModalBottomSheet;

// ---- BottomSheetScaffold ----------------------------------------------------------------

// Where a standard bottom sheet rests and where it is heading (Compose's SheetState). Its
// coroutines move it there with an animation; a drag or a fling moves it too.
class SheetState {
public:
    explicit SheetState(SheetValue initial = SheetValue::PartiallyExpanded, bool skipHiddenState = true);
    SheetValue currentValue() const;  // observable: where it rests (or last rested)
    SheetValue targetValue() const;   // observable: where it is going (currentValue at rest)
    bool isVisible() const { return currentValue() != SheetValue::Hidden; }
    bool hasExpandedState() const;  // taller than its peek
    Task expand() const;
    Task partialExpand() const;
    Task hide() const;  // not with skipHiddenState
    float requireOffset() const;  // its top, in dp from the scaffold's top (after the first layout)

    friend bool operator==(const SheetState& a, const SheetState& b) { return a.data_ == b.data_; }
    struct Data;
    std::shared_ptr<Data> data_;
};
template <>
struct Saver<SheetState> {
    static SavedValue save(const SheetState& s);
    static std::optional<SheetState> restore(const SavedValue& v);
};
SheetState rememberStandardBottomSheetState(SheetValue initialValue = SheetValue::PartiallyExpanded, bool skipHiddenState = true,
                                            CallSite site = CallSite::current());

struct BottomSheetScaffoldState {
    SheetState bottomSheetState;
};
BottomSheetScaffoldState rememberBottomSheetScaffoldState(CallSite site = CallSite::current());
BottomSheetScaffoldState rememberBottomSheetScaffoldState(SheetState bottomSheetState, CallSite site = CallSite::current());

namespace BottomSheetDefaults {
inline constexpr float SheetPeekHeight = 56;
void DragHandle(CallSite site = CallSite::current());
} // namespace BottomSheetDefaults

struct BottomSheetScaffoldOptions {
    std::optional<BottomSheetScaffoldState> scaffoldState;
    float sheetPeekHeight = BottomSheetDefaults::SheetPeekHeight;  // shown above the navigation bar while partly expanded
    std::optional<Color> sheetContainerColor;                       // surfaceContainerLow
    float sheetShadowElevation = 1;
    bool sheetSwipeEnabled = true;
    std::optional<Content> sheetDragHandle;  // BottomSheetDefaults::DragHandle; an empty Content: none
    Content topBar, snackbarHost;
    std::optional<Color> containerColor;  // the background (surface)
};

// A screen with a sheet at the bottom that is always there (Compose's BottomSheetScaffold): it
// peeks out sheetPeekHeight, a drag or a fling pulls it up to its content's height (at most
// the screen's) and back, and a list in it scrolls once the sheet is all the way up. `content`
// gets the padding that keeps it clear of the top bar and the peeking sheet.
//   auto state = rememberBottomSheetScaffoldState();
//   BottomSheetScaffold([&] { Text("Ayrıntılar"); }, Modifier(), scaffoldState = state, sheetPeekHeight = 128,
//                       [=](PaddingValues padding) { Map(Modifier().padding(padding)); });
//   scope.launch([state]() -> Task { co_await state.bottomSheetState.expand(); });
namespace detail {
void BottomSheetScaffold(const Content& sheetContent, const Modifier& modifier, const BottomSheetScaffoldOptions& options,
                         const std::function<void(PaddingValues)>& content, CallSite site = CallSite::current());
struct BottomSheetScaffoldImpl {
    using P = Params<names::SheetContent, names::Modifier, names::ScaffoldState, names::SheetPeekHeight, names::SheetContainerColor, names::SheetShadowElevation, names::SheetDragHandle, names::SheetSwipeEnabled, names::TopBar, names::SnackbarHost, names::ContainerColor, BlockWith<PaddingValues>>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::given<names::SheetContent, A...>(), "kompoz: BottomSheetScaffold takes a sheetContent");
        static_assert(P::blockGiven<A...>(), "kompoz: BottomSheetScaffold takes a block, [=](PaddingValues padding) { … }: its content");
        BottomSheetScaffoldOptions options;
        P::set<names::ScaffoldState>(options.scaffoldState, args...);
        P::set<names::SheetPeekHeight>(options.sheetPeekHeight, args...);
        P::set<names::SheetContainerColor>(options.sheetContainerColor, args...);
        P::set<names::SheetShadowElevation>(options.sheetShadowElevation, args...);
        P::set<names::SheetDragHandle>(options.sheetDragHandle, args...);
        P::set<names::SheetSwipeEnabled>(options.sheetSwipeEnabled, args...);
        P::set<names::TopBar>(options.topBar, args...);
        P::set<names::SnackbarHost>(options.snackbarHost, args...);
        P::set<names::ContainerColor>(options.containerColor, args...);
        BottomSheetScaffold(P::get<names::SheetContent>(Content{}, args...), P::get<names::Modifier>(Modifier{}, args...), options, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::BottomSheetScaffoldImpl> BottomSheetScaffold;

// ---- Navigation drawer and rail ---------------------------------------------------------

enum class DrawerValue { Closed, Open };

class DrawerState {
public:
    explicit DrawerState(DrawerValue initial = DrawerValue::Closed);
    bool isOpen() const;  // observable: the value it is at or going to
    Task open() const;
    Task close() const;
    void snapTo(DrawerValue value) const;
    float openFraction() const;  // 0 closed .. 1 open; not observable (read while drawing/laying out)
    friend bool operator==(const DrawerState& a, const DrawerState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};
template <>
struct Saver<DrawerState> {
    static SavedValue save(const DrawerState& s);
    static std::optional<DrawerState> restore(const SavedValue& v);
};
DrawerState rememberDrawerState(DrawerValue initial = DrawerValue::Closed, CallSite site = CallSite::current());

struct DrawerOptions {
    bool gesturesEnabled = true;  // drag sideways anywhere to open, on the drawer or scrim to close
};
// The drawer slides over `content` from the start edge with a scrim behind it.
namespace detail {
void ModalNavigationDrawer(const Content& drawerContent, DrawerState state, const Content& content,
                           DrawerOptions options = {}, CallSite site = CallSite::current());
struct ModalNavigationDrawerImpl {
    using P = Params<names::DrawerContent, names::DrawerState, names::GesturesEnabled, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::given<names::DrawerContent, A...>() && P::given<names::DrawerState, A...>(),
                      "kompoz: ModalNavigationDrawer takes a drawerContent and a drawerState");
        static_assert(P::blockGiven<A...>(), "kompoz: ModalNavigationDrawer takes a block: its content");
        DrawerOptions options;
        P::set<names::GesturesEnabled>(options.gesturesEnabled, args...);
        ModalNavigationDrawer(P::get<names::DrawerContent>(Content{}, args...), P::get<names::DrawerState>(DrawerState{}, args...), P::block(std::forward<A>(args)...), options, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::ModalNavigationDrawerImpl> ModalNavigationDrawer;
// In a ModalNavigationDrawer's drawer content it follows the drawer's back gesture (Android 14+,
// as M3's): stretched wider or narrower and lower, then closed; the other one takes that drawer's
// state, as M3's.
namespace detail {
void ModalDrawerSheet(const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
void ModalDrawerSheet(DrawerState drawerState, const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
struct ModalDrawerSheetImpl {
    using P = Params<names::Modifier, Block>;
    using WithDrawer = Params<names::DrawerState, names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (countOf<names::DrawerState, A...> > 0 || startsWith<DrawerState, A...>()) {  // following that drawer's back gesture
            using Q = WithDrawer;
            static_assert(Q::check<A...>());
            static_assert(Q::blockGiven<A...>(), "kompoz: ModalDrawerSheet takes a block: its content");
            ModalDrawerSheet(DrawerState(Q::arg<names::DrawerState>(args...)), Q::get<names::Modifier>(Modifier{}, args...), Q::block(std::forward<A>(args)...), site);
        } else {
            static_assert(P::check<A...>());
            static_assert(P::blockGiven<A...>(), "kompoz: ModalDrawerSheet takes a block: its content");
            ModalDrawerSheet(P::get<names::Modifier>(Modifier{}, args...), P::block(std::forward<A>(args)...), site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::ModalDrawerSheetImpl> ModalDrawerSheet;
// The drawer pushes `content` aside as it opens (no scrim); dragging and back work as for the
// modal one.
namespace detail {
void DismissibleNavigationDrawer(const Content& drawerContent, DrawerState state, const Content& content,
                                 DrawerOptions options = {}, CallSite site = CallSite::current());
struct DismissibleNavigationDrawerImpl {
    using P = Params<names::DrawerContent, names::DrawerState, names::GesturesEnabled, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::given<names::DrawerContent, A...>() && P::given<names::DrawerState, A...>(),
                      "kompoz: DismissibleNavigationDrawer takes a drawerContent and a drawerState");
        static_assert(P::blockGiven<A...>(), "kompoz: DismissibleNavigationDrawer takes a block: its content");
        DrawerOptions options;
        P::set<names::GesturesEnabled>(options.gesturesEnabled, args...);
        DismissibleNavigationDrawer(P::get<names::DrawerContent>(Content{}, args...), P::get<names::DrawerState>(DrawerState{}, args...), P::block(std::forward<A>(args)...), options, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::DismissibleNavigationDrawerImpl> DismissibleNavigationDrawer;
namespace detail {
void DismissibleDrawerSheet(const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
void DismissibleDrawerSheet(DrawerState drawerState, const Modifier& modifier, const Content& content,
                            CallSite site = CallSite::current());
struct DismissibleDrawerSheetImpl {
    using P = Params<names::Modifier, Block>;
    using WithDrawer = Params<names::DrawerState, names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (countOf<names::DrawerState, A...> > 0 || startsWith<DrawerState, A...>()) {  // following that drawer's back gesture
            using Q = WithDrawer;
            static_assert(Q::check<A...>());
            static_assert(Q::blockGiven<A...>(), "kompoz: DismissibleDrawerSheet takes a block: its content");
            DismissibleDrawerSheet(DrawerState(Q::arg<names::DrawerState>(args...)), Q::get<names::Modifier>(Modifier{}, args...), Q::block(std::forward<A>(args)...), site);
        } else {
            static_assert(P::check<A...>());
            static_assert(P::blockGiven<A...>(), "kompoz: DismissibleDrawerSheet takes a block: its content");
            DismissibleDrawerSheet(P::get<names::Modifier>(Modifier{}, args...), P::block(std::forward<A>(args)...), site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::DismissibleDrawerSheetImpl> DismissibleDrawerSheet;
// Always shown beside `content`, for wide windows.
namespace detail {
void PermanentNavigationDrawer(const Content& drawerContent, const Modifier& modifier, const Content& content,
                               CallSite site = CallSite::current());
struct PermanentNavigationDrawerImpl {
    using P = Params<names::DrawerContent, names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::given<names::DrawerContent, A...>(), "kompoz: PermanentNavigationDrawer takes a drawerContent");
        static_assert(P::blockGiven<A...>(), "kompoz: PermanentNavigationDrawer takes a block: its content");
        PermanentNavigationDrawer(P::get<names::DrawerContent>(Content{}, args...), P::get<names::Modifier>(Modifier{}, args...), P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::PermanentNavigationDrawerImpl> PermanentNavigationDrawer;
namespace detail {
void PermanentDrawerSheet(const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
struct PermanentDrawerSheetImpl {
    using P = Params<names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: PermanentDrawerSheet takes a block: its content");
        PermanentDrawerSheet(P::get<names::Modifier>(Modifier{}, args...), P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::PermanentDrawerSheetImpl> PermanentDrawerSheet;
namespace detail {
void NavigationDrawerItem(const Content& label, bool selected, std::function<void()> onClick, const Content& icon = {},
                          const Content& badge = {}, const Modifier& modifier = {}, CallSite site = CallSite::current());
struct NavigationDrawerItemImpl {
    using P = Params<names::Label, names::Selected, names::OnClick, names::Modifier, names::Icon, names::Badge>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::given<names::Label, A...>() && P::given<names::Selected, A...>() && P::given<names::OnClick, A...>(),
                      "kompoz: NavigationDrawerItem takes a label, selected and an onClick");
        NavigationDrawerItem(P::get<names::Label>(Content{}, args...), P::get<names::Selected>(false, args...),
                             P::get<names::OnClick>(std::function<void()>{}, args...), P::get<names::Icon>(Content{}, args...),
                             P::get<names::Badge>(Content{}, args...), P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::NavigationDrawerItemImpl> NavigationDrawerItem;

namespace detail {
// `from`'s interactions told again with their press positions moved back by *shift (Compose's
// MappedInteractionSource): for an indication on a part of the component that took the press.
MutableInteractionSource mappedInteractionSource(const MutableInteractionSource& from, std::shared_ptr<Offset> shift,
                                                 CallSite site = CallSite::current());
// NavigationBarItem's and NavigationRailItem's body (Compose 1.4's): an indicator
// `indicatorWidth` × 32 dp, `top` dp down, fading in (the default effects spring) and growing out
// of its middle (the fast spatial one); the icon 4 dp below its top, the label 4 dp under it,
// colored by the default effects spring; the ripple on the indicator alone.
void indicatorNavigationItem(bool selected, std::function<void()> onClick, const Content& icon, const Content& label, const Modifier& item,
                             float indicatorWidth, float top, bool enabled, std::optional<MutableInteractionSource> interactionSource);
} // namespace detail

// A vertical navigation bar for wide windows (instead of NavigationBar).
namespace detail {
void NavigationRail(const Modifier& modifier, const Content& header, const Content& content,
                    CallSite site = CallSite::current());
struct NavigationRailImpl {
    using P = Params<names::Modifier, names::Header, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: NavigationRail takes a block: its content");
        NavigationRail(P::get<names::Modifier>(Modifier{}, args...), P::get<names::Header>(Content{}, args...), P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::NavigationRailImpl> NavigationRail;
// With an icon and a label of its own composing (in the item's colors and labelMedium).
struct NavigationRailItemOptions {
    bool enabled = true;  // disabled: onSurface at 38%
    std::optional<MutableInteractionSource> interactionSource;
};
namespace detail {
void NavigationRailItem(bool selected, std::function<void()> onClick, const ImageVector& icon, std::string label,
                        CallSite site = CallSite::current());
void NavigationRailItem(bool selected, std::function<void()> onClick, const Content& icon, const Content& label, const Modifier& modifier = {},
                        const NavigationRailItemOptions& options = {}, CallSite site = CallSite::current());
struct NavigationRailItemImpl {
    using OfVector = Params<names::Selected, names::OnClick, names::Icon, names::Label>;
    using OfContent = Params<names::Selected, names::OnClick, names::Icon, names::Modifier, names::Enabled, names::Label, names::InteractionSource>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (std::is_convertible_v<decltype(OfContent::arg<names::Icon>(args...)), const ImageVector&>) {  // an ImageVector's
            using P = OfVector;
            static_assert(P::check<A...>());
            const ImageVector& icon = P::arg<names::Icon>(args...);
            NavigationRailItem(bool(P::arg<names::Selected>(args...)), std::function<void()>(P::arg<names::OnClick>(args...)), icon,
                               P::get<names::Label>(std::string(), args...), site);
        } else {
            using P = OfContent;
            static_assert(P::check<A...>());
            NavigationRailItemOptions options;
            P::set<names::Enabled>(options.enabled, args...);
            P::set<names::InteractionSource>(options.interactionSource, args...);
            NavigationRailItem(bool(P::arg<names::Selected>(args...)), std::function<void()>(P::arg<names::OnClick>(args...)),
                               Content(P::arg<names::Icon>(args...)), P::get<names::Label>(Content{}, args...), P::get<names::Modifier>(Modifier{}, args...),
                               options, site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::NavigationRailItemImpl> NavigationRailItem;

// ---- Swipe to dismiss --------------------------------------------------------------------

enum class SwipeToDismissBoxValue { Settled, StartToEnd, EndToStart };

class SwipeToDismissBoxState {
public:
    explicit SwipeToDismissBoxState(std::function<bool(SwipeToDismissBoxValue)> confirmValueChange = {});
    SwipeToDismissBoxValue currentValue() const;     // observable: Settled until swiped away
    SwipeToDismissBoxValue dismissDirection() const;  // observable: the way it is being dragged
    float progress() const;                           // 0..1 of the swipe; not observable
    Task reset() const;                               // slides back to Settled
    friend bool operator==(const SwipeToDismissBoxState& a, const SwipeToDismissBoxState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};
// confirmValueChange decides whether a swipe that went far enough dismisses (return false to
// let it spring back); remove the item there.
template <>
struct Saver<SwipeToDismissBoxState> {
    static SavedValue save(const SwipeToDismissBoxState& s);
    static std::optional<SwipeToDismissBoxState> restore(const SavedValue& v);
};
SwipeToDismissBoxState rememberSwipeToDismissBoxState(std::function<bool(SwipeToDismissBoxValue)> confirmValueChange = {},
                                                      CallSite site = CallSite::current());

struct SwipeToDismissOptions {
    bool enableDismissFromStartToEnd = true, enableDismissFromEndToStart = true;
};
// `content` follows the finger sideways over `backgroundContent` (sized like the content; read
// state.dismissDirection() there to pick a color or icon).
void SwipeToDismissBox(SwipeToDismissBoxState state, const Content& backgroundContent, const Content& content,
                       SwipeToDismissOptions options = {}, const Modifier& modifier = {},
                       CallSite site = CallSite::current());

} // namespace kompoz
