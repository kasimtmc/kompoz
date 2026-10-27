// Material 3 tooltips: TooltipBox with plain and rich tooltips.

#include <kompoz/animated.hpp>
#include <kompoz/animation.hpp>
#include <kompoz/coroutines.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/gestures.hpp>
#include <kompoz/material.hpp>
#include <kompoz/state.hpp>

#include <algorithm>

namespace kompoz {

namespace {

// Where the anchor is, for a tooltip's caret: told by its TooltipBox when the popup is placed,
// read while drawing. The tooltip tells how tall its caret is, to stand that much farther off.
struct Caret {
    float anchorX = 0, anchorY = 0;  // the anchor's middle, from the tooltip's top left
    enum class Side { Top, Bottom, Left, Right } side = Side::Bottom;  // the tooltip's side towards the anchor
    float height = 0;
};
const CompositionLocal<std::shared_ptr<Caret>> LocalTooltipCaret{nullptr};

// The caret of a tooltip of `color`, on the side towards the anchor (none when `size` is empty),
// with a shadow as the tooltip's `elevation` casts; it goes between the tooltip's shadow and its
// background, which then cover the shadows' seams.
Modifier caret(Size size, Color color, float elevation = 0) {
    auto info = LocalTooltipCaret.current();
    if (!info) return {};
    info->height = std::max(0.f, size.height);
    if (size.height <= 0 || size.width <= 0) return {};
    return Modifier().drawBehind([info, size, color, elevation](DrawScope& d) {
        float w = d.size().width, h = d.size().height;
        using Side = Caret::Side;
        bool across = info->side == Side::Top || info->side == Side::Bottom;  // on a horizontal side
        // Along its side, at the anchor's middle as far as the side lets it.
        float along = across ? std::clamp(info->anchorX, size.width / 2 + 4, std::max(size.width / 2 + 4, w - size.width / 2 - 4))
                             : std::clamp(info->anchorY, size.width / 2 + 4, std::max(size.width / 2 + 4, h - size.width / 2 - 4));
        // The caret `grow` bigger all round and `down` lower, its base a little into the tooltip.
        auto triangle = [&](float grow, float down) {
            float half = size.width / 2 + grow, out = size.height + grow;
            float base = info->side == Side::Top ? 0.5f : info->side == Side::Bottom ? h - 0.5f : info->side == Side::Left ? 0.5f : w - 0.5f;
            float tip = info->side == Side::Top || info->side == Side::Left ? -out : (across ? h : w) + out;
            auto at = [&](float a, float b) { return across ? Offset{a, b + down} : Offset{b, a + down}; };  // along, away
            Path path;
            Offset p0 = at(along - half, base), p1 = at(along, tip), p2 = at(along + half, base);
            path.moveTo(p0.x, p0.y);
            path.lineTo(p1.x, p1.y);
            path.lineTo(p2.x, p2.y);
            path.close();
            return path;
        };
        if (elevation > 0)  // its shadow, as the card's (an elevation shadow of its outline)
            d.context().drawElevationShadow(Outline::Generic(triangle(0, 0)), {d.bounds().x, d.bounds().y},
                                            ElevationShadow{{0, 0, elevation}, DefaultShadowColor, DefaultShadowColor, 1});
        d.drawPath(triangle(0, 0), color);
    });
}

} // namespace

struct TooltipState::Data {
    MutableState<bool> visible{false};
    bool persistent = false;
    bool byHover = false;  // shown for a resting mouse: hidden when it leaves
};

TooltipState::TooltipState(bool initialIsVisible, bool isPersistent) : data_(std::make_shared<Data>()) {
    data_->visible.set(initialIsVisible);
    data_->persistent = isPersistent;
}
bool TooltipState::isVisible() const { return data_->visible.get(); }
bool TooltipState::isPersistent() const { return data_->persistent; }
void TooltipState::show() const {
    static std::weak_ptr<Data> showing;  // the one that shows, if any
    if (auto other = showing.lock(); other && other != data_ && other->visible.peek()) {
        other->byHover = false;
        other->visible.set(false);
    }
    showing = data_;
    data_->byHover = false;
    if (!data_->visible.peek()) data_->visible.set(true);
}
void TooltipState::dismiss() const {
    data_->byHover = false;
    if (data_->visible.peek()) data_->visible.set(false);
}

TooltipState rememberTooltipState(bool initialIsVisible, bool isPersistent, CallSite site) {
    return remember([&] { return TooltipState(initialIsVisible, isPersistent); }, site);
}
BasicTooltipState rememberBasicTooltipState(bool initialIsVisible, bool isPersistent, CallSite site) {
    return remember([&] { return TooltipState(initialIsVisible, isPersistent); }, site);
}

namespace {
// What both boxes do: the anchor (taking a long press and a resting mouse when `input`), the
// tooltip in a popup at `position` while it shows — `animated`: growing and fading in and out
// (Material's), its caret told where the anchor is.
void tooltipBox(const TooltipPosition& position, const std::shared_ptr<Caret>& caretInfo, const Content& tooltip, const TooltipState& state,
                const Modifier& modifier, const Content& content, bool focusable, bool input, bool animated) {
    auto d = state.data_;
    auto hovering = remember([] { return mutableStateOf(false); });
    bool hovered = hovering.get(), visible = state.isVisible();
    // A mouse resting on the anchor shows the tooltip after a moment; leaving hides it.
    LaunchedEffect(hovered, [state, d, hovered]() -> Task {
        if (!hovered) {
            if (d->byHover) state.dismiss();
            co_return;
        }
        co_await delay(std::chrono::milliseconds(500));
        if (state.isVisible()) co_return;
        state.show();
        d->byHover = true;
    });
    // A transient one shown by a long press goes away by itself.
    LaunchedEffect(visible, [state, d, visible]() -> Task {
        if (!visible || d->persistent || d->byHover) co_return;
        co_await delay(std::chrono::milliseconds(1500));
        if (!d->byHover) state.dismiss();
    });
    // Compose 1.4's: in and out growing from (shrinking to) 80% on the fast spatial spring and
    // fading on the fast effects one; shown until it has faded out.
    // Worked out here as it turns and gets there; on the way, read as it is drawn.
    auto& composer = detail::Composer::require();
    int64_t now = composer.frameTimeNanos();
    auto clock = [&](float shown) {
        auto made = std::make_shared<detail::TransitionProgress>();
        made->from = made->to = shown;
        made->host = &composer.host();
        return made;
    };
    auto fade = remember([&] { return clock(visible ? 1.f : 0.f); });
    auto grow = remember([&] { return clock(visible ? 1.f : 0.8f); });
    if (animated && fade->to != (visible ? 1.f : 0.f)) fade->turn(visible ? 1.f : 0.f, motionScheme().fastEffectsSpec, now);
    if (animated && grow->to != (visible ? 1.f : 0.8f)) grow->turn(visible ? 1.f : 0.8f, motionScheme().fastSpatialSpec, now);
    bool faded = false, grown = false;
    float alpha = animated ? fade->at(now, &faded) : float(visible), scale = animated ? grow->at(now, &grown) : 1.f;
    if (!animated) faded = grown = true;
    if (!faded) composer.requestFrameAt(fade->end);
    if (!grown) composer.requestFrameAt(grow->end);
    Modifier anchor = modifier;
    if (input) {
        auto trigger = std::make_shared<TooltipTriggerElement>();
        trigger->onLongPress = [state] { state.show(); };
        trigger->onHoverChange = [hovering](bool h) { hovering.set(h); };
        anchor = anchor.then(trigger);
    }
    Box(anchor, [&] {
        content();
        if (!visible && faded) return;  // faded out
        Popup({.position = position, .onDismissRequest = [state] { state.dismiss(); }, .focusable = focusable}, [&] {
            auto shown = [&] {
                if (animated && (!faded || !grown))
                    Box(Modifier().graphicsLayer([fading = detail::live(fade), growing = detail::live(grow)](GraphicsLayerScope& l) {
                            l.scaleX = l.scaleY = growing(), l.alpha = fading();
                        }),
                        tooltip);
                else if (animated)
                    Box(Modifier().graphicsLayer({.scaleX = scale, .scaleY = scale, .alpha = alpha}), tooltip);
                else tooltip();
            };
            if (caretInfo) CompositionLocalProvider({LocalTooltipCaret.provides(caretInfo)}, shown);
            else shown();
        });
    });
}
} // namespace

void BasicTooltipBox(const TooltipPosition& position, const Content& tooltip, const BasicTooltipState& state, const Modifier& modifier,
                     const Content& content, bool focusable, bool enableUserInput, CallSite site) {
    ComposableScope scope(site);
    tooltipBox(position, nullptr, tooltip, state, modifier, content, focusable, enableUserInput, false);
}

void TooltipBox(const Content& tooltip, const TooltipState& state, const Modifier& modifier, const Content& content,
                CallSite site) {
    ComposableScope scope(site);
    TooltipBox(TooltipPositionProvider{}, tooltip, state, modifier, content);
}

void TooltipBox(const TooltipPositionProvider& positionProvider, const Content& tooltip, const TooltipState& state,
                const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    auto caretInfo = remember([] { return std::make_shared<Caret>(); });
    // Start and End as the layout goes.
    TooltipAnchorPosition side = positionProvider.positioning;
    bool rtl = LocalLayoutDirection.current() == LayoutDirection::Rtl;
    if (side == TooltipAnchorPosition::Start) side = rtl ? TooltipAnchorPosition::Right : TooltipAnchorPosition::Left;
    if (side == TooltipAnchorPosition::End) side = rtl ? TooltipAnchorPosition::Left : TooltipAnchorPosition::Right;
    // On its side of the anchor, centered on it across, or on the other side when there is no
    // room; 8 dp off the window's edges where it fits; a caret's height farther off.
    TooltipPosition position = [caretInfo, side, spacing = positionProvider.spacingBetweenTooltipAndAnchor](const Rect& anchor, Size size,
                                                                                                        Size viewport) {
        constexpr float kMargin = 8;
        using Side = Caret::Side;
        float gap = spacing + caretInfo->height;
        auto across = [&](float start, float length, float room, float own) {
            float margin = std::min(kMargin, std::max(0.f, (room - own) / 2));
            return std::clamp(start + (length - own) / 2, margin, room - own - margin);
        };
        float x, y;
        if (side == TooltipAnchorPosition::Above || side == TooltipAnchorPosition::Below) {
            x = across(anchor.x, anchor.width, viewport.width, size.width);
            float above = anchor.y - gap - size.height, below = anchor.bottom() + gap;
            bool under = side == TooltipAnchorPosition::Below ? below + size.height <= viewport.height || above < 0 : above < 0;
            y = under ? below : above;
            caretInfo->side = under ? Side::Top : Side::Bottom;
        } else {
            y = across(anchor.y, anchor.height, viewport.height, size.height);
            float left = anchor.x - gap - size.width, right = anchor.right() + gap;
            bool toRight = side == TooltipAnchorPosition::Right ? right + size.width <= viewport.width || left < 0 : left < 0;
            x = toRight ? right : left;
            caretInfo->side = toRight ? Side::Left : Side::Right;
        }
        caretInfo->anchorX = anchor.x + anchor.width / 2 - x;
        caretInfo->anchorY = anchor.y + anchor.height / 2 - y;
        return Offset{x, y};
    };
    // A transient one lets input through.
    tooltipBox(position, caretInfo, tooltip, state, modifier, content, state.isPersistent(), true, true);
}

void PlainTooltip(const Content& content, const Modifier& modifier, CallSite site) { PlainTooltip(content, modifier, Size{}, site); }

void PlainTooltip(const Content& content, const Modifier& modifier, Size caretSize, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    Box(modifier.heightIn(24).widthIn(40, 200).then(caret(caretSize, cs.inverseSurface)).background(cs.inverseSurface, 4).padding(8, 4),
        Alignment::CenterStart, [&] {
        CompositionLocalProvider({LocalContentColor.provides(cs.inverseOnSurface), LocalTextStyle.provides(typography().bodySmall)},
                                 content);
    });
}

void RichTooltip(const RichTooltipSlots& slots, const Modifier& modifier, CallSite site) { RichTooltip(slots, modifier, Size{}, site); }

void RichTooltip(const RichTooltipSlots& slots, const Modifier& modifier, Size caretSize, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    const Typography& t = typography();
    // Compose 1.4's: 3 dp up, 16 dp at the sides; the title's first baseline 28 dp down, the
    // text's 24 under it (or with an action) and 16 above the bottom (else 4 above and below);
    // the action at least 36 dp tall, 8 above the bottom.
    Modifier card = modifier.heightIn(24).widthIn(40, 320).shadow(3, RoundedCornerShape(12), false).then(caret(caretSize, cs.surfaceContainer, 3));  // (the caret past it)
    bool spaced = slots.title || slots.action;
    Column(card.background(cs.surfaceContainer, 12).padding(16, 0), [&] {
        if (slots.title)
            Box(Modifier().paddingFromBaseline(28), [&] {
                CompositionLocalProvider({LocalContentColor.provides(cs.onSurfaceVariant), LocalTextStyle.provides(t.titleSmall)},
                                         slots.title);
            });
        if (slots.text)
            Box(spaced ? Modifier().paddingFromBaseline(24).padding(0, 0, 0, 16) : Modifier().padding(0, 4), [&] {
                CompositionLocalProvider({LocalContentColor.provides(cs.onSurfaceVariant), LocalTextStyle.provides(t.bodyMedium)},
                                         slots.text);
            });
        if (slots.action)
            Box(Modifier().requiredHeightIn(36).padding(0, 0, 0, 8), [&] {
                CompositionLocalProvider({LocalContentColor.provides(cs.primary), LocalTextStyle.provides(t.labelLarge)}, slots.action);
            });
    });
}

void TooltipArea(const Content& tooltip, const Modifier& modifier, const Content& content, CallSite site) {
    TooltipArea(tooltip, modifier, TooltipAreaOptions{}, content, site);
}

void TooltipArea(const Content& tooltip, const Modifier& modifier, const TooltipAreaOptions& options, const Content& content,
                 CallSite site) {
    ComposableScope scope(site);
    auto cursor = remember([] { return mutableStateOf(Offset{}); });  // in the area
    auto visible = remember([] { return mutableStateOf(false); });
    auto waiting = remember([] { return std::make_shared<int>(0); });  // the pending show's number (0: none)
    auto coroutines = rememberCoroutineScope();
    auto next = remember([] { return std::make_shared<int>(0); });
    int delayMillis = options.delayMillis;
    // Shown once the mouse has been over it for the delay; a press or leaving hides it (and
    // forgets a pending show).
    auto show = [cursor, visible, waiting, next, coroutines, delayMillis](PointerEvent& e) {
        cursor.set(e.change.position);
        if (visible.peek() || *waiting) return;
        int number = *waiting = ++*next;
        coroutines.launch([visible, waiting, number, delayMillis]() -> Task {
            co_await delay(std::chrono::milliseconds(delayMillis));
            if (*waiting != number) co_return;
            *waiting = 0;
            visible.set(true);
        });
    };
    auto hide = [visible, waiting] {
        *waiting = 0;
        visible.set(false);
    };
    Box(Modifier()
            .onPointerEvent(PointerEventType::Enter, show)
            .onPointerEvent(PointerEventType::Move, show)
            .onPointerEvent(PointerEventType::Exit, [hide](PointerEvent&) { hide(); })
            .onPointerEvent(PointerEventType::Press, [hide](PointerEvent&) { hide(); })
            .then(modifier),
        [&] {
            content();
            if (!visible.get()) return;
            TooltipPlacement p = options.tooltipPlacement;
            Popup({.position = [cursor, p](const Rect& area, Size size, Size window) {
                       Offset at;
                       if (p.kind == TooltipPlacement::Kind::CursorPoint) {
                           Offset c = cursor.peek();
                           at = {area.x + c.x + p.offset.x, area.y + c.y + p.offset.y};
                           if (at.y + size.height > window.height - p.windowMargin)  // no room below: above it
                               at.y = area.y + c.y - p.offset.y - size.height;
                       } else {
                           Offset point{area.x + area.width * (1 + p.anchor.horizontal) / 2, area.y + area.height * (1 + p.anchor.vertical) / 2};
                           at = {point.x - size.width * (1 - p.alignment.horizontal) / 2 + p.offset.x,
                                 point.y - size.height * (1 - p.alignment.vertical) / 2 + p.offset.y};
                       }
                       float m = p.windowMargin;
                       return Offset{std::clamp(at.x, m, std::max(m, window.width - size.width - m)),
                                     std::clamp(at.y, m, std::max(m, window.height - size.height - m))};
                   },
                   .onDismissRequest = hide,
                   .dismissOnOutsideClick = false,
                   .focusable = false},
                  tooltip);
        });
}

namespace {
// The label over its content, while `visible`: above it, centered, 4 dp apart (below if there is
// no room above), kept 8 dp inside the window.
void labelOver(bool visible, const Content& label, const Modifier& modifier, const Content& content) {
    Box(modifier, [&] {
        content();
        if (!visible) return;
        Popup({.position = [](const Rect& anchor, Size size, Size window) {
                   float x = anchor.x + (anchor.width - size.width) / 2, y = anchor.y - 4 - size.height;
                   if (y < 0) y = anchor.bottom() + 4;
                   return Offset{std::clamp(x, 8.f, std::max(8.f, window.width - size.width - 8)), y};
               },
               .dismissOnOutsideClick = false,
               .focusable = false},
              label);
    });
}
} // namespace

void Label(const Content& label, const Modifier& modifier, std::optional<MutableInteractionSource> interactionSource, bool isPersistent,
           const Content& content, CallSite site) {
    ComposableScope scope(site);
    auto own = remember([] { return MutableInteractionSource(); });
    MutableInteractionSource source = interactionSource.value_or(own);
    bool shown = source.collectIsPressedAsState() || source.collectIsDraggedAsState() || source.collectIsHoveredAsState();
    labelOver(isPersistent || shown, label, modifier, content);
}

namespace SliderDefaults {
void Label(const SliderState& state, const Content& label, const Content& content, bool isPersistent, CallSite site) {
    ComposableScope scope(site);
    labelOver(isPersistent || state.isPressed(), label, Modifier(), content);
}
} // namespace SliderDefaults

} // namespace kompoz
