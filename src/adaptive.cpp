#include <kompoz/adaptive.hpp>
#include <kompoz/animation.hpp>
#include <kompoz/coroutines.hpp>
#include <kompoz/gestures.hpp>
#include <kompoz/host.hpp>

#include <algorithm>
#include <cmath>

namespace kompoz {

WindowSizeClass WindowSizeClass::compute(float widthDp, float heightDp) {
    WindowSizeClass c;
    c.minWidthDp = widthDp >= WIDTH_DP_EXPANDED_LOWER_BOUND ? WIDTH_DP_EXPANDED_LOWER_BOUND
                   : widthDp >= WIDTH_DP_MEDIUM_LOWER_BOUND ? WIDTH_DP_MEDIUM_LOWER_BOUND
                                                            : 0;
    c.minHeightDp = heightDp >= HEIGHT_DP_EXPANDED_LOWER_BOUND ? HEIGHT_DP_EXPANDED_LOWER_BOUND
                    : heightDp >= HEIGHT_DP_MEDIUM_LOWER_BOUND ? HEIGHT_DP_MEDIUM_LOWER_BOUND
                                                               : 0;
    c.windowWidthSizeClass = c.minWidthDp >= WIDTH_DP_EXPANDED_LOWER_BOUND ? WindowWidthSizeClass::Expanded
                             : c.minWidthDp >= WIDTH_DP_MEDIUM_LOWER_BOUND ? WindowWidthSizeClass::Medium
                                                                           : WindowWidthSizeClass::Compact;
    c.windowHeightSizeClass = c.minHeightDp >= HEIGHT_DP_EXPANDED_LOWER_BOUND ? WindowHeightSizeClass::Expanded
                              : c.minHeightDp >= HEIGHT_DP_MEDIUM_LOWER_BOUND ? WindowHeightSizeClass::Medium
                                                                              : WindowHeightSizeClass::Compact;
    return c;
}

WindowAdaptiveInfo currentWindowAdaptiveInfo() {
    Size window = LocalWindowSize.current();
    return {WindowSizeClass::compute(window.width, window.height), LocalUiMediaScope.current().windowPosture()};
}

NavigationSuiteType NavigationSuiteScaffoldDefaults::navigationSuiteType(const WindowAdaptiveInfo& adaptiveInfo) {
    const WindowSizeClass& c = adaptiveInfo.windowSizeClass;
    if (c.windowWidthSizeClass == WindowWidthSizeClass::Compact) return NavigationSuiteType::ShortNavigationBarCompact;
    if (adaptiveInfo.windowPosture == Posture::Tabletop || c.windowHeightSizeClass == WindowHeightSizeClass::Compact)
        return NavigationSuiteType::ShortNavigationBarMedium;
    return NavigationSuiteType::WideNavigationRailCollapsed;
}

NavigationSuiteType NavigationSuiteScaffoldDefaults::calculateFromAdaptiveInfo(const WindowAdaptiveInfo& adaptiveInfo) {
    const WindowSizeClass& c = adaptiveInfo.windowSizeClass;
    if (adaptiveInfo.windowPosture == Posture::Tabletop || c.windowHeightSizeClass == WindowHeightSizeClass::Compact ||
        c.windowWidthSizeClass == WindowWidthSizeClass::Compact)
        return NavigationSuiteType::NavigationBar;
    return NavigationSuiteType::NavigationRail;
}

namespace {

bool isBar(NavigationSuiteType t) {
    return t == NavigationSuiteType::ShortNavigationBarCompact || t == NavigationSuiteType::ShortNavigationBarMedium ||
           t == NavigationSuiteType::NavigationBar;
}

// Compose's: the default spatial spring.
const AnimationSpec kSuiteSpec = spring(0.9f, 700);

Task animateSuite(std::shared_ptr<NavigationSuiteScaffoldState::Data> data, float to);

} // namespace

struct NavigationSuiteScaffoldState::Data {
    Animatable<float> fraction;  // 1 shown, 0 hidden (read while laying out)
    explicit Data(float initial) : fraction(initial) {}
};

namespace {
Task animateSuite(std::shared_ptr<NavigationSuiteScaffoldState::Data> data, float to) { co_await data->fraction.animateTo(to, kSuiteSpec); }
} // namespace

NavigationSuiteScaffoldState::NavigationSuiteScaffoldState(NavigationSuiteScaffoldValue initialValue)
    : data_(std::make_shared<Data>(initialValue == NavigationSuiteScaffoldValue::Visible ? 1.f : 0.f)) {}
bool NavigationSuiteScaffoldState::isAnimating() const { return data_->fraction.isRunning(); }
NavigationSuiteScaffoldValue NavigationSuiteScaffoldState::targetValue() const {
    return data_->fraction.targetValue() == 1 ? NavigationSuiteScaffoldValue::Visible : NavigationSuiteScaffoldValue::Hidden;
}
NavigationSuiteScaffoldValue NavigationSuiteScaffoldState::currentValue() const {
    return data_->fraction.value() == 1 ? NavigationSuiteScaffoldValue::Visible : NavigationSuiteScaffoldValue::Hidden;
}
float NavigationSuiteScaffoldState::visibleFraction() const { return data_->fraction.value(); }
Task NavigationSuiteScaffoldState::hide() const { return animateSuite(data_, 0); }
Task NavigationSuiteScaffoldState::show() const { return animateSuite(data_, 1); }
Task NavigationSuiteScaffoldState::toggle() const { return animateSuite(data_, targetValue() == NavigationSuiteScaffoldValue::Visible ? 0.f : 1.f); }
void NavigationSuiteScaffoldState::snapTo(NavigationSuiteScaffoldValue value) const {
    data_->fraction.snapTo(value == NavigationSuiteScaffoldValue::Visible ? 1.f : 0.f);
}

SavedValue Saver<NavigationSuiteScaffoldState>::save(const NavigationSuiteScaffoldState& s) {
    return s.targetValue() == NavigationSuiteScaffoldValue::Visible;
}
std::optional<NavigationSuiteScaffoldState> Saver<NavigationSuiteScaffoldState>::restore(const SavedValue& v) {
    std::optional<bool> visible = v.boolean();
    if (!visible) return std::nullopt;
    return NavigationSuiteScaffoldState(*visible ? NavigationSuiteScaffoldValue::Visible : NavigationSuiteScaffoldValue::Hidden);
}

NavigationSuiteScaffoldState rememberNavigationSuiteScaffoldState(NavigationSuiteScaffoldValue initialValue, CallSite site) {
    return rememberSaveable([initialValue] { return NavigationSuiteScaffoldState(initialValue); }, site);
}

void NavigationSuiteScaffoldLayout(const Content& navigationSuite, NavigationSuiteType navigationSuiteType, const Content& content,
                                   std::optional<NavigationSuiteScaffoldState> state, const Content& primaryActionContent,
                                   HorizontalAlignment primaryActionContentHorizontalAlignment, CallSite site) {
    ComposableScope scope(site);
    NavigationSuiteScaffoldState shown = state ? *state : rememberNavigationSuiteScaffoldState();
    bool bar = isBar(navigationSuiteType);
    // The content keeps its place in the composition whatever the navigation is in (a window
    // getting wider keeps the screen's state); only the navigation's slot changes.
    auto policy = [type = navigationSuiteType, bar, align = primaryActionContentHorizontalAlignment.bias, shown](LayoutNode& node,
                                                                                                                 const Constraints& c) {
        float w = c.hasBoundedWidth() ? c.maxWidth : c.minWidth, h = c.hasBoundedHeight() ? c.maxHeight : c.minHeight;
        float p = std::clamp(shown.visibleFraction(), 0.f, 1.f);  // (read as it lays out: hiding lays out again)
        LayoutNode& suite = *node.children[0];
        LayoutNode& action = *node.children[1];
        LayoutNode& body = *node.children[2];
        if (bar) {
            Size nav = suite.measure({w, w, 0, h});
            float showing = nav.height * p;
            body.measure({0, w, 0, std::max(0.f, h - showing)});  // (as Compose's, content that doesn't fill keeps its size)
            body.place(0, 0);
            suite.place(0, h - showing);
            Size a = action.measure({0, w, 0, h});  // above the bar, 16 dp from the side it is aligned to
            constexpr float pad = NavigationSuiteScaffoldDefaults::PrimaryActionContentPadding;
            float x = align < 0 ? pad : align == 0 ? (w - a.width) / 2 : w - a.width - pad;
            action.place(x, h - a.height - pad - showing);
        } else if (type == NavigationSuiteType::None) {
            suite.measure({0, 0, 0, 0});
            suite.place(0, 0);
            action.measure({0, 0, 0, 0});
            body.measure({0, w, 0, h});
            body.place(0, 0);
        } else {  // at the start, sliding out to it
            Size nav = suite.measure({0, w, h, h});
            float showing = nav.width * p;
            suite.place(showing - nav.width, 0);
            action.measure({0, 0, 0, 0});
            body.measure({0, std::max(0.f, w - showing), 0, h});
            body.place(showing, 0);
        }
        return Size{w, h};
    };
    detail::Composer::require().emit(
        detail::keyOf(CallSite::current()),
        [&](LayoutNode& node) {
            node.modifier = Modifier();
            node.measurePolicy = policy;
            node.drawContent = {};
        },
        [&] {
            detail::stackSlot(navigationSuite);
            detail::stackSlot(bar ? primaryActionContent : Content{});  // rails and drawers have it at their top
            detail::stackSlot(content);
        });
}

void NavigationSuite(NavigationSuiteType navigationSuiteType, const Modifier& modifier, const Content& content,
                     const Content& primaryActionContent, Arrangement::Kind verticalArrangement, CallSite site) {
    ComposableScope scope(site);
    // The items at the top, in the middle or at the bottom of a rail or drawer.
    auto arranged = [&] {
        if (verticalArrangement != Arrangement::Kind::Start) Spacer(Modifier().weight(1));
        content();
        if (verticalArrangement == Arrangement::Kind::Center) Spacer(Modifier().weight(1));
    };
    switch (navigationSuiteType) {
    case NavigationSuiteType::ShortNavigationBarCompact:
    case NavigationSuiteType::ShortNavigationBarMedium: ShortNavigationBar(modifier, content); break;
    case NavigationSuiteType::WideNavigationRailCollapsed:
        WideNavigationRail(modifier, content, {.header = primaryActionContent, .arrangement = verticalArrangement});
        break;
    case NavigationSuiteType::WideNavigationRailExpanded: {
        WideNavigationRailState expanded = rememberWideNavigationRailState(true);
        WideNavigationRail(modifier, content, {.state = expanded, .header = primaryActionContent, .arrangement = verticalArrangement});
        break;
    }
    // (As Compose's: the older bar's items are short bar items, so its bar is a short one as tall.)
    case NavigationSuiteType::NavigationBar: ShortNavigationBar(modifier.heightIn(80), content); break;
    case NavigationSuiteType::NavigationRail: NavigationRail(modifier, primaryActionContent, arranged); break;
    case NavigationSuiteType::NavigationDrawer:
        PermanentDrawerSheet(modifier, [&] {
            if (primaryActionContent) primaryActionContent();
            arranged();
        });
        break;
    case NavigationSuiteType::None: break;
    }
}

void NavigationSuiteItem(bool selected, std::function<void()> onClick, const Content& icon, const Content& label, const Modifier& modifier,
                         const NavigationSuiteItemOptions& options, CallSite site) {
    ComposableScope scope(site);
    NavigationSuiteType type = options.navigationSuiteType.value_or(NavigationSuiteScaffoldDefaults::navigationSuiteType(currentWindowAdaptiveInfo()));
    Content badge = options.badge;
    Content picture = badge ? Content([&] { BadgedBox(badge, Modifier(), icon); }) : icon;
    switch (type) {
    case NavigationSuiteType::ShortNavigationBarCompact:
    case NavigationSuiteType::ShortNavigationBarMedium:
        ShortNavigationBarItem(selected, std::move(onClick), picture, label, modifier,
                               {.iconPosition = type == NavigationSuiteType::ShortNavigationBarCompact ? NavigationItemIconPosition::Top
                                                                                                       : NavigationItemIconPosition::Start,
                                .enabled = options.enabled,
                                .interactionSource = options.interactionSource});
        break;
    case NavigationSuiteType::WideNavigationRailCollapsed:
    case NavigationSuiteType::WideNavigationRailExpanded:
        WideNavigationRailItem(selected, std::move(onClick), picture, label, modifier,
                               {.railExpanded = type == NavigationSuiteType::WideNavigationRailExpanded,
                                .enabled = options.enabled,
                                .interactionSource = options.interactionSource});
        break;
    case NavigationSuiteType::NavigationBar:  // a short bar item, 8 dp lower: as the older bar's
        ShortNavigationBarItem(selected, std::move(onClick), picture, label, modifier.padding(0, 8, 0, 0),
                               {.iconPosition = NavigationItemIconPosition::Top, .enabled = options.enabled, .interactionSource = options.interactionSource});
        break;
    case NavigationSuiteType::NavigationRail:
        NavigationRailItem(selected, std::move(onClick), picture, label, modifier, {.enabled = options.enabled, .interactionSource = options.interactionSource});
        break;
    case NavigationSuiteType::NavigationDrawer: NavigationDrawerItem(label, selected, std::move(onClick), icon, badge, modifier); break;
    case NavigationSuiteType::None: break;
    }
}

namespace {

// What the bar or rail keeps clear of, the content needn't: below a bar, the navigation bar and
// the bar itself (laid out above it, the content is clear of its height too: a keyboard coming
// up takes those first, so imePadding inside leaves no gap above the keyboard). Hidden: nothing.
WindowInsets suiteInsets(NavigationSuiteType type, const NavigationSuiteScaffoldState& state) {
    if (state.currentValue() == NavigationSuiteScaffoldValue::Hidden && !state.isAnimating()) return {};
    WindowInsets bars = detail::systemBarsForVisualComponents();
    switch (type) {
    case NavigationSuiteType::NavigationBar: return bars.only(WindowInsetsSides::Bottom).add({0, 0, 0, 80});
    case NavigationSuiteType::ShortNavigationBarCompact:
    case NavigationSuiteType::ShortNavigationBarMedium: return bars.only(WindowInsetsSides::Bottom).add({0, 0, 0, 64});
    case NavigationSuiteType::None: return {};
    default: return bars.only(WindowInsetsSides::Start);
    }
}

void suiteScaffold(NavigationSuiteType type, const Modifier& modifier, const NavigationSuiteScaffoldOptions& options, const Content& suite,
                   const Content& content) {
    const ColorScheme& cs = colorScheme();
    NavigationSuiteScaffoldState state = options.state ? *options.state : rememberNavigationSuiteScaffoldState();
    WindowInsets taken = suiteInsets(type, state);
    Box(modifier.background(options.containerColor.value_or(cs.background)), [&] {
        CompositionLocalProvider({LocalContentColor.provides(options.contentColor.value_or(cs.onBackground))}, [&] {
            NavigationSuiteScaffoldLayout(suite, type, [&] { Box(Modifier().fillMaxSize().consumeWindowInsets(taken), content); }, state,
                                          options.primaryActionContent, options.primaryActionContentHorizontalAlignment);
        });
    });
}

} // namespace

void NavigationSuiteScaffold(const std::function<void(NavigationSuiteScope&)>& navigationSuiteItems, const Modifier& modifier,
                             const NavigationSuiteScaffoldOptions& options, const Content& content, CallSite site) {
    ComposableScope scope(site);
    NavigationSuiteScope nav;
    navigationSuiteItems(nav);
    NavigationSuiteType type = options.layoutType.value_or(NavigationSuiteScaffoldDefaults::calculateFromAdaptiveInfo(currentWindowAdaptiveInfo()));
    auto suite = [&] {
        switch (type) {
        case NavigationSuiteType::NavigationBar:
            NavigationBar(Modifier(), [&] {
                for (const auto& item : nav.items()) NavigationBarItem(item.selected, item.onClick, item.icon, item.label);
            });
            break;
        case NavigationSuiteType::NavigationRail:
            NavigationRail(Modifier(), options.railHeader ? options.railHeader : options.primaryActionContent, [&] {
                for (const auto& item : nav.items()) {
                    ImageVector icon = item.icon;
                    NavigationRailItem(item.selected, item.onClick, [icon] { Icon(icon, ""); }, [label = item.label] { Text(label); }, Modifier(),
                                       {.enabled = item.enabled});
                }
            });
            break;
        case NavigationSuiteType::NavigationDrawer:
            PermanentDrawerSheet(Modifier(), [&] {
                if (options.drawerHeader) options.drawerHeader();
                for (const auto& item : nav.items()) {
                    ImageVector icon = item.icon;
                    std::string label = item.label;
                    NavigationDrawerItem([label] { Text(label); }, item.selected, item.onClick, [icon, label] { Icon(icon, label); }, item.badge);
                }
            });
            break;
        case NavigationSuiteType::None: break;
        default:  // M3 Expressive's, asked for: its items
            NavigationSuite(type, Modifier(), [&] {
                for (const auto& item : nav.items()) {
                    ImageVector icon = item.icon;
                    std::string label = item.label;
                    NavigationSuiteItem(item.selected, item.onClick, [icon] { Icon(icon, ""); }, [label] { Text(label); }, Modifier(),
                                        {.navigationSuiteType = type, .enabled = item.enabled, .badge = item.badge});
                }
            }, options.primaryActionContent, options.navigationItemVerticalArrangement);
            break;
        }
    };
    suiteScaffold(type, modifier, options, suite, content);
}

void NavigationSuiteScaffold(const Content& navigationItems, const Modifier& modifier, const NavigationSuiteScaffoldOptions& options,
                             const Content& content, CallSite site) {
    ComposableScope scope(site);
    NavigationSuiteType type = options.layoutType.value_or(NavigationSuiteScaffoldDefaults::navigationSuiteType(currentWindowAdaptiveInfo()));
    suiteScaffold(type, modifier, options,
                  [&] { NavigationSuite(type, Modifier(), navigationItems, options.primaryActionContent, options.navigationItemVerticalArrangement); },
                  content);
}

// ---- Pane expansion ------------------------------------------------------------------------

float PaneExpansionAnchor::widthIn(float room) const {
    float w = proportion >= 0 ? proportion * room : offset >= 0 ? offset : room + offset;
    return std::clamp(w, 0.f, std::max(0.f, room));
}

struct PaneExpansionState::Data {
    std::vector<PaneExpansionAnchor> anchors;
    MutableState<bool> specified{false};
    Animatable<float> width{0.f};  // read while laying out
    // From the last layout: the room the panes share, the first one's width, which way they go.
    float room = 0, laidOut = 0;
    bool rtl = false;
    std::optional<CoroutineScope> scope;  // a remembered state's: settling animates
    DraggableState drag{{}};
};

namespace {

void dragBy(PaneExpansionState::Data& d, float delta) {
    float from = d.specified.peek() ? std::clamp(d.width.peek(), 0.f, d.room) : d.laidOut;
    d.width.snapTo(std::clamp(from + (d.rtl ? -delta : delta), 0.f, std::max(0.f, d.room)));
    if (!d.specified.peek()) d.specified.set(true);
}

// To the nearest anchor, or the next one the way it was flung.
void settle(const std::shared_ptr<PaneExpansionState::Data>& d, float velocity) {
    if (d->anchors.empty() || !d->specified.peek()) return;
    std::vector<float> widths;
    for (const PaneExpansionAnchor& a : d->anchors) widths.push_back(a.widthIn(d->room));
    std::sort(widths.begin(), widths.end());
    float at = std::clamp(d->width.peek(), 0.f, d->room), v = d->rtl ? -velocity : velocity;
    float target = widths.front();
    if (std::abs(v) > 400) {
        auto next = v > 0 ? std::find_if(widths.begin(), widths.end(), [at](float w) { return w > at + 0.5f; })
                          : std::find_if(widths.rbegin(), widths.rend(), [at](float w) { return w < at - 0.5f; }).base();
        target = v > 0 ? (next != widths.end() ? *next : widths.back()) : (next != widths.begin() ? *(next - 1) : widths.front());
    } else {
        for (float w : widths)
            if (std::abs(w - at) < std::abs(target - at)) target = w;
    }
    if (!d->scope) return d->width.snapTo(target);
    d->scope->launch([d, target]() -> Task {
        co_await d->width.animateTo(target, spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow));
    });
}

} // namespace

PaneExpansionState::PaneExpansionState(std::vector<PaneExpansionAnchor> anchors) : data_(std::make_shared<Data>()) {
    data_->anchors = std::move(anchors);
    std::weak_ptr<Data> weak = data_;
    data_->drag = DraggableState({.onDelta = [weak](float delta) { if (auto d = weak.lock()) dragBy(*d, delta); },
                                  .onDragStoppedWithVelocity = [weak](float velocity) { if (auto d = weak.lock()) settle(d, velocity); }});
}

bool PaneExpansionState::isUnspecified() const { return !data_->specified.get(); }
float PaneExpansionState::firstPaneWidth() const { return data_->width.value(); }
const std::vector<PaneExpansionAnchor>& PaneExpansionState::anchors() const { return data_->anchors; }

void PaneExpansionState::setFirstPaneWidth(float dp) const {
    data_->width.snapTo(std::max(0.f, dp));
    if (!data_->specified.peek()) data_->specified.set(true);
}

void PaneExpansionState::setFirstPaneProportion(float proportion) const { setFirstPaneWidth(proportion * data_->room); }

void PaneExpansionState::clear() const {
    if (data_->specified.peek()) data_->specified.set(false);
}

SavedValue Saver<PaneExpansionState>::save(const PaneExpansionState& s) {
    return SavedValue::List{s.data_->specified.peek(), s.data_->width.peek()};
}
std::optional<PaneExpansionState> Saver<PaneExpansionState>::restore(const SavedValue& v) {
    const SavedValue::List* l = v.list();
    if (!l || l->size() != 2) return std::nullopt;
    auto specified = (*l)[0].boolean();
    auto width = (*l)[1].number();
    if (!specified || !width) return std::nullopt;
    PaneExpansionState s;
    if (*specified) {  // the divider where the user left it
        s.data_->specified.set(true);
        s.data_->width.snapTo(float(*width));
    }
    return s;
}

PaneExpansionState rememberPaneExpansionState(std::vector<PaneExpansionAnchor> anchors, CallSite site) {
    ComposableScope group(site);
    auto scope = rememberCoroutineScope();
    auto& state = rememberSaveable([] { return PaneExpansionState(); });
    state.data_->anchors = std::move(anchors);
    state.data_->scope = scope;
    return state;
}

Modifier paneExpansionDraggable(const PaneExpansionState& state, std::optional<MutableInteractionSource> interactionSource) {
    return Modifier().sizeIn(48, 48).draggable(state.data_->drag, Orientation::Horizontal, true, std::move(interactionSource));
}

void detail::twoPanes(const Content& first, const Content& second, bool firstFixed, float fixedWidth, float spacerSize,
                      const std::optional<PaneExpansionState>& expansion, const std::function<void(PaneExpansionState)>& handle,
                      const Modifier& modifier) {
    std::shared_ptr<PaneExpansionState::Data> d = expansion ? expansion->data_ : nullptr;
    Layout(
        modifier.fillMaxSize(),
        [&] {
            Box(Modifier(), first);
            Box(Modifier(), Alignment::Center, [&] {
                if (handle && expansion) handle(*expansion);
            });
            Box(Modifier(), second);
        },
        [d, firstFixed, fixedWidth, spacerSize](const std::vector<Measurable>& children, const Constraints& c) {
            float w = c.hasBoundedWidth() ? c.maxWidth : c.minWidth, h = c.hasBoundedHeight() ? c.maxHeight : c.minHeight;
            float spacer = std::min(spacerSize, w), room = std::max(0.f, w - spacer), fixed = std::min(fixedWidth, room);
            float firstWidth = firstFixed ? fixed : room - fixed;
            if (d) {
                if (d->specified.get()) firstWidth = std::clamp(d->width.value(), 0.f, room);
                if (!detail::isProbeMeasure()) {
                    d->room = room, d->laidOut = firstWidth;
                    d->rtl = children[0].node->rtl;
                }
            }
            children[0].measure(Constraints::fixed(firstWidth, h)).place(0, 0);
            children[1].measure(Constraints::fixed(spacer, h)).place(firstWidth, 0);
            children[2].measure(Constraints::fixed(room - firstWidth, h)).place(firstWidth + spacer, 0);
            return Size{w, h};
        });
}

} // namespace kompoz
