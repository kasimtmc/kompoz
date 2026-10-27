// M3 Expressive's navigation: the short navigation bar and the wide navigation rail.
#include <kompoz/adaptive.hpp>
#include <kompoz/animation.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/gestures.hpp>
#include <kompoz/material.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>

namespace kompoz {

namespace {

constexpr float kBarHeight = 64;

// The bar's arrangement, for its items to lay themselves out by.
const CompositionLocal<ShortNavigationBarArrangement> LocalShortNavigationBarArrangement{ShortNavigationBarArrangement::EqualWeight};

// Grows from the middle as `shown` goes from 0 to 1 (read as it is drawn).
Modifier indicator(const Modifier& modifier, Color color, const AnimatedState<float>& shown) {
    return modifier.drawBehind([color, shown](DrawScope& d) {
        Size s = d.size();
        float w = s.width * std::clamp(shown.value(), 0.f, 1.f);
        if (w > 0) d.drawRoundRect(color, {(s.width - w) / 2, 0}, Size{w, s.height}, CornerRadius(s.height / 2));
    });
}

} // namespace

void ShortNavigationBar(const Modifier& modifier, const Content& content, const ShortNavigationBarOptions& options, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    ShortNavigationBarArrangement arrangement = options.arrangement.value_or(
        currentWindowAdaptiveInfo().windowSizeClass.isWidthAtLeastBreakpoint(600) ? ShortNavigationBarArrangement::Centered
                                                                                   : ShortNavigationBarArrangement::EqualWeight);
    PaddingValues insets =
        options.windowInsets.value_or(detail::systemBarsForVisualComponents().only(WindowInsetsSides::Horizontal | WindowInsetsSides::Bottom)).asPaddingValues();
    Modifier m = modifier.fillMaxWidth().background(options.containerColor.value_or(cs.surfaceContainer))
                     .padding(insets.start, 0, insets.end, insets.bottom);
    CompositionLocalProvider({LocalShortNavigationBarArrangement.provides(arrangement)}, [&] {
        if (arrangement == ShortNavigationBarArrangement::EqualWeight) {
            Row(m.height(kBarHeight), {.verticalAlignment = Alignment::CenterVertically}, content);
            return;
        }
        // Centered: as wide as the widest, together in the middle.
        Layout(m.height(kBarHeight), content, [](const std::vector<Measurable>& items, const Constraints& c) {
            float width = c.hasBoundedWidth() ? c.maxWidth : 0, height = c.hasBoundedHeight() ? c.maxHeight : kBarHeight;
            float widest = 0;
            {
                detail::ProbeMeasure probe;  // their own widths; measured at the widest below
                for (const Measurable& item : items) widest = std::max(widest, item.measure({0, kInfinity, height, height}).width);
            }
            if (!items.empty() && width > 0) widest = std::min(widest, width / float(items.size()));
            float x = std::max(0.f, (width - widest * float(items.size())) / 2);
            for (const Measurable& item : items) {
                item.measure({widest, widest, height, height}).place(x, 0);
                x += widest;
            }
            return Size{width, height};
        });
    });
}

void ShortNavigationBarItem(bool selected, std::function<void()> onClick, const Content& icon, const Content& label, const Modifier& modifier,
                            const ShortNavigationBarItemOptions& options, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    ShortNavigationBarArrangement arrangement = LocalShortNavigationBarArrangement.current();
    bool start = options.iconPosition.value_or(arrangement == ShortNavigationBarArrangement::Centered ? NavigationItemIconPosition::Start
                                                                                                      : NavigationItemIconPosition::Top) ==
                 NavigationItemIconPosition::Start;
    // The animations read where they are used: the indicator as it is drawn, the colors by the
    // icon and the label (not this, which is its caller's scope).
    auto shown = animateFloatAsState(selected ? 1.f : 0.f, motionScheme().defaultSpatialSpec);
    auto iconColor = animateColorAsState(selected ? cs.onSecondaryContainer : cs.onSurfaceVariant, 150);
    auto labelColor = animateColorAsState(selected ? cs.secondary : cs.onSurfaceVariant, 150);
    auto tinted = [&](const AnimatedState<Color>& color) {
        return options.enabled ? LocalContentColor.provides(color) : LocalContentColor.provides(cs.onSurface.copy(0.38f));
    };
    MutableInteractionSource source = options.interactionSource ? *options.interactionSource : remember([] { return MutableInteractionSource(); });
    Modifier item = arrangement == ShortNavigationBarArrangement::EqualWeight ? modifier.weight(1) : modifier;
    item = item.fillMaxHeight().selectable(selected, std::move(onClick), SemanticsRole::Tab,
                                           {.enabled = options.enabled, .interactionSource = source, .indication = false});
    // The ripple keeps to the indicator's shape, from where the finger went down on the item.
    auto shift = remember([] { return std::make_shared<Offset>(); });
    MutableInteractionSource mapped = detail::mappedInteractionSource(source, shift);
    auto pill = [&](const Modifier& m) {
        return indicator(m, cs.secondaryContainer, shown)
            .onGloballyPositioned([shift](const LayoutCoordinates& c) { *shift = c.positionInParent(); })
            .clip(CircleShape)
            .indication(mapped, ripple());
    };
    auto text = [&] { CompositionLocalProvider({tinted(labelColor), LocalTextStyle.provides(typography().labelMedium)}, label); };
    auto picture = [&] { CompositionLocalProvider({tinted(iconColor)}, icon); };
    if (start) {
        Box(item, Alignment::Center, [&] {
            Row(pill(Modifier().height(40)).padding(16, 0), {.horizontalArrangement = Arrangement::spacedBy(4), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    picture();
                    text();
                });
        });
    } else {
        Column(item, {.verticalArrangement = Arrangement::spacedBy(4, Arrangement::Kind::Center), .horizontalAlignment = Alignment::CenterHorizontally},
               [&] {
                   Box(pill(Modifier().size(56, 32)), Alignment::Center, picture);
                   text();
               });
    }
}

struct WideNavigationRailState::Data {
    MutableState<bool> expanded{false};
};

WideNavigationRailState::WideNavigationRailState(bool expanded) : data_(std::make_shared<Data>()) { data_->expanded.set(expanded); }
bool WideNavigationRailState::isExpanded() const { return data_->expanded.get(); }
void WideNavigationRailState::expand() const { data_->expanded.set(true); }
void WideNavigationRailState::collapse() const { data_->expanded.set(false); }
void WideNavigationRailState::toggle() const { data_->expanded.set(!data_->expanded.peek()); }

SavedValue Saver<WideNavigationRailState>::save(const WideNavigationRailState& s) { return s.data_->expanded.peek(); }
std::optional<WideNavigationRailState> Saver<WideNavigationRailState>::restore(const SavedValue& v) {
    std::optional<bool> expanded = v.boolean();
    if (!expanded) return std::nullopt;
    return WideNavigationRailState(*expanded);
}

WideNavigationRailState rememberWideNavigationRailState(bool initiallyExpanded, CallSite site) {
    return rememberSaveable([initiallyExpanded] { return WideNavigationRailState(initiallyExpanded); }, site);
}

namespace {

const CompositionLocal<bool> LocalWideNavigationRailExpanded{false};

} // namespace

namespace {

PaddingValues railInsets(const std::optional<WindowInsets>& given) {
    return given.value_or(detail::systemBarsForVisualComponents().only(WindowInsetsSides::Vertical | WindowInsetsSides::Start))
        .asPaddingValues();
}

// The rail itself: its header and items, laid out collapsed or expanded, `width` wide.
void railColumn(bool expanded, float width, Color container, Shape shape, const Modifier& modifier, const Content& header,
                const Content& content, PaddingValues insets, Arrangement::Kind arrangement = Arrangement::Kind::Start) {
    const ColorScheme& cs = colorScheme();
    float spacing = expanded ? 0.f : 4.f;
    CompositionLocalProvider({LocalWideNavigationRailExpanded.provides(expanded), LocalContentColor.provides(cs.onSurfaceVariant)}, [&] {
        Column(modifier.fillMaxHeight()
                   .background(container, shape)
                   .padding(insets.start, insets.top, 0, insets.bottom)
                   .width(width + insets.start)
                   .clipToBounds()
                   .padding(0, 44, 0, 12),
               {.verticalArrangement = Arrangement::spacedBy(spacing),
                .horizontalAlignment = expanded ? Alignment::Start : Alignment::CenterHorizontally},
               [&] {
                   if (header) Box(Modifier().padding(expanded ? 20.f : 0.f, 0, 0, 40 - spacing), header);  // 40 dp above the items
                   if (arrangement != Arrangement::Kind::Start) Spacer(Modifier().weight(1));  // the items in the middle or at the bottom
                   content();
                   if (arrangement == Arrangement::Kind::Center) Spacer(Modifier().weight(1));
               });
    });
}

} // namespace

void WideNavigationRail(const Modifier& modifier, const Content& content, const WideNavigationRailOptions& options, CallSite site) {
    ComposableScope scope(site);
    WideNavigationRailState state = options.state ? *options.state : rememberWideNavigationRailState();
    bool expanded = state.isExpanded();
    float p = animateFloatAsState(expanded ? 1.f : 0.f, motionScheme().defaultSpatialSpec);
    float width = options.collapsedWidth + (options.expandedWidth - options.collapsedWidth) * p;
    railColumn(expanded, width, options.containerColor.value_or(colorScheme().surface), RectangleShape, modifier, options.header, content,
               railInsets(options.windowInsets), options.arrangement);
}

void ModalWideNavigationRail(const Modifier& modifier, const Content& content, const ModalWideNavigationRailOptions& options, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    WideNavigationRailState state = options.state ? *options.state : rememberWideNavigationRailState();
    bool expanded = state.isExpanded();
    float p = animateFloatAsState(expanded ? 1.f : 0.f, spring(1, 700));
    PaddingValues insets = railInsets(options.windowInsets);
    if (!options.hideOnCollapse) railColumn(false, options.collapsedWidth, cs.surface, RectangleShape, modifier, options.header, content, insets);
    if (!expanded && p <= 0.001f) return;
    // Expanded: over everything, sliding in from the start above a scrim; a tap on the scrim or
    // Back collapses it.
    Popup({.fullScreen = true, .onDismissRequest = [state] { state.collapse(); }, .dismissOnOutsideClick = false}, [&] {
        Box(Modifier()
                .fillMaxSize()
                .background(colorScheme().scrim.copy(colorScheme().scrim.alpha() * 0.32f * std::clamp(p, 0.f, 1.f)))
                .pointerInput(0, [state](PointerInputScope& s) -> Task {
                    co_await detectTapGestures(s, {.onTap = [state](Offset) { state.collapse(); }});
                }),
            Alignment::TopStart, [&] {
                float shift = (1 - p) * (options.expandedWidth + insets.start);
                Box(Modifier()
                        .fillMaxHeight()
                        .offset([shift] { return Offset{-shift, 0}; })
                        .pointerInput(0, [](PointerInputScope& s) -> Task {  // taps on the rail stay on it
                            while (true) {
                                PointerEvent e = co_await s.awaitPointerEvent();
                                if (e.type == PointerEventType::Press) e.change.consume();
                            }
                        }),
                    [&] {
                        railColumn(true, options.expandedWidth, cs.surfaceContainer, RoundedCornerShape(0, 16, 16, 0), Modifier(), options.header,
                                   content, insets);
                    });
            });
    });
}

void WideNavigationRailItem(bool selected, std::function<void()> onClick, const Content& icon, const Content& label, const Modifier& modifier,
                            const WideNavigationRailItemOptions& options, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    bool expanded = options.railExpanded.value_or(LocalWideNavigationRailExpanded.current());
    auto shown = animateFloatAsState(selected ? 1.f : 0.f, motionScheme().defaultSpatialSpec);  // (as ShortNavigationBarItem's)
    auto iconColor = animateColorAsState(selected ? cs.onSecondaryContainer : cs.onSurfaceVariant, 150);
    auto labelColor = animateColorAsState(selected ? cs.secondary : cs.onSurfaceVariant, 150);
    auto tinted = [&](const AnimatedState<Color>& color) {
        return options.enabled ? LocalContentColor.provides(color) : LocalContentColor.provides(cs.onSurface.copy(0.38f));
    };
    MutableInteractionSource source = options.interactionSource ? *options.interactionSource : remember([] { return MutableInteractionSource(); });
    Modifier item = modifier.fillMaxWidth().selectable(selected, std::move(onClick), SemanticsRole::Tab,
                                                       {.enabled = options.enabled, .interactionSource = source, .indication = false});
    auto pill = [&](const Modifier& m) { return indicator(m, cs.secondaryContainer, shown).clip(CircleShape).indication(source, ripple()); };
    auto picture = [&] { CompositionLocalProvider({tinted(iconColor)}, icon); };
    if (expanded) {  // the label after the icon, the indicator round both
        Box(item.height(56).padding(20, 0, 20, 0), Alignment::CenterStart, [&] {
            Row(pill(Modifier().height(56)).padding(16, 0), {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    picture();
                    CompositionLocalProvider({tinted(labelColor), LocalTextStyle.provides(typography().labelLarge)}, label);
                });
        });
    } else {
        Column(item.height(64), {.verticalArrangement = Arrangement::spacedBy(4, Arrangement::Kind::Center), .horizontalAlignment = Alignment::CenterHorizontally},
               [&] {
                   Box(pill(Modifier().size(56, 32)), Alignment::Center, picture);
                   CompositionLocalProvider({tinted(labelColor), LocalTextStyle.provides(typography().labelMedium)}, label);
               });
    }
}

} // namespace kompoz
