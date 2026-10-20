// Material 3 search bar.

#include <kompoz/animation.hpp>
#include <kompoz/coroutines.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/host.hpp>
#include <kompoz/material.hpp>
#include <kompoz/state.hpp>

namespace kompoz {

namespace {

// The field, editing `o.query` or a TextFieldState.
void inputField(const SearchBarDefaults::InputFieldOptions& o, const std::optional<TextFieldState>& state, const Modifier& modifier) {
    const ColorScheme& cs = colorScheme();
    const Typography& t = typography();
    auto& requester = remember([] { return FocusRequester(); });
    // Expanded: the field takes the focus, and the keyboard comes up.
    LaunchedEffect(o.expanded, [requester, expanded = o.expanded]() -> Task {
        if (expanded) requester.requestFocus();
        co_return;
    });
    Modifier row = modifier.fillMaxWidth().height(56);
    if (!o.expanded)
        row = row.clickable([change = o.onExpandedChange] {
                     if (change) change(true);
                 })
                  .semantics({.contentDescription = o.query.empty() ? o.placeholder : o.query, .role = SemanticsRole::Button});
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurfaceVariant)}, [&] {
        Row(row, {.verticalAlignment = Alignment::CenterVertically}, [&] {
            if (o.leadingIcon) Box(Modifier().padding(16, 0, 0, 0), o.leadingIcon);
            Box(Modifier().weight(1).padding(16, 0), Alignment::CenterStart, [&] {
                if (o.expanded) {
                    auto search = [onSearch = o.onSearch, query = o.query] {
                        if (onSearch) onSearch(query);
                    };
                    TextFieldOptions options{.singleLine = true,
                                             .placeholder = o.placeholder,
                                             .keyboardOptions = {.imeAction = ImeAction::Search},
                                             .keyboardActions = {.onSearch = search}};
                    Modifier field = Modifier().fillMaxWidth().focusRequester(requester);
                    if (state)
                        BasicTextField(*state, field, std::move(options));
                    else
                        BasicTextField(o.query, o.onQueryChange, field, std::move(options));
                } else {
                    Text(o.query.empty() ? o.placeholder : o.query,
                         {.font = t.bodyLarge, .color = o.query.empty() ? cs.onSurfaceVariant : cs.onSurface, .maxLines = 1,
                          .overflow = TextOverflow::Ellipsis});
                }
            });
            if (o.trailingIcon) Box(Modifier().padding(0, 0, 16, 0), o.trailingIcon);
        });
    });
}

} // namespace

struct SearchBarState::Data {
    explicit Data(SearchBarValue v) : current(v), target(v), progress(v == SearchBarValue::Expanded ? 1.f : 0.f) {}
    MutableState<SearchBarValue> current, target;
    Animatable<float> progress;  // read while drawing
    Rect collapsed;              // the collapsed bar in the window, for the docked view
    // A remembered state's: the bars' animations outlive the field that starts them (the
    // collapsed bar's leaves as the bar expands).
    std::optional<CoroutineScope> scope;
};

SearchBarState::SearchBarState(SearchBarValue initialValue) : data_(std::make_shared<Data>(initialValue)) {}
SearchBarValue SearchBarState::currentValue() const { return data_->current.get(); }
SearchBarValue SearchBarState::targetValue() const { return data_->target.get(); }
float SearchBarState::progress() const { return data_->progress.value(); }

namespace {
Task animateTo(std::shared_ptr<SearchBarState::Data> d, SearchBarValue to) {
    d->target.set(to);
    bool expanding = to == SearchBarValue::Expanded;
    co_await d->progress.animateTo(expanding ? 1.f : 0.f, tween(expanding ? 300 : 200));
    if (d->target.peek() == to) d->current.set(to);
}
} // namespace

Task SearchBarState::animateToExpanded() const { return animateTo(data_, SearchBarValue::Expanded); }
Task SearchBarState::animateToCollapsed() const { return animateTo(data_, SearchBarValue::Collapsed); }

void SearchBarState::snapTo(SearchBarValue value) const {
    data_->target.set(value);
    data_->progress.snapTo(value == SearchBarValue::Expanded ? 1.f : 0.f);
    data_->current.set(value);
}

SavedValue Saver<SearchBarState>::save(const SearchBarState& s) { return Saver<SearchBarValue>::save(s.data_->current.peek()); }
std::optional<SearchBarState> Saver<SearchBarState>::restore(const SavedValue& v) {
    std::optional<SearchBarValue> value = Saver<SearchBarValue>::restore(v);
    if (!value) return std::nullopt;
    return SearchBarState(*value);
}

SearchBarState rememberSearchBarState(SearchBarValue initialValue, CallSite site) {
    ComposableScope group(site);
    auto scope = rememberCoroutineScope();
    auto& state = rememberSaveable([initialValue] { return SearchBarState(initialValue); });
    state.data_->scope = scope;
    return state;
}

namespace SearchBarDefaults {

void InputField(const InputFieldOptions& o, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    inputField(o, std::nullopt, modifier);
}

void InputField(const SearchBarState& state, const TextFieldState& textFieldState, const InputFieldOptions& options,
                const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    auto own = rememberCoroutineScope();
    CoroutineScope coroutines = state.data_->scope.value_or(own);
    InputFieldOptions o = options;
    o.query = textFieldState.text();
    o.expanded = state.targetValue() == SearchBarValue::Expanded;
    o.onExpandedChange = [state, coroutines](bool expand) {
        coroutines.launch([state, expand]() -> Task { co_await (expand ? state.animateToExpanded() : state.animateToCollapsed()); });
    };
    o.onSearch = [onSearch = options.onSearch, textFieldState](std::string) {
        if (onSearch) onSearch(textFieldState.text());
    };
    inputField(o, textFieldState, modifier);
}

} // namespace SearchBarDefaults

namespace {
// Collapses the state, from a dismissal.
std::function<void()> collapser(const SearchBarState& state, const CoroutineScope& coroutines) {
    return [state, coroutines] { coroutines.launch([state]() -> Task { co_await state.animateToCollapsed(); }); };
}
bool showsExpanded(const SearchBarState& state) {
    return state.targetValue() == SearchBarValue::Expanded || state.currentValue() == SearchBarValue::Expanded;
}
} // namespace

void SearchBar(const SearchBarState& state, const Content& inputField, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    auto d = state.data_;
    Box(modifier.heightIn(56)
            .onGloballyPositioned([d](const LayoutCoordinates& c) {
                Offset at = c.positionInWindow();
                d->collapsed = {at.x, at.y, c.size().width, c.size().height};
            })
            .background(cs.surfaceContainerHigh, 28),
        Alignment::CenterStart, [&] {
            if (state.targetValue() == SearchBarValue::Collapsed) inputField();
        });
}

namespace {
// A back gesture over the full screen search: its first step and its latest.
struct SearchBack {
    MutableState<std::optional<BackEventCompat>> first{std::nullopt}, last{std::nullopt};
    void clear() {
        first.set(std::nullopt);
        last.set(std::nullopt);
    }
    float progress() const {  // eased as M3's
        static constexpr Easing kPredictiveBackEasing{0.1f, 0.1f, 0.f, 1.f};
        auto e = last.peek();
        return e ? kPredictiveBackEasing(e->progress) : 0.f;
    }
};
} // namespace

// As M3's: the view grows out of the collapsed bar (its surface from the bar's bounds to the
// window's, the field kept at the bar's height, below the status bar and 8 dp more, the
// results fading in under it) and back into it. A back gesture shrinks it to nine tenths,
// away from the edge it came from, and up or down with the finger by up to 24 dp (never past
// the bar); done, it goes back into the bar from there.
void ExpandedFullScreenSearchBar(const SearchBarState& state, const Content& inputField, const Content& content, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    auto own = rememberCoroutineScope();
    CoroutineScope coroutines = state.data_->scope.value_or(own);
    auto& back = remember([] { return std::make_shared<SearchBack>(); });
    if (!showsExpanded(state)) return;
    auto d = state.data_;
    PaddingValues insets = WindowInsets::safeDrawing().asPaddingValues();
    Popup({.fullScreen = true, .onDismissRequest = collapser(state, coroutines), .dismissOnOutsideClick = false}, [&] {
        DisposableEffect(0, [back]() -> std::function<void()> { return [back] { back->clear(); }; });
        detail::backHandler({
            .onBack = collapser(state, coroutines),
            .onStarted = [back](const BackEventCompat& e) {
                back->first.set(e);
                back->last.set(e);
            },
            .onProgressed = [back](const BackEventCompat& e) { back->last.set(e); },
            .onCancelled = [back] { back->clear(); },
            .enabled = state.targetValue() == SearchBarValue::Expanded,
        });
        Color surface = cs.surfaceContainerHigh;
        Layout(
            Modifier().fillMaxSize(),
            [&] {
                Box(Modifier().drawBehind([d, back, surface](DrawScope& draw) {  // round as the bar, square once grown
                    back->last.get();
                    draw.drawRoundRect(surface, {}, {}, CornerRadius(28 * std::max(1 - d->progress.value(), back->progress())));
                }));
                Box(Modifier().padding(insets.start, 0, insets.end, 0), Alignment::CenterStart, inputField);
                Column(Modifier().padding(insets.start, 0, insets.end, insets.bottom).graphicsLayer([d](GraphicsLayerScope& l) {
                    l.alpha = d->progress.value();
                }),
                       [&] {
                           HorizontalDivider();
                           content();
                       });
            },
            [d, back, top = insets.top](const std::vector<Measurable>& children, const Constraints& c) {
                float w = c.maxWidth, h = c.maxHeight, p = d->progress.value();
                Rect bar = d->collapsed.width > 0 ? d->collapsed : Rect{0, 0, std::min(360.f, w), 56};
                std::optional<BackEventCompat> first = back->first.get(), last = back->last.get();
                float b = back->progress();
                float backWidth = std::max(w * 0.9f, bar.width), backHeight = std::max(h * 0.9f, bar.height);
                float width = lerp(bar.width, lerp(w, backWidth, b), p), height = lerp(bar.height, lerp(h, backHeight, b), p);
                float padTop = top + 8, above = lerp(0, padTop, std::min(p, 1 - b)), below = lerp(0, 8, p);
                float endX = 0, endY = 0;
                if (last) {
                    endX = last->swipeEdge == BackEventCompat::SwipeEdge::Left ? w - 8 - backWidth : 8;
                    endX = std::min(std::max(endX, bar.right() - backWidth), bar.x);
                    float dy = last->touchY - (first ? first->touchY : last->touchY);
                    float room = std::min(24.f, std::max(0.f, (h - backHeight) / 2 - 8));
                    endY = std::min(room * std::abs(dy) / h * (dy < 0 ? -1.f : dy > 0 ? 1.f : 0.f) + padTop, bar.y);
                }
                float x = lerp(bar.x, lerp(0, endX, b), p), y = lerp(bar.y, lerp(0, endY, b), p);
                children[0].measure(Constraints::fixed(width, height)).place(x, y);
                Placeable field = children[1].measure(Constraints::fixed(width, bar.height));
                field.place(x, y + above);
                children[2].measure(Constraints::fixed(width, std::max(0.f, height - (field.height + above + below)))).place(x, y + above + field.height + below);
                return Size{w, h};
            });
    });
}

void ExpandedDockedSearchBar(const SearchBarState& state, const Content& inputField, const Content& content, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    auto own = rememberCoroutineScope();
    CoroutineScope coroutines = state.data_->scope.value_or(own);
    if (!showsExpanded(state)) return;
    auto d = state.data_;
    float maxHeight = std::max(240.f, LocalWindowSize.current().height * 2 / 3);
    Popup({.position = [d](const Rect&, Size size, Size viewport) {
               // Over the collapsed bar, moved up only as far as it must to fit.
               return Offset{d->collapsed.x, std::max(0.f, std::min(d->collapsed.y, viewport.height - size.height))};
           },
           .onDismissRequest = collapser(state, coroutines)},
          [&] {
              Column(Modifier()
                         .width(d->collapsed.width)
                         .graphicsLayer([d](GraphicsLayerScope& l) { l.alpha = d->progress.value(); })
                         .shadow(3, 28)
                         .background(cs.surfaceContainerHigh, 28)
                         .clip(RoundedCornerShape(28)),
                     [&] {
                         Box(Modifier().fillMaxWidth().heightIn(56), Alignment::CenterStart, inputField);
                         HorizontalDivider();
                         Column(Modifier().fillMaxWidth().heightIn(240, maxHeight), content);
                     });
          });
}

void SearchBar(const Content& inputField, bool expanded, std::function<void(bool)> onExpandedChange,
               const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    auto change = rememberUpdatedState(std::move(onExpandedChange));
    // The pill keeps its place in the layout while the search view is open over everything.
    Box(modifier.heightIn(56).background(cs.surfaceContainerHigh, 28), Alignment::CenterStart, [&] {
        if (!expanded) inputField();
    });
    if (!expanded) return;
    auto appear = remember([] { return Animatable<float>(0.f); });
    LaunchedEffect(0, [appear]() -> Task { co_await appear.animateTo(1.f, tween(150)); });
    Popup({.fullScreen = true, .onDismissRequest = [change] { if (change.get()) change.get()(false); },
           .dismissOnOutsideClick = false},
          [&] {
              Column(Modifier()
                         .fillMaxSize()
                         .graphicsLayer([appear](GraphicsLayerScope& l) { l.alpha = appear.value(); })
                         .background(cs.surfaceContainerHigh)
                         .safeDrawingPadding(),  // under the status bar, above the keyboard
                     [&] {
                         Box(Modifier().fillMaxWidth().heightIn(72), Alignment::CenterStart, inputField);
                         HorizontalDivider();
                         Column(Modifier().fillMaxWidth().weight(1), content);
                     });
          });
}

void DockedSearchBar(const Content& inputField, bool expanded, std::function<void(bool)> onExpandedChange,
                     const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    auto change = rememberUpdatedState(std::move(onExpandedChange));
    auto width = remember([] { return mutableStateOf(0.f); });
    auto appear = remember([] { return Animatable<float>(0.f); });
    LaunchedEffect(expanded, [appear, expanded]() -> Task {
        if (expanded)
            co_await appear.animateTo(1.f, tween(150));
        else
            appear.snapTo(0.f);
    });
    float maxHeight = std::max(240.f, LocalWindowSize.current().height * 2 / 3);
    // The pill keeps its place; open, the same field heads a card over the page, at its width.
    Box(modifier.heightIn(56).onSizeChanged([width](Size s) { width.set(s.width); }).background(cs.surfaceContainerHigh, 28),
        Alignment::CenterStart, [&] {
            if (!expanded) return inputField();
            Popup({.position = [](const Rect& pill, Size size, Size viewport) {
                       // Over the pill, moved up only as far as it must to fit.
                       return Offset{pill.x, std::max(0.f, std::min(pill.y, viewport.height - size.height))};
                   },
                   .onDismissRequest = [change] { if (change.get()) change.get()(false); }},
                  [&] {
                      Column(Modifier().width(width.get()).shadow(3, 28).background(cs.surfaceContainerHigh, 28).clip(RoundedCornerShape(28)),
                             [&] {
                                 Box(Modifier().fillMaxWidth().heightIn(56), Alignment::CenterStart, inputField);
                                 HorizontalDivider();
                                 Column(Modifier()
                                            .fillMaxWidth()
                                            .heightIn(240, maxHeight)
                                            .graphicsLayer([appear](GraphicsLayerScope& l) { l.alpha = appear.value(); }),
                                        content);
                             });
                  });
        });
}

} // namespace kompoz
