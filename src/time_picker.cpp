// Material 3 time picker (clock dial and text input) and its dialog.

#include <kompoz/animation.hpp>
#include <kompoz/coroutines.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/gestures.hpp>
#include <kompoz/material.hpp>
#include <kompoz/state.hpp>

#include <cmath>
#include <format>
#include <numbers>

namespace kompoz {

struct TimePickerState::Data {
    MutableState<int> hour{0}, minute{0};
    MutableState<TimePickerSelectionMode> selection{TimePickerSelectionMode::Hour};
    bool is24Hour = true;
};

TimePickerState::TimePickerState(int initialHour, int initialMinute, bool is24Hour) : data_(std::make_shared<Data>()) {
    data_->is24Hour = is24Hour;
    setHour(initialHour);
    setMinute(initialMinute);
}

int TimePickerState::hour() const { return data_->hour.get(); }
void TimePickerState::setHour(int hour) const {
    hour = (hour % 24 + 24) % 24;
    if (hour != data_->hour.peek()) data_->hour.set(hour);
}
int TimePickerState::minute() const { return data_->minute.get(); }
void TimePickerState::setMinute(int minute) const {
    minute = (minute % 60 + 60) % 60;
    if (minute != data_->minute.peek()) data_->minute.set(minute);
}
bool TimePickerState::is24hour() const { return data_->is24Hour; }
TimePickerSelectionMode TimePickerState::selection() const { return data_->selection.get(); }
void TimePickerState::setSelection(TimePickerSelectionMode mode) const {
    if (mode != data_->selection.peek()) data_->selection.set(mode);
}

SavedValue Saver<TimePickerState>::save(const TimePickerState& s) {
    return SavedValue::List{s.data_->hour.peek(), s.data_->minute.peek(), s.data_->is24Hour, int64_t(s.data_->selection.peek())};
}
std::optional<TimePickerState> Saver<TimePickerState>::restore(const SavedValue& v) {
    const SavedValue::List* l = v.list();
    if (!l || l->size() != 4) return std::nullopt;
    auto hour = (*l)[0].integer(), minute = (*l)[1].integer();
    auto is24 = (*l)[2].boolean();
    auto selection = Saver<TimePickerSelectionMode>::restore((*l)[3]);
    if (!hour || !minute || !is24 || !selection) return std::nullopt;
    TimePickerState s(int(*hour), int(*minute), *is24);
    s.setSelection(*selection);
    return s;
}

TimePickerState rememberTimePickerState(int initialHour, int initialMinute, bool is24Hour, CallSite site) {
    return rememberSaveable([&] { return TimePickerState(initialHour, initialMinute, is24Hour); }, site);
}

namespace {

constexpr float kPi = std::numbers::pi_v<float>;
constexpr float kDial = 256, kOuterRadius = 101, kInnerRadius = 69, kHandleRadius = 24;

std::string twoDigits(int v) { return std::format("{:02}", v); }

// The hour as the clock shows it: 0..23, or 1..12 on a 12-hour clock.
int hourForDisplay(const TimePickerState& state) {
    int h = state.hour();
    return state.is24hour() ? h : h % 12 == 0 ? 12 : h % 12;
}

// Angles go clockwise from 12 o'clock, in radians.
float angleOf(const TimePickerState& state, TimePickerSelectionMode mode) {
    return mode == TimePickerSelectionMode::Hour ? float(state.hour() % 12) * kPi / 6 : float(state.minute()) * kPi / 30;
}
float normalized(float a) {
    a = std::fmod(a, 2 * kPi);
    return a < 0 ? a + 2 * kPi : a;
}
// `to` turned by whole circles to be nearest `from`, so the selector takes the short way round.
float nearestTurn(float from, float to) { return to + 2 * kPi * std::round((from - to) / (2 * kPi)); }
// The 24-hour dial has 00 and 13..23 on its inner ring.
bool onInnerRing(const TimePickerState& state) {
    int h = state.hour();
    return state.is24hour() && state.selection() == TimePickerSelectionMode::Hour && (h == 0 || h > 12);
}

// The selector's angle: animated, except under a finger.
struct Dial {
    explicit Dial(float angle) : angle(angle) {}
    Animatable<float> angle;
    MutableState<bool> dragging{false};
    MutableState<bool> dragInner{false};  // the finger's ring while dragging
    bool settling = false;                // a gesture animates the selector itself
};

void ClockFace(const TimePickerState& state, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    const Typography& t = typography();
    TimePickerSelectionMode mode = state.selection();
    bool hours = mode == TimePickerSelectionMode::Hour;
    int value = hours ? state.hour() : state.minute();
    float target = angleOf(state, mode);
    auto dial = remember([&] { return std::make_shared<Dial>(target); });
    CoroutineScope animations = rememberCoroutineScope();
    // A time set from code or the hour/minute boxes moves the selector round to it.
    LaunchedEffect(std::pair(int(mode), value), [dial, target]() -> Task {
        if (dial->dragging.peek() || dial->settling) co_return;
        co_await dial->angle.animateTo(nearestTurn(dial->angle.peek(), target), spring());
    });

    float angle = dial->angle.value();  // the face recomposes while the selector moves
    bool inner = dial->dragging.get() ? dial->dragInner.get() : onInnerRing(state);
    float radius = inner ? kInnerRadius : kOuterRadius;
    Offset handle{kDial / 2 + radius * std::sin(angle), kDial / 2 - radius * std::cos(angle)};

    struct Label {
        std::string text;
        float angle, radius;
    };
    std::vector<Label> labels;
    for (int i = 0; i < 12; ++i)
        labels.push_back({hours ? std::to_string(i == 0 ? 12 : i) : twoDigits(i * 5), float(i) * kPi / 6, kOuterRadius});
    if (hours && state.is24hour())
        for (int i = 0; i < 12; ++i) labels.push_back({i == 0 ? "00" : std::to_string(i + 12), float(i) * kPi / 6, kInnerRadius});
    auto centerOf = [](const Label& l) {
        return Offset{kDial / 2 + l.radius * std::sin(l.angle), kDial / 2 - l.radius * std::cos(l.angle)};
    };
    Color primary = cs.primary, onPrimary = cs.onPrimary;
    Font font = t.bodyLarge;
    auto drawTrack = [=](DrawScope& d) {
        Offset center{kDial / 2, kDial / 2};
        d.drawLine(primary, center, handle, 2);
        d.drawCircle(primary, 4.f, center);
    };
    // Inside the handle the numbers show in onPrimary, as far as it covers them, as in Compose
    // (drawn, so accessibility reads each number once).
    auto drawHandle = [=](DrawScope& d) {
        Offset origin{handle.x - kHandleRadius, handle.y - kHandleRadius};
        for (const Label& l : labels) {
            Offset c = centerOf(l);
            if (std::hypot(c.x - handle.x, c.y - handle.y) > 2 * kHandleRadius) continue;
            Size size = d.measureText(l.text, font);
            d.drawText(l.text, {c.x - origin.x - size.width / 2, c.y - origin.y - size.height / 2}, font, onPrimary);
        }
    };
    // Tap or drag to pick; letting go settles the selector and moves on from the hours.
    auto input = [state, dial, animations](PointerInputScope& s) -> Task {
        // A tap on the minutes picks the nearest number (every 5 minutes); a drag every minute.
        auto pick = [&](Offset p, bool tap) {
            float dx = p.x - s.size().width / 2, dy = p.y - s.size().height / 2;
            float a = normalized(std::atan2(dx, -dy));
            bool picksHour = state.selection() == TimePickerSelectionMode::Hour;
            bool inner = picksHour && state.is24hour() && std::hypot(dx, dy) < (kInnerRadius + kOuterRadius) / 2;
            if (picksHour) {
                int h = int(std::lround(a / (kPi / 6))) % 12;
                if (state.is24hour())
                    state.setHour(inner ? (h == 0 ? 0 : h + 12) : (h == 0 ? 12 : h));
                else
                    state.setHour(h + (state.isAfternoon() ? 12 : 0));
            } else {
                int step = tap ? 5 : 1;
                state.setMinute(int(std::lround(a / (kPi / 30) / float(step))) * step % 60);
            }
            return std::pair(a, inner);
        };
        auto settle = [&](bool moveOn) {
            dial->settling = true;
            animations.launch([state, dial, moveOn]() -> Task {
                TimePickerSelectionMode mode = state.selection();
                co_await dial->angle.animateTo(nearestTurn(dial->angle.peek(), angleOf(state, mode)), spring());
                dial->settling = false;
                if (moveOn && mode == TimePickerSelectionMode::Hour) state.setSelection(TimePickerSelectionMode::Minute);
            });
        };
        while (true) {
            PointerEvent down = co_await s.awaitPointerEvent();
            if (down.type != PointerEventType::Press || down.change.isConsumed()) continue;
            down.change.consume();
            int64_t id = down.change.id;
            Offset start = down.change.position;
            bool dragging = false, cancelled = false;
            while (true) {
                PointerEvent e = co_await s.awaitPointerEvent();
                if (e.change.id != id) continue;
                if (e.type == PointerEventType::Cancel) {
                    cancelled = true;
                    break;
                }
                if (e.type == PointerEventType::Release) break;
                if (e.type != PointerEventType::Move || e.change.isConsumed()) continue;
                Offset p = e.change.position;
                if (!dragging && std::hypot(p.x - start.x, p.y - start.y) > s.viewConfiguration().touchSlop) {
                    dragging = true;
                    dial->dragging.set(true);
                }
                if (!dragging) continue;
                e.change.consume();
                auto [a, inner] = pick(p, false);
                dial->angle.snapTo(nearestTurn(dial->angle.peek(), a));
                dial->dragInner.set(inner);
            }
            if (dragging) dial->dragging.set(false);
            if (!dragging && !cancelled) pick(start, true);
            settle(!cancelled);
        }
    };

    Box(Modifier().size(kDial).background(cs.surfaceContainerHighest, CircleShape).drawBehind(drawTrack).pointerInput(0, input),
        [&] {
            Layout(
                Modifier().fillMaxSize(),
                [&] {
                    for (const Label& l : labels)
                        Box(Modifier().size(2 * kHandleRadius), Alignment::Center,
                            [&] { Text(l.text, {.font = font, .color = cs.onSurface}); });
                },
                [labels, centerOf](const std::vector<Measurable>& children, const Constraints& c) {
                    for (size_t i = 0; i < children.size() && i < labels.size(); ++i) {
                        Placeable p = children[i].measure({0, c.maxWidth, 0, c.maxHeight});
                        Offset at = centerOf(labels[i]);
                        p.place(at.x - p.width / 2, at.y - p.height / 2);
                    }
                    return Size{c.maxWidth, c.maxHeight};
                });
            // The handle is drawn as a circle (anti-aliased wherever it is); the clip only cuts
            // the numbers in it.
            Box(Modifier()
                    .offset(handle.x - kHandleRadius, handle.y - kHandleRadius)
                    .size(2 * kHandleRadius)
                    .drawBehind([primary](DrawScope& d) { d.drawCircle(primary, kHandleRadius, Offset{kHandleRadius, kHandleRadius}); })
                    .clip(CircleShape)
                    .drawBehind(drawHandle));
        });
}

void TimeSelector(const TimePickerState& state, TimePickerSelectionMode mode, const std::string& label,
                  CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    bool selected = state.selection() == mode;
    int value = mode == TimePickerSelectionMode::Hour ? hourForDisplay(state) : state.minute();
    Box(Modifier()
            .size(96, 80)
            .background(selected ? cs.primaryContainer : cs.surfaceContainerHighest, 8)
            .clip(RoundedCornerShape(8))
            .clickable([state, mode] { state.setSelection(mode); })
            .semantics({.contentDescription = label, .role = SemanticsRole::Tab, .checked = selected}),
        Alignment::Center, [&] {
            Text(twoDigits(value), {.font = typography().displayLarge, .color = selected ? cs.onPrimaryContainer : cs.onSurface});
        });
}

void Separator(float height, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    Box(Modifier().size(24, height), Alignment::Center,
        [&] { Text(":", {.font = typography().displayLarge, .color = colorScheme().onSurface}); });
}

// AM / PM, stacked beside the time or in a row below it.
void PeriodToggle(const TimePickerState& state, const TimePickerOptions& o, bool vertical, Size size,
                  CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    auto item = [&](bool pm) {
        bool selected = state.isAfternoon() == pm;
        Modifier m = vertical ? Modifier().fillMaxWidth().weight(1) : Modifier().fillMaxHeight().weight(1);
        Box(m.background(selected ? cs.tertiaryContainer : colors::Transparent)
                .clickable([state, pm] {
                    if (state.isAfternoon() != pm) state.setHour(state.hour() + 12);
                })
                .semantics({.role = SemanticsRole::RadioButton, .checked = selected}),
            Alignment::Center, [&] {
                Text(pm ? o.pm.value_or(LocalStrings.current().pm) : o.am.value_or(LocalStrings.current().am),
                     {.font = typography().titleMedium, .color = selected ? cs.onTertiaryContainer : cs.onSurfaceVariant});
            });
    };
    // The outline goes over the clipped halves.
    Modifier box = Modifier().size(size.width, size.height).border(1, cs.outline, 8).clip(RoundedCornerShape(8));
    if (vertical)
        Column(box, [&] {
            item(false);
            Box(Modifier().fillMaxWidth().height(1).background(cs.outline));
            item(true);
        });
    else
        Row(box, [&] {
            item(false);
            Box(Modifier().fillMaxHeight().width(1).background(cs.outline));
            item(true);
        });
}

void ClockDisplay(const TimePickerState& state, const TimePickerOptions& o, bool horizontal, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    auto time = [&] {
        TimeSelector(state, TimePickerSelectionMode::Hour, o.hourLabel.value_or(LocalStrings.current().hour));
        Separator(80);
        TimeSelector(state, TimePickerSelectionMode::Minute, o.minuteLabel.value_or(LocalStrings.current().minute));
    };
    if (horizontal) {
        Column([&] {
            Row(time);
            if (!state.is24hour()) {
                Spacer(Modifier().height(12));
                PeriodToggle(state, o, false, {216, 38});
            }
        });
    } else {
        Row([&] {
            time();
            if (!state.is24hour()) {
                Spacer(Modifier().width(12));
                PeriodToggle(state, o, true, {52, 80});
            }
        });
    }
}

// One of TimeInput's fields: up to two digits, kept when they make a valid value.
void TimeField(MutableState<std::string> text, std::function<int()> current, std::function<bool(int)> accept,
               const std::string& label, ImeAction imeAction, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    const Typography& t = typography();
    auto focused = remember([] { return mutableStateOf(false); });
    bool isFocused = focused.get();
    Column([&] {
        Modifier box = Modifier()
                           .size(96, 72)
                           .background(isFocused ? cs.primaryContainer : cs.surfaceContainerHighest, 8)
                           .onFocusChanged([focused, text, current](FocusState s) {
                               if (focused.peek() && !s.hasFocus) text.set(twoDigits(current()));  // tidied when left
                               focused.set(s.hasFocus);
                           });
        if (isFocused) box = box.border(2, cs.primary, 8);
        Box(box.padding(8, 0), Alignment::Center, [&] {
            BasicTextField(
                text.get(),
                [text, accept](std::string v) {
                    auto digits = [](std::string_view s) {
                        std::string d;
                        for (char c : s)
                            if (c >= '0' && c <= '9') d += c;
                        return d;
                    };
                    // What was typed: the new text less what it shares with the old at either end.
                    std::string old = text.peek();
                    size_t p = 0, q = 0;
                    while (p < old.size() && p < v.size() && old[p] == v[p]) ++p;
                    while (q < old.size() - p && q < v.size() - p && old[old.size() - 1 - q] == v[v.size() - 1 - q]) ++q;
                    std::string all = digits(v), typed = digits(std::string_view(v).substr(p, v.size() - p - q));
                    if (all.empty()) return text.set("");
                    // Typing into a full field starts it over with what was typed.
                    for (const std::string& candidate : {all, typed})
                        if (!candidate.empty() && candidate.size() <= 2 && accept(std::stoi(candidate))) return text.set(candidate);
                },
                Modifier().fillMaxWidth(),
                {.singleLine = true,
                 .keyboardOptions = {.keyboardType = KeyboardType::Number, .imeAction = imeAction},
                 .textStyle = {.font = t.displayMedium,
                               .color = isFocused ? cs.onPrimaryContainer : cs.onSurface,
                               .textAlign = TextAlign::Center}});
        });
        Text(label, Modifier().padding(0, 7, 0, 0), {.font = t.bodySmall, .color = cs.onSurfaceVariant});
    });
}

} // namespace

void TimePicker(const TimePickerState& state, const Modifier& modifier, TimePickerOptions options, CallSite site) {
    ComposableScope scope(site);
    Size window = LocalWindowSize.current();
    TimePickerLayoutType layout = options.layoutType.value_or(window.width > window.height ? TimePickerLayoutType::Horizontal
                                                                                           : TimePickerLayoutType::Vertical);
    if (layout == TimePickerLayoutType::Horizontal) {
        Row(modifier, {.verticalAlignment = Alignment::CenterVertically}, [&] {
            ClockDisplay(state, options, true);
            Spacer(Modifier().width(52));
            ClockFace(state);
        });
    } else {
        Column(modifier, {.horizontalAlignment = Alignment::CenterHorizontally}, [&] {
            ClockDisplay(state, options, false);
            Spacer(Modifier().height(36));
            ClockFace(state);
            Spacer(Modifier().height(24));
        });
    }
}

void TimeInput(const TimePickerState& state, const Modifier& modifier, TimePickerOptions options, CallSite site) {
    ComposableScope scope(site);
    auto hourText = remember([&] { return mutableStateOf(twoDigits(hourForDisplay(state))); });
    auto minuteText = remember([&] { return mutableStateOf(twoDigits(state.minute())); });
    Row(modifier.padding(0, 0, 0, 24), [&] {
        TimeField(
            hourText, [state] { return hourForDisplay(state); },
            [state](int h) {
                if (state.is24hour() ? h > 23 : h < 1 || h > 12) return false;
                state.setHour(state.is24hour() ? h : h % 12 + (state.isAfternoon() ? 12 : 0));
                return true;
            },
            options.hourLabel.value_or(LocalStrings.current().hour), ImeAction::Next);
        Separator(72);
        TimeField(
            minuteText, [state] { return state.minute(); },
            [state](int m) {
                if (m > 59) return false;
                state.setMinute(m);
                return true;
            },
            options.minuteLabel.value_or(LocalStrings.current().minute), ImeAction::Done);
        if (!state.is24hour()) {
            Spacer(Modifier().width(12));
            PeriodToggle(state, options, true, {52, 72});
        }
    });
}

void TimePickerDialog(std::function<void()> onDismissRequest, const TimePickerDialogSlots& slots, const Content& content,
                      CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    Dialog(std::move(onDismissRequest), [&] {
        Column(Modifier().background(cs.surfaceContainerHigh, 28).padding(24), {.horizontalAlignment = Alignment::CenterHorizontally},
               [&] {
                   if (slots.title)
                       Box(Modifier().fillMaxWidth().padding(0, 0, 0, 20), [&] {
                           CompositionLocalProvider({LocalContentColor.provides(cs.onSurfaceVariant),
                                                     LocalTextStyle.provides(typography().labelMedium)},
                                                    slots.title);
                       });
                   content();
                   Row(Modifier().fillMaxWidth().height(40), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                       if (slots.modeToggleButton) slots.modeToggleButton();
                       Spacer(Modifier().weight(1));
                       Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                           if (slots.dismissButton) slots.dismissButton();
                           if (slots.confirmButton) slots.confirmButton();
                       });
                   });
               });
    });
}

} // namespace kompoz
