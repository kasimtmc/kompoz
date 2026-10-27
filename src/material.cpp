#include <kompoz/animated.hpp>
#include <kompoz/animation.hpp>
#include <kompoz/coroutines.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/host.hpp>
#include <kompoz/icons.hpp>
#include <kompoz/material.hpp>
#include <kompoz/state.hpp>
#include <kompoz/widgets.hpp>

#include <chrono>
#include <cmath>

namespace kompoz {

void Scaffold(const Modifier& modifier, const ScaffoldSlots& slots, const std::function<void(PaddingValues)>& content,
              CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    WindowInsets insets = slots.contentWindowInsets.value_or(detail::systemBarsForVisualComponents());
    // The content's padding comes from the measured bars: layout writes it before measuring the
    // content, and what reads it (calculate*Padding: a padding, a list's content padding) follows,
    // without the content recomposing (as Compose's: a top bar collapsing as a list scrolls).
    auto padding = remember([] { return std::make_shared<PaddingValues>(); });
    Color container = slots.containerColor.value_or(cs.background);

    auto policy = [padding, all = insets, position = slots.floatingActionButtonPosition](LayoutNode& node, const Constraints& c) {
        // Only what the layouts around it haven't kept clear of yet (a NavigationSuiteScaffold's bar has).
        WindowInsets insets = detail::unconsumedInsets(node, all);
        float w = c.hasBoundedWidth() ? c.maxWidth : c.minWidth, h = c.hasBoundedHeight() ? c.maxHeight : c.minHeight;
        auto& ch = node.children;  // content, top bar, bottom bar, snackbar host, FAB
        Constraints loose{0, w, 0, h};
        Size top = ch[1]->measure(loose);
        ch[1]->place(0, 0);
        Size bottom = ch[2]->measure(loose);
        ch[2]->place(0, h - bottom.height);
        float bottomEdge = h - (bottom.height > 0 ? bottom.height : insets.bottom);
        Size fab = ch[4]->measure(loose);
        float fabY = bottomEdge - fab.height - (fab.height > 0 ? 16.f : 0.f);
        float fabX = w - fab.width - 16 - insets.right;
        if (position == FabPosition::Start) fabX = 16 + insets.left;
        if (position == FabPosition::Center) fabX = (w - fab.width) / 2;
        if (position == FabPosition::EndOverlay) fabY = h - insets.bottom - fab.height - (fab.height > 0 ? 16.f : 0.f);  // over the bottom bar
        ch[4]->place(fabX, fabY);
        Size snack = ch[3]->measure({0, std::min(w, 640.f), 0, h});
        ch[3]->place((w - snack.width) / 2, (fab.height > 0 ? fabY - 8 : bottomEdge) - snack.height);
        PaddingValues inner{insets.left, top.height > 0 ? top.height : insets.top, insets.right,
                            bottom.height > 0 ? bottom.height : insets.bottom};
        if (!detail::isProbeMeasure() && !(inner == *padding)) *padding = inner, detail::notifyStateChanged(padding.get());
        ch[0]->measure(loose);  // (as Compose's: content that doesn't fill keeps its size)
        ch[0]->place(0, 0);
        return Size{w, h};
    };

    CompositionLocalProvider({LocalContentColor.provides(cs.onBackground)}, [&] {
        detail::Composer::require().emit(
            detail::keyOf(CallSite::current()),
            [&](LayoutNode& node) {
                node.modifier = modifier.background(container);
                node.measurePolicy = policy;
                node.drawContent = {};
            },
            [&] {
                detail::stackSlot([&] { content({padding->start, padding->top, padding->end, padding->bottom, padding}); });
                detail::stackSlot(slots.topBar);
                detail::stackSlot(slots.bottomBar);
                detail::stackSlot(slots.snackbarHost);
                detail::stackSlot(slots.floatingActionButton);
            });
    });
}

struct TopAppBarState::Data {
    MutableState<float> limit{-kInfinity};  // (a state, as Compose's: what reads it follows it once a bar sets it)
    Animatable<float> heightOffset{0.f};  // animated when a half collapsed bar settles
    MutableState<float> contentOffset{0.f};
};

TopAppBarState::TopAppBarState(float initialHeightOffsetLimit, float initialHeightOffset, float initialContentOffset)
    : data_(std::make_shared<Data>()) {
    data_->limit.set(initialHeightOffsetLimit);
    data_->heightOffset.snapTo(std::clamp(initialHeightOffset, initialHeightOffsetLimit, 0.f));
    data_->contentOffset.set(initialContentOffset);
}

float TopAppBarState::heightOffsetLimit() const { return data_->limit.get(); }

void TopAppBarState::setHeightOffsetLimit(float limit) const {
    if (limit == data_->limit.peek()) return;
    data_->limit.set(limit);
    setHeightOffset(data_->heightOffset.peek());
}

float TopAppBarState::heightOffset() const { return data_->heightOffset.value(); }

void TopAppBarState::setHeightOffset(float offset) const {
    float v = std::clamp(offset, std::min(data_->limit.peek(), 0.f), 0.f);
    if (v != data_->heightOffset.peek() || data_->heightOffset.isRunning()) data_->heightOffset.snapTo(v);
}

float TopAppBarState::contentOffset() const { return data_->contentOffset.get(); }

void TopAppBarState::setContentOffset(float offset) const {
    if (offset != data_->contentOffset.peek()) data_->contentOffset.set(offset);
}

float TopAppBarState::collapsedFraction() const {
    float limit = data_->limit.get();
    return limit < 0 && limit != -kInfinity ? heightOffset() / limit : 0.f;
}

float TopAppBarState::overlappedFraction() const {
    float limit = data_->limit.get();
    if (!(limit < 0) || limit == -kInfinity) return 0;
    return 1 - std::clamp(limit - contentOffset(), limit, 0.f) / limit;
}

SavedValue Saver<TopAppBarState>::save(const TopAppBarState& s) {
    return SavedValue::List{s.data_->limit.peek(), s.data_->heightOffset.peek(), s.data_->contentOffset.peek()};
}
std::optional<TopAppBarState> Saver<TopAppBarState>::restore(const SavedValue& v) {
    const SavedValue::List* l = v.list();
    if (!l || l->size() != 3) return std::nullopt;
    auto limit = (*l)[0].number(), offset = (*l)[1].number(), content = (*l)[2].number();
    if (!limit || !offset || !content) return std::nullopt;
    return TopAppBarState(float(*limit), float(*offset), float(*content));
}

TopAppBarState rememberTopAppBarState(float initialHeightOffsetLimit, float initialHeightOffset, float initialContentOffset, CallSite site) {
    return rememberSaveable([&] { return TopAppBarState(initialHeightOffsetLimit, initialHeightOffset, initialContentOffset); }, site);
}

namespace {

// A half collapsed bar finishes the move after a fling: the way it was flung, or to the nearer end.
void settleAppBar(const TopAppBarState& state, const CoroutineScope& scope, float velocity) {
    float f = state.collapsedFraction();
    if (f < 0.01f || f >= 1) return;
    float limit = state.heightOffsetLimit();
    float target = std::abs(velocity) > 300 ? (velocity < 0 ? limit : 0.f) : (f < 0.5f ? 0.f : limit);
    auto d = state.data_;
    scope.launch([d, target]() -> Task {
        co_await d->heightOffset.animateTo(target, spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow));
    });
}

struct AppBarConnection : NestedScrollConnection {
    AppBarConnection(TopAppBarState s, CoroutineScope c) : state(std::move(s)), scope(std::move(c)) {}
    TopAppBarState state;
    CoroutineScope scope;
};

struct PinnedConnection final : AppBarConnection {
    using AppBarConnection::AppBarConnection;
    Offset onPostScroll(Offset consumed, Offset available, NestedScrollSource) override {
        // Back at the top (nothing left to scroll down to): exactly zero, whatever was summed up.
        state.setContentOffset(consumed.y == 0 && available.y > 0 ? 0.f : state.contentOffset() + consumed.y);
        return {};
    }
};

struct EnterAlwaysConnection final : AppBarConnection {
    using AppBarConnection::AppBarConnection;
    // The bar moves first, either way; while it moves the content stays.
    Offset onPreScroll(Offset available, NestedScrollSource) override {
        float before = state.heightOffset();
        state.setHeightOffset(before + available.y);
        return state.heightOffset() != before ? Offset{0, available.y} : Offset{};
    }
    Offset onPostScroll(Offset consumed, Offset available, NestedScrollSource) override {
        state.setContentOffset(state.contentOffset() + consumed.y);
        float h = state.heightOffset();
        if ((h == 0 || h == state.heightOffsetLimit()) && consumed.y == 0 && available.y > 0) state.setContentOffset(0);
        state.setHeightOffset(h + consumed.y);
        return {};
    }
    Velocity onPostFling(Velocity, Velocity available) override {
        settleAppBar(state, scope, available.y);
        return {};
    }
};

struct ExitUntilCollapsedConnection final : AppBarConnection {
    using AppBarConnection::AppBarConnection;
    // Collapses before the content scrolls up; expands only with what the content leaves.
    Offset onPreScroll(Offset available, NestedScrollSource) override {
        if (available.y > 0) return {};
        float before = state.heightOffset();
        state.setHeightOffset(before + available.y);
        return state.heightOffset() != before ? Offset{0, available.y} : Offset{};
    }
    Offset onPostScroll(Offset consumed, Offset available, NestedScrollSource) override {
        state.setContentOffset(state.contentOffset() + consumed.y);
        float before = state.heightOffset();
        if (available.y < 0 || consumed.y < 0) {
            state.setHeightOffset(before + consumed.y);
            return {0, state.heightOffset() - before};
        }
        if (consumed.y == 0 && available.y > 0) state.setContentOffset(0);
        if (available.y > 0) {
            state.setHeightOffset(before + available.y);
            return {0, state.heightOffset() - before};
        }
        return {};
    }
    Velocity onPostFling(Velocity, Velocity available) override {
        settleAppBar(state, scope, available.y);
        return {};
    }
};

template <class C>
TopAppBarScrollBehavior scrollBehavior(const TopAppBarState* given, bool pinned, CallSite site) {
    ComposableScope group(site);
    TopAppBarState state = given ? *given : rememberTopAppBarState();
    auto scope = rememberCoroutineScope();
    auto& connection = remember([&] { return std::make_shared<C>(state, scope); });
    connection->state = state;
    return {state, connection, pinned};
}

} // namespace

namespace TopAppBarDefaults {
TopAppBarScrollBehavior pinnedScrollBehavior(CallSite site) {
    return scrollBehavior<PinnedConnection>(nullptr, true, site);
}
TopAppBarScrollBehavior pinnedScrollBehavior(const TopAppBarState& state, CallSite site) {
    return scrollBehavior<PinnedConnection>(&state, true, site);
}
TopAppBarScrollBehavior enterAlwaysScrollBehavior(CallSite site) {
    return scrollBehavior<EnterAlwaysConnection>(nullptr, false, site);
}
TopAppBarScrollBehavior enterAlwaysScrollBehavior(const TopAppBarState& state, CallSite site) {
    return scrollBehavior<EnterAlwaysConnection>(&state, false, site);
}
TopAppBarScrollBehavior exitUntilCollapsedScrollBehavior(CallSite site) {
    return scrollBehavior<ExitUntilCollapsedConnection>(nullptr, false, site);
}
TopAppBarScrollBehavior exitUntilCollapsedScrollBehavior(const TopAppBarState& state, CallSite site) {
    return scrollBehavior<ExitUntilCollapsedConnection>(&state, false, site);
}
WindowInsets windowInsets() { return WindowInsets::safeDrawing().only(WindowInsetsSides::Horizontal | WindowInsetsSides::Top); }
} // namespace TopAppBarDefaults

namespace {

constexpr float kAppBarHeight = 64;

PaddingValues appBarInsets(const TopAppBarSlots& slots) {
    return slots.windowInsets.value_or(TopAppBarDefaults::windowInsets()).asPaddingValues();
}

// The content colors (TopAppBarColors') of a bar's parts.
struct AppBarColors {
    Color navigationIcon, title, actions, subtitle;
};
AppBarColors appBarColors(const TopAppBarSlots& slots) {
    const ColorScheme& cs = colorScheme();
    return {slots.navigationIconContentColor.value_or(cs.onSurface), slots.titleContentColor.value_or(cs.onSurface),
            slots.actionIconContentColor.value_or(cs.onSurfaceVariant), slots.subtitleContentColor.value_or(cs.onSurfaceVariant)};
}
void tinted(Color color, const Content& content) {
    if (content) CompositionLocalProvider({LocalContentColor.provides(color)}, content);
}

// The container color: the scrolled one once content is under the bar (recomposes only when
// that flips, not on every scroll step; the color on its way is read as it is drawn).
AnimatedState<Color> appBarContainer(const TopAppBarSlots& slots) {
    const ColorScheme& cs = colorScheme();
    Color normal = slots.containerColor.value_or(cs.surface);
    Color scrolled = slots.scrolledContainerColor.value_or(cs.surfaceContainer);
    bool overlapped = false;
    if (slots.scrollBehavior) {
        TopAppBarState state = slots.scrollBehavior->state;
        auto& derived = remember([&] { return derivedStateOf([state] { return state.overlappedFraction() > 0.01f; }); });
        overlapped = derived.get();
    }
    return animateColorAsState(overlapped ? scrolled : normal, 300);
}

// The navigation icon, `title` and the actions on one 64 dp row.
// The title in the middle of the whole bar, kept between the icons (as wide as the space the
// wider side leaves on both).
// The title, and under it the subtitle if there is one (smaller), in their colors.
void titles(const TopAppBarSlots& slots, const Content& title, const Content& subtitle, HorizontalAlignment alignment,
            const Font& titleFont, const Font& subtitleFont) {
    AppBarColors colors = appBarColors(slots);
    Column(Modifier(), {.horizontalAlignment = alignment}, [&] {
        CompositionLocalProvider({LocalTextStyle.provides(titleFont)}, [&] { tinted(colors.title, title); });
        if (subtitle)
            CompositionLocalProvider({LocalTextStyle.provides(subtitleFont), LocalContentColor.provides(colors.subtitle)}, subtitle);
    });
}

// `fonts`: the title's and subtitle's (unset: titleLarge and labelMedium).
void centeredAppBarRow(const TopAppBarSlots& slots, const Modifier& modifier, const Content& title, const Content& subtitle, float height,
                       std::optional<std::pair<Font, Font>> fonts = {}) {
    AppBarColors colors = appBarColors(slots);
    auto [titleFont, subtitleFont] = fonts.value_or(std::pair{typography().titleLarge, typography().labelMedium});
    Layout(modifier.fillMaxWidth().height(height).padding(4, 0),
           [&] {
               Box([&] { tinted(colors.navigationIcon, slots.navigationIcon); });
               Box(Modifier().padding(12, 0),
                   [&] { titles(slots, title, subtitle, Alignment::CenterHorizontally, titleFont, subtitleFont); });
               Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, [&] { tinted(colors.actions, slots.actions); });
           },
           [height](const std::vector<Measurable>& m, const Constraints& c) {
               float w = c.hasBoundedWidth() ? c.maxWidth : c.minWidth, h = c.hasBoundedHeight() ? c.maxHeight : height;
               Constraints loose{0, w, 0, h};
               Placeable nav = m[0].measure(loose), actions = m[2].measure(loose);
               float side = std::max(nav.width, actions.width);
               Placeable title = m[1].measure({0, std::max(0.f, w - 2 * side), 0, h});
               nav.place(0, (h - nav.height) / 2);
               actions.place(w - actions.width, (h - actions.height) / 2);
               title.place((w - title.width) / 2, (h - title.height) / 2);
               return Size{w, h};
           });
}

void appBarRow(const TopAppBarSlots& slots, const Modifier& modifier, const Content& title, const Content& subtitle, float height,
               std::optional<std::pair<Font, Font>> fonts = {}) {
    AppBarColors colors = appBarColors(slots);
    auto [titleFont, subtitleFont] = fonts.value_or(std::pair{typography().titleLarge, typography().labelMedium});
    Row(modifier.fillMaxWidth().height(height).padding(4, 0), {.verticalAlignment = Alignment::CenterVertically},
        [&] {
            tinted(colors.navigationIcon, slots.navigationIcon);
            Box(Modifier().weight(1).padding(slots.navigationIcon ? 4.f : 12.f, 0), Alignment::CenterStart,
                [&] { titles(slots, title, subtitle, Alignment::Start, titleFont, subtitleFont); });
            if (slots.actions)
                Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, [&] { tinted(colors.actions, slots.actions); });
        });
}

// Shows `height` + heightOffset of what follows (read during layout: collapsing never recomposes),
// keeping it centered.
Modifier collapsing(const Modifier& m, const TopAppBarState& state, float height, bool center) {
    return m.clipToBounds().layout([state, height, center](const MeasureNext& measure, const Constraints& c, Offset& at) {
        Size s = measure({c.minWidth, c.maxWidth, height, height});
        float h = std::max(0.f, height + state.heightOffset());
        at = {0, center ? (h - height) / 2 : h - height};
        return Size{s.width, h};
    });
}

// `subtitleRoom`: what a subtitle adds to the expanded height; `titleBottom`: the large title's
// room below it (unset: 28 for a tall bar without a subtitle, 24 otherwise). TwoRowsTopAppBar's:
// the large row's own title and subtitle (`large`), and the small row in the same fonts.
struct LargeTitles {
    Content title, subtitle;
};
void twoRowsTopAppBar(const TopAppBarSlots& slots, const Modifier& modifier, float expandedHeight, const Font& titleFont,
                      const Font& subtitleFont, float subtitleRoom = 24, std::optional<float> titleBottom = {},
                      const LargeTitles* large = nullptr) {
    const ColorScheme& cs = colorScheme();
    float collapsedHeight = std::max(0.f, slots.collapsedHeight.value_or(kAppBarHeight));
    if (slots.subtitle) expandedHeight += subtitleRoom;
    expandedHeight = std::max(collapsedHeight, slots.expandedHeight.value_or(expandedHeight));
    PaddingValues insets = appBarInsets(slots);
    AnimatedState<Color> container = appBarContainer(slots);
    std::optional<TopAppBarState> state;
    if (slots.scrollBehavior) {
        state = slots.scrollBehavior->state;
        state->setHeightOffsetLimit(collapsedHeight - expandedHeight);
    }
    // Fades in the small title as the bar collapses and fades out the large one (while drawing).
    auto fade = [state](bool in) {
        return Modifier().graphicsLayer([state, in](GraphicsLayerScope& l) {
            float f = state ? state->collapsedFraction() : 0.f;
            l.alpha = in ? std::clamp((f - 0.5f) * 2, 0.f, 1.f) : 1 - f;
        });
    };
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurface)}, [&] {
        Column(detail::animatedBackground(modifier.fillMaxWidth(), container).padding(insets.start, insets.top, insets.end, 0), [&] {
            Content smallSubtitle;
            if (slots.subtitle) smallSubtitle = [&] { Box(fade(true), slots.subtitle); };
            Content smallTitle = [&] {
                if (slots.title) Box(fade(true), slots.title);
            };
            bool centered = slots.titleHorizontalAlignment.bias == 0;
            std::optional<std::pair<Font, Font>> smallFonts;
            if (large) smallFonts = std::pair{titleFont, subtitleFont};
            if (centered) centeredAppBarRow(slots, Modifier(), smallTitle, smallSubtitle, collapsedHeight, smallFonts);
            else appBarRow(slots, Modifier(), smallTitle, smallSubtitle, collapsedHeight, smallFonts);
            Modifier bottom = Modifier().fillMaxWidth();
            bottom = state ? collapsing(bottom, *state, expandedHeight - collapsedHeight, false)
                           : bottom.height(expandedHeight - collapsedHeight);
            float below = titleBottom.value_or(expandedHeight > 120 && !slots.subtitle ? 28.f : 24.f);
            Box(bottom, Alignment{slots.titleHorizontalAlignment.bias, 1}, [&] {
                Box(fade(false).padding(16, 0, 16, below), [&] {
                    titles(slots, large ? large->title : slots.title, large ? large->subtitle : slots.subtitle, slots.titleHorizontalAlignment,
                           titleFont, subtitleFont);
                });
            });
        });
    });
}

// TopAppBar and CenterAlignedTopAppBar: one row (its title centered or not), sliding up under
// what is above it as it collapses.
void singleRowTopAppBar(const TopAppBarSlots& slots, const Modifier& modifier, bool centered) {
    const ColorScheme& cs = colorScheme();
    PaddingValues insets = appBarInsets(slots);
    AnimatedState<Color> container = appBarContainer(slots);
    float height = std::max(0.f, slots.expandedHeight.value_or(TopAppBarDefaults::TopAppBarExpandedHeight));
    const auto& behavior = slots.scrollBehavior;
    if (behavior) behavior->state.setHeightOffsetLimit(-height);
    auto row = [&] {
        if (centered)
            centeredAppBarRow(slots, Modifier(), slots.title, slots.subtitle, height);
        else
            appBarRow(slots, Modifier(), slots.title, slots.subtitle, height);
    };
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurface)}, [&] {
        Box(detail::animatedBackground(modifier.fillMaxWidth(), container).padding(insets.start, insets.top, insets.end, 0), [&] {
            if (behavior && !behavior->isPinned)
                Box(collapsing(Modifier().fillMaxWidth(), behavior->state, height, true), row);
            else
                row();
        });
    });
}

} // namespace

void TopAppBar(const TopAppBarSlots& slots, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    singleRowTopAppBar(slots, modifier, false);
}

void AppBarWithSearch(const SearchBarState& state, const Content& inputField, const Modifier& modifier, const AppBarWithSearchSlots& slots,
                      CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    constexpr float kHeight = 72;
    PaddingValues insets = WindowInsets::safeDrawing().only(WindowInsetsSides::Horizontal | WindowInsetsSides::Top).asPaddingValues();
    const auto& behavior = slots.scrollBehavior;
    if (behavior) behavior->state.setHeightOffsetLimit(-kHeight);
    auto row = [&] {
        Row(Modifier().fillMaxWidth().height(kHeight).padding(slots.navigationIcon ? 4.f : 16.f, 0, slots.actions ? 4.f : 16.f, 0),
            {.horizontalArrangement = Arrangement::spacedBy(4), .verticalAlignment = Alignment::CenterVertically}, [&] {
                if (slots.navigationIcon) slots.navigationIcon();
                SearchBar(state, inputField, Modifier().weight(1));
                if (slots.actions) Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, slots.actions);
            });
    };
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurface)}, [&] {
        Box(modifier.fillMaxWidth().background(cs.surface).padding(insets.start, insets.top, insets.end, 0), [&] {
            if (behavior && !behavior->isPinned)
                Box(collapsing(Modifier().fillMaxWidth(), behavior->state, kHeight, false), row);  // slides up, under the status bar
            else
                row();
        });
    });
}

void TopSearchBar(const SearchBarState& state, const Content& inputField, const Modifier& modifier,
                  std::optional<TopAppBarScrollBehavior> scrollBehavior, CallSite site) {
    ComposableScope scope(site);
    constexpr float kPadding = 8, kHeight = 56 + 2 * kPadding;
    PaddingValues insets = WindowInsets::safeDrawing().only(WindowInsetsSides::Horizontal | WindowInsetsSides::Top).asPaddingValues();
    if (scrollBehavior) scrollBehavior->state.setHeightOffsetLimit(-kHeight);
    auto bar = [&] {
        Box(Modifier().fillMaxWidth().padding(kPadding), Alignment::TopCenter,
            [&] { SearchBar(state, inputField, Modifier().widthIn(0, 720).fillMaxWidth()); });
    };
    Box(modifier.fillMaxWidth().padding(insets.start, insets.top, insets.end, 0), [&] {
        if (scrollBehavior && !scrollBehavior->isPinned)
            Box(collapsing(Modifier().fillMaxWidth(), scrollBehavior->state, kHeight, false), bar);  // slides up, under the status bar
        else
            bar();
    });
}

TopAppBarScrollBehavior SearchBarDefaults::enterAlwaysSearchBarScrollBehavior(CallSite site) {
    return TopAppBarDefaults::enterAlwaysScrollBehavior(site);
}

void CenterAlignedTopAppBar(const TopAppBarSlots& slots, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    singleRowTopAppBar(slots, modifier, true);
}

void MediumTopAppBar(const TopAppBarSlots& slots, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    twoRowsTopAppBar(slots, modifier, TopAppBarDefaults::MediumAppBarExpandedHeight, typography().headlineSmall, typography().labelLarge);
}

void MediumFlexibleTopAppBar(const TopAppBarSlots& slots, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    twoRowsTopAppBar(slots, modifier, 112, typography().headlineMedium, typography().labelLarge, 24, 24);
}

void LargeFlexibleTopAppBar(const TopAppBarSlots& slots, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    twoRowsTopAppBar(slots, modifier, 120, typography().displaySmall, typography().titleMedium, 32, 28);
}

void TwoRowsTopAppBar(const std::function<void(bool expanded)>& title, const TopAppBarSlots& slots, const Modifier& modifier,
                      const std::function<void(bool expanded)>& subtitle, CallSite site) {
    ComposableScope scope(site);
    TopAppBarSlots small = slots;
    small.title = [&] { title(false); };
    small.subtitle = subtitle ? Content([&] { subtitle(false); }) : Content();
    LargeTitles large{[&] { title(true); }, subtitle ? Content([&] { subtitle(true); }) : Content()};
    Font ambient = LocalTextStyle.current();  // the titles style themselves
    twoRowsTopAppBar(small, modifier, 112, ambient, ambient, 24, std::nullopt, &large);
}

void LargeTopAppBar(const TopAppBarSlots& slots, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    twoRowsTopAppBar(slots, modifier, TopAppBarDefaults::LargeAppBarExpandedHeight, typography().headlineMedium, typography().titleMedium);
}

void NavigationBar(const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    // The keyboard covers it rather than lifting it.
    PaddingValues insets = detail::systemBarsForVisualComponents().only(WindowInsetsSides::Horizontal | WindowInsetsSides::Bottom).asPaddingValues();
    Row(modifier.fillMaxWidth().background(cs.surfaceContainer).padding(insets.start, 0, insets.end, insets.bottom),
        {.horizontalArrangement = Arrangement::spacedBy(8)}, content);
}

MutableInteractionSource detail::mappedInteractionSource(const MutableInteractionSource& from, std::shared_ptr<Offset> shift, CallSite site) {
    ComposableScope scope(site);
    auto to = remember([] { return MutableInteractionSource(); });
    LaunchedEffect(from.data_.get(), [from, to, shift]() -> Task {
        co_await from.interactions().collect([to, shift](const Interaction& i) {
            Interaction moved = i;
            moved.pressPosition = {i.pressPosition.x - shift->x, i.pressPosition.y - shift->y};
            to.tryEmit(moved);
        });
    });
    return to;
}

void detail::indicatorNavigationItem(bool selected, std::function<void()> onClick, const Content& icon, const Content& label,
                                     const Modifier& item, float indicatorWidth, float top, bool enabled,
                                     std::optional<MutableInteractionSource> interactionSource) {
    const ColorScheme& cs = colorScheme();
    const MotionScheme& motion = motionScheme();
    // The animations read where they are used: the indicator's as it is laid out and drawn, the
    // colors by the icon and the label (not this, which is its caller's scope).
    auto iconColor = animateColorAsState(selected ? cs.onSecondaryContainer : cs.onSurfaceVariant, motion.defaultEffectsSpec);
    auto labelColor = animateColorAsState(selected ? cs.secondary : cs.onSurfaceVariant, motion.defaultEffectsSpec);
    auto tinted = [&](const AnimatedState<Color>& color) {
        return enabled ? LocalContentColor.provides(color) : LocalContentColor.provides(cs.onSurface.copy(0.38f));
    };
    auto alpha = animateFloatAsState(selected ? 1.f : 0.f, motion.defaultEffectsSpec);
    auto grown = animateFloatAsState(selected ? 1.f : 0.f, motion.fastSpatialSpec);
    MutableInteractionSource source = interactionSource ? *interactionSource : remember([] { return MutableInteractionSource(); });
    auto shift = remember([] { return std::make_shared<Offset>(); });
    MutableInteractionSource pill = mappedInteractionSource(source, shift);
    Modifier m = item.onSizeChanged([shift, indicatorWidth, top](Size s) { *shift = {(s.width - indicatorWidth) / 2, top}; });
    Box(m.selectable(selected, std::move(onClick), SemanticsRole::Tab, {.enabled = enabled, .interactionSource = source, .indication = false}),
        Alignment::TopCenter, [&] {
            Box(Modifier().offset(0, top).size(indicatorWidth, 32).clip(CircleShape).indication(pill, ripple()));
            Modifier grows = Modifier().offset(0, top).layout([grown, indicatorWidth](const MeasureNext& next, const Constraints& c, Offset& at) {
                at = {};  // (as size(indicatorWidth * grown, 32))
                float w = std::clamp(indicatorWidth * std::max(0.f, grown.value()), c.minWidth, c.maxWidth), h = std::clamp(32.f, c.minHeight, c.maxHeight);
                next({w, w, h, h});
                return Size{w, h};
            });
            Box(detail::animatedAlpha(grows, alpha).background(cs.secondaryContainer, CircleShape));
            Box(Modifier().offset(0, top + 4), [&] { CompositionLocalProvider({tinted(iconColor)}, icon); });
            Box(Modifier().offset(0, top + 32 + 4), [&] {
                CompositionLocalProvider({tinted(labelColor), LocalTextStyle.provides(typography().labelMedium)}, label);
            });
        });
}

void NavigationBarItem(bool selected, std::function<void()> onClick, const ImageVector& icon, std::string label,
                       CallSite site) {
    ComposableScope scope(site);
    // Compose 1.4's: 80 dp tall, the 64 dp wide indicator 12 dp down.
    detail::indicatorNavigationItem(selected, std::move(onClick), [icon] { Icon(icon, ""); }, [label] { Text(label); },  // (the label says it)
                                    Modifier().weight(1).height(80), 64, 12, true, std::nullopt);
}

namespace {
// A FAB's container (Compose 1.4's): primaryContainer, 6 dp up, 8 while hovered (6 again while
// pressed), eased in 120 ms and out 150 as the buttons' elevations.
Modifier fab(const Modifier& m, Shape shape, std::function<void()> onClick, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    auto source = remember([] { return MutableInteractionSource(); });
    bool hovered = source.collectIsHoveredAsState() && !source.collectIsPressedAsState();
    auto elevation = animateFloatAsState(hovered ? 8.f : 6.f, hovered ? tween(120) : tween(150, 0, Easing{0.4f, 0, 0.6f, 1}));
    return detail::animatedShadow(m, elevation, shape).background(colorScheme().primaryContainer, shape).clickable(std::move(onClick), {.interactionSource = source});
}
} // namespace

void SmallFloatingActionButton(std::function<void()> onClick, const Content& content, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    CompositionLocalProvider({LocalContentColor.provides(cs.onPrimaryContainer)}, [&] {
        Box(fab(modifier.minimumInteractiveComponentSize().size(40), RoundedCornerShape(12), std::move(onClick)), Alignment::Center, content);
    });
}

void LargeFloatingActionButton(std::function<void()> onClick, const Content& content, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    CompositionLocalProvider({LocalContentColor.provides(cs.onPrimaryContainer)}, [&] {
        Box(fab(modifier.size(96), RoundedCornerShape(28), std::move(onClick)), Alignment::Center, content);
    });
}

void FloatingActionButton(std::function<void()> onClick, const Content& content, const Modifier& modifier,
                          CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    CompositionLocalProvider({LocalContentColor.provides(cs.onPrimaryContainer)}, [&] {
        Box(fab(modifier.size(56), RoundedCornerShape(16), std::move(onClick)), Alignment::Center, content);
    });
}

void ExtendedFloatingActionButton(std::function<void()> onClick, const ImageVector& icon, std::string text,
                                  const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    // 16 dp before the icon, 12 between, 20 after the label; at least 80 wide.
    CompositionLocalProvider({LocalContentColor.provides(cs.onPrimaryContainer)}, [&] {
        Row(fab(modifier.heightIn(56).widthIn(80), RoundedCornerShape(16), std::move(onClick)).padding(16, 0, 20, 0),
            {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically},
            [&] {
                Icon(icon, text);
                Text(text, {.font = typography().labelLarge});
            });
    });
}

void ToggleFloatingActionButton(bool checked, std::function<void(bool)> onCheckedChange,
                                const std::function<void(const ToggleFloatingActionButtonScope&)>& content, const Modifier& modifier,
                                const ToggleFloatingActionButtonOptions& options, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    // Its size, corners and colors along the progress, read as it is laid out and drawn.
    auto progress = animateFloatAsState(checked ? 1.f : 0.f, motionScheme().fastSpatialSpec);
    auto sizeAt = [from = options.containerSize, to = options.checkedContainerSize](float p) { return from + (to - from) * p; };
    Shape shape{Shape::Kind::Rounded, 0, 0, true};
    shape.animatedCorners = std::make_shared<const std::function<std::array<CornerSize, 4>()>>([progress, sizeAt, corner = options.cornerRadius] {
        float p = progress.value(), radius = corner + (sizeAt(p) / 2 - corner) * std::clamp(p, 0.f, 1.f);
        return std::array<CornerSize, 4>{CornerSize{radius}, CornerSize{radius}, CornerSize{radius}, CornerSize{radius}};
    });
    Color off = options.containerColor.value_or(cs.primaryContainer), on = options.checkedContainerColor.value_or(cs.primary);
    detail::Live<Color> container{[progress, off, on] { return lerp(off, on, std::clamp(progress.value(), 0.f, 1.f)); }};
    struct ContentColor {  // (a state for LocalContentColor: its readers follow it)
        AnimatedState<float> progress;
        Color off, on;
        Color value() const { return lerp(off, on, std::clamp(progress.value(), 0.f, 1.f)); }
        bool operator==(const ContentColor&) const = default;
    };
    Modifier sized = Modifier().layout([progress, sizeAt](const MeasureNext& next, const Constraints& c, Offset& at) {
        at = {};  // (as size(size))
        float s = sizeAt(progress.value()), w = std::clamp(s, c.minWidth, c.maxWidth), h = std::clamp(s, c.minHeight, c.maxHeight);
        next({w, w, h, h});
        return Size{w, h};
    });
    float outer = std::max(options.containerSize, options.checkedContainerSize);
    Box(modifier.size(outer), options.contentAlignment, [&] {
        CompositionLocalProvider({LocalContentColor.provides(ContentColor{progress, cs.onPrimaryContainer, cs.onPrimary})}, [&] {
            Box(detail::animatedBackground(sized.shadow(6, shape), container, shape).toggleable(checked, std::move(onCheckedChange)),
                Alignment::Center, [&] { content(ToggleFloatingActionButtonScope(progress)); });
        });
    });
}

struct FloatingActionButtonMenuScope::Data {
    bool expanded = false;
    int index = 0;  // of the next item while composing
    int count = 0;  // items the last time
};

void FloatingActionButtonMenu(bool expanded, const Content& button, const std::function<void(const FloatingActionButtonMenuScope&)>& content,
                              const Modifier& modifier, HorizontalAlignment horizontalAlignment, CallSite site) {
    ComposableScope scope(site);
    auto data = remember([] { return std::make_shared<FloatingActionButtonMenuScope::Data>(); });
    data->expanded = expanded;
    data->index = 0;
    Column(modifier, {.horizontalAlignment = horizontalAlignment}, [&] {
        Column(Modifier().padding(0, 0, 0, 8), {.verticalArrangement = Arrangement::spacedBy(4), .horizontalAlignment = horizontalAlignment},
               [&] { content(FloatingActionButtonMenuScope(data)); });
        button();
    });
    data->count = data->index;
}

void FloatingActionButtonMenuItem(const FloatingActionButtonMenuScope& menu, std::function<void()> onClick, const Content& text,
                                  const Content& icon, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    auto& d = *menu.data();
    int i = d.index++, n = std::max(d.count, d.index);
    // One after another: opening from the bottom (next to the button), closing from the top.
    constexpr float kStagger = 50;
    float opening = float(n - 1 - i) * kStagger, closing = float(i) * kStagger;
    const ColorScheme& cs = colorScheme();
    AnimatedVisibility(d.expanded, Modifier(), fadeIn(0, tween(200, opening)) + expandVertically(tween(200, opening)),
                       fadeOut(0, tween(150, closing)) + shrinkVertically(tween(150, closing)), [&] {
                           CompositionLocalProvider({LocalContentColor.provides(cs.onPrimaryContainer)}, [&] {
                               Row(modifier.height(56).background(cs.primaryContainer, CircleShape).clickable(std::move(onClick)).padding(24, 0),
                                   {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically}, [&] {
                                       icon();
                                       CompositionLocalProvider({LocalTextStyle.provides(typography().titleMedium)}, text);
                                   });
                           });
                       });
}

void MediumFloatingActionButton(std::function<void()> onClick, const Content& content, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    CompositionLocalProvider({LocalContentColor.provides(cs.onPrimaryContainer)}, [&] {
        Box(fab(modifier.size(80), RoundedCornerShape(20), std::move(onClick)), Alignment::Center, content);
    });
}

namespace {

struct ExtendedFabSize {
    float height, radius, icon, padding, gap;
    Font label;
};

// An extended FAB that narrows to a square around its icon as `expanded` goes off, the label
// fading out.
void extendedFab(const ExtendedFabSize& size, std::function<void()> onClick, const ImageVector& icon, const std::string& text, bool expanded,
                 const Modifier& modifier) {
    const ColorScheme& cs = colorScheme();
    auto p = animateFloatAsState(expanded ? 1.f : 0.f, spring(1, 700));  // read as it is laid out and drawn
    CompositionLocalProvider({LocalContentColor.provides(cs.onPrimaryContainer)}, [&] {
        Layout(fab(modifier.height(size.height), RoundedCornerShape(size.radius), std::move(onClick))
                   .semantics({.contentDescription = text})
                   .clipToBounds(),
               [&] {
                   Icon(icon, "", Modifier().size(size.icon));
                   Text(text, Modifier().graphicsLayer([p](GraphicsLayerScope& l) { l.alpha = std::clamp(p.value(), 0.f, 1.f); }),
                        {.font = size.label, .maxLines = 1});
               },
               [size, state = p](const std::vector<Measurable>& m, const Constraints& c) {
                   float p = state.value();
                   Placeable picture = m[0].measure({}), label = m[1].measure({});
                   float h = c.hasBoundedHeight() ? c.maxHeight : size.height;
                   float full = size.padding + picture.width + size.gap + label.width + size.padding;
                   float width = h + (full - h) * p;  // a square, then the full width
                   float x = (h - picture.width) / 2 + (size.padding - (h - picture.width) / 2) * p;
                   picture.place(x, (h - picture.height) / 2);
                   label.place(x + picture.width + size.gap, (h - label.height) / 2);
                   return c.constrain({width, h});
               });
    });
}

} // namespace

void SmallExtendedFloatingActionButton(std::function<void()> onClick, const ImageVector& icon, std::string text, bool expanded,
                                       const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    extendedFab({56, 16, 24, 16, 8, typography().titleMedium}, std::move(onClick), icon, text, expanded, modifier);
}

void MediumExtendedFloatingActionButton(std::function<void()> onClick, const ImageVector& icon, std::string text, bool expanded,
                                        const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    extendedFab({80, 20, 28, 26, 12, typography().titleLarge}, std::move(onClick), icon, text, expanded, modifier);
}

void LargeExtendedFloatingActionButton(std::function<void()> onClick, const ImageVector& icon, std::string text, bool expanded,
                                       const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    extendedFab({96, 28, 36, 28, 16, typography().headlineSmall}, std::move(onClick), icon, text, expanded, modifier);
}

struct SnackbarHostState::Data {
    MutableState<std::optional<SnackbarData>> current{std::nullopt};
    std::deque<SnackbarData> queue;
    uint64_t nextId = 1;
};

SnackbarHostState::SnackbarHostState() : data_(std::make_shared<Data>()) {}

void SnackbarHostState::enqueue(SnackbarData d) const {
    d.id = data_->nextId++;
    if (!data_->current.peek())
        data_->current.set(std::move(d));
    else
        data_->queue.push_back(std::move(d));
}

void SnackbarHostState::showSnackbar(std::string message, std::string actionLabel, std::function<void()> onAction,
                                     SnackbarDuration duration) const {
    enqueue({0, std::move(message), std::move(actionLabel), std::move(onAction), duration});
}

SnackbarAwaiter SnackbarHostState::showSnackbarAsync(std::string message, std::string actionLabel, std::optional<SnackbarDuration> duration,
                                                     bool withDismissAction) const {
    SnackbarDuration d = duration.value_or(actionLabel.empty() ? SnackbarDuration::Short : SnackbarDuration::Indefinite);
    return SnackbarAwaiter(data_, {0, std::move(message), std::move(actionLabel), {}, d, withDismissAction, {}});
}

void SnackbarHostState::remove(uint64_t id) const {
    std::erase_if(data_->queue, [&](const SnackbarData& d) { return d.id == id; });
    const auto& current = data_->current.peek();
    if (current && current->id == id) {
        auto next = data_->queue.empty() ? std::optional<SnackbarData>() : std::optional(data_->queue.front());
        if (next) data_->queue.pop_front();
        data_->current.set(std::move(next));
    }
}

SnackbarAwaiter::SnackbarAwaiter(std::shared_ptr<void> state, SnackbarData data) : state_(std::move(state)), data_(std::move(data)) {}

void SnackbarAwaiter::suspend(std::coroutine_handle<> handle, detail::JobState* job) {
    std::weak_ptr<detail::JobState> weak = job->weak_from_this();
    detail::Dispatcher* dispatcher = job->dispatcher;
    data_.onResult = [result = result_, weak, dispatcher, handle](SnackbarResult r) {
        *result = r;
        if (auto j = weak.lock()) dispatcher->resumeNow(j, handle);
    };
    SnackbarHostState host;
    host.data_ = std::static_pointer_cast<SnackbarHostState::Data>(state_);
    host.enqueue(data_);
    data_.id = host.data_->nextId - 1;
    queued_ = true;
}

SnackbarAwaiter::~SnackbarAwaiter() {
    if (!queued_ || result_->has_value()) return;
    SnackbarHostState host;  // cancelled while waiting: the snackbar goes too
    host.data_ = std::static_pointer_cast<SnackbarHostState::Data>(state_);
    host.remove(data_.id);
}

std::optional<SnackbarData> SnackbarHostState::current() const { return data_->current.get(); }

void SnackbarHostState::dismiss(uint64_t id, SnackbarResult result) const {
    const auto& current = data_->current.peek();
    if (!current || current->id != id) return;
    auto onResult = current->onResult;
    if (data_->queue.empty()) {
        data_->current.set(std::nullopt);
    } else {
        data_->current.set(std::move(data_->queue.front()));
        data_->queue.pop_front();
    }
    if (onResult) onResult(result);  // last: it may resume a coroutine that shows the next one
}

void AppBarScope::clickableItem(std::function<void()> onClick, Content icon, std::string label, bool enabled) {
    items.push_back({[onClick, icon, enabled] { IconButton(onClick, Modifier(), {.enabled = enabled}, icon); },
                     [onClick, label, enabled](const AppBarMenuState& menu) {
                         DropdownMenuItem([label] { Text(label); }, [onClick, menu] {
                             onClick();
                             menu.dismiss();
                         }, {.enabled = enabled});
                     }});
}

void AppBarScope::toggleableItem(bool checked, std::function<void(bool)> onCheckedChange, Content icon, std::string label, bool enabled) {
    items.push_back({[checked, onCheckedChange, icon, enabled] { IconToggleButton(checked, onCheckedChange, Modifier(), {.enabled = enabled}, icon); },
                     [checked, onCheckedChange, label, enabled](const AppBarMenuState& menu) {
                         DropdownMenuItem([label] { Text(label); }, [checked, onCheckedChange, menu] {
                             onCheckedChange(!checked);
                             menu.dismiss();
                         }, {.enabled = enabled});
                     }});
}

void AppBarScope::customItem(Content appBarContent, std::function<void(const AppBarMenuState&)> menuContent) {
    items.push_back({std::move(appBarContent), std::move(menuContent)});
}

namespace {
// AppBarRow and AppBarColumn: the items, then the overflow button with its menu of what didn't
// fit (Compose's OverflowMeasurePolicy: room kept for the button, the last item may take it).
void appBarItems(bool vertical, std::function<void(AppBarScope&)> content, const Modifier& modifier, const AppBarRowOptions& o) {
    AppBarScope row;
    content(row);
    auto menu = remember([] { return AppBarMenuState(); });
    auto visible = remember([] { return mutableStateOf(-1); });  // how many fit, from the last layout (-1: not laid out yet)
    int count = int(row.items.size()), maxItems = std::max(1, o.maxItemCount);
    Layout(modifier, [&] {
        for (const auto& item : row.items) item.appBar();
        Box([&] {
            if (o.overflowIndicator) o.overflowIndicator(menu);
            else IconButton([menu] { menu.show(); }, [] { Icon(Icons::Filled::MoreVert, LocalStrings.current().moreOptions); });
            DropdownMenu(menu.isShowing(), [menu] { menu.dismiss(); }, [&] {
                for (int i = std::max(0, visible.get()); i < count; ++i) row.items[size_t(i)].menu(menu);
            });
        });
    }, [visible, count, maxItems, vertical](const std::vector<Measurable>& children, const Constraints& c) {
        auto along = [vertical](float w, float h) { return vertical ? h : w; };
        Constraints loose = c.loosen();
        const Measurable& overflowButton = children.back();
        float reserved;
        {
            detail::ProbeMeasure probe;  // only its size, for the room to keep
            Placeable p = overflowButton.measure(loose);
            reserved = along(p.width, p.height);
        }
        float remaining = std::max(0.f, along(c.maxWidth, c.maxHeight) - reserved);
        std::vector<Placeable> shown;
        for (int i = 0; i < count && i < int(children.size()) - 1; ++i) {
            Placeable p = children[size_t(i)].measure(loose);
            bool last = i == count - 1;
            if (!last && i == maxItems - 1) break;  // the button takes the last slot
            float size = along(p.width, p.height);
            if (size <= remaining || (last && size <= remaining + reserved)) {
                shown.push_back(p);
                remaining -= size;
            } else {
                break;
            }
        }
        if (!detail::isProbeMeasure() && visible.peek() != int(shown.size())) visible.set(int(shown.size()));
        if (int(shown.size()) != count) shown.push_back(overflowButton.measure(loose));
        float across = 0, at = 0;  // the thickness, and how far along
        for (const Placeable& p : shown) across = std::max(across, vertical ? p.width : p.height);
        for (const Placeable& p : shown) {
            if (vertical) p.place((across - p.width) / 2, at);
            else p.place(at, (across - p.height) / 2);
            at += along(p.width, p.height);
        }
        return c.constrain(vertical ? Size{across, at} : Size{at, across});
    });
}
}  // namespace

void AppBarRow(std::function<void(AppBarScope&)> content, const Modifier& modifier, AppBarRowOptions o, CallSite site) {
    ComposableScope scope(site);
    appBarItems(false, std::move(content), modifier, o);
}

void AppBarColumn(std::function<void(AppBarScope&)> content, const Modifier& modifier, AppBarRowOptions o, CallSite site) {
    ComposableScope scope(site);
    appBarItems(true, std::move(content), modifier, o);
}

void SnackbarHost(SnackbarHostState state, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    SnackbarHost(state, modifier, [](const SnackbarData& data) { Snackbar(data); });
}

void SnackbarHost(SnackbarHostState state, const Modifier& modifier, std::function<void(const SnackbarData&)> snackbar, CallSite site) {
    composable(site, state, modifier, [=] {
        std::optional<SnackbarData> current = state.current();
        auto& shown = remember([] { return std::optional<SnackbarData>(); });  // kept for the exit animation
        if (current) shown = current;
        // Composed even with nothing to show, so the first snackbar animates in.
        const SnackbarData* data = shown ? &*shown : nullptr;
        uint64_t id = data ? data->id : 0;
        int millis = !data ? 0 : data->duration == SnackbarDuration::Short ? 4000 : data->duration == SnackbarDuration::Long ? 10000 : 0;
        if (millis > 0 && current)
            LaunchedEffect(id, [state, id, millis]() -> Task {
                co_await delay(std::chrono::milliseconds(millis));
                state.dismiss(id);
            });
        // In and out as Compose's: faded and grown from 80% on its fast springs.
        AnimationSpec effects = motionScheme().fastEffectsSpec, spatial = motionScheme().fastSpatialSpec;
        AnimatedVisibility(current.has_value(), modifier, fadeIn(0, effects) + scaleIn(0.8f, {0.5f, 0.5f}, spatial),
                           fadeOut(0, effects) + scaleOut(0.8f, {0.5f, 0.5f}, spatial), [&] {
            if (!data) return;
            SnackbarData handed = *data;
            handed.finish = [state, id, onAction = data->onAction](SnackbarResult result) {
                if (result == SnackbarResult::ActionPerformed && onAction) onAction();
                state.dismiss(id, result);
            };
            snackbar(handed);
        });
    });
}

namespace {
constexpr float kSnackbarMaxWidth = 600;
constexpr float kSnackbarStart = 16, kSnackbarButtonSide = 8;

// Compose's one row snackbar (the text, then the action and the close button at the end): one
// line 48 dp tall, the action on the text's baseline; more, 68 dp at least, the first baseline
// 30 dp down and the buttons in the middle.
Size oneRowSnackbar(const std::vector<Measurable>& children, const Constraints& c) {
    float width = std::min(c.maxWidth, kSnackbarMaxWidth);
    std::optional<Measurable> text, action, dismiss;
    for (const Measurable& m : children) {
        std::string id = m.layoutId();
        (id == "dismiss" ? dismiss : id == "action" ? action : text) = m;
    }
    std::optional<Placeable> a, d;
    if (action) a = action->measure(c.loosen());
    if (dismiss) d = dismiss->measure(c.loosen());
    float aw = a ? a->width : 0, ah = a ? a->height : 0, dw = d ? d->width : 0, dh = d ? d->height : 0;
    float textMax = std::max(c.minWidth, width - aw - dw - (d ? 0.f : kSnackbarButtonSide));
    Placeable t = text->measure({0, textMax, 0, c.maxHeight});
    float first = t[FirstBaseline], last = t[LastBaseline];
    bool oneLine = std::isnan(first) || std::isnan(last) || first == last;
    float height, textY, actionY = 0;
    if (oneLine) {
        height = std::max({48.f, ah, dh});
        textY = (height - t.height) / 2;
        if (a && !std::isnan((*a)[FirstBaseline]) && !std::isnan(first)) actionY = textY + first - (*a)[FirstBaseline];
    } else {
        textY = 30 - first;
        height = std::max(68.f, textY + t.height);
        actionY = (height - ah) / 2;
    }
    t.place(0, textY);
    if (a) a->place(width - dw - aw, actionY);
    if (d) d->place(width - dw, (height - dh) / 2);
    return {width, height};
}
} // namespace

void Snackbar(const Content& content, const Modifier& modifier, const SnackbarOptions& o, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    Shape shape = o.shape.value_or(RoundedCornerShape(4));
    Color container = o.containerColor.value_or(cs.inverseSurface), contentColor = o.contentColor.value_or(cs.inverseOnSurface);
    Color actionColor = o.actionContentColor.value_or(cs.inversePrimary);
    Color dismissColor = o.dismissActionContentColor.value_or(cs.inverseOnSurface);
    auto action = [&] {
        CompositionLocalProvider({LocalContentColor.provides(actionColor), LocalTextStyle.provides(typography().labelLarge)}, o.action);
    };
    auto dismiss = [&] { CompositionLocalProvider({LocalContentColor.provides(dismissColor)}, o.dismissAction); };
    Box(modifier.shadow(6, shape).background(container, shape).clip(shape), [&] {
        CompositionLocalProvider({LocalContentColor.provides(contentColor), LocalTextStyle.provides(typography().bodyMedium)}, [&] {
            if (o.actionOnNewLine && o.action) {  // the text, then the buttons under it at the end
                Column(Modifier().widthIn(0, kSnackbarMaxWidth).fillMaxWidth().padding(kSnackbarStart, 0, 0, 2), [&] {
                    Box(Modifier().paddingFromBaseline(30, 12).padding(0, 0, kSnackbarButtonSide, 0), content);
                    Box(Modifier().align(Alignment::End).padding(0, 0, o.dismissAction ? 0.f : kSnackbarButtonSide, 0), [&] {
                        Row([&] {
                            action();
                            if (o.dismissAction) dismiss();
                        });
                    });
                });
                return;
            }
            Layout(Modifier().padding(kSnackbarStart, 0, o.dismissAction ? 0.f : kSnackbarButtonSide, 0), [&] {
                Box(Modifier().padding(0, 6), content);
                if (o.action) Box(Modifier().layoutId("action"), action);
                if (o.dismissAction) Box(Modifier().layoutId("dismiss"), dismiss);
            }, oneRowSnackbar);
        });
    });
}

void Snackbar(const SnackbarData& data, const Modifier& modifier, SnackbarOptions o, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    if (!data.actionLabel.empty())
        o.action = [&data, color = o.actionColor.value_or(cs.inversePrimary)] {
            TextButton([data] { data.performAction(); }, Modifier(), {.contentColor = color}, [&] { Text(data.actionLabel); });
        };
    if (data.withDismissAction)
        o.dismissAction = [&data] { IconButton([data] { data.dismiss(); }, [] { Icon(Icons::Filled::Close, LocalStrings.current().dismiss); }); };
    Snackbar([&] { Text(data.message); }, modifier.padding(12), o);
}

} // namespace kompoz
