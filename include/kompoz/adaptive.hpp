#pragma once

// Layouts that adapt to the window, as Compose's material3-adaptive: the window's size class
// (compact phones, medium tablets and folded foldables, expanded tablets and desktop windows)
// and a scaffold that puts its navigation in a bar, a rail or a drawer to suit it.
//
//   NavigationSuiteScaffold([&](NavigationSuiteScope& nav) {
//       nav.item(tab == 0, [&] { tab = 0; }, Icons::Filled::Home, "Ana sayfa");
//       nav.item(tab == 1, [&] { tab = 1; }, Icons::Filled::Settings, "Ayarlar");
//   }, Modifier(), {}, [&] { Screen(tab); });

#include "animated.hpp"
#include "material.hpp"
#include "media_query.hpp"

#include <optional>
#include <string>
#include <vector>

namespace kompoz {

// Breakpoints of the width and the height (dp), as androidx.window's.
inline constexpr float WIDTH_DP_MEDIUM_LOWER_BOUND = 600, WIDTH_DP_EXPANDED_LOWER_BOUND = 840;
inline constexpr float HEIGHT_DP_MEDIUM_LOWER_BOUND = 480, HEIGHT_DP_EXPANDED_LOWER_BOUND = 900;

enum class WindowWidthSizeClass { Compact, Medium, Expanded };   // < 600, < 840, wider
enum class WindowHeightSizeClass { Compact, Medium, Expanded };  // < 480, < 900, taller

// The window's size in classes: lay out by these, not by exact sizes.
struct WindowSizeClass {
    float minWidthDp = 0, minHeightDp = 0;  // the breakpoints it is at least
    WindowWidthSizeClass windowWidthSizeClass = WindowWidthSizeClass::Compact;
    WindowHeightSizeClass windowHeightSizeClass = WindowHeightSizeClass::Compact;

    static WindowSizeClass compute(float widthDp, float heightDp);
    bool isWidthAtLeastBreakpoint(float widthDp) const { return minWidthDp >= widthDp; }
    bool isHeightAtLeastBreakpoint(float heightDp) const { return minHeightDp >= heightDp; }
    friend bool operator==(const WindowSizeClass&, const WindowSizeClass&) = default;
};

struct WindowAdaptiveInfo {
    WindowSizeClass windowSizeClass;
    Posture windowPosture = Posture::Flat;  // a foldable's (Tabletop: a horizontal fold)
    friend bool operator==(const WindowAdaptiveInfo&, const WindowAdaptiveInfo&) = default;
};
// The window's, inside a composition (it recomposes when the window's size changes).
WindowAdaptiveInfo currentWindowAdaptiveInfo();

// ---- NavigationSuiteScaffold --------------------------------------------------------------

// Where the navigation goes. M3 Expressive's (Compose 1.4): a short bar, its items' icons above
// their labels (Compact) or before them (Medium), or a wide rail, collapsed or expanded; and the
// older bar, rail and permanent drawer.
enum class NavigationSuiteType {
    ShortNavigationBarCompact, ShortNavigationBarMedium, WideNavigationRailCollapsed, WideNavigationRailExpanded,
    NavigationBar, NavigationRail, NavigationDrawer, None
};

namespace NavigationSuiteScaffoldDefaults {
// M3 Expressive's choice (Compose's navigationSuiteType): a compact window gets a short bar, a
// short one (a phone on its side) or a tabletop foldable a short bar with its labels beside the
// icons, the others a collapsed wide rail.
NavigationSuiteType navigationSuiteType(const WindowAdaptiveInfo& adaptiveInfo);
// The older choice: a bar on compact windows, short ones and tabletop foldables, a rail on the others.
NavigationSuiteType calculateFromAdaptiveInfo(const WindowAdaptiveInfo& adaptiveInfo);
inline constexpr float PrimaryActionContentPadding = 16;  // dp between a bar's primary action (a FAB) and the edges
} // namespace NavigationSuiteScaffoldDefaults

// Whether the scaffold shows its navigation (Compose's NavigationSuiteScaffoldState): hiding
// slides the bar down (the rail out to the start) and the content takes its room. Copies share it.
enum class NavigationSuiteScaffoldValue { Visible, Hidden };
class NavigationSuiteScaffoldState {
public:
    explicit NavigationSuiteScaffoldState(NavigationSuiteScaffoldValue initialValue = NavigationSuiteScaffoldValue::Visible);
    bool isAnimating() const;
    NavigationSuiteScaffoldValue targetValue() const;
    NavigationSuiteScaffoldValue currentValue() const;  // observable: Visible only once all the way in
    float visibleFraction() const;                      // observable: 0 hidden .. 1 shown
    // In a coroutine: animated there (a spatial spring).
    Task hide() const;
    Task show() const;
    Task toggle() const;
    void snapTo(NavigationSuiteScaffoldValue value) const;
    friend bool operator==(const NavigationSuiteScaffoldState& a, const NavigationSuiteScaffoldState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};
template <>
struct Saver<NavigationSuiteScaffoldState> {
    static SavedValue save(const NavigationSuiteScaffoldState& s);
    static std::optional<NavigationSuiteScaffoldState> restore(const SavedValue& v);
};
NavigationSuiteScaffoldState rememberNavigationSuiteScaffoldState(
    NavigationSuiteScaffoldValue initialValue = NavigationSuiteScaffoldValue::Visible, CallSite site = CallSite::current());

class NavigationSuiteScope {
public:
    struct Item {
        bool selected;
        std::function<void()> onClick;
        ImageVector icon;
        std::string label;
        bool enabled = true;
        Content badge;  // over the icon (a Badge)
    };
    void item(bool selected, std::function<void()> onClick, const ImageVector& icon, std::string label, bool enabled = true,
              Content badge = {}) {
        items_.push_back({selected, std::move(onClick), icon, std::move(label), enabled, std::move(badge)});
    }
    const std::vector<Item>& items() const { return items_; }

private:
    std::vector<Item> items_;
};

struct NavigationSuiteScaffoldOptions {
    // By default from currentWindowAdaptiveInfo(): navigationSuiteType() with navigation items,
    // calculateFromAdaptiveInfo() with a NavigationSuiteScope.
    std::optional<NavigationSuiteType> layoutType;
    std::optional<NavigationSuiteScaffoldState> state;  // shown or hidden; by default its own, shown
    Content railHeader;    // above a rail's items (a FAB, say)
    Content drawerHeader;  // above a drawer's items (the app's name, say)
    std::optional<Color> containerColor;  // behind the content (background)
    std::optional<Color> contentColor;    // onBackground
    // With navigation items: a primary action (a FAB) at the top of a rail or drawer, above the end
    // (Start, Center: aligned so) of a bar; and where in a rail or drawer the items go.
    Content primaryActionContent;
    HorizontalAlignment primaryActionContentHorizontalAlignment = Alignment::End;
    Arrangement::Kind navigationItemVerticalArrangement = Arrangement::Kind::Start;  // Start (top), Center, End (bottom)
};

// The app's navigation items in the bar, rail or drawer that suits the window, `content`
// beside it — and kept clear of what the bar or rail already keeps clear of (the navigation
// bar's inset below a bar): an inner Scaffold pads only by the rest.
void NavigationSuiteScaffold(const std::function<void(NavigationSuiteScope&)>& navigationSuiteItems, const Modifier& modifier,
                             const NavigationSuiteScaffoldOptions& options, const Content& content, CallSite site = CallSite::current());
// The same with items of the app's own composing (Compose 1.4's): NavigationSuiteItems for the
// type the scaffold uses, M3 Expressive's by default.
//   NavigationSuiteScaffold([&] {
//       NavigationSuiteItem(tab == 0, [&] { tab = 0; }, [] { Icon(Icons::Filled::Home, ""); }, [] { Text("Ana sayfa"); });
//   }, Modifier(), {.primaryActionContent = [] { FloatingActionButton(...); }}, [&] { Screen(tab); });
void NavigationSuiteScaffold(const Content& navigationItems, const Modifier& modifier, const NavigationSuiteScaffoldOptions& options,
                             const Content& content, CallSite site = CallSite::current());

// Its parts, to put together another way: the navigation component of a type around `content`
// (its items), with the primary action (rails and drawers: at their top) and where the items go...
void NavigationSuite(NavigationSuiteType navigationSuiteType, const Modifier& modifier, const Content& content,
                     const Content& primaryActionContent = {}, Arrangement::Kind verticalArrangement = Arrangement::Kind::Start,
                     CallSite site = CallSite::current());
// ...an item made for it (the bar's, the rail's, the drawer's)...
struct NavigationSuiteItemOptions {
    std::optional<NavigationSuiteType> navigationSuiteType;  // by default navigationSuiteType(currentWindowAdaptiveInfo())
    bool enabled = true;
    Content badge;  // over the icon
    std::optional<MutableInteractionSource> interactionSource;
};
void NavigationSuiteItem(bool selected, std::function<void()> onClick, const Content& icon, const Content& label,
                         const Modifier& modifier = {}, const NavigationSuiteItemOptions& options = {}, CallSite site = CallSite::current());
// ...and the layout: the navigation at the bottom (bars) or the start (the others), sliding away
// as `state` hides it; a bar's primary action above it, 16 dp from the edge it is aligned to.
void NavigationSuiteScaffoldLayout(const Content& navigationSuite, NavigationSuiteType navigationSuiteType, const Content& content,
                                   std::optional<NavigationSuiteScaffoldState> state = {}, const Content& primaryActionContent = {},
                                   HorizontalAlignment primaryActionContentHorizontalAlignment = Alignment::End,
                                   CallSite site = CallSite::current());

// ---- Pane expansion ------------------------------------------------------------------------
//
// Two panes side by side resized by dragging the space between them (Compose's
// PaneExpansionState and paneExpansionDragHandle):
//   auto expansion = rememberPaneExpansionState({PaneExpansionAnchor::Proportion(0), PaneExpansionAnchor::Offset(360),
//                                                PaneExpansionAnchor::Proportion(0.5f), PaneExpansionAnchor::Proportion(1)});
//   ListDetailPaneScaffold<int>(navigator, list, details, Modifier(),
//       {.paneExpansionState = expansion, .paneExpansionDragHandle = [](PaneExpansionState state) {
//           auto source = remember([] { return MutableInteractionSource(); });
//           VerticalDragHandle(paneExpansionDraggable(state, source), source);
//       }});

// Where a released handle may leave the first pane's end: a share of the room the two panes
// have, or dp from its start (from its end when negative).
struct PaneExpansionAnchor {
    float proportion = -1;  // 0..1; below 0: an offset
    float offset = 0;
    static constexpr PaneExpansionAnchor Proportion(float proportion) { return {proportion, 0}; }
    static constexpr PaneExpansionAnchor Offset(float dp) { return {-1, dp}; }
    float widthIn(float room) const;  // the first pane's width it stands for
    friend constexpr bool operator==(const PaneExpansionAnchor&, const PaneExpansionAnchor&) = default;
};

// How wide the first of two side-by-side panes is: unspecified (the scaffold's own widths)
// until a handle drags it or code sets it. Copies share it; remember one.
class PaneExpansionState {
public:
    explicit PaneExpansionState(std::vector<PaneExpansionAnchor> anchors = {});
    bool isUnspecified() const;    // observable
    float firstPaneWidth() const;  // dp, once specified (observable)
    void setFirstPaneWidth(float dp) const;               // up to the room the panes have
    void setFirstPaneProportion(float proportion) const;  // of that room, once laid out
    void clear() const;                                   // back to the scaffold's widths
    const std::vector<PaneExpansionAnchor>& anchors() const;

    friend bool operator==(const PaneExpansionState& a, const PaneExpansionState& b) { return a.data_ == b.data_; }
    struct Data;
    std::shared_ptr<Data> data_;
};
template <>
struct Saver<PaneExpansionState> {
    static SavedValue save(const PaneExpansionState& s);
    static std::optional<PaneExpansionState> restore(const SavedValue& v);
};
PaneExpansionState rememberPaneExpansionState(std::vector<PaneExpansionAnchor> anchors = {}, CallSite site = CallSite::current());

// For the scaffold's drag handle: dragging it moves the split, and a release settles on the
// nearest anchor (the next one the way it was flung; without anchors, where it was let go). At
// least 48 dp tall and as wide as the space between the panes, to be easy to grab.
Modifier paneExpansionDraggable(const PaneExpansionState& state, std::optional<MutableInteractionSource> interactionSource = {});

// ---- ListDetailPaneScaffold ---------------------------------------------------------------
//
// A list and the details of what is picked in it (Compose's ListDetailPaneScaffold): side by
// side on an expanded window, one at a time on narrower ones — picking an item slides its
// details in, Back slides them out.
//   auto navigator = rememberListDetailPaneScaffoldNavigator<int>();
//   ListDetailPaneScaffold<int>(navigator, [&] {
//       LazyColumn(..., [&](LazyListScope& l) { l.items(n, [&](int i) {
//           ListItem({.headline = ...}, Modifier().clickable([navigator, i] { navigator.navigateTo(ListDetailPaneScaffoldRole::Detail, i); }));
//       }); });
//   }, [&](const std::optional<int>& item) { if (item) Details(*item); else Text("Bir öğe seçin"); });

enum class ListDetailPaneScaffoldRole { List, Detail };
enum class SupportingPaneScaffoldRole { Main, Supporting };

template <class T, class Role = ListDetailPaneScaffoldRole>
struct ThreePaneScaffoldDestinationItem {
    Role pane = Role{};  // the first one: List, Main
    std::optional<T> contentKey;
    friend bool operator==(const ThreePaneScaffoldDestinationItem&, const ThreePaneScaffoldDestinationItem&) = default;
};

namespace detail {

// Where a pane scaffold is and where it has been (T: what its second pane shows; copyable and
// ==). Copies share it.
template <class Role, class T>
class PaneScaffoldNavigator {
public:
    using Destination = ThreePaneScaffoldDestinationItem<T, Role>;
    PaneScaffoldNavigator() : history_(mutableStateOf(std::vector<Destination>{Destination{}})) {}
    Destination currentDestination() const { return history_.get().back(); }  // observable; the first pane at first
    void navigateTo(Role pane, std::optional<T> contentKey = std::nullopt) const {
        auto h = history_.peek();
        h.push_back({pane, std::move(contentKey)});
        history_.set(std::move(h));
    }
    bool canNavigateBack() const { return history_.get().size() > 1; }  // observable
    bool navigateBack() const {
        auto h = history_.peek();
        if (h.size() <= 1) return false;
        h.pop_back();
        history_.set(std::move(h));
        return true;
    }
    friend bool operator==(const PaneScaffoldNavigator& a, const PaneScaffoldNavigator& b) { return a.history_ == b.history_; }

protected:
    // The latest destination of `pane`'s key (none before any).
    std::optional<T> keyOf(Role pane) const {
        const auto& h = history_.get();
        for (auto it = h.rbegin(); it != h.rend(); ++it)
            if (it->pane == pane) return it->contentKey;
        return std::nullopt;
    }

private:
    MutableState<std::vector<Destination>> history_;
};

// Two panes side by side, one of them `fixedWidth` wide (the first or the second) unless the
// expansion state says how wide the first is; the drag handle in the space between them.
void twoPanes(const Content& first, const Content& second, bool firstFixed, float fixedWidth, float spacerSize,
              const std::optional<PaneExpansionState>& expansion, const std::function<void(PaneExpansionState)>& handle,
              const Modifier& modifier);

// Two panes side by side (twoPanes), or one at a time: the second slides in from the end over
// the first and back out, a leaving second pane keeping what it showed.
template <class T>
void paneScaffold(int panes, bool showingSecond, const std::optional<T>& key, const Content& first,
                  const std::function<void(const std::optional<T>&)>& second, bool firstFixed, float fixedWidth, float spacerSize,
                  const std::optional<PaneExpansionState>& expansion, const std::function<void(PaneExpansionState)>& handle,
                  const Modifier& modifier) {
    if (panes >= 2) return twoPanes(first, [&] { second(key); }, firstFixed, fixedWidth, spacerSize, expansion, handle, modifier);
    struct Shown {
        bool second;
        std::optional<T> key;
        bool operator==(const Shown&) const = default;
    };
    AnimatedContent<Shown>(
        Shown{showingSecond, key},
        [&](const Shown& s) {
            Box(Modifier().fillMaxSize(), [&] {
                if (s.second)
                    second(s.key);
                else
                    first();
            });
        },
        modifier.fillMaxSize(),
        [](const Shown&, const Shown& to) {
            float way = to.second ? 1.f : -1.f;
            return togetherWith(slideInHorizontally(way) + fadeIn(), slideOutHorizontally(-way) + fadeOut()).withSizeTransform(std::nullopt);
        },
        {.contentKey = [](const Shown& s) { return uint64_t(s.second); }});
}

inline int defaultPanes(std::optional<int> maxHorizontalPartitions) {
    bool expanded = currentWindowAdaptiveInfo().windowSizeClass.windowWidthSizeClass == WindowWidthSizeClass::Expanded;
    return maxHorizontalPartitions.value_or(expanded ? 2 : 1);
}

// The app's expansion state; one of the scaffold's own when only a handle is given.
inline std::optional<PaneExpansionState> expansionOf(const std::optional<PaneExpansionState>& given,
                                                     const std::function<void(PaneExpansionState)>& handle) {
    auto own = rememberPaneExpansionState();
    return given ? given : handle ? std::optional(own) : std::nullopt;
}

} // namespace detail

template <class T>
class ListDetailPaneScaffoldNavigator : public detail::PaneScaffoldNavigator<ListDetailPaneScaffoldRole, T> {
public:
    // What the detail pane shows: the latest detail destination's key (none before any).
    std::optional<T> detailKey() const { return this->keyOf(ListDetailPaneScaffoldRole::Detail); }
};

template <class T>
ListDetailPaneScaffoldNavigator<T> rememberListDetailPaneScaffoldNavigator(CallSite site = CallSite::current()) {
    return remember([] { return ListDetailPaneScaffoldNavigator<T>(); }, site);
}

struct ListDetailPaneScaffoldOptions {
    std::optional<int> maxHorizontalPartitions;  // panes side by side: by default 2 on an expanded window, else 1
    float listPaneWidth = 360;                   // beside the details
    float spacerSize = 24;                       // between them
    bool handleBack = true;                      // Back navigates back while it can
    std::optional<PaneExpansionState> paneExpansionState;             // how wide the list pane is
    std::function<void(PaneExpansionState)> paneExpansionDragHandle;  // between the panes (a VerticalDragHandle)
};

template <class T>
void ListDetailPaneScaffold(const ListDetailPaneScaffoldNavigator<T>& navigator, const Content& listPane,
                            const std::function<void(const std::optional<T>&)>& detailPane, const Modifier& modifier = {},
                            const ListDetailPaneScaffoldOptions& options = {}, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    if (options.handleBack) BackHandler(navigator.canNavigateBack(), [navigator] { navigator.navigateBack(); });
    detail::paneScaffold<T>(detail::defaultPanes(options.maxHorizontalPartitions),
                            navigator.currentDestination().pane == ListDetailPaneScaffoldRole::Detail, navigator.detailKey(), listPane,
                            detailPane, true, options.listPaneWidth, options.spacerSize,
                            detail::expansionOf(options.paneExpansionState, options.paneExpansionDragHandle),
                            options.paneExpansionDragHandle, modifier);
}

// ---- SupportingPaneScaffold ----------------------------------------------------------------
//
// A main pane and one that supports it (Compose's SupportingPaneScaffold): side by side on an
// expanded window, the supporting one at the end; on narrower ones the main pane, and the
// supporting one sliding in when navigated to (Back slides it out).
//   auto navigator = rememberSupportingPaneScaffoldNavigator<int>();
//   SupportingPaneScaffold<int>(navigator, [&] {
//       Article(); Button([navigator] { navigator.navigateTo(SupportingPaneScaffoldRole::Supporting); }, ...);
//   }, [&](const std::optional<int>&) { Related(); });

template <class T>
class SupportingPaneScaffoldNavigator : public detail::PaneScaffoldNavigator<SupportingPaneScaffoldRole, T> {
public:
    // What the supporting pane shows: the latest supporting destination's key (none before any).
    std::optional<T> supportingKey() const { return this->keyOf(SupportingPaneScaffoldRole::Supporting); }
};

template <class T>
SupportingPaneScaffoldNavigator<T> rememberSupportingPaneScaffoldNavigator(CallSite site = CallSite::current()) {
    return remember([] { return SupportingPaneScaffoldNavigator<T>(); }, site);
}

struct SupportingPaneScaffoldOptions {
    std::optional<int> maxHorizontalPartitions;  // panes side by side: by default 2 on an expanded window, else 1
    float supportingPaneWidth = 360;             // beside the main pane
    float spacerSize = 24;
    bool handleBack = true;  // Back navigates back while it can
    std::optional<PaneExpansionState> paneExpansionState;             // how wide the main pane is
    std::function<void(PaneExpansionState)> paneExpansionDragHandle;  // between the panes (a VerticalDragHandle)
};

template <class T>
void SupportingPaneScaffold(const SupportingPaneScaffoldNavigator<T>& navigator, const Content& mainPane,
                            const std::function<void(const std::optional<T>&)>& supportingPane, const Modifier& modifier = {},
                            const SupportingPaneScaffoldOptions& options = {}, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    if (options.handleBack) BackHandler(navigator.canNavigateBack(), [navigator] { navigator.navigateBack(); });
    detail::paneScaffold<T>(detail::defaultPanes(options.maxHorizontalPartitions),
                            navigator.currentDestination().pane == SupportingPaneScaffoldRole::Supporting, navigator.supportingKey(),
                            mainPane, supportingPane, false, options.supportingPaneWidth, options.spacerSize,
                            detail::expansionOf(options.paneExpansionState, options.paneExpansionDragHandle),
                            options.paneExpansionDragHandle, modifier);
}

} // namespace kompoz
