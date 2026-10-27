// Material 3 dialogs, dropdown menus and the modal bottom sheet, built on Popup.

#include <kompoz/anchored_draggable.hpp>
#include <kompoz/animated.hpp>
#include <kompoz/coroutines.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/gestures.hpp>
#include <kompoz/host.hpp>
#include <kompoz/icons.hpp>
#include <kompoz/material.hpp>
#include <kompoz/nested_scroll.hpp>
#include <kompoz/state.hpp>

#include <chrono>
#include <cmath>

namespace kompoz {

namespace {

constexpr float kScrimAlpha = 0.32f;

void provide(Color color, const Font& font, const Content& content) {
    if (content) CompositionLocalProvider({LocalContentColor.provides(color), LocalTextStyle.provides(font)}, content);
}

// Presses on a dialog or sheet must not reach the scrim behind it (which would close it).
Modifier swallowPresses(const Modifier& m) {
    return m.pointerInput(0, [](PointerInputScope& scope) -> Task {
        while (true) {
            PointerEvent e = co_await scope.awaitPointerEvent();
            if (e.type == PointerEventType::Press) e.change.consume();
        }
    });
}

// Taps that nobody above consumed (the scrim around a dialog or sheet).
Modifier onTap(const Modifier& m, UpdatedState<std::function<void()>> action) {
    return m.pointerInput(0, [action](PointerInputScope& scope) -> Task {
        co_await detectTapGestures(scope, {.onTap = [action](Offset) {
                                       if (action.get()) action.get()();
                                   }});
    });
}

// A menu is as wide as its widest item (112..280 dp), and every item gets that width.
Size menuPolicy(LayoutNode& node, const Constraints& c) {
    float width = 0;
    {
        detail::ProbeMeasure probe;
        for (auto* child : node.children) width = std::max(width, child->measure({}).width);
    }
    width = std::max(std::clamp(width, std::min(112.f, c.maxWidth), std::min(280.f, c.maxWidth)), c.minWidth);  // or as told
    float y = 0;
    for (auto* child : node.children) {
        Size s = child->measure({width, width, 0, kInfinity});
        child->place(0, y);
        y += s.height;
    }
    return c.constrain({width, y});
}

} // namespace

void Dialog(std::function<void()> onDismissRequest, const Content& content, DialogProperties properties, CallSite site) {
    ComposableScope scope(site);
    auto dismiss = rememberUpdatedState(std::move(onDismissRequest));
    auto outside = rememberUpdatedState(std::function<void()>([dismiss, properties] {
        if (properties.dismissOnClickOutside && dismiss.get()) dismiss.get()();
    }));
    auto appear = remember([] { return Animatable<float>(0.f); });
    LaunchedEffect(0, [appear]() -> Task { co_await appear.animateTo(1.f, tween(150)); });
    OverlaySpec spec{.fullScreen = true, .dismissOnOutsideClick = false};
    if (properties.dismissOnBackPress)
        spec.onDismissRequest = [dismiss] {
            if (dismiss.get()) dismiss.get()();
        };
    Popup(std::move(spec), [&] {
        // The fade-in is read while drawing: no recomposition per frame.
        Box(onTap(Modifier().fillMaxSize().drawBehind([appear, scrim = colorScheme().scrim](DrawScope& d) {
                      d.drawRect(scrim.copy(scrim.alpha() * kScrimAlpha * appear.value()));
                  }),
                  outside)
                .safeDrawingPadding()
                .padding(properties.usePlatformDefaultWidth ? 48.f : 0.f, properties.usePlatformDefaultWidth ? 24.f : 0.f),
            Alignment::Center, [&] {
                Box(swallowPresses(Modifier().graphicsLayer([appear](GraphicsLayerScope& l) {
                        float v = appear.value();
                        l.alpha = v;
                        l.scaleX = l.scaleY = 0.92f + 0.08f * v;
                    })),
                    content);
            });
    });
}

namespace AlertDialogDefaults {
Color containerColor() { return colorScheme().surfaceContainerHigh; }
Color iconContentColor() { return colorScheme().secondary; }
Color titleContentColor() { return colorScheme().onSurface; }
Color textContentColor() { return colorScheme().onSurfaceVariant; }
} // namespace AlertDialogDefaults

namespace {
constexpr float kDialogMinWidth = 280, kDialogMaxWidth = 560;

// A dialog's buttons from the end, 8 dp apart, as many on a row as fit; the rows they wrap to
// stack upwards 12 dp apart, so the confirming button (the last) ends up over the dismissing one.
void dialogButtons(const Content& buttons) {
    Layout(Modifier().fillMaxWidth(), buttons, [](const std::vector<Measurable>& children, const Constraints& c) {
        constexpr float kGap = 8, kRowGap = 12;
        struct Line {
            std::vector<Placeable> items;
            float width = 0, height = 0;
        };
        std::vector<Line> lines;
        Line line;
        for (const Measurable& m : children) {
            Placeable p = m.measure({0, c.maxWidth, 0, c.maxHeight});
            if (!line.items.empty() && line.width + kGap + p.width > c.maxWidth) {
                lines.insert(lines.begin(), line);
                line = {};
            }
            line.width += (line.items.empty() ? 0 : kGap) + p.width;
            line.height = std::max(line.height, p.height);
            line.items.push_back(p);
        }
        if (!line.items.empty()) lines.insert(lines.begin(), line);
        float width = c.hasBoundedWidth() ? c.maxWidth : 0, y = 0;
        for (const Line& l : lines) width = std::max(width, l.width);
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i > 0) y += kRowGap;
            float x = width - lines[i].width;  // at the end
            for (const Placeable& p : lines[i].items) {
                p.place(x, y + (lines[i].height - p.height) / 2);  // (mirrored right to left)
                x += p.width + kGap;
            }
            y += lines[i].height;
        }
        return c.constrain({width, y});
    });
}
} // namespace

void AlertDialog(std::function<void()> onDismissRequest, const AlertDialogSlots& slots, const Modifier& modifier,
                 DialogProperties properties, CallSite site) {
    ComposableScope scope(site);
    Dialog(
        std::move(onDismissRequest),
        [&] {
            const ColorScheme& cs = colorScheme();
            const Typography& t = typography();
            Shape shape = slots.shape.value_or(RoundedCornerShape(AlertDialogDefaults::cornerRadius));
            Column(modifier.widthIn(kDialogMinWidth, kDialogMaxWidth)
                       .background(slots.containerColor.value_or(AlertDialogDefaults::containerColor()), shape)
                       .padding(24),
                   {.horizontalAlignment = slots.icon ? Alignment::CenterHorizontally : Alignment::Start}, [&] {
                       if (slots.icon)
                           Box(Modifier().padding(0, 0, 0, 16),
                               [&] { provide(slots.iconContentColor.value_or(AlertDialogDefaults::iconContentColor()), t.bodyMedium, slots.icon); });
                       if (slots.title)
                           Box(Modifier().padding(0, 0, 0, 16), [&] {
                               provide(slots.titleContentColor.value_or(AlertDialogDefaults::titleContentColor()), t.headlineSmall, slots.title);
                           });
                       if (slots.text)  // gives way to the buttons in a short window
                           Box(Modifier().fillMaxWidth().weight(1, false).padding(0, 0, 0, 24), [&] {
                               provide(slots.textContentColor.value_or(AlertDialogDefaults::textContentColor()), t.bodyMedium, slots.text);
                           });
                       provide(cs.primary, t.labelLarge, [&] {
                           dialogButtons([&] {
                               if (slots.dismissButton) slots.dismissButton();
                               if (slots.confirmButton) slots.confirmButton();
                           });
                       });
                   });
        },
        properties);
}

void BasicAlertDialog(std::function<void()> onDismissRequest, const Modifier& modifier, const Content& content,
                      DialogProperties properties, CallSite site) {
    ComposableScope scope(site);
    Dialog(
        std::move(onDismissRequest),
        [&] {
            // Its children get its least width too (Compose's propagateMinConstraints).
            Layout(modifier.widthIn(kDialogMinWidth, kDialogMaxWidth), content,
                   [](const std::vector<Measurable>& children, const Constraints& c) {
                       Size size{c.minWidth, c.minHeight};
                       std::vector<Placeable> placed;
                       for (const Measurable& m : children) {
                           Placeable p = m.measure(c);
                           size = {std::max(size.width, p.width), std::max(size.height, p.height)};
                           placed.push_back(p);
                       }
                       for (const Placeable& p : placed) p.place(0, 0);
                       return c.constrain(size);
                   });
        },
        properties);
}

namespace {

// A menu's popup under its anchor, on a card or (expressive, with groups for cards) bare.
void menuPopup(bool expanded, std::function<void()> onDismissRequest, const Content& content, const Modifier& modifier, bool card) {
    // Its progress worked out here as it turns and gets there, read on the way as it is drawn.
    auto& composer = detail::Composer::require();
    int64_t now = composer.frameTimeNanos();
    auto clock = remember([&] {
        auto made = std::make_shared<detail::TransitionProgress>();
        made->from = made->to = expanded ? 1.f : 0.f;
        made->host = &composer.host();
        return made;
    });
    if (clock->to != (expanded ? 1.f : 0.f)) clock->turn(expanded ? 1.f : 0.f, tween(expanded ? 120 : 75), now);
    bool done = false;
    float p = clock->at(now, &done);
    if (!expanded && done) return;  // closed: not composed at all
    if (!done) composer.requestFrameAt(clock->end);
    const ColorScheme& cs = colorScheme();
    PaddingValues insets = WindowInsets::safeDrawing().asPaddingValues();
    auto dismiss = rememberUpdatedState(std::move(onDismissRequest));
    OverlaySpec spec;
    bool rtl = LocalLayoutDirection.current() == LayoutDirection::Rtl;  // under the anchor's end edge
    spec.position = [insets, rtl](const Rect& anchor, Size size, Size viewport) {
        float top = insets.top + 8, bottom = viewport.height - insets.bottom - 8;
        float x = std::clamp(rtl ? anchor.right() - size.width : anchor.x, 8.f, std::max(8.f, viewport.width - size.width - 8));
        float y = anchor.bottom();
        if (y + size.height > bottom && anchor.y - size.height >= top) y = anchor.y - size.height;  // flip above
        return Offset{x, std::clamp(y, top, std::max(top, bottom - size.height))};
    };
    if (expanded)
        spec.onDismissRequest = [dismiss] {
            if (dismiss.get()) dismiss.get()();
        };
    spec.focusable = expanded;  // while closing, input goes to what is below
    Popup(std::move(spec), [&] {
        CompositionLocalProvider({LocalContentColor.provides(cs.onSurface)}, [&] {
            float s = 0.8f + 0.2f * p;
            detail::Live<float> progress = detail::live(clock);
            detail::Composer::require().emit(
                detail::keyOf(CallSite::current()),
                [&](LayoutNode& node) {
                    Modifier m = done ? Modifier().graphicsLayer({.scaleX = s, .scaleY = s, .alpha = p, .transformOrigin = {0, 0}})
                                      : Modifier().graphicsLayer([progress](GraphicsLayerScope& l) {
                                            float now = progress();
                                            l.scaleX = l.scaleY = 0.8f + 0.2f * now, l.alpha = now, l.transformOrigin = {0, 0};
                                        });
                    node.modifier = card ? m.shadow(3, 4).background(cs.surfaceContainer, 4).then(modifier).padding(0, 8) : m.then(modifier);
                    node.measurePolicy = menuPolicy;
                    node.drawContent = {};
                },
                content);
        });
    });
}

// An item's row: the text with its supporting line, between the icons.
void menuItem(const Modifier& modifier, Color textColor, Color iconColor, const Content& text, const Content& leading,
              const MenuItemOptions& options) {
    const ColorScheme& cs = colorScheme();
    const Typography& t = typography();
    float vertical = options.supportingText ? 8.f : 0.f;
    Row(modifier.heightIn(48).padding(12, vertical), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically},
        [&] {
            provide(iconColor, t.labelLarge, leading);
            Column(Modifier().weight(1), [&] {
                // Compose's (1.4 and the expressive ones): the text labelLarge, supporting text bodyMedium.
                provide(textColor, t.labelLarge, text);
                if (options.supportingText) provide(options.enabled ? cs.onSurfaceVariant : cs.onSurface.copy(0.38f), t.bodyMedium, options.supportingText);
            });
            provide(iconColor, t.labelLarge, options.trailingIcon);
        });
}

// A checked or selected item: in tertiaryContainer, 12 dp round, its check before the text.
void markedItem(bool marked, const std::function<Modifier(const Modifier&)>& interact, const Content& text, const MenuItemOptions& o) {
    const ColorScheme& cs = colorScheme();
    auto container = animateColorAsState(marked ? cs.tertiaryContainer : cs.tertiaryContainer.copy(0), 150);  // (read as it is drawn)
    Color content = !o.enabled ? cs.onSurface.copy(0.38f) : marked ? cs.onTertiaryContainer : cs.onSurface;
    Color icon = !o.enabled ? cs.onSurface.copy(0.38f) : marked ? cs.onTertiaryContainer : cs.onSurfaceVariant;
    Shape shape = marked ? RoundedCornerShape(12) : o.shape.value_or(RoundedCornerShape(4));
    Content leading = !marked ? o.leadingIcon
                      : o.checkedLeadingIcon ? o.checkedLeadingIcon
                                             : Content([] { Icon(Icons::Filled::Check, "", Modifier().size(24)); });
    menuItem(interact(detail::animatedBackground(Modifier().padding(4, 0), container, shape)), content, icon, text, leading, o);
}

} // namespace

void DropdownMenu(bool expanded, std::function<void()> onDismissRequest, const Content& content,
                  const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    menuPopup(expanded, std::move(onDismissRequest), content, modifier, true);
}

void DropdownMenuPopup(bool expanded, std::function<void()> onDismissRequest, const Content& content, const Modifier& modifier,
                       CallSite site) {
    ComposableScope scope(site);
    menuPopup(expanded, std::move(onDismissRequest), content, modifier, false);
}

void DropdownMenuGroup(const Shape& shape, const Content& content, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    Column(modifier.fillMaxWidth().shadow(3, shape).background(cs.surfaceContainerLow, shape).padding(0, 4), content);
}

namespace MenuDefaults {

Shape groupShape(int index, int count) {
    float top = index == 0 ? 16.f : 4.f, bottom = index == count - 1 ? 16.f : 4.f;
    return RoundedCornerShape(top, top, bottom, bottom);
}

Shape itemShape(int index, int count) {
    float top = index == 0 ? 12.f : 4.f, bottom = index == count - 1 ? 12.f : 4.f;
    return RoundedCornerShape(top, top, bottom, bottom);
}

} // namespace MenuDefaults

void DropdownMenuItem(bool checked, std::function<void(bool)> onCheckedChange, const Content& text, const MenuItemOptions& options,
                      CallSite site) {
    ComposableScope scope(site);
    markedItem(checked, [&](const Modifier& m) {
        return m.toggleable(checked, std::move(onCheckedChange), SemanticsRole::Checkbox, {.enabled = options.enabled});
    }, text, options);
}

void DropdownMenuItem(bool selected, std::function<void()> onClick, const Content& text, const MenuItemOptions& options, CallSite site) {
    ComposableScope scope(site);
    markedItem(selected, [&](const Modifier& m) {
        return m.selectable(selected, std::move(onClick), SemanticsRole::RadioButton, {.enabled = options.enabled});
    }, text, options);
}

void DropdownMenuItem(const Content& text, std::function<void()> onClick, const MenuItemOptions& options, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    Color textColor = options.enabled ? cs.onSurface : cs.onSurface.copy(0.38f);
    Color iconColor = options.enabled ? cs.onSurfaceVariant : cs.onSurface.copy(0.38f);
    Modifier m = options.shape ? Modifier().padding(4, 0).clip(*options.shape) : Modifier();  // in a group: inset, shaped
    menuItem(m.clickable(std::move(onClick), options.enabled), textColor, iconColor, text, options.leadingIcon, options);
}

namespace {
// Compose 1.4's sheet motion: shown and settled after a drag on the default spatial spring,
// hidden on the fast effects one (the standard motion scheme's: the state lives outside the theme).
const AnimationSpec kSheetShow = MotionScheme::standard().defaultSpatialSpec, kSheetHide = MotionScheme::standard().fastEffectsSpec;
AnchoredDraggableState<SheetValue> sheetDraggable(SheetValue value) {
    return AnchoredDraggableState<SheetValue>(value, {.snapAnimationSpec = kSheetShow});
}
} // namespace

struct ModalBottomSheetState::Data {
    AnchoredDraggableState<SheetValue> anchored = sheetDraggable(SheetValue::Hidden);
    bool skipPartiallyExpanded = false;
    bool appeared = false;        // the opening slide has started (reset when the sheet goes)
    bool hiddenByCode = false;    // hide(): the caller removes the sheet; otherwise onDismissRequest
    float sheet = 0, room = 0;    // its height and the height it may take (from the layout)
    float width = 0;
    std::optional<CoroutineScope> coroutines;

    SheetValue opening() const {
        return anchored.anchors().hasPositionFor(SheetValue::PartiallyExpanded) ? SheetValue::PartiallyExpanded : SheetValue::Expanded;
    }
    // Anchors in dp down from all the way up: up there 0, half way (a tall sheet's), hidden below.
    void updateAnchors() {
        if (sheet <= 0 || room <= 0) return;
        DraggableAnchors<SheetValue> anchors{{SheetValue::Expanded, 0}, {SheetValue::Hidden, sheet}};
        if (!skipPartiallyExpanded && sheet > room / 2) anchors.at(SheetValue::PartiallyExpanded, sheet - room / 2);
        anchored.updateAnchors(std::move(anchors));
        // Back (saved) half way, but now too short for half way: all the way up.
        if (appeared && !anchored.isAnimationRunning() && !anchored.anchors().hasPositionFor(anchored.targetValue()))
            anchored.snapTo(opening());
        if (!appeared && coroutines) {  // slides in once it knows its size
            appeared = true;
            hiddenByCode = false;
            coroutines->launch([a = anchored, to = opening()]() -> Task { co_await a.animateTo(to, kSheetShow); });
        }
    }
};

ModalBottomSheetState::ModalBottomSheetState(bool skipPartiallyExpanded) : data_(std::make_shared<Data>()) {
    data_->skipPartiallyExpanded = skipPartiallyExpanded;
}
bool ModalBottomSheetState::isVisible() const { return data_->anchored.currentValue() != SheetValue::Hidden; }
SheetValue ModalBottomSheetState::currentValue() const { return data_->anchored.currentValue(); }
SheetValue ModalBottomSheetState::targetValue() const { return data_->anchored.targetValue(); }
bool ModalBottomSheetState::hasPartiallyExpandedState() const {
    return data_->anchored.anchors().hasPositionFor(SheetValue::PartiallyExpanded);
}
Task ModalBottomSheetState::show() const {
    data_->hiddenByCode = false;
    return data_->anchored.animateTo(data_->opening(), kSheetShow);
}
Task ModalBottomSheetState::hide() const {
    data_->hiddenByCode = true;
    return data_->anchored.animateTo(SheetValue::Hidden, kSheetHide);
}
Task ModalBottomSheetState::expand() const { return data_->anchored.animateTo(SheetValue::Expanded); }
Task ModalBottomSheetState::partialExpand() const {
    return data_->anchored.animateTo(hasPartiallyExpandedState() ? SheetValue::PartiallyExpanded : SheetValue::Expanded);
}
float ModalBottomSheetState::requireOffset() const { return data_->anchored.requireOffset(); }

SavedValue Saver<ModalBottomSheetState>::save(const ModalBottomSheetState& s) {
    return SavedValue::List{s.data_->skipPartiallyExpanded, int64_t(s.currentValue())};
}
std::optional<ModalBottomSheetState> Saver<ModalBottomSheetState>::restore(const SavedValue& v) {
    const SavedValue::List* l = v.list();
    if (!l || l->size() != 2) return std::nullopt;
    auto skip = (*l)[0].boolean();
    auto value = Saver<SheetValue>::restore((*l)[1]);
    if (!skip || !value) return std::nullopt;
    ModalBottomSheetState s(*skip);
    if (*value != SheetValue::Hidden) {  // open: shown where it was, without sliding in again
        s.data_->anchored = sheetDraggable(*value);
        s.data_->appeared = true;
    }
    return s;
}

ModalBottomSheetState rememberModalBottomSheetState(bool skipPartiallyExpanded, CallSite site) {
    return rememberSaveable([&] { return ModalBottomSheetState(skipPartiallyExpanded); }, site);
}

void ModalBottomSheet(std::function<void()> onDismissRequest, const Content& content, CallSite site) {
    ComposableScope scope(site);
    ModalBottomSheet(std::move(onDismissRequest), rememberModalBottomSheetState(), content);
}

void ModalBottomSheet(std::function<void()> onDismissRequest, const ModalBottomSheetState& sheetState, const Content& content,
                      CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    PaddingValues insets = WindowInsets::safeDrawing().asPaddingValues();
    auto dismiss = rememberUpdatedState(std::move(onDismissRequest));
    auto d = sheetState.data_;
    auto coroutines = rememberCoroutineScope();
    d->coroutines = coroutines;
    AnchoredDraggableState<SheetValue> anchored = d->anchored;
    // Gone from the composition: the next time it opens it slides in again.
    DisposableEffect(d, [d]() -> std::function<void()> {
        return [d] {
            d->appeared = false;
            d->anchored.snapTo(SheetValue::Hidden);
        };
    });
    // How far a back gesture has shrunk it (M3's predictive back): kept while it hides after one.
    auto backProgress = remember([] { return Animatable<float>(0.f); });
    // Hidden by the user (the scrim, back, dragged down): onDismissRequest once it is out.
    bool hidden = anchored.currentValue() == SheetValue::Hidden && d->appeared && !anchored.isAnimationRunning();
    LaunchedEffect(hidden, [hidden, d, dismiss, backProgress]() -> Task {
        if (hidden) backProgress.snapTo(0);
        if (hidden && !d->hiddenByCode && dismiss.get()) dismiss.get()();
        co_return;
    });
    auto close = rememberUpdatedState(std::function<void()>([anchored, coroutines] {
        if (anchored.targetValue() == SheetValue::Hidden) return;
        coroutines.launch([anchored]() -> Task { co_await anchored.animateTo(SheetValue::Hidden, kSheetHide); });
    }));
    // A list in the sheet scrolls first; pulled down at its top the sheet takes what it leaves
    // (its anchoredDraggable is the list's scroll parent, and settles at the fling's end). Pushed
    // up, the sheet comes back up — or expands from half way — before the list scrolls.
    auto connection = rememberNestedScrollConnection({
        .onPreScroll = [anchored](Offset available, NestedScrollSource source) -> Offset {
            if (source != NestedScrollSource::UserInput || available.y >= 0) return {};
            return {0, anchored.dispatchRawDelta(available.y)};
        },
        .onPreFling = [anchored, coroutines](Velocity available) -> Velocity {
            if (available.y >= 0 || anchored.requireOffset() <= anchored.anchors().minPosition()) return {};
            coroutines.launch([anchored, v = available.y]() -> Task { co_await anchored.settle(v); });
            return {0, available.y};
        },
    });
    auto scrim = animateFloatAsState(anchored.targetValue() != SheetValue::Hidden ? kScrimAlpha : 0.f, motionScheme().defaultEffectsSpec);  // (read as it is drawn)
    constexpr float kHiddenCorner = 28;  // the sheet reaches below the window so only its top corners are round

    // Shrunk by a back gesture as M3's: up to 48 dp narrower and 24 dp lower, towards its bottom
    // middle; its content kept from stretching (scaled back along the height, from the top).
    auto shrink = [d, backProgress] {
        float p = backProgress.value();
        float sx = d->width > 0 ? 1 - std::min(48.f, d->width) * p / d->width : 1.f;
        float sy = d->sheet > 0 ? 1 - std::min(24.f, d->sheet) * p / d->sheet : 1.f;
        return std::pair{sx, sy};
    };

    Popup({.fullScreen = true, .onDismissRequest = [close] { close.get()(); }, .dismissOnOutsideClick = false}, [&] {
        // Back as M3's sheet takes it: the gesture shrinks it; done, an expanded sheet that stops
        // half way goes back there (and grows back), any other hides; called off, it grows back.
        static constexpr Easing kPredictiveBackEasing{0.1f, 0.1f, 0.f, 1.f};
        auto follow = [backProgress](const BackEventCompat& e) { backProgress.snapTo(kPredictiveBackEasing(e.progress)); };
        auto growBack = [coroutines, backProgress] { coroutines.launch([backProgress]() -> Task { co_await backProgress.animateTo(0.f); }); };
        detail::backHandler({
            .onBack = [anchored, coroutines, close, growBack] {
                if (anchored.currentValue() == SheetValue::Expanded && anchored.anchors().hasPositionFor(SheetValue::PartiallyExpanded)) {
                    growBack();
                    coroutines.launch([anchored]() -> Task { co_await anchored.animateTo(SheetValue::PartiallyExpanded); });
                } else {
                    close.get()();
                }
            },
            .onStarted = follow,
            .onProgressed = follow,
            .onCancelled = growBack,
            .enabled = anchored.targetValue() != SheetValue::Hidden,
        });
        // Stays clear of the status bar: a tall sheet leaves some scrim to tap.
        Box(onTap(detail::animatedBackground(Modifier().fillMaxSize(),
                                             detail::Live<Color>{[scrim, color = cs.scrim] { return color.copy(color.alpha() * scrim.value()); }}),
                  close)
                .padding(0, insets.top + 48, 0, 0)
                .onSizeChanged([d](Size s) {
                    d->room = s.height;
                    d->updateAnchors();
                }),
            Alignment::BottomCenter, [&] {
                Modifier sheet = Modifier()
                                     .offset([anchored] {
                                         float o = anchored.offset();
                                         return Offset{0, kHiddenCorner + (std::isnan(o) ? 100000.f : o)};  // below until it knows its size
                                     })
                                     .widthIn(0, 640)
                                     .fillMaxWidth()
                                     .onSizeChanged([d](Size s) {
                                         d->sheet = s.height;
                                         d->width = s.width;
                                         d->updateAnchors();
                                     })
                                     .graphicsLayer([d, shrink](GraphicsLayerScope& layer) {  // read while drawing
                                         auto [sx, sy] = shrink();
                                         layer.scaleX = sx, layer.scaleY = sy;
                                         if (d->sheet > 0) layer.transformOrigin = {0.5f, (d->sheet - kHiddenCorner) / d->sheet};
                                     })
                                     .background(cs.surfaceContainerLow, 28);
                sheet = swallowPresses(sheet).anchoredDraggable(anchored, Orientation::Vertical).nestedScroll(connection);
                CompositionLocalProvider({LocalContentColor.provides(cs.onSurface)}, [&] {
                    Column(sheet.padding(0, 0, 0, insets.bottom + kHiddenCorner), [&] {
                        Column(Modifier().fillMaxWidth().graphicsLayer([shrink](GraphicsLayerScope& layer) {
                                   auto [sx, sy] = shrink();
                                   layer.scaleY = sx / sy;
                                   layer.transformOrigin = {0.5f, 0};
                               }),
                               {.horizontalAlignment = Alignment::CenterHorizontally}, [&] {
                                   BottomSheetDefaults::DragHandle();
                                   Column(Modifier().fillMaxWidth(), content);
                               });
                    });
                });
            });
    });
}

} // namespace kompoz
