#pragma once

// Layout model: constraints, modifiers and the retained LayoutNode tree.
// Measuring works like Compose: constraints go down, sizes come up, parents place children.

#include "coroutines.hpp"
#include "graphics.hpp"
#include "interaction.hpp"
#include "runtime.hpp"

#include <algorithm>
#include <any>
#include <array>
#include <chrono>
#include <functional>
#include <memory>
#include <limits>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kompoz {

class ComposeHost;
class LayoutNode;
class DrawScope;
class ContentDrawScope;
class CacheDrawScope;
struct DrawResult;
class PointerInputScope;
class Task;
class DraggableState;
class Draggable2DState;
class Scrollable2DState;
struct Draggable2DOptions;
struct MagnifierOptions;
class TransformableState;
struct AnimationSpec;
class NestedScrollConnection;
class NestedScrollDispatcher;
class PullToRefreshState;
template <class T>
class AnchoredDraggableState;
namespace detail {
struct AnchoredDragTarget;
// The size of the lazy list whose items are being measured (fillParentMaxWidth/Height);
// NaN outside one.
Size lazyParentSize();
class LazyParentScope {  // set by a lazy list while it measures its items
public:
    explicit LazyParentScope(Size size);
    ~LazyParentScope();
    LazyParentScope(const LazyParentScope&) = delete;
    LazyParentScope& operator=(const LazyParentScope&) = delete;

private:
    Size previous_;
};
}

namespace detail {
struct PointerInputState;
}

struct Constraints {
    float minWidth = 0, maxWidth = kInfinity, minHeight = 0, maxHeight = kInfinity;

    static constexpr Constraints fixed(float width, float height) { return {width, width, height, height}; }
    friend constexpr bool operator==(const Constraints&, const Constraints&) = default;

    constexpr bool hasBoundedWidth() const { return maxWidth != kInfinity; }
    constexpr bool hasBoundedHeight() const { return maxHeight != kInfinity; }
    constexpr Constraints loosen() const { return {0, maxWidth, 0, maxHeight}; }
    constexpr Size constrain(Size s) const {
        return {std::clamp(s.width, minWidth, maxWidth), std::clamp(s.height, minHeight, maxHeight)};
    }
    // Shrinks the available space, e.g. for padding.
    constexpr Constraints deflate(float horizontal, float vertical) const {
        auto sub = [](float v, float d) { return v == kInfinity ? v : std::max(0.f, v - d); };
        return {std::max(0.f, minWidth - horizontal), sub(maxWidth, horizontal), std::max(0.f, minHeight - vertical),
                sub(maxHeight, vertical)};
    }
};

struct HorizontalAlignment {
    float bias = -1;
};
struct VerticalAlignment {
    float bias = -1;
};

class Painter;

// Bias based, like Compose's BiasAlignment: -1 = start/top, 0 = center, 1 = end/bottom.
struct Alignment {
    float horizontal = -1, vertical = -1;

    constexpr Offset align(Size content, Size space) const {
        return {(space.width - content.width) / 2 * (1 + horizontal),
                (space.height - content.height) / 2 * (1 + vertical)};
    }

    friend constexpr bool operator==(const Alignment&, const Alignment&) = default;

    static const Alignment TopStart, TopCenter, TopEnd, CenterStart, Center, CenterEnd, BottomStart, BottomCenter,
        BottomEnd;
    static const HorizontalAlignment Start, CenterHorizontally, End;
    static const VerticalAlignment Top, CenterVertically, Bottom;
};

inline constexpr Alignment Alignment::TopStart{-1, -1};
inline constexpr Alignment Alignment::TopCenter{0, -1};
inline constexpr Alignment Alignment::TopEnd{1, -1};
inline constexpr Alignment Alignment::CenterStart{-1, 0};
inline constexpr Alignment Alignment::Center{0, 0};
inline constexpr Alignment Alignment::CenterEnd{1, 0};
inline constexpr Alignment Alignment::BottomStart{-1, 1};
inline constexpr Alignment Alignment::BottomCenter{0, 1};
inline constexpr Alignment Alignment::BottomEnd{1, 1};
inline constexpr HorizontalAlignment Alignment::Start{-1};
inline constexpr HorizontalAlignment Alignment::CenterHorizontally{0};
inline constexpr HorizontalAlignment Alignment::End{1};
inline constexpr VerticalAlignment Alignment::Top{-1};
inline constexpr VerticalAlignment Alignment::CenterVertically{0};
inline constexpr VerticalAlignment Alignment::Bottom{1};

// Where a snapping list or pager lines its item up: its start with the list's, its middle with
// the middle, its end with the end.
enum class SnapPosition { Start, Center, End };

// Main-axis placement for Row and Column.
struct Arrangement {
    enum class Kind { Start, End, Center, SpaceBetween, SpaceAround, SpaceEvenly };
    Kind kind = Kind::Start;
    float spacing = 0;

    static constexpr Arrangement spacedBy(float space, Kind kind = Kind::Start) { return {kind, space}; }

    // Writes the main-axis position of each child into `positions`.
    void arrange(float total, const std::vector<float>& sizes, std::vector<float>& positions) const;

    static const Arrangement Start, End, Top, Bottom, Center, SpaceBetween, SpaceAround, SpaceEvenly;
};

inline constexpr Arrangement Arrangement::Start{Kind::Start};
inline constexpr Arrangement Arrangement::Top{Kind::Start};
inline constexpr Arrangement Arrangement::End{Kind::End};
inline constexpr Arrangement Arrangement::Bottom{Kind::End};
inline constexpr Arrangement Arrangement::Center{Kind::Center};
inline constexpr Arrangement Arrangement::SpaceBetween{Kind::SpaceBetween};
inline constexpr Arrangement Arrangement::SpaceAround{Kind::SpaceAround};
inline constexpr Arrangement Arrangement::SpaceEvenly{Kind::SpaceEvenly};

// Keys a hardware keyboard sends (Compose's Key.Enter, Key.DirectionUp, Key.A, ...).
enum class Key {
    Unknown,
    Enter, Escape, Tab, Backspace, Delete, Space,
    Left, Right, Up, Down, Home, End, PageUp, PageDown,
    A, B, C, D, E, F, G, H, I, J, K, L, M, N, O, P, Q, R, S, T, U, V, W, X, Y, Z,
    Zero, One, Two, Three, Four, Five, Six, Seven, Eight, Nine,
    F1, F2, F3, F4, F5, F6, F7, F8, F9, F10, F11, F12,
};
struct KeyModifiers {
    bool shift = false, ctrl = false;  // ctrl: Command on macOS
    bool alt = false;
};
enum class KeyEventType { KeyDown, KeyUp };
struct KeyEvent {
    Key key = Key::Unknown;
    KeyEventType type = KeyEventType::KeyDown;
    KeyModifiers modifiers;
    bool isShiftPressed() const { return modifiers.shift; }
    bool isCtrlPressed() const { return modifiers.ctrl; }
    bool isAltPressed() const { return modifiers.alt; }
};

// What the software keyboard shows and its action key does (Compose's KeyboardOptions).
enum class KeyboardType { Text, Ascii, Number, Decimal, Phone, Uri, Email, Password, NumberPassword };
enum class ImeAction { Default, None, Go, Search, Send, Previous, Next, Done };
enum class KeyboardCapitalization { None, Characters, Words, Sentences };
struct KeyboardOptions {
    KeyboardCapitalization capitalization = KeyboardCapitalization::None;
    bool autoCorrect = true;
    KeyboardType keyboardType = KeyboardType::Text;
    ImeAction imeAction = ImeAction::Default;  // Default: Done on single-line fields, a new line on others
    // false: the field focused from code (a FocusRequester, the Tab key) takes the typing without
    // bringing up the software keyboard; a tap on it does (Compose's showKeyboardOnFocus).
    bool showKeyboardOnFocus = true;
    friend constexpr bool operator==(const KeyboardOptions&, const KeyboardOptions&) = default;
};
// What the platform's input method is told about the focused field (Compose's ImeOptions).
struct ImeOptions : KeyboardOptions {
    bool singleLine = true;
    bool showKeyboard = true;  // false: the typing goes to the field, the software keyboard stays as it is
};

struct FocusState {
    bool isFocused = false;  // this node has the focus
    bool hasFocus = false;   // it or a node inside it does
    friend constexpr bool operator==(const FocusState&, const FocusState&) = default;
};
enum class Cursor { Default, Pointer, Text, Crosshair, Move, ResizeHorizontal, ResizeVertical, NotAllowed, Wait };
// Compose's PointerIcon names: Modifier().pointerHoverIcon(PointerIcon::Hand).
struct PointerIcon {
    static constexpr Cursor Default = Cursor::Default, Hand = Cursor::Pointer, Text = Cursor::Text, Crosshair = Cursor::Crosshair;
};

// Preferred color for content (text, pressed/hover overlays) in the current subtree.
inline const CompositionLocal<Color> LocalContentColor{colors::Black};

// Which way layouts go (Compose's LocalLayoutDirection): in Rtl a Row starts at the right,
// Start / End (alignments, padding, offset) mirror, horizontal scrolling starts at the right
// and follows the finger the other way. Drawing, absoluteOffset/absolutePadding and the
// window insets don't mirror.
//   CompositionLocalProvider({LocalLayoutDirection.provides(LayoutDirection::Rtl)}, [&] { ... });
inline const CompositionLocal<LayoutDirection> LocalLayoutDirection{LayoutDirection::Ltr};
// Whether a language is written right to left ("ar", "he-IL", "fa_IR", "ur"...): what the host
// takes the app's direction from (ComposeHost::setLayoutDirection, the system's language).
bool isRtlLanguage(std::string_view languageTag);

// How a clickable shows that it is pressed, hovered or focused (Compose's Indication): a
// Material ripple — a circle that spreads from where it was pressed over the shape and fades
// once let go — over a state layer for hover and focus (and a focus ring).
class ContentDrawScope;

// An indication of one's own (Compose's IndicationNodeFactory and its node): the factory makes
// a node for each component it is used on; the node hears of that component's interactions —
// presses (and where), releases, hovers, focus — and draws around its content (what comes after
// the clickable or indication() in the chain: put it before a background to draw that too). Animate in its
// coroutineScope(); what it reads while drawing (an Animatable's value) draws it again.
//   struct Shrink final : IndicationNode {
//       Animatable<float> scale{1.f};
//       void onInteraction(const Interaction& i) override {
//           float to = i.type == Interaction::Type::Press ? 0.9f : 1.f;
//           if (i.type != Interaction::Type::Press && i.type != Interaction::Type::Release && i.type != Interaction::Type::Cancel) return;
//           coroutineScope().launch([s = scale, to]() -> Task { co_await s.animateTo(to); });
//       }
//       void draw(ContentDrawScope& d) override { d.scale(scale.value(), scale.value(), [&](DrawScope&) { d.drawContent(); }); }
//   };
//   struct ShrinkIndication final : IndicationNodeFactory {
//       std::shared_ptr<IndicationNode> create() const override { return std::make_shared<Shrink>(); }
//   };
//   CompositionLocalProvider({LocalIndication.provides(Indication(std::make_shared<ShrinkIndication>()))}, [&] { Button(...); });
class IndicationNode {
public:
    virtual ~IndicationNode() = default;  // its coroutines go with it
    virtual void onInteraction(const Interaction& interaction) { (void)interaction; }
    virtual void draw(ContentDrawScope& scope);  // by default the content as it is
    // Its own coroutines (Compose's node's coroutineScope): cancelled when its component goes.
    const CoroutineScope& coroutineScope() const { return *scope_; }

private:
    friend class ComposeHost;
    std::optional<CoroutineScope> scope_;
};
class IndicationNodeFactory {
public:
    virtual ~IndicationNodeFactory() = default;
    virtual std::shared_ptr<IndicationNode> create() const = 0;
};

struct Indication {
    enum class Kind { None, StateLayer, Ripple, Custom };
    Kind kind = Kind::Ripple;  // StateLayer: the pressed state shows as a flat overlay instead
    bool bounded = true;       // clipped to the shape, from the press; unbounded: from the middle, as wide as `radius`
    float radius = 0;          // 0: to the farthest corner
    std::optional<Color> color;  // unset: the content color
    Indication() = default;
    Indication(Kind k) : kind(k) {}
    Indication(bool shown) : kind(shown ? Kind::Ripple : Kind::None) {}  // false: none (Compose's indication = null)
    // One of one's own (Custom): equal to another made by the same factory.
    explicit Indication(std::shared_ptr<const IndicationNodeFactory> f) : kind(f ? Kind::Custom : Kind::None), factory(std::move(f)) {}
    std::shared_ptr<const IndicationNodeFactory> factory;
    friend bool operator==(const Indication&, const Indication&) = default;
};
inline Indication ripple(bool bounded = true, float radius = 0, std::optional<Color> color = {}) {
    Indication i;
    i.bounded = bounded;
    i.radius = radius;
    i.color = color;
    return i;
}
// What clickables without their own indication use (the Material theme's ripple).
inline const CompositionLocal<Indication> LocalIndication{Indication{}};
// The least room (dp) minimumInteractiveComponentSize keeps for something to touch (Compose's
// LocalMinimumInteractiveComponentSize); 0 keeps none.
inline const CompositionLocal<float> LocalMinimumInteractiveComponentSize{48};

// Scroll position shared between the composition and the layout. Changing it only
// re-runs layout and drawing, never composition.
class ScrollState {
public:
    ScrollState() = default;
    explicit ScrollState(float initial) { data_->value.set(std::max(0.f, initial)); }  // kept until the first layout says how far it goes
    // Observable (Compose's): a composable reading them recomposes as it scrolls.
    float value() const { return data_->value.get(); }
    float maxValue() const { return data_->maxValue.get(); }
    bool canScrollForward() const { return value() < maxValue(); }
    bool canScrollBackward() const { return value() > 0; }
    // Observable: dragged or flung by the user, or scrolling by animateScrollTo / animateScrollBy.
    bool isScrollInProgress() const { return data_->userScrolling.get() || data_->animations.get() > 0; }
    float viewportSize() const { return data_->viewportSize; }  // dp in view along the scroll, from the last layout
    void setViewportSize(float size) const { data_->viewportSize = std::max(0.f, size); }
    void scrollTo(float v) const {
        float clamped = std::clamp(v, 0.f, data_->maxValue.peek());
        if (clamped == data_->value.peek()) return;
        data_->value.set(clamped);  // the scroller read it as it measured: it's measured again
    }
    // Returns how much of `delta` was used (less at either end).
    float scrollBy(float delta) const {
        float before = data_->value.peek();
        scrollTo(before + delta);
        return data_->value.peek() - before;
    }
    void setMaxValue(float max) const {
        if (float m = std::max(0.f, max); m != data_->maxValue.peek()) data_->maxValue.set(m);
        scrollTo(data_->value.peek());
    }
    // Animated, by a spring unless `spec` says otherwise (Compose's animateScrollTo/By): run it in
    // a coroutine, it ends when it gets there. A drag, a fling or another one stops it.
    Task animateScrollTo(float value) const;
    Task animateScrollTo(float value, const AnimationSpec& spec) const;
    Task animateScrollBy(float delta) const;
    Task animateScrollBy(float delta, const AnimationSpec& spec) const;
    void stopScroll() const { ++data_->generation; }  // stops a running animated scroll
    // Hears the user's drags of it (collectIsDraggedAsState()).
    const MutableInteractionSource& interactionSource() const { return data_->interactions; }
    friend bool operator==(const ScrollState& a, const ScrollState& b) { return a.data_ == b.data_; }

private:
    friend struct ScrollElement;
    Task animate(float delta, AnimationSpec spec) const;
    struct Data {
        MutableState<float> value{0}, maxValue{0};
        float viewportSize = 0;
        uint64_t generation = 0;  // moved on by whatever stops an animated scroll
        MutableState<bool> userScrolling{false};  // dragged or flung (the host says)
        MutableState<int> animations{0};          // animated scrolls running
        MutableInteractionSource interactions;
    };
    std::shared_ptr<Data> data_ = std::make_shared<Data>();
};

template <>
struct Saver<ScrollState> {
    static SavedValue save(const ScrollState& s) { return s.value(); }
    static std::optional<ScrollState> restore(const SavedValue& v) {
        auto n = v.number();
        return n ? std::optional(ScrollState(float(*n))) : std::nullopt;
    }
};

enum class Orientation { Vertical, Horizontal };

// Modifier::scrollable(): what a drag or fling along it does. The callback gets each delta in
// the finger's direction (positive: down or right) and returns how much of it it used; the
// rest goes to the scrollables around it.
class ScrollableState {
public:
    explicit ScrollableState(std::function<float(float)> consumeScrollDelta = {})
        : data_(std::make_shared<Data>(Data{std::move(consumeScrollDelta)})) {}
    float dispatchRawDelta(float delta) const { return data_->consume ? data_->consume(delta) : 0; }
    friend bool operator==(const ScrollableState& a, const ScrollableState& b) { return a.data_ == b.data_; }

    struct Data {
        std::function<float(float)> consume;
    };
    std::shared_ptr<Data> data_;
};

// dp per second, following the finger (see nested_scroll.hpp).
struct Velocity {
    float x = 0, y = 0;
    friend constexpr bool operator==(const Velocity&, const Velocity&) = default;
};

// What moves a scroll step that is offered to nested scroll parents: a drag, a fling or the
// mouse wheel. (Compose's Drag and Fling are the old names of UserInput and SideEffect.)
enum class NestedScrollSource { UserInput, SideEffect, Wheel, Drag = UserInput, Fling = SideEffect };

using MeasureNext = std::function<Size(const Constraints&)>;
// What approachLayout's measure knows: the size its content is going to and the constraints it
// was measured with for it.
struct ApproachMeasureScope {
    Size lookaheadSize;
    Constraints lookaheadConstraints;
};

class LayoutNode;

namespace detail {
// While a GraphicsLayer records (DrawScope::record): where a modifier's drawContent() draws
// instead of the canvas it was given, so the content goes into the recording.
GraphicsContext*& recordingInto();
// While Modifier::blur(BlurType::Self) draws `node` in two parts: what the node draws past its
// modifier's elements is only its own drawing (`own`), or only its children, drawn onto
// `children` (the elements' own drawing between swallowed), recordingInto() as it was outside.
struct DrawPart {
    const LayoutNode* node = nullptr;
    bool own = false;
    GraphicsContext* children = nullptr;
    GraphicsContext* recording = nullptr;
};
DrawPart& drawPart();
} // namespace detail

// How sharedBounds fits its contents into the moving bounds (Compose's ResizeMode): each laid
// out at its own size and scaled by `contentScale` (FillWidth by default: text doesn't reflow),
// placed by `alignment`; or laid out again at the bounds' size every frame (RemeasureToBounds).
struct SharedResizeMode {
    bool remeasure = false;
    ContentScale contentScale = ContentScale::FillWidth;
    Alignment alignment = Alignment::Center;
    static SharedResizeMode scaleToBounds(ContentScale contentScale = ContentScale::FillWidth, Alignment alignment = Alignment::Center) {
        return {false, contentScale, alignment};
    }
    static SharedResizeMode remeasureToBounds() { return {true}; }
    friend bool operator==(const SharedResizeMode&, const SharedResizeMode&) = default;
};

// Overscroll (Compose's OverscrollFactory): the stretch a scroll container shows when a finger
// pulls it past an end. LocalOverscrollFactory = std::nullopt turns it off for the scroll
// containers made inside (Compose's `LocalOverscrollFactory provides null`):
//   CompositionLocalProvider({LocalOverscrollFactory.provides(std::nullopt)}, [&] { ... });
struct OverscrollFactory {
    friend constexpr bool operator==(const OverscrollFactory&, const OverscrollFactory&) = default;
};
inline const CompositionLocal<std::optional<OverscrollFactory>> LocalOverscrollFactory{OverscrollFactory{}};
namespace detail {
// Whether a scroll container made now stretches: LocalOverscrollFactory's (yes outside a composition).
bool overscrollWanted();
} // namespace detail

class ContentDrawScope;

// How a scroll container brings something inside it into view (Compose's BringIntoViewSpec):
// a focused item, a field above the keyboard, a BringIntoViewRequester. `offset` is the item's
// leading edge from the viewport's (dp along it), `size` the item's, `containerSize` the
// viewport's; it returns how far to scroll towards the end (negative: back; 0: leave it). Unset,
// the default: just far enough that it shows, its start first if it is bigger than the viewport.
// Provide one for the containers made inside, e.g. to keep the focused item a third of the way in:
//   CompositionLocalProvider({LocalBringIntoViewSpec.provides(BringIntoViewSpec{
//       [](float offset, float size, float container) { return offset - (container - size) * 0.3f; }})}, ...);
struct BringIntoViewSpec {
    std::function<float(float offset, float size, float containerSize)> calculateScrollDistance;
    float scrollDistance(float offset, float size, float containerSize) const {
        return calculateScrollDistance ? calculateScrollDistance(offset, size, containerSize)
                                       : defaultScrollDistance(offset, size, containerSize);
    }
    static float defaultScrollDistance(float offset, float size, float containerSize) {
        float trailing = offset + size;  // forward until its end shows (but never past its start), or back to its start
        return trailing > containerSize ? std::min(trailing - containerSize, offset) : offset < 0 ? offset : 0.f;
    }
};
inline const CompositionLocal<BringIntoViewSpec> LocalBringIntoViewSpec{BringIntoViewSpec{}};
namespace detail {
BringIntoViewSpec currentBringIntoViewSpec();  // LocalBringIntoViewSpec's (the default outside a composition)
} // namespace detail

// What a scroll container does with a finger's drag and fling past its ends, and how that shows
// (Compose's OverscrollEffect). The scroll containers have Android 12's stretch by default
// (rememberOverscrollEffect()); give one your own, or none (nullptr):
//   LazyColumn(Modifier(), {.overscrollEffect = nullptr}, ...);         // no stretch
//   auto effect = rememberOverscrollEffect();                           // one stretch for two:
//   Box(Modifier().overscroll(effect).scrollable(state, Orientation::Vertical, effect), ...);
// Deltas and velocities go the finger's way (dp, dp/s), as in nested scrolling. Only touch
// drags and their flings go through it (not the mouse, the wheel or code).
class OverscrollEffect {
public:
    virtual ~OverscrollEffect() = default;
    // A drag step of `delta`: it may take some first (taking back what it shows), must call
    // performScroll exactly once with the rest (the scroll and its nested scroll parents), and
    // may show what that left; returns what it and performScroll used.
    virtual Offset applyToScroll(Offset delta, NestedScrollSource source, const std::function<Offset(Offset)>& performScroll) = 0;
    // The finger let go at `velocity` (the end of a drag, even a still one): it may take some
    // first, must co_await performFling exactly once with the rest (the fling and its parents;
    // it returns the velocity used — the rest is what was left when it hit an end), and may
    // animate after it.
    virtual Task applyToFling(Velocity velocity, std::function<TaskOf<Velocity>(Velocity)> performFling) = 0;
    // Whether it shows anything now.
    virtual bool isInProgress() const = 0;
    // How it shows (Compose's node): draw around scope.drawContent() — the scroll container's
    // viewport, or where Modifier.overscroll put it. By default the content as it is.
    virtual void draw(ContentDrawScope& scope);
    // Its size where it is drawn, after each layout.
    virtual void onRemeasured(Size size) { (void)size; }
};

// The scroll containers' own: Android 12's stretch (LocalOverscrollFactory's), null under
// LocalOverscrollFactory = std::nullopt. Pass it to a scroll container, or to scrollable() with
// Modifier.overscroll() where it should show.
std::shared_ptr<OverscrollEffect> rememberOverscrollEffect(CallSite site = CallSite::current());
// The same effect, not drawn (another node draws it with Modifier.overscroll) / not taking the
// drags and flings (only drawn here).
std::shared_ptr<OverscrollEffect> withoutVisualEffect(std::shared_ptr<OverscrollEffect> effect);
std::shared_ptr<OverscrollEffect> withoutEventHandling(std::shared_ptr<OverscrollEffect> effect);

namespace detail {
// Android 12's stretch: the content grows along an axis from the edge pulled past, up to 35%
// longer; it springs back once let go, and a fling hitting an end bumps it.
class StretchOverscroll final : public OverscrollEffect {
public:
    Offset applyToScroll(Offset delta, NestedScrollSource source, const std::function<Offset(Offset)>& performScroll) override;
    Task applyToFling(Velocity velocity, std::function<TaskOf<Velocity>(Velocity)> performFling) override;
    bool isInProgress() const override;
    void draw(ContentDrawScope& scope) override;
    void onRemeasured(Size size) override { size_ = size; }
    // Per axis (vertical, horizontal): a fraction of its size, positive at the start edge, and
    // how fast that changes.
    float amount[2] = {}, velocity[2] = {};

private:
    float length(int axis) const { return axis == 0 ? size_.height : size_.width; }
    Size size_;
    bool held_ = false;  // a finger is on it (since a drag step, until let go)
    int releases_ = 0;   // let go this many times: an older spring back stops
};
} // namespace detail

// One link of a Modifier chain. Each element wraps everything after it, so order matters:
// padding(8).background(red) differs from background(red).padding(8), exactly as in Compose.
class ModifierElement {
public:
    virtual ~ModifierElement() = default;

    // Measures the wrapped content through `next`, returns this layer's size and where the
    // wrapped content is placed inside it.
    virtual Size measure(const Constraints& c, const MeasureNext& next, Offset& contentOffset) const {
        contentOffset = {};
        return next(c);
    }
    // `bounds` is this layer's absolute rect; drawContent() draws everything it wraps.
    virtual void draw(GraphicsContext&, const Rect& bounds, const LayoutNode& node, const std::function<void()>& drawContent) const {
        (void)bounds, (void)node;
        drawContent();
    }
    virtual bool clipsContent() const { return false; }
    // What it clips to, given its bounds, when clipsContent (its bounds, unless it says more).
    virtual Rect clipBounds(const Rect& bounds) const { return bounds; }
    // Taps, clicks and keyboard focus pass over the node and everything in it (content leaving
    // with an animation).
    virtual bool ignoresInput() const { return false; }
    // Taps anywhere in it reach the input elements inside it (minimumInteractiveComponentSize:
    // the room kept around a smaller control for touching it).
    virtual bool expandsTouchArea() const { return false; }
    // Its content's offset mirrors in a right-to-left layout (padding, alignment, offset); not
    // for absolute ones (absoluteOffset, window insets).
    virtual bool mirrorsInRtl() const { return true; }
    // Called on every layout of the node that carries it.
    virtual void attach(LayoutNode& node) const { (void)node; }
    // Value equality, so that a composable called with an equal Modifier can be skipped.
    // Elements holding callbacks can't be compared and are only equal to themselves.
    virtual bool equals(const ModifierElement& other) const { return this == &other; }
};

struct ClickableElement final : ModifierElement {
    std::function<void()> onClick;
    std::function<void()> onLongClick, onDoubleClick;  // combinedClickable()
    bool enabled = true;
    Shape shape;  // of the pressed/hover overlay
    Color indicationColor = colors::Black;
    Indication indication;
    std::optional<MutableInteractionSource> interactionSource;  // told of presses, hovers and focus
    void draw(GraphicsContext&, const Rect&, const LayoutNode&, const std::function<void()>&) const override;
};

// A long press target that goes before what is under it (the click there then doesn't happen):
// a TooltipBox's anchor, which also hears of a mouse pointer going over it and away, and a
// selectable Text, which hears where it was pressed.
struct TooltipTriggerElement final : ModifierElement {
    std::function<void()> onLongPress;
    std::function<void(Offset local)> onLongPressAt;
    std::function<void(bool)> onHoverChange;
};

// Anything the user can drag, fling or wheel to scroll. The host routes gestures to the
// innermost scrollable of the matching orientation.
class ScrollableElement : public ModifierElement {
public:
    virtual Orientation orientation() const = 0;
    // Scrolls `delta` dp towards the end (negative: towards the start); returns what was used.
    virtual float scrollBy(float delta) const = 0;
    // As a nested scroll parent: takes what a scrollable inside it left over.
    virtual float scrollByNested(float delta, NestedScrollSource source) const {
        (void)source;
        return scrollBy(delta);
    }
    // A drag on it ended with `velocity` dp/s towards the end (or a fling inside it ended with
    // that much left). Returns true if it settles by itself (a pager snapping to a page) instead
    // of the host's decaying fling.
    virtual bool settle(float velocity) const {
        (void)velocity;
        return false;
    }
    // A finger's drag and fling past an end go through its overscroll effect: the one it was
    // given, or else (overscrolls()) the stretch its node keeps (verticalScroll, lazy lists,
    // pagers; not a bare scrollable(); none under LocalOverscrollFactory = std::nullopt).
    std::shared_ptr<OverscrollEffect> overscrollEffect;
    virtual bool overscrolls() const { return overscroll; }
    bool overscroll = true;  // set as it is made: detail::overscrollWanted(); false with an effect given
    // Draws its effect around its content (a bare scrollable doesn't: Modifier.overscroll does).
    virtual bool drawsOverscroll() const { return true; }
    // How far it scrolls to bring something into view (LocalBringIntoViewSpec where it was made).
    BringIntoViewSpec bringIntoViewSpec;
    // false (Compose's userScrollEnabled / enabled): scrolled only in code and to bring something
    // into view; no drags, flings, wheel, nested scroll or accessibility scrolling.
    bool userScrollEnabled = true;
    // Told when the user starts and ends dragging it (DragStart, DragStop / DragCancel).
    virtual std::optional<MutableInteractionSource> interactionSource() const { return std::nullopt; }
    virtual bool scrollsWithWheel() const { return true; }  // anchoredDraggable: only drags move it
    // Horizontal, in a right-to-left layout: starts at the right and follows the finger the other
    // way (scrolls, lazy rows, pagers; not anchoredDraggable, which keeps to the screen as in Compose).
    virtual bool followsLayoutDirection() const { return true; }
    // Told when the user starts moving it (a drag, then the fling after it) and when that ends:
    // its state's isScrollInProgress.
    virtual void scrollInProgress(bool on) const { (void)on; }
    bool clipsContent() const override { return true; }
    // Its bounds along it, and 30 dp more each side across (Compose's clipScrollableContainer):
    // what scrolls out goes, the shadows of its cards show.
    Rect clipBounds(const Rect& bounds) const override {
        constexpr float kMaxSupportedElevation = 30;
        return orientation() == Orientation::Vertical
                   ? Rect{bounds.x - kMaxSupportedElevation, bounds.y, bounds.width + 2 * kMaxSupportedElevation, bounds.height}
                   : Rect{bounds.x, bounds.y - kMaxSupportedElevation, bounds.width, bounds.height + 2 * kMaxSupportedElevation};
    }
};

// scrollable()
struct ScrollableModifierElement final : ScrollableElement {
    std::shared_ptr<ScrollableState::Data> state;
    Orientation axis = Orientation::Vertical;
    bool reverse = false;
    bool area = false;  // scrollableArea(): clipped along it, its overscroll (the stretch, or its effect) drawn here
    std::optional<MutableInteractionSource> source;
    Orientation orientation() const override { return axis; }
    std::optional<MutableInteractionSource> interactionSource() const override { return source; }
    bool overscrolls() const override { return area && overscroll; }
    bool drawsOverscroll() const override { return area; }
    float scrollBy(float delta) const override {
        if (!state->consume) return 0;
        float finger = reverse ? delta : -delta;  // the state sees the finger's direction
        float used = state->consume(finger);
        return reverse ? used : -used;
    }
    bool clipsContent() const override { return area; }
    void draw(GraphicsContext&, const Rect&, const LayoutNode&, const std::function<void()>&) const override;
};

// verticalScroll() / horizontalScroll()
struct ScrollElement final : ScrollableElement {
    ScrollState state;
    Orientation axis = Orientation::Vertical;
    bool reverse = false;  // reverseScrolling: 0 at the end (the bottom), scrolling on goes back
    Orientation orientation() const override { return axis; }
    std::optional<MutableInteractionSource> interactionSource() const override { return state.interactionSource(); }
    float scrollBy(float delta) const override {
        state.stopScroll();  // the user (or bringing a field into view) takes over
        return reverse ? -state.scrollBy(-delta) : state.scrollBy(delta);
    }
    void scrollInProgress(bool on) const override {
        if (state.data_->userScrolling.peek() != on) state.data_->userScrolling.set(on);
    }
    Size measure(const Constraints&, const MeasureNext&, Offset&) const override;
    void draw(GraphicsContext&, const Rect&, const LayoutNode&, const std::function<void()>&) const override;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const ScrollElement*>(&other);
        return o && o->state == state && o->axis == axis && o->overscroll == overscroll && o->overscrollEffect == overscrollEffect &&
               o->reverse == reverse && o->userScrollEnabled == userScrollEnabled && !bringIntoViewSpec.calculateScrollDistance &&
               !o->bringIntoViewSpec.calculateScrollDistance;
    }
};

// The complete editing state as an input method sees it. Offsets are UTF-8 byte offsets
// into `text`; the composition is the word an IME is still working on (-1 when none).
// A range of text, in bytes of its UTF-8 (Compose's TextRange): a selection, or a caret where
// start == end. start may come after end (a selection made backwards).
struct TextRange {
    int start = 0, end = 0;
    int min() const { return start < end ? start : end; }
    int max() const { return start < end ? end : start; }
    int length() const { return max() - min(); }
    bool collapsed() const { return start == end; }
    friend bool operator==(const TextRange&, const TextRange&) = default;
};

struct TextEditState {
    std::string text;
    int selectionStart = 0, selectionEnd = 0;
    int compositionStart = -1, compositionEnd = -1;

    static TextEditState caretAtEnd(std::string text) {
        int end = int(text.size());
        return {std::move(text), end, end};
    }
    friend bool operator==(const TextEditState&, const TextEditState&) = default;
};

// Receives text and key input while its node is focused. Input methods always report the
// whole new state, never a delta, so the field and the IME cannot drift apart.
enum class CaretMove { LineUp, LineDown, LineStart, LineEnd };

// A focus target. With onEdit it is a text field (taps focus it, the keyboard shows); without,
// a plain focusable() reached with Tab or the arrow keys.
struct FocusableElement final : ModifierElement {
    std::string value;  // current text, sent to the input method when it (re)connects
    std::function<void(const TextEditState&)> onEdit;
    bool singleLine = true;  // false: Enter starts a new line
    // Multi-line fields: where the caret goes from `offset` (Up / Down, Home / End).
    std::function<int(int offset, CaretMove move)> moveCaret;
    std::function<void(ImeAction)> onImeAction;  // Enter / the keyboard's action key
    std::function<int(Offset local)> offsetAt;   // text offset under a point (tap to move the caret)
    KeyboardOptions keyboardOptions;
    bool allowCopy = true;  // false for passwords
    bool readOnly = false;  // focusable and selectable, but no keyboard comes up
    // Touch selection started (on) or ended: a long press selected a word (the handles and the
    // toolbar show), or a tap put the caret (a caret handle shows; the toolbar on a tap on it).
    std::function<void(bool on, bool toolbar)> onTouchSelection;
    std::optional<MutableInteractionSource> interactionSource;  // told of focus (and a text field's presses)
    // A field whose state keeps its selection (TextFieldState): the input method starts from it
    // and takes it when the app changes it. Unset, the caret goes to the end of a new value.
    std::optional<TextRange> selection;
    // Turns an edit typed or pasted into the one kept (an input transformation: a maximum
    // length, capitals, digits only); the input method is told when it differs.
    std::function<TextEditState(const TextEditState& before, const TextEditState& proposed)> transformEdit;
    // Opens the field's own context menu (cut, copy, paste, select all) at a point in the window.
    std::function<void(Offset window)> onContextMenu;
};

// A context menu's area (ContextMenuArea): a secondary click on it opens the menu there.
struct ContextMenuElement final : ModifierElement {
    std::function<void(Offset window)> onOpen;
};

// Around a text field (its icons): a tap here that no clickable takes goes to the field inside,
// the caret to the nearest end of its text — the decoration is part of the field, as in Compose.
struct TextFieldDecorationElement final : ModifierElement {
    bool equals(const ModifierElement& other) const override { return dynamic_cast<const TextFieldDecorationElement*>(&other) != nullptr; }
};

// hoverable(): told when a mouse pointer goes over it and away (touch has no hover).
struct HoverableElement final : ModifierElement {
    MutableInteractionSource source;
};

// indication(source, indication): a source's presses, hovers and focus shown on this node.
struct IndicationElement final : ModifierElement {
    MutableInteractionSource source;
    Indication indication;
    Shape shape;  // of the last background(), border() or clip() before it
    Color color = colors::Black;  // the content color where it was made
    void draw(GraphicsContext&, const Rect&, const LayoutNode&, const std::function<void()>&) const override;
};

// pointerHoverIcon(): the mouse pointer's look over it.
struct PointerHoverIconElement final : ModifierElement {
    Cursor icon = Cursor::Default;
    bool overrideDescendants = false;  // also over what is inside it (a clickable's hand, a field's I-beam, their own icons)
};

// onKeyEvent() / onPreviewKeyEvent(): keys reach the focused node and the nodes around it. Preview
// handlers run first, from the outermost in; then the focused text field; then onKeyEvent
// handlers from the focused node out. Returning true stops the key there.
struct KeyEventElement final : ModifierElement {
    std::function<bool(const KeyEvent&)> handler;
    bool preview = false;
};

// onFocusChanged(): told when the node gets or loses focus (itself or inside it).
struct FocusChangedElement final : ModifierElement {
    std::function<void(FocusState)> onFocusChanged;
    bool everyEvent = false;  // onFocusEvent: told of every focus event, the state the same or not
};

// Moves focus from code: remember a FocusRequester, attach it with Modifier::focusRequester
// and call requestFocus() (from an event handler or an effect).
class FocusRequester {
public:
    FocusRequester();
    void requestFocus() const;
    void freeFocus() const;  // clears the focus if this requester's node has it
    // In focusProperties: the move doesn't happen, the focus stays (Compose's FocusRequester.Cancel).
    static FocusRequester cancel();
    friend bool operator==(const FocusRequester& a, const FocusRequester& b) { return a.data_ == b.data_; }

    struct Data {
        ComposeHost* host = nullptr;  // set when attached
    };
    std::shared_ptr<Data> data_;
};

// Where keyboard focus may go (Compose's focusProperties): whether the node can take it at all
// (no Tab, no tap, no requestFocus), and where each move from it goes instead of the next or
// nearest one — a node with that FocusRequester, or FocusRequester::cancel() to stay put.
//   TextField(..., Modifier().focusProperties({.next = sendButton}));
struct FocusProperties {
    bool canFocus = true;
    std::optional<FocusRequester> next, previous, up, down, left, right;
    friend bool operator==(const FocusProperties&, const FocusProperties&) = default;
};
struct FocusPropertiesElement final : ModifierElement {
    FocusProperties properties;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const FocusPropertiesElement*>(&other);
        return o && o->properties == properties;
    }
};

// Modifier::magnifier's look (Compose's Modifier.magnifier; these defaults are Android's text
// magnifier's): the content around the source `zoom` times bigger, in a rounded rect of `size`
// with a shadow of `elevation`, centered at magnifierCenter (in the node; unset: 42 dp above
// the source).
struct MagnifierOptions {
    std::function<Offset()> magnifierCenter;
    float zoom = 1.25f;
    Size size{100, 48};
    float cornerRadius = 24;
    float elevation = 4;
};
struct MagnifierElement final : ModifierElement {
    std::function<Offset()> sourceCenter;  // in the node; NaN: none shown
    MagnifierOptions options;
    void attach(LayoutNode& node) const override;
};

// Modifier::focusGroup(): the arrow keys look for the next focus among what is inside first.
struct FocusGroupElement final : ModifierElement {
    bool equals(const ModifierElement& other) const override { return dynamic_cast<const FocusGroupElement*>(&other) != nullptr; }
};

// Modifier::focusRestorer: focus coming back into it goes to where it left (the host keeps that).
struct FocusRestorerElement final : ModifierElement {
    std::optional<FocusRequester> fallback;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const FocusRestorerElement*>(&other);
        return o && o->fallback == fallback;
    }
};

// ---- Drag and drop -------------------------------------------------------------------------

// What a drag carries (Compose's DragAndDropTransferData): text and its MIME types, and for a
// drag within the app anything at all.
struct DragAndDropTransferData {
    std::string text;
    std::vector<std::string> mimeTypes{"text/plain"};
    std::any localState;
    std::vector<std::string> files;  // dropped from outside the app ("text/uri-list"): their paths
};
struct DragAndDropEvent {
    std::shared_ptr<const DragAndDropTransferData> data;
    Offset position;        // the pointer, in the target's coordinates (dp)
    bool accepted = false;  // in onEnded: whether a target took the drop
    const std::string& text() const { return data->text; }
    const std::vector<std::string>& mimeTypes() const { return data->mimeTypes; }
    bool hasMimeType(std::string_view type) const {
        return std::find(data->mimeTypes.begin(), data->mimeTypes.end(), type) != data->mimeTypes.end();
    }
    template <class T>
    const T* localState() const {  // null if it isn't a T
        return std::any_cast<T>(&data->localState);
    }
};
// What a target does with a drag that takes part (Compose's DragAndDropTarget): onStarted when
// it begins, onEntered / onMoved / onExited as the pointer goes over it, onDrop when it is
// released over it (true: taken), onEnded when it is over, wherever it was dropped.
struct DragAndDropTarget {
    std::function<bool(const DragAndDropEvent&)> onDrop;
    std::function<void(const DragAndDropEvent&)> onStarted, onEntered, onMoved, onExited, onEnded;
};
struct DragAndDropSourceElement final : ModifierElement {
    std::function<std::optional<DragAndDropTransferData>(Offset)> transferData;
};
struct DragAndDropTargetElement final : ModifierElement {
    std::function<bool(const DragAndDropEvent&)> shouldStart;
    DragAndDropTarget target;
};

struct FocusRequesterElement final : ModifierElement {
    FocusRequester requester;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const FocusRequesterElement*>(&other);
        return o && o->requester == requester;
    }
};

// Scrolls the node it is attached to into view (Compose's BringIntoViewRequester), through the
// scrollables around it, the innermost first:
//   auto& requester = remember([] { return BringIntoViewRequester(); });
//   Box(Modifier().bringIntoViewRequester(requester)...);
//   scope.launch([requester]() -> Task { co_await requester.bringIntoView(); });
class BringIntoViewRequester {
public:
    BringIntoViewRequester();
    // In a coroutine (Compose's suspend bringIntoView): `rect` of the node (from its top left;
    // unset: all of it) scrolled just into view on the scrollables' spring, as little as it takes;
    // done once it shows (or the node is gone).
    Task bringIntoView(std::optional<Rect> rect = std::nullopt) const;
    friend bool operator==(const BringIntoViewRequester& a, const BringIntoViewRequester& b) { return a.data_ == b.data_; }

    struct Data {
        std::weak_ptr<LayoutNode*> node;  // set when attached
    };
    std::shared_ptr<Data> data_;
};
struct BringIntoViewRequesterElement final : ModifierElement {
    BringIntoViewRequester requester;
    void attach(LayoutNode& node) const override;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const BringIntoViewRequesterElement*>(&other);
        return o && o->requester == requester;
    }
};

// LocalFocusManager: clears or moves the focus (Next / Previous in layout order).
// Next / Previous: in reading order (Tab); Up / Down / Left / Right: the nearest one that way.
enum class FocusDirection { Next, Previous, Up, Down, Left, Right };
class FocusManager {
public:
    explicit FocusManager(ComposeHost* host) : host_(host) {}
    void clearFocus() const;
    bool moveFocus(FocusDirection direction) const;

private:
    ComposeHost* host_;
};
FocusManager focusManager();  // inside a composition

// What a node means to accessibility services (TalkBack) and tests: Modifier::semantics.
enum class SemanticsRole { None, Button, Checkbox, Switch, RadioButton, Tab, Image, Slider };
enum class ToggleableState { On, Off, Indeterminate };  // TriStateCheckbox, triStateToggleable
// A value within a range (Compose's ProgressBarRangeInfo): a slider's or a progress bar's, with
// `steps` values in between (0: any); Indeterminate: progress without a value.
struct ProgressBarRangeInfo {
    float current = 0, rangeStart = 0, rangeEnd = 1;
    int steps = 0;
    friend bool operator==(const ProgressBarRangeInfo&, const ProgressBarRangeInfo&) = default;
    static const ProgressBarRangeInfo Indeterminate;
};
inline const ProgressBarRangeInfo ProgressBarRangeInfo::Indeterminate{0, 0, 0, 0};
// How accessibility services announce a change to a node's text (Compose's LiveRegionMode):
// Polite waits for what is being said, Assertive cuts in.
enum class LiveRegionMode { None, Polite, Assertive };
// An action of a node's own that accessibility services offer (TalkBack's actions menu), as
// Compose's CustomAccessibilityAction: returns whether it was done.
struct CustomAccessibilityAction {
    std::string label;
    std::function<bool()> action;
};
// What a text field takes (Compose's ContentType), for the platform's autofill: a password
// manager offers what it keeps for it, and to keep what was typed into it. Combined with +:
//   TextField(user, setUser, Modifier().semantics({.contentType = ContentType::Username + ContentType::EmailAddress}));
struct ContentType {
    std::vector<std::string> hints;  // Android's autofill hints
    friend ContentType operator+(ContentType a, const ContentType& b) {
        a.hints.insert(a.hints.end(), b.hints.begin(), b.hints.end());
        return a;
    }
    friend bool operator==(const ContentType&, const ContentType&) = default;
    static const ContentType Username, Password, EmailAddress, NewUsername, NewPassword, PostalAddress, PostalCode,
        CreditCardNumber, CreditCardSecurityCode, CreditCardExpirationDate, CreditCardExpirationMonth, CreditCardExpirationYear,
        CreditCardExpirationDay, AddressCountry, AddressRegion, AddressLocality, AddressStreet, AddressAuxiliaryDetails,
        PostalCodeExtended, PersonFullName, PersonFirstName, PersonLastName, PersonMiddleName, PersonMiddleInitial,
        PersonNamePrefix, PersonNameSuffix, PhoneNumber, PhoneNumberDevice, PhoneCountryCode, PhoneNumberNational, Gender,
        BirthDateFull, BirthDateDay, BirthDateMonth, BirthDateYear, SmsOtpCode;
};
inline const ContentType ContentType::Username{{"username"}}, ContentType::Password{{"password"}},
    ContentType::EmailAddress{{"emailAddress"}}, ContentType::NewUsername{{"newUsername"}}, ContentType::NewPassword{{"newPassword"}},
    ContentType::PostalAddress{{"postalAddress"}}, ContentType::PostalCode{{"postalCode"}},
    ContentType::CreditCardNumber{{"creditCardNumber"}}, ContentType::CreditCardSecurityCode{{"creditCardSecurityCode"}},
    ContentType::CreditCardExpirationDate{{"creditCardExpirationDate"}}, ContentType::CreditCardExpirationMonth{{"creditCardExpirationMonth"}},
    ContentType::CreditCardExpirationYear{{"creditCardExpirationYear"}}, ContentType::CreditCardExpirationDay{{"creditCardExpirationDay"}},
    ContentType::AddressCountry{{"addressCountry"}}, ContentType::AddressRegion{{"addressRegion"}},
    ContentType::AddressLocality{{"addressLocality"}}, ContentType::AddressStreet{{"streetAddress"}},
    ContentType::AddressAuxiliaryDetails{{"extendedAddress"}}, ContentType::PostalCodeExtended{{"extendedPostalCode"}},
    ContentType::PersonFullName{{"personName"}}, ContentType::PersonFirstName{{"personGivenName"}},
    ContentType::PersonLastName{{"personFamilyName"}}, ContentType::PersonMiddleName{{"personMiddleName"}},
    ContentType::PersonMiddleInitial{{"personMiddleInitial"}}, ContentType::PersonNamePrefix{{"personNamePrefix"}},
    ContentType::PersonNameSuffix{{"personNameSuffix"}}, ContentType::PhoneNumber{{"phoneNumber"}},
    ContentType::PhoneNumberDevice{{"phoneNumberDevice"}}, ContentType::PhoneCountryCode{{"phoneCountryCode"}},
    ContentType::PhoneNumberNational{{"phoneNational"}}, ContentType::Gender{{"gender"}}, ContentType::BirthDateFull{{"birthDateFull"}},
    ContentType::BirthDateDay{{"birthDateDay"}}, ContentType::BirthDateMonth{{"birthDateMonth"}},
    ContentType::BirthDateYear{{"birthDateYear"}}, ContentType::SmsOtpCode{{"smsOTPCode"}};

struct SemanticsProperties {
    std::string contentDescription;
    SemanticsRole role = SemanticsRole::None;
    std::optional<bool> checked;  // checkboxes, switches, radio buttons, selected tabs
    std::string stateDescription;  // e.g. a slider's value
    bool heading = false;
    bool selectableGroup = false;  // the radio buttons (selectables) in it belong together
    // What is inside is said as part of it, as a clickable's is (an icon and the texts of a
    // row read as one); interactive nodes inside still stand on their own.
    bool mergeDescendants = false;
    bool clearDescendants = false;  // clearAndSetSemantics: only what is set here is said
    bool hidden = false;            // hideFromAccessibility: neither it nor what is inside is said
    std::optional<ProgressBarRangeInfo> progressBarRangeInfo;
    // Accessibility services set the value with it (a slider's; Compose's setProgress).
    std::function<bool(float)> setProgress;
    LiveRegionMode liveRegion = LiveRegionMode::None;
    std::vector<CustomAccessibilityAction> customActions;
    // Its place among the nodes of its semantics parent, the lower first (Compose's
    // traversalIndex); equal ones keep their order.
    float traversalIndex = 0;
    // For autofill: what the text field in it takes (Compose's contentType).
    std::optional<ContentType> contentType;
    bool hasCallbacks() const { return setProgress || !customActions.empty(); }
    friend bool operator==(const SemanticsProperties& a, const SemanticsProperties& b) {
        auto labels = [](const SemanticsProperties& p) {
            std::vector<std::string> out;
            for (const auto& action : p.customActions) out.push_back(action.label);
            return out;
        };
        return a.contentDescription == b.contentDescription && a.role == b.role && a.checked == b.checked &&
               a.stateDescription == b.stateDescription && a.heading == b.heading && a.selectableGroup == b.selectableGroup &&
               a.mergeDescendants == b.mergeDescendants && a.clearDescendants == b.clearDescendants && a.hidden == b.hidden &&
               a.progressBarRangeInfo == b.progressBarRangeInfo && bool(a.setProgress) == bool(b.setProgress) &&
               a.liveRegion == b.liveRegion && labels(a) == labels(b) && a.traversalIndex == b.traversalIndex &&
               a.contentType == b.contentType;
    }
};
struct SemanticsElement final : ModifierElement {
    std::string description;  // = properties.contentDescription
    SemanticsProperties properties;
    // Never the same as another while it holds callbacks: the newer ones must be the ones called.
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const SemanticsElement*>(&other);
        return o && o->properties == properties && !properties.hasCallbacks();
    }
};
class Modifier;
namespace detail {
// All of a node's semantics modifiers as one (a toggleable's role and state with the
// description given after it): descriptions joined, the first role, state and state
// description, any flag. None: std::nullopt.
std::optional<SemanticsProperties> semanticsOf(const Modifier& modifier);
}

// One node of the semantics tree the host hands to accessibility services (dp bounds).
struct SemanticsNode {
    int id = 0, parent = -1;  // ids stay the same while the node exists
    Rect bounds;
    std::string label;  // text or content description; a clickable node merges what it contains
    std::string value;  // a text field's text
    std::string stateDescription;
    SemanticsRole role = SemanticsRole::None;
    // focused: has the keyboard focus (a text field, or a clickable that Tab went to)
    bool clickable = false, enabled = true, focused = false, editable = false, scrollable = false, heading = false;
    bool selectableGroup = false;  // the selectables inside are one group (a list of choices)
    std::optional<bool> checked;
    std::optional<ProgressBarRangeInfo> range;
    bool settable = false;  // SetProgress sets its value
    LiveRegionMode liveRegion = LiveRegionMode::None;
    std::vector<std::string> customActions;  // their labels, CustomAction's argument their index
    // A text field's: what it takes (its own contentType or one around it), and whether it
    // hides what is typed (a password).
    std::vector<std::string> contentType;
    bool password = false;
};
// SetProgress's argument is the value; CustomAction's, the action's index; SetText's (a text
// field's whole text: autofill, accessibility services), the text. Focus: the keyboard focus, on a
// text field or an enabled clickable.
enum class SemanticsAction { Click, LongClick, Focus, ScrollForward, ScrollBackward, SetProgress, CustomAction, SetText };

// layoutId(): names a child for a custom Layout (Measurable::layoutId).
struct LayoutIdElement final : ModifierElement {
    std::string id;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const LayoutIdElement*>(&other);
        return o && o->id == id;
    }
};

enum class IntrinsicSize { Min, Max };

struct ZIndexElement final : ModifierElement {
    float zIndex = 0;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const ZIndexElement*>(&other);
        return o && o->zIndex == zIndex;
    }
};

struct TestTagElement final : ModifierElement {
    std::string tag;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const TestTagElement*>(&other);
        return o && o->tag == tag;
    }
};

// Information a child gives to its parent layout (weight in Row/Column, align in Box).
// A horizontal line a layout reports for others to line up with (Compose's AlignmentLine): a
// text's first and last baselines. The layouts around a text pass them on, measured from their
// own top (several: the topmost first baseline, the bottommost last one).
enum class AlignmentLine { FirstBaseline, LastBaseline };
inline constexpr AlignmentLine FirstBaseline = AlignmentLine::FirstBaseline;
inline constexpr AlignmentLine LastBaseline = AlignmentLine::LastBaseline;

// A FlexBox child's flexibility (Compose's FlexConfigScope, Modifier.flex): how much of the room
// left over it takes (grow, relative to its siblings'), how much it gives up when they don't fit
// (shrink, weighted by its base size), its size before either (basis: its natural size, dp, or a
// share of the box), where it sits across its line (alignSelf; Auto: the box's alignItems) and
// where in the order (lower first; equal: as composed).
enum class FlexAlignSelf { Auto, Start, Center, End, Stretch, Baseline };
struct FlexBasis {
    enum class Kind { Auto, Dp, Percent };
    Kind kind = Kind::Auto;
    float value = 0;
    static constexpr FlexBasis Auto() { return {}; }
    static constexpr FlexBasis Dp(float dp) { return {Kind::Dp, dp}; }
    static constexpr FlexBasis Percent(float fraction) { return {Kind::Percent, fraction}; }  // 0..1 of the box's main size
    friend constexpr bool operator==(const FlexBasis&, const FlexBasis&) = default;
};
struct FlexConfig {
    float grow = 0, shrink = 1;
    FlexBasis basis;
    FlexAlignSelf alignSelf = FlexAlignSelf::Auto;
    int order = 0;
    std::optional<AlignmentLine> baselineLine;  // alignSelf Baseline by this line (unset: the box's, else FirstBaseline)
    friend bool operator==(const FlexConfig&, const FlexConfig&) = default;
};

// A Grid child's cells (Compose's GridScope.gridItem): its row and column, 1-based (negative:
// from the end of the configured tracks, -1 the last; 0: wherever there's room), how many it
// spans and where it sits in them. Or a named area of the Grid's config.
inline constexpr int kGridMaxIndex = 1000;
struct GridItemConfig {
    int row = 0, column = 0, rowSpan = 1, columnSpan = 1;
    Alignment alignment = Alignment::TopStart;
    std::string area;
    friend bool operator==(const GridItemConfig&, const GridItemConfig&) = default;
};
// Rows (columns) first to last, both 1-based and included: {1, 2} is the first two.
struct GridRange {
    int first = 1, last = 1;
};

struct ParentData {
    float weight = 0;
    bool fill = true;
    std::optional<Alignment> boxAlign;
    std::optional<float> crossAxisBias;
    std::optional<AlignmentLine> alignBy;  // in a Row: lined up with the others by this line
    bool matchParentSize = false;  // in a Box: as big as the Box the others made
    std::optional<FlexConfig> flex;  // in a FlexBox
    std::optional<GridItemConfig> grid;  // in a Grid
    friend bool operator==(const ParentData&, const ParentData&) = default;
};

// Modifier::paddingFrom: room around the content so that an alignment line of it lands where asked.
struct AlignmentLinePaddingElement final : ModifierElement {
    AlignmentLine line = AlignmentLine::FirstBaseline;
    float before = std::numeric_limits<float>::quiet_NaN(), after = std::numeric_limits<float>::quiet_NaN();
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const AlignmentLinePaddingElement*>(&other);
        auto same = [](float a, float b) { return a == b || (a != a && b != b); };  // NaN: unset on both
        return o && o->line == line && same(o->before, before) && same(o->after, after);
    }
};

struct ParentDataElement final : ModifierElement {
    ParentData data;
    bool hasWeight = false;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const ParentDataElement*>(&other);
        return o && o->data == data && o->hasWeight == hasWeight;
    }
};

struct PaddingValues {
    float start = 0, top = 0, end = 0, bottom = 0;
    friend constexpr bool operator==(const PaddingValues&, const PaddingValues&) = default;
};

// Modifier::blur's edges (Compose's BlurredEdgeTreatment): with a shape, the blur is cut to it
// (Rectangle: the bounds), the pixels past the bounds taken as the edge's (TileMode::Clamp);
// without one (Unbounded) it spreads out past them, fading (TileMode::Decal).
struct BlurredEdgeTreatment {
    std::optional<Shape> shape;
    static const BlurredEdgeTreatment Rectangle;
    static const BlurredEdgeTreatment Unbounded;
    friend bool operator==(const BlurredEdgeTreatment&, const BlurredEdgeTreatment&) = default;
};
inline const BlurredEdgeTreatment BlurredEdgeTreatment::Rectangle{RectangleShape};
inline const BlurredEdgeTreatment BlurredEdgeTreatment::Unbounded{};

// What Modifier::blur blurs: the node and its children (Family, Compose's); what is drawn behind
// the node, within its shape, the node and its children sharp over it (Morph: frosted glass, as
// iOS's); or only what the node draws itself (its background, border, image, drawBehind...), its
// children sharp over it (Self). The alpha goes with what is blurred: all of it, what is behind,
// what the node draws.
enum class BlurType { Family, Morph, Self };

// What Modifier::blur's block sets while drawing (Compose's BlurScope), each time from these: how
// much it blurs where, its edges, its alpha, what it blurs; the node's size (dp) to read.
struct BlurScope {
    Size size;
    BlurRadiusSpec blurRadiusSpec = BlurRadiusSpec::uniform(0);
    BlurredEdgeTreatment edgeTreatment = BlurredEdgeTreatment::Rectangle;
    float alpha = 1;
    BlurType type = BlurType::Family;
    friend bool operator==(const BlurScope&, const BlurScope&) = default;
};

// How graphicsLayer() composites its content (Compose's CompositingStrategy): Auto uses an
// offscreen layer only when it needs one (alpha, a transform, a shaped clip); Offscreen always
// does, so blend modes inside (BlendMode::DstIn to fade an edge) act on the layer alone, not on
// what is behind it. ModulateAlpha is taken as Auto.
enum class CompositingStrategy { Auto, Offscreen, ModulateAlpha };

// How far a layer reaches past its bounds when it is drawn offscreen (Compose's LayerOutsets): a
// shadow or what draws outside them isn't cut off. Its clip and shadow still go by its bounds.
struct LayerOutsets {
    float left = 0, top = 0, right = 0, bottom = 0;  // dp
    friend constexpr bool operator==(const LayerOutsets&, const LayerOutsets&) = default;
};

// Properties of graphicsLayer() (Compose's GraphicsLayerScope): how the content is drawn, without
// changing its layout. Scaled, then turned about z (clockwise) or in 3D about x (the top going
// back), y (the right side going back) and z, seen from a camera cameraDistance * 72 px away (as
// Android draws it), then moved; all about the pivot (transformOrigin). It is drawn offscreen,
// then composited, only where it has to be: alpha below 1 (unless ModulateAlpha, which fades each
// draw instead), a blend mode other than SrcOver, a color filter, a render effect (what is drawn
// within its bounds goes through it, then is cut to its shape if clip is set), or
// CompositingStrategy::Offscreen. With a shadowElevation, its shape's shadow (clipped or not) is
// drawn first, turned and moved with it, as Android draws a RenderNode's: lit from above the
// window's top middle (GraphicsContext::light), faded with it.
struct GraphicsLayerScope {
    float scaleX = 1, scaleY = 1, alpha = 1;
    float translationX = 0, translationY = 0;  // dp
    float shadowElevation = 0;                 // dp: a shadow of its shape, as Android draws one (GraphicsContext::drawElevationShadow)
    Color ambientShadowColor = DefaultShadowColor, spotShadowColor = DefaultShadowColor;
    float rotationX = 0, rotationY = 0;        // degrees
    float rotationZ = 0;                       // degrees, clockwise
    float cameraDistance = 8;                  // Compose's DefaultCameraDistance
    Offset transformOrigin{0.5f, 0.5f};        // fraction of the size
    Shape shape = RectangleShape;              // outline used when clip is set
    bool clip = false;
    std::optional<RenderEffect> renderEffect;  // what its pixels go through (a blur): drawn offscreen
    BlendMode blendMode = BlendMode::SrcOver;  // how it goes onto what is under it
    std::optional<ColorFilter> colorFilter;    // what its colors go through on the way
    CompositingStrategy compositingStrategy = CompositingStrategy::Auto;
    LayerOutsets outsets;
    Size size;  // the layer's (dp): set before a graphicsLayer block runs, to read in it
    friend bool operator==(const GraphicsLayerScope& a, const GraphicsLayerScope& b) {
        return a.scaleX == b.scaleX && a.scaleY == b.scaleY && a.alpha == b.alpha && a.translationX == b.translationX &&
               a.translationY == b.translationY && a.shadowElevation == b.shadowElevation && a.ambientShadowColor == b.ambientShadowColor &&
               a.spotShadowColor == b.spotShadowColor && a.rotationX == b.rotationX && a.rotationY == b.rotationY && a.rotationZ == b.rotationZ &&
               a.cameraDistance == b.cameraDistance && a.transformOrigin.x == b.transformOrigin.x &&
               a.transformOrigin.y == b.transformOrigin.y && a.shape == b.shape && a.clip == b.clip && a.renderEffect == b.renderEffect && a.blendMode == b.blendMode &&
               a.colorFilter == b.colorFilter && a.compositingStrategy == b.compositingStrategy && a.outsets == b.outsets &&
               a.size.width == b.size.width && a.size.height == b.size.height;
    }
};

// Provided by the host from the platform:
// what each kind of system UI covers, in dp (read through WindowInsets::statusBars() & co.)...
struct SystemInsets {
    PaddingValues statusBars, navigationBars, ime, displayCutout, systemGestures;
    // A freeform window's title bar; the gesture edges no app can take; what a tap reaches the
    // system through (the taskbar, a three-button bar); a screen's curved edges (Android 11+).
    PaddingValues captionBar, mandatorySystemGestures, tappableElement, waterfall;
    // The keyboard: while it slides in or out (Android 11+) `ime` follows it frame by frame, from
    // where it was to where it is going; whether it is (or is coming) up.
    bool imeAnimating = false, imeVisible = false;
    PaddingValues imeAnimationSource, imeAnimationTarget;
    friend constexpr bool operator==(const SystemInsets&, const SystemInsets&) = default;
};
inline const CompositionLocal<SystemInsets> LocalSystemInsets{SystemInsets{}};
// ...the union of the bars, the cutout and the keyboard (WindowInsets::safeDrawing())...
inline const CompositionLocal<PaddingValues> LocalWindowInsets{PaddingValues{}};
// ...and whether the system is in dark mode.
inline const CompositionLocal<bool> LocalSystemDarkTheme{false};
// Material's HCT color space (Material Color Utilities'): hue and chroma as CAM16 sees them
// (under its default viewing conditions), tone as L* (0 black, 100 white). toColor() gives the
// color of that hue and tone with as much of that chroma as sRGB has there.
struct Hct {
    float hue = 0, chroma = 0, tone = 0;
    static Hct fromColor(Color color);
    Color toColor() const;  // (opaque)
    friend constexpr bool operator==(const Hct&, const Hct&) = default;
};
// The system's color palettes where it derives them from the wallpaper (Android 12+, "Material
// You"): five key colors in 13 tones each, from tone 100 (white) through 99, 95, 90, 80, ..., 10
// down to 0 (black). Read by dynamicLightColorScheme() / dynamicDarkColorScheme().
struct TonalPalette {
    static constexpr int kTones[13] = {100, 99, 95, 90, 80, 70, 60, 50, 40, 30, 20, 10, 0};
    std::array<Color, 13> accent1, accent2, accent3, neutral1, neutral2;
    // The palettes Material You makes from one color (Android 13+'s "tonal spot"): its hue at
    // chroma 36, 16 (accent2), 24 turned 60° (accent3), 6 and 8 (the neutrals). On Linux the
    // desktop's accent color (the XDG portal's) is turned into them.
    static TonalPalette fromSeed(Color seed);
    // Material You from a picture, as Android makes it from the wallpaper (WallpaperColors and
    // System UI): fromSeed of the picture's best seed (seedColors' first), the picture first
    // scaled to at most 112 × 112 of area without filtering, as Android does. An empty picture
    // gives Google blue's.
    static TonalPalette fromImage(const ImageBitmap& image);
    friend bool operator==(const TonalPalette&, const TonalPalette&) = default;
};
// The colors a picture is made of, as Material Color Utilities' QuantizerCelebi finds them: Wu's
// quantizer, its colors then refined by k-means in L*a*b*. Up to `maxColors` (at most 256), each
// with how many pixels went to it, the most first. Pixels not fully opaque are left out.
std::vector<std::pair<Color, uint32_t>> quantizeColors(const ImageBitmap& image, int maxColors = 128);
// Which of those would make good theme seeds, best first, as Android's System UI picks a
// wallpaper's (monet's ColorScheme.getSeedColors; Material Color Utilities' Score follows it):
// each scored by its chroma (48 is best) and by how much of the picture is within 15° of its hue
// (its greys left out), the greyish (chroma below 5) and the rare (1% or less) not taken, then
// picked as far apart in hue as `desired` of them can be. When none is left: Google blue alone.
std::vector<Color> seedColors(const std::vector<std::pair<Color, uint32_t>>& colors, int desired = 4);
inline const CompositionLocal<std::optional<TonalPalette>> LocalDynamicTonalPalette{std::nullopt};
// The wallpaper's own palette, where the platform can read the wallpaper (TonalPalette::fromImage
// of it; Android's wallpaper colors): read by wallpaperLightColorScheme() and
// wallpaperDarkColorScheme(). Whether an app takes its colors from it, from the system's accent
// (LocalDynamicTonalPalette) or from its own scheme is the app's choice, or its user's.
inline const CompositionLocal<std::optional<TonalPalette>> LocalWallpaperTonalPalette{std::nullopt};
// The window's size in dp (pick a layout for phones, tablets and desktops).
inline const CompositionLocal<Size> LocalWindowSize{Size{}};
// The window the composition is in (Compose's LocalWindowInfo): whether it has the input focus
// (another window, a system dialog or the notification shade over it: not), and its size (dp
// here; Compose's containerSize is in pixels).
struct WindowInfo {
    bool isWindowFocused = true;
    Size containerSize;
    friend bool operator==(const WindowInfo&, const WindowInfo&) = default;
};
inline const CompositionLocal<WindowInfo> LocalWindowInfo{WindowInfo{}};
// The platform's thresholds for gestures (Compose's ViewConfiguration): how long a press takes
// to become a long press, how soon a second tap makes a double tap (and how soon is too soon),
// how far (dp) a pointer may move before a press turns into a drag, the smallest touch target,
// and the fastest (dp/s) a fling goes. Android gives its own (android.view.ViewConfiguration).
struct ViewConfiguration {
    std::chrono::milliseconds longPressTimeout{400}, doubleTapTimeout{300}, doubleTapMinTime{40};
    float touchSlop = 8;
    Size minimumTouchTargetSize{48, 48};
    float maximumFlingVelocity = 8000;
    friend bool operator==(const ViewConfiguration&, const ViewConfiguration&) = default;
};
inline const CompositionLocal<ViewConfiguration> LocalViewConfiguration{ViewConfiguration{}};
// A step of the system's back gesture (Compose's BackEventCompat; Android 14+): where the finger
// is (dp, in the window), how far along the gesture is (0..1, as the system eases it) and the
// edge it came from.
struct BackEventCompat {
    enum class SwipeEdge : uint8_t { Left, Right, None };
    float touchX = 0, touchY = 0;
    float progress = 0;
    SwipeEdge swipeEdge = SwipeEdge::Left;
    friend bool operator==(const BackEventCompat&, const BackEventCompat&) = default;
};
// Pixels per dp, and the system font size setting that scales text (sp).
enum class WindowInsetsSides : uint8_t {
    Left = 1, Top = 2, Right = 4, Bottom = 8,
    Start = Left, End = Right, Horizontal = Left | Right, Vertical = Top | Bottom, All = 15,
};
constexpr WindowInsetsSides operator|(WindowInsetsSides a, WindowInsetsSides b) {
    return WindowInsetsSides(uint8_t(a) | uint8_t(b));
}

// Compose's WindowInsets: how far system UI reaches into the window on each side, in dp.
// The kinds are read during composition (the reader recomposes when they change).
struct WindowInsets {
    float left = 0, top = 0, right = 0, bottom = 0;

    static WindowInsets statusBars();
    static WindowInsets navigationBars();
    static WindowInsets systemBars();  // status + navigation + caption bars
    static WindowInsets captionBar();
    static WindowInsets mandatorySystemGestures();  // swipes the system always takes (not exclusions)
    static WindowInsets tappableElement();          // the taskbar, a three-button navigation bar
    static WindowInsets waterfall();                // a curved screen's edges
    static WindowInsets ime();         // the software keyboard (as it slides, Android 11+)
    // Where the keyboard's slide started and where it goes (Compose's imeAnimationSource /
    // imeAnimationTarget: a layout can go straight to where it ends up); ime() when it isn't moving.
    static WindowInsets imeAnimationSource();
    static WindowInsets imeAnimationTarget();
    static bool isImeVisible();  // the keyboard is up, or on its way up (not on its way down)
    static WindowInsets displayCutout();
    static WindowInsets systemGestures();  // edges where swipes go to the system (back, home)
    static WindowInsets safeGestures();    // all of the gesture edges, tappable elements and waterfall
    static WindowInsets safeDrawing();     // bars, cutout and keyboard: keep content clear of these
    static WindowInsets safeContent();     // safeDrawing and safeGestures: keep touch targets clear
    static WindowInsets of(PaddingValues p) { return {p.start, p.top, p.end, p.bottom}; }

    WindowInsets unionWith(const WindowInsets& o) const;  // Compose's union: the larger on each side
    WindowInsets exclude(const WindowInsets& o) const;    // what is left of these after `o`
    WindowInsets add(const WindowInsets& o) const;
    WindowInsets only(WindowInsetsSides sides) const;
    PaddingValues asPaddingValues() const { return {left, top, right, bottom}; }
    friend constexpr bool operator==(const WindowInsets&, const WindowInsets&) = default;
};
namespace detail {
// The bars and the display cutout (Compose's systemBarsForVisualComponents): what bars, rails,
// drawers and scaffolds keep clear of; the keyboard covers them.
WindowInsets systemBarsForVisualComponents();
} // namespace detail

inline const CompositionLocal<Density> LocalDensity{Density{}};

class SharedContentState;
class AnimatedVisibilityScope;
class SharedTransitionScope;
struct AnimationSpec;

// Where a node was laid out, as onGloballyPositioned and onPlaced tell it (Compose's
// LayoutCoordinates): taken after the layout, in dp. The root is the window (popups' content
// too); graphicsLayer moves and scales aren't counted.
class LayoutCoordinates {
public:
    LayoutCoordinates() = default;
    LayoutCoordinates(Rect bounds, Rect visible, Offset inParent, std::shared_ptr<const LayoutCoordinates> parent = {})
        : bounds_(bounds), visible_(visible), inParent_(inParent), parent_(std::move(parent)) {}
    Size size() const { return {bounds_.width, bounds_.height}; }
    Offset positionInRoot() const { return {bounds_.x, bounds_.y}; }
    Offset positionInWindow() const { return positionInRoot(); }
    Offset positionInParent() const { return inParent_; }  // from the top left of the node it is in
    // The part that can show: clipped by the scrollers and clips around it (empty: scrolled out).
    Rect boundsInRoot() const { return visible_; }
    Rect boundsInWindow() const { return visible_; }
    Offset localToRoot(Offset local) const { return {bounds_.x + local.x, bounds_.y + local.y}; }
    Offset localToWindow(Offset local) const { return localToRoot(local); }
    Offset rootToLocal(Offset p) const { return {p.x - bounds_.x, p.y - bounds_.y}; }
    Offset windowToLocal(Offset p) const { return rootToLocal(p); }
    bool isAttached() const { return true; }
    // Its place and size in the node it is in (Compose's boundsInParent).
    Rect boundsInParent() const { return {inParent_.x, inParent_.y, bounds_.width, bounds_.height}; }
    // A point of another's (`relativeToSource` in it) here (Compose's localPositionOf).
    Offset localPositionOf(const LayoutCoordinates& source, Offset relativeToSource = {}) const {
        return rootToLocal(source.localToRoot(relativeToSource));
    }
    // Another's bounds here — the part that can show, with clipBounds (Compose's localBoundingBoxOf).
    Rect localBoundingBoxOf(const LayoutCoordinates& source, bool clipBounds = true) const {
        Rect r = clipBounds ? source.visible_ : source.bounds_;
        return {r.x - bounds_.x, r.y - bounds_.y, r.width, r.height};
    }
    // The node it is in, as it was then (none for the root; Compose's parentLayoutCoordinates;
    // parentCoordinates is the same here).
    std::optional<LayoutCoordinates> parentLayoutCoordinates() const {
        return parent_ ? std::optional<LayoutCoordinates>(*parent_) : std::nullopt;
    }
    std::optional<LayoutCoordinates> parentCoordinates() const { return parentLayoutCoordinates(); }

    friend bool operator==(const LayoutCoordinates& a, const LayoutCoordinates& b) {
        auto same = [](const Rect& p, const Rect& q) { return p.x == q.x && p.y == q.y && p.width == q.width && p.height == q.height; };
        return same(a.bounds_, b.bounds_) && same(a.visible_, b.visible_) && a.inParent_.x == b.inParent_.x && a.inParent_.y == b.inParent_.y;
    }

private:
    Rect bounds_, visible_;
    Offset inParent_;
    std::shared_ptr<const LayoutCoordinates> parent_;  // (not compared: the callbacks hear of this one's changes)
};

// LookaheadScope(): the node animateBounds measures places from.
struct LookaheadScopeElement final : ModifierElement {
    bool equals(const ModifierElement& other) const override { return dynamic_cast<const LookaheadScopeElement*>(&other) != nullptr; }
};

// onGloballyPositioned() / onPlaced(): told after a layout that moved or resized it.
struct PositionedElement final : ModifierElement {
    std::function<void(const LayoutCoordinates&)> onPositioned;
    bool inParentOnly = false;  // onPlaced: where it is in its parent and its size, not where on the screen
};

// Where a node was laid out, for onVisibilityChanged / onFirstVisible to measure against
// instead of the window (Compose's LayoutBoundsHolder): Modifier::layoutBounds(holder) on a
// list or a scrolling box, then {.viewport = holder} on what is in it.
class LayoutBoundsHolder {
public:
    LayoutBoundsHolder() : data_(std::make_shared<std::optional<Rect>>()) {}
    std::optional<Rect> bounds() const { return *data_; }  // in the window, from the last layout
    friend bool operator==(const LayoutBoundsHolder& a, const LayoutBoundsHolder& b) { return a.data_ == b.data_; }
    std::shared_ptr<std::optional<Rect>> data_;
};
struct LayoutBoundsElement final : ModifierElement {
    LayoutBoundsHolder holder;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const LayoutBoundsElement*>(&other);
        return o && o->holder == holder;
    }
};
// When onVisibilityChanged / onFirstVisible take it as visible: this much of it showing in the
// window (or the viewport), for this long.
struct VisibilityOptions {
    int64_t minDurationMs = 0;      // how long it must be visible first (not visible is told at once)
    float minFractionVisible = 1;   // more of it than this, or all of it
    std::optional<LayoutBoundsHolder> viewport;  // unset: the window
};
struct VisibilityElement final : ModifierElement {
    std::function<void(bool)> onChanged;  // onVisibilityChanged
    std::function<void()> onFirst;        // onFirstVisible
    VisibilityOptions options;
};
// Where a node is, for onLayoutRectChanged (Compose's RelativeLayoutBounds): its bounds in the
// root and in the window (the same here: the root fills the window), and the window.
struct RelativeLayoutBounds {
    Rect boundsInRoot, boundsInWindow, window;
    Offset positionInRoot() const { return {boundsInRoot.x, boundsInRoot.y}; }
    Offset positionInWindow() const { return {boundsInWindow.x, boundsInWindow.y}; }
    float width() const { return boundsInWindow.width; }
    float height() const { return boundsInWindow.height; }
    // How much of it shows in `rect` (window coordinates), 0..1; and in the window.
    float fractionVisibleInRect(const Rect& rect) const {
        Rect in = boundsInWindow.intersect(rect);
        float area = boundsInWindow.width * boundsInWindow.height;
        return area > 0 ? std::max(0.f, in.width) * std::max(0.f, in.height) / area : 0.f;
    }
    float fractionVisibleInWindow() const { return fractionVisibleInRect(window); }
    friend bool operator==(const RelativeLayoutBounds& a, const RelativeLayoutBounds& b) {
        auto same = [](const Rect& p, const Rect& q) { return p.x == q.x && p.y == q.y && p.width == q.width && p.height == q.height; };
        return same(a.boundsInRoot, b.boundsInRoot) && same(a.boundsInWindow, b.boundsInWindow) && same(a.window, b.window);
    }
};
// Keeps the system's edge swipes (back, home) off a node (Compose's systemGestureExclusion):
// a drag that starts on it stays the app's. `exclusion` narrows it to a rect of its own, local
// to it; unset, all of it.
struct SystemGestureExclusionElement final : ModifierElement {
    std::function<Rect(const LayoutCoordinates&)> exclusion;
};
// overscroll(effect): draws the effect around what comes after it.
struct OverscrollElement final : ModifierElement {
    std::shared_ptr<OverscrollEffect> effect;
    void draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode& node, const std::function<void()>& drawContent) const override;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const OverscrollElement*>(&other);
        return o && o->effect == effect;
    }
};
// Hints that it shows something sensitive (a password, a card number): kept out of screen
// shares and recordings where the platform can (Compose's sensitiveContent; Android 15+).
struct SensitiveContentElement final : ModifierElement {
    bool sensitive = true;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const SensitiveContentElement*>(&other);
        return o && o->sensitive == sensitive;
    }
};
// How fast it would like the screen to refresh (Compose's FrameRateCategory): Normal (60 Hz),
// High (the screen's fastest), or no wish of its own.
enum class FrameRateCategory { Default, NoPreference, Normal, High };
// preferredFrameRate(): a rate (Hz), or a category.
struct FrameRateElement final : ModifierElement {
    float rate = 0;
    FrameRateCategory category = FrameRateCategory::Default;
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const FrameRateElement*>(&other);
        return o && o->rate == rate && o->category == category;
    }
};
// clipScrollableContainer(): its bounds along `axis`, 30 dp more each side across.
struct ScrollableContainerClipElement final : ModifierElement {
    Orientation axis = Orientation::Vertical;
    bool clipsContent() const override { return true; }
    Rect clipBounds(const Rect& b) const override {
        constexpr float kMaxSupportedElevation = 30;
        return axis == Orientation::Vertical ? Rect{b.x - kMaxSupportedElevation, b.y, b.width + 2 * kMaxSupportedElevation, b.height}
                                             : Rect{b.x, b.y - kMaxSupportedElevation, b.width, b.height + 2 * kMaxSupportedElevation};
    }
    void draw(GraphicsContext& canvas, const Rect& bounds, const LayoutNode&, const std::function<void()>& drawContent) const override {
        canvas.pushClip(clipBounds(bounds));
        drawContent();
        canvas.popClip();
    }
    bool equals(const ModifierElement& other) const override {
        auto* o = dynamic_cast<const ScrollableContainerClipElement*>(&other);
        return o && o->axis == axis;
    }
};
// Keeps the screen from going dark while it is laid out (Compose's keepScreenOn).
struct KeepScreenOnElement final : ModifierElement {
    bool equals(const ModifierElement& other) const override { return dynamic_cast<const KeepScreenOnElement*>(&other); }
};
struct LayoutRectElement final : ModifierElement {
    std::function<void(const RelativeLayoutBounds&)> callback;
    int64_t throttleMillis = 0, debounceMillis = 64;
};

// Pointer events (gestures.hpp) and the mouse's buttons.
struct PointerEvent;
enum class PointerEventType;
enum class PointerButton { Primary, Secondary, Tertiary, Back, Forward };

// onPointerEvent(): told of the pointer's events of one kind over the node (Compose's
// Modifier.onPointerEvent, desktop): a mouse coming over it and leaving (Enter, Exit), moving
// over it, a button pressed or let go there, the wheel turned there (consuming it stops the
// scrolling).
struct PointerEventElement final : ModifierElement {
    PointerEventType type;
    std::function<void(PointerEvent&)> onEvent;
};

// Which pointers and buttons a Modifier::onClick answers (Compose's PointerMatcher).
struct PointerMatcher {
    bool acceptsTouch = true, acceptsMouse = true;
    PointerButton button = PointerButton::Primary;
    static PointerMatcher Primary() { return {}; }
    static PointerMatcher mouse(PointerButton button) { return {false, true, button}; }
    static PointerMatcher touch() { return {true, false, PointerButton::Primary}; }
};

struct OnClickOptions {
    bool enabled = true;
    PointerMatcher matcher;  // default: a finger, or the mouse's primary button
    std::function<bool(const KeyModifiers&)> keyboardModifiers;  // the keys held down it needs (unset: any)
    std::function<void()> onDoubleClick, onLongClick;            // the primary button and fingers only
};

// Modifier::clickable(onClick, options).
struct ClickableOptions {
    bool enabled = true;
    std::optional<MutableInteractionSource> interactionSource;  // told of presses, hovers and focus
    // The ripple and the hover/focus overlays: unset, LocalIndication's (a ripple); false, none
    // (Compose's indication = null); ripple(false, 24) and so on.
    std::optional<Indication> indication;
    std::function<void()> onLongClick, onDoubleClick;  // combinedClickable()'s
};

// Modifier::basicMarquee(): when it starts scrolling (Compose's MarqueeAnimationMode).
enum class MarqueeAnimationMode {
    Immediately,  // as soon as the content doesn't fit
    WhileFocused  // only while it (or something in it) has the focus: put it before focusable()
};

// The gap between the content and its copy coming after it (Compose's MarqueeSpacing), from the
// content's and the container's widths (dp).
struct MarqueeSpacing {
    MarqueeSpacing(float spacing) : calculate([spacing](float, float) { return spacing; }) {}
    explicit MarqueeSpacing(std::function<float(float contentWidth, float containerWidth)> calculate)
        : calculate(std::move(calculate)) {}
    static MarqueeSpacing fractionOfContainer(float fraction) {
        return MarqueeSpacing([fraction](float, float container) { return fraction * container; });
    }
    std::function<float(float contentWidth, float containerWidth)> calculate;
};

namespace MarqueeDefaults {
inline constexpr int Iterations = 3;
inline constexpr int Forever = 0x7fffffff;  // Compose's Int.MAX_VALUE iterations
inline constexpr int RepeatDelayMillis = 1200;
inline constexpr float Velocity = 30;  // dp per second
inline MarqueeSpacing Spacing() { return MarqueeSpacing::fractionOfContainer(1.f / 3); }
} // namespace MarqueeDefaults

struct MarqueeOptions {
    int iterations = MarqueeDefaults::Iterations;  // MarqueeDefaults::Forever: never stops
    MarqueeAnimationMode animationMode = MarqueeAnimationMode::Immediately;
    int repeatDelayMillis = MarqueeDefaults::RepeatDelayMillis;  // a pause before every later pass
    std::optional<int> initialDelayMillis;  // before the first: unset, repeatDelayMillis (Immediately) or 0 (WhileFocused)
    MarqueeSpacing spacing = MarqueeDefaults::Spacing();
    float velocity = MarqueeDefaults::Velocity;  // dp per second; negative: the other way
};

class Modifier {
public:
    Modifier padding(float all) const;
    Modifier padding(float horizontal, float vertical) const;
    Modifier padding(float start, float top, float end, float bottom) const;
    Modifier padding(PaddingValues values) const;
    Modifier absolutePadding(float left, float top, float right, float bottom) const;  // not mirrored
    // Pads by system insets, less what insets paddings around it (or consumeWindowInsets) already
    // took, so nested ones don't add up: Scaffold content with .imePadding() inside
    // .consumeWindowInsets(padding) pads only by the part of the keyboard below the bars.
    Modifier windowInsetsPadding(const WindowInsets& insets) const;
    // Told when it becomes visible and stops being (Compose's onVisibilityChanged): all of it (or
    // more than options.minFractionVisible) in the window or options.viewport, and for
    // options.minDurationMs; not visible at once, also when it leaves the layout. onFirstVisible:
    // only the first time (an impression).
    Modifier onVisibilityChanged(std::function<void(bool visible)> callback, VisibilityOptions options = {}) const;
    // Told where it is when that changes (Compose's onLayoutRectChanged): once it has stayed put
    // for debounceMillis (a scroll is told once it stops) and, with throttleMillis, also every
    // that long while it moves; with neither, at once on every change. A node leaving the
    // layout is not told.
    Modifier onLayoutRectChanged(std::function<void(const RelativeLayoutBounds&)> callback, int64_t throttleMillis = 0,
                                 int64_t debounceMillis = 64) const;
    Modifier onFirstVisible(std::function<void()> callback, VisibilityOptions options = {}) const;
    // Keeps where it is laid out in `holder`, as a viewport for onVisibilityChanged.
    Modifier layoutBounds(const LayoutBoundsHolder& holder) const;
    // Edge swipes that start on it are the app's, not the system's back or home (Android 10+
    // with gesture navigation; Android lets at most 200 dp of each edge go so). The second one
    // keeps them off the rect `exclusion` gives, local to the node.
    Modifier systemGestureExclusion() const;
    // The screen stays on (no dimming, no sleep) while this is laid out: a video playing, a
    // recipe being cooked from (Compose's keepScreenOn).
    Modifier keepScreenOn() const;
    // What it shows is sensitive (Compose's sensitiveContent): while one is laid out the window
    // is kept out of screen shares and recordings (Android 15+); false takes the hint back.
    Modifier sensitiveContent(bool isContentSensitive = true) const;
    // The screen's refresh rate it would like while it is laid out (Compose's preferredFrameRate):
    // the fastest one asked for wins, a rate over a category (Android 11+; elsewhere nothing).
    Modifier preferredFrameRate(float frameRate) const;
    Modifier preferredFrameRate(FrameRateCategory category) const;
    // Clips along `orientation` only, leaving 30 dp each side across for shadows (Compose's
    // clipScrollableContainer, as the scroll containers clip).
    Modifier clipScrollableContainer(Orientation orientation) const;
    Modifier systemGestureExclusion(std::function<Rect(const LayoutCoordinates&)> exclusion) const;
    // As tall as `insets` at the top (or bottom), as wide as at the start (or end), as Compose's
    // windowInsetsTopHeight & co: a Spacer under the status bar or over the navigation bar. What
    // windowInsetsPadding / consumeWindowInsets around it took is left out; it takes nothing.
    Modifier windowInsetsTopHeight(const WindowInsets& insets) const;
    Modifier windowInsetsBottomHeight(const WindowInsets& insets) const;
    Modifier windowInsetsStartWidth(const WindowInsets& insets) const;
    Modifier windowInsetsEndWidth(const WindowInsets& insets) const;
    Modifier safeDrawingPadding() const;  // keeps content clear of system bars, cutouts and the keyboard
    Modifier safeContentPadding() const;
    Modifier systemBarsPadding() const;
    Modifier statusBarsPadding() const;
    Modifier navigationBarsPadding() const;
    Modifier imePadding() const;  // keeps content above the software keyboard
    Modifier displayCutoutPadding() const;
    Modifier captionBarPadding() const;
    Modifier waterfallPadding() const;
    Modifier systemGesturesPadding() const;
    Modifier mandatorySystemGesturesPadding() const;
    Modifier safeGesturesPadding() const;
    // Tells insets paddings inside that this much is already taken care of.
    Modifier consumeWindowInsets(const WindowInsets& insets) const;
    // Called during layout with the insets taken before it (by windowInsetsPadding & co. and
    // consumeWindowInsets on it and around it) when they change (Compose's
    // onConsumedWindowInsetsChanged): what is left for it is WindowInsets::...().exclude(them).
    Modifier onConsumedWindowInsetsChanged(std::function<void(const WindowInsets&)> block) const;
    Modifier consumeWindowInsets(PaddingValues padding) const;
    Modifier size(float size) const;
    Modifier size(float width, float height) const;
    Modifier width(float width) const;
    Modifier height(float height) const;
    Modifier widthIn(float min, float max = kInfinity) const;
    Modifier heightIn(float min, float max = kInfinity) const;
    Modifier sizeIn(float minWidth, float minHeight, float maxWidth = kInfinity, float maxHeight = kInfinity) const;
    // Minimum sizes that only apply where the layout is otherwise unconstrained (an incoming
    // minimum of 0), as Compose's defaultMinSize: a component's default that callers can override.
    Modifier defaultMinSize(float minWidth = 0, float minHeight = 0) const;
    Modifier fillMaxWidth(float fraction = 1) const;
    Modifier fillMaxHeight(float fraction = 1) const;
    Modifier fillMaxSize(float fraction = 1) const;
    Modifier offset(float x, float y) const;  // x towards the end: mirrored right to left
    // Read during layout, so a changing offset (dragging) never recomposes.
    Modifier offset(std::function<Offset()> offset) const;
    // Not mirrored right to left: x is to the right whatever the layout direction.
    Modifier absoluteOffset(float x, float y) const;
    Modifier absoluteOffset(std::function<Offset()> offset) const;
    // Sizes the content to `ratio` = width / height within the constraints.
    Modifier aspectRatio(float ratio) const;
    // Lets the content be smaller than the minimum constraints and aligns it in the space.
    Modifier wrapContentSize(Alignment align = Alignment::Center) const;
    Modifier wrapContentWidth(HorizontalAlignment align = Alignment::CenterHorizontally) const;
    Modifier wrapContentHeight(VerticalAlignment align = Alignment::CenterVertically) const;
    // Exact sizes that ignore the incoming constraints (centered if they don't fit).
    Modifier requiredSize(float size) const;
    Modifier requiredSize(float width, float height) const;
    Modifier requiredWidth(float width) const;
    Modifier requiredHeight(float height) const;
    // Size ranges that ignore the incoming constraints (Compose's requiredWidthIn & co.): a max of
    // kInfinity keeps the incoming one (but never under the min); a min over the max comes down to
    // it; outside the incoming constraints it is centered, as requiredSize.
    Modifier requiredWidthIn(float min, float max = kInfinity) const;
    Modifier requiredHeightIn(float min, float max = kInfinity) const;
    Modifier requiredSizeIn(float minWidth, float minHeight, float maxWidth = kInfinity, float maxHeight = kInfinity) const;
    // As tall / wide as the content wants to be (e.g. Row(Modifier().height(IntrinsicSize::Min))
    // so a fillMaxHeight() divider matches the tallest text). An approximation of Compose's
    // intrinsics: both Min and Max measure the content with that axis unbounded.
    Modifier height(IntrinsicSize size) const;
    Modifier width(IntrinsicSize size) const;
    // A custom layout step: measure the wrapped content with `measure`, return this layer's size
    // and set where the content goes (Compose's Modifier.layout { measurable, constraints -> }).
    Modifier layout(std::function<Size(const MeasureNext& measure, const Constraints& constraints, Offset& placeAt)> fn) const;
    Modifier layoutId(std::string id) const;
    Modifier background(Color color, float cornerRadius = 0) const;
    Modifier background(Color color, Shape shape) const;
    Modifier background(const Brush& brush, Shape shape = RectangleShape) const;
    Modifier border(float width, Color color, float cornerRadius = 0) const;
    Modifier border(float width, Color color, Shape shape) const;
    // A BorderStroke's: a color, or a brush (a gradient border), `width` inside the shape.
    Modifier border(const BorderStroke& border, Shape shape = RectangleShape) const;
    // Clips what follows to `shape` (rounded shapes render through an offscreen layer).
    Modifier clip(Shape shape) const;
    // Draws what follows with `alpha` opacity (through an offscreen layer when 0 < alpha < 1).
    Modifier alpha(float alpha) const;
    // Shared element transitions, in a SharedTransitionLayout (see shared_transition.hpp): while
    // the content it is in enters, the element moves and resizes from where its match (the same
    // key in the content leaving) was, drawn above everything; the match is hidden.
    // sharedBounds shows both, cross-fading, in the moving bounds. `boundsTransform` paces it.
    Modifier sharedElement(const SharedContentState& state, const AnimatedVisibilityScope& scope) const;
    Modifier sharedElement(const SharedContentState& state, const AnimatedVisibilityScope& scope,
                           const AnimationSpec& boundsTransform) const;
    Modifier sharedBounds(const SharedContentState& state, const AnimatedVisibilityScope& scope) const;
    Modifier sharedBounds(const SharedContentState& state, const AnimatedVisibilityScope& scope,
                          const AnimationSpec& boundsTransform) const;
    Modifier sharedBounds(const SharedContentState& state, const AnimatedVisibilityScope& scope, const AnimationSpec& boundsTransform,
                          const SharedResizeMode& resizeMode) const;
    // Where it is drawn while it moves (Compose's renderInOverlayDuringTransition and
    // zIndexInOverlay): in the SharedTransitionLayout's overlay above the content (true, the
    // default), ordered there by zIndexInOverlay (lower first); or where it is, under what
    // comes after it in the content (false).
    Modifier sharedElement(const SharedContentState& state, const AnimatedVisibilityScope& scope, const AnimationSpec& boundsTransform,
                           bool renderInOverlayDuringTransition, float zIndexInOverlay = 0) const;
    Modifier sharedBounds(const SharedContentState& state, const AnimatedVisibilityScope& scope, const AnimationSpec& boundsTransform,
                          const SharedResizeMode& resizeMode, bool renderInOverlayDuringTransition, float zIndexInOverlay = 0) const;
    // Drawn in `scope`'s overlay instead of where it is while renderInOverlay() says so (unset:
    // while a shared element moves), ordered among the moving elements by zIndexInOverlay
    // (Compose's renderInSharedTransitionScopeOverlay): a bottom bar the elements pass under.
    Modifier renderInSharedTransitionScopeOverlay(const SharedTransitionScope& scope, std::function<bool()> renderInOverlay = {},
                                                  float zIndexInOverlay = 0) const;
    // Compose's shadow(elevation, shape, clip, ambientColor, spotColor): a graphicsLayer with that
    // shadowElevation, shape and shadow colors, clipped to the shape (unless `clip` says otherwise,
    // when the elevation is above 0). Neither an elevation nor a clip: nothing.
    Modifier shadow(float elevation, Shape shape = RectangleShape, std::optional<bool> clip = {}, Color ambientColor = DefaultShadowColor,
                    Color spotColor = DefaultShadowColor) const;
    // The same with RoundedCornerShape(cornerRadius).
    Modifier shadow(float elevation, float cornerRadius) const { return shadow(elevation, RoundedCornerShape(cornerRadius)); }
    // Compose 1.9's shadows of a shape, drawn before what comes after: under it (dropShadow:
    // spread out, moved, blurred) or inside it (innerShadow: as if its edge cast it in). The
    // shape's rounded rect (outline shapes: their bounds).
    Modifier dropShadow(Shape shape, Shadow shadow) const;
    Modifier innerShadow(Shape shape, Shadow shadow) const;
    Modifier clipToBounds() const;
    // The pressed/hover overlay takes the shape of the last background(), border() or clip().
    Modifier clickable(std::function<void()> onClick, bool enabled = true) const;
    // With an interaction source (told of presses, hovers and focus), without the overlay
    // (Compose's indication = null), long and double clicks: {.interactionSource = source}.
    Modifier clickable(std::function<void()> onClick, const ClickableOptions& options) const;
    // A clickable that is selected or not (a radio button's row: role RadioButton), toggled on
    // and off (a checkbox's or switch's row), or in one of three states — with its semantics.
    // Give the control inside no callback (RadioButton(selected, {})) so the row takes the tap.
    Modifier selectable(bool selected, std::function<void()> onClick, SemanticsRole role = SemanticsRole::None,
                        const ClickableOptions& options = {}) const;
    Modifier toggleable(bool value, std::function<void(bool)> onValueChange, SemanticsRole role = SemanticsRole::None,
                        const ClickableOptions& options = {}) const;
    Modifier triStateToggleable(ToggleableState state, std::function<void()> onClick, SemanticsRole role = SemanticsRole::None,
                                const ClickableOptions& options = {}) const;
    // The selectables inside belong together (a radio group), for accessibility services.
    Modifier selectableGroup() const;
    // A click that can also be long pressed or double clicked (onClick then waits for the
    // double-click timeout).
    Modifier combinedClickable(std::function<void()> onClick, std::function<void()> onLongClick,
                               std::function<void()> onDoubleClick = {}, bool enabled = true) const;
    // Drags along one axis report their delta to the state (rememberDraggableState, gestures.hpp).
    Modifier draggable(const DraggableState& state, Orientation orientation, bool enabled = true,
                       std::optional<MutableInteractionSource> interactionSource = {}) const;
    // Drags in any direction (past the touch slop, or at once) go to the state as offsets
    // (Compose's Modifier.draggable2D); the drag is consumed, so scrolling around it leaves it.
    Modifier draggable2D(const Draggable2DState& state) const;
    // Scrolls the state in any direction (Compose's Modifier.scrollable2D): a drag past the
    // touch slop, a fling after it (the scrollables' decay, axis by axis until an end), the
    // mouse wheel (and its sideways one). The gesture is consumed; a press stops a fling.
    Modifier scrollable2D(const Scrollable2DState& state, bool enabled = true,
                          std::optional<MutableInteractionSource> interactionSource = {}) const;
    Modifier draggable2D(const Draggable2DState& state, const Draggable2DOptions& options) const;
    // Drags along `orientation` move the state's offset between its anchors; a release settles
    // on one (anchored_draggable.hpp). The drag is all its own: none of it goes on to the
    // scrollables around it.
    template <class T>
    Modifier anchoredDraggable(const AnchoredDraggableState<T>& state, Orientation orientation, bool enabled = true,
                               bool reverseDirection = false, std::optional<MutableInteractionSource> interactionSource = {}) const;
    Modifier anchoredDraggable(std::shared_ptr<detail::AnchoredDragTarget> target, Orientation orientation, bool enabled,
                               bool reverseDirection, std::optional<MutableInteractionSource> interactionSource) const;
    // Pinch, twist and drag report zoom, rotation and pan to the state (gestures.hpp). A single
    // finger only pans when canPan says so (e.g. once zoomed in), leaving it to scrolling otherwise.
    Modifier transformable(const TransformableState& state, std::function<bool(Offset pan)> canPan = {},
                           bool lockRotationOnZoomPan = false, bool enabled = true) const;
    // Handles raw pointer input with a coroutine (see gestures.hpp); restarts when `key` changes.
    template <class K, class F>
    Modifier pointerInput(const K& key, F block) const;
    // Custom drawing behind the content / around it (call drawContent()), see draw.hpp.
    Modifier drawBehind(std::function<void(DrawScope&)> onDraw) const;
    Modifier drawWithContent(std::function<void(ContentDrawScope&)> onDraw) const;
    Modifier drawWithCache(std::function<DrawResult(CacheDrawScope&)> build) const;
    // Draws a Painter behind the content (Compose's Modifier.paint): scaled by `contentScale`
    // and placed by `alignment` in the node; with sizeToIntrinsics the node is as big as the
    // painter (scaled to fit what it may be) unless its size is fixed.
    Modifier paint(const Painter& painter, bool sizeToIntrinsics = true, Alignment alignment = Alignment::Center,
                   ContentScale contentScale = ContentScale::Inside, float alpha = 1, std::optional<ColorFilter> colorFilter = {}) const;
    // Scales, rotates, moves or fades the content as drawn (through an offscreen layer).
    Modifier graphicsLayer(const GraphicsLayerScope& layer) const;
    // The content blurred by `radius` dp across and down (Compose's Modifier.blur): a graphicsLayer
    // with a BlurEffect, clipped to the edge treatment's shape if it has one. Unless it is
    // clipped, a radius of 0 either way draws it as it is.
    // `type`: what it blurs (BlurType; Family: a graphicsLayer, as Compose's).
    Modifier blur(float radius, const BlurredEdgeTreatment& edgeTreatment = BlurredEdgeTreatment::Rectangle,
                  BlurType type = BlurType::Family) const {
        return blur(radius, radius, edgeTreatment, type);
    }
    Modifier blur(float radiusX, float radiusY, const BlurredEdgeTreatment& edgeTreatment = BlurredEdgeTreatment::Rectangle,
                  BlurType type = BlurType::Family) const;
    // Blurred as much as `blurRadiusSpec` says where (a progressive blur: Compose 1.13's), clipped
    // to the edge treatment's shape if it has one, faded by `alpha`: always a graphicsLayer, which
    // a uniform radius of 0 leaves without an effect.
    Modifier blur(const BlurRadiusSpec& blurRadiusSpec, const BlurredEdgeTreatment& edgeTreatment = BlurredEdgeTreatment::Rectangle,
                  float alpha = 1, BlurType type = BlurType::Family) const;
    // The same, set by `block` while drawing: animated values read there never recompose.
    Modifier blur(std::function<void(BlurScope&)> block) const;
    // Evaluated while drawing: animated values read here never recompose.
    Modifier graphicsLayer(std::function<void(GraphicsLayerScope&)> block) const;
    // `enabled` false: scrolled only in code; `reverseScrolling`: it starts at the end (the bottom,
    // the right), its value counting back from there (Compose's).
    Modifier verticalScroll(const ScrollState& state, bool enabled = true, bool reverseScrolling = false) const;
    Modifier horizontalScroll(const ScrollState& state, bool enabled = true, bool reverseScrolling = false) const;
    // With an overscroll effect of its own (drawn here too), or none (nullptr).
    Modifier verticalScroll(const ScrollState& state, std::shared_ptr<OverscrollEffect> overscrollEffect, bool enabled = true,
                            bool reverseScrolling = false) const;
    Modifier horizontalScroll(const ScrollState& state, std::shared_ptr<OverscrollEffect> overscrollEffect, bool enabled = true,
                              bool reverseScrolling = false) const;
    // Draws `overscrollEffect` around what comes after it (Compose's overscroll): for an effect
    // given to scrollable(), which doesn't draw it.
    Modifier overscroll(std::shared_ptr<OverscrollEffect> overscrollEffect) const;
    // Drags and flings along `orientation` go to `state` (nothing is moved or clipped for you).
    Modifier scrollable(const ScrollableState& state, Orientation orientation, bool enabled = true,
                        bool reverseDirection = false, std::optional<MutableInteractionSource> interactionSource = {}) const;
    // With an overscroll effect taking the touch drags and flings past its ends (not drawn here:
    // put Modifier.overscroll(effect) where it should show).
    Modifier scrollable(const ScrollableState& state, Orientation orientation, std::shared_ptr<OverscrollEffect> overscrollEffect,
                        bool enabled = true, bool reverseDirection = false,
                        std::optional<MutableInteractionSource> interactionSource = {}) const;
    // A scrollable area (Compose's scrollableArea), for a layout that places its content from
    // the state itself: scrollable(), and what comes after it clipped along `orientation` (as
    // the scroll containers clip) with the overscroll drawn over it — the stretch, or
    // `overscrollEffect` (nullptr: none). `enabled` false: scrolled only in code, still clipped.
    Modifier scrollableArea(const ScrollableState& state, Orientation orientation, bool enabled = true, bool reverseScrolling = false,
                            std::optional<MutableInteractionSource> interactionSource = {}) const;
    Modifier scrollableArea(const ScrollableState& state, Orientation orientation, std::shared_ptr<OverscrollEffect> overscrollEffect,
                            bool enabled = true, bool reverseScrolling = false,
                            std::optional<MutableInteractionSource> interactionSource = {}) const;
    // graphicsLayer() shorthands.
    Modifier scale(float scale) const;
    Modifier scale(float scaleX, float scaleY) const;
    Modifier rotate(float degrees) const;
    // Takes part in the scrolling of the scrollables inside it (nested_scroll.hpp); with a
    // dispatcher, a custom gesture here can offer its scroll to the parents too.
    Modifier nestedScroll(std::shared_ptr<NestedScrollConnection> connection) const;
    // The keyboard follows the finger past the end of what scrolls inside (Compose's
    // imeNestedScroll; Android 11+, with a text field focused): pulled on up past the end it
    // comes up, pushed down it goes first; let go, it finishes up or down (by the fling, or
    // where it is). Elsewhere it does nothing. Made in composition.
    Modifier imeNestedScroll() const;
    Modifier nestedScroll(std::shared_ptr<NestedScrollConnection> connection, const NestedScrollDispatcher& dispatcher) const;
    // Material 3 pull to refresh (material.hpp) on what holds scrollable content: pulled down at
    // the content's top, `state` follows (half the finger's way, slower past `threshold` dp); let
    // go past the threshold, onRefresh runs. While isRefreshing the state waits at 1 and scrolls
    // pass by; then it goes back to 0. Put an indicator over the content (PullToRefreshDefaults).
    Modifier pullToRefresh(bool isRefreshing, const PullToRefreshState& state, std::function<void()> onRefresh,
                           bool enabled = true, float threshold = 80, CallSite site = CallSite::current()) const;
    Modifier weight(float weight, bool fill = true) const;  // in Row / Column
    Modifier flex(const FlexConfig& config) const;          // in a FlexBox
    // In a Grid: these cells (row, column: 1-based, negative from the end, 0 where there's room).
    Modifier gridItem(int row = 0, int column = 0, int rowSpan = 1, int columnSpan = 1, Alignment alignment = Alignment::TopStart) const;
    Modifier gridItem(GridRange rows, GridRange columns, Alignment alignment = Alignment::TopStart) const;
    Modifier gridItem(std::string area, Alignment alignment = Alignment::TopStart) const;  // a GridConfig area
    Modifier align(Alignment alignment) const;              // in Box
    // In a Box: as big as the Box is once its other children are measured, without making it
    // bigger (a background, a scrim, a ripple over the others).
    Modifier matchParentSize() const;
    // In a LazyColumn / LazyRow item: a fraction of the list's own width or height (its viewport),
    // as Compose's LazyItemScope.fillParentMaxHeight(0.5f); elsewhere as fillMaxWidth / Height.
    Modifier fillParentMaxWidth(float fraction = 1) const;
    Modifier fillParentMaxHeight(float fraction = 1) const;
    Modifier fillParentMaxSize(float fraction = 1) const;
    Modifier align(HorizontalAlignment alignment) const;    // in Column
    Modifier align(VerticalAlignment alignment) const;      // in Row
    // In a Row: the children with it lined up by that line (Compose's alignBy / alignByBaseline):
    // texts of different sizes on one baseline, a button's label on a text's. One without the
    // line (no text in it) is aligned as the Row says.
    Modifier alignBy(AlignmentLine line) const;
    Modifier alignByBaseline() const { return alignBy(FirstBaseline); }
    // Room above and below so that `line` of the content is `before` from the top and `after` from
    // the bottom (Compose's paddingFrom); NaN: none that way. paddingFromBaseline: the first
    // baseline `top` from the top, the last one `bottom` from the bottom (text set on a grid).
    Modifier paddingFrom(AlignmentLine line, float before = std::numeric_limits<float>::quiet_NaN(),
                         float after = std::numeric_limits<float>::quiet_NaN()) const;
    Modifier paddingFromBaseline(float top = std::numeric_limits<float>::quiet_NaN(),
                                 float bottom = std::numeric_limits<float>::quiet_NaN()) const;
    // Called during layout when the measured size changes (e.g. to keep it in a state).
    Modifier onSizeChanged(std::function<void(Size)> onSizeChanged) const;
    // Told after a layout where it ended up and how big it is — again whenever that changes,
    // a scroll moving it too (Compose's onGloballyPositioned). To place a popup by it, to know
    // whether it is in view (boundsInRoot), to measure it.
    Modifier onGloballyPositioned(std::function<void(const LayoutCoordinates&)> onGloballyPositioned) const;
    // ...only when its size or its place in its parent change (Compose's onPlaced).
    Modifier onPlaced(std::function<void(const LayoutCoordinates&)> onPlaced) const;
    // Animates the size whenever the content's size changes (clipping the content meanwhile).
    // Uses composition (remember), so build it while composing, like every modifier here.
    Modifier animateContentSize(CallSite site = CallSite::current()) const;
    Modifier animateContentSize(const AnimationSpec& spec, CallSite site = CallSite::current()) const;
    // Compose's animateBounds: when a layout moves or resizes the node, it glides there from where
    // it was drawn (and its content is laid out at the size along the way), its place measured from
    // the nearest LookaheadScope (scrolling that doesn't move it). Only the drawing moves; taps land
    // where it is laid out. Uses composition (remember).
    Modifier animateBounds(CallSite site = CallSite::current()) const;
    // While a layout around it animates its size (animateBounds, a shared element moving), its
    // content is laid out at the size it is going to, not the one along the way (Compose's
    // skipToLookaheadSize): text doesn't reflow as it grows. Its own size follows the animation.
    Modifier skipToLookaheadSize() const;
    // A layout step that approaches where its content is going (Compose's approachLayout): its
    // content is first measured as it will be (the lookahead: `lookaheadSize`), then, while
    // isMeasurementApproachInProgress(lookaheadSize), approachMeasure lays it out along the way
    // (measure it, return the size, set where it goes) — say towards the lookahead size through
    // a DeferredTargetAnimation; after, as the lookahead. What it reads there lays it out again.
    Modifier approachLayout(std::function<bool(Size lookaheadSize)> isMeasurementApproachInProgress,
                            std::function<Size(const ApproachMeasureScope& scope, const MeasureNext& measure, const Constraints& constraints,
                                               Offset& placeAt)>
                                approachMeasure) const;
    Modifier animateBounds(const AnimationSpec& spec, CallSite site = CallSite::current()) const;
    // Compose's basicMarquee: content wider than the room it has (measured without a width limit)
    // scrolls through it towards its start, followed by a copy of itself, `spacing` behind, as on
    // a ticker. Only the drawing moves (taps land where the content is laid out); it pauses
    // `repeatDelayMillis` between passes and stops after `iterations`. Uses composition (remember).
    Modifier basicMarquee(const MarqueeOptions& options = {}, CallSite site = CallSite::current()) const;
    // On a LazyColumn/LazyRow item: glides to its new place when items are added, removed or
    // reordered (instead of jumping), fades in when it is inserted (not when scrolled in) and
    // out where it was when it is removed (shown as it last was, taking no input).
    Modifier animateItem() const;
    Modifier animateItem(const AnimationSpec& placementSpec) const;
    Modifier animateItem(const AnimationSpec& fadeInSpec, const AnimationSpec& placementSpec, const AnimationSpec& fadeOutSpec) const;
    Modifier testTag(std::string tag) const;
    // Among its siblings (the children of the same parent), drawn above those with a smaller
    // zIndex and below those with a bigger one, and touched first where it covers them; equal
    // ones keep their order. Added to the zIndex its parent placed it with (Placeable::place).
    Modifier zIndex(float zIndex) const;
    Modifier semantics(std::string contentDescription) const;
    Modifier semantics(SemanticsProperties properties) const;
    // Compose's clearAndSetSemantics: what is inside says nothing (not even its buttons, to
    // accessibility services), this node says `properties` (a rating of five star icons read
    // as "4 / 5").
    Modifier clearAndSetSemantics(SemanticsProperties properties) const;
    // Leaves it and what is inside out of the semantics tree (a decoration, a duplicate).
    Modifier hideFromAccessibility() const;
    Modifier onFocusChanged(std::function<void(FocusState)> onFocusChanged) const;
    // Told of every focus event about it or what is inside it, even when its state stays the
    // same (the focus moving among its children), unlike onFocusChanged (Compose's onFocusEvent).
    Modifier onFocusEvent(std::function<void(FocusState)> onFocusEvent) const;
    // Says it shows progress (Compose's progressSemantics): `value` (kept in the range) from
    // valueFrom to valueTo, with `steps` values in between; without one, progress of no value.
    Modifier progressSemantics(float value, float valueFrom = 0, float valueTo = 1, int steps = 0) const;
    Modifier progressSemantics() const;
    // Shows what `interactionSource` hears — ripples for its presses, the state layer for hover
    // and focus — on this node (Compose's Modifier.indication): a clickable with
    // {.interactionSource = s, .indication = false} around it, the ripple only on the icon in it.
    Modifier indication(const MutableInteractionSource& interactionSource, const Indication& indication) const;
    // The mouse pointer's look over it (Compose Desktop's pointerHoverIcon): the innermost one
    // under the pointer wins — clickables show a hand and text fields an I-beam of their own —
    // unless one around them says overrideDescendants.
    Modifier pointerHoverIcon(Cursor icon, bool overrideDescendants = false) const;
    // Lets Tab and the arrow keys focus it (clickables already are); taps don't.
    Modifier focusable(bool enabled = true, std::optional<MutableInteractionSource> interactionSource = {}) const;
    // Tells `interactionSource` when a mouse pointer goes over it and away.
    Modifier hoverable(const MutableInteractionSource& interactionSource, bool enabled = true) const;
    // At least LocalMinimumInteractiveComponentSize (48 dp) each way, what is smaller centered in
    // it, so that it can be touched (Compose's minimumInteractiveComponentSize).
    Modifier minimumInteractiveComponentSize() const;
    Modifier onPointerEvent(PointerEventType eventType, std::function<void(PointerEvent&)> onEvent) const;
    // A click as Compose Multiplatform's Modifier.onClick: by the buttons the matcher names, with
    // the keys `keyboardModifiers` wants held down; no ripple, focus or semantics (clickable has
    // those). Uses composition (remember).
    Modifier onClick(std::function<void()> onClick, OnClickOptions options = {}, CallSite site = CallSite::current()) const;
    Modifier onKeyEvent(std::function<bool(const KeyEvent&)> onKeyEvent) const;
    Modifier onPreviewKeyEvent(std::function<bool(const KeyEvent&)> onPreviewKeyEvent) const;
    Modifier focusRequester(const FocusRequester& requester) const;
    Modifier bringIntoViewRequester(const BringIntoViewRequester& requester) const;
    // Its focusables go together for the arrow keys (Compose's focusGroup): a move looks among
    // them first, and from outside the group is one target, entered at its focusable nearest
    // that way. (Tab goes in order either way.)
    Modifier focusGroup() const;
    // Focus coming back into it from outside — the keys, or requestFocus on it — goes to what had
    // it when it left (Compose's focusRestorer): a row of chips re-entered at the one left. The
    // first time (or that one gone), `fallback`'s node if set, else the move's own target.
    Modifier focusRestorer(std::optional<FocusRequester> fallback = std::nullopt) const;
    // Makes it a focus target (Compose's focusTarget): as focusable(), the lower-level name.
    Modifier focusTarget() const { return focusable(); }
    // A magnifier over everything showing the content around sourceCenter (read as it is drawn,
    // in this node; NaN hides it) bigger (Compose's Modifier.magnifier).
    Modifier magnifier(std::function<Offset()> sourceCenter, MagnifierOptions options = {}) const;
    Modifier focusProperties(FocusProperties properties) const;
    // Lets the user drag it — a finger's long press, then a drag; the mouse just drags — and
    // drop it on a dragAndDropTarget (Compose's dragAndDropSource); a faded copy of it follows
    // the pointer. transferData(where it was grabbed) says what goes with the drag (nothing: no
    // drag). Escape cancels it.
    Modifier dragAndDropSource(std::function<std::optional<DragAndDropTransferData>(Offset)> transferData) const;
    // Takes drops (Compose's dragAndDropTarget): shouldStartDragAndDrop says, as a drag starts,
    // whether this one takes part (by its MIME types, say); then `target` hears of it.
    Modifier dragAndDropTarget(std::function<bool(const DragAndDropEvent&)> shouldStartDragAndDrop, DragAndDropTarget target) const;
    // From the android/snippets RecomposeHighlighter: a border that shows how often the
    // composable emitting this node ran (blue once, green twice, then yellow to red, thicker).
    Modifier recomposeHighlighter() const;

    Modifier then(const Modifier& other) const;
    Modifier then(std::shared_ptr<const ModifierElement> element) const;

    const std::vector<std::shared_ptr<const ModifierElement>>& elements() const { return elements_; }

    friend bool operator==(const Modifier& a, const Modifier& b) {
        if (a.elements_.size() != b.elements_.size()) return false;
        for (size_t i = 0; i < a.elements_.size(); ++i)
            if (!a.elements_[i]->equals(*b.elements_[i])) return false;
        return true;
    }

    template <class T>
    const T* find() const {
        for (auto it = elements_.rbegin(); it != elements_.rend(); ++it)
            if (auto* e = dynamic_cast<const T*>(it->get())) return e;
        return nullptr;
    }

private:
    std::vector<std::shared_ptr<const ModifierElement>> elements_;
};

// A popup root (Popup, Dialog, DropdownMenu): composed where it is called, so it keeps its
// state and CompositionLocals, but laid out and drawn above everything else by the host.
struct OverlaySpec {
    bool fullScreen = false;  // measured at the window size (dialogs, sheets); otherwise wraps its content
    // Where a wrapping popup goes, given its anchor (the node it was composed in), in dp.
    std::function<Offset(const Rect& anchor, Size size, Size viewport)> position;
    std::function<void()> onDismissRequest;  // back / Escape and, if set, taps outside
    bool dismissOnOutsideClick = true;
    bool focusable = true;  // takes all input while on top; false: input passes through...
    bool touchable = false;  // ...except on its own interactive elements (text selection handles)
};

namespace detail {
// While one is alive, a layout is measuring children only to learn their natural size (an
// intrinsic, a menu's or a tab row's widths) before measuring them for real. Layout-time state
// writes (BoxWithConstraints, onSizeChanged, animateContentSize, ...) skip probe measures, or a
// child measured twice with different constraints would recompose every frame.
struct ProbeMeasure {
    ProbeMeasure();
    ~ProbeMeasure();
    ProbeMeasure(const ProbeMeasure&) = delete;
    ProbeMeasure& operator=(const ProbeMeasure&) = delete;
};
bool isProbeMeasure();
// The passes of a layout that animates towards where its content goes (animateBounds, a moving
// shared element): the lookahead one finds where it goes, the approach one lays the content out
// at the size along the way. skipToLookaheadSize lays its content out at the lookahead size in both.
enum class LayoutPass { Plain, Lookahead, Approach };
struct InLayoutPass {
    explicit InLayoutPass(LayoutPass pass);
    ~InLayoutPass();
    InLayoutPass(const InLayoutPass&) = delete;
    InLayoutPass& operator=(const InLayoutPass&) = delete;
    LayoutPass previous;
};
LayoutPass currentLayoutPass();
// While one is alive, a probe asks for min intrinsic widths (IntrinsicSize::Min): texts break at
// every space but don't split words, so they come out as wide as their widest word.
struct MinIntrinsicWidth {
    MinIntrinsicWidth();
    ~MinIntrinsicWidth();
    MinIntrinsicWidth(const MinIntrinsicWidth&) = delete;
    MinIntrinsicWidth& operator=(const MinIntrinsicWidth&) = delete;
};
bool isMinIntrinsicWidth();
// The node being measured, if any: a state read now is read by its layout.
LayoutNode* measuringNode();
// The node being drawn, if any: a state read now is read by its drawing, and a redraw asked for
// now is its (ComposeHost::requestRedraw).
LayoutNode* drawingNode();
// Layout is in whole pixels, as Compose's (its sizes, constraints and places are Int px): the
// px a dp of the host measuring now (1 with none), and a length in dp at the nearest whole px
// there (a half up, as Kotlin's roundToInt; an infinite one as it is).
float layoutDensity();
float toWholePx(float dp);
Size toWholePx(Size size);
Constraints toWholePx(const Constraints& constraints);
// At the whole px at or above it (Compose's ceil of a text's size), at `density` px a dp.
float wholePxAbove(float dp, float density);
Size wholePxAbove(Size size);  // (at layoutDensity())
// How many times a node was measured afresh or moved so far: a layout that leaves it as it was
// changed nothing.
uint64_t& relaidOutCount();
// Layout nodes (of this thread) with popups composed inside them: while none has, nothing need
// look for them.
int& nodesWithPopups();
// While one is alive, every node is measured afresh, none kept as it was (the layout check's
// reference layout).
struct MeasureEverything {
    MeasureEverything();
    ~MeasureEverything();
    MeasureEverything(const MeasureEverything&) = delete;
    MeasureEverything& operator=(const MeasureEverything&) = delete;
};
// What of `insets` the windowInsetsPadding()s / consumeWindowInsets() of `node` and its
// ancestors haven't taken yet: for a layout that keeps clear of them itself (BottomSheetScaffold).
WindowInsets unconsumedInsets(const LayoutNode& node, const WindowInsets& insets);
} // namespace detail

// Measures the node's children (placing them with LayoutNode::place) and returns the content size.
using MeasurePolicy = std::function<Size(LayoutNode& node, const Constraints& constraints)>;
using DrawContent = std::function<void(GraphicsContext& canvas, const Rect& contentBounds, const LayoutNode& node)>;

// A node of the retained UI tree. Nodes survive recompositions; composition only updates
// their properties and child lists.
class LayoutNode {
public:
    explicit LayoutNode(ComposeHost* host) : host_(host) {}
    ~LayoutNode();
    LayoutNode(const LayoutNode&) = delete;
    LayoutNode& operator=(const LayoutNode&) = delete;

    // Written during composition.
    Modifier modifier;
    MeasurePolicy measurePolicy;
    DrawContent drawContent;
    std::vector<LayoutNode*> children;
    LayoutNode* parent = nullptr;  // the enclosing node (a popup root's anchor); null for the root
    std::string semanticsText;  // what tests and accessibility services read
    std::vector<std::shared_ptr<detail::PointerInputState>> pointerInputs;  // see gestures.hpp
    std::shared_ptr<const OverlaySpec> overlay;  // set on popup roots
    // A focusable popup root's own back: the back handlers composed in it (which carry it)
    // get back while it is on top, before it is dismissed.
    std::shared_ptr<const void> backScope;
    std::vector<LayoutNode*> overlays;          // popups composed inside this node (not in `children`)

    // Interaction state, owned by the host.
    bool pressed = false, hovered = false, focused = false;
    bool rtl = false;  // laid out right to left (LocalLayoutDirection where it was composed)
    // The stretch of a scrollable on it with no overscroll effect of its own (made by the host).
    std::shared_ptr<OverscrollEffect> stretch;
    // A custom indication's node on it (made by the host after layout, from its factory), and
    // the interactions it hears when its clickable has no source of its own.
    std::shared_ptr<IndicationNode> indicationNode;
    const IndicationNodeFactory* indicationFactory = nullptr;
    std::optional<MutableInteractionSource> indicationSource, indicationHeard;  // its own; the one its node hears
    // A scrollable's overscroll effect (on this node): its own, or the node's stretch.
    OverscrollEffect* overscrollOf(const ScrollableElement& s) const {
        return s.overscrollEffect ? s.overscrollEffect.get() : s.overscrolls() ? stretch.get() : nullptr;
    }
    uint32_t compositions = 0;  // how many times the composable emitting this node ran
    // A text's own baselines (first, last) from the top of its content, set by its measure
    // policy; NaN: none. Nodes without them pass on their children's (alignmentLine).
    float textBaselines[2] = {std::numeric_limits<float>::quiet_NaN(), std::numeric_limits<float>::quiet_NaN()};

    // Layout
    Size measure(const Constraints& constraints);
    // Layout is incremental: a node measured again with the constraints of its last measure,
    // with nothing changed in it or below it, keeps its size and its children's places. What
    // changes a node (composing it, a state its measure read, a layout animation's next frame)
    // marks it, and the nodes around it, to be measured again.
    void invalidateMeasure();
    bool needsMeasure() const { return needsMeasure_; }
    // The host's walk after layout goes only where nodes it acts on are: how many its subtree
    // holds (itself too) is kept as their elements change (worked out when a node is measured) and
    // worked out again from its children when they change.
    void recountReported();
    // Where its parent puts it: at the nearest whole pixels (detail::toWholePx).
    void place(float x, float y, float zIndex = 0);
    Size size() const { return size_; }
    // Placed by its parent's last layout. One only measured (a size probe, a subcomposed slot
    // left out) isn't drawn, hit or told to accessibility services, as in Compose.
    bool isPlaced() const { return placedNow_; }
    // Its place and those of all in it, from where its parent's content starts. Moved without being
    // measured again (a list scrolled), nothing in it was: its children's places are only marked
    // stale (worked out when asked, written down when drawn), not walked into.
    void updateAbsolutePosition(Offset parentContentOrigin);
    // Once measured: where `line` is, from its top (Compose's placeable[FirstBaseline]); NaN when
    // nothing in it has the line. For a layout of your own lining children up.
    float alignmentLine(AlignmentLine line) const;
    Rect bounds() const;
    Rect contentBounds() const;
    Rect layerBounds(size_t index) const;
    ParentData parentData() const;
    float zIndex() const { return zIndex_; }  // where it was placed and Modifier::zIndex, from the last layout
    // The children bottom to top: as composed, or by zIndex where some have one.
    const std::vector<LayoutNode*>& childrenInDrawOrder() const {
        return zOrdered_ && zOrder_.size() == children.size() ? zOrder_ : children;
    }

    void draw(GraphicsContext& canvas);
    // What it draws, or what something in it draws, changed since a frame kept it
    // (DisplayList::keepNodes): the next frame draws it again, and its parents. Its measure, the
    // states its drawing read, asking for a redraw as it draws (an animation drawn on), the host's
    // pressed, hovered, focused and ripples say so; a drawing from anything else says so itself.
    void invalidateDraw() const;
    // Draws what `element` (one of its modifier's) wraps, without it and the elements before it:
    // shared elements are drawn this way above everything, wherever their parents are.
    void drawAfter(GraphicsContext& canvas, const ModifierElement* element);
    std::optional<Rect> boundsOf(const ModifierElement* element) const;  // that element's layer rect

    ComposeHost* host() const { return host_; }
    TextMeasurer& textMeasurer() const;
    // Outlives the node and reads null once it is gone (for objects that may outlive it).
    std::weak_ptr<LayoutNode*> handle();

private:
    Size measureLayer(size_t index, const Constraints& constraints);
    float contentAlignmentLine(AlignmentLine line) const;  // from the content's top
    float alignmentLineInside(size_t layer, AlignmentLine line) const;  // from that layer's top
    void drawLayer(GraphicsContext& canvas, size_t index);
    mutable bool drawChanged_ = true;  // invalidateDraw's: not kept (its parents' too, up to one that is so already)
    bool childrenStale_ = false;  // it moved since its children's places were written down (updateAbsolutePosition)
    void setChildrenStale(bool stale);
    void placeAt(Offset origin);  // its own places written down, from its top left (not its children's)
    void settleChildren();        // its children's places written down from its (theirs marked stale if they moved)
    void settlePlace();           // its own written down, its stale parents' children's on the way to it
    // Its top left as it is: written down, or worked out from its nearest parent that is (each one
    // down from there as updateAbsolutePosition would, to the same floats).
    Offset trueOrigin() const;
    // `node` and its parents up to the highest whose parent's children are stale, that last first
    // (none stale: empty). A popup's root is placed on its own, not from its parent.
    static const std::vector<const LayoutNode*>& staleChain(const LayoutNode& node);
    std::vector<const void*> drawReads_;  // the states its last drawing read

    struct Layer {
        Size size;
        Offset contentOffset;
        Rect bounds;
    };
    ComposeHost* host_;
    std::shared_ptr<LayoutNode*> handle_;
    std::vector<Layer> layers_;
    Offset position_;
    bool placedNow_ = false;  // placed by its parent's last layout (a right-to-left parent mirrors it)
    Size size_, contentSize_;
    Rect bounds_, contentBounds_;
    float placedZIndex_ = 0, zIndex_ = 0;
    bool zOrdered_ = false;
    std::vector<LayoutNode*> zOrder_;
    bool needsMeasure_ = true;  // set: so are its ancestors'
    std::optional<Constraints> measuredWith_;  // its last measure's (none after a size probe)
    detail::LayoutPass measuredIn_ = detail::LayoutPass::Plain;
    std::vector<const void*> layoutReads_;  // the states its last measure read
    bool relaidOut_ = true;  // measured or moved since its absolute place was last worked out
    // The host's walk after layout: what each of these elements is to it (bits), and whether none
    // is one it acts on.
    std::vector<std::shared_ptr<const ModifierElement>> reportCheckedFor_;
    std::vector<uint16_t> reported_;
    bool reportsNothing_ = true;  // (none: nothing)
    bool reports_ = false;   // the walk acts on it (its elements, a stretch or an indication node it keeps)
    int reportedBelow_ = 0;  // nodes the walk acts on in its subtree, itself too
    void checkReported();    // its elements found out again if they changed (host.cpp)
    bool scrollableAt(size_t index) const;  // its element `index` is a ScrollableElement (as found out; host.cpp)
    void setReports(bool on);
    friend class ComposeHost;
};

namespace detail {
// The children measured with the node's own constraints, all at its top left; as big as the
// biggest. For a node that only holds its content (a scaffold slot, a list item's container).
Size stackPolicy(LayoutNode& node, const Constraints& c);
// A node of its own for `content`, laid out by stackPolicy, so a layout can find it by index.
void stackSlot(const Content& content);
} // namespace detail

} // namespace kompoz
