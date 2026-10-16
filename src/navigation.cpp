// Material 3 navigation drawer and navigation rail.

#include <kompoz/animation.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/gestures.hpp>
#include <kompoz/host.hpp>
#include <kompoz/material.hpp>
#include <kompoz/state.hpp>

namespace kompoz {

struct DrawerState::Data {
    MutableState<bool> open{false};     // where it is or is going to
    MutableState<bool> visible{false};  // scrim and drawer shown (open, or moving)
    Animatable<float> fraction{0.f};    // 0 closed .. 1 open
    MutableState<float> width{360.f};   // measured drawer width (its offset reads it as it lays out)
    // How a back gesture stretches its sheet (M3's predictive back), in dp: wider (from the
    // drawer's own edge) or narrower (from the other), and lower.
    Animatable<float> backX{0.f};
    MutableState<float> backY{0.f};
    MutableState<bool> backFromEdge{true};
    void clearBack() {
        backX.snapTo(0);
        backY.set(0);
        backFromEdge.set(true);
    }
};

DrawerState::DrawerState(DrawerValue initial) : data_(std::make_shared<Data>()) {
    bool open = initial == DrawerValue::Open;
    data_->open.set(open);
    data_->visible.set(open);
    data_->fraction.snapTo(open ? 1.f : 0.f);
}

bool DrawerState::isOpen() const { return data_->open.get(); }
float DrawerState::openFraction() const { return data_->fraction.value(); }

void DrawerState::snapTo(DrawerValue value) const {
    bool open = value == DrawerValue::Open;
    data_->open.set(open);
    data_->visible.set(open);
    data_->fraction.snapTo(open ? 1.f : 0.f);
}

namespace {

AnimationSpec drawerSpec() { return spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow); }

// The coroutines take the shared data as a parameter (copied into the frame): a Task starts
// when awaited, possibly after this handle is gone.
Task openDrawer(std::shared_ptr<DrawerState::Data> d) {
    d->open.set(true);
    d->visible.set(true);
    co_await d->fraction.animateTo(1.f, drawerSpec());
}

Task closeDrawer(std::shared_ptr<DrawerState::Data> d) {
    d->open.set(false);
    co_await d->fraction.animateTo(0.f, drawerSpec());
    if (d->open.peek()) co_return;
    d->visible.set(false);
    d->clearBack();
}

} // namespace

Task DrawerState::open() const { return openDrawer(data_); }
Task DrawerState::close() const { return closeDrawer(data_); }

SavedValue Saver<DrawerState>::save(const DrawerState& s) { return s.data_->open.peek(); }
std::optional<DrawerState> Saver<DrawerState>::restore(const SavedValue& v) {
    std::optional<bool> open = v.boolean();
    if (!open) return std::nullopt;
    return DrawerState(*open ? DrawerValue::Open : DrawerValue::Closed);
}

DrawerState rememberDrawerState(DrawerValue initial, CallSite site) {
    return rememberSaveable([initial] { return DrawerState(initial); }, site);
}

namespace {

// Dragging moves the drawer with the finger; letting go settles it open or closed. Right to
// left the drawer is at the right: the finger opens it going left.
std::function<Task(PointerInputScope&)> drawerDrag(DrawerState state, CoroutineScope coroutines, bool rtl) {
    auto d = state.data_;
    return [d, state, coroutines, rtl](PointerInputScope& s) -> Task {
        co_await detectHorizontalDragGestures(
            s, {.onDragStart = [d](Offset) {
                    d->fraction.stop();
                    d->visible.set(true);
                },
                .onDrag = [d, rtl](PointerInputChange& change, float dx) {
                    change.consume();
                    d->fraction.snapTo(std::clamp(d->fraction.value() + (rtl ? -dx : dx) / std::max(1.f, d->width.peek()), 0.f, 1.f));
                },
                .onDragEnd = [d, state, coroutines] {
                    bool open = d->fraction.value() > 0.5f;
                    coroutines.launch([state, open]() -> Task { co_await (open ? state.open() : state.close()); });
                },
                .onDragCancel = [d, state, coroutines] {
                    bool open = d->fraction.value() > 0.5f;
                    coroutines.launch([state, open]() -> Task { co_await (open ? state.open() : state.close()); });
                }});
    };
}

// The drawer its sheet is in (Modal and DismissibleNavigationDrawer give it to their drawer
// content): the sheet follows its back gesture.
const CompositionLocal<std::shared_ptr<DrawerState::Data>> LocalDrawer{std::shared_ptr<DrawerState::Data>{}};

// Back closes an open drawer; its gesture stretches the sheet as M3's: wider towards the edge
// it came from if that is the drawer's (up to 12 dp), otherwise narrower (24 dp), and lower
// (48 dp). Done, a sheet stretched wider goes back as the drawer closes.
void drawerBack(DrawerState state, CoroutineScope coroutines, bool enabled, bool rtl) {
    static constexpr Easing kPredictiveBackEasing{0.1f, 0.1f, 0.f, 1.f};
    auto d = state.data_;
    auto follow = [d, rtl](const BackEventCompat& e) {
        float p = kPredictiveBackEasing(e.progress);
        bool fromEdge = (e.swipeEdge == BackEventCompat::SwipeEdge::Left) != rtl;
        d->backFromEdge.set(fromEdge);
        d->backX.snapTo(p * (fromEdge ? 12.f : 24.f));
        d->backY.set(p * 48);
    };
    detail::backHandler({
        .onBack = [d, state, coroutines] {
            if (d->backFromEdge.peek()) coroutines.launch([d]() -> Task { co_await d->backX.animateTo(0.f); });
            coroutines.launch([state]() -> Task { co_await state.close(); });
        },
        .onStarted = follow,
        .onProgressed = follow,
        .onCancelled = [d] { d->clearBack(); },
        .enabled = enabled,
    });
}

// A sheet's container and content as its drawer's back gesture stretches it: the container from
// the middle of its outer edge (`edge`: where the window's edge is across it), the content
// scaled back across to keep its shape, from the top of its inner side. Nothing without a drawer.
struct Stretch {
    Modifier container, content;
};
Stretch backStretch(const std::shared_ptr<DrawerState::Data>& d, const std::shared_ptr<Size>& size, float edge, bool rtl) {
    if (!d) return {};
    auto scales = [d, size] {
        float sx = size->width > 0 ? 1 + d->backX.value() * (d->backFromEdge.get() ? 1 : -1) / size->width : 1.f;
        float sy = size->height > 0 ? 1 - d->backY.get() / size->height : 1.f;
        return std::pair{sx, sy};
    };
    return {Modifier().onSizeChanged([size](Size s) { *size = s; }).graphicsLayer([scales, edge, rtl](GraphicsLayerScope& layer) {
                auto [sx, sy] = scales();
                layer.scaleX = sx, layer.scaleY = sy;
                layer.transformOrigin = {rtl ? 1 - edge : edge, 0.5f};
            }),
            Modifier().graphicsLayer([scales, rtl](GraphicsLayerScope& layer) {
                auto [sx, sy] = scales();
                layer.scaleX = sx != 0 ? sy / sx : 1.f;
                layer.transformOrigin = {rtl ? 0.f : 1.f, 0};
            })};
}

// A standard (not modal) sheet: on the surface, square, 240..360 dp wide. It sits in the
// page, so it pads by what is left of the system bars there (none, inside a padded screen).
void standardSheet(const std::shared_ptr<DrawerState::Data>& d, const Modifier& modifier, const Content& content) {
    const ColorScheme& cs = colorScheme();
    auto& size = remember([] { return std::make_shared<Size>(); });
    Stretch stretch = backStretch(d, size, 0, LocalLayoutDirection.current() == LayoutDirection::Rtl);
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurfaceVariant)}, [&] {
        Column(modifier.fillMaxHeight().widthIn(240, 360).then(stretch.container).background(cs.surface), [&] {
            Column(Modifier()
                       .fillMaxSize()
                       .then(stretch.content)
                       .windowInsetsPadding(detail::systemBarsForVisualComponents().only(WindowInsetsSides::Vertical | WindowInsetsSides::Start))
                       .padding(12),
                   content);
        });
    });
}

void modalSheet(const std::shared_ptr<DrawerState::Data>& d, const Modifier& modifier, const Content& content) {
    const ColorScheme& cs = colorScheme();
    PaddingValues insets = detail::systemBarsForVisualComponents().only(WindowInsetsSides::Vertical | WindowInsetsSides::Start).asPaddingValues();
    float width = std::min(360.f, std::max(0.f, LocalWindowSize.current().width - 56));
    auto& size = remember([] { return std::make_shared<Size>(); });
    Stretch stretch = backStretch(d, size, 16 / (width + 16), LocalLayoutDirection.current() == LayoutDirection::Rtl);
    // Reaches 16 dp past the start edge so only the end corners show rounded.
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurfaceVariant)}, [&] {
        Column(modifier.fillMaxHeight()
                   .width(width + 16)
                   .offset(-16, 0)
                   .then(stretch.container)
                   .background(cs.surfaceContainerLow, 16)
                   .pointerInput(0, [](PointerInputScope& s) -> Task {  // taps on the sheet stay on it
                       while (true) {
                           PointerEvent e = co_await s.awaitPointerEvent();
                           if (e.type == PointerEventType::Press) e.change.consume();
                       }
                   }),
               [&] {
                   Column(Modifier().fillMaxSize().then(stretch.content).padding(16 + 12 + insets.start, insets.top + 12, 12, insets.bottom + 12),
                          content);
               });
    });
}

} // namespace

void ModalNavigationDrawer(const Content& drawerContent, DrawerState state, const Content& content,
                           DrawerOptions options, CallSite site) {
    ComposableScope scope(site);
    auto d = state.data_;
    auto coroutines = rememberCoroutineScope();
    bool visible = d->visible.get();
    bool open = d->open.get();
    bool rtl = LocalLayoutDirection.current() == LayoutDirection::Rtl;
    auto drag = drawerDrag(state, coroutines, rtl);
    Modifier root = Modifier().fillMaxSize();
    if (options.gesturesEnabled) root = root.pointerInput(rtl, drag);
    Box(root, [&] {
        content();
        if (!visible) return;  // closed: nothing above the content
        Box(Modifier()
                .fillMaxSize()
                .drawBehind([d, scrim = colorScheme().scrim](DrawScope& draw) { draw.drawRect(scrim.copy(scrim.a * 0.32f * d->fraction.value())); })
                .pointerInput(0, [state, coroutines](PointerInputScope& s) -> Task {
                    co_await detectTapGestures(s, {.onTap = [state, coroutines](Offset) {
                                                   coroutines.launch([state]() -> Task { co_await state.close(); });
                                               }});
                }));
        Box(Modifier()
                .fillMaxHeight()
                .offset([d] { return Offset{-(d->width.get() + 16) * (1 - d->fraction.value()), 0}; })
                .onSizeChanged([d](Size s) { d->width.set(s.width); }),
            [&] { CompositionLocalProvider({LocalDrawer.provides(d)}, drawerContent); });
    });
    drawerBack(state, coroutines, visible && open, rtl);
}

void ModalDrawerSheet(const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    modalSheet(LocalDrawer.current(), modifier, content);
}

void ModalDrawerSheet(DrawerState drawerState, const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    modalSheet(drawerState.data_, modifier, content);
}

void DismissibleNavigationDrawer(const Content& drawerContent, DrawerState state, const Content& content,
                                 DrawerOptions options, CallSite site) {
    ComposableScope scope(site);
    auto d = state.data_;
    auto coroutines = rememberCoroutineScope();
    bool visible = d->visible.get(), open = d->open.get();
    bool rtl = LocalLayoutDirection.current() == LayoutDirection::Rtl;
    Modifier root = Modifier().fillMaxSize();
    if (options.gesturesEnabled) root = root.pointerInput(rtl, drawerDrag(state, coroutines, rtl));
    // The drawer is measured first; the content moves over by as much of it as shows.
    Layout(
        root,
        [&] {
            if (visible) Box(Modifier().fillMaxHeight(), [&] { CompositionLocalProvider({LocalDrawer.provides(d)}, drawerContent); });
            Box(Modifier().fillMaxSize(), content);
        },
        [d, visible](const std::vector<Measurable>& children, const Constraints& c) {
            float f = d->fraction.value(), shown = 0;
            if (visible && children.size() == 2) {
                Placeable drawer = children[0].measure({0, c.maxWidth, c.maxHeight, c.maxHeight});
                d->width.set(drawer.width);
                shown = drawer.width * f;
                drawer.place(shown - drawer.width, 0);
            }
            Placeable page = children.back().measure(Constraints::fixed(c.maxWidth, c.maxHeight));
            page.place(shown, 0);
            return Size{c.maxWidth, c.maxHeight};
        });
    drawerBack(state, coroutines, visible && open, rtl);
}

void DismissibleDrawerSheet(const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    standardSheet(LocalDrawer.current(), modifier, content);
}

void DismissibleDrawerSheet(DrawerState drawerState, const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    standardSheet(drawerState.data_, modifier, content);
}

void PermanentNavigationDrawer(const Content& drawerContent, const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    Row(modifier.fillMaxSize(), [&] {
        drawerContent();
        Box(Modifier().weight(1).fillMaxHeight(), content);
    });
}

void PermanentDrawerSheet(const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    standardSheet(nullptr, modifier, content);
}

void NavigationDrawerItem(const Content& label, bool selected, std::function<void()> onClick, const Content& icon,
                          const Content& badge, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    // Compose 1.4's: at least 56 dp, a pill in secondaryContainer when selected (at once), the
    // label in the text style around it; read as a tab.
    Color container = selected ? cs.secondaryContainer : colors::Transparent;
    Color content = selected ? cs.onSecondaryContainer : cs.onSurfaceVariant;
    CompositionLocalProvider({LocalContentColor.provides(content)}, [&] {
        Row(modifier.fillMaxWidth().heightIn(56).background(container, 28).selectable(selected, std::move(onClick), SemanticsRole::Tab)
                .padding(16, 0, 24, 0),
            {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically}, [&] {
                if (icon) icon();
                Box(Modifier().weight(1), label);
                if (badge) badge();
            });
    });
}

void NavigationRail(const Modifier& modifier, const Content& header, const Content& content, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    PaddingValues insets = detail::systemBarsForVisualComponents().only(WindowInsetsSides::Vertical | WindowInsetsSides::Start).asPaddingValues();
    // Compose 1.4's: at least 80 dp wide, 4 dp above, below and between the items, 8 under the header.
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurface)}, [&] {
        Column(modifier.fillMaxHeight().background(cs.surface).padding(insets.start, insets.top, 0, insets.bottom).widthIn(80)
                   .padding(0, 4),
               {.verticalArrangement = Arrangement::spacedBy(4), .horizontalAlignment = Alignment::CenterHorizontally},
               [&] {
                   if (header) {
                       header();
                       Spacer(Modifier().height(8));
                   }
                   content();
               });
    });
}

void NavigationRailItem(bool selected, std::function<void()> onClick, const ImageVector& icon, std::string label,
                        CallSite site) {
    NavigationRailItem(selected, std::move(onClick), [icon] { Icon(icon, ""); }, [label] { Text(label); }, Modifier(), {}, site);  // (the label says it)
}

void NavigationRailItem(bool selected, std::function<void()> onClick, const Content& icon, const Content& label, const Modifier& modifier,
                        const NavigationRailItemOptions& options, CallSite site) {
    ComposableScope scope(site);
    // Compose 1.4's: 80 × 56, the 56 dp wide indicator at the top.
    detail::indicatorNavigationItem(selected, std::move(onClick), icon, label, modifier.widthIn(80).height(56), 56, 0, options.enabled,
                                    options.interactionSource);
}

} // namespace kompoz
