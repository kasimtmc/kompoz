#include <kompoz/animation.hpp>
#include <kompoz/channel.hpp>
#include <kompoz/coroutines.hpp>
#include <kompoz/draw.hpp>
#include <kompoz/gestures.hpp>
#include <kompoz/host.hpp>
#include <kompoz/icons.hpp>
#include <kompoz/scrollbar.hpp>
#include <kompoz/strings.hpp>
#include <kompoz/widgets.hpp>

#include "text_layout.hpp"

#include <array>
#include <optional>
#include <unordered_map>
#include <cmath>

namespace kompoz {

// A field writes what is typed (and the selection) into its TextFieldState.
struct TextFieldStateAccess {
    static void set(const TextFieldState& state, std::string text, TextRange selection) {
        state.set(std::move(text), selection, TextFieldState::Source::User);
    }
};

namespace {

void layoutNode(const CallSite& site, const Modifier& modifier, MeasurePolicy policy, const Content& content,
                DrawContent draw = {}, std::string semantics = {}) {
    detail::Composer::require().emit(
        detail::keyOf(site),
        [&](LayoutNode& node) {
            node.modifier = modifier;
            node.measurePolicy = std::move(policy);
            node.drawContent = std::move(draw);
            node.semanticsText = std::move(semantics);
        },
        content);
}

void emitText(const std::string& text, const Modifier& modifier, const TextStyle& style);
void emitStyledText(const AnnotatedString& text, const Modifier& modifier, const TextStyle& style,
                    std::function<void(const TextLayoutResult&)> onTextLayout = {},
                    const std::map<std::string, InlineTextContent>* inlineContent = nullptr);

MeasurePolicy boxPolicy(Alignment contentAlignment) {
    return [contentAlignment](LayoutNode& node, const Constraints& c) {
        std::vector<Size> sizes;
        sizes.reserve(node.children.size());
        Size size{c.minWidth, c.minHeight};
        for (auto* child : node.children) {
            Size s = child->parentData().matchParentSize ? Size{} : child->measure(c.loosen());
            sizes.push_back(s);
            size = {std::max(size.width, s.width), std::max(size.height, s.height)};
        }
        size = c.constrain(size);
        for (size_t i = 0; i < node.children.size(); ++i)  // matchParentSize: as big as the others made it
            if (node.children[i]->parentData().matchParentSize) sizes[i] = node.children[i]->measure(Constraints::fixed(size.width, size.height));
        for (size_t i = 0; i < node.children.size(); ++i) {
            Alignment a = node.children[i]->parentData().boxAlign.value_or(contentAlignment);
            Offset o = a.align(sizes[i], size);
            node.children[i]->place(o.x, o.y);
        }
        return size;
    };
}

// Shared by Row (horizontal) and Column.
MeasurePolicy linearPolicy(bool horizontal, Arrangement arrangement, float crossBias) {
    return [=](LayoutNode& node, const Constraints& c) {
        auto mainOf = [&](Size s) { return horizontal ? s.width : s.height; };
        auto crossOf = [&](Size s) { return horizontal ? s.height : s.width; };
        auto make = [&](float mainMin, float mainMax, float crossMax) {
            return horizontal ? Constraints{mainMin, mainMax, 0, crossMax} : Constraints{0, crossMax, mainMin, mainMax};
        };
        float minMain = horizontal ? c.minWidth : c.minHeight, maxMain = horizontal ? c.maxWidth : c.maxHeight;
        float minCross = horizontal ? c.minHeight : c.minWidth, maxCross = horizontal ? c.maxHeight : c.maxWidth;

        size_t n = node.children.size();
        std::vector<Size> sizes(n);
        std::vector<ParentData> data(n);
        float totalWeight = 0, used = 0, crossSize = 0;
        float spacing = arrangement.spacing * float(n > 0 ? n - 1 : 0);
        for (size_t i = 0; i < n; ++i) {
            data[i] = node.children[i]->parentData();
            totalWeight += std::max(0.f, data[i].weight);
        }
        // Children without weight take what they need...
        for (size_t i = 0; i < n; ++i) {
            if (data[i].weight > 0) continue;
            float remaining = maxMain == kInfinity ? kInfinity : std::max(0.f, maxMain - used - spacing);
            sizes[i] = node.children[i]->measure(make(0, remaining, maxCross));
            used += mainOf(sizes[i]);
            crossSize = std::max(crossSize, crossOf(sizes[i]));
        }
        // ...and weighted children share the rest.
        if (totalWeight > 0) {
            float remaining = maxMain == kInfinity ? 0 : std::max(0.f, maxMain - used - spacing);
            for (size_t i = 0; i < n; ++i) {
                if (data[i].weight <= 0) continue;
                float share = remaining * data[i].weight / totalWeight;
                // Unbounded (measuring the natural size): a weight has nothing to share out.
                sizes[i] = maxMain == kInfinity ? node.children[i]->measure(make(0, kInfinity, maxCross))
                                                : node.children[i]->measure(make(data[i].fill ? share : 0, share, maxCross));
                used += mainOf(sizes[i]);
                crossSize = std::max(crossSize, crossOf(sizes[i]));
            }
        }
        // Children lined up by an alignment line (a Row's alignBy): the line at the same height
        // in all of them, the row tall enough for what is above and below it.
        std::vector<float> lines(n, std::numeric_limits<float>::quiet_NaN());
        float above = 0, below = 0;
        bool lined = false;
        for (size_t i = 0; i < n && horizontal; ++i) {
            if (!data[i].alignBy) continue;
            lines[i] = node.children[i]->alignmentLine(*data[i].alignBy);
            if (std::isnan(lines[i])) continue;
            lined = true;
            above = std::max(above, lines[i]);
            below = std::max(below, sizes[i].height - lines[i]);
        }
        if (lined) crossSize = std::max(crossSize, above + below);
        // As big as the children came out (weighted ones that fill take all their share; ones
        // that don't may leave some of it), as in Compose.
        float mainSize = std::clamp(used + spacing, minMain, maxMain);
        crossSize = std::clamp(crossSize, minCross, maxCross);

        std::vector<float> mains(n), positions;
        for (size_t i = 0; i < n; ++i) mains[i] = mainOf(sizes[i]);
        arrangement.arrange(mainSize, mains, positions);
        for (size_t i = 0; i < n; ++i) {
            float bias = data[i].crossAxisBias.value_or(crossBias);
            float cross = !std::isnan(lines[i]) ? above - lines[i] : (crossSize - crossOf(sizes[i])) / 2 * (1 + bias);
            if (horizontal)
                node.children[i]->place(positions[i], cross);
            else
                node.children[i]->place(cross, positions[i]);
        }
        return horizontal ? Size{mainSize, crossSize} : Size{crossSize, mainSize};
    };
}

enum class ButtonKind { Filled, Tonal, Elevated, Outlined, Text };

Shape morphingShape(const Shape& target, CallSite site = CallSite::current()) { return detail::morphingShape(target, site); }

// A button's padding, which gives way before its label does: given an exact width narrower
// than the label and its padding (squeezed in a ButtonGroup, a fixed width), the sides shrink
// first (each by its share) and the label wraps only once they're gone.
Modifier buttonPadding(const Modifier& m, PaddingValues padding) {
    return m.layout([padding](const MeasureNext& next, const Constraints& c, Offset& at) {
        float sides = padding.start + padding.end, vertical = padding.top + padding.bottom;
        Constraints inner = c.deflate(sides, vertical);
        float start = padding.start, end = padding.end;
        if (c.minWidth == c.maxWidth && c.maxWidth < kInfinity) {
            float natural;
            {
                detail::ProbeMeasure probe;  // the label's own width; measured for real below
                natural = next({0, kInfinity, inner.minHeight, inner.maxHeight}).width;
            }
            if (natural > inner.maxWidth) {
                inner.minWidth = inner.maxWidth = std::min(natural, c.maxWidth);
                float left = c.maxWidth - inner.maxWidth;
                start = sides > 0 ? left * padding.start / sides : left / 2;
                end = left - start;
            }
        }
        Size s = next(inner);
        at = {start, padding.top};  // (mirrored right to left)
        return c.constrain({s.width + start + end, s.height + vertical});
    });
}
Modifier buttonPadding(const Modifier& m, float horizontal, float vertical) { return buttonPadding(m, {horizontal, vertical, horizontal, vertical}); }

void buttonImpl(ButtonKind kind, std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style,
                const Content& content, const CallSite& site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    Shape shape = RoundedCornerShape(20);
    std::optional<MutableInteractionSource> source = style.interactionSource;
    if (style.shapes) {
        if (!source) source = remember([] { return MutableInteractionSource(); });
        shape = morphingShape(source->collectIsPressedAsState() ? style.shapes->pressedShape : style.shapes->shape);
    }
    bool filled = kind == ButtonKind::Filled || kind == ButtonKind::Tonal || kind == ButtonKind::Elevated;
    Color container = kind == ButtonKind::Filled     ? cs.primary
                      : kind == ButtonKind::Tonal    ? cs.secondaryContainer
                      : kind == ButtonKind::Elevated ? cs.surfaceContainerLow
                                                     : colors::Transparent;
    Color contentColor = kind == ButtonKind::Filled ? cs.onPrimary : kind == ButtonKind::Tonal ? cs.onSecondaryContainer : cs.primary;
    container = style.containerColor.value_or(container);
    contentColor = style.contentColor.value_or(contentColor);
    if (!style.enabled) {
        if (filled) container = cs.onSurface.copy(0.12f);
        contentColor = cs.onSurface.copy(0.38f);
    }
    // Compose 1.4's elevations: filled and tonal ones 1 dp up while hovered, elevated ones 1 dp
    // (3 hovered); back down while pressed; eased in 120 ms, out 150.
    float elevation = 0;
    if (filled) {
        if (!source) source = remember([] { return MutableInteractionSource(); });
        float rest = kind == ButtonKind::Elevated ? 1.f : 0.f, hover = kind == ButtonKind::Elevated ? 3.f : 1.f;
        bool hovered = source->collectIsHoveredAsState() && !source->collectIsPressedAsState();
        AnimationSpec in = tween(120), out = tween(150, 0, Easing{0.4f, 0, 0.6f, 1});
        elevation = animateFloatAsState(!style.enabled ? 0.f : hovered ? hover : rest, hovered ? in : out);
    }

    Modifier m = modifier.minimumInteractiveComponentSize().heightIn(ButtonDefaults::MinHeight).widthIn(ButtonDefaults::MinWidth);
    if (elevation > 0) m = m.shadow(elevation, shape);
    m = m.background(container, shape);
    if (kind == ButtonKind::Outlined) m = m.border(1, cs.outlineVariant.copy(style.enabled ? 1.f : 0.1f), shape);

    CompositionLocalProvider(
        {LocalContentColor.provides(contentColor), LocalTextStyle.provides(typography().labelLarge)}, [&] {
            Row(buttonPadding(m.semantics({.role = SemanticsRole::Button})
                                  .clickable(std::move(onClick), {.enabled = style.enabled, .interactionSource = source}),
                              style.contentPadding.value_or(kind == ButtonKind::Text ? ButtonDefaults::TextButtonContentPadding : ButtonDefaults::ContentPadding)),
                {.horizontalArrangement = Arrangement::spacedBy(8, Arrangement::Kind::Center),
                 .verticalAlignment = Alignment::CenterVertically},
                content);
        });
}

enum class ToggleKind { Standard, Elevated, Tonal, Outlined };

void toggleButtonImpl(ToggleKind kind, bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                      const ToggleButtonStyle& style, const Content& content, const CallSite& site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    Color container = kind == ToggleKind::Standard   ? cs.surfaceContainer
                      : kind == ToggleKind::Elevated ? cs.surfaceContainerLow
                      : kind == ToggleKind::Tonal    ? cs.secondaryContainer
                                                     : colors::Transparent;
    Color contentColor = kind == ToggleKind::Elevated ? cs.primary : kind == ToggleKind::Tonal ? cs.onSecondaryContainer : cs.onSurfaceVariant;
    Color checkedContainer = kind == ToggleKind::Tonal ? cs.secondary : kind == ToggleKind::Outlined ? cs.inverseSurface : cs.primary;
    Color checkedContent = kind == ToggleKind::Tonal ? cs.onSecondary : kind == ToggleKind::Outlined ? cs.inverseOnSurface : cs.onPrimary;
    container = checked ? style.checkedContainerColor.value_or(checkedContainer) : style.containerColor.value_or(container);
    contentColor = checked ? style.checkedContentColor.value_or(checkedContent) : style.contentColor.value_or(contentColor);
    if (!style.enabled) {
        if (kind != ToggleKind::Outlined || checked) container = cs.onSurface.copy(0.1f);
        contentColor = cs.onSurface.copy(0.38f);
    }
    container = animateColorAsState(container, 150);
    contentColor = animateColorAsState(contentColor, 150);
    MutableInteractionSource source = style.interactionSource ? *style.interactionSource : remember([] { return MutableInteractionSource(); });
    bool pressed = source.collectIsPressedAsState();
    Shape shape = morphingShape(pressed ? style.shapes.pressedShape : checked ? style.shapes.checkedShape : style.shapes.shape);

    Modifier m = modifier.minimumInteractiveComponentSize().heightIn(40).widthIn(64);
    if (kind == ToggleKind::Elevated && style.enabled) m = m.shadow(2, shape);
    m = m.background(container, shape);
    if (kind == ToggleKind::Outlined && !checked) m = m.border(1, style.enabled ? cs.outlineVariant : cs.onSurface.copy(0.12f), shape);

    CompositionLocalProvider(
        {LocalContentColor.provides(contentColor), LocalTextStyle.provides(typography().labelLarge)}, [&] {
            Row(buttonPadding(m.toggleable(checked, std::move(onCheckedChange), SemanticsRole::Checkbox,
                                           {.enabled = style.enabled, .interactionSource = source}),
                              16, 10),
                {.horizontalArrangement = Arrangement::spacedBy(8, Arrangement::Kind::Center),
                 .verticalAlignment = Alignment::CenterVertically},
                content);
        });
}

} // namespace

void MaterialTheme(const ColorScheme& scheme, const Content& content, CallSite site) {
    MaterialTheme(scheme, LocalMotionScheme.current(), content, site);
}

void MaterialTheme(const ColorScheme& scheme, const MotionScheme& motion, const Content& content, CallSite site) {
    // Scrollbars in the scheme's onSurface (as Compose Multiplatform's Material theme gives
    // them): seen on a dark theme too.
    ScrollbarStyle scrollbars = LocalScrollbarStyle.current();
    scrollbars.unhoverColor = scheme.onSurface.copy(0.12f);
    scrollbars.hoverColor = scheme.onSurface.copy(0.5f);
    CompositionLocalProvider({LocalColorScheme.provides(scheme), LocalContentColor.provides(scheme.onBackground),
                              LocalTextStyle.provides(LocalTypography.current().bodyLarge),
                              LocalTextSelectionColors.provides(TextSelectionColors{scheme.primary, scheme.primary.copy(0.4f)}),
                              LocalMotionScheme.provides(motion), LocalScrollbarStyle.provides(scrollbars)},
                             content, site);
}

void MaterialExpressiveTheme(std::optional<ColorScheme> scheme, const Content& content, CallSite site) {
    MaterialExpressiveTheme(std::move(scheme), MotionScheme::expressive(), content, site);
}

void MaterialExpressiveTheme(std::optional<ColorScheme> scheme, const MotionScheme& motion, const Content& content, CallSite site) {
    MaterialTheme(scheme.value_or(LocalColorScheme.current()), motion, content, site);
}

Shape detail::morphingShape(const Shape& target, CallSite site) {
    ComposableScope scope(site);
    Shape shape = lerp(target, RoundedCornerShape(0, 0, 0, 0), 0);  // corner by corner
    AnimationSpec spec = motionScheme().fastSpatialSpec;
    for (CornerSize& corner : shape.corners) corner = {animateFloatAsState(corner.dp, spec), animateFloatAsState(corner.percent, spec)};
    return shape;
}

void Box(const Content& content, CallSite site) { Box(Modifier{}, Alignment::TopStart, content, site); }
void LookaheadScope(const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    Box(modifier.then(std::make_shared<LookaheadScopeElement>()), content);
}

void Box(const Modifier& modifier, const Content& content, CallSite site) {
    Box(modifier, Alignment::TopStart, content, site);
}
void Box(const Modifier& modifier, Alignment contentAlignment, const Content& content, CallSite site) {
    layoutNode(site, modifier, boxPolicy(contentAlignment), content);
}

void Column(const Content& content, CallSite site) { Column(Modifier{}, ColumnStyle{}, content, site); }
void Column(const Modifier& modifier, const Content& content, CallSite site) {
    Column(modifier, ColumnStyle{}, content, site);
}
void Column(const Modifier& modifier, ColumnStyle style, const Content& content, CallSite site) {
    layoutNode(site, modifier, linearPolicy(false, style.verticalArrangement, style.horizontalAlignment.bias), content);
}

void Row(const Content& content, CallSite site) { Row(Modifier{}, RowStyle{}, content, site); }
void Row(const Modifier& modifier, const Content& content, CallSite site) { Row(modifier, RowStyle{}, content, site); }
void Row(const Modifier& modifier, RowStyle style, const Content& content, CallSite site) {
    layoutNode(site, modifier, linearPolicy(true, style.horizontalArrangement, style.verticalAlignment.bias), content);
}

LayoutMeasurePolicy detail::menuRows(float maxWidth) {
    return [maxWidth](const std::vector<Measurable>& children, const Constraints&) {
        float width = 112;
        {
            ProbeMeasure probe;
            for (const Measurable& m : children) width = std::max(width, m.measure({0, maxWidth, 0, kInfinity}).width);
        }
        float y = 0;
        for (const Measurable& m : children) {
            Placeable p = m.measure({width, width, 0, kInfinity});
            p.place(0, y);
            y += p.height;
        }
        return Size{width, y};
    };
}

void Layout(const Modifier& modifier, const Content& content, LayoutMeasurePolicy measurePolicy, CallSite site) {
    ComposableScope scope(site);
    layoutNode(
        CallSite::current(), modifier,
        [measurePolicy](LayoutNode& node, const Constraints& c) {
            std::vector<Measurable> children;
            children.reserve(node.children.size());
            for (auto* child : node.children) children.push_back({child});
            return c.constrain(measurePolicy(children, c));
        },
        content);
}

struct SubcomposeMeasureScope::Data {
    struct Slot {
        size_t id;
        Content content;
        size_t dependency;
    };
    std::vector<Slot> slots;  // composed, as the children (one container node each)
    std::vector<Slot> asked;  // by the measure under way
    MutableState<int> version{0};
};

std::vector<Measurable> SubcomposeMeasureScope::slot(size_t id, Content content, size_t dependency) {
    data_->asked.push_back({id, std::move(content), dependency});
    for (size_t i = 0; i < data_->slots.size() && i < node_->children.size(); ++i)
        if (data_->slots[i].id == id) {
            std::vector<Measurable> out;
            for (auto* child : node_->children[i]->children) out.push_back({child});
            return out;
        }
    return {};  // composed for the next pass of this frame
}

void SubcomposeLayout(const Modifier& modifier, SubcomposeMeasurePolicy measurePolicy, CallSite site) {
    ComposableScope scope(site);
    auto d = remember([] { return std::make_shared<SubcomposeMeasureScope::Data>(); });
    d->version.get();  // composed again when the slots asked for change
    auto policy = [d, measurePolicy](LayoutNode& node, const Constraints& c) {
        // The slots' containers, first: at the origin, so what the policy places in them is
        // placed in it (measured after, they would forget what was placed in them).
        for (auto* container : node.children) {
            container->measure(Constraints::fixed(c.hasBoundedWidth() ? c.maxWidth : c.minWidth, c.hasBoundedHeight() ? c.maxHeight : c.minHeight));
            container->place(0, 0);
        }
        d->asked.clear();
        SubcomposeMeasureScope scope(*d, node);
        Size size = c.constrain(measurePolicy(scope, c));
        if (!detail::isProbeMeasure()) {
            bool same = d->asked.size() == d->slots.size();
            for (size_t i = 0; same && i < d->asked.size(); ++i)
                same = d->asked[i].id == d->slots[i].id && d->asked[i].dependency == d->slots[i].dependency;
            d->slots = std::move(d->asked);  // the latest content for the slots, in any case
            d->asked.clear();
            if (!same) d->version.set(d->version.peek() + 1);  // new, gone or changed: composed again, this frame
        }
        return size;
    };
    auto container = [](LayoutNode&, const Constraints& c) { return Size{c.maxWidth, c.maxHeight}; };  // its children are the policy's to measure
    layoutNode(CallSite::current(), modifier, policy, [&] {
        for (const auto& slot : d->slots)
            key(slot.id, [&] { layoutNode(CallSite::current(), Modifier(), container, slot.content); });
    });
}

namespace {

// Lines along the main axis that wrap when the next child doesn't fit.
MeasurePolicy flowPolicy(bool horizontal, FlowStyle style) {
    return [horizontal, style](LayoutNode& node, const Constraints& c) {
        float maxMain = horizontal ? c.maxWidth : c.maxHeight;
        float mainGap = horizontal ? style.horizontalSpacing : style.verticalSpacing;
        float crossGap = horizontal ? style.verticalSpacing : style.horizontalSpacing;
        Constraints loose{0, c.maxWidth, 0, c.maxHeight};
        float main = 0, cross = 0, lineCross = 0, widest = 0;
        int inLine = 0;
        for (auto* child : node.children) {
            Size s = child->measure(loose);
            float m = horizontal ? s.width : s.height, x = horizontal ? s.height : s.width;
            bool full = style.maxItemsInEachLine > 0 && inLine >= style.maxItemsInEachLine;
            if (inLine > 0 && (full || main + mainGap + m > maxMain)) {  // next line
                cross += lineCross + crossGap;
                main = lineCross = 0;
                inLine = 0;
            }
            float at = inLine > 0 ? main + mainGap : 0;
            if (horizontal)
                child->place(at, cross);
            else
                child->place(cross, at);
            main = at + m;
            widest = std::max(widest, main);
            lineCross = std::max(lineCross, x);
            ++inLine;
        }
        float totalCross = node.children.empty() ? 0 : cross + lineCross;
        return horizontal ? Size{widest, totalCross} : Size{totalCross, widest};
    };
}

} // namespace

void FlowRow(const Modifier& modifier, const FlowStyle& style, const Content& content, CallSite site) {
    layoutNode(site, modifier, flowPolicy(true, style), content);
}
void FlowColumn(const Modifier& modifier, const FlowStyle& style, const Content& content, CallSite site) {
    layoutNode(site, modifier, flowPolicy(false, style), content);
}

void BoxWithConstraints(const Modifier& modifier, const std::function<void(const Constraints&)>& content,
                        Alignment contentAlignment, CallSite site) {
    ComposableScope scope(site);
    auto constraints = remember([] { return mutableStateOf(Constraints{}); });
    Constraints current = constraints.get();
    MeasurePolicy box = boxPolicy(contentAlignment);
    layoutNode(
        CallSite::current(), modifier,
        [box, constraints](LayoutNode& node, const Constraints& c) {
            if (!detail::isProbeMeasure()) constraints.set(c);  // a change recomposes the content before drawing
            return box(node, c);
        },
        [&] { content(current); });
}

namespace {
// The back scope of the focusable popup being composed (LayoutNode::backScope): back handlers in it carry it.
const CompositionLocal<std::shared_ptr<const void>> LocalBackScope{std::shared_ptr<const void>{}};
} // namespace

void Popup(OverlaySpec spec, const Content& content, CallSite site) {
    auto shared = std::make_shared<const OverlaySpec>(std::move(spec));
    std::shared_ptr<const void> backScope = LocalBackScope.current();
    detail::Composer::require().emit(
        detail::keyOf(site),
        [&](LayoutNode& node) {
            if (shared->focusable && !node.backScope) node.backScope = std::make_shared<char>();
            if (shared->focusable) backScope = node.backScope;
            node.modifier = {};
            node.measurePolicy = detail::stackPolicy;  // children get the popup's constraints
            node.drawContent = {};
            node.overlay = shared;
        },
        [&] { CompositionLocalProvider({LocalBackScope.provides(backScope)}, content); });
}

void detail::backHandler(ComposeHost::BackCallback callbacks, CallSite site) {
    ComposableScope scope(site);
    ComposeHost* host = &detail::Composer::require().host();
    auto& callback = remember([] { return std::make_shared<ComposeHost::BackCallback>(); });
    std::shared_ptr<const void> owner = LocalBackScope.current();
    callbacks.owner = owner;
    callbacks.owned = owner != nullptr;
    *callback = std::move(callbacks);
    DisposableEffect(0, [host, callback]() -> std::function<void()> {
        host->addBackHandler(callback);
        return [host, callback] { host->removeBackHandler(callback); };
    });
}

void BackHandler(bool enabled, std::function<void()> onBack, CallSite site) {
    detail::backHandler({.onBack = std::move(onBack), .enabled = enabled}, site);
}

void PredictiveBackHandler(bool enabled, std::function<Task(Flow<BackEventCompat>)> onBack, CallSite site) {
    ComposableScope scope(site);
    detail::Dispatcher* dispatcher = &detail::currentDispatcher();
    // One run of onBack, with the flow of a channel the gesture's steps go to (as androidx's
    // OnBackInstance): started by the gesture, or by a press.
    struct Run {
        std::function<Task(Flow<BackEventCompat>)> onBack;
        std::optional<CoroutineScope> scope;
        std::optional<Channel<BackEventCompat>> steps;
        bool predictive = false;
        void start(detail::Dispatcher& d, bool gesture) {
            cancel();
            steps.emplace(Channel<BackEventCompat>::Buffered);
            scope.emplace(d);
            predictive = gesture;
            scope->launch([onBack = onBack, flow = steps->consumeAsFlow()]() -> Task { co_await onBack(flow); });
        }
        void cancel() {  // the flow's channel goes with the collection
            if (scope) scope->cancel();
            steps.reset(), scope.reset();
            predictive = false;
        }
    };
    auto& run = remember([] { return std::make_shared<Run>(); });
    run->onBack = std::move(onBack);
    detail::backHandler({
        .onBack = [run = run, dispatcher] {
            if (!run->predictive) run->start(*dispatcher, false);  // a press: a run of its own
            run->steps->close();  // the flow ends; the coroutine goes on to its end
            run->predictive = false;
        },
        .onStarted = [run = run, dispatcher](const BackEventCompat&) { run->start(*dispatcher, true); },
        .onProgressed = [run = run](const BackEventCompat& event) {
            if (run->steps) run->steps->trySend(event);
        },
        .onCancelled = [run = run] { run->cancel(); },
        .enabled = enabled,
    });
    DisposableEffect(0, [run]() -> std::function<void()> { return [run] { run->cancel(); }; });
}

void Spacer(const Modifier& modifier, CallSite site) {
    layoutNode(site, modifier, [](LayoutNode&, const Constraints& c) { return Size{c.minWidth, c.minHeight}; }, {});
}

void ProvideTextStyle(const TextStyle& value, const Content& content, CallSite site) {
    ComposableScope scope(site);
    Font font = detail::styledFont(value, LocalTextStyle.current());
    if (value.color)
        CompositionLocalProvider({LocalTextStyle.provides(font), LocalContentColor.provides(*value.color)}, content);
    else
        CompositionLocalProvider({LocalTextStyle.provides(font)}, content);
}

namespace {
// Compose's TextStyle.Default for BasicText: 14 sp, black, nothing from around it.
void withDefaultTextStyle(const Content& content) {
    CompositionLocalProvider({LocalTextStyle.provides(Font{.size = 14}), LocalContentColor.provides(colors::Black)}, content);
}
} // namespace

void BasicText(std::string text, const Modifier& modifier, const TextStyle& style, CallSite site) {
    ComposableScope scope(site);
    withDefaultTextStyle([&] { Text(std::move(text), modifier, style); });
}
void BasicText(const AnnotatedString& text, const Modifier& modifier, const TextStyle& style, CallSite site) {
    ComposableScope scope(site);
    withDefaultTextStyle([&] { Text(text, modifier, style); });
}

void Text(std::string text, CallSite site) { Text(std::move(text), Modifier{}, TextStyle{}, site); }
void Text(std::string text, const TextStyle& style, CallSite site) { Text(std::move(text), Modifier{}, style, site); }
// Text is a restartable, skippable scope: called again with the same text, modifier and style
// it doesn't run (unless the theme values it read changed).
void Text(std::string text, const Modifier& modifier, const TextStyle& style, CallSite site) {
    composable(site, text, modifier, style, [text, modifier, style] { emitText(text, modifier, style); });
}
void Text(const AnnotatedString& text, const Modifier& modifier, const TextStyle& style, CallSite site) {
    composable(site, text, modifier, style, [text, modifier, style] { emitStyledText(text, modifier, style); });
}
void Text(const AnnotatedString& text, const Modifier& modifier, const TextStyle& style,
          const std::map<std::string, InlineTextContent>& inlineContent, CallSite site) {
    ComposableScope scope(site);  // (the contents' callbacks can't be compared: run each time)
    emitStyledText(text, modifier, style, {}, &inlineContent);
}
void Text(std::string text, const Modifier& modifier, const TextStyle& style, std::function<void(const TextLayoutResult&)> onTextLayout,
          CallSite site) {
    composable(site, text, modifier, style,
               [text, modifier, style, onTextLayout] { emitStyledText(AnnotatedString(text), modifier, style, onTextLayout); });
}
void Text(const AnnotatedString& text, const Modifier& modifier, const TextStyle& style,
          std::function<void(const TextLayoutResult&)> onTextLayout, CallSite site) {
    composable(site, text, modifier, style, [text, modifier, style, onTextLayout] { emitStyledText(text, modifier, style, onTextLayout); });
}

namespace {

// Text and AnnotatedString share one layout: styled runs, word wrap, maxLines and overflow.

// A Text inside a SelectionContainer: what the selection's gestures need, kept across
// recompositions.
struct SelectableText {
    int64_t id = 0;
    std::shared_ptr<detail::StyledTextLayout> layout;
    std::string text;
    TextAlign align = TextAlign::Start;
    float width = 0;
    TextMeasurer* measurer = nullptr;
    std::weak_ptr<LayoutNode*> node;  // where it is on screen
    bool placed() const {
        auto n = node.lock();
        return n && *n && layout;
    }
    Rect bounds() const { return (*node.lock())->contentBounds(); }  // in the window; only if placed()
    int offsetAt(Offset p) const { return detail::offsetAt(*layout, *measurer, text, p, width, align); }
    Offset caretPoint(int offset) const { return detail::caretPoint(*layout, *measurer, text, offset, width, align); }
    float lineHeightAt(int offset) const {  // of the line the caret at `offset` is on
        float y = caretPoint(offset).y, height = 0;
        for (const auto& line : layout->lines)
            if (y >= line.top + line.height - 0.5f) height = line.height;
        return height;
    }
};

// One selection in a SelectionContainer: from an offset in one of its texts to an offset in the
// same text or a later one (in reading order: top to bottom, then left to right).
struct TextSelection {
    int64_t startText = -1, endText = -1;
    int start = 0, end = 0;
    bool handles = false;  // made with a finger: the handles and the toolbar are shown
    bool empty() const { return startText < 0; }
    bool operator==(const TextSelection&) const = default;
};

struct SelectionRegistry {
    explicit SelectionRegistry(MutableState<TextSelection> selection) : selection(selection) {}
    MutableState<TextSelection> selection;
    std::vector<std::weak_ptr<SelectableText>> texts;
    int64_t nextId = 0;
    std::weak_ptr<LayoutNode*> container;
    ComposeHost* host = nullptr;

    std::shared_ptr<SelectableText> add() {
        auto text = std::make_shared<SelectableText>();
        text->id = nextId++;
        std::erase_if(texts, [](const auto& t) { return t.expired(); });
        texts.push_back(text);
        return text;
    }
    // The texts on screen, in reading order.
    std::vector<std::shared_ptr<SelectableText>> ordered() const {
        std::vector<std::shared_ptr<SelectableText>> result;
        for (const auto& weak : texts)
            if (auto t = weak.lock(); t && t->placed()) result.push_back(std::move(t));
        std::stable_sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
            Rect ra = a->bounds(), rb = b->bounds();
            return ra.y != rb.y ? ra.y < rb.y : ra.x < rb.x;
        });
        return result;
    }
    static int indexOf(const std::vector<std::shared_ptr<SelectableText>>& order, int64_t id) {
        for (size_t i = 0; i < order.size(); ++i)
            if (order[i]->id == id) return int(i);
        return -1;
    }
    // The part of `text` the selection covers, or {-1, -1}.
    std::pair<int, int> rangeOf(const SelectableText& text, const TextSelection& s) const {
        if (s.empty()) return {-1, -1};
        auto order = ordered();
        int first = indexOf(order, s.startText), last = indexOf(order, s.endText), self = indexOf(order, text.id);
        if (first < 0 || last < 0 || self < first || self > last) return {-1, -1};
        int size = int(text.text.size());
        int from = self == first ? std::min(s.start, size) : 0, to = self == last ? std::min(s.end, size) : size;
        return from < to ? std::pair{from, to} : std::pair{-1, -1};
    }
    // The text nearest a point in the window and the offset there: above a text, its start;
    // below it, its end.
    struct Anchor {
        int64_t text = -1;
        int offset = 0;
        int order = -1;
    };
    Anchor anchorAt(Offset p) const {
        auto order = ordered();
        Anchor best;
        float bestDy = 0, bestDx = 0;
        for (size_t i = 0; i < order.size(); ++i) {
            Rect b = order[i]->bounds();
            float dy = p.y < b.y ? b.y - p.y : p.y > b.y + b.height ? p.y - b.y - b.height : 0;
            float dx = p.x < b.x ? b.x - p.x : p.x > b.x + b.width ? p.x - b.x - b.width : 0;
            if (best.order >= 0 && (dy > bestDy || (dy == bestDy && dx >= bestDx))) continue;
            const SelectableText& t = *order[i];
            int offset = p.y < b.y ? 0 : p.y > b.y + b.height ? int(t.text.size()) : t.offsetAt({p.x - b.x, p.y - b.y});
            best = {t.id, offset, int(i)}, bestDy = dy, bestDx = dx;
        }
        return best;
    }
    // What clicks take around an anchor in its text: itself (one), the word (two), the paragraph (three).
    std::pair<Anchor, Anchor> unitAt(Anchor at, int clicks) const {
        if (at.order < 0 || clicks <= 1) return {at, at};
        auto order = ordered();
        const std::string& t = order[size_t(at.order)]->text;
        auto [from, to] = clicks == 2 ? detail::wordAround(t, at.offset) : detail::paragraphAround(t, at.offset);
        return {{at.text, from, at.order}, {at.text, to, at.order}};
    }
    // Where the selection starts, as an anchor (none: nothing selected).
    Anchor selectionStart() const {
        TextSelection s = selection.peek();
        if (s.empty()) return {};
        return {s.startText, s.start, indexOf(ordered(), s.startText)};
    }
    static bool before(const Anchor& a, const Anchor& b) { return std::pair{a.order, a.offset} < std::pair{b.order, b.offset}; }
    // The selection between two anchors, whichever comes first.
    void select(Anchor a, Anchor b, bool handles) {
        if (a.order < 0 || b.order < 0) return;
        if (std::pair{b.order, b.offset} < std::pair{a.order, a.offset}) std::swap(a, b);
        // An end at the very start of a text is the end of the one before, and a start at the
        // very end of one the start of the next: the handles stay by the selected text.
        auto order = ordered();
        if (b.order > a.order && b.offset == 0) {
            const SelectableText& t = *order[size_t(b.order - 1)];
            b = {t.id, int(t.text.size()), b.order - 1};
        }
        if (a.order < b.order && a.offset >= int(order[size_t(a.order)]->text.size()))
            a = {order[size_t(a.order + 1)]->id, 0, a.order + 1};
        selection.set({a.text, b.text, a.offset, b.offset, handles});
    }
    bool selectWordAt(Offset p, bool handles) {
        Anchor at = anchorAt(p);
        auto order = ordered();
        if (at.order < 0) return false;
        auto [from, to] = detail::wordAround(order[size_t(at.order)]->text, at.offset);
        if (from == to) return false;  // not on a word
        select({at.text, from, at.order}, {at.text, to, at.order}, handles);
        return true;
    }
    void selectAll(bool handles) {
        auto order = ordered();
        if (order.empty()) return;
        selection.set({order.front()->id, order.back()->id, 0, int(order.back()->text.size()), handles});
    }
    // The selected parts of the texts, a line each.
    std::string selectedText() const {
        TextSelection s = selection.peek();
        auto order = ordered();
        int first = indexOf(order, s.startText), last = indexOf(order, s.endText);
        std::string result;
        for (int i = first; first >= 0 && i <= last; ++i) {
            const std::string& t = order[size_t(i)]->text;
            int size = int(t.size());
            int from = i == first ? std::min(s.start, size) : 0, to = i == last ? std::min(s.end, size) : size;
            if (i > first) result += '\n';
            if (from < to) result += t.substr(size_t(from), size_t(to - from));
        }
        return result;
    }
    void clear() {
        if (!selection.peek().empty()) selection.set({});
    }
    // From the container's input handler's coordinates to the window's.
    Offset toWindow(Offset local) const {
        auto n = container.lock();
        if (!n || !*n) return local;
        Rect b = (*n)->layerBounds((*n)->modifier.elements().size() - 1);
        return {b.x + local.x, b.y + local.y};
    }
};
const CompositionLocal<std::shared_ptr<SelectionRegistry>> LocalSelectionRegistry{nullptr};

// Shows the text magnifier while a handle is dragged; gone when the drag ends, however it ends
// (the handle may go first).
struct TextMagnifierShown {
    ComposeHost* host;
    MutableState<bool> dragging;  // the selection toolbar is hidden meanwhile, as Android's
    TextMagnifierShown(ComposeHost* host, MutableState<bool> dragging) : host(host), dragging(dragging) { dragging.set(true); }
    TextMagnifierShown(const TextMagnifierShown&) = delete;
    ~TextMagnifierShown() {
        host->hideTextMagnifier();
        dragging.set(false);
    }
};

constexpr float kHandleSize = 22;

// A selection handle: a teardrop hanging under one end of a selection, its point up at that end
// (the start's on its right). `position` places it (as a Popup's); on a press `grab` says where
// the finger holds it from the middle of that end's line, and while it is dragged `drag` moves
// that end to the text there. Both take places in the handle's own coordinates. (Only the
// handle hears its touches: a touchable popup's.)
struct SelectionHandleDrag {
    std::function<Offset(const Rect& anchor)> position;
    std::function<Offset(Offset pressed)> grab;
    std::function<void(Offset at)> drag;
};
void selectionHandle(bool start, Color color, ComposeHost* host, MutableState<bool> dragging, const SelectionHandleDrag& handle) {
    Popup({.position = [position = handle.position](const Rect& anchor, Size, Size) { return position(anchor); },
           .focusable = false,
           .touchable = true},
          [&] {
              Box(Modifier()
                      .size(kHandleSize)
                      .drawBehind([start, color](DrawScope& d) {
                          d.drawCircle(color, kHandleSize / 2, Offset{kHandleSize / 2, kHandleSize / 2});
                          d.drawRect(color, {start ? kHandleSize / 2 : 0, 0}, Size{kHandleSize / 2, kHandleSize / 2});  // the point
                      })
                      .pointerInput(start, [host, dragging, grab = handle.grab, drag = handle.drag](PointerInputScope& s) -> Task {
                          while (true) {
                              PointerEvent down = co_await s.awaitPointerEvent();
                              if (down.type != PointerEventType::Press) continue;
                              Offset grip = grab(down.change.position);
                              TextMagnifierShown shown{host, dragging};
                              while (true) {
                                  PointerEvent e = co_await s.awaitPointerEvent();
                                  if (e.type == PointerEventType::Release || e.type == PointerEventType::Cancel) break;
                                  if (e.type != PointerEventType::Move) continue;
                                  drag({e.change.position.x + grip.x, e.change.position.y + grip.y});
                              }
                          }
                      }));
          });
}

// A finger's selection: a handle at each end, dragged from text to text, and a toolbar.
void textSelectionPopups(std::shared_ptr<SelectionRegistry> registry) {
    TextSelection s = registry->selection.get();
    if (s.empty() || !s.handles) return;
    const ColorScheme& cs = colorScheme();
    Color handleColor = LocalTextSelectionColors.current().handleColor;
    ComposeHost* host = &detail::Composer::require().host();
    auto dragging = remember([] { return mutableStateOf(false); });  // a handle: no toolbar meanwhile
    auto origin = remember([] { return std::make_shared<std::array<Offset, 2>>(); });  // where the handles were put
    // The caret at one end of the selection, in the window, and its text.
    auto caret = [registry](bool start) -> std::optional<std::pair<Offset, std::shared_ptr<SelectableText>>> {
        TextSelection s = registry->selection.peek();
        auto order = registry->ordered();
        int i = SelectionRegistry::indexOf(order, start ? s.startText : s.endText);
        if (i < 0) return std::nullopt;
        auto text = order[size_t(i)];
        Offset c = text->caretPoint(std::min(start ? s.start : s.end, int(text->text.size())));
        Rect b = text->bounds();
        return std::pair{Offset{b.x + c.x, b.y + c.y}, text};
    };
    auto handle = [&](bool start) {
        selectionHandle(start, handleColor, host, dragging, {
            .position = [caret, origin, start](const Rect&) {
                auto c = caret(start);  // none: its text is gone
                (*origin)[start ? 0 : 1] = c ? Offset{c->first.x - (start ? kHandleSize : 0), c->first.y} : Offset{-10000, -10000};
                return (*origin)[start ? 0 : 1];
            },
            .grab = [registry, caret, origin, start](Offset pressed) {
                auto c = caret(start);
                if (!c) return Offset{};  // (its text is gone: the handle is out of reach)
                const Offset& at = (*origin)[start ? 0 : 1];
                TextSelection s = registry->selection.peek();
                float lineHeight = c->second->lineHeightAt(start ? s.start : s.end);
                return Offset{c->first.x - (at.x + pressed.x), c->first.y - lineHeight / 2 - (at.y + pressed.y)};
            },
            .drag = [registry, caret, origin, start, host](Offset to) {
                const Offset& at = (*origin)[start ? 0 : 1];
                auto moved = registry->anchorAt({at.x + to.x, at.y + to.y});
                TextSelection s = registry->selection.peek();
                auto order = registry->ordered();
                SelectionRegistry::Anchor other{start ? s.endText : s.startText, start ? s.end : s.start,
                                                SelectionRegistry::indexOf(order, start ? s.endText : s.startText)};
                if (moved.order < 0 || other.order < 0) return;
                // A handle stops at the other one.
                auto key = [](const SelectionRegistry::Anchor& a) { return std::pair{a.order, a.offset}; };
                if (start ? key(other) < key(moved) : key(moved) < key(other)) moved = other;
                registry->select(start ? moved : other, start ? other : moved, true);
                // The magnifier: on the moved end's line, the finger's way across.
                if (auto now = caret(start)) {
                    TextSelection after = registry->selection.peek();
                    float h = now->second->lineHeightAt(start ? after.start : after.end);
                    host->showTextMagnifier({at.x + to.x, now->first.y - h / 2});
                }
            }});
    };
    key(0, [&] { handle(true); });  // the same composables twice: keyed apart
    key(1, [&] { handle(false); });
    if (dragging.get()) return;
    if (auto custom = LocalTextToolbar.current()) {  // the app's own: copy and select all
        detail::customTextToolbar(
            custom,
            [caret](const Rect&) {
                auto from = caret(true), to = caret(false);
                if (!from || !to) return Rect{};
                float lineHeight = from->second->layout->lines.empty() ? 20 : from->second->layout->lines.front().height;
                float left = std::min(from->first.x, to->first.x), right = std::max(from->first.x, to->first.x);
                return Rect{left, from->first.y - lineHeight, right - left, to->first.y - from->first.y + lineHeight};
            },
            [registry] {
                registry->host->setClipboardText(registry->selectedText());
                registry->clear();
            },
            nullptr, nullptr, [registry] { registry->selectAll(true); });
        return;
    }
    Popup({.position = [caret](const Rect&, Size size, Size viewport) {
               auto from = caret(true), to = caret(false);
               if (!from || !to) return Offset{-10000, -10000};
               float lineHeight = from->second->layout->lines.empty() ? 20 : from->second->layout->lines.front().height;
               bool oneLine = std::abs(from->first.y - to->first.y) < 1;
               float x = (oneLine ? (from->first.x + to->first.x) / 2 : viewport.width / 2) - size.width / 2;
               float y = from->first.y - lineHeight - 8 - size.height;
               if (y < 0) y = to->first.y + 30;
               return Offset{std::clamp(x, 8.f, std::max(8.f, viewport.width - size.width - 8)), y};
           },
           .focusable = false,
           .touchable = true},
          [&] {
              Row(Modifier().shadow(3, 8).background(cs.surfaceContainer, 8), [&] {
                  TextButton([registry] {
                      registry->host->setClipboardText(registry->selectedText());
                      registry->clear();
                  }, [] { Text(LocalStrings.current().copy); });
                  TextButton([registry] { registry->selectAll(true); }, [] { Text(LocalStrings.current().selectAll); });
              });
          });
}

void emitStyledText(const AnnotatedString& text, const Modifier& modifier, const TextStyle& given,
                    std::function<void(const TextLayoutResult&)> onTextLayout, const std::map<std::string, InlineTextContent>* inlineContent) {
    TextStyle style = given;  // text with no direction of its own (digits) goes the layout's way
    if (style.textDirection == TextDirection::Content)
        style.textDirection = LocalLayoutDirection.current() == LayoutDirection::Rtl ? TextDirection::ContentOrRtl : TextDirection::ContentOrLtr;
    Font font = detail::styledFont(style, LocalTextStyle.current());
    Color color = style.color.value_or(LocalContentColor.current());
    // Links: the one focused, the one the mouse is over and the one pressed get their styles
    // (TextLinkStyles). Each has a node of its own over it (see below) to be focused and clicked
    // from the keyboard and by accessibility services: its interaction source says how it is.
    std::optional<MutableState<int>> hoveredLink, pressedLink;
    std::shared_ptr<std::vector<MutableInteractionSource>> linkSources;
    AnnotatedString shown = text;
    if (!text.links().empty()) {
        hoveredLink = remember([] { return mutableStateOf(-1); });
        pressedLink = remember([] { return mutableStateOf(-1); });
        linkSources = remember([] { return std::make_shared<std::vector<MutableInteractionSource>>(); });
        while (linkSources->size() < text.links().size()) linkSources->push_back(MutableInteractionSource());
        for (int i = 0; i < int(text.links().size()); ++i) {
            const auto& link = text.links()[size_t(i)];
            const MutableInteractionSource& source = (*linkSources)[size_t(i)];
            bool focused = source.collectIsFocusedAsState();
            bool hovered = hoveredLink->get() == i || source.collectIsHoveredAsState();
            bool pressed = pressedLink->get() == i || source.collectIsPressedAsState();
            if (!link.annotation || !link.annotation->styles) continue;
            const TextLinkStyles& s = *link.annotation->styles;
            if (focused && s.focusedStyle) shown.addStyle(*s.focusedStyle, link.start, link.end);
            if (hovered && s.hoveredStyle) shown.addStyle(*s.hoveredStyle, link.start, link.end);
            if (pressed && s.pressedStyle) shown.addStyle(*s.pressedStyle, link.start, link.end);
        }
    }
    // Inline content: each appendInlineContent the map has, a room of its placeholder's size.
    std::vector<detail::PlaceholderRange> placeholders;
    std::vector<std::pair<const InlineTextContent*, std::string>> inlines;  // and what goes there, told its alternate text
    if (inlineContent)
        for (const auto& a : text.annotations()) {
            if (a.tag != kInlineContentTag) continue;
            auto it = inlineContent->find(a.item);
            if (it == inlineContent->end()) continue;
            placeholders.push_back({a.start, a.end, it->second.placeholder});
            inlines.push_back({&it->second, text.text().substr(size_t(a.start), size_t(a.end - a.start))});
        }
    auto runs = detail::resolveRuns(shown, font, color, style.textDecoration, colorScheme().primary, style.brush, style.shadow, placeholders);
    auto layout = std::make_shared<detail::StyledTextLayout>();
    // onTextLayout: told each time the layout comes out different (what it last told).
    std::shared_ptr<std::optional<std::tuple<float, float, float, size_t, bool, bool>>> told;
    if (onTextLayout) told = remember([] { return std::make_shared<std::optional<std::tuple<float, float, float, size_t, bool, bool>>>(); });
    std::vector<std::pair<int, int>> links;  // each link's bytes
    for (const auto& l : text.links()) links.push_back({l.start, l.end});
    auto measure = [string = text.text(), paragraphs = text.paragraphStyles(), runs, style, layout, base = font.size, onTextLayout, told, font,
                    links, inlineCount = inlines.size()](LayoutNode& node, const Constraints& c) {
        auto lay = [&](float scale) {
            std::vector<detail::TextRun> sized = runs;
            if (scale != 1)
                for (auto& run : sized) run.font.size *= scale;  // its own sizes (spans) in proportion
            // A min intrinsic width: as narrow as it gets, a word a line (IntrinsicSize::Min).
            bool narrowest = detail::isMinIntrinsicWidth() && style.softWrap;
            *layout = detail::layoutStyledText(node.textMeasurer(), string, std::move(sized), narrowest ? 0.f : c.maxWidth,
                                               narrowest ? std::numeric_limits<int>::max() : style.maxLines, style.softWrap,
                                               style.overflow, style.textDirection, style.textIndent, style.textAlign == TextAlign::Justify,
                                               style.lineBreak.strategy, &paragraphs);
        };
        if (const auto& a = style.autoSize; a && base > 0 && a->maxFontSize >= a->minFontSize) {
            // The largest step that fits: overflowing neither way, nothing cut off.
            auto fits = [&] {
                return !layout->clipped && layout->width <= c.maxWidth + 0.5f && layout->height <= c.maxHeight + 0.5f;
            };
            float step = std::max(0.01f, a->stepSize);
            int low = 0, high = int(std::floor((a->maxFontSize - a->minFontSize) / step));
            while (low < high) {  // the last step that fits (the smallest one if none does)
                int mid = (low + high + 1) / 2;
                lay((a->minFontSize + float(mid) * step) / base);
                if (fits()) low = mid;
                else high = mid - 1;
            }
            lay((a->minFontSize + float(low) * step) / base);
        } else {
            lay(1);
        }
        if (int lines = int(layout->lines.size()); style.minLines > lines) {  // empty lines below, as tall as the style's
            TextMeasurer& m = node.textMeasurer();
            float line = font.lineHeight > 0 ? font.lineHeight * m.fontScale() : m.metrics(font).lineHeight;
            layout->height += float(style.minLines - lines) * line;
        }
        if (onTextLayout && !detail::isProbeMeasure()) {
            float width = std::max(layout->width, c.minWidth);
            std::tuple key{width, layout->height, c.maxHeight, layout->lines.size(), layout->truncated, layout->tooWide};
            if (*told != key) {
                *told = key;
                onTextLayout(detail::makeTextLayoutResult(*layout, string, node.textMeasurer(), style.textAlign, style.overflow, width,
                                                          c.maxWidth, c.maxHeight));
            }
        }
        // As wide as its lines, unless they had to wrap to the width: then all of it, as Compose's
        // min(maxIntrinsicWidth, maxWidth) — a background or a centered alignment spans the width,
        // not the longest line it came out with. (A line too long unwrapped is cut to it anyway.)
        float width = layout->wrapped && c.hasBoundedWidth() ? c.maxWidth : layout->width;
        Size size{std::ceil(width), std::ceil(layout->height)};
        bool lines = !layout->lines.empty();  // its baselines, for alignBy and paddingFromBaseline
        node.textBaselines[0] = lines ? layout->lines.front().top + layout->lines.front().ascent : std::numeric_limits<float>::quiet_NaN();
        node.textBaselines[1] = lines ? layout->lines.back().top + layout->lines.back().ascent : std::numeric_limits<float>::quiet_NaN();
        // Each link's node over its first line (one left out of what shows isn't placed).
        if (!node.children.empty()) {
            float width = c.constrain(size).width;
            for (size_t i = 0; i < node.children.size() && i < links.size(); ++i) {
                std::vector<Rect> rects = detail::selectionRects(*layout, node.textMeasurer(), string, links[i].first, links[i].second, width,
                                                                 style.textAlign);
                if (rects.empty() || rects[0].width <= 0) continue;
                node.children[i]->measure(Constraints::fixed(rects[0].width, rects[0].height));
                node.children[i]->place(rects[0].x, rects[0].y);
            }
            // Then each inline content in its room (one past maxLines isn't placed).
            std::vector<Rect> rooms = detail::placeholderRects(*layout, node.textMeasurer(), width, style.textAlign);
            for (size_t k = 0; k < inlineCount && links.size() + k < node.children.size(); ++k) {
                if (k >= rooms.size() || rooms[k].height <= 0) continue;
                LayoutNode* child = node.children[links.size() + k];
                child->measure(Constraints::fixed(rooms[k].width, rooms[k].height));
                child->place(rooms[k].x, rooms[k].y);
            }
        }
        return size;
    };
    // In a SelectionContainer: the part of the selection in this text, drawn highlighted.
    auto registry = LocalSelectionRegistry.current();
    std::pair<int, int> range{-1, -1};
    std::shared_ptr<SelectableText> shared;
    if (registry) {
        shared = remember([registry] { return registry->add(); });
        shared->layout = layout;
        shared->text = text.text();
        shared->align = style.textAlign;
        shared->measurer = &detail::Composer::require().host().textMeasurer();
        range = registry->rangeOf(*shared, registry->selection.get());
    }
    Color highlight = LocalTextSelectionColors.current().backgroundColor;
    auto drawnWidth = std::make_shared<float>(0.f);  // what the lines are aligned in (finding the link under the mouse)
    auto draw = [layout, align = style.textAlign, overflow = style.overflow, shared, range, highlight, drawnWidth](
                    GraphicsContext& canvas, const Rect& b, const LayoutNode& node) {
        *drawnWidth = b.width;
        if (shared && range.first >= 0) {
            shared->width = b.width;
            for (const Rect& r : detail::selectionRects(*layout, node.textMeasurer(), shared->text, range.first, range.second, b.width, align))
                canvas.fillRoundRect({b.x + r.x, b.y + r.y, r.width, r.height}, 0, highlight);
        }
        detail::drawStyledText(canvas, *layout, b, align, overflow);
    };
    Modifier m = modifier;
    if (registry) {
        // A finger's long press selects the word under it (the mouse drags or double clicks).
        auto trigger = std::make_shared<TooltipTriggerElement>();
        trigger->onLongPressAt = [shared, registry](Offset p) {
            if (registry->host->pointerType() != ComposeHost::PointerType::Touch || !shared->placed()) return;
            Rect b = shared->bounds();
            registry->selectWordAt({b.x + p.x, b.y + p.y}, true);
            registry->host->performHapticFeedback(HapticFeedbackType::LongPress);
        };
        m = m.then(trigger);
    }
    std::function<void(int)> activate;
    bool overLink = false;
    if (!text.links().empty()) {
        // Taps on a link run call its onClick, its LinkAnnotation's listener or open its URL (the
        // latest ones: the input coroutine keeps running). The mouse over one shows a hand.
        auto& latest = remember([] { return std::make_shared<std::vector<AnnotatedString::Link>>(); });
        *latest = text.links();
        ComposeHost* host = &detail::Composer::require().host();
        auto linkAt = [layout, align = style.textAlign](Offset p, float width) {
            int run = detail::runAt(*layout, p, width, align);
            return run < 0 ? -1 : layout->runs[size_t(run)].link;
        };
        // A link's onClick, its LinkAnnotation's listener or its URL opened.
        activate = [latest, host](int link) {
            if (link < 0 || size_t(link) >= latest->size()) return;
            const auto& l = (*latest)[size_t(link)];
            if (l.onClick) return l.onClick();
            if (!l.annotation) return;
            if (l.annotation->linkInteractionListener) return l.annotation->linkInteractionListener(*l.annotation);
            if (l.annotation->kind == LinkAnnotation::Kind::Url) host->openUri(l.annotation->url);
        };
        MutableState<int> hovered = *hoveredLink, pressed = *pressedLink;
        m = m.pointerInput(0, [activate, linkAt, pressed](PointerInputScope& scope) -> Task {
            co_await detectTapGestures(scope, {.onPress = [linkAt, pressed, &scope](Offset p) { pressed.set(linkAt(p, scope.size().width)); },
                                               .onTap = [activate, linkAt, pressed, &scope](Offset p) {
                                                   pressed.set(-1);
                                                   activate(linkAt(p, scope.size().width));
                                               },
                                               .onCancel = [pressed] { pressed.set(-1); }});
        });
        auto hover = [linkAt, hovered, drawnWidth](PointerEvent& e) { hovered.set(linkAt(e.change.position, *drawnWidth)); };
        m = m.onPointerEvent(PointerEventType::Move, hover)
                .onPointerEvent(PointerEventType::Enter, hover)
                .onPointerEvent(PointerEventType::Exit, [hovered](PointerEvent&) { hovered.set(-1); });
        overLink = hovered.get() >= 0;
    }
    // A link under the mouse shows a hand; selectable text the text cursor (Compose's
    // SelectionController: textPointerIcon).
    if (overLink) m = m.pointerHoverIcon(Cursor::Pointer);
    else if (registry) m = m.pointerHoverIcon(Cursor::Text);
    MeasurePolicy policy = measure;
    if (shared)  // offsets under a press depend on the width (alignment)
        policy = [measure, shared](LayoutNode& node, const Constraints& c) {
            Size size = measure(node, c);
            if (!detail::isProbeMeasure()) shared->width = std::max(size.width, c.minWidth);
            shared->node = node.handle();
            return size;
        };
    // Each link's node, placed over its first line by the measure: focusable from the keyboard
    // (Enter opens it) and clicked by accessibility services; a tap on it does what a tap on the
    // text there would.
    Content linkNodes;
    if (!text.links().empty())
        linkNodes = [&] {
            for (size_t i = 0; i < text.links().size(); ++i) {
                const auto& link = text.links()[i];
                std::string label = text.text().substr(size_t(link.start), size_t(link.end - link.start));
                Box(Modifier()
                        .clickable([activate, i] { activate(int(i)); }, {.interactionSource = (*linkSources)[i], .indication = false})
                        .semantics({.contentDescription = label})
                        .pointerHoverIcon(Cursor::Pointer));
            }
        };
    // Then the inline contents, a node each (their own Box), placed in their rooms by the measure.
    Content children = linkNodes;
    if (!inlines.empty())
        children = [&] {
            if (linkNodes) linkNodes();
            for (const auto& [content, alternate] : inlines)
                Box(Modifier(), [&, content = content, alternate = alternate] {
                    if (content->children) content->children(alternate);
                });
        };
    layoutNode(CallSite::current(), m, policy, children, draw, text.text());
}

void emitText(const std::string& text, const Modifier& modifier, const TextStyle& style) {
    emitStyledText(AnnotatedString(text), modifier, style);
}

} // namespace

void detail::customTextToolbar(const std::shared_ptr<TextToolbar>& toolbar, std::function<Rect(const Rect& anchor)> selection,
                               std::function<void()> onCopy, std::function<void()> onPaste, std::function<void()> onCut,
                               std::function<void()> onSelectAll, CallSite site) {
    ComposableScope scope(site);
    // What it was told last: the rect and which actions it had.
    struct Told {
        std::optional<Rect> rect;
        std::array<bool, 4> actions{};
    };
    auto told = remember([] { return std::make_shared<Told>(); });
    std::array<bool, 4> actions{bool(onCopy), bool(onPaste), bool(onCut), bool(onSelectAll)};
    if (told->actions != actions) told->rect.reset();  // new actions: told again at the next layout
    told->actions = actions;
    // Told from the layout (where the selection is), by a popup that takes no room of its own.
    Popup({.position = [toolbar, selection, told, onCopy, onPaste, onCut, onSelectAll](const Rect& anchor, Size, Size) {
               Rect r = selection(anchor);
               if (!told->rect || told->rect->x != r.x || told->rect->y != r.y || told->rect->width != r.width || told->rect->height != r.height) {
                   told->rect = r;
                   toolbar->showMenu(r, onCopy, onPaste, onCut, onSelectAll);
               }
               return Offset{-10000, -10000};
           },
           .focusable = false,
           .touchable = false},
          [] {});
    // Hidden once it isn't composed any more (the selection ended, the field lost the focus).
    DisposableEffect(toolbar, [toolbar, told]() -> std::function<void()> {
        return [toolbar, told] {
            told->rect.reset();
            toolbar->hide();
        };
    });
}

void SelectionContainer(const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    auto registry = remember([] { return std::make_shared<SelectionRegistry>(mutableStateOf(TextSelection{})); });
    registry->host = &detail::Composer::require().host();
    auto focus = remember([] { return FocusRequester(); });
    // A press anywhere in it clears the selection (the handles and the toolbar take their own);
    // the mouse drags a new one, or double clicks a word, and Ctrl+C copies it.
    Modifier m = modifier.focusRequester(focus)
                     .focusable()
                     .onKeyEvent([registry](const KeyEvent& e) {
                         if (e.type != KeyEventType::KeyDown) return false;
                         if (e.key == Key::Escape && !registry->selection.peek().empty()) {
                             registry->clear();
                             return true;
                         }
                         if (!e.modifiers.ctrl) return false;
                         if (e.key == Key::A) registry->selectAll(false);
                         else if (e.key == Key::C && !registry->selection.peek().empty())
                             registry->host->setClipboardText(registry->selectedText());
                         else
                             return false;
                         return true;
                     })
                     .pointerInput(0, [registry, focus](PointerInputScope& s) -> Task {
                         // The mouse, as Compose's: one click and a drag select from there, two
                         // take words and three paragraphs (dragging by them), Shift extends.
                         // Clicks count press to press, within the double tap timeout.
                         using Anchor = SelectionRegistry::Anchor;
                         int64_t lastPress = -1;
                         Offset lastAt;
                         int clicks = 0;
                         Anchor caret;  // where the last click was: a selection of nothing to extend from
                         while (true) {
                             PointerEvent down = co_await s.awaitPointerEvent();
                             if (down.type != PointerEventType::Press) continue;
                             bool mouse = registry->host->pointerType() == ComposeHost::PointerType::Mouse;
                             Anchor extendFrom = !mouse || !down.keyboardModifiers.shift ? Anchor{}
                                                 : registry->selectionStart().order >= 0 ? registry->selectionStart()
                                                                                         : caret;
                             registry->clear();
                             if (!mouse) continue;
                             focus.requestFocus();
                             Offset from = registry->toWindow(down.change.position);
                             int64_t timeout = std::chrono::duration_cast<std::chrono::nanoseconds>(s.viewConfiguration().doubleTapTimeout).count();
                             bool again = lastPress >= 0 && s.nowNanos() - lastPress < timeout &&
                                          std::hypot(from.x - lastAt.x, from.y - lastAt.y) < s.viewConfiguration().touchSlop;
                             clicks = again ? clicks + 1 : 1;
                             lastPress = s.nowNanos(), lastAt = from;
                             int unit = extendFrom.order >= 0 ? 1 : std::min(clicks, 3);
                             auto [anchorStart, anchorEnd] = extendFrom.order >= 0 ? std::pair{extendFrom, extendFrom} : registry->unitAt(registry->anchorAt(from), unit);
                             if (extendFrom.order < 0) caret = anchorStart;
                             // Extended by what the pointer reaches: before the anchor, from its end back.
                             auto extendTo = [&](Offset p) {
                                 auto [a, b] = registry->unitAt(registry->anchorAt(p), unit);
                                 if (SelectionRegistry::before(a, anchorStart)) registry->select(anchorEnd, a, false);
                                 else registry->select(anchorStart, b, false);
                             };
                             if (extendFrom.order >= 0) extendTo(from);
                             else if (unit > 1) registry->select(anchorStart, anchorEnd, false);
                             bool dragging = false;
                             while (true) {
                                 PointerEvent e = co_await s.awaitPointerEvent();
                                 if (e.type == PointerEventType::Release || e.type == PointerEventType::Cancel) break;
                                 if (e.type != PointerEventType::Move) continue;
                                 Offset to = registry->toWindow(e.change.position);
                                 if (!dragging && std::hypot(to.x - from.x, to.y - from.y) <= s.viewConfiguration().touchSlop) continue;
                                 dragging = true;
                                 e.change.consume();  // selecting, not scrolling
                                 extendTo(to);
                             }
                         }
                     });
    MeasurePolicy box = boxPolicy(Alignment::TopStart);
    layoutNode(CallSite::current(), m,
               [box, registry](LayoutNode& node, const Constraints& c) {
                   registry->container = node.handle();
                   return box(node, c);
               },
               [&] {
                   CompositionLocalProvider({LocalSelectionRegistry.provides(registry)}, content);
                   // Their own labels aren't selectable.
                   CompositionLocalProvider({LocalSelectionRegistry.provides(nullptr)},
                                            [&] { composable(CallSite::current(), registry, [registry] { textSelectionPopups(registry); }); });
               });
}

void DisableSelection(const Content& content, CallSite site) {
    ComposableScope scope(site);
    CompositionLocalProvider({LocalSelectionRegistry.provides(nullptr)}, content);
}

void Button(std::function<void()> onClick, const Content& content, CallSite site) {
    buttonImpl(ButtonKind::Filled, std::move(onClick), Modifier{}, ButtonStyle{}, content, site);
}
void Button(std::function<void()> onClick, const Modifier& modifier, const Content& content, CallSite site) {
    buttonImpl(ButtonKind::Filled, std::move(onClick), modifier, ButtonStyle{}, content, site);
}
void Button(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style, const Content& content,
            CallSite site) {
    buttonImpl(ButtonKind::Filled, std::move(onClick), modifier, style, content, site);
}
void OutlinedButton(std::function<void()> onClick, const Content& content, CallSite site) {
    buttonImpl(ButtonKind::Outlined, std::move(onClick), Modifier{}, ButtonStyle{}, content, site);
}
void OutlinedButton(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style,
                    const Content& content, CallSite site) {
    buttonImpl(ButtonKind::Outlined, std::move(onClick), modifier, style, content, site);
}
void FilledTonalButton(std::function<void()> onClick, const Content& content, CallSite site) {
    buttonImpl(ButtonKind::Tonal, std::move(onClick), Modifier{}, ButtonStyle{}, content, site);
}
void FilledTonalButton(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style,
                       const Content& content, CallSite site) {
    buttonImpl(ButtonKind::Tonal, std::move(onClick), modifier, style, content, site);
}
void ElevatedButton(std::function<void()> onClick, const Content& content, CallSite site) {
    buttonImpl(ButtonKind::Elevated, std::move(onClick), Modifier{}, ButtonStyle{}, content, site);
}
void ElevatedButton(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style,
                    const Content& content, CallSite site) {
    buttonImpl(ButtonKind::Elevated, std::move(onClick), modifier, style, content, site);
}
void TextButton(std::function<void()> onClick, const Content& content, CallSite site) {
    buttonImpl(ButtonKind::Text, std::move(onClick), Modifier{}, ButtonStyle{}, content, site);
}
void TextButton(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style,
                const Content& content, CallSite site) {
    buttonImpl(ButtonKind::Text, std::move(onClick), modifier, style, content, site);
}

void ToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content, CallSite site) {
    toggleButtonImpl(ToggleKind::Standard, checked, std::move(onCheckedChange), Modifier{}, {}, content, site);
}
void ToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier, const ToggleButtonStyle& style,
                  const Content& content, CallSite site) {
    toggleButtonImpl(ToggleKind::Standard, checked, std::move(onCheckedChange), modifier, style, content, site);
}
void ElevatedToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content, CallSite site) {
    toggleButtonImpl(ToggleKind::Elevated, checked, std::move(onCheckedChange), Modifier{}, {}, content, site);
}
void ElevatedToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                          const ToggleButtonStyle& style, const Content& content, CallSite site) {
    toggleButtonImpl(ToggleKind::Elevated, checked, std::move(onCheckedChange), modifier, style, content, site);
}
void TonalToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content, CallSite site) {
    toggleButtonImpl(ToggleKind::Tonal, checked, std::move(onCheckedChange), Modifier{}, {}, content, site);
}
void TonalToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                       const ToggleButtonStyle& style, const Content& content, CallSite site) {
    toggleButtonImpl(ToggleKind::Tonal, checked, std::move(onCheckedChange), modifier, style, content, site);
}
void OutlinedToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content, CallSite site) {
    toggleButtonImpl(ToggleKind::Outlined, checked, std::move(onCheckedChange), Modifier{}, {}, content, site);
}
void OutlinedToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                          const ToggleButtonStyle& style, const Content& content, CallSite site) {
    toggleButtonImpl(ToggleKind::Outlined, checked, std::move(onCheckedChange), modifier, style, content, site);
}

void SplitButtonLayout(const Content& leadingButton, const Content& trailingButton, const Modifier& modifier, CallSite site) {
    ComposableScope scope(site);
    Row(modifier, {.horizontalArrangement = Arrangement::spacedBy(SplitButtonDefaults::Spacing), .verticalAlignment = Alignment::CenterVertically},
        [&] {
            leadingButton();
            trailingButton();
        });
}

namespace SplitButtonDefaults {

namespace {

ButtonKind buttonKind(SplitButtonKind kind) {
    return kind == SplitButtonKind::Tonal      ? ButtonKind::Tonal
           : kind == SplitButtonKind::Elevated ? ButtonKind::Elevated
           : kind == SplitButtonKind::Outlined ? ButtonKind::Outlined
                                               : ButtonKind::Filled;
}

} // namespace

void LeadingButton(std::function<void()> onClick, const Content& content, CallSite site) {
    LeadingButton(std::move(onClick), Modifier(), {}, content, site);
}
void LeadingButton(std::function<void()> onClick, const Modifier& modifier, const SplitButtonStyle& style, const Content& content,
                   CallSite site) {
    buttonImpl(buttonKind(style.kind), std::move(onClick), modifier,
               {.enabled = style.enabled, .interactionSource = style.interactionSource, .shapes = leadingButtonShapes()}, content, site);
}
void TrailingButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content, CallSite site) {
    TrailingButton(checked, std::move(onCheckedChange), Modifier(), {}, content, site);
}
void TrailingButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier, const SplitButtonStyle& style,
                    const Content& content, CallSite site) {
    // The leading button's colors either way; checked, it turns round.
    ToggleButtonShapes shapes = trailingButtonShapes();
    ButtonShapes now = checked ? ButtonShapes{shapes.checkedShape, shapes.checkedShape} : ButtonShapes{shapes.shape, shapes.pressedShape};
    buttonImpl(buttonKind(style.kind), [onCheckedChange, checked] { if (onCheckedChange) onCheckedChange(!checked); },
               modifier.width(TrailingButtonWidth).semantics({.checked = checked}),
               {.enabled = style.enabled, .interactionSource = style.interactionSource, .shapes = now}, content, site);
}
void TrailingButtonIcon(bool checked, CallSite site) {
    ComposableScope scope(site);
    float turn = animateFloatAsState(checked ? 180.f : 0.f, motionScheme().fastSpatialSpec);
    Icon(Icons::Filled::KeyboardArrowDown, checked ? LocalStrings.current().closeMenu : LocalStrings.current().openMenu,
         Modifier().size(18).graphicsLayer([turn](GraphicsLayerScope& l) { l.rotationZ = turn; }));
}

} // namespace SplitButtonDefaults

void Icon(const ImageVector& icon, std::string contentDescription, const Modifier& modifier, std::optional<Color> tint,
          CallSite site) {
    composable(site, icon, contentDescription, modifier, tint, [=] {
        Color color = tint.value_or(LocalContentColor.current());
        layoutNode(
            CallSite::current(), modifier, [](LayoutNode&, const Constraints& c) { return c.constrain({24, 24}); }, {},
            [icon, color](GraphicsContext& canvas, const Rect& b, const LayoutNode& node) {
                if (icon.empty() || color.a <= 0) return;
                detail::drawImageVector(canvas, icon.autoMirror() && node.rtl ? icon.mirrored() : icon, b, color);  // a back arrow points to the start
            },
            contentDescription);
    });
}

void Icon(const Painter& painter, std::string contentDescription, const Modifier& modifier, std::optional<Color> tint,
          CallSite site) {
    composable(site, painter, contentDescription, modifier, tint, [=] {
        Color color = tint.value_or(LocalContentColor.current());
        Modifier sized = painter.hasIntrinsicSize() ? modifier : modifier.size(24);  // its own size, or an icon's
        layoutNode(CallSite::current(), sized.paint(painter, true, Alignment::Center, ContentScale::Fit, 1, ColorFilter::tint(color)),
                   [](LayoutNode&, const Constraints& c) { return Size{c.minWidth, c.minHeight}; }, {}, {}, contentDescription);
    });
}

namespace {

// Where the image goes for a ContentScale (centered), and which part of it is visible.
std::pair<Rect, Rect> fitImage(float iw, float ih, const Rect& b, ContentScale scale) {
    float sx = 1, sy = 1;
    switch (scale) {
    case ContentScale::Fit: sx = sy = std::min(b.width / iw, b.height / ih); break;
    case ContentScale::Inside: sx = sy = std::min({1.f, b.width / iw, b.height / ih}); break;
    case ContentScale::Crop: sx = sy = std::max(b.width / iw, b.height / ih); break;
    case ContentScale::FillBounds: sx = b.width / iw, sy = b.height / ih; break;
    case ContentScale::FillWidth: sx = sy = b.width / iw; break;
    case ContentScale::FillHeight: sx = sy = b.height / ih; break;
    case ContentScale::None: break;
    }
    Rect full{b.x + (b.width - iw * sx) / 2, b.y + (b.height - ih * sy) / 2, iw * sx, ih * sy};
    Rect dst = full.intersect(b);
    Rect src{(dst.x - full.x) / sx, (dst.y - full.y) / sy, dst.width / sx, dst.height / sy};
    return {src, dst};
}

} // namespace

namespace {

// An image's size in these constraints: its own (in dp), scaled down to fit; with one fixed
// side, the other follows its aspect ratio.
Size imageSize(float iw, float ih, const Constraints& c) {
    if (iw <= 0 || ih <= 0) return {c.minWidth, c.minHeight};
    bool fixedW = c.minWidth == c.maxWidth, fixedH = c.minHeight == c.maxHeight;
    if (fixedW && fixedH) return {c.maxWidth, c.maxHeight};
    if (fixedW) return c.constrain({c.maxWidth, c.maxWidth * ih / iw});
    if (fixedH) return c.constrain({c.maxHeight * iw / ih, c.maxHeight});
    float s = std::min({1.f, c.maxWidth / iw, c.maxHeight / ih});
    return c.constrain({iw * s, ih * s});
}

// Images loaded by AsyncImage, by model (the biggest one loaded so far); the least recently
// used go beyond the limit. And the loads in flight, with who waits for each. UI thread only.
struct ImageCache {
    struct Entry {
        ImageBitmap image;
        size_t bytes = 0;
        uint64_t used = 0;
        bool whole = false;  // not decoded smaller: good for any size
    };
    struct Waiter {
        ImageRequestSize size;
        std::function<void(ImageBitmap)> done;
    };
    static constexpr size_t limit = size_t(64) << 20;
    std::unordered_map<std::string, Entry> entries;
    std::unordered_map<std::string, std::vector<Waiter>> loading;
    size_t total = 0;
    uint64_t clock = 0;

    // The model's image if one big enough for `size` is here; with no size, any.
    std::optional<ImageBitmap> find(const std::string& model, std::optional<ImageRequestSize> size) {
        auto it = entries.find(model);
        if (it == entries.end()) return std::nullopt;
        Entry& e = it->second;
        if (size && !e.whole && !imageCovers(e.image.width(), e.image.height(), *size)) return std::nullopt;
        e.used = ++clock;
        return e.image;
    }
    void put(const std::string& model, const ImageBitmap& image, bool whole) {
        if (auto it = entries.find(model); it != entries.end()) {
            const Entry& old = it->second;
            if (old.whole || (!whole && old.image.width() >= image.width() && old.image.height() >= image.height())) {
                it->second.used = ++clock;  // a smaller one than what is here
                return;
            }
            total -= old.bytes;
        }
        size_t bytes = size_t(image.width()) * size_t(image.height()) * 4;
        entries[model] = {image, bytes, ++clock, whole};
        total += bytes;
        while (total > limit && entries.size() > 1) {
            auto oldest = std::min_element(entries.begin(), entries.end(),
                                           [](const auto& a, const auto& b) { return a.second.used < b.second.used; });
            total -= oldest->second.bytes;
            entries.erase(oldest);
        }
    }
};
ImageCache& imageCache() {
    static ImageCache cache;
    return cache;
}

using ImageLoader = std::function<void(const std::string&, ImageRequestSize, std::function<void(ImageBitmap)>)>;

// `done` with the model's image for `size` (empty if it failed), on the UI thread: from the
// cache, or once a load finishes. One load per model at a time: who waits for a bigger image
// than it brought gets another.
void requestImage(const ImageLoader& load, const std::string& model, ImageRequestSize size, std::function<void(ImageBitmap)> done) {
    ImageCache& cache = imageCache();
    if (auto hit = cache.find(model, size)) return done(*hit);
    if (!load) return done({});
    auto& waiting = cache.loading[model];
    waiting.push_back({size, std::move(done)});
    if (waiting.size() > 1) return;  // already on its way
    load(model, size, [load, model, size](ImageBitmap image) {
        runOnUiThread([load, model, size, image] {
            ImageCache& cache = imageCache();
            // Smaller than asked for: the decoder couldn't do better, it is the whole image.
            bool whole = size.any() || !imageCovers(image.width(), image.height(), size);
            if (!image.empty()) cache.put(model, image, whole);  // failures are tried again next time
            auto it = cache.loading.find(model);
            if (it == cache.loading.end()) return;
            auto waiters = std::move(it->second);
            cache.loading.erase(it);
            for (auto& w : waiters) {
                if (image.empty() || whole || imageCovers(image.width(), image.height(), w.size)) w.done(image);
                else requestImage(load, model, w.size, std::move(w.done));
            }
        });
    });
}

// The loading state of `model`, loaded at `size` once it is known (until then only from the
// cache): AsyncImage asks for the size its layout first gets.
AsyncImageState asyncImageState(const std::string& model, std::optional<ImageRequestSize> size) {
    ComposeHost& host = detail::Composer::require().host();
    using Status = AsyncImageState::Status;
    auto state = remember(model, [&] {
        auto hit = imageCache().find(model, std::nullopt);  // any size at once; a bigger one may follow
        return mutableStateOf(hit ? AsyncImageState{Status::Success, *hit} : AsyncImageState{});
    });
    struct Key {
        std::string model;
        std::optional<ImageRequestSize> size;
        bool operator==(const Key&) const = default;
    };
    DisposableEffect(Key{model, size}, [&host, model, size, state]() -> std::function<void()> {
        if (!size) return [] {};
        AsyncImageState now = state.peek();
        if (now.status == Status::Success && imageCache().find(model, *size) == now.image) return [] {};
        auto alive = std::make_shared<bool>(true);  // gone from the composition: the result is dropped
        requestImage(host.imageLoader(), model, *size, [state, alive](ImageBitmap image) {
            if (!*alive) return;
            if (!image.empty()) state.set(AsyncImageState{Status::Success, image});
            else if (state.peek().status != Status::Success) state.set(AsyncImageState{Status::Error, {}});  // a smaller one stays
        });
        return [alive] { *alive = false; };
    });
    return state.get();
}

} // namespace

void Image(const ImageBitmap& bitmap, std::string contentDescription, const Modifier& modifier,
           ContentScale contentScale, float alpha, std::optional<ColorFilter> colorFilter, CallSite site) {
    composable(site, bitmap, contentDescription, modifier, contentScale, alpha, colorFilter, [=] {
        float iw = float(bitmap.width()), ih = float(bitmap.height());
        auto measure = [iw, ih](LayoutNode&, const Constraints& c) -> Size { return imageSize(iw, ih, c); };
        ImageBitmap shown = colorFilter ? colorFilter->apply(bitmap) : bitmap;
        auto draw = [bitmap = shown, contentScale, alpha, iw, ih](GraphicsContext& canvas, const Rect& b, const LayoutNode&) {
            if (iw <= 0 || ih <= 0 || b.width <= 0 || b.height <= 0) return;
            auto [src, dst] = fitImage(iw, ih, b, contentScale);
            if (dst.width > 0 && dst.height > 0) canvas.drawImage(bitmap, src, dst, alpha);
        };
        layoutNode(CallSite::current(), modifier, measure, {}, draw, contentDescription);
    });
}

ImageBitmap imageResource(const std::string& path, CallSite site) {
    auto& host = detail::Composer::require().host();
    return remember(path, [&] { return host.loadImage(path); }, site);
}

Painter painterResource(const std::string& path, CallSite site) { return BitmapPainter(imageResource(path, site)); }

ImageVector vectorResource(const std::string& path, CallSite site) {
    ComposeHost& host = detail::Composer::require().host();
    return remember(
        path,
        [&] {
            std::vector<uint8_t> bytes = host.readAsset(path);
            return ImageVector::fromVectorXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        },
        site);
}

Painter rememberVectorPainter(const ImageVector& image, CallSite site) {
    (void)site;  // nothing to keep: equal vectors make equal painters
    return VectorPainter(image);
}

AnimatedImageVector animatedVectorResource(const std::string& path, CallSite site) {
    ComposeHost& host = detail::Composer::require().host();
    return remember(
        path,
        [&] {
            std::vector<uint8_t> bytes = host.readAsset(path);
            return AnimatedImageVector::fromAnimatedVectorXml(std::string_view(reinterpret_cast<const char*>(bytes.data()), bytes.size()));
        },
        site);
}

Painter rememberAnimatedVectorPainter(const AnimatedImageVector& vector, bool atEnd, CallSite site) {
    ComposableScope scope(site);
    float total = float(vector.totalDurationMillis());
    float millis = animateFloatAsState(atEnd ? total : 0.f, tween(total, 0, LinearEasing));
    ImageVector now = vector.vectorAt(millis);
    auto paths = [&](ImageVector::Builder& b) {
        for (const VectorPath& p : now.paths()) b.path(p);
    };
    return rememberVectorPainter(now.defaultWidth(), now.defaultHeight(), now.viewportWidth(), now.viewportHeight(), paths, now.autoMirror());
}

Painter rememberVectorPainter(float defaultWidth, float defaultHeight, float viewportWidth, float viewportHeight,
                              const std::function<void(ImageVector::Builder&)>& content, bool autoMirror, CallSite site) {
    ComposableScope scope(site);  // what the content remembers stays in here
    ImageVector::Builder builder(defaultWidth, defaultHeight, viewportWidth, viewportHeight);
    content(builder);
    ImageVector made = builder.autoMirror(autoMirror).build();
    ImageVector& last = remember([] { return ImageVector(); });
    bool same = !last.empty() && last.paths() == made.paths() && last.viewportWidth() == made.viewportWidth() &&
                last.viewportHeight() == made.viewportHeight() && last.defaultWidth() == made.defaultWidth() &&
                last.defaultHeight() == made.defaultHeight() && last.autoMirror() == made.autoMirror();
    if (!same) last = made;
    return VectorPainter(last);
}

void Image(const Painter& painter, std::string contentDescription, const Modifier& modifier, Alignment alignment,
           ContentScale contentScale, float alpha, std::optional<ColorFilter> colorFilter, CallSite site) {
    composable(site, painter, contentDescription, modifier, alignment, contentScale, alpha, colorFilter, [=] {
        layoutNode(CallSite::current(), modifier.clipToBounds().paint(painter, true, alignment, contentScale, alpha, colorFilter),
                   [](LayoutNode&, const Constraints& c) { return Size{c.minWidth, c.minHeight}; }, {}, {}, contentDescription);
    });
}

AsyncImageState rememberAsyncImage(const std::string& model, ImageRequestSize size, CallSite site) {
    ComposableScope scope(site);
    return asyncImageState(model, size);
}

Painter rememberAsyncImagePainter(const std::string& model, ImageRequestSize size, CallSite site) {
    ComposableScope scope(site);
    AsyncImageState state = asyncImageState(model, size);
    return state.status == AsyncImageState::Status::Success ? BitmapPainter(state.image) : Painter();
}

void clearImageCache() {
    ImageCache& cache = imageCache();
    cache.entries.clear();
    cache.total = 0;
}

void AsyncImage(std::string model, std::string contentDescription, const Modifier& modifier, const AsyncImageOptions& options,
                CallSite site) {
    ComposableScope scope(site);
    using Status = AsyncImageState::Status;
    // The size it is loaded at: what its layout first gets, in pixels, as Coil resolves it from
    // the constraints (a big photo shown small is decoded small).
    auto requested = remember(model, [] { return mutableStateOf(std::optional<ImageRequestSize>{}); });
    AsyncImageState state = asyncImageState(model, requested.get());
    bool loaded = state.status == Status::Success;
    if (options.onState) {
        auto& told = remember(model, [] { return std::make_shared<std::optional<AsyncImageState>>(); });
        if (*told != state) {
            *told = state;
            SideEffect([onState = options.onState, state] { onState(state); });
        }
    }
    // From the placeholder to the image; not for one that was there at once (from the cache).
    bool atOnce = remember(model, [&] { return loaded; });
    auto fade = remember(model, [&] { return Animatable<float>(atOnce || options.crossfadeMillis <= 0 ? 1.f : 0.f); });
    LaunchedEffect(loaded, [fade, loaded, millis = options.crossfadeMillis]() -> Task {
        if (loaded) co_await fade.animateTo(1.f, tween(millis));
    });
    float shown = loaded ? fade.value() : 0.f;
    // Sized by the image once there is one, else by the placeholder (or the modifier); both
    // fill that size, so the content scale applies to all of it while they cross-fade.
    float iw = float(state.image.width()), ih = float(state.image.height());
    float density = std::max(1.f, LocalDensity.current().density);
    auto measure = [iw, ih, requested, density, scale = options.contentScale](LayoutNode& node, const Constraints& c) -> Size {
        if (!requested.peek() && !detail::isProbeMeasure()) {
            ImageRequestSize size{.fill = scale == ContentScale::Crop || scale == ContentScale::FillBounds};
            if (scale != ContentScale::None) {  // None: the image as it is
                if (c.hasBoundedWidth() && scale != ContentScale::FillHeight) size.width = int(std::ceil(c.maxWidth * density));
                if (c.hasBoundedHeight() && scale != ContentScale::FillWidth) size.height = int(std::ceil(c.maxHeight * density));
            }
            requested.set(size);
        }
        Size size = imageSize(iw, ih, c);
        if (iw <= 0 || ih <= 0)
            for (auto* child : node.children) {
                Size s = child->measure(c);
                size = {std::max(size.width, s.width), std::max(size.height, s.height)};
            }
        for (auto* child : node.children) {
            child->measure(Constraints::fixed(size.width, size.height));
            child->place(0, 0);
        }
        return size;
    };
    layoutNode(site, modifier, measure, [&] {
        const Content& slot = state.status == Status::Error ? options.error : options.placeholder;
        if (shown < 1 && slot) Box(Modifier(), Alignment::Center, slot);
        if (loaded) Image(state.image, contentDescription, Modifier(), options.contentScale, options.alpha * shown, options.colorFilter);
    });
}

namespace {

enum class IconButtonKind { Standard, Filled, Tonal, Outlined };

// The box every icon button is: a 40dp circle in `container` (in 48 dp of room to touch it), an
// optional outline, and `content` in `contentColor`; `click` makes it clickable (or toggleable) —
// on top of the circle and under the content color, so that its ripple keeps to the circle in
// that color.
void iconButtonBox(const Modifier& modifier, Color container, Color contentColor, std::optional<Color> border,
                   const std::function<Modifier(const Modifier&)>& click, const Content& content) {
    Modifier m = modifier.minimumInteractiveComponentSize().size(40).background(container, 20);
    if (border) m = m.border(1, *border, 20);
    CompositionLocalProvider({LocalContentColor.provides(contentColor)},
                             [&] { Box(click(m), Alignment::Center, content); });
}

void iconButtonImpl(IconButtonKind kind, std::function<void()> onClick, const Modifier& modifier,
                    const IconButtonStyle& style, const Content& content, const CallSite& site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    // Compose 1.4's: filled primary, tonal secondaryContainer (disabled: the surface's text at 10%).
    Color container = style.containerColor.value_or(kind == IconButtonKind::Filled  ? cs.primary
                                                    : kind == IconButtonKind::Tonal ? cs.secondaryContainer
                                                                                    : colors::Transparent);
    // The standard and outlined ones take the content color around them, as Compose's
    // iconButtonColors() (an app bar's, a snackbar's), and fade it when disabled; an outlined
    // one's outline is in it too.
    bool plain = kind == IconButtonKind::Standard || kind == IconButtonKind::Outlined;
    Color around = LocalContentColor.current();
    Color contentColor = style.contentColor.value_or(kind == IconButtonKind::Filled  ? cs.onPrimary
                                                     : kind == IconButtonKind::Tonal ? cs.onSecondaryContainer
                                                                                     : around);
    std::optional<Color> border;
    if (kind == IconButtonKind::Outlined) border = style.enabled ? around : around.copy(around.a * 0.38f);
    if (!style.enabled) {
        if (kind == IconButtonKind::Filled || kind == IconButtonKind::Tonal) container = cs.onSurface.copy(0.1f);
        contentColor = plain ? contentColor.copy(contentColor.a * 0.38f) : cs.onSurface.copy(0.38f);
    }
    iconButtonBox(modifier, container, contentColor, border, [&](const Modifier& m) {
        return m.clickable(std::move(onClick), {.enabled = style.enabled, .interactionSource = style.interactionSource});
    }, content);
}

// The toggles' colors, as Material 3's IconButtonDefaults.*IconToggleButtonColors.
void iconToggleButtonImpl(IconButtonKind kind, bool checked, std::function<void(bool)> onCheckedChange,
                          const Modifier& modifier, const IconToggleButtonStyle& style, const Content& content,
                          const CallSite& site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    // Compose 1.4's: filled ones surfaceContainer/onSurfaceVariant off, primary/onPrimary on;
    // tonal ones secondaryContainer off, secondary on; outlined ones the content color around
    // them off (its outline too), inverseSurface on (without the outline); disabled, the filled
    // and tonal ones on the surface's text at 10%, the others clear.
    Color around = LocalContentColor.current();
    Color container = colors::Transparent, contentColor = around;  // as the plain ones'
    Color checkedContainer = colors::Transparent, checkedContent = cs.primary;
    switch (kind) {
    case IconButtonKind::Standard: break;
    case IconButtonKind::Filled:
        container = cs.surfaceContainer, contentColor = cs.onSurfaceVariant;
        checkedContainer = cs.primary, checkedContent = cs.onPrimary;
        break;
    case IconButtonKind::Tonal:
        container = cs.secondaryContainer, contentColor = cs.onSecondaryContainer;
        checkedContainer = cs.secondary, checkedContent = cs.onSecondary;
        break;
    case IconButtonKind::Outlined: checkedContainer = cs.inverseSurface, checkedContent = cs.inverseOnSurface; break;
    }
    Color c = checked ? style.checkedContainerColor.value_or(checkedContainer) : style.containerColor.value_or(container);
    Color fg = checked ? style.checkedContentColor.value_or(checkedContent) : style.contentColor.value_or(contentColor);
    std::optional<Color> border;
    if (kind == IconButtonKind::Outlined && !checked) border = style.enabled ? around : around.copy(around.a * 0.38f);
    if (!style.enabled) {
        bool filled = kind == IconButtonKind::Filled || kind == IconButtonKind::Tonal;
        c = filled ? cs.onSurface.copy(0.1f) : colors::Transparent;
        fg = filled ? cs.onSurface.copy(0.38f) : around.copy(around.a * 0.38f);
    }
    iconButtonBox(modifier, c, fg, border, [&](const Modifier& m) {
        return m.toggleable(checked, std::move(onCheckedChange), SemanticsRole::Checkbox,
                            {.enabled = style.enabled, .interactionSource = style.interactionSource});
    }, content);
}

} // namespace

void IconButton(std::function<void()> onClick, const Content& content, CallSite site) {
    iconButtonImpl(IconButtonKind::Standard, std::move(onClick), Modifier{}, {}, content, site);
}
void IconButton(std::function<void()> onClick, const Modifier& modifier, const IconButtonStyle& style,
                const Content& content, CallSite site) {
    iconButtonImpl(IconButtonKind::Standard, std::move(onClick), modifier, style, content, site);
}
void FilledIconButton(std::function<void()> onClick, const Content& content, CallSite site) {
    iconButtonImpl(IconButtonKind::Filled, std::move(onClick), Modifier{}, {}, content, site);
}
void FilledIconButton(std::function<void()> onClick, const Modifier& modifier, const IconButtonStyle& style,
                      const Content& content, CallSite site) {
    iconButtonImpl(IconButtonKind::Filled, std::move(onClick), modifier, style, content, site);
}
void FilledTonalIconButton(std::function<void()> onClick, const Content& content, CallSite site) {
    iconButtonImpl(IconButtonKind::Tonal, std::move(onClick), Modifier{}, {}, content, site);
}
void FilledTonalIconButton(std::function<void()> onClick, const Modifier& modifier, const IconButtonStyle& style,
                           const Content& content, CallSite site) {
    iconButtonImpl(IconButtonKind::Tonal, std::move(onClick), modifier, style, content, site);
}
void OutlinedIconButton(std::function<void()> onClick, const Content& content, CallSite site) {
    iconButtonImpl(IconButtonKind::Outlined, std::move(onClick), Modifier{}, {}, content, site);
}
void OutlinedIconButton(std::function<void()> onClick, const Modifier& modifier, const IconButtonStyle& style,
                        const Content& content, CallSite site) {
    iconButtonImpl(IconButtonKind::Outlined, std::move(onClick), modifier, style, content, site);
}
void IconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content, CallSite site) {
    iconToggleButtonImpl(IconButtonKind::Standard, checked, std::move(onCheckedChange), Modifier{}, {}, content, site);
}
void IconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
            const IconToggleButtonStyle& style, const Content& content, CallSite site) {
    iconToggleButtonImpl(IconButtonKind::Standard, checked, std::move(onCheckedChange), modifier, style, content, site);
}
void FilledIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content, CallSite site) {
    iconToggleButtonImpl(IconButtonKind::Filled, checked, std::move(onCheckedChange), Modifier{}, {}, content, site);
}
void FilledIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
            const IconToggleButtonStyle& style, const Content& content, CallSite site) {
    iconToggleButtonImpl(IconButtonKind::Filled, checked, std::move(onCheckedChange), modifier, style, content, site);
}
void FilledTonalIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content, CallSite site) {
    iconToggleButtonImpl(IconButtonKind::Tonal, checked, std::move(onCheckedChange), Modifier{}, {}, content, site);
}
void FilledTonalIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
            const IconToggleButtonStyle& style, const Content& content, CallSite site) {
    iconToggleButtonImpl(IconButtonKind::Tonal, checked, std::move(onCheckedChange), modifier, style, content, site);
}
void OutlinedIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content, CallSite site) {
    iconToggleButtonImpl(IconButtonKind::Outlined, checked, std::move(onCheckedChange), Modifier{}, {}, content, site);
}
void OutlinedIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
            const IconToggleButtonStyle& style, const Content& content, CallSite site) {
    iconToggleButtonImpl(IconButtonKind::Outlined, checked, std::move(onCheckedChange), modifier, style, content, site);
}

namespace {

void progressNode(const Modifier& modifier, Size size, DrawContent draw, std::string semantics) {
    layoutNode(
        CallSite::current(), modifier, [size](LayoutNode&, const Constraints& c) { return c.constrain(size); }, {},
        std::move(draw), std::move(semantics));
}

} // namespace

namespace {
// Compose 1.4's progress indicators (ProgressIndicator.kt): a gap of 4 dp (and the stroke, for its
// round caps) between the indicator and its track; its indeterminate motion.
constexpr Easing kEmphasizedAccelerate{0.3f, 0, 0.8f, 0.15f}, kStandard{0.2f, 0, 0, 1};
// A keyframed 0 → 1: held at 0 until `delay`, eased to 1 in `duration`, held there (ms).
float keyframe01(float t, float delay, float duration, Easing easing) {
    return t < delay ? 0.f : t >= delay + duration ? 1.f : easing((t - delay) / duration);
}
// A round-capped bar of a linear indicator between two fractions of its width (from the layout's
// start), kept inside it.
void linearBar(GraphicsContext& canvas, const Rect& b, bool rtl, float from, float to, Color color) {
    if (from == to) return;
    float w = b.width, h = b.height, lo = std::min(from, to), hi = std::max(from, to);
    float x0 = std::clamp((rtl ? 1 - hi : lo) * w, h / 2, w - h / 2), x1 = std::clamp((rtl ? 1 - lo : hi) * w, h / 2, w - h / 2);
    canvas.fillRoundRect({b.x + x0 - h / 2, b.y, x1 - x0 + h, h}, h / 2, color);
}
float circularGapSweep(float diameter, float strokeWidth) { return (4 + strokeWidth) / (3.14159265f * diameter) * 360; }
} // namespace

void CircularProgressIndicator(const Modifier& modifier, std::optional<Color> color, float strokeWidth, CallSite site) {
    composable(site, modifier, color, strokeWidth, [=] {
        Color c = color.value_or(colorScheme().primary);
        auto clock = detail::rememberDrawClock();  // read as it draws: nothing composed or laid out as it turns
        progressNode(
            modifier.semantics({.progressBarRangeInfo = ProgressBarRangeInfo::Indeterminate}), {40, 40},
            [=](GraphicsContext& canvas, const Rect& b, const LayoutNode& node) {
                // One 6 s cycle: a 1080° turn, 90° more in 300 ms every 1.5 s, the arc out to 87% and back to 10%.
                float t = 6000 * clock.progress(node, infiniteRepeatable(tween(6000, 0, LinearEasing)));
                float step = std::floor(t / 1500), within = t - step * 1500;
                float rotation = t / 6000 * 1080 + 90 * (step + std::min(1.f, within / 300));
                float progress = t < 3000 ? lerp(0.1f, 0.87f, t / 3000) : lerp(0.87f, 0.1f, kStandard((t - 3000) / 3000));
                float half = strokeWidth / 2;
                Rect oval{b.x + half, b.y + half, b.width - strokeWidth, b.height - strokeWidth};
                canvas.drawArc(oval, rotation, progress * 360, strokeWidth, c, true);  // (its track: transparent)
            },
            "loading");
    });
}

void CircularProgressIndicator(float progress, const Modifier& modifier, std::optional<Color> color, float strokeWidth,
                               CallSite site) {
    composable(site, progress, modifier, color, strokeWidth, [=] {
        Color c = color.value_or(colorScheme().primary), track = colorScheme().secondaryContainer;
        float p = std::clamp(progress, 0.f, 1.f);
        progressNode(
            modifier.semantics({.progressBarRangeInfo = ProgressBarRangeInfo{p, 0, 1}}), {40, 40},
            [=](GraphicsContext& canvas, const Rect& b, const LayoutNode&) {
                float half = strokeWidth / 2;
                Rect oval{b.x + half, b.y + half, b.width - strokeWidth, b.height - strokeWidth};
                float sweep = p * 360, gap = std::min(sweep, circularGapSweep(b.width, strokeWidth));  // from 12 o'clock
                canvas.drawArc(oval, -90 + sweep + gap, 360 - sweep - 2 * gap, strokeWidth, track, true);
                if (p > 0) canvas.drawArc(oval, -90, sweep, strokeWidth, c, true);
            },
            std::to_string(int(std::lround(p * 100))) + "%");
    });
}

void LinearProgressIndicator(const Modifier& modifier, std::optional<Color> color, CallSite site) {
    composable(site, modifier, color, [=] {
        Color c = color.value_or(colorScheme().primary), track = colorScheme().secondaryContainer;
        auto clock = detail::rememberDrawClock();  // read as it draws
        progressNode(
            modifier.semantics({.progressBarRangeInfo = ProgressBarRangeInfo::Indeterminate}), {240, 4},
            [=](GraphicsContext& canvas, const Rect& b, const LayoutNode& node) {
                // Two lines chasing across in 1.75 s, each head and tail eased out in its own time.
                float t = 1750 * clock.progress(node, infiniteRepeatable(tween(1750, 0, LinearEasing)));
                float head1 = keyframe01(t, 0, 1000, kEmphasizedAccelerate), tail1 = keyframe01(t, 250, 1000, kEmphasizedAccelerate);
                float head2 = keyframe01(t, 650, 850, kEmphasizedAccelerate), tail2 = keyframe01(t, 900, 850, kEmphasizedAccelerate);
                float gap = (4 + b.height) / b.width;
                bool rtl = node.rtl;
                if (head1 < 1 - gap) linearBar(canvas, b, rtl, head1 > 0 ? head1 + gap : 0, 1, track);  // the track ahead of line 1
                if (head1 > tail1) linearBar(canvas, b, rtl, tail1, head1, c);
                if (tail1 > gap) linearBar(canvas, b, rtl, head2 > 0 ? head2 + gap : 0, tail1 < 1 ? tail1 - gap : 1, track);  // between them
                if (head2 > tail2) linearBar(canvas, b, rtl, tail2, head2, c);
                if (tail2 > gap) linearBar(canvas, b, rtl, 0, tail2 < 1 ? tail2 - gap : 1, track);  // behind line 2
            },
            "loading");
    });
}

void LinearProgressIndicator(float progress, const Modifier& modifier, std::optional<Color> color, CallSite site) {
    composable(site, progress, modifier, color, [=] {
        Color c = color.value_or(colorScheme().primary), track = colorScheme().secondaryContainer;
        float p = std::clamp(progress, 0.f, 1.f);
        progressNode(
            modifier.semantics({.progressBarRangeInfo = ProgressBarRangeInfo{p, 0, 1}}), {240, 4},
            [=](GraphicsContext& canvas, const Rect& b, const LayoutNode& node) {
                float gap = (4 + b.height) / b.width;
                if (float start = p + std::min(p, gap); start <= 1) linearBar(canvas, b, node.rtl, start, 1, track);
                linearBar(canvas, b, node.rtl, 0, p, c);
                // The stop at its end: a dot as tall as it (4 dp at most), up to 6 dp in.
                float size = std::min(4.f, b.height), in = std::min((b.height - size) / 2, 6.f);
                float x = node.rtl ? b.x + in : b.right() - in - size;
                canvas.fillRoundRect({x, b.y + (b.height - size) / 2, size, size}, size / 2, c);
            },
            std::to_string(int(std::lround(p * 100))) + "%");
    });
}

// Compose 1.4's cards: 12 dp corners clipping their content; filled ones surfaceContainerHighest,
// elevated ones surfaceContainerLow 1 dp up, outlined ones the surface in a 1 dp outlineVariant line.
constexpr Shape kCardShape = RoundedCornerShape(12);

std::optional<Color> ColorScheme::contentColorFor(Color c) const {
    const std::pair<Color, Color> pairs[] = {
        {primary, onPrimary}, {secondary, onSecondary}, {tertiary, onTertiary}, {background, onBackground}, {error, onError},
        {primaryContainer, onPrimaryContainer}, {secondaryContainer, onSecondaryContainer}, {tertiaryContainer, onTertiaryContainer},
        {errorContainer, onErrorContainer}, {inverseSurface, inverseOnSurface}, {surface, onSurface}, {surfaceVariant, onSurfaceVariant},
        {surfaceBright, onSurface}, {surfaceContainer, onSurface}, {surfaceContainerHigh, onSurface}, {surfaceContainerHighest, onSurface},
        {surfaceContainerLow, onSurface}, {surfaceContainerLowest, onSurface}, {surfaceDim, onSurface},
        {primaryFixed, onPrimaryFixed}, {primaryFixedDim, onPrimaryFixed}, {secondaryFixed, onSecondaryFixed},
        {secondaryFixedDim, onSecondaryFixed}, {tertiaryFixed, onTertiaryFixed}, {tertiaryFixedDim, onTertiaryFixed}};
    for (const auto& [background, content] : pairs)  // the first match, as Compose's `when`
        if (background == c) return content;
    return std::nullopt;
}

namespace {
enum class CardKind { Filled, Elevated, Outlined };
// A card's container (onSurface content on it, clipped to its shape), clickable if `onClick` is set.
void card(CardKind kind, std::function<void()> onClick, const Modifier& modifier, const Content& content) {
    const ColorScheme& cs = colorScheme();
    Modifier m = kind == CardKind::Elevated ? modifier.shadow(1, kCardShape).background(cs.surfaceContainerLow, kCardShape)
                 : kind == CardKind::Outlined ? modifier.background(cs.surface, kCardShape).border(1, cs.outlineVariant, kCardShape)
                                              : modifier.background(cs.surfaceContainerHighest, kCardShape);
    m = m.clip(kCardShape);
    if (onClick) m = m.clickable(std::move(onClick));
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurface)}, [&] { Column(m, content); });
}
} // namespace

void Card(const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    card(CardKind::Filled, {}, modifier, content);
}
void ElevatedCard(const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    card(CardKind::Elevated, {}, modifier, content);
}
void OutlinedCard(const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    card(CardKind::Outlined, {}, modifier, content);
}
void Card(std::function<void()> onClick, const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    card(CardKind::Filled, std::move(onClick), modifier, content);
}
void ElevatedCard(std::function<void()> onClick, const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    card(CardKind::Elevated, std::move(onClick), modifier, content);
}
void OutlinedCard(std::function<void()> onClick, const Modifier& modifier, const Content& content, CallSite site) {
    ComposableScope scope(site);
    card(CardKind::Outlined, std::move(onClick), modifier, content);
}

void Surface(const Modifier& modifier, const Content& content, CallSite site) {
    const ColorScheme& cs = colorScheme();
    Surface(modifier, cs.background, cs.onBackground, content, site);
}
void Surface(const Modifier& modifier, Color color, Color contentColor, const Content& content, CallSite site) {
    ComposableScope scope(site);
    CompositionLocalProvider({LocalContentColor.provides(contentColor)},
                             [&] { Box(modifier.background(color), content); });
}

void HorizontalDivider(const Modifier& modifier, CallSite site) {
    layoutNode(site, modifier.fillMaxWidth().height(1).background(colorScheme().outlineVariant), {}, {});
}

void VerticalDivider(const Modifier& modifier, CallSite site) {
    layoutNode(site, modifier.fillMaxHeight().width(1).background(colorScheme().outlineVariant), {}, {});
}

void SystemBarsAppearance(bool lightStatusBars, bool lightNavigationBars, CallSite site) {
    ComposableScope scope(site);
    ComposeHost* host = &detail::Composer::require().host();
    SideEffect([host, lightStatusBars, lightNavigationBars] { host->setSystemBarsAppearance(lightStatusBars, lightNavigationBars); });
}

namespace {
void checkbox(ToggleableState state, std::function<void()> onClick, const Modifier& modifier, bool enabled,
              std::optional<MutableInteractionSource> interactionSource) {
    const ColorScheme& cs = colorScheme();
    const MotionScheme& motion = motionScheme();
    bool on = state != ToggleableState::Off;
    // Compose 1.4's colors (CheckboxDefaults.colors): disabled ones the surface's text faded, at
    // once; enabled ones in by the default effects spring, out by the fast one.
    AnimationSpec colorSpec = on ? motion.defaultEffectsSpec : motion.fastEffectsSpec;
    auto colorOf = [&](Color target) { return enabled ? animateColorAsState(target, colorSpec) : target; };
    Color fill = colorOf(on ? (enabled ? cs.primary : cs.onSurface.copy(0.38f)) : cs.primary.copy(0));
    Color border = colorOf(enabled ? (on ? cs.primary : cs.onSurfaceVariant) : cs.onSurface.copy(0.38f));
    Color mark = enabled ? animateColorAsState(on ? cs.onPrimary : cs.onPrimary.copy(0), colorSpec) : on ? cs.onPrimary : cs.onPrimary.copy(0);
    // The tick drawn along (by the default spatial spring), and bent into a dash for the
    // indeterminate state; taken away at once 100 ms after unticking.
    auto& states = remember([state] { return std::pair{state, state}; });  // the one before, the one now
    if (states.second != state) states = {states.second, state};
    ToggleableState from = states.first;
    AnimationSpec gone = snap(100);
    float drawn = animateFloatAsState(on ? 1.f : 0.f, state == ToggleableState::Off && from != state ? gone : motion.defaultSpatialSpec);
    float bent = animateFloatAsState(state == ToggleableState::Indeterminate ? 1.f : 0.f,
                                     from == ToggleableState::Off ? snap() : state == ToggleableState::Off ? gone : motion.defaultSpatialSpec);

    auto draw = [=](GraphicsContext& canvas, const Rect& b, const LayoutNode&) {
        // 20 dp, its 2 dp outline inside it (strokeRoundRect strokes inside the rect).
        Offset c = b.center();
        Rect box{c.x - 10, c.y - 10, 20, 20};
        if (fill == border) {
            canvas.fillRoundRect(box, 2, fill);
        } else {
            canvas.fillRoundRect({box.x + 2, box.y + 2, 16, 16}, 0, fill);
            canvas.strokeRoundRect(box, 2, 2, border);
        }
        if (drawn <= 0) return;
        Path tick, piece;
        tick.moveTo(4, 10);
        tick.lineTo(lerp(8, 10, bent), lerp(14, 10, bent));
        tick.lineTo(16, lerp(6, 10, bent));
        PathMeasure measure(tick);
        if (measure.getSegment(0, measure.length() * std::min(drawn, 1.f), piece))
            canvas.drawPath(piece, {box.x, box.y}, Brush::solid(mark), box, {.width = 2, .cap = StrokeCap::Square});
    };
    const char* description = state == ToggleableState::On ? "checked" : state == ToggleableState::Off ? "unchecked" : "indeterminate";
    // 24 dp; one that takes taps itself (not a row around it) gets 48 of room and a 40 dp ripple.
    Modifier m = modifier.semantics({.role = SemanticsRole::Checkbox, .checked = state == ToggleableState::On});
    if (onClick)
        m = m.minimumInteractiveComponentSize().clickable(
            std::move(onClick), {.enabled = enabled, .interactionSource = std::move(interactionSource), .indication = ripple(false, 20)});
    layoutNode(CallSite::current(), m.size(24), {}, {}, draw, description);
}
} // namespace

void Checkbox(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier, bool enabled,
              std::optional<MutableInteractionSource> interactionSource, CallSite site) {
    ComposableScope scope(site);
    std::function<void()> toggle;  // none: not clickable (the row around it takes the tap)
    if (onCheckedChange) toggle = [checked, onCheckedChange = std::move(onCheckedChange)] { onCheckedChange(!checked); };
    checkbox(checked ? ToggleableState::On : ToggleableState::Off, toggle, modifier, enabled, std::move(interactionSource));
}

void TriStateCheckbox(ToggleableState state, std::function<void()> onClick, const Modifier& modifier, bool enabled,
                      std::optional<MutableInteractionSource> interactionSource, CallSite site) {
    ComposableScope scope(site);
    checkbox(state, std::move(onClick), modifier, enabled, std::move(interactionSource));
}

void Switch(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier, bool enabled,
            std::optional<MutableInteractionSource> interactionSource, CallSite site) {
    Switch(checked, std::move(onCheckedChange), modifier, SwitchOptions{enabled, std::move(interactionSource), {}}, site);
}

void Switch(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier, const SwitchOptions& options, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    bool enabled = options.enabled;
    // Compose 1.4's colors (SwitchDefaults.colors): disabled ones faded over the surface.
    auto faded = [&](Color c, float a) { return Color{lerp(cs.surface.r, c.r, a), lerp(cs.surface.g, c.g, a), lerp(cs.surface.b, c.b, a), 1}; };
    Color track = enabled ? (checked ? cs.primary : cs.surfaceContainerHighest) : checked ? faded(cs.onSurface, 0.12f) : faded(cs.surfaceContainerHighest, 0.12f);
    Color outline = enabled ? (checked ? cs.primary : cs.outline) : checked ? track : faded(cs.onSurface, 0.12f);
    Color thumb = enabled ? (checked ? cs.onPrimary : cs.outline) : checked ? cs.surface : faded(cs.onSurface, 0.38f);
    MutableInteractionSource source = options.interactionSource ? *options.interactionSource : remember([] { return MutableInteractionSource(); });
    bool pressed = source.collectIsPressedAsState() && enabled;
    // 16 dp off, 24 on (or with an icon), 28 while held; moved by the fast spatial spring, but at
    // once to its pressed size (Compose's SnapSpec while pressed).
    AnimationSpec move = motionScheme().fastSpatialSpec, snap = tween(0);
    float t = animateFloatAsState(checked ? 1.f : 0.f, pressed ? snap : move);
    float d = animateFloatAsState(pressed ? 28.f : checked || options.thumbContent ? 24.f : 16.f, pressed ? snap : move);
    // Where the thumb's centre is, from the switch's centre (on at the end: the left, right to left).
    auto thumbX = [t](bool rtl) { return rtl ? lerp(10, -10, t) : lerp(-10, 10, t); };

    auto draw = [=](GraphicsContext& canvas, const Rect& b, const LayoutNode& node) {
        Rect trackRect{b.center().x - 26, b.center().y - 16, 52, 32};
        canvas.fillRoundRect(trackRect, 16, track);
        canvas.strokeRoundRect(trackRect, 16, 2, outline);
        float cx = b.center().x + thumbX(node.rtl), cy = trackRect.center().y;
        canvas.fillRoundRect({cx - d / 2, cy - d / 2, d, d}, d / 2, thumb);
    };
    // The icon in the thumb, at its centre.
    MeasurePolicy policy = [thumbX](LayoutNode& node, const Constraints& c) {
        Size size = c.constrain({52, 32});
        for (auto* child : node.children) {
            Size s = child->measure(Constraints::fixed(16, 16));
            child->place(size.width / 2 + thumbX(false) - s.width / 2, size.height / 2 - s.height / 2);  // (mirrored right to left)
        }
        return size;
    };
    std::function<void()> toggle;  // none: not clickable (the row around it takes the tap)
    if (onCheckedChange) toggle = [checked, onCheckedChange = std::move(onCheckedChange)] { onCheckedChange(!checked); };
    Color icon = enabled ? (checked ? cs.onPrimaryContainer : cs.surfaceContainerHighest)
                         : checked ? faded(cs.onSurface, 0.38f) : faded(cs.surfaceContainerHighest, 0.38f);
    Modifier m = modifier.semantics({.role = SemanticsRole::Switch, .checked = checked});
    if (toggle) m = m.minimumInteractiveComponentSize();  // 48 dp of room when it takes taps itself
    m = m.size(52, 32).background(colors::Transparent, 16);
    if (toggle) m = m.clickable(toggle, {.enabled = enabled, .interactionSource = source});
    layoutNode(CallSite::current(), m,
               policy,
               [&] {
                   if (options.thumbContent) CompositionLocalProvider({LocalContentColor.provides(icon)}, options.thumbContent);
               },
               draw, checked ? "on" : "off");
}

namespace {

// A context menu at `at` (in the window), flipped to stay in it: its items as rows; picking one
// closes it and runs it.
void contextMenuPopup(Offset at, const std::vector<ContextMenuItem>& items, const std::function<void()>& close) {
    const ColorScheme& cs = colorScheme();
    const Typography& t = typography();
    Popup({.position = [at](const Rect&, Size size, Size viewport) {
               float x = at.x + size.width > viewport.width - 8 ? at.x - size.width : at.x;  // no room on the right: on the left
               float y = at.y + size.height > viewport.height - 8 ? at.y - size.height : at.y;
               return Offset{std::clamp(x, 8.f, std::max(8.f, viewport.width - size.width - 8)),
                             std::clamp(y, 8.f, std::max(8.f, viewport.height - size.height - 8))};
           },
           .onDismissRequest = close},
          [&] {
              Layout(Modifier().shadow(3, 4).background(cs.surfaceContainer, 4).padding(0, 8),
                     [&] {
                         for (const ContextMenuItem& item : items) {
                             Color color = item.enabled ? cs.onSurface : cs.onSurface.copy(0.38f);
                             Box(Modifier()
                                     .heightIn(40)
                                     .clickable(
                                         [close, run = item.onClick] {
                                             close();
                                             if (run) run();
                                         },
                                         item.enabled)
                                     .padding(12, 0),
                                 Alignment::CenterStart, [&] { Text(item.label, {.font = t.bodyLarge, .color = color}); });
                         }
                     },
                     detail::menuRows(280));
          });
}

enum class FieldKind { Filled, Outlined, Plain };  // Plain: BasicTextField, the text alone
constexpr std::string_view kBullet = "•";

// A secure field's text: dots of `bullet` (but the character starting at byte `revealed`), or the
// text itself; never copied.
struct Obfuscation {
    std::string bullet;
    int revealed = -1;
    bool visible = false;
};

// The text a field shows for its value — one dot per character (cluster) of a password, or
// what an OutputTransformation made of it — and the offsets between the two.
struct ShownText {
    std::string text;
    std::vector<int> toShown;  // per offset of the value, 0..its length
    std::vector<int> toValue;  // per offset of the shown text
};
using Display = std::shared_ptr<const ShownText>;  // null: the value as it is

// One `bullet` per character (cluster) of a password, but the one starting at byte `revealed`,
// shown as it is.
Display passwordDisplay(const std::string& value, std::string_view bullet = kBullet, int revealed = -1) {
    auto d = std::make_shared<ShownText>();
    d->toShown.assign(value.size() + 1, 0);
    for (size_t k = 0; k < value.size();) {
        size_t next = utf8::nextCluster(value, k);
        size_t at = d->text.size();
        d->text += int(k) == revealed ? std::string_view(value).substr(k, next - k) : bullet;
        for (size_t o = k + 1; o <= next; ++o) d->toShown[o] = int(d->text.size());  // an offset inside a character: after it
        d->toValue.insert(d->toValue.end(), d->text.size() - at, int(k));
        k = next;
    }
    d->toValue.push_back(int(value.size()));
    return d;
}

// Every byte keeps where it came from; an offset of the value goes before what was inserted
// there (after it at the very start: "+90 |"), and a tap on inserted text goes to where it was put.
Display outputDisplay(const std::string& value, const std::function<void(TextFieldBuffer&)>& transform) {
    int n = int(value.size());
    TextFieldBuffer::Tracking tracking;
    tracking.originalLength = n;
    for (int i = 0; i < n; ++i) tracking.origins.push_back({i, false});
    TextFieldBuffer buffer(value, {n, n});
    buffer.track(&tracking);
    transform(buffer);
    buffer.track(nullptr);
    if (!buffer.changed() && buffer.text() == value) return nullptr;
    auto d = std::make_shared<ShownText>();
    d->text = buffer.text();
    const auto& origins = tracking.origins;
    int m = int(d->text.size());
    std::vector<int> first(size_t(n) + 1, -1), original(size_t(n) + 1, -1);
    for (int s = m - 1; s >= 0; --s) {
        const auto& o = origins[size_t(s)];
        first[size_t(o.offset)] = s;
        if (!o.inserted) original[size_t(o.offset)] = s;
    }
    original[size_t(n)] = m;
    if (first[size_t(n)] < 0) first[size_t(n)] = m;
    d->toShown.assign(size_t(n) + 1, m);
    for (int o = n; o >= 0; --o) {
        int at = o == 0 && original[0] >= 0 ? original[0] : first[size_t(o)];
        d->toShown[size_t(o)] = at >= 0 ? at : d->toShown[size_t(std::min(o + 1, n))];  // removed: where what follows went
    }
    d->toValue.resize(size_t(m) + 1);
    for (int s = 0; s < m; ++s) d->toValue[size_t(s)] = origins[size_t(s)].offset;
    d->toValue[size_t(m)] = n;
    return d;
}

std::string displayText(const std::string& value, const Display& d) { return d ? d->text : value; }
int toDisplay(const std::string&, int offset, const Display& d) {
    if (!d) return offset;
    return d->toShown[size_t(std::clamp(offset, 0, int(d->toShown.size()) - 1))];
}
int fromDisplay(const std::string&, int offset, const Display& d) {
    if (!d) return offset;
    return d->toValue[size_t(std::clamp(offset, 0, int(d->toValue.size()) - 1))];
}

// A line of a multi-line field: byte offsets into the shown text, without the line break.
struct FieldLine {
    int start = 0, end = 0;
    bool rtl = false;  // its paragraph goes right to left (each paragraph by its own letters)
};

// Where the text sits inside the field's text area, shared by drawing, taps and caret moves.
struct FieldGeometry {
    float scroll = 0;   // single line: <= 0 when the caret would be past the right edge
    float scrollY = 0;  // multi-line: how far the lines are scrolled up
    std::vector<FieldLine> lines;  // multi-line, as last drawn
    float lineHeight = 0;
    float width = 0;   // multi-line: the lines' area, for their alignment
    float height = 0;  // single line: the text node's
};

// A field's lines: the style's line height (Material's 24 sp for bodyLarge) or the font's own.
float fieldLineHeight(TextMeasurer& tm, const Font& font) {
    return font.lineHeight > 0 ? font.lineHeight * tm.fontScale() : tm.metrics(font).lineHeight;
}

// How far a line `lineWidth` wide starts into a field `width` wide, for its alignment and the
// text's direction (Start is the right edge of Arabic text).
float alignShift(TextAlign align, bool rtl, float width, float lineWidth) {
    return detail::alignedX(align, rtl, width, std::min(lineWidth, width));
}

// Breaks at '\n' and wraps at spaces (or anywhere in a word longer than the line). `rtlOf`
// tells a paragraph's direction (Arabic in a Turkish field is a paragraph of its own way).
std::vector<FieldLine> breakLines(TextMeasurer& tm, std::string_view text, const Font& font, float width,
                                  const std::function<bool(std::string_view)>& rtlOf = {}) {
    std::vector<FieldLine> lines;
    size_t p = 0;
    while (true) {
        size_t newline = text.find('\n', p);
        size_t end = newline == std::string_view::npos ? text.size() : newline;
        bool rtl = rtlOf && rtlOf(text.substr(p, end - p));
        int start = int(p), lastBreak = -1;
        float w = 0;
        for (size_t i = p; i < end;) {
            size_t next = std::min(utf8::nextCluster(text, i), end);
            float cw = tm.measureWidth(text.substr(i, next - i), font);
            if (w + cw > width && int(i) > start) {
                if (lastBreak > start) {  // after the last space, which stays on this line
                    lines.push_back({start, lastBreak, rtl});
                    start = lastBreak;
                    w = tm.measureWidth(text.substr(size_t(start), next - size_t(start)), font);
                    lastBreak = -1;
                } else {
                    lines.push_back({start, int(i), rtl});
                    start = int(i);
                    w = cw;
                }
            } else {
                w += cw;
            }
            if (text[i] == ' ') lastBreak = int(next);
            i = next;
        }
        lines.push_back({start, int(end), rtl});
        if (newline == std::string_view::npos) return lines;
        p = newline + 1;
    }
}

// The line an offset is on: at a wrap the caret goes to the start of the next line.
size_t lineOf(const std::vector<FieldLine>& lines, int offset) {
    size_t i = 0;
    while (i + 1 < lines.size() && lines[i + 1].start <= offset) ++i;
    return i;
}

// The caret's x at a display offset on `line`, from where the line is drawn (right-to-left
// text is measured from its right end; see bidi.hpp).
float caretXOn(TextMeasurer& tm, std::string_view text, const Font& font, const FieldLine& line, int offset, bool rtl) {
    int to = std::clamp(offset, line.start, line.end);
    return detail::caretX(tm, text.substr(size_t(line.start), size_t(line.end - line.start)), font, to - line.start, rtl);
}

// The character boundary on `line` nearest to `x` (from where the line is drawn).
int nearestOffset(TextMeasurer& tm, std::string_view text, const Font& font, const FieldLine& line, float x, bool rtl) {
    int best = line.start;
    float bestDistance = std::abs(caretXOn(tm, text, font, line, line.start, rtl) - x);
    for (size_t i = size_t(line.start); i < size_t(line.end);) {
        i = std::min(utf8::nextCluster(text, i), size_t(line.end));
        float d = std::abs(caretXOn(tm, text, font, line, int(i), rtl) - x);
        if (d < bestDistance) bestDistance = d, best = int(i);
    }
    return best;
}

// A selection handle dragged: the selection follows, with a tick when it reaches another
// character (`ime` is the field's state, updated as the host applies it).
void moveSelection(ComposeHost* host, const TextEditState& ime, int start, int end) {
    int was[2] = {ime.selectionStart, ime.selectionEnd};
    host->setTextSelection(start, end);
    if (ime.selectionStart != was[0] || ime.selectionEnd != was[1]) host->performHapticFeedback(HapticFeedbackType::TextHandleMove);
}

// The display range [from, to) of `line` as spans of x, from where the line is drawn.
std::vector<std::pair<float, float>> rangeOn(TextMeasurer& tm, std::string_view text, const Font& font, const FieldLine& line,
                                             int from, int to, bool rtl) {
    return detail::rangeXs(tm, text.substr(size_t(line.start), size_t(line.end - line.start)), font,
                           std::clamp(from, line.start, line.end) - line.start, std::clamp(to, line.start, line.end) - line.start, rtl);
}

// A field's own undo history (one whose text the app keeps; a TextFieldState has its own), below.
using HistoryApply = std::function<void(const std::string& text, TextRange selection)>;
std::shared_ptr<detail::TextHistory> makeTextHistory();
void setHistoryApply(detail::TextHistory& history, HistoryApply apply);
void recordTyped(detail::TextHistory& history, const std::string& before, TextRange beforeSelection, const std::string& after,
                 TextRange afterSelection);
void undoStep(detail::TextHistory& history, bool redo);

// `state`: a TextFieldState's field, which keeps its selection there too.
void textField(FieldKind kind, const std::string& value, std::function<void(std::string)> onValueChange,
               const Modifier& modifier, TextFieldOptions o, const CallSite& site, std::optional<TextFieldState> state = {},
               std::optional<Obfuscation> secure = {}) {
    ComposableScope scope(site);
    if (o.lineLimits) {
        o.singleLine = o.lineLimits->singleLine;
        o.minLines = o.lineLimits->minHeightInLines;
        o.maxLines = o.lineLimits->maxHeightInLines;
    }
    const ColorScheme& cs = colorScheme();
    const Typography& type = typography();
    Font font = detail::styledFont(o.textStyle, type.bodyLarge);
    TextAlign align = o.textStyle.textAlign;
    TextMeasurer* measurer = &detail::Composer::require().host().textMeasurer();
    FocusManager focus = focusManager();

    // The input method's last report: where the caret is and which word it is still composing.
    auto ime = remember([] { return std::make_shared<TextEditState>(); });
    auto geometry = remember([] { return std::make_shared<FieldGeometry>(); });
    auto focused = remember([] { return mutableStateOf(false); });
    auto touching = remember([] { return mutableStateOf(false); });  // touch selection: handles and toolbar
    auto toolbarShown = remember([] { return mutableStateOf(false); });
    auto selecting = remember([] { return mutableStateOf(false); });  // a selection (two handles) or a caret (one)
    auto handleDragged = remember([] { return mutableStateOf(false); });  // a handle: no toolbar meanwhile
    // What the handles' gestures need, kept across recompositions (a drag outlives them).
    struct HandleState {
        std::function<int(Offset)> offsetAt;
        std::function<Offset(int)> caretPoint;
        Offset origin[3];  // where the start, end and caret handles were put, in the text node
        Offset field;      // the text node's place in the window (for the magnifier)
    };
    auto handles = remember([] { return std::make_shared<HandleState>(); });
    auto menuAt = remember([] { return mutableStateOf(std::optional<Offset>()); });  // its context menu, open there
    ComposeHost* host = &detail::Composer::require().host();
    // The text the app has: onValueChange only reports a different one (not a caret moving),
    // as in Compose.
    auto known = remember([] { return std::make_shared<std::string>(); });
    *known = value;
    // Undone or redone (Ctrl+Z, Ctrl+Shift+Z, Ctrl+Y) in a field whose text the app keeps: the
    // text goes to the app, its selection to the field once the app has the text.
    auto history = remember([] { return makeTextHistory(); });
    auto undone = remember([] { return std::make_shared<std::optional<std::pair<std::string, TextRange>>>(); });
    auto latestChange = remember([] { return std::make_shared<std::function<void(std::string)>>(); });
    *latestChange = onValueChange;
    if (!state)
        setHistoryApply(*history, [undone, latestChange](const std::string& text, TextRange selection) {
            *undone = {text, selection};
            if (*latestChange) (*latestChange)(text);
        });
    if (*undone && (*undone)->first != value) undone->reset();  // the app didn't take it
    bool isFocused = focused.get();
    // The label inside the field (floating up), or above it outside (Above): only one inside
    // takes room in it and covers the placeholder while down.
    bool plainKind = kind == FieldKind::Plain;
    bool labelAbove = !o.label.empty() && !plainKind && o.labelPosition.kind == TextFieldLabelPosition::Kind::Above;
    bool hasLabel = !o.label.empty() && !plainKind && !labelAbove;
    bool alwaysUp = hasLabel && o.labelPosition.alwaysMinimize;
    float p = animateFloatAsState(isFocused || !value.empty() || alwaysUp ? 1.f : 0.f, 150);  // 1: label floated
    // Disabled: no focus (a focused field lets go of it).
    bool enabled = o.enabled;
    SideEffect([enabled, isFocused, focus] {
        if (!enabled && isFocused) focus.clearFocus();
    });
    // Its colors now (Compose's TextFieldColors): what was asked for, else Material 3's;
    // disabled wins, then error, then focus.
    const TextFieldColors& tc = o.colors;
    auto pick = [&](const std::optional<Color>& focusedColor, const std::optional<Color>& unfocusedColor,
                    const std::optional<Color>& disabledColor, const std::optional<Color>& errorColor, Color focusedDefault,
                    Color unfocusedDefault, Color disabledDefault, Color errorDefault) {
        return !enabled    ? disabledColor.value_or(disabledDefault)
               : o.isError ? errorColor.value_or(errorDefault)
               : isFocused ? focusedColor.value_or(focusedDefault)
                           : unfocusedColor.value_or(unfocusedDefault);
    };
    Color quiet = cs.onSurfaceVariant, faded = cs.onSurface.copy(0.38f);
    Color textColor = o.textStyle.color.value_or(pick(tc.focusedTextColor, tc.unfocusedTextColor, tc.disabledTextColor, tc.errorTextColor,
                                                      cs.onSurface, cs.onSurface, faded, cs.onSurface));
    Color fill = kind == FieldKind::Filled ? cs.surfaceContainerHighest : colors::Transparent;
    Color containerColor = pick(tc.focusedContainerColor, tc.unfocusedContainerColor, tc.disabledContainerColor, tc.errorContainerColor,
                                fill, fill, fill, fill);
    Color cursorColor = o.isError ? tc.errorCursorColor.value_or(cs.error) : tc.cursorColor.value_or(cs.primary);
    TextSelectionColors selectionColors = tc.textSelectionColors.value_or(LocalTextSelectionColors.current());
    Color placeholderColor = pick(tc.focusedPlaceholderColor, tc.unfocusedPlaceholderColor, tc.disabledPlaceholderColor,
                                  tc.errorPlaceholderColor, quiet, quiet, faded, quiet);
    Color prefixColor = pick(tc.focusedPrefixColor, tc.unfocusedPrefixColor, tc.disabledPrefixColor, tc.errorPrefixColor, quiet, quiet,
                             quiet.copy(0.38f), quiet);
    Color suffixColor = pick(tc.focusedSuffixColor, tc.unfocusedSuffixColor, tc.disabledSuffixColor, tc.errorSuffixColor, quiet, quiet,
                             quiet.copy(0.38f), quiet);
    Color indicator = animateColorAsState(pick(tc.focusedIndicatorColor, tc.unfocusedIndicatorColor, tc.disabledIndicatorColor,
                                               tc.errorIndicatorColor, cs.primary, kind == FieldKind::Outlined ? cs.outline : quiet,
                                               cs.onSurface.copy(kind == FieldKind::Outlined ? 0.12f : 0.38f), cs.error),
                                          150);
    Color labelColor = pick(tc.focusedLabelColor, tc.unfocusedLabelColor, tc.disabledLabelColor, tc.errorLabelColor, cs.primary, quiet,
                            faded, cs.error);
    float indicatorWidth = isFocused ? 2.f : 1.f;
    Shape shape = o.shape.value_or(kind == FieldKind::Filled ? RoundedCornerShape(4, 4, 0, 0) : RoundedCornerShape(4));
    bool layoutRtl = LocalLayoutDirection.current() == LayoutDirection::Rtl;
    bool plain = kind == FieldKind::Plain;
    float padStart = plain || o.leadingIcon ? 0.f : 16.f, padEnd = plain || o.trailingIcon ? 0.f : 16.f;
    float textStart = o.leadingIcon ? 52.f : 16.f;  // in the field, for the label
    // The prefix and suffix, 2dp from the text, take room from it (taps and the caret follow).
    constexpr float kAffixGap = 2;
    float prefixWidth = o.prefix.empty() ? 0.f : measurer->measureWidth(o.prefix, font) + kAffixGap;
    float suffixWidth = o.suffix.empty() ? 0.f : measurer->measureWidth(o.suffix, font) + kAffixGap;
    padStart += prefixWidth, padEnd += suffixWidth;
    float affixAlpha = hasLabel ? p : 1.f;  // shown with the floated label
    float textShift = kind == FieldKind::Filled && hasLabel ? 8.f : 0.f;  // below the floated label
    Display transform = secure ? (secure->visible ? nullptr : passwordDisplay(value, secure->bullet, secure->revealed))
                        : o.visualTransformation == VisualTransformation::Password ? passwordDisplay(value)
                        : o.outputTransformation && o.outputTransformation->transformOutput
                            ? outputDisplay(value, o.outputTransformation->transformOutput)
                            : nullptr;
    std::string shown = displayText(value, transform);
    // Its direction, from the text (or the placeholder): Arabic starts at the right.
    TextDirection direction = o.textStyle.textDirection;
    if (direction == TextDirection::Content)  // digits only: the layout's way
        direction = LocalLayoutDirection.current() == LayoutDirection::Rtl ? TextDirection::ContentOrRtl : TextDirection::ContentOrLtr;
    bool rtl = detail::isRtl(direction, value.empty() ? std::string_view(o.placeholder) : std::string_view(shown));
    // A multi-line field's paragraphs each go their own way (by their own first letter).
    std::function<bool(std::string_view)> paragraphRtl = [direction](std::string_view paragraph) { return detail::isRtl(direction, paragraph); };
    bool singleLine = o.singleLine;
    int minLines = std::max(1, o.minLines), maxLines = std::max(minLines, o.maxLines);
    // Multi-line: around the lines (a filled field's label sits in its top padding, as in Material 3).
    float padTop = plain ? 0.f : textShift > 0 ? 24.f : 16.f, padBottom = plain ? 0.f : textShift > 0 ? 8.f : 16.f;

    auto focusable = std::make_shared<FocusableElement>();
    focusable->value = value;
    focusable->keyboardOptions = o.keyboardOptions;
    focusable->allowCopy = !secure && o.visualTransformation != VisualTransformation::Password;
    focusable->onContextMenu = [menuAt](Offset at) { menuAt.set(at); };
    focusable->singleLine = singleLine;
    focusable->readOnly = o.readOnly;
    focusable->interactionSource = o.interactionSource;
    if (state) focusable->selection = state->selection();
    else if (*undone) focusable->selection = (*undone)->second;
    if (o.inputTransformation && o.inputTransformation->transformInput)
        focusable->transformEdit = [transform = o.inputTransformation->transformInput](const TextEditState& before, const TextEditState& proposed) {
            TextFieldBuffer buffer(before.text, {before.selectionStart, before.selectionEnd}, proposed.text,
                                   {proposed.selectionStart, proposed.selectionEnd});
            transform(buffer);
            if (!buffer.changed()) return proposed;  // as typed (its composing word too)
            return TextEditState{buffer.text(), buffer.selection().start, buffer.selection().end};
        };
    focusable->onTouchSelection = [touching, toolbarShown](bool on, bool toolbar) {
        touching.set(on);
        toolbarShown.set(on && toolbar);
    };
    focusable->onEdit = [ime, onValueChange, singleLine, readOnly = o.readOnly, value, known, selecting, state, history,
                         undone](const TextEditState& edit) {
        if (readOnly && edit.text != value) return;  // the caret and selection may move, the text stays
        TextRange before = ime->text == *known ? TextRange{ime->selectionStart, ime->selectionEnd} : TextRange{int(known->size()), int(known->size())};
        *ime = edit;
        selecting.set(edit.selectionStart != edit.selectionEnd);
        if (singleLine) std::replace(ime->text.begin(), ime->text.end(), '\n', ' ');  // pasted lines join
        if (state) TextFieldStateAccess::set(*state, ime->text, {ime->selectionStart, ime->selectionEnd});
        if (*undone && (*undone)->first == ime->text) undone->reset();  // the field has the undone text: no step of its own
        if (ime->text == *known) return;
        if (!state) recordTyped(*history, *known, before, ime->text, {ime->selectionStart, ime->selectionEnd});
        *known = ime->text;
        if (onValueChange) onValueChange(ime->text);
    };
    focusable->onImeAction = [actions = o.keyboardActions, onSubmit = o.onSubmit, focus](ImeAction action) {
        auto run = [](const std::function<void()>& f) {
            if (f) f();
            return bool(f);
        };
        switch (action) {
        case ImeAction::Next:
            if (!run(actions.onNext)) focus.moveFocus(FocusDirection::Next);
            break;
        case ImeAction::Previous:
            if (!run(actions.onPrevious)) focus.moveFocus(FocusDirection::Previous);
            break;
        case ImeAction::Go: run(actions.onGo) || run(onSubmit); break;
        case ImeAction::Search: run(actions.onSearch) || run(onSubmit); break;
        case ImeAction::Send: run(actions.onSend) || run(onSubmit); break;
        case ImeAction::Default:
        case ImeAction::Done:
            if (!run(actions.onDone) && !run(onSubmit)) focus.clearFocus();
            break;
        case ImeAction::None: break;
        }
    };
    if (!singleLine)
        focusable->moveCaret = [measurer, font, geometry, value, transform, align](int offset, CaretMove move) {
            const auto& lines = geometry->lines;
            if (lines.empty()) return offset;
            std::string text = displayText(value, transform);
            int d = toDisplay(value, offset, transform);
            size_t line = lineOf(lines, d);
            switch (move) {
            case CaretMove::LineStart: return fromDisplay(value, lines[line].start, transform);
            case CaretMove::LineEnd: return fromDisplay(value, lines[line].end, transform);
            case CaretMove::LineUp:
            case CaretMove::LineDown: {
                bool up = move == CaretMove::LineUp;
                if (up && line == 0) return 0;  // past the first / last line: the text's start / end
                if (!up && line + 1 == lines.size()) return int(value.size());
                const FieldLine& from = lines[line];
                const FieldLine& to = lines[up ? line - 1 : line + 1];
                auto shift = [&](const FieldLine& l) {
                    return alignShift(align, l.rtl, geometry->width - 2,
                                      measurer->measureWidth(std::string_view(text).substr(size_t(l.start), size_t(l.end - l.start)), font));
                };
                float x = caretXOn(*measurer, text, font, from, d, from.rtl) + shift(from) - shift(to);
                return fromDisplay(value, nearestOffset(*measurer, text, font, to, x, to.rtl), transform);
            }
            }
            return offset;
        };
    focusable->offsetAt = [measurer, font, geometry, padStart, padTop, value, transform, singleLine, align, rtl](Offset local) {
        std::string text = displayText(value, transform);
        if (!singleLine && !geometry->lines.empty()) {
            float lh = std::max(1.f, geometry->lineHeight);
            int line = std::clamp(int(std::floor((local.y - padTop + geometry->scrollY) / lh)), 0, int(geometry->lines.size()) - 1);
            const FieldLine& l = geometry->lines[size_t(line)];
            float shift = alignShift(align, l.rtl, geometry->width - 2,
                                     measurer->measureWidth(std::string_view(text).substr(size_t(l.start), size_t(l.end - l.start)), font));
            return fromDisplay(value, nearestOffset(*measurer, text, font, l, local.x - padStart - shift, l.rtl), transform);
        }
        FieldLine all{0, int(text.size())};
        return fromDisplay(value, nearestOffset(*measurer, text, font, all, local.x - padStart - geometry->scroll, rtl), transform);
    };

    float minHeight = plain ? 0.f : 56.f;  // a decorated field is at least 56 dp tall
    auto measureText = [font, singleLine, shown, geometry, padStart, padEnd, padTop, padBottom, minLines, maxLines, plain,
                        minHeight, paragraphRtl, textShift](LayoutNode& node, const Constraints& c) {
        FontMetrics m = node.textMeasurer().metrics(font);
        m.lineHeight = fieldLineHeight(node.textMeasurer(), font);
        if (singleLine) {
            Size size = c.constrain({c.maxWidth, plain ? m.lineHeight : std::max(56.f, m.lineHeight + 32)});
            if (!detail::isProbeMeasure()) geometry->height = size.height;
            // Its baseline, where the text is drawn (alignByBaseline next to a label).
            node.textBaselines[0] = node.textBaselines[1] = (size.height - (m.ascent + m.descent)) / 2 + m.ascent + textShift;
            return size;
        }
        float width = c.hasBoundedWidth() ? c.maxWidth : 280;
        if (!detail::isProbeMeasure()) {  // the lines caret moves and taps use, before anything is drawn
            geometry->lines = breakLines(node.textMeasurer(), shown, font, std::max(1.f, width - padStart - padEnd), paragraphRtl);
            geometry->lineHeight = m.lineHeight;
        }
        size_t count = detail::isProbeMeasure()
                           ? breakLines(node.textMeasurer(), shown, font, std::max(1.f, width - padStart - padEnd)).size()
                           : geometry->lines.size();
        int visible = std::clamp(int(count), minLines, maxLines);
        // The first line's baseline and the last shown one's.
        float first = padTop + (m.lineHeight - (m.ascent + m.descent)) / 2 + m.ascent;
        node.textBaselines[0] = first;
        node.textBaselines[1] = first + float(std::max(1, std::min(int(count), visible)) - 1) * m.lineHeight;
        return c.constrain({width, std::max(minHeight, padTop + float(visible) * m.lineHeight + padBottom)});
    };
    // The prefix and suffix on the (first) line's baseline, at the text area's ends.
    auto drawAffixes = [=, prefix = o.prefix, suffix = o.suffix](GraphicsContext& canvas, const Rect& b, float baseline) {
        if (affixAlpha < 0.01f) return;
        if (!prefix.empty())
            canvas.drawText(prefix, {b.x + padStart - prefixWidth, baseline}, font, prefixColor.copy(prefixColor.a * affixAlpha));
        if (!suffix.empty())
            canvas.drawText(suffix, {b.right() - padEnd + kAffixGap, baseline}, font, suffixColor.copy(suffixColor.a * affixAlpha));
    };
    // Multi-line: lines from the top, the caret's line kept in view.
    auto drawLines = [=, placeholder = o.placeholder](GraphicsContext& canvas, const Rect& b, const LayoutNode& node) {
        bool focusedNow = node.focused;
        TextMeasurer& tm = node.textMeasurer();
        FontMetrics m = tm.metrics(font);
        float lh = fieldLineHeight(tm, font), glyphTop = (lh - (m.ascent + m.descent)) / 2;
        Rect area{b.x + padStart, b.y + padTop, std::max(0.f, b.width - padStart - padEnd),
                  std::max(0.f, b.height - padTop - padBottom)};
        auto lines = breakLines(tm, shown, font, std::max(1.f, area.width), paragraphRtl);
        bool synced = focusedNow && ime->text == value;
        int caret = toDisplay(value, synced ? ime->selectionEnd : int(value.size()), transform);
        size_t caretLine = lineOf(lines, caret);
        float& scrollY = geometry->scrollY;
        if (focusedNow) {
            float caretTop = float(caretLine) * lh;
            if (caretTop < scrollY) scrollY = caretTop;
            if (caretTop + lh > scrollY + area.height) scrollY = caretTop + lh - area.height;
        }
        scrollY = std::clamp(scrollY, 0.f, std::max(0.f, float(lines.size()) * lh - area.height));
        geometry->lines = lines;
        geometry->lineHeight = lh;
        geometry->width = area.width;
        std::string_view text = shown;
        auto shiftOf = [&](const FieldLine& line) {  // where the line starts, from the area's start
            return alignShift(align, line.rtl, area.width - 2, tm.measureWidth(text.substr(size_t(line.start), size_t(line.end - line.start)), font));
        };
        auto xAt = [&](const FieldLine& line, int offset) { return shiftOf(line) + caretXOn(tm, text, font, line, offset, line.rtl); };
        auto lineTop = [&](size_t i) { return area.y + float(i) * lh - scrollY; };
        // Highlights a display range on every line it covers (a selected line break shows a little).
        auto highlight = [&](int from, int to, auto&& paint) {
            for (size_t i = 0; i < lines.size(); ++i) {
                const FieldLine& line = lines[i];
                if (to < line.start || from > line.end || (from == to)) continue;
                float shift = shiftOf(line);
                for (auto [x0, x1] : rangeOn(tm, text, font, line, from, to, line.rtl)) paint(area.x + shift + x0, lineTop(i), x1 - x0);
                if (to > line.end) {  // the line break, at the line's end
                    float w = tm.measureWidth(text.substr(size_t(line.start), size_t(line.end - line.start)), font);
                    paint(area.x + shift + (line.rtl ? -4.f : w), lineTop(i), 4.f);
                }
            }
        };
        canvas.pushClip({b.x, area.y, b.width, area.height});
        if (synced && ime->selectionStart != ime->selectionEnd)
            highlight(toDisplay(value, std::min(ime->selectionStart, ime->selectionEnd), transform),
                      toDisplay(value, std::max(ime->selectionStart, ime->selectionEnd), transform),
                      [&](float x, float y, float w) { canvas.fillRoundRect({x, y, w, lh}, 0, selectionColors.backgroundColor); });
        if (value.empty()) {
            if (!hasLabel || focusedNow || alwaysUp)
                canvas.drawText(placeholder,
                                {area.x + alignShift(align, rtl, area.width - 2, tm.measureWidth(placeholder, font)), area.y + glyphTop + m.ascent},
                                font, placeholderColor, rtl);
        } else {
            for (size_t i = 0; i < lines.size(); ++i) {
                float top = lineTop(i);
                if (top + lh < area.y || top > area.bottom()) continue;
                const FieldLine& line = lines[i];
                canvas.drawText(text.substr(size_t(line.start), size_t(line.end - line.start)),
                                {area.x + shiftOf(line), top + glyphTop + m.ascent}, font, textColor, line.rtl);
            }
        }
        if (synced && ime->compositionStart >= 0)  // the word being composed, underlined like Android
            highlight(toDisplay(value, ime->compositionStart, transform), toDisplay(value, ime->compositionEnd, transform),
                      [&](float x, float y, float w) {
                          canvas.drawLine({x, y + glyphTop + m.ascent + 3}, {x + w, y + glyphTop + m.ascent + 3}, 1.5f, textColor);
                      });
        bool selecting = synced && ime->selectionStart != ime->selectionEnd;  // no caret over a selection
        if (focusedNow && !o.readOnly && !selecting) {
            float x = area.x + xAt(lines[caretLine], caret) + 1, y = lineTop(caretLine) + glyphTop;
            canvas.drawLine({x, y}, {x, y + m.ascent + m.descent}, 2, cursorColor);
        }
        canvas.popClip();
        drawAffixes(canvas, b, area.y + glyphTop + m.ascent);  // they stay as the lines scroll
    };
    auto drawText = [=, placeholder = o.placeholder](GraphicsContext& canvas, const Rect& b, const LayoutNode& node) {
        if (!singleLine) return drawLines(canvas, b, node);
        bool focusedNow = node.focused;
        Rect inner{b.x + padStart, b.y, std::max(0.f, b.width - padStart - padEnd), b.height};
        TextMeasurer& tm = node.textMeasurer();
        FontMetrics m = tm.metrics(font);
        float baseline = b.y + (b.height - (m.ascent + m.descent)) / 2 + m.ascent + textShift;
        // Caret, selection and composition come from the IME while its text is what we show.
        bool synced = focusedNow && ime->text == value;
        auto widthTo = [&](int offset) { return detail::caretX(tm, shown, font, toDisplay(value, offset, transform), rtl); };
        auto spans = [&](int from, int to) {
            return detail::rangeXs(tm, shown, font, toDisplay(value, from, transform), toDisplay(value, to, transform), rtl);
        };
        float caretOffset = widthTo(synced ? ime->selectionEnd : int(value.size()));
        float shift = alignShift(align, rtl, inner.width - 2, tm.measureWidth(value.empty() ? std::string_view(placeholder) : shown, font));
        geometry->scroll = std::min(0.f, inner.width - caretOffset - 2) + shift;  // keep the caret visible
        float x = inner.x + geometry->scroll;
        canvas.pushClip(inner);
        if (synced && ime->selectionStart != ime->selectionEnd)
            for (auto [a, e] : spans(std::min(ime->selectionStart, ime->selectionEnd), std::max(ime->selectionStart, ime->selectionEnd)))
                canvas.fillRoundRect({x + a, baseline - m.ascent, e - a, m.ascent + m.descent}, 0, selectionColors.backgroundColor);
        if (value.empty()) {
            if (!hasLabel || focusedNow || alwaysUp) canvas.drawText(placeholder, {x, baseline}, font, placeholderColor, rtl);
        } else {
            canvas.drawText(shown, {x, baseline}, font, textColor, rtl);
        }
        if (synced && ime->compositionStart >= 0)  // underline the word being composed, like Android
            for (auto [a, e] : spans(ime->compositionStart, ime->compositionEnd))
                canvas.drawLine({x + a, baseline + 3}, {x + e, baseline + 3}, 1.5f, textColor);
        if (focusedNow && !o.readOnly && !(synced && ime->selectionStart != ime->selectionEnd))  // no caret over a selection
            canvas.drawLine({x + caretOffset + 1, baseline - m.ascent}, {x + caretOffset + 1, baseline + m.descent}, 2, cursorColor);
        canvas.popClip();
        drawAffixes(canvas, b, baseline);
    };

    // Touch selection: where the caret at `offset` stands in the text node (its x, and the
    // bottom of its line), for the handles and the toolbar.
    auto caretPoint = [measurer, font, geometry, padStart, padTop, value, transform, singleLine, align, textShift, rtl](int offset) {
        std::string text = displayText(value, transform);
        std::string_view view = text;
        int d = std::clamp(toDisplay(value, offset, transform), 0, int(text.size()));
        FontMetrics m = measurer->metrics(font);
        if (!singleLine && !geometry->lines.empty()) {
            const auto& lines = geometry->lines;
            size_t line = lineOf(lines, d);
            const FieldLine& l = lines[line];
            float shift = alignShift(align, l.rtl, geometry->width - 2,
                                     measurer->measureWidth(view.substr(size_t(l.start), size_t(l.end - l.start)), font));
            return Offset{padStart + shift + caretXOn(*measurer, view, font, l, d, l.rtl),
                          padTop + float(line + 1) * geometry->lineHeight - geometry->scrollY};
        }
        float baseline = (geometry->height - (m.ascent + m.descent)) / 2 + m.ascent + textShift;
        return Offset{padStart + geometry->scroll + detail::caretX(*measurer, view, font, d, rtl), baseline + m.descent};
    };
    handles->offsetAt = focusable->offsetAt;
    handles->caretPoint = caretPoint;
    // A teardrop handle hanging under one end of the selection; dragging it moves that end to
    // the text over the finger (the ends don't pass each other).
    float lineHeight = fieldLineHeight(*measurer, font);
    Color handleColor = selectionColors.handleColor;
    auto ends = [ime] {
        return std::pair{std::min(ime->selectionStart, ime->selectionEnd), std::max(ime->selectionStart, ime->selectionEnd)};
    };
    auto fieldSelectionHandle = [=](bool start) {
        selectionHandle(start, handleColor, host, handleDragged, {
            .position = [handles, start, ends](const Rect& field) {
                handles->field = {field.x, field.y};
                Offset c = handles->caretPoint(start ? ends().first : ends().second);
                Offset& origin = handles->origin[start ? 0 : 1];
                origin = {c.x - (start ? kHandleSize : 0), c.y};
                return Offset{field.x + origin.x, field.y + origin.y};
            },
            .grab = [handles, start, ends, lineHeight](Offset pressed) {
                const Offset& origin = handles->origin[start ? 0 : 1];
                Offset caret = handles->caretPoint(start ? ends().first : ends().second);
                return Offset{caret.x - (origin.x + pressed.x), caret.y - lineHeight / 2 - (origin.y + pressed.y)};
            },
            .drag = [host, ime, handles, start, ends, lineHeight](Offset to) {
                const Offset& origin = handles->origin[start ? 0 : 1];
                Offset at{origin.x + to.x, origin.y + to.y};
                int offset = handles->offsetAt ? handles->offsetAt(at) : 0;
                auto [a, b] = ends();
                if (start)
                    moveSelection(host, *ime, std::min(offset, b), b);
                else
                    moveSelection(host, *ime, a, std::max(offset, a));
                auto [from, until] = ends();  // the magnifier on the moved end's line
                Offset c = handles->caretPoint(start ? from : until);
                host->showTextMagnifier({handles->field.x + at.x, handles->field.y + c.y - lineHeight / 2});
            }});
    };
    // Under a caret a finger put: drag it to move the caret, tap it for the toolbar.
    auto caretHandle = [=] {
        constexpr float kSize = 22;
        Popup({.position = [ime, handles](const Rect& field, Size, Size) {
                   handles->field = {field.x, field.y};
                   Offset c = handles->caretPoint(ime->selectionEnd);
                   handles->origin[2] = {c.x - kSize / 2, c.y};
                   return Offset{field.x + handles->origin[2].x, field.y + handles->origin[2].y};
               },
               .focusable = false,
               .touchable = true},
              [&] {
                  Box(Modifier()
                          .size(kSize)
                          .drawBehind([handleColor](DrawScope& d) {  // a teardrop pointing up at the caret
                              Path tip;
                              tip.moveTo(kSize / 2, 1);
                              tip.lineTo(kSize / 2 + 6.5f, 9);
                              tip.lineTo(kSize / 2 - 6.5f, 9);
                              tip.close();
                              d.drawPath(tip, handleColor);
                              d.drawCircle(handleColor, 8.f, Offset{kSize / 2, 13});
                          })
                          .pointerInput(0, [host, ime, handles, lineHeight, toolbarShown, handleDragged](PointerInputScope& s) -> Task {
                              const Offset& origin = handles->origin[2];
                              while (true) {
                                  PointerEvent down = co_await s.awaitPointerEvent();
                                  if (down.type != PointerEventType::Press) continue;
                                  down.change.consume();
                                  Offset finger{origin.x + down.change.position.x, origin.y + down.change.position.y};
                                  Offset caret = handles->caretPoint(ime->selectionEnd);
                                  Offset grip{caret.x - finger.x, caret.y - lineHeight / 2 - finger.y};
                                  float travelled = 0;
                                  TextMagnifierShown shown{host, handleDragged};
                                  while (true) {
                                      PointerEvent e = co_await s.awaitPointerEvent();
                                      if (e.type == PointerEventType::Cancel) break;
                                      if (e.type == PointerEventType::Release) {
                                          if (travelled <= s.viewConfiguration().touchSlop) toolbarShown.set(!toolbarShown.peek());  // a tap
                                          break;
                                      }
                                      if (e.type != PointerEventType::Move) continue;
                                      e.change.consume();
                                      Offset d = e.change.positionChange();
                                      travelled += std::hypot(d.x, d.y);
                                      if (travelled <= s.viewConfiguration().touchSlop) continue;
                                      toolbarShown.set(false);
                                      Offset at{origin.x + e.change.position.x + grip.x, origin.y + e.change.position.y + grip.y};
                                      int offset = handles->offsetAt ? handles->offsetAt(at) : 0;
                                      moveSelection(host, *ime, offset, offset);
                                      Offset c = handles->caretPoint(offset);
                                      host->showTextMagnifier({handles->field.x + at.x, handles->field.y + c.y - lineHeight / 2});
                                  }
                              }
                          }));
              });
    };
    // Cut, copy, paste and select all, over the selection.
    auto selectionToolbar = [=] {
        if (auto custom = LocalTextToolbar.current()) {  // the app's own: told what it may offer, and where
            bool selected = ime->selectionStart != ime->selectionEnd;
            bool canCopy = selected && focusable->allowCopy, canEdit = !o.readOnly;
            auto act = [host](ComposeHost::TextAction a) { return std::function<void()>([host, a] { host->textAction(a); }); };
            detail::customTextToolbar(
                custom,
                [ime, caretPoint, lineHeight](const Rect& field) {
                    int a = std::min(ime->selectionStart, ime->selectionEnd), b = std::max(ime->selectionStart, ime->selectionEnd);
                    Offset from = caretPoint(a), to = caretPoint(b);
                    float left = std::min(from.x, to.x), right = std::max(from.x, to.x);
                    return Rect{field.x + left, field.y + from.y - lineHeight, right - left, to.y - from.y + lineHeight};
                },
                canCopy ? act(ComposeHost::TextAction::Copy) : nullptr, canEdit ? act(ComposeHost::TextAction::Paste) : nullptr,
                canCopy && canEdit ? act(ComposeHost::TextAction::Cut) : nullptr, act(ComposeHost::TextAction::SelectAll));
            return;
        }
        Popup({.position = [ime, caretPoint, lineHeight](const Rect& field, Size size, Size viewport) {
                   int a = std::min(ime->selectionStart, ime->selectionEnd), b = std::max(ime->selectionStart, ime->selectionEnd);
                   Offset from = caretPoint(a), to = caretPoint(b);
                   float x = field.x + (from.x + to.x) / 2 - size.width / 2;
                   float y = field.y + from.y - lineHeight - 8 - size.height;
                   if (y < 0) y = field.y + to.y + 30;  // no room above: under the handles
                   return Offset{std::clamp(x, 8.f, std::max(8.f, viewport.width - size.width - 8)), y};
               },
               .focusable = false,
               .touchable = true},
              [&] {
                  bool selected = ime->selectionStart != ime->selectionEnd;
                  bool canCopy = selected && focusable->allowCopy, canEdit = !o.readOnly;
                  const Strings& strings = LocalStrings.current();
                  Row(Modifier().shadow(3, 8).background(cs.surfaceContainer, 8), [&] {
                      auto action = [&](const std::string& label, ComposeHost::TextAction a) {
                          TextButton([host, a] { host->textAction(a); }, [label] { Text(label); });
                      };
                      if (canCopy && canEdit) action(strings.cut, ComposeHost::TextAction::Cut);
                      if (canCopy) action(strings.copy, ComposeHost::TextAction::Copy);
                      if (canEdit) action(strings.paste, ComposeHost::TextAction::Paste);
                      action(strings.selectAll, ComposeHost::TextAction::SelectAll);
                  });
              });
    };
    bool showSelection = touching.get() && isFocused;

    // The container (outline or fill, indicator line) and the label, around the row.
    auto decoration = [=, label = o.label](ContentDrawScope& d) {
        Size s = d.size();
        float h = s.height;
        Font small{lerp(16, 12, p)};
        float labelWidth = hasLabel ? d.measureText(label, Font{12}).width : 0;
        d.drawContent();
        if (kind == FieldKind::Filled) {
            d.drawRect(indicator, {0, h - indicatorWidth}, Size{s.width, indicatorWidth});
        } else if (shape.pathed()) {  // a shape of its own: all of it, no gap
            d.drawPath(shape.outlinePath({0, 0, s.width, h}, layoutRtl), indicator, Stroke(indicatorWidth));
        } else {
            // The outline along the shape's corners, with a gap for the floated label.
            float half = indicatorWidth / 2, x0 = half, y0 = half, x1 = s.width - half, y1 = h - half;
            CornerRadii r = shape.radiiFor({0, 0, s.width, h}, layoutRtl);
            float gapStart = 12, gapEnd = 12 + (labelWidth + 8) * p;
            bool gap = hasLabel && p > 0.01f;
            Path outline;
            outline.moveTo(gap ? gapEnd : x0 + r.topLeft, y0);
            outline.lineTo(x1 - r.topRight, y0);
            outline.quadraticTo(x1, y0, x1, y0 + r.topRight);
            outline.lineTo(x1, y1 - r.bottomRight);
            outline.quadraticTo(x1, y1, x1 - r.bottomRight, y1);
            outline.lineTo(x0 + r.bottomLeft, y1);
            outline.quadraticTo(x0, y1, x0, y1 - r.bottomLeft);
            outline.lineTo(x0, y0 + r.topLeft);
            outline.quadraticTo(x0, y0, x0 + r.topLeft, y0);
            if (gap)
                outline.lineTo(gapStart, y0);
            else
                outline.close();
            d.drawPath(outline, indicator, Stroke(indicatorWidth));
        }
        if (hasLabel) {
            float rest = (std::min(h, 56.f) - d.measureText(label, Font{16}).height) / 2;  // on the first line
            float floatedY = kind == FieldKind::Outlined ? -d.measureText(label, Font{12}).height / 2 : 8.f;
            float floatedX = kind == FieldKind::Outlined ? 16.f : textStart;
            d.drawText(label, {lerp(textStart, floatedX, p), lerp(rest, floatedY, p)}, small, labelColor);
        }
    };

    Color leadingIconColor = pick(tc.focusedLeadingIconColor, tc.unfocusedLeadingIconColor, tc.disabledLeadingIconColor,
                                  tc.errorLeadingIconColor, quiet, quiet, faded, quiet);
    Color trailingIconColor = pick(tc.focusedTrailingIconColor, tc.unfocusedTrailingIconColor, tc.disabledTrailingIconColor,
                                   tc.errorTrailingIconColor, quiet, quiet, faded, cs.error);
    Color supportingColor = pick(tc.focusedSupportingTextColor, tc.unfocusedSupportingTextColor, tc.disabledSupportingTextColor,
                                 tc.errorSupportingTextColor, quiet, quiet, faded, cs.error);
    auto container = [](LayoutNode& node, const Constraints& c) {  // 280 dp unless told otherwise, then stacked
        float w = std::clamp(280.f, c.minWidth, c.maxWidth), y = 0;
        for (auto* child : node.children) {
            Size s = child->measure({w, w, 0, kInfinity});
            child->place(0, y);
            y += s.height;
        }
        return c.constrain({w, y});
    };
    // An outlined field's label floats half above its outline: room for it on top, inside the
    // field (Compose's minimizedLabelHalfHeight, half of bodySmall's line).
    Modifier outer = kind == FieldKind::Outlined && hasLabel ? modifier.padding(0, type.bodySmall.lineHeight / 2, 0, 0) : modifier;
    layoutNode(CallSite::current(), outer, container, [&] {
        if (labelAbove) {  // over the field, outside it (Compose's AboveLabel paddings)
            float bias = o.labelPosition.alignment.bias;
            Text(o.label, Modifier().fillMaxWidth().padding(4, 0, 4, 4),
                 {.font = type.bodySmall, .color = labelColor, .textAlign = bias < 0 ? TextAlign::Start : bias > 0 ? TextAlign::End : TextAlign::Center});
        }
        // The text itself (Compose's innerTextField): between the icons, or where a decorator puts it.
        auto innerTextField = [&] {
            layoutNode(CallSite::current(),
                       Modifier()
                           .weight(1)
                           .semantics(o.label.empty() ? o.placeholder : o.label)  // what accessibility reads for it
                           .onFocusChanged([focused](FocusState s) { focused.set(s.isFocused); })
                           .then(!o.readOnly ? Modifier().onPreviewKeyEvent([undo = state ? std::optional(state->undoState()) : std::nullopt,
                                                                              history](const KeyEvent& e) {
                               if (e.type != KeyEventType::KeyDown || !e.modifiers.ctrl) return false;
                               bool redo = e.key == Key::Y || (e.key == Key::Z && e.modifiers.shift);
                               if (e.key != Key::Z && !redo) return false;
                               if (undo) redo ? undo->redo() : undo->undo();
                               else undoStep(*history, redo);
                               return true;
                           }) : Modifier())
                           .then(enabled ? Modifier().then(focusable) : Modifier()),
                       measureText,
                       [&] {
                           if (auto at = menuAt.get()) {  // a right click's menu: what the selection allows
                               bool selected = ime->selectionStart != ime->selectionEnd;
                               bool canCopy = selected && focusable->allowCopy, canEdit = !o.readOnly;
                               auto act = [host](ComposeHost::TextAction a) { return [host, a] { host->textAction(a); }; };
                               const Strings& strings = LocalStrings.current();
                               contextMenuPopup(*at,
                                                {{strings.cut, act(ComposeHost::TextAction::Cut), canCopy && canEdit},
                                                 {strings.copy, act(ComposeHost::TextAction::Copy), canCopy},
                                                 {strings.paste, act(ComposeHost::TextAction::Paste), canEdit},
                                                 {strings.selectAll, act(ComposeHost::TextAction::SelectAll)}},
                                                [menuAt] { menuAt.set(std::nullopt); });
                           }
                           if (!showSelection) return;
                           if (selecting.get()) {
                               key(0, [&] { fieldSelectionHandle(true); });  // the same composables twice: keyed apart
                               key(1, [&] { fieldSelectionHandle(false); });
                           } else {
                               caretHandle();
                           }
                           if ((toolbarShown.get() || selecting.get()) && !handleDragged.get()) selectionToolbar();
                       },
                       drawText, shown);
        };
        if (plain && o.decorator) {
            Box(Modifier().then(std::make_shared<TextFieldDecorationElement>()), [&] { o.decorator(innerTextField); });
        } else {
            Row((plain ? Modifier()
                        : Modifier()
                              .heightIn(56)
                              .then(containerColor.a > 0 ? Modifier().background(containerColor, shape) : Modifier())
                              .drawWithContent(decoration)).then(std::make_shared<TextFieldDecorationElement>()),
                {.verticalAlignment = Alignment::CenterVertically},
                [&] {
                if (o.leadingIcon)
                    Box(Modifier().padding(12, 0, 16, 0), [&] {
                        CompositionLocalProvider({LocalContentColor.provides(leadingIconColor)}, o.leadingIcon);
                    });
                innerTextField();
                if (o.trailingIcon)
                    Box(Modifier().padding(16, 0, 12, 0), [&] {
                        CompositionLocalProvider({LocalContentColor.provides(trailingIconColor)}, o.trailingIcon);
                    });
            });
        }
        if (!o.supportingText.empty())
            Text(o.supportingText, Modifier().padding(16, 4, 16, 0),
                 {.font = type.bodySmall, .color = supportingColor});
    });
}

} // namespace

void TextField(const std::string& value, std::function<void(std::string)> onValueChange, const Modifier& modifier,
               TextFieldOptions options, CallSite site) {
    textField(FieldKind::Filled, value, std::move(onValueChange), modifier, std::move(options), site);
}

void OutlinedTextField(const std::string& value, std::function<void(std::string)> onValueChange,
                       const Modifier& modifier, TextFieldOptions options, CallSite site) {
    textField(FieldKind::Outlined, value, std::move(onValueChange), modifier, std::move(options), site);
}

void BasicTextField(const std::string& value, std::function<void(std::string)> onValueChange, const Modifier& modifier,
                    TextFieldOptions options, CallSite site) {
    textField(FieldKind::Plain, value, std::move(onValueChange), modifier, std::move(options), site);
}

// ---- TextFieldBuffer, InputTransformation, TextFieldState --------------------------------

TextFieldBuffer::TextFieldBuffer(std::string text, TextRange selection)
    : original_(text), text_(std::move(text)), originalSelection_(selection) {
    setSelection(selection);
    originalSelection_ = selection_;
    changed_ = false;
}

TextFieldBuffer::TextFieldBuffer(std::string original, TextRange originalSelection, std::string text, TextRange selection)
    : original_(std::move(original)), text_(std::move(text)), originalSelection_(originalSelection) {
    setSelection(selection);
    changed_ = false;
}

void TextFieldBuffer::setSelection(TextRange selection) {
    int n = length();
    TextRange clamped{std::clamp(selection.start, 0, n), std::clamp(selection.end, 0, n)};
    if (clamped == selection_) return;
    selection_ = clamped;
    changed_ = true;
}

void TextFieldBuffer::replace(int start, int end, std::string_view text) {
    int n = length();
    start = std::clamp(start, 0, n), end = std::clamp(end, start, n);
    if (tracking_) {  // what comes in belongs where the replaced text (or the insertion point) was
        auto& o = tracking_->origins;
        int at = start < int(o.size()) ? o[size_t(start)].offset : tracking_->originalLength;
        o.erase(o.begin() + start, o.begin() + end);
        o.insert(o.begin() + start, text.size(), Origin{at, true});
    }
    text_.replace(size_t(start), size_t(end - start), text);
    int inserted = int(text.size());
    auto moved = [&](int offset) {  // after the range: with the text; inside it: to its end
        return offset <= start ? offset : offset >= end ? offset + inserted - (end - start) : start + inserted;
    };
    selection_ = {moved(selection_.start), moved(selection_.end)};
    changed_ = true;
}

void TextFieldBuffer::revertAllChanges() {
    text_ = original_;
    selection_ = originalSelection_;
    changed_ = true;
    if (tracking_) {
        tracking_->origins.clear();
        for (int i = 0; i < length(); ++i) tracking_->origins.push_back({i, false});
    }
}

namespace {

size_t codePoints(std::string_view s) {
    size_t count = 0;
    for (size_t i = 0; i < s.size(); ++count) utf8::next(s, i);
    return count;
}

// The capital of a letter: Latin (with Latin-1 and Extended-A), Greek and Cyrillic; Turkish
// and Azerbaijani dot their i.
char32_t toUpper(char32_t c, bool turkic) {
    if (turkic && c == U'i') return U'İ';
    if (c == U'ı') return U'I';
    if (c >= U'a' && c <= U'z') return c - 32;
    if ((c >= 0xE0 && c <= 0xFE && c != 0xF7)) return c - 32;  // à..þ, not ÷
    if (c == 0xFF) return 0x178;                                 // ÿ
    if (c >= 0x100 && c <= 0x17F && c != 0x131 && c != 0x138 && c != 0x149 && c != 0x17F) {
        bool oddLower = (c >= 0x139 && c <= 0x148) || (c >= 0x179 && c <= 0x17E);  // Ĺ..ň, Ź..ž pair odd/even
        if (oddLower ? (c % 2 == 0) : (c % 2 == 1)) return c - 1;
        return c;
    }
    if (c >= 0x3B1 && c <= 0x3C9 && c != 0x3C2) return c - 32;  // α..ω (not final ς)
    if (c == 0x3C2) return 0x3A3;
    if (c >= 0x430 && c <= 0x44F) return c - 32;  // а..я
    if (c >= 0x450 && c <= 0x45F) return c - 80;  // ѐ..џ
    return c;
}

std::string upper(std::string_view s, bool turkic) {
    std::string out;
    for (size_t i = 0; i < s.size();) utf8::append(out, toUpper(utf8::next(s, i), turkic));
    return out;
}

} // namespace

InputTransformation InputTransformation::maxLength(int characters) {
    return {[characters](TextFieldBuffer& b) {
        if (codePoints(b.text()) > size_t(std::max(0, characters))) b.revertAllChanges();
    }};
}

InputTransformation InputTransformation::allCaps(std::string language) {
    bool turkic = language == "tr" || language == "az";
    return {[turkic](TextFieldBuffer& b) {
        // Only what the edit brought: the text between what it kept at either end.
        const std::string &was = b.originalText(), &now = b.text();
        auto continuation = [&](size_t i) { return i < now.size() && (static_cast<unsigned char>(now[i]) & 0xC0) == 0x80; };
        size_t head = 0;
        while (head < was.size() && head < now.size() && was[head] == now[head]) ++head;
        while (head > 0 && continuation(head)) --head;  // whole characters (before the tail is measured from it)
        size_t tail = 0;
        while (tail < was.size() - head && tail < now.size() - head && was[was.size() - 1 - tail] == now[now.size() - 1 - tail]) ++tail;
        while (tail > 0 && continuation(now.size() - tail)) --tail;
        std::string_view added = std::string_view(now).substr(head, now.size() - head - tail);
        std::string capitals = upper(added, turkic);
        if (capitals != added) b.replace(int(head), int(head + added.size()), capitals);
    }};
}

InputTransformation InputTransformation::byValue(std::function<std::string(const std::string&, const std::string&)> transform) {
    return {[transform = std::move(transform)](TextFieldBuffer& b) {
        std::string kept = transform(b.originalText(), b.text());
        if (kept == b.text()) return;
        if (kept == b.originalText()) return b.revertAllChanges();
        b.replace(0, b.length(), kept);
        b.placeCursorAtEnd();
    }};
}

InputTransformation InputTransformation::then(InputTransformation next) const {
    return {[first = transformInput, next = std::move(next.transformInput)](TextFieldBuffer& b) {
        if (first) first(b);
        if (next) next(b);
    }};
}

// The steps that can be undone and redone. A typed step merges into the one before it while it
// goes on from there: letters after letters (a space after a word starts a new step, a line
// break is one of its own), deletions next to deletions.
struct detail::TextHistory {
    enum class Kind { Insert, Delete, Other };
    struct Step {
        std::string before, after;
        TextRange beforeSelection, afterSelection;
        Kind kind;
        int start, length;  // what was inserted or deleted, in the text after / before it
        bool typed;
    };
    static constexpr size_t kLimit = 100;
    std::vector<Step> undo, redo;
    MutableState<int> version = mutableStateOf(0);
    std::optional<MutableState<std::string>> text;  // the state's own (copies share them)
    std::optional<MutableState<TextRange>> selection;
    std::function<void(const std::string&, TextRange)> applyTo;  // else: a field's own (the app keeps the text)

    void record(const std::string& before, TextRange beforeSelection, const std::string& after, TextRange afterSelection, bool typed) {
        size_t head = 0;
        while (head < before.size() && head < after.size() && before[head] == after[head]) ++head;
        auto continuation = [](const std::string& s, size_t i) { return i < s.size() && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80; };
        while (head > 0 && (continuation(after, head) || continuation(before, head))) --head;
        size_t tail = 0;
        while (tail < before.size() - head && tail < after.size() - head && before[before.size() - 1 - tail] == after[after.size() - 1 - tail]) ++tail;
        int removed = int(before.size() - head - tail), inserted = int(after.size() - head - tail);
        Kind kind = removed == 0 ? Kind::Insert : inserted == 0 ? Kind::Delete : Kind::Other;
        Step step{before, after, beforeSelection, afterSelection, kind, int(head), kind == Kind::Delete ? removed : inserted, typed};
        if (typed && !undo.empty() && merge(undo.back(), step)) {
            redo.clear();
            version.set(version.peek() + 1);
            return;
        }
        undo.push_back(std::move(step));
        if (undo.size() > kLimit) undo.erase(undo.begin());
        redo.clear();
        version.set(version.peek() + 1);
    }

    static bool merge(Step& last, const Step& next) {
        if (!last.typed || last.kind != next.kind) return false;
        if (next.kind == Kind::Insert) {
            std::string_view added = std::string_view(next.after).substr(size_t(next.start), size_t(next.length));
            std::string_view had = std::string_view(last.after).substr(size_t(last.start), size_t(last.length));
            bool wordEnds = !added.empty() && added.front() == ' ' && !had.empty() && had.back() != ' ';
            if (next.start != last.start + last.length || added.find('\n') != added.npos || had.find('\n') != had.npos || wordEnds)
                return false;
            last.length += next.length;
        } else if (next.kind == Kind::Delete) {
            if (next.start + next.length == last.start) last.start = next.start;  // Backspace, Backspace
            else if (next.start != last.start) return false;                     // Delete, Delete
            last.length += next.length;
        } else {
            return false;
        }
        last.after = next.after;
        last.afterSelection = next.afterSelection;
        return true;
    }

    void apply(const std::string& t, TextRange s) {  // undone or redone: not a step itself
        if (text) {
            if (t != text->peek()) text->set(t);
            if (!(s == selection->peek())) selection->set(s);
        } else if (applyTo) {
            applyTo(t, s);
        }
        version.set(version.peek() + 1);
    }
};

namespace {
std::shared_ptr<detail::TextHistory> makeTextHistory() { return std::make_shared<detail::TextHistory>(); }
void setHistoryApply(detail::TextHistory& history, HistoryApply apply) { history.applyTo = std::move(apply); }
void recordTyped(detail::TextHistory& history, const std::string& before, TextRange beforeSelection, const std::string& after,
                 TextRange afterSelection) {
    history.record(before, beforeSelection, after, afterSelection, true);
}
void undoStep(detail::TextHistory& h, bool redo) {
    auto& from = redo ? h.redo : h.undo;
    auto& to = redo ? h.undo : h.redo;
    if (from.empty()) return;
    to.push_back(std::move(from.back()));
    from.pop_back();
    const auto& step = to.back();
    h.apply(redo ? step.after : step.before, redo ? step.afterSelection : step.beforeSelection);
}
} // namespace

bool UndoState::canUndo() const { return history_->version.get(), !history_->undo.empty(); }
bool UndoState::canRedo() const { return history_->version.get(), !history_->redo.empty(); }
void UndoState::undo() const { undoStep(*history_, false); }
void UndoState::redo() const { undoStep(*history_, true); }
void UndoState::clearHistory() const {
    history_->undo.clear();
    history_->redo.clear();
    history_->version.set(history_->version.peek() + 1);
}

TextFieldState::TextFieldState(std::string initialText)
    : text_(mutableStateOf(initialText)), selection_(mutableStateOf(TextRange{int(initialText.size()), int(initialText.size())})),
      history_(std::make_shared<detail::TextHistory>()) {
    history_->text = text_;
    history_->selection = selection_;
}

std::string TextFieldState::text() const { return text_.get(); }
TextRange TextFieldState::selection() const { return selection_.get(); }

void TextFieldState::set(std::string text, TextRange selection, Source source) const {
    if (text != text_.peek()) {
        history_->record(text_.peek(), selection_.peek(), text, selection, source == Source::User);
        text_.set(std::move(text));
    }
    if (!(selection == selection_.peek())) selection_.set(selection);
}

void TextFieldState::edit(const std::function<void(TextFieldBuffer&)>& change) const {
    TextFieldBuffer buffer(text_.peek(), selection_.peek());
    change(buffer);
    set(buffer.text(), buffer.selection());
}

// The field puts the caret at the end of text it didn't type itself.
void TextFieldState::setTextAndPlaceCursorAtEnd(std::string text) const {
    int end = int(text.size());
    set(std::move(text), {end, end});
}

void TextFieldState::setTextAndSelectAll(std::string text) const {
    int end = int(text.size());
    set(std::move(text), {0, end});
}

TextFieldState rememberTextFieldState(std::string initialText, CallSite site) {
    return rememberSaveable([&] { return TextFieldState(std::move(initialText)); }, site);
}

void TextField(const TextFieldState& state, const Modifier& modifier, TextFieldOptions options, CallSite site) {
    textField(FieldKind::Filled, state.text(), {}, modifier, std::move(options), site, state);
}

void OutlinedTextField(const TextFieldState& state, const Modifier& modifier, TextFieldOptions options, CallSite site) {
    textField(FieldKind::Outlined, state.text(), {}, modifier, std::move(options), site, state);
}

void BasicTextField(const TextFieldState& state, const Modifier& modifier, TextFieldOptions options, CallSite site) {
    textField(FieldKind::Plain, state.text(), {}, modifier, std::move(options), site, state);
}

namespace {

// Whether an edit typed exactly one character into the text (nothing replaced, nothing else
// changed): where it starts, or -1.
int typedCharacter(const TextFieldBuffer& b) {
    const std::string &before = b.originalText(), &after = b.text();
    TextRange caret = b.originalSelection();
    if (!caret.collapsed() || after.size() <= before.size() || size_t(caret.start) > before.size()) return -1;
    size_t at = size_t(caret.start), added = after.size() - before.size();
    bool inserted = after.compare(0, at, before, 0, at) == 0 && after.compare(at + added, std::string::npos, before, at) == 0;
    return inserted && utf8::nextCluster(after, at) == at + added ? int(at) : -1;
}

void secureTextField(FieldKind kind, const TextFieldState& state, const Modifier& modifier, SecureTextFieldOptions o, const CallSite& site) {
    ComposableScope scope(site);
    // The character just typed (where it starts, and the text it made): shown until anything
    // else changes the text, the field loses its focus or 1.5 s pass.
    auto revealAt = remember([] { return mutableStateOf(-1); });
    auto revealedIn = remember([] { return std::make_shared<std::string>(); });
    auto typed = remember([] { return mutableStateOf(0); });  // each one restarts the clock
    auto ownSource = remember([] { return MutableInteractionSource(); });
    MutableInteractionSource source = o.interactionSource.value_or(ownSource);
    bool reveal = o.textObfuscationMode == TextObfuscationMode::RevealLastTyped;
    LaunchedEffect(typed.get(), [revealAt]() -> Task {
        co_await delay(std::chrono::milliseconds(1500));
        revealAt.set(-1);
    });
    InputTransformation input{[user = o.inputTransformation, reveal, revealAt, revealedIn, typed](TextFieldBuffer& b) {
        if (user && user->transformInput) user->transformInput(b);
        if (b.text() == b.originalText()) return;  // refused: what shows stays
        int at = reveal ? typedCharacter(b) : -1;
        revealAt.set(at);
        if (at < 0) return;
        *revealedIn = b.text();
        typed.set(typed.peek() + 1);
    }};
    const std::string& text = state.text();
    int revealed = revealAt.get();
    if (!reveal || !source.collectIsFocusedAsState() || text != *revealedIn) revealed = -1;
    std::string bullet = o.textObfuscationCharacter.empty() ? std::string(kBullet) : o.textObfuscationCharacter;
    textField(kind, text, {}, modifier,
              {.singleLine = true,
               .placeholder = std::move(o.placeholder),
               .onSubmit = std::move(o.onSubmit),
               .label = std::move(o.label),
               .prefix = std::move(o.prefix),
               .suffix = std::move(o.suffix),
               .leadingIcon = std::move(o.leadingIcon),
               .trailingIcon = std::move(o.trailingIcon),
               .supportingText = std::move(o.supportingText),
               .isError = o.isError,
               .keyboardOptions = o.keyboardOptions,
               .keyboardActions = std::move(o.keyboardActions),
               .textStyle = std::move(o.textStyle),
               .enabled = o.enabled,
               .interactionSource = source,
               .inputTransformation = std::move(input),
               .colors = std::move(o.colors),
               .shape = o.shape,
               .labelPosition = o.labelPosition,
               .decorator = std::move(o.decorator)},
              CallSite::current(), state, Obfuscation{std::move(bullet), revealed, o.textObfuscationMode == TextObfuscationMode::Visible});
}

} // namespace

void SecureTextField(const TextFieldState& state, const Modifier& modifier, SecureTextFieldOptions options, CallSite site) {
    secureTextField(FieldKind::Filled, state, modifier, std::move(options), site);
}

void OutlinedSecureTextField(const TextFieldState& state, const Modifier& modifier, SecureTextFieldOptions options, CallSite site) {
    secureTextField(FieldKind::Outlined, state, modifier, std::move(options), site);
}

void BasicSecureTextField(const TextFieldState& state, const Modifier& modifier, SecureTextFieldOptions options, CallSite site) {
    secureTextField(FieldKind::Plain, state, modifier, std::move(options), site);
}

void ContextMenuArea(std::function<std::vector<ContextMenuItem>()> items, const Content& content, const Modifier& modifier, bool enabled,
                     CallSite site) {
    ComposableScope scope(site);
    auto at = remember([] { return mutableStateOf(std::optional<Offset>()); });
    auto element = std::make_shared<ContextMenuElement>();
    if (enabled) element->onOpen = [at](Offset p) { at.set(p); };
    Box(modifier.then(element), content);
    if (auto p = at.get(); p && enabled && items) contextMenuPopup(*p, items(), [at] { at.set(std::nullopt); });
}

} // namespace kompoz
