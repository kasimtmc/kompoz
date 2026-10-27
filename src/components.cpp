// Material 3 lists, chips, selection controls, tabs, badges and the bottom app bar.

#include <kompoz/animation.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/gestures.hpp>
#include <kompoz/icons.hpp>
#include <kompoz/material.hpp>
#include <kompoz/state.hpp>
#include <kompoz/widgets.hpp>

#include <cmath>

namespace kompoz {

namespace {

void provide(Color color, const Font& font, const Content& content) {
    if (content) CompositionLocalProvider({LocalContentColor.provides(color), LocalTextStyle.provides(font)}, content);
}

enum class ChipKind { Assist, Suggestion, Filter, Input };

// A chip's leading or trailing icon in its color; a selectable chip's grows in (the fast spatial
// spring) and fades in (the slow effects one) when it comes, and its room shrinks away (the
// default effects spring) when it goes.
void chipIcon(const Content& icon, Color color, bool animated, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    if (!animated) return provide(color, typography().labelLarge, icon);
    const MotionScheme& motion = motionScheme();
    float alpha = animateFloatAsState(icon ? 1.f : 0.f, icon ? motion.slowEffectsSpec : snap());
    Box(Modifier().animateContentSize(icon ? motion.fastSpatialSpec : motion.defaultEffectsSpec).alpha(alpha), Alignment::Center,
        [&] { provide(color, typography().labelLarge, icon); });
}

// Compose 1.4's chips: 32 dp, 8 dp corners, an outlineVariant line (none when selected or
// elevated), 8 dp around and between their parts (an input chip 4 dp at an end without an icon).
// Elevated: on surfaceContainerLow with a shadow instead of a border.
void chip(ChipKind kind, bool selected, std::function<void()> onClick, const Content& label, const ChipOptions& o,
          const Modifier& modifier, const CallSite& site, bool elevated = false) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    bool on = o.enabled, selectable = kind == ChipKind::Filter || kind == ChipKind::Input;
    Color faded = cs.onSurface.copy(0.12f);
    Color container = elevated ? (on ? (selected ? cs.secondaryContainer : cs.surfaceContainerLow) : faded)
                      : selected ? (on ? cs.secondaryContainer : faded)
                                 : colors::Transparent;
    Color border = selected || elevated ? colors::Transparent : on ? cs.outlineVariant : faded;
    Color labelColor = selected ? cs.onSecondaryContainer : kind == ChipKind::Assist ? cs.onSurface : cs.onSurfaceVariant;
    Color leadingColor = kind == ChipKind::Input ? (selected ? cs.primary : cs.onSurfaceVariant) : selected ? cs.onSecondaryContainer : cs.primary;
    Color trailingColor = selected ? cs.onSecondaryContainer : kind == ChipKind::Assist ? cs.primary : cs.onSurfaceVariant;
    if (!on) labelColor = leadingColor = trailingColor = cs.onSurface.copy(0.38f);
    Content leading = o.leadingIcon;
    bool avatar = kind == ChipKind::Input && o.avatar;
    if (avatar) leading = [&] { Box(Modifier().clip(CircleShape).alpha(on ? 1.f : 0.38f), o.avatar); };
    float start = kind == ChipKind::Input && (avatar || !o.leadingIcon) ? 4.f : 8.f;
    float end = kind == ChipKind::Input && !o.trailingIcon ? 4.f : 8.f;

    Modifier m = modifier.minimumInteractiveComponentSize().height(32);  // 48 dp of room to touch it
    if (selectable) m = m.semantics({.role = SemanticsRole::Checkbox, .checked = selected});
    if (elevated && on) m = m.shadow(1, 8);
    m = m.background(container, 8).border(1, border, 8).clickable(std::move(onClick), on);
    Row(m.padding(start, 0, end, 0), {.verticalAlignment = Alignment::CenterVertically}, [&] {
        chipIcon(leading, avatar ? labelColor : leadingColor, selectable);  // (an avatar keeps the label's)
        Box(Modifier().padding(8, 0), [&] { provide(labelColor, typography().labelLarge, label); });
        chipIcon(o.trailingIcon, trailingColor, selectable);
    });
}

} // namespace

void ListItem(const ListItemSlots& slots, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    const Typography& t = typography();
    // Compose 1.4's ListItemLayout: 16 dp at the sides and between the parts; three lines (12 dp
    // above and below, the sides at the top, at least 88) with an overline and supporting text
    // or supporting text running over a line, two (72) with either, else one (56); 8 dp above
    // and below and everything centered when not three lines.
    bool leading = bool(slots.leading), overline = bool(slots.overline), supporting = bool(slots.supporting), trailing = bool(slots.trailing);
    LayoutMeasurePolicy policy = [=](const std::vector<Measurable>& m, const Constraints& c) {
        size_t next = 0;
        float width = c.hasBoundedWidth() ? c.maxWidth : 0, room = std::max(0.f, width - 32);
        auto loose = [&](float w) { return Constraints{0, w, 0, kInfinity}; };
        std::optional<Placeable> lead, over, head, support, trail;
        if (leading) lead = m[next++].measure(loose(room));
        float leadWidth = lead ? lead->width : 0;
        size_t overAt = overline ? next++ : 0, headAt = next++, supportAt = supporting ? next++ : 0;
        if (trailing) trail = m[next].measure(loose(std::max(0.f, room - leadWidth)));
        float main = std::max(0.f, room - leadWidth - (trail ? trail->width : 0));
        head = m[headAt].measure(loose(main));
        if (supporting) support = m[supportAt].measure(loose(main));
        if (overline) over = m[overAt].measure(loose(main));
        bool multiline = support && (*support)[FirstBaseline] != (*support)[LastBaseline] && !std::isnan((*support)[FirstBaseline]);
        int lines = (overline && supporting) || multiline ? 3 : overline || supporting ? 2 : 1;
        float pad = lines == 3 ? 12.f : 8.f;
        float mainHeight = head->height + (over ? over->height : 0) + (support ? support->height : 0);
        float sides = std::max(lead ? lead->height : 0.f, trail ? trail->height : 0.f);
        float height = std::max({c.minHeight, lines == 3 ? 88.f : lines == 2 ? 72.f : 56.f, 2 * pad + std::max(mainHeight, sides)});
        if (c.hasBoundedHeight()) height = std::min(height, c.maxHeight);
        if (!c.hasBoundedWidth()) width = 32 + leadWidth + std::max({head->width, over ? over->width : 0, support ? support->width : 0}) + (trail ? trail->width : 0);
        auto y = [&](float h) { return lines == 3 ? pad : (height - h) / 2; };
        if (lead) lead->place(16, y(lead->height));
        float x = 16 + leadWidth, at = y(mainHeight);
        if (over) over->place(x, at), at += over->height;
        head->place(x, at), at += head->height;
        if (support) support->place(x, at);
        if (trail) trail->place(width - 16 - trail->width, y(trail->height));
        return Size{width, height};
    };
    Color container = slots.containerColor.value_or(cs.surface);
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurface)}, [&] {
        Layout(modifier.semantics({.mergeDescendants = true}).fillMaxWidth().background(container), [&] {
            if (leading) Box(Modifier().padding(0, 0, 16, 0), [&] { provide(cs.onSurfaceVariant, LocalTextStyle.current(), slots.leading); });
            if (overline) Box(Modifier(), [&] { provide(cs.onSurfaceVariant, t.labelSmall, slots.overline); });
            Box(Modifier(), [&] { provide(cs.onSurface, t.bodyLarge, slots.headline); });
            if (supporting) Box(Modifier(), [&] { provide(cs.onSurfaceVariant, t.bodyMedium, slots.supporting); });
            if (trailing) Box(Modifier().padding(16, 0, 0, 0), [&] { provide(cs.onSurfaceVariant, t.labelSmall, slots.trailing); });
        }, policy);
    });
}

ListItemShapes ListItemDefaults::segmentedShapes(int index, int count, ListItemShapes base) {
    if (count <= 1) return base;
    auto corners = base.shape.radiiFor(Rect{0, 0, 1000, 1000});  // the base's corners (no percents here)
    float top = index == 0 ? 16.f : corners.topLeft, bottom = index == count - 1 ? 16.f : corners.bottomLeft;
    if (index == 0 || index == count - 1) base.shape = RoundedCornerShape(top, top, bottom, bottom);
    return base;
}

namespace {
enum class ListItemKind { Click, Select, Toggle };

// Compose's InteractiveListItem: the state's shape (morphing) and colors, the row taking the tap.
void interactiveListItem(ListItemKind kind, bool selected, std::function<void()> onClick, std::function<void(bool)> onCheckedChange,
                         const ListItemSlots& slots, const Modifier& modifier, const InteractiveListItemOptions& o, const CallSite& site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    const Typography& t = typography();
    auto own = remember([] { return MutableInteractionSource(); });
    MutableInteractionSource source = o.interactionSource.value_or(own);
    bool pressed = source.collectIsPressedAsState(), focused = source.collectIsFocusedAsState(), hovered = source.collectIsHoveredAsState();
    ListItemShapes shapes = o.shapes.value_or(ListItemDefaults::shapes());
    Shape shape = detail::morphingShape(pressed    ? shapes.pressedShape
                                        : focused  ? shapes.focusedShape
                                        : selected ? shapes.selectedShape
                                        : hovered  ? shapes.hoveredShape
                                                   : shapes.shape);
    const ListItemColors& c = o.colors;
    Color faded = cs.onSurface.copy(0.38f);
    Color container = animateColorAsState(selected ? c.selectedContainerColor.value_or(cs.secondaryContainer) : c.containerColor.value_or(cs.surface), 200);
    Color content = !o.enabled ? faded : selected ? c.selectedContentColor.value_or(cs.onSecondaryContainer) : c.contentColor.value_or(cs.onSurface);
    auto quiet = [&](const std::optional<Color>& color) {
        return !o.enabled ? faded : selected ? c.selectedContentColor.value_or(cs.onSecondaryContainer) : color.value_or(cs.onSurfaceVariant);
    };
    ClickableOptions click{.enabled = o.enabled, .interactionSource = source, .onLongClick = o.onLongClick};
    Modifier m = modifier.fillMaxWidth().heightIn(56).minimumInteractiveComponentSize().background(container, shape).clip(shape);
    m = kind == ListItemKind::Toggle ? m.toggleable(selected, onCheckedChange, SemanticsRole::Checkbox, click)
        : kind == ListItemKind::Select ? m.selectable(selected, onClick, SemanticsRole::None, click)
                                       : m.clickable(onClick, click);
    int lines = 1 + (slots.overline ? 1 : 0) + (slots.supporting ? 1 : 0);
    Row(m.padding(16, 10, 16, 10),
        {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = lines == 3 ? Alignment::Top : Alignment::CenterVertically},
        [&] {
            provide(quiet(c.leadingContentColor), t.bodyLarge, slots.leading);
            Column(Modifier().weight(1), {.verticalArrangement = Arrangement::Center}, [&] {
                provide(quiet(std::nullopt), t.labelSmall, slots.overline);
                provide(content, t.bodyLarge, slots.headline);
                provide(quiet(c.supportingContentColor), t.bodyMedium, slots.supporting);
            });
            provide(quiet(c.trailingContentColor), t.labelSmall, slots.trailing);
        });
}
}  // namespace

void ListItem(std::function<void()> onClick, const ListItemSlots& slots, const Modifier& modifier, const InteractiveListItemOptions& options,
              CallSite site) {
    interactiveListItem(ListItemKind::Click, false, std::move(onClick), {}, slots, modifier, options, site);
}
void ListItem(bool selected, std::function<void()> onClick, const ListItemSlots& slots, const Modifier& modifier,
              const InteractiveListItemOptions& options, CallSite site) {
    interactiveListItem(ListItemKind::Select, selected, std::move(onClick), {}, slots, modifier, options, site);
}
void ListItem(bool checked, std::function<void(bool)> onCheckedChange, const ListItemSlots& slots, const Modifier& modifier,
              const InteractiveListItemOptions& options, CallSite site) {
    interactiveListItem(ListItemKind::Toggle, checked, {}, std::move(onCheckedChange), slots, modifier, options, site);
}
void SegmentedListItem(std::function<void()> onClick, const ListItemShapes& shapes, const ListItemSlots& slots, const Modifier& modifier,
                       InteractiveListItemOptions options, CallSite site) {
    options.shapes = shapes;
    interactiveListItem(ListItemKind::Click, false, std::move(onClick), {}, slots, modifier, options, site);
}
void SegmentedListItem(bool selected, std::function<void()> onClick, const ListItemShapes& shapes, const ListItemSlots& slots,
                       const Modifier& modifier, InteractiveListItemOptions options, CallSite site) {
    options.shapes = shapes;
    interactiveListItem(ListItemKind::Select, selected, std::move(onClick), {}, slots, modifier, options, site);
}

void AssistChip(std::function<void()> onClick, const Content& label, const ChipOptions& options,
                const Modifier& modifier, CallSite site) {
    chip(ChipKind::Assist, false, std::move(onClick), label, options, modifier, site);
}
void SuggestionChip(std::function<void()> onClick, const Content& label, const ChipOptions& options,
                    const Modifier& modifier, CallSite site) {
    chip(ChipKind::Suggestion, false, std::move(onClick), label, options, modifier, site);
}
void FilterChip(bool selected, std::function<void()> onClick, const Content& label, const ChipOptions& options,
                const Modifier& modifier, CallSite site) {
    chip(ChipKind::Filter, selected, std::move(onClick), label, options, modifier, site);
}
void InputChip(bool selected, std::function<void()> onClick, const Content& label, const ChipOptions& options,
               const Modifier& modifier, CallSite site) {
    chip(ChipKind::Input, selected, std::move(onClick), label, options, modifier, site);
}
void ElevatedAssistChip(std::function<void()> onClick, const Content& label, const ChipOptions& options,
                        const Modifier& modifier, CallSite site) {
    chip(ChipKind::Assist, false, std::move(onClick), label, options, modifier, site, true);
}
void ElevatedSuggestionChip(std::function<void()> onClick, const Content& label, const ChipOptions& options,
                            const Modifier& modifier, CallSite site) {
    chip(ChipKind::Suggestion, false, std::move(onClick), label, options, modifier, site, true);
}
void ElevatedFilterChip(bool selected, std::function<void()> onClick, const Content& label, const ChipOptions& options,
                        const Modifier& modifier, CallSite site) {
    chip(ChipKind::Filter, selected, std::move(onClick), label, options, modifier, site, true);
}

void RadioButton(bool selected, std::function<void()> onClick, const Modifier& modifier, bool enabled,
                 std::optional<MutableInteractionSource> interactionSource, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    // Compose 1.4's: the color by the default effects spring (disabled: the surface's text faded,
    // at once), the dot's radius by the fast spatial one.
    Color target = enabled ? (selected ? cs.primary : cs.onSurfaceVariant) : cs.onSurface.copy(0.38f);
    Color ring = enabled ? animateColorAsState(target, motionScheme().defaultEffectsSpec) : target;
    float dot = animateFloatAsState(selected ? 6.f : 0.f, motionScheme().fastSpatialSpec);
    // 24 dp; one that takes taps itself (not a row around it) gets 48 of room and a 40 dp ripple.
    Modifier m = modifier.semantics({.role = SemanticsRole::RadioButton, .checked = selected});
    if (onClick)
        m = m.minimumInteractiveComponentSize().clickable(
            std::move(onClick), {.enabled = enabled, .interactionSource = std::move(interactionSource), .indication = ripple(false, 20)});
    Box(m.size(24)
            .drawBehind([ring, dot](DrawScope& d) {
                d.drawCircle(ring, 9.f, {}, 1, Stroke(2));  // 20 dp
                if (dot > 1) d.drawCircle(ring, dot - 1);
            }));
}

namespace {
// A slider press (and the drag it turns into) told to an interaction source; ended as
// cancelled if the slider leaves meanwhile (the input coroutine's frame goes with it).
struct SliderTouch {
    std::optional<MutableInteractionSource> source;
    std::optional<Interaction> press, drag;
    void pressed(std::optional<MutableInteractionSource> to, Offset position) {
        source = std::move(to);
        if (source) source->tryEmit(*(press = Interaction::press(position)));
    }
    void dragged() {
        if (source && press && !drag) source->tryEmit(*(drag = Interaction::dragStart()));
    }
    void end(bool released) {
        if (source && drag) source->tryEmit(released ? drag->stop() : drag->cancel());
        if (source && press) source->tryEmit(released ? press->release() : press->cancel());
        press.reset(), drag.reset();
    }
    ~SliderTouch() { end(false); }
};

// Where `v` is between `from` and `to`, 0..1; a fraction moved to the nearest of the stops
// `steps` makes between the ends (none: as it is); and those stops, ends included.
float fractionOf(float v, float from, float to) {
    float span = to - from;
    return span > 0 ? std::clamp((v - from) / span, 0.f, 1.f) : 0.f;
}
float snapToSteps(float f, int steps) { return steps > 0 ? std::round(f * float(steps + 1)) / float(steps + 1) : f; }
std::vector<float> stepFractions(int steps) {
    std::vector<float> ticks;
    for (int k = 0; steps > 0 && k <= steps + 1; ++k) ticks.push_back(float(k) / float(steps + 1));
    return ticks;
}

// A slider's touches: a press it takes (not one taken already, nor while it is disabled), then
// the drag that press turns into once it goes past the touch slop along the slider (`moved`:
// how far along so far), until the finger lifts (`released`) or the gesture is called off.
struct SliderGesture {
    std::function<bool()> enabled;
    std::function<void(Offset position)> press;
    std::function<void()> dragStart;
    std::function<void(Offset position, float moved)> drag;
    std::function<void(bool released)> end;
};
Task sliderGesture(PointerInputScope& scope, bool vertical, SliderGesture g) {
    while (true) {
        PointerEvent down = co_await scope.awaitPointerEvent();
        if (down.type != PointerEventType::Press || down.change.isConsumed() || !g.enabled()) continue;
        down.change.consume();
        g.press(down.change.position);
        float moved = 0;
        bool sideways = false, released = false;
        while (true) {
            PointerEvent e = co_await scope.awaitPointerEvent();
            if (e.type == PointerEventType::Release || e.type == PointerEventType::Cancel) {
                released = e.type == PointerEventType::Release;
                break;
            }
            if (e.type != PointerEventType::Move || e.change.isConsumed()) continue;
            moved += vertical ? e.change.positionChange().y : e.change.positionChange().x;
            if (!sideways && std::abs(moved) > scope.viewConfiguration().touchSlop) {
                sideways = true;
                g.dragStart();
            }
            if (!sideways) continue;
            e.change.consume();
            g.drag(e.change.position, moved);
        }
        g.end(released);
    }
}
} // namespace

struct SliderState::Data {
    MutableState<float> value = mutableStateOf(0.f);
    float valueFrom = 0, valueTo = 1;
    int steps = 0;
    std::function<void(float)> onValueChange;  // the caller's (Slider(value, ...)); unset: the value is kept
    std::function<void()> onValueChangeFinished;
    MutableState<bool> pressed = mutableStateOf(false), dragging = mutableStateOf(false);
    std::optional<SliderColors> colors;
    bool enabled = true, vertical = false, flipped = false;
    Size thumbSize;       // as laid out
    float tickInset = 0;  // the track's: its ticks (and a thumb on them) this far in from its ends
};

float SliderState::value() const { return data_->value.get(); }
void SliderState::setValue(float v) const {
    Data& d = *data_;
    float snapped = d.valueFrom + snapToSteps(fractionOf(v, d.valueFrom, d.valueTo), d.steps) * (d.valueTo - d.valueFrom);
    if (d.onValueChange) d.onValueChange(snapped);
    else d.value.set(snapped);
}
float SliderState::valueFrom() const { return data_->valueFrom; }
float SliderState::valueTo() const { return data_->valueTo; }
int SliderState::steps() const { return data_->steps; }
float SliderState::valueAsFraction() const { return fractionOf(value(), data_->valueFrom, data_->valueTo); }
std::vector<float> SliderState::tickFractions() const { return stepFractions(data_->steps); }
bool SliderState::isPressed() const { return data_->pressed.get() || data_->dragging.get(); }
bool SliderState::isDragging() const { return data_->dragging.get(); }
bool SliderState::enabled() const { return data_->enabled; }
bool SliderState::vertical() const { return data_->vertical; }
const std::optional<SliderColors>& SliderState::colors() const { return data_->colors; }

namespace {

// A slider along either axis: `flipped`, the value grows towards the left (right to left) or
// upwards (a vertical one's default). Its thumb and track are composed from its state; the
// track is as long as the slider less the thumb, the thumb's middle at the value along it.
void slider(bool vertical, bool flipped, std::shared_ptr<SliderState::Data> data, const Modifier& modifier, const SliderOptions& options) {
    data->enabled = options.enabled;
    data->colors = options.colors;
    data->vertical = vertical;
    data->flipped = flipped;
    SliderState state{data};
    float fraction = state.valueAsFraction();
    // The input coroutine runs across recompositions: it reads the latest state and options.
    struct Latest {
        std::shared_ptr<SliderState::Data> data;
        SliderOptions options;
    };
    auto latest = rememberUpdatedState(Latest{data, options});
    auto along = [vertical](Offset p) { return vertical ? p.y : p.x; };
    Modifier hover = options.interactionSource && options.enabled ? Modifier().hoverable(*options.interactionSource) : Modifier();
    Modifier m = (vertical ? modifier.fillMaxHeight() : modifier.fillMaxWidth())
                     .semantics({.role = SemanticsRole::Slider,
                                 .stateDescription = std::to_string(int(std::lround(fraction * 100))) + "%",
                                 .progressBarRangeInfo = ProgressBarRangeInfo{state.value(), data->valueFrom, data->valueTo, data->steps},
                                 .setProgress = options.enabled ? std::function<bool(float)>([data](float v) {
                                     SliderState{data}.setValue(v);  // snapped to its steps
                                     if (data->onValueChangeFinished) data->onValueChangeFinished();
                                     return true;
                                 }) : nullptr})
                     .then(hover)
                     .pointerInput(0, [latest, vertical, along](PointerInputScope& scope) -> Task {
                         auto set = [&](Offset p) {
                             const SliderState::Data& d = *latest.get().data;
                             float thumb = vertical ? d.thumbSize.height : d.thumbSize.width;
                             float length = (vertical ? scope.size().height : scope.size().width) - thumb;
                             float f = length > 0 ? std::clamp((along(p) - thumb / 2) / length, 0.f, 1.f) : 0.f;
                             if (d.flipped) f = 1 - f;
                             SliderState{latest.get().data}.setValue(d.valueFrom + f * (d.valueTo - d.valueFrom));
                         };
                         SliderTouch touch;
                         co_await sliderGesture(scope, vertical, {
                             .enabled = [&] { return latest.get().options.enabled; },
                             .press = [&](Offset p) {
                                 latest.get().data->pressed.set(true);
                                 touch.pressed(latest.get().options.interactionSource, p);
                                 set(p);
                             },
                             .dragStart = [&] {
                                 latest.get().data->dragging.set(true);
                                 touch.dragged();
                             },
                             .drag = [&](Offset p, float) { set(p); },
                             .end = [&](bool released) {
                                 touch.end(released);
                                 latest.get().data->pressed.set(false);
                                 latest.get().data->dragging.set(false);
                                 if (latest.get().data->onValueChangeFinished) latest.get().data->onValueChangeFinished();
                             }});
                     })
                     .minimumInteractiveComponentSize();
    auto policy = [data, vertical, flipped, fraction](const std::vector<Measurable>& children, const Constraints& c) {
        Placeable thumb = children[1].measure({0, c.maxWidth, 0, c.maxHeight});  // drawn over the track
        data->thumbSize = {float(thumb.width), float(thumb.height)};
        float thumbAlong = vertical ? thumb.height : thumb.width, thumbAcross = vertical ? thumb.width : thumb.height;
        float length = vertical ? c.maxHeight : c.maxWidth;
        if (length >= kInfinity) length = 200;  // unbounded: a default length
        float trackLength = std::max(0.f, length - thumbAlong);
        Placeable track = vertical ? children[0].measure({0, c.maxWidth, trackLength, trackLength})
                                   : children[0].measure({trackLength, trackLength, 0, c.maxHeight});
        float trackAcross = vertical ? track.width : track.height;
        float across = std::max(thumbAcross, trackAcross);  // and 48 dp to touch around it
        // On a step (not an end) the thumb stands on its tick, which the track may hold in from its ends.
        bool onEnd = fraction <= 0 || fraction >= 1;
        float inset = data->steps > 0 && !onEnd ? data->tickInset : 0;
        float f = flipped ? 1 - fraction : fraction;
        float at = inset + (trackLength - 2 * inset) * f;  // the thumb's middle, from the track's start
        if (vertical) {
            track.place((across - track.width) / 2, thumbAlong / 2);
            thumb.place((across - thumb.width) / 2, at);
            return c.constrain({across, length});
        }
        track.place(thumbAlong / 2, (across - track.height) / 2);
        thumb.place(at, (across - thumb.height) / 2);
        return c.constrain({length, across});
    };
    Layout(m, [&] {
        Box(Modifier(), [&] {
            if (options.track) options.track(state);
            else SliderDefaults::ClassicTrack(state);
        });
        Box(Modifier(), [&] {
            if (options.thumb) options.thumb(state);
            else SliderDefaults::CircleThumb(state);
        });
    }, policy);
}

// A Slider(value, onValueChange)'s state: made each time, its held flags kept.
std::shared_ptr<SliderState::Data> sliderData(float value, std::function<void(float)> onValueChange, const SliderOptions& options) {
    auto pressed = remember([] { return mutableStateOf(false); });
    auto dragging = remember([] { return mutableStateOf(false); });
    auto d = std::make_shared<SliderState::Data>();
    d->value = mutableStateOf(value);
    d->valueFrom = options.valueFrom;
    d->valueTo = options.valueTo;
    d->steps = std::max(0, options.steps);
    d->onValueChange = std::move(onValueChange);
    d->onValueChangeFinished = options.onValueChangeFinished;
    d->pressed = pressed;
    d->dragging = dragging;
    return d;
}

} // namespace

void Slider(float value, std::function<void(float)> onValueChange, const Modifier& modifier, SliderOptions options, CallSite site) {
    ComposableScope scope(site);
    // Right to left the track starts at the right.
    slider(false, LocalLayoutDirection.current() == LayoutDirection::Rtl, sliderData(value, std::move(onValueChange), options), modifier,
           options);
}

void Slider(const SliderState& state, const Modifier& modifier, SliderOptions options, CallSite site) {
    ComposableScope scope(site);
    slider(false, LocalLayoutDirection.current() == LayoutDirection::Rtl, state.data_, modifier, options);
}

SliderState rememberSliderState(float value, int steps, float valueFrom, float valueTo, std::function<void()> onValueChangeFinished,
                                CallSite site) {
    auto data = remember([&] {
        auto d = std::make_shared<SliderState::Data>();
        d->valueFrom = valueFrom;
        d->valueTo = valueTo;
        d->steps = std::max(0, steps);
        d->value = mutableStateOf(value);
        return d;
    }, site);
    data->onValueChangeFinished = std::move(onValueChangeFinished);
    return SliderState{data};
}

void VerticalSlider(float value, std::function<void(float)> onValueChange, const Modifier& modifier, SliderOptions options,
                    bool reverseDirection, CallSite site) {
    ComposableScope scope(site);
    slider(true, !reverseDirection, sliderData(value, std::move(onValueChange), options), modifier, options);
}

namespace SliderDefaults {

SliderColors colors() {
    const ColorScheme& cs = colorScheme();
    return {cs.primary, cs.primary, cs.secondaryContainer, cs.secondaryContainer, cs.primary,
            cs.onSurface.copy(0.38f), cs.onSurface.copy(0.38f), cs.onSurface.copy(0.12f), cs.onSurface.copy(0.12f), cs.onSurface.copy(0.38f)};
}

SliderColors classicColors() {
    const ColorScheme& cs = colorScheme();
    return {cs.primary, cs.primary, cs.onPrimary, cs.surfaceContainerHighest, cs.onSurfaceVariant,
            cs.onSurface.copy(0.38f), cs.onSurface.copy(0.38f), cs.onPrimary, cs.onSurface.copy(0.12f), cs.onSurfaceVariant};
}

void CircleThumb(const SliderState& state, float radius, CallSite site) {
    ComposableScope scope(site);
    Color c = state.colors().value_or(classicColors()).thumb(state.enabled());
    bool halo = state.isPressed();
    Box(Modifier().size(2 * radius).drawBehind([c, halo, radius](DrawScope& d) {
        if (halo) d.drawCircle(c.copy(0.12f), 2 * radius);
        d.drawCircle(c, radius);
    }));
}

void Thumb(const SliderState& state, Size size, CallSite site) {
    ComposableScope scope(site);
    Color c = state.colors().value_or(colors()).thumb(state.enabled());
    float thick = state.isPressed() ? size.width / 2 : size.width;  // across the bar
    Box(state.vertical() ? Modifier().size(size.height, thick).background(c, CircleShape) : Modifier().size(thick, size.height).background(c, CircleShape));
}

void ShapeThumb(const SliderState& state, Shape shape, Size size, CallSite site) {
    ComposableScope scope(site);
    Color c = state.colors().value_or(colors()).thumb(state.enabled());
    float grow = animateFloatAsState(state.isPressed() ? 1.15f : 1.f, 120);
    Box(Modifier().size(size.width, size.height).scale(grow).background(c, shape));
}

void IconThumb(const SliderState& state, const ImageVector& icon, float size, CallSite site) {
    ComposableScope scope(site);
    Color c = state.colors().value_or(colors()).thumb(state.enabled());
    float grow = animateFloatAsState(state.isPressed() ? 1.1f : 1.f, 120);
    Box(Modifier().size(size).scale(grow).background(c, CircleShape), Alignment::Center,
        [&] { Icon(icon, "", Modifier().size(size * 0.6f), colorScheme().onPrimary); });
}

void ValueThumb(const SliderState& state, std::function<std::string(float)> label, CallSite site) {
    ComposableScope scope(site);
    Color c = state.colors().value_or(colors()).thumb(state.enabled());
    std::string text = label ? label(state.value()) : std::format("{:.0f}", state.value());
    float grow = animateFloatAsState(state.isPressed() ? 1.1f : 1.f, 120);
    Box(Modifier().heightIn(28).widthIn(40).scale(grow).background(c, RoundedCornerShape(14)).padding(8, 4), Alignment::Center,
        [&] { Text(text, {.font = typography().labelMedium, .color = colorScheme().onPrimary}); });
}

void ClassicTrack(const SliderState& state, CallSite site) {
    ComposableScope scope(site);
    SliderColors colors = state.colors().value_or(classicColors());
    bool enabled = state.enabled(), vertical = state.vertical(), flipped = state.data_->flipped;
    float fraction = state.valueAsFraction();
    std::vector<float> ticks = state.tickFractions();
    state.data_->tickInset = 0;
    Box((vertical ? Modifier().width(4).fillMaxHeight() : Modifier().fillMaxWidth().height(4))
            .drawBehind([=](DrawScope& d) {
                float length = vertical ? d.size().height : d.size().width;
                auto at = [&](float f) { return flipped ? length * (1 - f) : length * f; };
                auto bar = [&](Color col, float from, float to) {
                    float lo = std::min(from, to), hi = std::max(from, to);
                    d.drawRoundRect(col, vertical ? Offset{0, lo} : Offset{lo, 0}, vertical ? Size{4, hi - lo} : Size{hi - lo, 4}, CornerRadius(2));
                };
                bar(colors.track(enabled, false), 0, length);
                bar(colors.track(enabled, true), at(0), at(fraction));
                for (float f : ticks) {
                    Offset p = vertical ? Offset{2, at(f)} : Offset{at(f), 2};
                    d.drawCircle(colors.tick(enabled, f <= fraction), 1.f, p);
                }
            }));
}

void Track(const SliderState& state, bool drawStopIndicator, float thumbTrackGapSize, float trackInsideCornerSize, CallSite site) {
    ComposableScope scope(site);
    SliderColors colors = state.colors().value_or(SliderDefaults::colors());
    bool enabled = state.enabled(), vertical = state.vertical(), flipped = state.data_->flipped;
    float fraction = state.valueAsFraction();
    std::vector<float> ticks = state.tickFractions();
    auto data = state.data_;
    data->tickInset = TrackHeight / 2;
    Box((vertical ? Modifier().width(TrackHeight).fillMaxHeight() : Modifier().fillMaxWidth().height(TrackHeight))
            .drawBehind([=](DrawScope& d) {
                // Along the track from its start (flipped: from the other end), as Compose's drawTrack.
                float length = vertical ? d.size().height : d.size().width, corner = TrackHeight / 2, inside = trackInsideCornerSize;
                bool onEnd = fraction <= 0 || fraction >= 1;
                float value = !ticks.empty() && !onEnd ? corner + (length - 2 * corner) * fraction : length * fraction;
                float thumb = vertical ? data->thumbSize.height : data->thumbSize.width;
                float gap = thumbTrackGapSize > 0 ? thumb / 2 + thumbTrackGapSize : 0;
                Rect b = d.bounds();
                // A stretch [from, to) along it, its start and end corners.
                auto stretch = [&](float from, float to, float startCorner, float endCorner, Color col) {
                    float lo = flipped ? length - to : from, hi = flipped ? length - from : to;
                    float a = flipped ? endCorner : startCorner, z = flipped ? startCorner : endCorner;  // the corners at lo and hi
                    Rect r = vertical ? Rect{b.x, b.y + lo, b.width, hi - lo} : Rect{b.x + lo, b.y, hi - lo, b.height};
                    CornerRadii radii = vertical ? CornerRadii(a, a, z, z) : CornerRadii(a, z, z, a);
                    d.context().fillRoundRect(r, radii, col);
                };
                auto dot = [&](float along, float size, Color col) {
                    float x = flipped ? length - along : along;
                    d.drawCircle(col, size / 2, vertical ? Offset{d.size().width / 2, x} : Offset{x, d.size().height / 2});
                };
                if (value < length - gap - corner) {  // the inactive stretch, a stop dot at its end
                    stretch(value + gap, length, inside, corner, colors.track(enabled, false));
                    if (drawStopIndicator) dot(length - corner, TrackStopIndicatorSize, colors.track(enabled, true));
                }
                float activeEnd = value - gap;
                if (activeEnd > corner) stretch(0, activeEnd, corner, inside, colors.track(enabled, true));
                for (size_t i = 0; i < ticks.size(); ++i) {
                    if (drawStopIndicator && i + 1 == ticks.size()) continue;  // the stop dot is there
                    float t = corner + (length - 2 * corner) * ticks[i];
                    if (t >= value - gap && t <= value + gap) continue;  // in the gap around the thumb
                    dot(t, TickSize, colors.tick(enabled, t <= activeEnd));
                }
            }));
}

} // namespace SliderDefaults

struct RangeSliderState::Data {
    MutableState<std::pair<float, float>> range = mutableStateOf(std::pair{0.f, 1.f});
    float valueFrom = 0, valueTo = 1;
    int steps = 0;
    std::function<void(std::pair<float, float>)> onValueChange;  // the caller's; unset: the range is kept
    std::function<void()> onValueChangeFinished;
    MutableState<int> held = mutableStateOf(-1);
    MutableState<bool> dragging = mutableStateOf(false);
    std::optional<SliderColors> colors;
    bool enabled = true, flipped = false;
    Size startThumbSize, endThumbSize;  // as laid out
    float tickInset = 0;
};

namespace {
float rangeFraction(const RangeSliderState::Data& d, float v) { return fractionOf(v, d.valueFrom, d.valueTo); }
} // namespace

float RangeSliderState::activeRangeStart() const { return data_->range.get().first; }
float RangeSliderState::activeRangeEnd() const { return data_->range.get().second; }
void RangeSliderState::setActiveRange(float start, float end) const {
    Data& d = *data_;
    float span = d.valueTo - d.valueFrom;
    float s = d.valueFrom + snapToSteps(rangeFraction(d, start), d.steps) * span;
    float e = std::max(s, d.valueFrom + snapToSteps(rangeFraction(d, end), d.steps) * span);
    if (d.onValueChange) d.onValueChange({s, e});
    else d.range.set({s, e});
}
float RangeSliderState::valueFrom() const { return data_->valueFrom; }
float RangeSliderState::valueTo() const { return data_->valueTo; }
int RangeSliderState::steps() const { return data_->steps; }
float RangeSliderState::startFraction() const { return rangeFraction(*data_, activeRangeStart()); }
float RangeSliderState::endFraction() const { return std::max(startFraction(), rangeFraction(*data_, activeRangeEnd())); }
std::vector<float> RangeSliderState::tickFractions() const { return stepFractions(data_->steps); }
int RangeSliderState::held() const { return data_->held.get(); }
bool RangeSliderState::enabled() const { return data_->enabled; }
const std::optional<SliderColors>& RangeSliderState::colors() const { return data_->colors; }

namespace {
// One thumb of a range as a slider's state: its own value, held while it is the one moved.
SliderState rangeThumb(const RangeSliderState& range, int thumb) {
    const RangeSliderState::Data& r = *range.data_;
    auto d = std::make_shared<SliderState::Data>();
    d->value = mutableStateOf(thumb == 0 ? range.activeRangeStart() : range.activeRangeEnd());
    d->valueFrom = r.valueFrom;
    d->valueTo = r.valueTo;
    d->steps = r.steps;
    d->pressed = mutableStateOf(range.held() == thumb);
    d->dragging = mutableStateOf(range.held() == thumb && r.dragging.get());
    d->colors = r.colors;
    d->enabled = r.enabled;
    d->flipped = r.flipped;
    d->thumbSize = thumb == 0 ? r.startThumbSize : r.endThumbSize;
    return SliderState{d};
}
} // namespace

SliderState RangeSliderState::startThumb() const { return rangeThumb(*this, 0); }
SliderState RangeSliderState::endThumb() const { return rangeThumb(*this, 1); }

namespace {

// Two thumbs on one track: the track starts half the start thumb in (the end thumb, right to
// left) and is as long as the slider less half of each; a press takes the nearer thumb (with
// both together, the way the drag goes decides), and they don't pass each other.
void rangeSlider(bool flipped, std::shared_ptr<RangeSliderState::Data> data, const Modifier& modifier, const SliderOptions& options) {
    data->enabled = options.enabled;
    data->colors = options.colors;
    data->flipped = flipped;
    RangeSliderState state{data};
    float from = state.startFraction(), to = state.endFraction();
    struct Latest {
        std::shared_ptr<RangeSliderState::Data> data;
        SliderOptions options;
    };
    auto latest = rememberUpdatedState(Latest{data, options});
    auto percent = [](float f) { return std::to_string(int(std::lround(f * 100))) + "%"; };
    // The track's start and length along the slider of `width`.
    auto trackOf = [](const RangeSliderState::Data& d, float width) {
        float lead = (d.flipped ? d.endThumbSize.width : d.startThumbSize.width) / 2;
        return std::pair{lead, std::max(0.f, width - (d.startThumbSize.width + d.endThumbSize.width) / 2)};
    };
    Modifier m = modifier.fillMaxWidth()
                     .semantics({.role = SemanticsRole::Slider, .stateDescription = percent(from) + " – " + percent(to)})
                     .pointerInput(0, [latest, trackOf](PointerInputScope& scope) -> Task {
                         auto fractionAt = [&](float x) {
                             const RangeSliderState::Data& d = *latest.get().data;
                             auto [lead, length] = trackOf(d, scope.size().width);
                             float f = length > 0 ? std::clamp((x - lead) / length, 0.f, 1.f) : 0.f;
                             return d.flipped ? 1 - f : f;
                         };
                         // Moves `thumb` to `x`, up to the other one.
                         auto set = [&](int thumb, float x) {
                             RangeSliderState st{latest.get().data};
                             const RangeSliderState::Data& d = *latest.get().data;
                             float v = d.valueFrom + fractionAt(x) * (d.valueTo - d.valueFrom);
                             float s = st.activeRangeStart(), e = st.activeRangeEnd();
                             float ns = thumb == 0 ? std::min(v, e) : s, ne = thumb == 0 ? e : std::max(v, s);
                             float span = d.valueTo - d.valueFrom;  // where it lands: only a real change is told
                             float ss = d.valueFrom + snapToSteps(rangeFraction(d, ns), d.steps) * span;
                             float se = d.valueFrom + snapToSteps(rangeFraction(d, ne), d.steps) * span;
                             if (ss != s || se != e) st.setActiveRange(ns, ne);
                         };
                         auto thumbX = [&](float v) {
                             const RangeSliderState::Data& d = *latest.get().data;
                             auto [lead, length] = trackOf(d, scope.size().width);
                             float f = rangeFraction(d, v);
                             return lead + length * (d.flipped ? 1 - f : f);
                         };
                         // Towards the start of the track: the left, or right to left the right.
                         auto towardsStart = [&](float dx) { return latest.get().data->flipped ? dx > 0 : dx < 0; };
                         auto sourceOf = [&](int thumb) {
                             const SliderOptions& o = latest.get().options;
                             return thumb == 0 ? o.interactionSource : o.endInteractionSource;
                         };
                         SliderTouch touch;
                         int thumb = -1;  // the one held: none yet with both together
                         Offset at;       // where it was pressed
                         co_await sliderGesture(scope, false, {
                             .enabled = [&] { return latest.get().options.enabled; },
                             .press = [&](Offset p) {
                                 auto data = latest.get().data;
                                 RangeSliderState st{data};
                                 at = p;
                                 float startX = thumbX(st.activeRangeStart()), endX = thumbX(st.activeRangeEnd());
                                 float ds = std::abs(p.x - startX), de = std::abs(p.x - endX);
                                 thumb = std::abs(ds - de) < 0.5f ? (towardsStart(p.x - startX) ? 0 : towardsStart(endX - p.x) ? 1 : -1)
                                                                  : ds < de ? 0 : 1;
                                 if (thumb >= 0) {
                                     data->held.set(thumb);
                                     touch.pressed(sourceOf(thumb), at);
                                     set(thumb, p.x);
                                 }
                             },
                             .dragStart = [&] { latest.get().data->dragging.set(true); },
                             .drag = [&](Offset p, float moved) {
                                 if (thumb < 0) {  // both together: the drag's direction picks one
                                     thumb = towardsStart(moved) ? 0 : 1;
                                     latest.get().data->held.set(thumb);
                                     touch.pressed(sourceOf(thumb), at);
                                 }
                                 touch.dragged();
                                 set(thumb, p.x);
                             },
                             .end = [&](bool released) {
                                 touch.end(released);
                                 latest.get().data->held.set(-1);
                                 latest.get().data->dragging.set(false);
                                 if (latest.get().data->onValueChangeFinished) latest.get().data->onValueChangeFinished();
                             }});
                     })
                     .minimumInteractiveComponentSize();
    auto policy = [data, trackOf, from, to](const std::vector<Measurable>& children, const Constraints& c) {
        Placeable start = children[1].measure({0, c.maxWidth, 0, c.maxHeight});
        Placeable end = children[2].measure({0, c.maxWidth, 0, c.maxHeight});
        data->startThumbSize = {float(start.width), float(start.height)};
        data->endThumbSize = {float(end.width), float(end.height)};
        float width = c.maxWidth < kInfinity ? c.maxWidth : 200;
        auto [lead, length] = trackOf(*data, width);
        Placeable track = children[0].measure({length, length, 0, c.maxHeight});
        float height = std::max({float(start.height), float(end.height), float(track.height)});  // and 48 dp to touch around it
        auto place = [&](Placeable& thumb, float f) {
            bool onEnd = f <= 0 || f >= 1;
            float inset = data->steps > 0 && !onEnd ? data->tickInset : 0;
            float along = inset + (length - 2 * inset) * (data->flipped ? 1 - f : f);
            thumb.place(lead + along - thumb.width / 2.f, (height - thumb.height) / 2);
        };
        track.place(lead, (height - track.height) / 2);
        place(start, from);
        place(end, to);
        return c.constrain({width, height});
    };
    SliderState startThumb = state.startThumb(), endThumb = state.endThumb();
    Layout(m, [&] {
        Box(Modifier(), [&] {
            if (options.rangeTrack) options.rangeTrack(state);
            else SliderDefaults::ClassicTrack(state);
        });
        auto thumb = [&](const std::function<void(const SliderState&)>& own, const SliderState& s) {
            Box(Modifier(), [&] {
                if (own) own(s);
                else if (options.thumb) options.thumb(s);
                else SliderDefaults::CircleThumb(s);
            });
        };
        thumb(options.startThumb, startThumb);
        thumb(options.endThumb, endThumb);
    }, policy);
}

std::shared_ptr<RangeSliderState::Data> rangeSliderData(std::pair<float, float> value, std::function<void(std::pair<float, float>)> onValueChange,
                                                         const SliderOptions& options) {
    auto held = remember([] { return mutableStateOf(-1); });
    auto dragging = remember([] { return mutableStateOf(false); });
    auto d = std::make_shared<RangeSliderState::Data>();
    d->range = mutableStateOf(value);
    d->valueFrom = options.valueFrom;
    d->valueTo = options.valueTo;
    d->steps = std::max(0, options.steps);
    d->onValueChange = std::move(onValueChange);
    d->onValueChangeFinished = options.onValueChangeFinished;
    d->held = held;
    d->dragging = dragging;
    return d;
}

} // namespace

void RangeSlider(std::pair<float, float> value, std::function<void(std::pair<float, float>)> onValueChange, const Modifier& modifier,
                 SliderOptions options, CallSite site) {
    ComposableScope scope(site);
    rangeSlider(LocalLayoutDirection.current() == LayoutDirection::Rtl, rangeSliderData(value, std::move(onValueChange), options), modifier,
                options);
}

void RangeSlider(const RangeSliderState& state, const Modifier& modifier, SliderOptions options, CallSite site) {
    ComposableScope scope(site);
    rangeSlider(LocalLayoutDirection.current() == LayoutDirection::Rtl, state.data_, modifier, options);
}

RangeSliderState rememberRangeSliderState(float activeRangeStart, float activeRangeEnd, int steps, float valueFrom, float valueTo,
                                          std::function<void()> onValueChangeFinished, CallSite site) {
    auto data = remember([&] {
        auto d = std::make_shared<RangeSliderState::Data>();
        d->valueFrom = valueFrom;
        d->valueTo = valueTo;
        d->steps = std::max(0, steps);
        d->range = mutableStateOf(std::pair{activeRangeStart, std::max(activeRangeStart, activeRangeEnd)});
        return d;
    }, site);
    data->onValueChangeFinished = std::move(onValueChangeFinished);
    return RangeSliderState{data};
}

namespace SliderDefaults {

void ClassicTrack(const RangeSliderState& state, CallSite site) {
    ComposableScope scope(site);
    SliderColors colors = state.colors().value_or(classicColors());
    bool enabled = state.enabled(), flipped = state.data_->flipped;
    float from = state.startFraction(), to = state.endFraction();
    std::vector<float> ticks = state.tickFractions();
    state.data_->tickInset = 0;
    Box(Modifier().fillMaxWidth().height(4).drawBehind([=](DrawScope& d) {
        float length = d.size().width;
        auto at = [&](float f) { return flipped ? length * (1 - f) : length * f; };
        d.drawRoundRect(colors.track(enabled, false), {}, Size{length, 4}, CornerRadius(2));
        float a = at(from), b = at(to);
        d.drawRect(colors.track(enabled, true), {std::min(a, b), 0}, Size{std::abs(b - a), 4});
        for (float f : ticks) d.drawCircle(colors.tick(enabled, f >= from && f <= to), 1.f, Offset{at(f), 2});
    }));
}

void Track(const RangeSliderState& state, bool drawStopIndicator, float thumbTrackGapSize, float trackInsideCornerSize, CallSite site) {
    ComposableScope scope(site);
    SliderColors colors = state.colors().value_or(SliderDefaults::colors());
    bool enabled = state.enabled(), flipped = state.data_->flipped;
    float from = state.startFraction(), to = state.endFraction();
    std::vector<float> ticks = state.tickFractions();
    auto data = state.data_;
    data->tickInset = TrackHeight / 2;
    Box(Modifier().fillMaxWidth().height(TrackHeight).drawBehind([=](DrawScope& d) {
        // As Compose's drawTrack for a range: inactive on both sides, each apart from its thumb.
        float length = d.size().width, corner = TrackHeight / 2, inside = trackInsideCornerSize;
        auto valueAt = [&](float f) { return !ticks.empty() && f > 0 && f < 1 ? corner + (length - 2 * corner) * f : length * f; };
        float vs = valueAt(from), ve = valueAt(to);
        float startGap = thumbTrackGapSize > 0 ? data->startThumbSize.width / 2 + thumbTrackGapSize : 0;
        float endGap = thumbTrackGapSize > 0 ? data->endThumbSize.width / 2 + thumbTrackGapSize : 0;
        Rect b = d.bounds();
        auto stretch = [&](float lo0, float hi0, float startCorner, float endCorner, Color col) {
            float lo = flipped ? length - hi0 : lo0, hi = flipped ? length - lo0 : hi0;
            float a = flipped ? endCorner : startCorner, z = flipped ? startCorner : endCorner;
            d.context().fillRoundRect({b.x + lo, b.y, hi - lo, b.height}, CornerRadii(a, z, z, a), col);
        };
        auto dot = [&](float along, float size, Color col) {
            d.drawCircle(col, size / 2, Offset{flipped ? length - along : along, d.size().height / 2});
        };
        if (vs > startGap + corner) {  // before the start thumb, a stop dot at the track's start
            stretch(0, vs - startGap, corner, inside, colors.track(enabled, false));
            if (drawStopIndicator) dot(corner, TrackStopIndicatorSize, colors.track(enabled, true));
        }
        if (ve < length - endGap - corner) {  // after the end thumb, a stop dot at its end
            stretch(ve + endGap, length, inside, corner, colors.track(enabled, false));
            if (drawStopIndicator) dot(length - corner, TrackStopIndicatorSize, colors.track(enabled, true));
        }
        float activeStart = vs + startGap, activeEnd = ve - endGap;
        if (activeEnd - activeStart > inside) stretch(activeStart, activeEnd, inside, inside, colors.track(enabled, true));
        for (size_t i = 0; i < ticks.size(); ++i) {
            if (drawStopIndicator && (i == 0 || i + 1 == ticks.size())) continue;  // the stop dots are there
            float t = corner + (length - 2 * corner) * ticks[i];
            if ((t >= vs - startGap && t <= vs + startGap) || (t >= ve - endGap && t <= ve + endGap)) continue;  // in a gap
            dot(t, TickSize, colors.tick(enabled, t >= activeStart && t <= activeEnd));
        }
    }));
}

} // namespace SliderDefaults

namespace {

// The rows: the outline goes over the clipped segments, each of which draws its own separator.
// Segments drawn 40 dp tall, in 48 dp of room to touch them (LocalMinimumInteractiveComponentSize):
// the row is that tall, its outline and rounded ends on the band in the middle, and each segment
// takes taps all the way up and down.
constexpr float kSegmentHeight = 40;
Rect segmentBand(Size size) {
    float h = std::min(kSegmentHeight, size.height);
    return {0, (size.height - h) / 2, size.width, h};
}

void segmentedRow(const Modifier& modifier, const Content& content) {
    float room = std::max(kSegmentHeight, LocalMinimumInteractiveComponentSize.current());
    Color outline = colorScheme().outline;
    Row(modifier.height(room).drawWithContent([outline](ContentDrawScope& d) {
        Rect band = segmentBand(d.size()), b = d.bounds();
        band = {b.x + band.x, b.y + band.y, band.width, band.height};
        d.context().pushClipRoundRect(band, band.height / 2);
        d.drawContent();
        d.context().popClip();
        d.context().strokeRoundRect(band, band.height / 2, 1, outline);
    }), content);
}

// One segment: `click` makes it selectable (a radio button) or toggleable (a checkbox).
void segment(bool selected, const std::function<Modifier(const Modifier&)>& click, const Content& label, const Modifier& modifier,
             const SegmentedButtonOptions& options) {
    const ColorScheme& cs = colorScheme();
    const MotionScheme& motion = motionScheme();
    // Compose 1.4's: the container at once; the check fading in (the default effects spring) and
    // growing from its bottom start corner (the fast spatial one) when selected, gone at once when
    // not; room for an 18 dp icon and 8 dp kept before the label, which slides back over half of
    // it (the fast spatial spring) while there's no icon.
    Color container = selected ? cs.secondaryContainer : colors::Transparent;
    Color content = !options.enabled ? cs.onSurface.copy(0.38f) : selected ? cs.onSecondaryContainer : cs.onSurface;
    Color separator = cs.outline;
    bool icon = options.icon || selected;
    float grown = animateFloatAsState(selected ? 1.f : 0.f, selected ? motion.fastSpatialSpec : snap());
    float shown = animateFloatAsState(selected ? 1.f : 0.f, selected ? motion.defaultEffectsSpec : snap());
    float offset = animateFloatAsState(icon ? 0.f : -13.f, motion.fastSpatialSpec);
    LayoutMeasurePolicy policy = [icon, offset](const std::vector<Measurable>& m, const Constraints& c) {
        std::optional<Placeable> picture;
        if (icon) picture = m[0].measure(c.loosen());
        Placeable text = m[icon ? 1 : 0].measure(c.loosen());
        float iw = picture ? picture->width : 0, h = std::max(text.height, picture ? picture->height : 0.f);
        if (picture) picture->place(0, (h - picture->height) / 2);
        text.place(18 + 8 + offset, (h - text.height) / 2);
        return Size{std::max(18.f, iw) + 8 + text.width, h};
    };
    Box(click(modifier.weight(1).fillMaxHeight().drawBehind([container, separator](DrawScope& d) {
            Rect band = segmentBand(d.size());
            d.drawRect(container, {0, band.y}, Size{band.width, band.height});
            d.drawRect(separator, {band.width - 1, band.y}, Size{1, band.height});
        }))
            .padding(12, 0),
        Alignment::Center, [&] {
            CompositionLocalProvider({LocalContentColor.provides(content), LocalTextStyle.provides(typography().labelLarge)}, [&] {
                Layout(Modifier(), [&] {
                    if (options.icon)
                        options.icon();
                    else if (selected)
                        Icon(Icons::Filled::Check, LocalStrings.current().selected,
                             Modifier().graphicsLayer({.scaleX = grown, .scaleY = grown, .alpha = shown, .transformOrigin = {0, 1}}).size(18));
                    label();
                }, policy);
            });
        });
}

} // namespace

namespace {

// How far a button in a ButtonGroup has grown: 0..1 of the group's expandedRatio (its spring
// can overshoot a little).
struct ButtonGroupItemElement final : ModifierElement {
    float expansion = 0;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const ButtonGroupItemElement*>(&other);
        return o && o->expansion == expansion;
    }
};

} // namespace

Modifier ButtonGroupScope::animateWidth(const MutableInteractionSource& source, CallSite site) const {
    ComposableScope scope(site);
    auto item = std::make_shared<ButtonGroupItemElement>();
    item->expansion = animateFloatAsState(source.collectIsPressedAsState() ? 1.f : 0.f, motionScheme().fastSpatialSpec);
    return Modifier().then(std::move(item));
}

void ButtonGroup(const std::function<void(const ButtonGroupScope&)>& content, CallSite site) {
    ButtonGroup(Modifier(), {}, content, site);
}

void ButtonGroup(const Modifier& modifier, const ButtonGroupOptions& options, const std::function<void(const ButtonGroupScope&)>& content,
                 CallSite site) {
    ComposableScope scope(site);
    ButtonGroupScope group;
    Layout(modifier, [&] { content(group); }, [options](const std::vector<Measurable>& children, const Constraints& c) {
        size_t n = children.size();
        float spacing = options.spacing * float(n > 0 ? n - 1 : 0), used = 0;
        std::vector<Placeable> placed(n);
        std::vector<Constraints> loose(n);
        std::vector<float> natural(n), naturalHeight(n);
        float height = c.minHeight;
        {
            detail::ProbeMeasure probe;  // their own sizes first; measured for real below
            for (size_t i = 0; i < n; ++i) {
                float remaining = c.maxWidth == kInfinity ? kInfinity : std::max(0.f, c.maxWidth - used - spacing);
                loose[i] = {0, remaining, 0, c.maxHeight};
                Placeable p = children[i].measure(loose[i]);
                natural[i] = p.width, naturalHeight[i] = p.height;
                used += natural[i];
                height = std::max(height, p.height);
            }
        }
        // A held button grows; its neighbours give the room up: in whole px, each part rounded
        // as Compose's (the row a px wider or narrower for it, as there).
        std::vector<float> widths = natural;
        float d = detail::layoutDensity();
        auto px = [d](float v) { return std::floor(v + 0.5f) / d; };  // (v in px)
        for (size_t i = 0; i < n; ++i) {
            auto* item = children[i].node->modifier.find<ButtonGroupItemElement>();
            float growth = item ? natural[i] * d * options.expandedRatio * item->expansion : 0;
            bool before = i > 0, after = i + 1 < n;
            if (growth == 0 || (!before && !after)) continue;
            widths[i] += px(growth);
            if (before) widths[i - 1] -= after ? px(growth / 2) : px(growth);
            if (after) widths[i + 1] -= before ? px(growth / 2) : px(growth);
        }
        // A squeezed one keeps its height, even if its label has to wrap.
        for (size_t i = 0; i < n; ++i) {
            float w = std::max(0.f, widths[i]);
            placed[i] = children[i].measure(w != natural[i] ? Constraints{w, w, 0, naturalHeight[i]} : loose[i]);
        }
        float x = 0;
        for (const Placeable& p : placed) {
            p.place(x, (height - p.height) / 2);
            x += p.width + options.spacing;
        }
        return c.constrain({std::max(0.f, x - options.spacing), height});
    });
}

namespace {

using GroupItems = std::shared_ptr<std::vector<ButtonGroupItem>>;  // the latest, for callbacks to read when tapped

// An item as a button (a toggle button with a checked state).
void groupButton(const GroupItems& items, size_t i, const Modifier& modifier, std::optional<MutableInteractionSource> source) {
    const ButtonGroupItem& item = (*items)[i];
    Content content = [icon = item.icon, label = item.label] {
        if (icon) Icon(*icon, "", Modifier().size(18));
        Text(label);
    };
    if (item.checked)
        ToggleButton(*item.checked, [items, i](bool on) {
            if (i < items->size() && (*items)[i].onCheckedChange) (*items)[i].onCheckedChange(on);
        }, modifier, {.interactionSource = source}, content);
    else
        Button([items, i] {
            if (i < items->size() && (*items)[i].onClick) (*items)[i].onClick();
        }, modifier, {.interactionSource = source}, content);
}

// The ⋮ button and its menu of the items from `first` on.
void groupOverflow(const GroupItems& items, size_t first, const MutableState<bool>& open) {
    Box([&] {
        IconButton([open] { open.set(true); }, [] { Icon(Icons::Filled::MoreVert, LocalStrings.current().moreOptions); });
        DropdownMenu(open.get(), [open] { open.set(false); }, [&] {
            for (size_t i = first; i < items->size(); ++i) {
                const ButtonGroupItem& item = (*items)[i];
                Content leading;
                if (item.checked && *item.checked) leading = [] { Icon(Icons::Filled::Check, LocalStrings.current().selected); };
                else if (item.icon) leading = [icon = item.icon] { Icon(*icon, ""); };
                DropdownMenuItem([label = item.label] { Text(label); }, [items, i, open] {
                    open.set(false);
                    if (i >= items->size()) return;
                    const ButtonGroupItem& it = (*items)[i];
                    if (it.checked && it.onCheckedChange) it.onCheckedChange(!*it.checked);
                    else if (it.onClick) it.onClick();
                }, {.leadingIcon = leading});
            }
        });
    });
}

} // namespace

void ButtonGroup(const std::vector<ButtonGroupItem>& items, const Modifier& modifier, const ButtonGroupOptions& options, CallSite site) {
    ComposableScope scope(site);
    GroupItems latest = remember([] { return std::make_shared<std::vector<ButtonGroupItem>>(); });
    *latest = items;
    size_t look = items.size();  // what the buttons show: their slots are composed again when it changes
    for (const ButtonGroupItem& item : items)
        look = look * 1000003u ^ std::hash<std::string>{}(item.label) ^ std::hash<const void*>{}(item.icon) * 31u ^ size_t(item.checked ? 1 + *item.checked : 0);
    auto open = remember([] { return mutableStateOf(false); });
    SubcomposeLayout(modifier, [latest, look, open, options](SubcomposeMeasureScope& s, const Constraints& c) {
        size_t n = latest->size();
        // Each one's own width as a button, measured out of sight.
        std::vector<float> widths(n);
        for (size_t i = 0; i < n; ++i) {
            // Never placed: neither drawn nor told to accessibility services.
            auto measured = s.subcompose("w" + std::to_string(i), look, [latest, i] { groupButton(latest, i, Modifier(), std::nullopt); });
            detail::ProbeMeasure probe;
            for (const Measurable& m : measured) widths[i] = std::max(widths[i], m.measure({0, kInfinity, 0, c.maxHeight}).width);
        }
        float room = c.hasBoundedWidth() ? c.maxWidth : kInfinity, spacing = options.spacing;
        float all = 0;
        for (float w : widths) all += w;
        all += spacing * float(n > 0 ? n - 1 : 0);
        size_t shown = n;
        if (all > room) {  // as many as fit before the ⋮ button
            float used = 48;
            for (shown = 0; shown < n && used + widths[shown] + spacing <= room; ++shown) used += widths[shown] + spacing;
        }
        auto group = s.subcompose(std::string("group"), look * 31u + shown, [latest, shown, open, options] {
            ButtonGroup(Modifier(), options, [&](const ButtonGroupScope& g) {
                for (size_t i = 0; i < shown; ++i) {
                    auto source = remember([] { return MutableInteractionSource(); });
                    groupButton(latest, i, g.animateWidth(source), source);
                }
                if (shown < latest->size()) groupOverflow(latest, shown, open);
            });
        });
        Size size{};
        for (const Measurable& m : group) {
            Placeable p = m.measure(c);
            p.place(0, 0);
            size = {std::max(size.width, p.width), std::max(size.height, p.height)};
        }
        return c.constrain(size);
    });
}

void SingleChoiceSegmentedButtonRow(const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    segmentedRow(modifier.selectableGroup(), content);
}

void MultiChoiceSegmentedButtonRow(const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    segmentedRow(modifier, content);
}

void SegmentedButton(bool selected, std::function<void()> onClick, const Content& label, const Modifier& modifier,
                     CallSite site) {
    SegmentedButton(selected, std::move(onClick), label, modifier, SegmentedButtonOptions{}, site);
}

void SegmentedButton(bool selected, std::function<void()> onClick, const Content& label, const Modifier& modifier,
                     const SegmentedButtonOptions& options, CallSite site) {
    ComposableScope scope(site);
    segment(selected, [&](const Modifier& m) {
        return m.selectable(selected, std::move(onClick), SemanticsRole::RadioButton,
                            {.enabled = options.enabled, .interactionSource = options.interactionSource});
    }, label, modifier, options);
}

void SegmentedButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& label, const Modifier& modifier,
                     const SegmentedButtonOptions& options, CallSite site) {
    ComposableScope scope(site);
    segment(checked, [&](const Modifier& m) {
        return m.toggleable(checked, std::move(onCheckedChange), SemanticsRole::Checkbox,
                            {.enabled = options.enabled, .interactionSource = options.interactionSource});
    }, label, modifier, options);
}

namespace {

// Secondary rows show the selected tab in onSurface.
const CompositionLocal<bool> LocalSecondaryTabRow{false};

struct TabRowStyle {
    bool secondary = false, scrollable = false;
    float edgePadding = 0;
    std::optional<ScrollState> scroll;
};

// Primary indicators are as wide as the tab's content, secondary ones as the tab. Scrollable
// rows give each tab its own width; the others share the row's.
void tabRow(int selectedTabIndex, const Modifier& modifier, const Content& tabs, TabRowStyle style, const CallSite& site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    // Where each tab's indicator goes (x, width): measured by the layout, which writes it here.
    using Spans = std::vector<std::pair<float, float>>;
    auto spans = remember([] { return mutableStateOf(Spans{}); });
    auto measured = remember([] { return std::make_shared<bool>(false); });
    const Spans& s = spans.get();
    std::pair<float, float> target = selectedTabIndex >= 0 && size_t(selectedTabIndex) < s.size() ? s[size_t(selectedTabIndex)]
                                                                                                    : std::pair{0.f, 0.f};
    // The first measured position is taken as is; later ones slide.
    bool first = !*measured;
    AnimationSpec spec = first ? snap() : motionScheme().defaultSpatialSpec;  // Compose 1.4's
    float shownX = animateFloatAsState(target.first, spec), shownWidth = animateFloatAsState(target.second, spec);
    if (!s.empty()) *measured = true;
    Color indicator = cs.primary, divider = cs.outlineVariant;
    bool secondary = style.secondary, scrollable = style.scrollable;
    float edge = style.edgePadding;

    auto policy = [spans, secondary, scrollable, edge](LayoutNode& node, const Constraints& c) {
        size_t n = node.children.size();
        float d = detail::layoutDensity();  // (a tab's width whole px, as Compose's: the row's divided, the rest left over)
        float w = c.hasBoundedWidth() ? c.maxWidth : 360.f, tabWidth = n ? std::floor(w * d / float(n) + 1.f / 1024) / d : w, h = 0,
              at = scrollable ? edge : 0;
        Spans out;
        for (size_t i = 0; i < n; ++i) {
            LayoutNode* child = node.children[i];
            float content;
            {
                detail::ProbeMeasure probe;
                content = std::min(child->measure({0, kInfinity, 0, c.maxHeight}).width, scrollable ? kInfinity : tabWidth);  // (its own width)
            }
            float tw = scrollable ? std::max(90.f, content) : tabWidth;
            Size size = child->measure({tw, tw, 0, c.maxHeight});
            child->place(at, 0);
            h = std::max(h, size.height);
            float inner = std::max(24.f, content - 32);  // minus the tab's padding
            out.push_back(secondary ? std::pair{at, tw} : std::pair{at + (tw - inner) / 2, inner});
            at += tw;
        }
        Size own = scrollable ? c.constrain({at + edge, h}) : c.constrain({w, h});
        if (node.rtl)  // the tabs are mirrored after this: so are their indicators
            for (auto& [x, width] : out) x = own.width - x - width;
        if (!detail::isProbeMeasure() && out != spans.peek()) spans.set(std::move(out));
        return own;
    };
    auto drawIndicator = [=](ContentDrawScope& d) {
        d.drawContent();
        float h = d.size().height;
        if (!scrollable) d.drawRect(divider, {0, h - 1}, Size{d.size().width, 1});
        float px = d.density().density;  // (placed and sized in whole px along the way, as Compose's indicator)
        float x = std::floor(shownX * px + 0.5f) / px, width = std::floor(shownWidth * px + 0.5f) / px;
        if (width <= 0) return;
        if (secondary)  // Compose 1.4's SecondaryIndicator: 3 dp, the tab's width
            d.drawRect(indicator, {x, h - 3}, Size{width, 3});
        else
            d.drawRoundRect(indicator, {x, h - 3}, Size{width, 3}, CornerRadius(1.5f));
    };
    auto row = [&](const Modifier& m) {
        detail::Composer::require().emit(
            detail::keyOf(CallSite::current()),
            [&](LayoutNode& node) {
                node.modifier = m.drawWithContent(drawIndicator);
                node.measurePolicy = policy;
                node.drawContent = {};
            },
            [&] {
                CompositionLocalProvider({LocalSecondaryTabRow.provides(secondary), LocalContentColor.provides(secondary ? cs.onSurface : cs.primary)},
                                         tabs);
            });
    };
    if (!scrollable) return row(modifier.fillMaxWidth().background(cs.surface));

    // Scrollable: the divider spans the view, the tabs and their indicator scroll over it. A
    // newly selected tab comes into view, centered where it can be.
    ScrollState scroll = style.scroll ? *style.scroll : rememberScrollState();
    auto viewport = remember([] { return mutableStateOf(0.f); });
    float view = viewport.get();
    bool rtl = LocalLayoutDirection.current() == LayoutDirection::Rtl;  // the scroll starts at the right
    LaunchedEffect(std::tuple(selectedTabIndex, target.first, target.second, view, rtl), [scroll, target, view, first, rtl]() -> Task {
        if (view <= 0 || target.second <= 0) co_return;
        float center = target.first + target.second / 2;
        float to = std::clamp(rtl ? scroll.maxValue() + view / 2 - center : center - view / 2, 0.f, scroll.maxValue());
        if (first) co_return scroll.scrollTo(to);
        float from = scroll.value();
        int64_t start = -1;
        for (float p = 0; p < 1;) {
            int64_t now = co_await awaitFrame();
            if (start < 0) start = now;
            p = std::min(1.f, float(now - start) / 250e6f);
            scroll.scrollTo(from + (to - from) * FastOutSlowInEasing(p));
        }
    });
    Box(modifier.fillMaxWidth()
            .background(cs.surface)
            .onSizeChanged([viewport](Size size) { viewport.set(size.width); })
            .drawBehind([divider](DrawScope& d) { d.drawRect(divider, {0, d.size().height - 1}, Size{d.size().width, 1}); }),
        [&] { row(Modifier().horizontalScroll(scroll)); });
}

} // namespace

void PrimaryTabRow(int selectedTabIndex, const Modifier& modifier, const Content& tabs, CallSite site) {
    tabRow(selectedTabIndex, modifier, tabs, {}, site);
}
void SecondaryTabRow(int selectedTabIndex, const Modifier& modifier, const Content& tabs, CallSite site) {
    tabRow(selectedTabIndex, modifier, tabs, {.secondary = true}, site);
}
void PrimaryScrollableTabRow(int selectedTabIndex, const Modifier& modifier, const Content& tabs, ScrollableTabRowOptions options,
                             CallSite site) {
    tabRow(selectedTabIndex, modifier, tabs, {.scrollable = true, .edgePadding = options.edgePadding, .scroll = options.scrollState}, site);
}
void SecondaryScrollableTabRow(int selectedTabIndex, const Modifier& modifier, const Content& tabs, ScrollableTabRowOptions options,
                               CallSite site) {
    tabRow(selectedTabIndex, modifier, tabs,
           {.secondary = true, .scrollable = true, .edgePadding = options.edgePadding, .scroll = options.scrollState}, site);
}

namespace {
// A tab's content color (Compose 1.4's TabTransition): eased in by the default effects spring as
// it is selected, out by the fast one.
Color tabColor(bool selected, const TabOptions& o) {
    Color on = o.selectedContentColor.value_or(LocalContentColor.current()), off = o.unselectedContentColor.value_or(on);
    return animateColorAsState(selected ? on : off, selected ? motionScheme().defaultEffectsSpec : motionScheme().fastEffectsSpec);
}

// A tab taking the tap: selectable as a tab, its ripple in the selected color.
Modifier tabTap(const Modifier& m, bool selected, std::function<void()> onClick, const TabOptions& o) {
    Color ripplePaint = o.selectedContentColor.value_or(LocalContentColor.current());
    return m.selectable(selected, std::move(onClick), SemanticsRole::Tab,
                        {.enabled = o.enabled, .interactionSource = o.interactionSource, .indication = ripple(true, 0, ripplePaint)});
}
} // namespace

void Tab(bool selected, std::function<void()> onClick, const Content& text, const Content& icon,
         const Modifier& modifier, CallSite site) {
    Tab(selected, std::move(onClick), text, icon, modifier, {}, site);
}

void Tab(bool selected, std::function<void()> onClick, const Content& text, const Content& icon, const Modifier& modifier,
         const TabOptions& options, CallSite site) {
    ComposableScope scope(site);
    Color content = tabColor(selected, options);
    // Compose's TabBaselineLayout: the text 16 dp in from the sides; alone, it or the icon in the
    // middle of 48 dp; together in 72, the text's last baseline 17 dp (14 + the indicator's 3; 9
    // for two lines) above the bottom, the icon 20 dp above its first.
    LayoutMeasurePolicy policy = [hasText = bool(text), hasIcon = bool(icon)](const std::vector<Measurable>& m, const Constraints& c) {
        std::optional<Placeable> label, picture;
        if (hasText) label = m[0].measure({c.minWidth, c.maxWidth, 0, c.maxHeight});
        if (hasIcon) picture = m[hasText ? 1 : 0].measure(c);
        float tw = label ? label->width : 0, th = label ? label->height : 0, iw = picture ? picture->width : 0, ih = picture ? picture->height : 0;
        float width = std::max(tw, iw);
        float height = std::max(label && picture ? 72.f : 48.f, ih + th + 20);
        if (label && picture) {
            float first = (*label)[FirstBaseline], last = (*label)[LastBaseline];
            if (std::isnan(first)) first = last = th;
            float textY = height - last - ((first == last ? 14.f : 6.f) + 3);
            label->place((width - tw) / 2, textY);
            picture->place((width - iw) / 2, textY - (ih + 20 - first));
        } else if (label) {
            label->place(0, (height - th) / 2);
        } else if (picture) {
            picture->place(0, (height - ih) / 2);
        }
        return Size{width, height};
    };
    Column(tabTap(modifier, selected, std::move(onClick), options).fillMaxWidth(),
           {.verticalArrangement = Arrangement::Center, .horizontalAlignment = Alignment::CenterHorizontally}, [&] {
               CompositionLocalProvider({LocalContentColor.provides(content)}, [&] {
                   Layout(Modifier(), [&] {
                       if (text)
                           Box(Modifier().padding(16, 0), [&] {
                               CompositionLocalProvider({LocalTextStyle.provides(typography().titleSmall)}, text);
                           });
                       if (icon) Box(Modifier(), icon);
                   }, policy);
               });
           });
}

void LeadingIconTab(bool selected, std::function<void()> onClick, const Content& text, const Content& icon,
                    const Modifier& modifier, CallSite site) {
    LeadingIconTab(selected, std::move(onClick), text, icon, modifier, {}, site);
}

void LeadingIconTab(bool selected, std::function<void()> onClick, const Content& text, const Content& icon, const Modifier& modifier,
                    const TabOptions& options, CallSite site) {
    ComposableScope scope(site);
    Color content = tabColor(selected, options);
    Row(tabTap(modifier.height(48), selected, std::move(onClick), options).padding(16, 0).fillMaxWidth(),
        {.horizontalArrangement = Arrangement::Center, .verticalAlignment = Alignment::CenterVertically}, [&] {
            CompositionLocalProvider({LocalContentColor.provides(content), LocalTextStyle.provides(typography().titleSmall)}, [&] {
                if (icon) icon();
                Spacer(Modifier().width(8));
                if (text) text();
            });
        });
}

void VerticalDragHandle(const Modifier& modifier, std::optional<MutableInteractionSource> interactionSource, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    auto own = remember([] { return MutableInteractionSource(); });
    MutableInteractionSource source = interactionSource.value_or(own);
    bool held = source.collectIsPressedAsState() || source.collectIsDraggedAsState();
    AnimationSpec spec = spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMedium);
    float width = animateFloatAsState(held ? 12.f : 4.f, spec), height = animateFloatAsState(held ? 52.f : 48.f, spec);
    Color color = animateColorAsState(held ? cs.onSurface : cs.outline, 150);
    Box(modifier, Alignment::Center, [&] { Box(Modifier().size(width, height).background(color, RoundedCornerShapePercent(50))); });
}

struct SwipeToDismissBoxState::Data {
    std::function<bool(SwipeToDismissBoxValue)> confirm;
    MutableState<SwipeToDismissBoxValue> current{SwipeToDismissBoxValue::Settled};
    MutableState<SwipeToDismissBoxValue> direction{SwipeToDismissBoxValue::Settled};
    Animatable<float> offset{0.f};  // dp; read while laying out
    float width = 1;
};

SwipeToDismissBoxState::SwipeToDismissBoxState(std::function<bool(SwipeToDismissBoxValue)> confirmValueChange)
    : data_(std::make_shared<Data>()) {
    data_->confirm = std::move(confirmValueChange);
}
SwipeToDismissBoxValue SwipeToDismissBoxState::currentValue() const { return data_->current.get(); }
SwipeToDismissBoxValue SwipeToDismissBoxState::dismissDirection() const { return data_->direction.get(); }
float SwipeToDismissBoxState::progress() const {
    return std::min(1.f, std::abs(data_->offset.value()) / std::max(1.f, data_->width));
}

namespace {
// Back in place, on a spring from `velocity` (dp/s towards the end: a fling's).
Task settleSwipe(std::shared_ptr<SwipeToDismissBoxState::Data> d, float velocity = 0) {
    co_await d->offset.animateTo(0.f, spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow), velocity);
    d->current.set(SwipeToDismissBoxValue::Settled);
    d->direction.set(SwipeToDismissBoxValue::Settled);
}
constexpr float kDismissVelocity = 125;  // dp/s: a fling this fast away dismisses (Compose's velocity threshold)
} // namespace

Task SwipeToDismissBoxState::reset() const { return settleSwipe(data_); }

SavedValue Saver<SwipeToDismissBoxState>::save(const SwipeToDismissBoxState& s) {
    return Saver<SwipeToDismissBoxValue>::save(s.data_->current.peek());
}
std::optional<SwipeToDismissBoxState> Saver<SwipeToDismissBoxState>::restore(const SavedValue& v) {
    std::optional<SwipeToDismissBoxValue> value = Saver<SwipeToDismissBoxValue>::restore(v);
    if (!value) return std::nullopt;
    SwipeToDismissBoxState s;
    s.data_->current.set(*value);  // swiped away: off its edge once it knows its width
    s.data_->direction.set(*value);
    return s;
}

SwipeToDismissBoxState rememberSwipeToDismissBoxState(std::function<bool(SwipeToDismissBoxValue)> confirmValueChange,
                                                      CallSite site) {
    auto& state = rememberSaveable([&] { return SwipeToDismissBoxState(confirmValueChange); }, site);
    state.data_->confirm = std::move(confirmValueChange);  // the latest callback
    return state;
}

void SwipeToDismissBox(SwipeToDismissBoxState state, const Content& backgroundContent, const Content& content,
                       SwipeToDismissOptions options, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    auto d = state.data_;
    auto coroutines = rememberCoroutineScope();
    // How fast the drag went at its end (along the offset): the last 100 ms of it.
    auto tracker = remember([] { return std::make_shared<std::pair<VelocityTracker, float>>(); });
    auto onDragEnd = [d, coroutines, tracker] {
        float w = std::max(1.f, d->width), x = d->offset.value(), v = tracker->first.calculateVelocity().x;
        // As Compose's anchors decide: flung, the next one that way (off the edge, or back); else
        // off the edge once past 40% of the width.
        bool off = std::abs(v) >= kDismissVelocity ? x != 0 && (v > 0) == (x > 0) : std::abs(x) > w * 0.4f;
        auto value = x > 0 ? SwipeToDismissBoxValue::StartToEnd : SwipeToDismissBoxValue::EndToStart;
        if (off && (!d->confirm || d->confirm(value))) {  // off the edge, and stays dismissed
            d->current.set(value);
            coroutines.launch([d, target = x > 0 ? w : -w, v]() -> Task {
                struct Bounded {  // not past the edge while it goes (whatever ends it)
                    Animatable<float> offset;
                    ~Bounded() { offset.updateBounds(std::nullopt, std::nullopt); }
                } bounded{d->offset};
                d->offset.updateBounds(-std::abs(target), std::abs(target));
                co_await d->offset.animateTo(target, spring(Spring::DampingRatioNoBouncy, Spring::StiffnessMediumLow), v);
            });
        } else {
            coroutines.launch([d, v]() -> Task { co_await settleSwipe(d, v); });
        }
    };
    // The offset is towards the end (mirrored right to left), so is the finger's movement.
    bool rtl = LocalLayoutDirection.current() == LayoutDirection::Rtl;
    Modifier dragged = Modifier()
                           .offset([d] { return Offset{d->offset.value(), 0}; })
                           .pointerInput(rtl, [d, options, onDragEnd, rtl, tracker](PointerInputScope& s) -> Task {
                               co_await detectHorizontalDragGestures(
                                   s, {.onDragStart = [d, tracker](Offset) {
                                           d->offset.stop();
                                           tracker->first.resetTracking();
                                           tracker->second = 0;
                                       },
                                       .onDrag = [d, options, rtl, tracker](PointerInputChange& change, float dx) {
                                           change.consume();
                                           tracker->second += rtl ? -dx : dx;  // the finger's way along the offset (the box moves under it)
                                           tracker->first.addPosition(change.uptimeMillis, {tracker->second, 0});
                                           float x = d->offset.value() + (rtl ? -dx : dx);
                                           if (!options.enableDismissFromStartToEnd) x = std::min(0.f, x);
                                           if (!options.enableDismissFromEndToStart) x = std::max(0.f, x);
                                           d->offset.snapTo(x);
                                           auto direction = x > 0   ? SwipeToDismissBoxValue::StartToEnd
                                                            : x < 0 ? SwipeToDismissBoxValue::EndToStart
                                                                    : SwipeToDismissBoxValue::Settled;
                                           if (d->direction.peek() != direction) d->direction.set(direction);
                                       },
                                       .onDragEnd = onDragEnd,
                                       .onDragCancel = onDragEnd});
                           });
    // Background below, content above; the background gets the content's size. It is only
    // composed while the content is moved off it (else it is covered, and out of the semantics).
    bool showBackground = state.dismissDirection() != SwipeToDismissBoxValue::Settled ||
                          state.currentValue() != SwipeToDismissBoxValue::Settled;
    Layout(modifier.clipToBounds(), [&] {
        if (showBackground) Box(Modifier(), backgroundContent);
        Box(dragged, content);
    }, [d](const std::vector<Measurable>& children, const Constraints& c) {
        if (children.empty()) return Size{};
        Placeable front = children.back().measure(c);
        if (!detail::isProbeMeasure()) {
            d->width = front.width;
            // Swiped away when saved: off its edge, as it was.
            SwipeToDismissBoxValue current = d->current.peek();
            if (current != SwipeToDismissBoxValue::Settled && d->offset.peek() == 0 && !d->offset.isRunning() && front.width > 0)
                d->offset.snapTo(current == SwipeToDismissBoxValue::StartToEnd ? front.width : -front.width);
        }
        if (children.size() > 1) children[0].measure(Constraints::fixed(front.width, front.height)).place(0, 0);
        front.place(0, 0);
        return Size{front.width, front.height};
    });
}

void Badge(const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    if (!content) {
        Box(modifier.size(6).background(cs.error, CircleShape));
        return;
    }
    CompositionLocalProvider({LocalContentColor.provides(cs.onError), LocalTextStyle.provides(typography().labelSmall)}, [&] {
        Row(modifier.heightIn(16).widthIn(16).background(cs.error, 8).padding(4, 0),
            {.horizontalArrangement = Arrangement::Center, .verticalAlignment = Alignment::CenterVertically}, content);
    });
}

void BadgedBox(const Content& badge, const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    // children: the anchor, then the badge on its top-end corner (outside its bounds).
    auto policy = [](LayoutNode& node, const Constraints& c) {
        if (node.children.size() < 2) return Size{};
        Size anchor = node.children[0]->measure(c.loosen());
        node.children[0]->place(0, 0);
        Size badge = node.children[1]->measure({});
        // Compose 1.4's: a dot 6 dp in from the end and down from the top edge; one with content 12
        // in from the end, its bottom 14 dp below the top.
        bool large = badge.width > 6;
        node.children[1]->place(anchor.width - (large ? 12.f : 6.f), -badge.height + (large ? 14.f : 6.f));
        return c.constrain(anchor);
    };
    detail::Composer::require().emit(
        detail::keyOf(CallSite::current()),
        [&](LayoutNode& node) {
            node.modifier = modifier;
            node.measurePolicy = policy;
            node.drawContent = {};
        },
        [&] {
            Box(Modifier(), content);
            Box(Modifier(), badge);
        });
}

namespace {

PaddingValues bottomBarInsets(bool padded) {
    return padded ? detail::systemBarsForVisualComponents().only(WindowInsetsSides::Horizontal | WindowInsetsSides::Bottom).asPaddingValues()
                  : PaddingValues{};
}

// With a scroll behavior the bar shows only what of its height isn't out of view (read during
// layout: scrolling doesn't recompose), sliding down and cut at the bottom.
Modifier exiting(const Modifier& m, const std::optional<BottomAppBarScrollBehavior>& behavior) {
    if (!behavior) return m;
    BottomAppBarState state = behavior->state;
    return m.clipToBounds().layout([state](const MeasureNext& measure, const Constraints& c, Offset& at) {
        Size s = measure({c.minWidth, c.maxWidth, 0, kInfinity});
        if (!detail::isProbeMeasure()) state.setOffsetLimit(s.height);
        at = {};
        return c.constrain({s.width, std::max(0.f, s.height - state.offset())});
    });
}

} // namespace

void FlexibleBottomAppBar(const Modifier& modifier, const Content& content, const FlexibleBottomAppBarOptions& options, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    PaddingValues insets = bottomBarInsets(options.windowInsets);
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurfaceVariant)}, [&] {
        Row(exiting(modifier.fillMaxWidth(), options.scrollBehavior)
                .background(options.containerColor.value_or(cs.surfaceContainer))
                .padding(insets.start, 0, insets.end, insets.bottom)
                .height(options.expandedHeight)
                .padding(options.contentPadding),
            {.horizontalArrangement = options.horizontalArrangement, .verticalAlignment = Alignment::CenterVertically}, content);
    });
}

void BottomAppBar(const BottomAppBarSlots& slots, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    PaddingValues insets = bottomBarInsets(slots.windowInsets);
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurfaceVariant)}, [&] {
        Row(exiting(modifier.fillMaxWidth(), slots.scrollBehavior)
                .background(slots.containerColor.value_or(cs.surfaceContainer))
                .padding(insets.start, 0, insets.end, insets.bottom)
                .height(80)
                .padding(4, 4, 4, 0),  // Compose 1.4's ContentPadding
            {.verticalAlignment = Alignment::CenterVertically}, [&] {
                Row(Modifier().weight(1), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                    if (slots.actions) slots.actions();
                });
                // 8 dp below the padding's top, 12 in from its end (16 from the bar's).
                if (slots.floatingActionButton)
                    Box(Modifier().fillMaxHeight().padding(0, 8, 12, 0), Alignment::TopStart, slots.floatingActionButton);
            });
    });
}

} // namespace kompoz
