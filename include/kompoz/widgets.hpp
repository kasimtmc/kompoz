#pragma once

// Foundation layouts (Box, Row, Column, Text, Spacer) and a small Material-like component set.
// Arguments that are named in Kotlin are passed as designated initializers here:
//   Column(Modifier().fillMaxSize(), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] { ... });

#include "animation.hpp"
#include "layout.hpp"
#include "text.hpp"
#include "runtime.hpp"

#include <functional>
#include <map>
#include <limits>
#include <optional>
#include <string>

namespace kompoz {

// ---- Theme -----------------------------------------------------------------------------

struct ColorScheme {
    Color primary, onPrimary, primaryContainer, onPrimaryContainer;
    Color background, onBackground, surface, onSurface, surfaceContainer, onSurfaceVariant;
    Color outline, outlineVariant, error;
    Color inverseSurface, inverseOnSurface, inversePrimary, secondaryContainer, onSecondaryContainer,
        surfaceContainerHigh;
    Color secondary, onSecondary, tertiary, onTertiary, onError, surfaceContainerLow, surfaceContainerHighest;
    Color tertiaryContainer, onTertiaryContainer;
    // The rest of Compose 1.4's scheme.
    Color errorContainer, onErrorContainer, surfaceVariant, surfaceTint, scrim, surfaceBright, surfaceDim, surfaceContainerLowest;
    Color primaryFixed, primaryFixedDim, onPrimaryFixed, onPrimaryFixedVariant;
    Color secondaryFixed, secondaryFixedDim, onSecondaryFixed, onSecondaryFixedVariant;
    Color tertiaryFixed, tertiaryFixedDim, onTertiaryFixed, onTertiaryFixedVariant;

    // The content color that goes on `background` when it is one of this scheme's (Compose's
    // ColorScheme.contentColorFor): onPrimary on primary, onSurface on the surfaces...; none for
    // another color.
    std::optional<Color> contentColorFor(Color background) const;

    // Applies `f` to every color (e.g. to animate a theme change color by color).
    template <class F>
    ColorScheme map(F&& f) const {
        return {f(primary), f(onPrimary), f(primaryContainer), f(onPrimaryContainer), f(background),
                f(onBackground), f(surface), f(onSurface), f(surfaceContainer), f(onSurfaceVariant), f(outline),
                f(outlineVariant), f(error), f(inverseSurface), f(inverseOnSurface), f(inversePrimary),
                f(secondaryContainer), f(onSecondaryContainer), f(surfaceContainerHigh), f(secondary),
                f(onSecondary), f(tertiary), f(onTertiary), f(onError), f(surfaceContainerLow),
                f(surfaceContainerHighest), f(tertiaryContainer), f(onTertiaryContainer),
                f(errorContainer), f(onErrorContainer), f(surfaceVariant), f(surfaceTint), f(scrim), f(surfaceBright),
                f(surfaceDim), f(surfaceContainerLowest), f(primaryFixed), f(primaryFixedDim), f(onPrimaryFixed),
                f(onPrimaryFixedVariant), f(secondaryFixed), f(secondaryFixedDim), f(onSecondaryFixed),
                f(onSecondaryFixedVariant), f(tertiaryFixed), f(tertiaryFixedDim), f(onTertiaryFixed),
                f(onTertiaryFixedVariant)};
    }
    friend constexpr bool operator==(const ColorScheme&, const ColorScheme&) = default;
};

constexpr ColorScheme lightColorScheme() {
    return {Color::hex(0x6750A4), Color::hex(0xFFFFFF), Color::hex(0xEADDFF), Color::hex(0x21005D),
            Color::hex(0xFEF7FF), Color::hex(0x1D1B20), Color::hex(0xFEF7FF), Color::hex(0x1D1B20),
            Color::hex(0xF3EDF7), Color::hex(0x49454F), Color::hex(0x79747E), Color::hex(0xCAC4D0),
            Color::hex(0xB3261E), Color::hex(0x322F35), Color::hex(0xF5EFF7), Color::hex(0xD0BCFF),
            Color::hex(0xE8DEF8), Color::hex(0x1D192B), Color::hex(0xECE6F0),
            Color::hex(0x625B71), Color::hex(0xFFFFFF), Color::hex(0x7D5260), Color::hex(0xFFFFFF),
            Color::hex(0xFFFFFF), Color::hex(0xF7F2FA), Color::hex(0xE6E0E9),
            Color::hex(0xFFD8E4), Color::hex(0x31111D),
            Color::hex(0xF9DEDC), Color::hex(0x410E0B), Color::hex(0xE7E0EC), Color::hex(0x6750A4), Color::hex(0x000000),
            Color::hex(0xFEF7FF), Color::hex(0xDED8E1), Color::hex(0xFFFFFF),
            Color::hex(0xEADDFF), Color::hex(0xD0BCFF), Color::hex(0x21005D), Color::hex(0x4F378B),
            Color::hex(0xE8DEF8), Color::hex(0xCCC2DC), Color::hex(0x1D192B), Color::hex(0x4A4458),
            Color::hex(0xFFD8E4), Color::hex(0xEFB8C8), Color::hex(0x31111D), Color::hex(0x633B48)};
}

constexpr ColorScheme darkColorScheme() {
    return {Color::hex(0xD0BCFF), Color::hex(0x381E72), Color::hex(0x4F378B), Color::hex(0xEADDFF),
            Color::hex(0x141218), Color::hex(0xE6E0E9), Color::hex(0x141218), Color::hex(0xE6E0E9),
            Color::hex(0x211F26), Color::hex(0xCAC4D0), Color::hex(0x938F99), Color::hex(0x49454F),
            Color::hex(0xF2B8B5), Color::hex(0xE6E0E9), Color::hex(0x322F35), Color::hex(0x6750A4),
            Color::hex(0x4A4458), Color::hex(0xE8DEF8), Color::hex(0x2B2930),
            Color::hex(0xCCC2DC), Color::hex(0x332D41), Color::hex(0xEFB8C8), Color::hex(0x492532),
            Color::hex(0x601410), Color::hex(0x1D1B20), Color::hex(0x36343B),
            Color::hex(0x633B48), Color::hex(0xFFD8E4),
            Color::hex(0x8C1D18), Color::hex(0xF9DEDC), Color::hex(0x49454F), Color::hex(0xD0BCFF), Color::hex(0x000000),
            Color::hex(0x3B383E), Color::hex(0x141218), Color::hex(0x0F0D13),
            Color::hex(0xEADDFF), Color::hex(0xD0BCFF), Color::hex(0x21005D), Color::hex(0x4F378B),
            Color::hex(0xE8DEF8), Color::hex(0xCCC2DC), Color::hex(0x1D192B), Color::hex(0x4A4458),
            Color::hex(0xFFD8E4), Color::hex(0xEFB8C8), Color::hex(0x31111D), Color::hex(0x633B48)};
}

// Android 12+ "Material You": the scheme the system derives from the wallpaper (the same tone
// mapping as Compose's dynamicLightColorScheme / dynamicDarkColorScheme). Read during composition;
// where the platform has no palette (desktop, older Android) they return the baseline schemes.
bool isDynamicColorAvailable();
// The system bars' icons: dark over a light app, light over a dark one (on Android what a theme
// does with WindowInsetsController's isAppearanceLightStatusBars). Typically
// SystemBarsAppearance(!dark) next to MaterialTheme; applied after composition.
void SystemBarsAppearance(bool lightStatusBars, bool lightNavigationBars, CallSite site = CallSite::current());
inline void SystemBarsAppearance(bool lightBars, CallSite site = CallSite::current()) {
    SystemBarsAppearance(lightBars, lightBars, site);
}
ColorScheme dynamicLightColorScheme();
ColorScheme dynamicDarkColorScheme();
ColorScheme dynamicLightColorScheme(const TonalPalette& palette);
ColorScheme dynamicDarkColorScheme(const TonalPalette& palette);
// A tone (0 black .. 100 white, CIELAB lightness) of one of the palette's key colors.
Color paletteTone(const std::array<Color, 13>& keyColor, float tone);

namespace detail {
// One of Material 3's type styles: size, line height and letter spacing in sp, each line
// centered in its height with nothing trimmed (Material's DefaultLineHeightStyle).
constexpr Font typeScale(float size, float lineHeight, float letterSpacing, FontWeight weight = FontWeight::Normal) {
    return {.size = size,
            .weight = weight,
            .letterSpacing = letterSpacing,
            .lineHeight = lineHeight,
            .lineHeightStyle = {.alignment = LineHeightStyle::Alignment::Center, .trim = LineHeightStyle::Trim::None}};
}
} // namespace detail

// Material 3's type scale (TypeScaleTokens).
struct Typography {
    Font displayLarge = detail::typeScale(57, 64, -0.25f);
    Font displayMedium = detail::typeScale(45, 52, 0);
    Font displaySmall = detail::typeScale(36, 44, 0);
    Font headlineLarge = detail::typeScale(32, 40, 0);
    Font headlineMedium = detail::typeScale(28, 36, 0);
    Font headlineSmall = detail::typeScale(24, 32, 0);
    Font titleLarge = detail::typeScale(22, 28, 0);
    Font titleMedium = detail::typeScale(16, 24, 0.15f, FontWeight::Medium);
    Font bodyLarge = detail::typeScale(16, 24, 0.5f);
    Font titleSmall = detail::typeScale(14, 20, 0.1f, FontWeight::Medium);
    Font bodyMedium = detail::typeScale(14, 20, 0.25f);
    Font bodySmall = detail::typeScale(12, 16, 0.4f);
    Font labelLarge = detail::typeScale(14, 20, 0.1f, FontWeight::Medium);
    Font labelMedium = detail::typeScale(12, 16, 0.5f, FontWeight::Medium);
    Font labelSmall = detail::typeScale(11, 16, 0.5f, FontWeight::Medium);
    friend constexpr bool operator==(const Typography&, const Typography&) = default;
};

inline const CompositionLocal<ColorScheme> LocalColorScheme{lightColorScheme()};
inline const CompositionLocal<Typography> LocalTypography{Typography{}};
inline const CompositionLocal<Font> LocalTextStyle{Font{}};
// What selected text looks like (Compose's TextSelectionColors): the handles' color and the
// highlight behind the selection, in a SelectionContainer and in fields. MaterialTheme provides
// its scheme's: primary, and primary at 40%; a field's TextFieldColors can have its own.
struct TextSelectionColors {
    Color handleColor, backgroundColor;
    friend constexpr bool operator==(const TextSelectionColors&, const TextSelectionColors&) = default;
};
inline const CompositionLocal<TextSelectionColors> LocalTextSelectionColors{
    TextSelectionColors{lightColorScheme().primary, lightColorScheme().primary.copy(0.4f)}};

// Material 3's motion (Compose's MotionScheme): springs for what moves (spatial: places, sizes,
// shapes) and for what changes how it looks (effects: colors, alpha), each default, fast (small
// things: a button's shape) and slow (big ones). standard() hardly overshoots; expressive()
// (MaterialExpressiveTheme's) lets the spatial ones bounce. Components animate with the one
// around them:
//   float x = animateFloatAsState(open ? 200.f : 0.f, motionScheme().defaultSpatialSpec);
struct MotionScheme {
    AnimationSpec defaultSpatialSpec, fastSpatialSpec, slowSpatialSpec;
    AnimationSpec defaultEffectsSpec, fastEffectsSpec, slowEffectsSpec;
    static MotionScheme standard() {
        return {spring(0.9f, 700), spring(0.9f, 1400), spring(0.9f, 300), spring(1, 1600), spring(1, 3800), spring(1, 800)};
    }
    static MotionScheme expressive() {
        return {spring(0.8f, 380), spring(0.6f, 800), spring(0.8f, 200), spring(1, 1600), spring(1, 3800), spring(1, 800)};
    }
    friend bool operator==(const MotionScheme&, const MotionScheme&) = default;
};
inline const CompositionLocal<MotionScheme> LocalMotionScheme{MotionScheme::standard()};
inline const MotionScheme& motionScheme() { return LocalMotionScheme.current(); }

// The motion stays the one around it (standard() at the top).
void MaterialTheme(const ColorScheme& colorScheme, const Content& content, CallSite site = CallSite::current());
void MaterialTheme(const ColorScheme& colorScheme, const MotionScheme& motionScheme, const Content& content,
                   CallSite site = CallSite::current());
// Material 3 Expressive's theme (Compose's MaterialExpressiveTheme): MaterialTheme with the
// expressive motion (unless told another), in `colorScheme` or the one around it.
void MaterialExpressiveTheme(std::optional<ColorScheme> colorScheme, const Content& content, CallSite site = CallSite::current());
void MaterialExpressiveTheme(std::optional<ColorScheme> colorScheme, const MotionScheme& motionScheme, const Content& content,
                             CallSite site = CallSite::current());
namespace detail {
// The shape on its way to `target`: each corner morphs on the motion scheme's fast spatial spring
// (buttons' and list items' shapes as they are pressed or selected).
Shape morphingShape(const Shape& target, CallSite site = CallSite::current());
}  // namespace detail
inline bool isSystemInDarkTheme() { return LocalSystemDarkTheme.current(); }
inline const ColorScheme& colorScheme() { return LocalColorScheme.current(); }
// The content color for `background` (Compose's contentColorFor): the theme's matching one, or
// the content color around it when `background` isn't a color of the theme.
inline Color contentColorFor(Color background) { return colorScheme().contentColorFor(background).value_or(LocalContentColor.current()); }
inline const Typography& typography() { return LocalTypography.current(); }

// ---- Layouts ---------------------------------------------------------------------------

void Box(const Content& content, CallSite site = CallSite::current());
// A Box whose content's animateBounds measure their places from it (Compose's LookaheadScope).
void LookaheadScope(const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
void Box(const Modifier& modifier, const Content& content = {}, CallSite site = CallSite::current());
void Box(const Modifier& modifier, Alignment contentAlignment, const Content& content,
         CallSite site = CallSite::current());

struct ColumnStyle {
    Arrangement verticalArrangement = Arrangement::Top;
    HorizontalAlignment horizontalAlignment = Alignment::Start;
};
void Column(const Content& content, CallSite site = CallSite::current());
void Column(const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
void Column(const Modifier& modifier, ColumnStyle style, const Content& content, CallSite site = CallSite::current());

struct RowStyle {
    Arrangement horizontalArrangement = Arrangement::Start;
    VerticalAlignment verticalAlignment = Alignment::Top;
};
void Row(const Content& content, CallSite site = CallSite::current());
void Row(const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
void Row(const Modifier& modifier, RowStyle style, const Content& content, CallSite site = CallSite::current());

void Spacer(const Modifier& modifier, CallSite site = CallSite::current());

// ---- Custom layouts ------------------------------------------------------------------------
//
//   Layout(modifier, content, [](const std::vector<Measurable>& children, const Constraints& c) {
//       float y = 0, w = 0;
//       for (const Measurable& m : children) {
//           Placeable p = m.measure(c.loosen());
//           p.place(0, y);
//           y += p.height, w = std::max(w, p.width);
//       }
//       return Size{w, y};
//   });

struct Placeable {
    LayoutNode* node;
    float width = 0, height = 0;
    void place(float x, float y, float zIndex = 0) const { node->place(x, y, zIndex); }
    // Where an alignment line of it is, from its top (Compose's placeable[FirstBaseline]); NaN: none.
    float operator[](AlignmentLine line) const { return node->alignmentLine(line); }
};
struct Measurable {
    LayoutNode* node;
    Placeable measure(const Constraints& constraints) const {
        Size s = node->measure(constraints);
        return {node, s.width, s.height};
    }
    ParentData parentData() const { return node->parentData(); }
    std::string layoutId() const {
        auto* id = node->modifier.find<LayoutIdElement>();
        return id ? id->id : std::string();
    }
};
using LayoutMeasurePolicy = std::function<Size(const std::vector<Measurable>& children, const Constraints& constraints)>;
// One node per child of `content` (each child's own nodes, as emitted).
void Layout(const Modifier& modifier, const Content& content, LayoutMeasurePolicy measurePolicy,
            CallSite site = CallSite::current());
namespace detail {
// A menu's rows, one under another: as wide as the widest (112 dp to `maxWidth`), each that wide.
LayoutMeasurePolicy menuRows(float maxWidth);
} // namespace detail

// A layout that composes some of its content while it measures, from what it knows then
// (Compose's SubcomposeLayout): subcompose(slotId, content) gives the slot's measurables — a
// slot that depends on another's size, or one picked by the constraints. A slot asked for the
// first time (or whose `dependency` changed) is composed, and the layout run again, in the
// same frame (until then it has no measurables). The content runs later, in composition:
// capture by value.
//   SubcomposeLayout(Modifier(), [](SubcomposeMeasureScope& s, const Constraints& c) {
//       auto main = s.subcompose("main", [] { Text("Başlık"); });
//       Size size = ...;  // measure and place them
//       for (auto& m : s.subcompose("badge", size, [size] { Badge(size); })) ...
//       return size;
//   });
class SubcomposeMeasureScope {
public:
    struct Data;
    SubcomposeMeasureScope(Data& data, LayoutNode& node) : data_(&data), node_(&node) {}
    template <class K>
    std::vector<Measurable> subcompose(const K& slotId, Content content) {
        return slot(std::hash<K>{}(slotId), std::move(content), 0);
    }
    template <class K, class D>
    std::vector<Measurable> subcompose(const K& slotId, const D& dependency, Content content) {
        return slot(std::hash<K>{}(slotId), std::move(content), std::hash<D>{}(dependency) | 1);
    }

private:
    std::vector<Measurable> slot(size_t id, Content content, size_t dependency);
    Data* data_;
    LayoutNode* node_;
};
using SubcomposeMeasurePolicy = std::function<Size(SubcomposeMeasureScope& scope, const Constraints& constraints)>;
void SubcomposeLayout(const Modifier& modifier, SubcomposeMeasurePolicy measurePolicy, CallSite site = CallSite::current());

// Rows (columns) that wrap to the next line when full, e.g. for chips.
struct FlowStyle {
    float horizontalSpacing = 0, verticalSpacing = 0;
    int maxItemsInEachLine = 0;  // 0: as many as fit
};
void FlowRow(const Modifier& modifier, const FlowStyle& style, const Content& content, CallSite site = CallSite::current());
void FlowColumn(const Modifier& modifier, const FlowStyle& style, const Content& content, CallSite site = CallSite::current());

// A layout of CSS's flexbox (Compose 1.12's FlexBox, a superset of Row, Column, FlowRow and
// FlowColumn): its children along the main axis (direction), wrapping to more lines or not,
// spread along it (justifyContent), lined up across their line (alignItems; a child's own
// alignSelf), the lines spread across (alignContent, when wrapping), rowGap and columnGap
// between them. Each child takes its share with Modifier().flex({.grow, .shrink, .basis, ...}).
//   FlexBox(Modifier().fillMaxWidth(), {.wrap = FlexWrap::Wrap, .justifyContent = FlexJustifyContent::SpaceBetween, .columnGap = 8}, [] {
//       Text("Ad", Modifier().flex({.grow = 1}));
//       Button(save, [] { Text("Kaydet"); });
//   });
enum class FlexDirection { Row, RowReverse, Column, ColumnReverse };  // Row: start to end; Column: top down
enum class FlexWrap { NoWrap, Wrap, WrapReverse };                     // WrapReverse: new lines before the first
enum class FlexJustifyContent { Start, Center, End, SpaceBetween, SpaceAround, SpaceEvenly };
enum class FlexAlignItems { Start, Center, End, Stretch, Baseline };
enum class FlexAlignContent { Start, Center, End, Stretch, SpaceBetween, SpaceAround };
struct FlexBoxConfig {
    FlexDirection direction = FlexDirection::Row;
    FlexWrap wrap = FlexWrap::NoWrap;
    FlexJustifyContent justifyContent = FlexJustifyContent::Start;
    FlexAlignItems alignItems = FlexAlignItems::Start;
    FlexAlignContent alignContent = FlexAlignContent::Start;
    float rowGap = 0, columnGap = 0;  // dp between lines of a row (rowGap) and items of it (columnGap)
    std::optional<AlignmentLine> baselineLine;  // alignItems Baseline by this line (unset: FirstBaseline)
    friend bool operator==(const FlexBoxConfig&, const FlexBoxConfig&) = default;
};
void FlexBox(const Modifier& modifier, const FlexBoxConfig& config, const Content& content, CallSite site = CallSite::current());
// The config by the constraints it is laid out with (Compose's FlexBoxConfig reading
// `constraints`): wrap only when narrow, say.
void FlexBox(const Modifier& modifier, std::function<FlexBoxConfig(const Constraints&)> config, const Content& content,
             CallSite site = CallSite::current());

// A layout of rows and columns (Compose 1.12's Grid, CSS's grid): the tracks it's configured
// with, more Auto ones as its children need; each child in the cells it asks for
// (Modifier().gridItem), else in the next free ones along the flow.
//   Grid(Modifier().fillMaxWidth(), {.columns = {GridTrackSize::Fixed(72), GridTrackSize::Flex(1)}, .rowGap = 8, .columnGap = 8,
//                                    .areas = {{"başlık", 1, 1, 1, 2}}}, [] {
//       Text("Başlık", Modifier().gridItem("başlık"));
//       Icon(icons::Person, "");
//       Text("Ad");
//   });
// A track's size: Fixed dp; a Percentage of the grid after the gaps (the content's, unbounded);
// Flex: a share of the room left, by weight, from its min content (MinMax: from `minDp`);
// MinContent / MaxContent of its items; Auto: from min towards max content in the room left.
struct GridTrackSize {
    enum class Kind { Fixed, Percentage, Flex, MinContent, MaxContent, Auto, MinMax };
    Kind kind = Kind::Auto;
    float value = 0, weight = 0;  // dp, fraction or weight; MinMax: min dp and weight
    static constexpr GridTrackSize Fixed(float dp) { return {Kind::Fixed, dp < 0 ? 0 : dp}; }
    static constexpr GridTrackSize Percentage(float fraction) { return {Kind::Percentage, fraction < 0 ? 0 : fraction}; }
    static constexpr GridTrackSize Flex(float weight) { return {Kind::Flex, weight < 0 ? 0 : weight}; }
    static constexpr GridTrackSize MinMax(float minDp, float weight) { return {Kind::MinMax, minDp < 0 ? 0 : minDp, weight < 0 ? 0 : weight}; }
    static constexpr GridTrackSize MinContent() { return {Kind::MinContent}; }
    static constexpr GridTrackSize MaxContent() { return {Kind::MaxContent}; }
    static constexpr GridTrackSize Auto() { return {}; }
    friend constexpr bool operator==(const GridTrackSize&, const GridTrackSize&) = default;
};
// Where children without cells go: along the row, onto the next (Row), or down the column (Column).
enum class GridFlow { Row, Column };
// Named cells for gridItem(area): 1-based row and column as gridItem's; one of them 0: the first
// free cell of the other.
struct GridArea {
    std::string name;
    int row = 0, column = 0, rowSpan = 1, columnSpan = 1;
    friend bool operator==(const GridArea&, const GridArea&) = default;
};
struct GridConfig {
    std::vector<GridTrackSize> columns, rows;
    float rowGap = 0, columnGap = 0;  // dp between rows, between columns
    GridFlow flow = GridFlow::Row;
    std::vector<GridArea> areas;
    friend bool operator==(const GridConfig&, const GridConfig&) = default;
};
void Grid(const Modifier& modifier, const GridConfig& config, const Content& content, CallSite site = CallSite::current());
// The config by the constraints it is laid out with: fewer columns when narrow, say.
void Grid(const Modifier& modifier, std::function<GridConfig(const Constraints&)> config, const Content& content,
          CallSite site = CallSite::current());

// A Box whose content knows the constraints it is laid out with (to pick a layout by width).
// The layout reports them and the content recomposes in the same frame; the very first
// composition sees unbounded constraints.
void BoxWithConstraints(const Modifier& modifier, const std::function<void(const Constraints&)>& content,
                        Alignment contentAlignment = Alignment::TopStart, CallSite site = CallSite::current());

// Content shown above everything else (see OverlaySpec): the building block of dialogs and menus.
void Popup(OverlaySpec spec, const Content& content, CallSite site = CallSite::current());

// Where a TooltipArea's tooltip goes (Compose Multiplatform's TooltipPlacement): at the mouse
// pointer moved by `offset` (and kept `windowMargin` inside the window, above it when there is
// no room below), or by a point of the area: `anchor` names the point, `alignment` which way
// from it the tooltip goes (BottomCenter: below it, centered), then moved by `offset`.
struct TooltipPlacement {
    enum class Kind { CursorPoint, ComponentRect };
    Kind kind = Kind::CursorPoint;
    Offset offset{0, 16};
    Alignment anchor = Alignment::BottomCenter, alignment = Alignment::BottomEnd;
    float windowMargin = 4;
    static TooltipPlacement CursorPoint(Offset offset = {0, 16}, float windowMargin = 4) {
        return {Kind::CursorPoint, offset, Alignment::BottomCenter, Alignment::BottomEnd, windowMargin};
    }
    static TooltipPlacement ComponentRect(Alignment anchor = Alignment::BottomCenter, Alignment alignment = Alignment::BottomCenter,
                                          Offset offset = {}) {
        return {Kind::ComponentRect, offset, anchor, alignment, 4};
    }
};
struct TooltipAreaOptions {
    int delayMillis = 500;  // from the mouse coming over the area
    TooltipPlacement tooltipPlacement;
};
// Compose Multiplatform's desktop TooltipArea: `tooltip` shows a moment after the mouse comes
// over `content` (and follows it), until it leaves or presses. Input passes through it.
void TooltipArea(const Content& tooltip, const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
void TooltipArea(const Content& tooltip, const Modifier& modifier, const TooltipAreaOptions& options, const Content& content,
                 CallSite site = CallSite::current());

// Handles the back button (Escape on desktop) while enabled; the most recently composed wins.
void BackHandler(bool enabled, std::function<void()> onBack, CallSite site = CallSite::current());
template <class T>
class Flow;
// Back as a gesture (Compose's PredictiveBackHandler): while enabled, the system's back gesture
// (Android 14+) launches `onBack` with a flow of its steps, to follow the finger with. The flow
// ends once the gesture is done, and the coroutine goes on (close, go back); a gesture called
// off cancels it (an RAII guard in it puts things back). A back press, or Escape, launches it
// with a flow that ends at once.
void PredictiveBackHandler(bool enabled, std::function<Task(Flow<BackEventCompat>)> onBack, CallSite site = CallSite::current());

// Saveable, as in Compose: a screen on the back stack keeps its scroll position.
inline ScrollState rememberScrollState(CallSite site = CallSite::current()) {
    return rememberSaveable([] { return ScrollState{}; }, site);
}
// The state keeps the latest callback (it may capture new values each composition).
inline ScrollableState rememberScrollableState(std::function<float(float)> consumeScrollDelta,
                                               CallSite site = CallSite::current()) {
    auto& state = remember([] { return ScrollableState{}; }, site);
    state.data_->consume = std::move(consumeScrollDelta);
    return state;
}

// ---- Text ------------------------------------------------------------------------------

// StartEllipsis and MiddleEllipsis cut a single line (maxLines = 1 or no soft wrap) at its start
// or in its middle; more lines are clipped (as on Android).
enum class TextOverflow { Clip, Ellipsis, Visible, StartEllipsis, MiddleEllipsis };

// Text that takes the largest font size from minFontSize to maxFontSize (sp, in steps of
// stepSize) at which it fits where it is — its width, its height, its maxLines (Compose's
// TextAutoSize.StepBased). With room to grow downwards it only shrinks for a word too wide.
struct TextAutoSize {
    float minFontSize = 12, maxFontSize = 112, stepSize = 0.25f;
    static TextAutoSize StepBased(float minFontSize = 12, float maxFontSize = 112, float stepSize = 0.25f) {
        return {minFontSize, maxFontSize, stepSize};
    }
    friend bool operator==(const TextAutoSize&, const TextAutoSize&) = default;
};

// Unset fields fall back to LocalTextStyle / LocalContentColor, like Compose's "Unspecified".
struct TextStyle {
    std::optional<Font> font;
    std::optional<TextUnit> fontSize;  // em: of LocalTextStyle's size
    std::optional<FontWeight> fontWeight;
    std::optional<Color> color;
    TextAlign textAlign = TextAlign::Start;
    TextDecoration textDecoration = TextDecoration::None;
    int maxLines = 0;  // 0 = unlimited
    int minLines = 1;  // at least this many lines tall (empty ones as tall as the style's)
    std::optional<FontStyle> fontStyle;
    std::optional<FontFamily> fontFamily;
    std::optional<TextUnit> letterSpacing;  // after each character (Font::letterSpacing)
    // OpenType features the font has, as CSS's font-feature-settings writes them: "tnum" (digits
    // as wide), "smcp" (small capitals), "frac", "\"liga\" 0"… (Font::features).
    std::optional<std::string> fontFeatureSettings;
    TextOverflow overflow = TextOverflow::Clip;  // what the last line shows when text is cut off
    bool softWrap = true;                        // false: one line per paragraph
    // Content: the paragraph's first letter that has a direction decides; text without one (digits)
    // goes the layout's way (LocalLayoutDirection), as Compose's.
    TextDirection textDirection = TextDirection::Content;
    std::optional<TextAutoSize> autoSize;  // its font size from where it is, not fontSize
    // sp from one line's baseline to the next (Font::lineHeight; Material's type styles have
    // theirs): where the room added or taken goes is lineHeightStyle's; by default above and
    // below each line in proportion to its ascent and descent, but not above the first line or
    // below the last (LineHeightStyle's Default; Material's styles center each line and trim
    // nothing). Text fields center their lines in it.
    std::optional<TextUnit> lineHeight;
    std::optional<LineHeightStyle> lineHeightStyle;
    TextIndent textIndent;  // not in text fields
    LineBreak lineBreak;    // not in text fields
    // A brush paints the text instead of its color: a gradient spreads over the whole text
    // (Brush::horizontalGradient({...})). A shadow goes under its glyphs.
    std::optional<Brush> brush;
    std::optional<Shadow> shadow;
    // Stroke(width, cap, join): its glyphs outlined (width dp, centered on their outlines), as
    // Compose's TextStyle.drawStyle; Fill (or unset) fills them. A path effect isn't applied.
    std::optional<DrawStyle> drawStyle;
    // Its glyphs stretched across and slanted (TextGeometricTransform); emoji stay as they are.
    std::optional<TextGeometricTransform> textGeometricTransform;
    friend bool operator==(const TextStyle&, const TextStyle&) = default;
};

// The text style of what is inside, merged into the current one (Compose's ProvideTextStyle):
// its font and size, weight, style and family go to LocalTextStyle, a color to LocalContentColor.
//   ProvideTextStyle({.fontSize = 20, .fontWeight = FontWeight::Bold}, [&] { Text("Başlık"); });
void ProvideTextStyle(const TextStyle& value, const Content& content, CallSite site = CallSite::current());

// Text that takes nothing from around it (Compose's BasicText): its own style over Compose's
// TextStyle.Default — 14 sp, black — not LocalTextStyle or LocalContentColor.
void BasicText(std::string text, const Modifier& modifier = {}, const TextStyle& style = {}, CallSite site = CallSite::current());
void BasicText(const AnnotatedString& text, const Modifier& modifier = {}, const TextStyle& style = {},
               CallSite site = CallSite::current());

void Text(std::string text, CallSite site = CallSite::current());
void Text(std::string text, const TextStyle& style, CallSite site = CallSite::current());
void Text(std::string text, const Modifier& modifier, const TextStyle& style = {}, CallSite site = CallSite::current());
// Texts inside can be selected and copied, one selection across them all: a finger's long press
// selects a word, with handles to drag (from text to text) and a toolbar (copy, select all);
// the mouse drags a selection or double clicks a word, and Ctrl+C copies it. A press elsewhere
// in it clears it.
void SelectionContainer(const Modifier& modifier, const Content& content, CallSite site = CallSite::current());

// The floating toolbar over a text selection (Compose's TextToolbar): a field's cut, copy,
// paste and select all, a SelectionContainer's copy and select all. Kompoz draws its own unless
// one is provided through LocalTextToolbar (your own look, or the platform's): then showMenu is
// called with the selection's rect (window dp) and the actions that can be done now (the others
// empty) while it should show — again when they change — and hide() once it shouldn't.
enum class TextToolbarStatus { Shown, Hidden };
class TextToolbar {
public:
    virtual ~TextToolbar() = default;
    virtual void showMenu(Rect rect, std::function<void()> onCopyRequested, std::function<void()> onPasteRequested,
                          std::function<void()> onCutRequested, std::function<void()> onSelectAllRequested) = 0;
    virtual void hide() = 0;
    virtual TextToolbarStatus status() const = 0;
};
inline const CompositionLocal<std::shared_ptr<TextToolbar>> LocalTextToolbar{nullptr};  // none: Kompoz's own
namespace detail {
// A provided toolbar shown over the selection while composed: `selection` gives its rect (window
// dp) from the anchor's bounds at layout; told again only when the rect or the actions change.
void customTextToolbar(const std::shared_ptr<TextToolbar>& toolbar, std::function<Rect(const Rect& anchor)> selection,
                       std::function<void()> onCopy, std::function<void()> onPaste, std::function<void()> onCut,
                       std::function<void()> onSelectAll, CallSite site = CallSite::current());
} // namespace detail
// Texts inside it aren't selectable, in a SelectionContainer (labels, buttons).
void DisableSelection(const Content& content, CallSite site = CallSite::current());
// Several styles in one text (see text.hpp); links are clickable.
void Text(const AnnotatedString& text, const Modifier& modifier = {}, const TextStyle& style = {},
          CallSite site = CallSite::current());
// A composable in a text (Compose's InlineTextContent): the room the text keeps for it, and what
// goes there (told the alternate text).
struct InlineTextContent {
    Placeholder placeholder;
    std::function<void(const std::string& alternateText)> children;
};
// With inline content: each appendInlineContent(id) of the text gets inlineContent[id] in a
// room of its placeholder's size, on its line as it says.
//   Text(buildAnnotatedString([](auto& b) { b.append("Hava "); b.appendInlineContent("güneş", "[güneş]"); b.append(" açık"); }),
//        Modifier(), {}, {{"güneş", {{.width = 1.2f, .height = 1.2f}, [](auto&) { Icon(Icons::Filled::Star, ""); }}}});
void Text(const AnnotatedString& text, const Modifier& modifier, const TextStyle& style,
          const std::map<std::string, InlineTextContent>& inlineContent, CallSite site = CallSite::current());

// ---- Components ------------------------------------------------------------------------

// M3 Expressive's shape morph (Compose's ButtonShapes): the corners go from `shape` to
// `pressedShape` while the button is held.
struct ButtonShapes {
    Shape shape = CircleShape;
    Shape pressedShape = RoundedCornerShape(8);
};

struct ButtonStyle {
    bool enabled = true;
    std::optional<Color> containerColor;
    std::optional<Color> contentColor;
    std::optional<MutableInteractionSource> interactionSource;  // told of presses, hovers and focus
    std::optional<ButtonShapes> shapes;                         // set: the corners morph while pressed
    std::optional<PaddingValues> contentPadding;                // unset: ButtonDefaults' for its kind
};

// Compose 1.4's ButtonDefaults sizes: an icon before the label takes ButtonWithIconContentPadding
// (TextButtonWithIconContentPadding for a TextButton), IconSize big and IconSpacing from it.
namespace ButtonDefaults {
inline constexpr PaddingValues ContentPadding{24, 8, 24, 8};
inline constexpr PaddingValues ButtonWithIconContentPadding{16, 8, 24, 8};
inline constexpr PaddingValues TextButtonContentPadding{12, 8, 12, 8};
inline constexpr PaddingValues TextButtonWithIconContentPadding{12, 8, 16, 8};
inline constexpr float MinWidth = 58, MinHeight = 40, IconSize = 18, IconSpacing = 8;
} // namespace ButtonDefaults

void Button(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void Button(std::function<void()> onClick, const Modifier& modifier, const Content& content,
            CallSite site = CallSite::current());
void Button(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style, const Content& content,
            CallSite site = CallSite::current());

void OutlinedButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void OutlinedButton(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style,
                    const Content& content, CallSite site = CallSite::current());

void FilledTonalButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void FilledTonalButton(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style,
                       const Content& content, CallSite site = CallSite::current());

void ElevatedButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void ElevatedButton(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style,
                    const Content& content, CallSite site = CallSite::current());

void TextButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void TextButton(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style,
                const Content& content, CallSite site = CallSite::current());

// M3 Expressive's toggle buttons (Compose's ToggleButton and its Elevated, Tonal and Outlined
// kinds): a button that stays checked, its corners morphing from round to square when it is
// and squarer still while held. Checked colors: primary (Tonal: secondary; Outlined:
// inverseSurface).
struct ToggleButtonShapes {
    Shape shape = CircleShape;
    Shape pressedShape = RoundedCornerShape(8);
    Shape checkedShape = RoundedCornerShape(12);
};
struct ToggleButtonStyle {
    bool enabled = true;
    std::optional<Color> containerColor, contentColor;                // unchecked
    std::optional<Color> checkedContainerColor, checkedContentColor;  // checked
    std::optional<MutableInteractionSource> interactionSource;
    ToggleButtonShapes shapes;
};
void ToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content, CallSite site = CallSite::current());
void ToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier, const ToggleButtonStyle& style,
                  const Content& content, CallSite site = CallSite::current());
void ElevatedToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content,
                          CallSite site = CallSite::current());
void ElevatedToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                          const ToggleButtonStyle& style, const Content& content, CallSite site = CallSite::current());
void TonalToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content,
                       CallSite site = CallSite::current());
void TonalToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                       const ToggleButtonStyle& style, const Content& content, CallSite site = CallSite::current());
void OutlinedToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content,
                          CallSite site = CallSite::current());
void OutlinedToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                          const ToggleButtonStyle& style, const Content& content, CallSite site = CallSite::current());

// M3 Expressive's split button (Compose's SplitButtonLayout and SplitButtonDefaults' buttons):
// a main action and a menu button 2 dp apart, rounded on their outer sides, squarish where they
// meet (rounder while held). The menu button is checked while its menu is open: it turns round
// and TrailingButtonIcon's arrow turns over.
//   SplitButtonLayout(
//       [&] { SplitButtonDefaults::LeadingButton([] { save(); }, [] { Text("Kaydet"); }); },
//       [&] {
//           SplitButtonDefaults::TrailingButton(open.get(), [open](bool on) { open.set(on); },
//                                               [&] { SplitButtonDefaults::TrailingButtonIcon(open.get()); });
//       });
void SplitButtonLayout(const Content& leadingButton, const Content& trailingButton, const Modifier& modifier = {},
                       CallSite site = CallSite::current());
enum class SplitButtonKind { Filled, Tonal, Elevated, Outlined };
struct SplitButtonStyle {
    SplitButtonKind kind = SplitButtonKind::Filled;  // the colors of Button, FilledTonalButton, ...
    bool enabled = true;
    std::optional<MutableInteractionSource> interactionSource;
};
namespace SplitButtonDefaults {
inline constexpr float Spacing = 2, InnerCornerSize = 4, PressedInnerCornerSize = 8, TrailingButtonWidth = 48;  // dp
inline ButtonShapes leadingButtonShapes() {
    return {RoundedCornerShape(CornerPercent(50), {InnerCornerSize}, {InnerCornerSize}, CornerPercent(50)),
            RoundedCornerShape(CornerPercent(50), {PressedInnerCornerSize}, {PressedInnerCornerSize}, CornerPercent(50))};
}
inline ToggleButtonShapes trailingButtonShapes() {
    return {RoundedCornerShape({InnerCornerSize}, CornerPercent(50), CornerPercent(50), {InnerCornerSize}),
            RoundedCornerShape({PressedInnerCornerSize}, CornerPercent(50), CornerPercent(50), {PressedInnerCornerSize}), CircleShape};
}
void LeadingButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void LeadingButton(std::function<void()> onClick, const Modifier& modifier, const SplitButtonStyle& style, const Content& content,
                   CallSite site = CallSite::current());
void TrailingButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content, CallSite site = CallSite::current());
void TrailingButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier, const SplitButtonStyle& style,
                    const Content& content, CallSite site = CallSite::current());
// The menu arrow, turned over (animated) while `checked`.
void TrailingButtonIcon(bool checked, CallSite site = CallSite::current());
} // namespace SplitButtonDefaults

// Shows a bitmap. Its pixels count as dp; with only one fixed dimension (fillMaxWidth()) the
// other follows the image's aspect ratio.
void Image(const ImageBitmap& bitmap, std::string contentDescription, const Modifier& modifier = {},
           ContentScale contentScale = ContentScale::Fit, float alpha = 1, std::optional<ColorFilter> colorFilter = {},
           CallSite site = CallSite::current());

// Loads an image asset once ("images/photo.png"), like painterResource: JPEG, PNG, WebP, BMP
// (on the web PNG and BMP: the browser decodes the others asynchronously, use AsyncImage).
ImageBitmap imageResource(const std::string& path, CallSite site = CallSite::current());
// The same as a Painter (Compose's painterResource, for bitmaps).
Painter painterResource(const std::string& path, CallSite site = CallSite::current());
// An Android vector drawable asset ("vectors/heart.xml"), read once (Compose's
// ImageVector.vectorResource; see ImageVector::fromVectorXml): Image(VectorPainter(it)) in its
// own colors, Icon(it) tinted. Empty if there is no such asset.
ImageVector vectorResource(const std::string& path, CallSite site = CallSite::current());
// A vector as a Painter, made once (Compose's rememberVectorPainter).
Painter rememberVectorPainter(const ImageVector& image, CallSite site = CallSite::current());
// A vector made by composable content (Compose's rememberVectorPainter { Group { Path() } }):
// what the content reads (state, animations) makes it again when it changes; made the same, it
// stays the same vector (equal painters, a backend's raster kept).
//   float drawn = animateFloatAsState(done.get() ? 1.f : 0.f, tween(600));
//   Painter check = rememberVectorPainter(48, 48, 24, 24, [&](ImageVector::Builder& v) {
//       VectorPath tick{.commands = VectorPath::parse("M5,12l5,5l9,-10"), .fill = std::nullopt,
//                       .stroke = Brush::solid(color), .strokeLineWidth = 2.5f, .trimPathEnd = drawn};
//       v.path(tick);
//   });
Painter rememberVectorPainter(float defaultWidth, float defaultHeight, float viewportWidth, float viewportHeight,
                              const std::function<void(ImageVector::Builder&)>& content, bool autoMirror = false,
                              CallSite site = CallSite::current());
// An animated vector drawable asset ("vectors/play_pause.xml"), read once (Compose's
// AnimatedImageVector.animatedVectorResource; see AnimatedImageVector::fromAnimatedVectorXml).
AnimatedImageVector animatedVectorResource(const std::string& path, CallSite site = CallSite::current());
// Draws it as it starts, or as it ends while `atEnd`; a change plays its animations there (back
// to its start when `atEnd` goes off), as Compose's rememberAnimatedVectorPainter.
//   auto playing = remember([] { return mutableStateOf(false); });
//   Image(rememberAnimatedVectorPainter(animatedVectorResource("vectors/play_pause.xml"), playing.get()), "Oynat",
//         Modifier().clickable([playing] { playing.set(!playing.peek()); }));
Painter rememberAnimatedVectorPainter(const AnimatedImageVector& vector, bool atEnd, CallSite site = CallSite::current());
// A Painter's drawing as an image (Compose's Image(painter)): as big as the painter is (scaled
// down to fit), placed in it by `alignment` and `contentScale` and clipped to it.
//   Image(painterResource("images/photo.jpg"), "Fotoğraf", Modifier().size(120), Alignment::Center, ContentScale::Crop);
void Image(const Painter& painter, std::string contentDescription, const Modifier& modifier = {},
           Alignment alignment = Alignment::Center, ContentScale contentScale = ContentScale::Fit, float alpha = 1,
           std::optional<ColorFilter> colorFilter = {}, CallSite site = CallSite::current());

// An image loaded away from the UI thread, as Coil's AsyncImage: `model` is an asset or file
// path ("images/photo.jpg") or an http(s) URL; JPEG, PNG, WebP and BMP. It is decoded at the
// size its layout gets (in pixels; a big photo in a small box is decoded small, by a power of
// two, never smaller than the box). Loaded images stay in a memory cache (64 MB, the least
// recently used go first), downloaded ones on disk too (Application::imageDiskCacheBytes),
// and several AsyncImages showing the same model share one load.
struct AsyncImageState {  // how it is loading (Coil's AsyncImagePainter.State)
    enum class Status { Loading, Success, Error };
    Status status = Status::Loading;
    ImageBitmap image;  // once it succeeded
    friend bool operator==(const AsyncImageState&, const AsyncImageState&) = default;
};
struct AsyncImageOptions {
    Content placeholder;  // while it loads
    Content error;        // if it couldn't be loaded
    ContentScale contentScale = ContentScale::Fit;
    float alpha = 1;
    std::optional<ColorFilter> colorFilter;
    int crossfadeMillis = 0;  // from the placeholder to the image (Coil's crossfade(true) is 200)
    std::function<void(const AsyncImageState&)> onState;  // after each change (Coil's onState)
};
void AsyncImage(std::string model, std::string contentDescription, const Modifier& modifier = {},
                const AsyncImageOptions& options = {}, CallSite site = CallSite::current());

// The loading itself, for a UI of your own (Coil's rememberAsyncImagePainter(...).state);
// `size`: what it is decoded for (by default the whole image, at most maxImageDimension).
AsyncImageState rememberAsyncImage(const std::string& model, ImageRequestSize size = {}, CallSite site = CallSite::current());
// The same as a Painter (Coil's rememberAsyncImagePainter): the image once it is loaded, nothing
// (no size) until then or if it fails — for Image(painter) or Modifier.paint; rememberAsyncImage
// tells the state.
Painter rememberAsyncImagePainter(const std::string& model, ImageRequestSize size = {}, CallSite site = CallSite::current());
// Empties the memory cache (loads in flight finish).
void clearImageCache();

// A 24dp icon tinted with LocalContentColor (or `tint`); a size() modifier changes its size.
void Icon(const ImageVector& icon, std::string contentDescription, const Modifier& modifier = {},
          std::optional<Color> tint = {}, CallSite site = CallSite::current());
// A Painter as an icon: its own size (24 dp if it has none), tinted the same way (Compose's
// Icon(painter); a bitmap turns into its shape in the tint).
void Icon(const Painter& painter, std::string contentDescription, const Modifier& modifier = {},
          std::optional<Color> tint = {}, CallSite site = CallSite::current());

struct IconButtonStyle {
    bool enabled = true;
    std::optional<Color> containerColor;
    std::optional<Color> contentColor;
    std::optional<MutableInteractionSource> interactionSource;  // told of presses, hovers and focus
};

void IconButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void IconButton(std::function<void()> onClick, const Modifier& modifier, const IconButtonStyle& style,
                const Content& content, CallSite site = CallSite::current());
void FilledIconButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void FilledIconButton(std::function<void()> onClick, const Modifier& modifier, const IconButtonStyle& style,
                      const Content& content, CallSite site = CallSite::current());
void FilledTonalIconButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void FilledTonalIconButton(std::function<void()> onClick, const Modifier& modifier, const IconButtonStyle& style,
                           const Content& content, CallSite site = CallSite::current());
// A 1dp outline around a transparent one (`containerColor` fills it; the outline stays).
void OutlinedIconButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void OutlinedIconButton(std::function<void()> onClick, const Modifier& modifier, const IconButtonStyle& style,
                        const Content& content, CallSite site = CallSite::current());

// Icon buttons that are on or off (Compose's IconToggleButton family): a tap calls
// onCheckedChange(!checked), and `checked` picks the colors — a star that turns primary, a
// filled one that goes from a gray container to a primary one, an outlined one that loses its
// outline for a dark (inverse surface) container. Read as a checkbox by accessibility services.
//
//   IconToggleButton(liked.get(), [liked](bool on) { liked.set(on); }, [&] {
//       Icon(liked.get() ? Icons::Filled::Favorite : Icons::Filled::FavoriteBorder, "Beğen");
//   });
struct IconToggleButtonStyle {
    bool enabled = true;
    std::optional<Color> containerColor;  // unchecked
    std::optional<Color> contentColor;
    std::optional<Color> checkedContainerColor;
    std::optional<Color> checkedContentColor;
    std::optional<MutableInteractionSource> interactionSource;
};
void IconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content,
                      CallSite site = CallSite::current());
void IconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                      const IconToggleButtonStyle& style, const Content& content, CallSite site = CallSite::current());
void FilledIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content,
                            CallSite site = CallSite::current());
void FilledIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                            const IconToggleButtonStyle& style, const Content& content, CallSite site = CallSite::current());
void FilledTonalIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content,
                                 CallSite site = CallSite::current());
void FilledTonalIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                                 const IconToggleButtonStyle& style, const Content& content,
                                 CallSite site = CallSite::current());
void OutlinedIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content,
                              CallSite site = CallSite::current());
void OutlinedIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                              const IconToggleButtonStyle& style, const Content& content,
                              CallSite site = CallSite::current());

// Indeterminate (spinning) and determinate (0..1) progress, Material 3 style.
void CircularProgressIndicator(const Modifier& modifier = {}, std::optional<Color> color = {}, float strokeWidth = 4,
                               CallSite site = CallSite::current());
void CircularProgressIndicator(float progress, const Modifier& modifier = {}, std::optional<Color> color = {},
                               float strokeWidth = 4, CallSite site = CallSite::current());
void LinearProgressIndicator(const Modifier& modifier = {}, std::optional<Color> color = {},
                             CallSite site = CallSite::current());
void LinearProgressIndicator(float progress, const Modifier& modifier = {}, std::optional<Color> color = {},
                             CallSite site = CallSite::current());

namespace WavyProgressIndicatorDefaults {
// The wave's height (0..1 of the full one) for a progress: flat near the start and the end.
inline float indicatorAmplitude(float progress) { return progress <= 0.1f || progress >= 0.95f ? 0.f : 1.f; }
inline constexpr float LinearContainerWidth = 240, LinearContainerHeight = 10, CircularContainerSize = 48;  // dp
inline constexpr float LinearDeterminateWavelength = 40, LinearIndeterminateWavelength = 20, CircularWavelength = 15;
inline constexpr float CircularAmplitude = 1.6f;  // dp up and down (a linear one fills its height)
} // namespace WavyProgressIndicatorDefaults

struct WavyProgressIndicatorOptions {
    std::optional<Color> color;       // primary
    std::optional<Color> trackColor;  // secondaryContainer
    float strokeWidth = 4, trackStrokeWidth = 4;  // dp, with round ends
    float gapSize = 4;   // between the indicator and the track
    float stopSize = 4;  // linear determinate: the dot that ends the track (0: none)
    // Determinate: the wave's height for a progress (empty: WavyProgressIndicatorDefaults::
    // indicatorAmplitude); changes animate. Indeterminate ones use indeterminateAmplitude.
    std::function<float(float progress)> amplitude;
    float indeterminateAmplitude = 1;
    std::optional<float> wavelength;  // dp; Defaults' by kind
    std::optional<float> waveSpeed;   // dp per second the wave moves while it's not flat (one wavelength)
};

// M3 Expressive's wavy progress (Compose's LinearWavyProgressIndicator and
// CircularWavyProgressIndicator): the indicator waves while it moves, the track stays flat
// behind a gap. Linear ones start at the layout's start side.
void LinearWavyProgressIndicator(float progress, const Modifier& modifier = {}, const WavyProgressIndicatorOptions& options = {},
                                 CallSite site = CallSite::current());
void LinearWavyProgressIndicator(const Modifier& modifier = {}, const WavyProgressIndicatorOptions& options = {},
                                 CallSite site = CallSite::current());
void CircularWavyProgressIndicator(float progress, const Modifier& modifier = {}, const WavyProgressIndicatorOptions& options = {},
                                   CallSite site = CallSite::current());
void CircularWavyProgressIndicator(const Modifier& modifier = {}, const WavyProgressIndicatorOptions& options = {},
                                   CallSite site = CallSite::current());

// M3 Expressive's loading indicator (Compose's LoadingIndicator / ContainedLoadingIndicator): a
// shape that keeps turning and morphing from one to the next (soft burst, cookie, pentagon, pill,
// sunny, four-sided cookie, oval); the determinate one grows from a circle into a soft burst,
// turning, as progress goes from 0 to 1. The contained one sits in a primaryContainer circle.
namespace LoadingIndicatorDefaults {
inline constexpr float ContainerSize = 48, IndicatorSize = 38;  // dp
inline constexpr float MorphMillis = 650, TurnMillis = 4666;    // a shape to the next; a whole turn
} // namespace LoadingIndicatorDefaults
void LoadingIndicator(const Modifier& modifier = {}, std::optional<Color> color = {}, CallSite site = CallSite::current());
void LoadingIndicator(float progress, const Modifier& modifier = {}, std::optional<Color> color = {}, CallSite site = CallSite::current());
void ContainedLoadingIndicator(const Modifier& modifier = {}, std::optional<Color> indicatorColor = {},
                               std::optional<Color> containerColor = {}, CallSite site = CallSite::current());
void ContainedLoadingIndicator(float progress, const Modifier& modifier = {}, std::optional<Color> indicatorColor = {},
                               std::optional<Color> containerColor = {}, CallSite site = CallSite::current());

void Card(const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
void ElevatedCard(const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
void OutlinedCard(const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
// Clickable cards (Compose's Card(onClick = ...)): the ripple keeps to their shape.
void Card(std::function<void()> onClick, const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
void ElevatedCard(std::function<void()> onClick, const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
void OutlinedCard(std::function<void()> onClick, const Modifier& modifier, const Content& content, CallSite site = CallSite::current());

void Surface(const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
void Surface(const Modifier& modifier, Color color, Color contentColor, const Content& content,
             CallSite site = CallSite::current());

void HorizontalDivider(const Modifier& modifier = {}, CallSite site = CallSite::current());
void VerticalDivider(const Modifier& modifier = {}, CallSite site = CallSite::current());

void Checkbox(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier = {},
              bool enabled = true, std::optional<MutableInteractionSource> interactionSource = {}, CallSite site = CallSite::current());
// A checkbox that can also be partly checked (Compose's TriStateCheckbox): a parent of
// checkboxes some of which are checked shows Indeterminate (a dash).
void TriStateCheckbox(ToggleableState state, std::function<void()> onClick, const Modifier& modifier = {}, bool enabled = true,
                      std::optional<MutableInteractionSource> interactionSource = {}, CallSite site = CallSite::current());

void Switch(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier = {},
            bool enabled = true, std::optional<MutableInteractionSource> interactionSource = {}, CallSite site = CallSite::current());
// thumbContent: an icon in the thumb (16 dp; onPrimaryContainer when on, surfaceContainerHighest
// when off), which keeps the thumb at its large size. The thumb grows to 28 dp while held.
struct SwitchOptions {
    bool enabled = true;
    std::optional<MutableInteractionSource> interactionSource;
    Content thumbContent;
};
void Switch(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier, const SwitchOptions& options,
            CallSite site = CallSite::current());

// What the keyboard's action key does; unset: Next/Previous move the focus, Done hides the
// keyboard (or runs TextFieldOptions::onSubmit).
struct KeyboardActions {
    std::function<void()> onDone, onGo, onNext, onPrevious, onSearch, onSend;
};

enum class VisualTransformation { None, Password };  // Password shows dots

// Text being changed, with its selection (Compose's TextFieldBuffer): what TextFieldState::edit
// and an InputTransformation get. Offsets are bytes of the UTF-8 text; a replacement moves
// the selection with the text after it (one inside the replaced range goes to its end).
class TextFieldBuffer {
public:
    TextFieldBuffer(std::string text, TextRange selection);  // nothing changed yet
    TextFieldBuffer(std::string original, TextRange originalSelection, std::string text, TextRange selection);

    const std::string& text() const { return text_; }
    int length() const { return int(text_.size()); }
    TextRange selection() const { return selection_; }
    bool hasSelection() const { return !selection_.collapsed(); }
    void setSelection(TextRange selection);  // clamped to the text

    void replace(int start, int end, std::string_view text);
    void insert(int index, std::string_view text) { replace(index, index, text); }
    void append(std::string_view text) { replace(length(), length(), text); }
    void remove(int start, int end) { replace(start, end, {}); }
    void placeCursorAtEnd() { setSelection({length(), length()}); }
    void placeCursorBeforeCharAt(int index) { setSelection({index, index}); }
    void selectAll() { setSelection({0, length()}); }

    // In an InputTransformation: the text before the user's edit, and refusing the edit.
    const std::string& originalText() const { return original_; }
    TextRange originalSelection() const { return originalSelection_; }
    void revertAllChanges();
    bool changed() const { return changed_; }  // since it was made (by anything but the user's edit)

    // Where each byte of the text came from: a byte of the original (its offset) or inserted at
    // an original offset. Kept while an OutputTransformation runs, to map the caret between them.
    struct Origin {
        int offset;
        bool inserted;
    };
    struct Tracking {
        std::vector<Origin> origins;
        int originalLength = 0;
    };
    void track(Tracking* tracking) { tracking_ = tracking; }

private:
    std::string original_, text_;
    TextRange originalSelection_, selection_;
    bool changed_ = false;
    Tracking* tracking_ = nullptr;
};

// What becomes of an edit the user types or pastes before the field keeps it (Compose's
// InputTransformation): change it (capitals), or refuse it (too long). Changes made in code
// (TextFieldState::edit, a new value) don't go through it.
//   TextField(state, Modifier(), {.inputTransformation = InputTransformation::maxLength(11)});
struct InputTransformation {
    std::function<void(TextFieldBuffer&)> transformInput;

    // Refuses an edit that would make the text longer than `characters` (code points).
    static InputTransformation maxLength(int characters);
    // Letters typed come in as capitals ("tr" or "az": i → İ, ı → I).
    static InputTransformation allCaps(std::string language = {});
    // The text to keep from the current and the proposed one: the proposed one keeps it as it
    // is, the current one refuses the edit, anything else replaces it (the caret at the end).
    static InputTransformation byValue(std::function<std::string(const std::string& current, const std::string& proposed)> transform);
    // This, then `next` on what it left.
    InputTransformation then(InputTransformation next) const;
};

// How the text is shown, not what it is (Compose's OutputTransformation): separators in a
// phone or card number, a unit after it. The value, the keyboard and copying keep the text
// itself; the caret and taps step over what was added.
//   TextField(phone, Modifier(), {.outputTransformation = OutputTransformation{[](TextFieldBuffer& b) {
//       for (int at : {8, 6, 3}) if (b.length() > at) b.insert(at, " ");  // 555 123 45 67
//   }}});
struct OutputTransformation {
    std::function<void(TextFieldBuffer&)> transformOutput;
};

// A field's colors (Compose's TextFieldColors, TextFieldDefaults.colors()): each part focused,
// unfocused, disabled and in error (disabled wins, then error, then focus). Unset ones are
// Material 3's: the text onSurface (a textStyle color wins); the container
// surfaceContainerHighest filled, none outlined; the cursor primary (error); the indicator
// primary focused, unfocused onSurfaceVariant filled and outline outlined, error; the label
// primary focused, onSurfaceVariant, error; icons, placeholder, supporting text, prefix and
// suffix onSurfaceVariant (the trailing icon and supporting text error in error); disabled,
// onSurface at 38% (the outline at 12%, prefix and suffix onSurfaceVariant at 38%, the container
// as it is); the selection LocalTextSelectionColors.
//   TextField(v, set, {}, {.colors = {.focusedContainerColor = cs.primaryContainer, .cursorColor = cs.tertiary}});
struct TextFieldColors {
    std::optional<Color> focusedTextColor, unfocusedTextColor, disabledTextColor, errorTextColor;
    std::optional<Color> focusedContainerColor, unfocusedContainerColor, disabledContainerColor, errorContainerColor;
    std::optional<Color> cursorColor, errorCursorColor;
    std::optional<TextSelectionColors> textSelectionColors;
    std::optional<Color> focusedIndicatorColor, unfocusedIndicatorColor, disabledIndicatorColor, errorIndicatorColor;
    std::optional<Color> focusedLeadingIconColor, unfocusedLeadingIconColor, disabledLeadingIconColor, errorLeadingIconColor;
    std::optional<Color> focusedTrailingIconColor, unfocusedTrailingIconColor, disabledTrailingIconColor, errorTrailingIconColor;
    std::optional<Color> focusedLabelColor, unfocusedLabelColor, disabledLabelColor, errorLabelColor;
    std::optional<Color> focusedPlaceholderColor, unfocusedPlaceholderColor, disabledPlaceholderColor, errorPlaceholderColor;
    std::optional<Color> focusedSupportingTextColor, unfocusedSupportingTextColor, disabledSupportingTextColor, errorSupportingTextColor;
    std::optional<Color> focusedPrefixColor, unfocusedPrefixColor, disabledPrefixColor, errorPrefixColor;
    std::optional<Color> focusedSuffixColor, unfocusedSuffixColor, disabledSuffixColor, errorSuffixColor;
};

// Where a field's label goes (Compose's TextFieldLabelPosition): Attached, inside it, floating
// up while it is focused or filled — or always up (alwaysMinimize: the placeholder, prefix and
// suffix show beside it while it is empty); Above, over the field outside it (bodySmall, at the
// start, the middle or the end).
struct TextFieldLabelPosition {
    enum class Kind { Attached, Above };
    Kind kind = Kind::Attached;
    bool alwaysMinimize = false;
    HorizontalAlignment alignment = Alignment::Start;  // Above's
    static TextFieldLabelPosition Attached(bool alwaysMinimize = false) { return {Kind::Attached, alwaysMinimize}; }
    static TextFieldLabelPosition Above(HorizontalAlignment alignment = Alignment::Start) { return {Kind::Above, false, alignment}; }
};

// How many lines a field has (Compose's TextFieldLineLimits, the state-based fields' way of
// saying singleLine / minLines / maxLines): SingleLine — one line, scrolled sideways, Enter runs
// the keyboard action; MultiLine(min, max) — wrapped, min..max lines tall, scrolled beyond.
struct TextFieldLineLimits {
    bool singleLine = false;
    int minHeightInLines = 1, maxHeightInLines = std::numeric_limits<int>::max();
    static const TextFieldLineLimits SingleLine, Default;  // Default: MultiLine()
    static TextFieldLineLimits MultiLine(int minHeightInLines = 1, int maxHeightInLines = std::numeric_limits<int>::max()) {
        return {false, minHeightInLines, maxHeightInLines};
    }
};
inline const TextFieldLineLimits TextFieldLineLimits::SingleLine{true, 1, 1};
inline const TextFieldLineLimits TextFieldLineLimits::Default{};

struct TextFieldOptions {
    // As in Compose, a field wraps its text over lines and Enter starts a new one unless it is
    // singleLine (then Enter runs the keyboard action). It is minLines..maxLines tall and
    // scrolls beyond.
    bool singleLine = false;
    int minLines = 1, maxLines = std::numeric_limits<int>::max();
    std::optional<TextFieldLineLimits> lineLimits;  // set: in place of the three above
    std::string placeholder;
    std::function<void()> onSubmit;  // Enter / Done, Go, Search, Send without their own action
    std::string label;               // floats above the text when focused or filled
    // Around the text, in a quieter color ("₺ 250", "70 kg"), once the label has floated
    // (always without a label); the text keeps clear of them.
    std::string prefix, suffix;
    Content leadingIcon, trailingIcon;
    std::string supportingText;
    bool isError = false;
    KeyboardOptions keyboardOptions;
    KeyboardActions keyboardActions;
    VisualTransformation visualTransformation = VisualTransformation::None;
    TextStyle textStyle;  // the text's font, color (onSurface by default) and alignment
    // false: neither focused, edited nor selected, in the disabled colors (Compose's enabled); a
    // focused field let go of the focus.
    bool enabled = true;
    bool readOnly = false;  // can be focused and its text selected, not edited (no caret, no keyboard)
    // Told of its focus and of presses on it (a read-only field that opens a picker on a
    // Release, as Compose apps do).
    std::optional<MutableInteractionSource> interactionSource;
    std::optional<InputTransformation> inputTransformation;  // what typing may change or refuse
    std::optional<OutputTransformation> outputTransformation;  // how the text is shown
    TextFieldColors colors;
    // The container's and outline's corners (Compose's shape). Unset: 4 dp at the top filled
    // (TextFieldDefaults.shape), all round outlined.
    std::optional<Shape> shape;
    TextFieldLabelPosition labelPosition;
    // BasicTextField's own decoration around its text (Compose's decorationBox / decorator):
    // call innerTextField where the text goes (once). A tap on the decoration that nothing in it
    // takes goes to the field, as on the text itself.
    //   BasicTextField(query, Modifier(), {.decorator = [&](const Content& innerTextField) {
    //       Row(Modifier().border(1, cs.outline, CircleShape).padding(12, 8), {.verticalAlignment = Alignment::CenterVertically}, [&] {
    //           Icon(Icons::Filled::Search, ""); Box(Modifier().weight(1).padding(8, 0, 0, 0), innerTextField);
    //       });
    //   }});
    std::function<void(const Content& innerTextField)> decorator;
};

// Text fields with hoisted state, like Compose's TextField(value, onValueChange): filled
// (TextField) and outlined. Tap to place the caret; with a hardware keyboard the arrows,
// Home/End, Delete and Ctrl+A/C/X/V work too.
void TextField(const std::string& value, std::function<void(std::string)> onValueChange,
               const Modifier& modifier = {}, TextFieldOptions options = {}, CallSite site = CallSite::current());
void OutlinedTextField(const std::string& value, std::function<void(std::string)> onValueChange,
                       const Modifier& modifier = {}, TextFieldOptions options = {}, CallSite site = CallSite::current());
// Editable text without decoration (Compose's BasicTextField): no container, padding or label;
// the placeholder shows while it is empty. For building other fields (a search bar's).
void BasicTextField(const std::string& value, std::function<void(std::string)> onValueChange,
                    const Modifier& modifier = {}, TextFieldOptions options = {}, CallSite site = CallSite::current());

// The text of a field kept in a state object instead of hoisted (Compose's TextFieldState):
//   auto notes = rememberTextFieldState();
//   OutlinedTextField(notes, Modifier().fillMaxWidth(), {.label = "Not", .maxLines = 5});
//   Button([notes] { save(notes.text()); notes.clearText(); }, ...);
//
// Its selection is kept too: read it, or change both in one go (Compose's edit { }):
//   notes.edit([](TextFieldBuffer& b) { b.append("\n— imza"); b.placeCursorAtEnd(); });
namespace detail {
struct TextHistory;
}

// A TextFieldState's undo history (Compose's TextFieldState.undoState): what is typed comes
// back a word at a time, what is deleted in one go; an edit made in code is a step of its own.
// Its field undoes with Ctrl+Z and redoes with Ctrl+Shift+Z or Ctrl+Y.
//   TextButton([notes] { notes.undoState().undo(); }, Modifier(), {.enabled = notes.undoState().canUndo()}, ...);
class UndoState {
public:
    bool canUndo() const;  // observable
    bool canRedo() const;
    void undo() const;
    void redo() const;
    void clearHistory() const;

private:
    friend class TextFieldState;
    explicit UndoState(std::shared_ptr<detail::TextHistory> history) : history_(std::move(history)) {}
    std::shared_ptr<detail::TextHistory> history_;
};

class TextFieldState {
public:
    explicit TextFieldState(std::string initialText = {});  // the caret at its end
    std::string text() const;      // observable
    TextRange selection() const;   // observable; in bytes of the text
    void edit(const std::function<void(TextFieldBuffer&)>& change) const;
    void setTextAndPlaceCursorAtEnd(std::string text) const;
    void setTextAndSelectAll(std::string text) const;
    void clearText() const { setTextAndPlaceCursorAtEnd({}); }
    UndoState undoState() const { return UndoState(history_); }
    friend bool operator==(const TextFieldState& a, const TextFieldState& b) { return a.text_ == b.text_; }

private:
    friend struct TextFieldStateAccess;
    enum class Source { User, Code };  // typed (steps may merge into the last one), set in code
    void set(std::string text, TextRange selection, Source source = Source::Code) const;
    MutableState<std::string> text_;
    MutableState<TextRange> selection_;
    std::shared_ptr<detail::TextHistory> history_;
};
template <>
struct Saver<TextFieldState> {
    static SavedValue save(const TextFieldState& s) { return s.text(); }
    static std::optional<TextFieldState> restore(const SavedValue& v) {
        return v.string() ? std::optional(TextFieldState(*v.string())) : std::nullopt;
    }
};
TextFieldState rememberTextFieldState(std::string initialText = {}, CallSite site = CallSite::current());
void TextField(const TextFieldState& state, const Modifier& modifier = {}, TextFieldOptions options = {},
               CallSite site = CallSite::current());
void BasicTextField(const TextFieldState& state, const Modifier& modifier = {}, TextFieldOptions options = {},
                    CallSite site = CallSite::current());
void OutlinedTextField(const TextFieldState& state, const Modifier& modifier = {}, TextFieldOptions options = {},
                       CallSite site = CallSite::current());

// ---- Context menus (Compose Multiplatform's ContextMenuArea) ----

struct ContextMenuItem {
    std::string label;
    std::function<void()> onClick;
    bool enabled = true;
};
// A secondary (right) click anywhere on `content` opens these items there, on the desktop;
// picking one runs it and closes the menu, as do a click outside and Escape. Text fields have
// their own (cut, copy, paste, select all).
//   ContextMenuArea([&] { return std::vector<ContextMenuItem>{{"Yenile", reload}, {"Sil", remove}}; }, [&] { Card(...); });
void ContextMenuArea(std::function<std::vector<ContextMenuItem>()> items, const Content& content, const Modifier& modifier = {},
                     bool enabled = true, CallSite site = CallSite::current());

// How a secure field shows its text (Compose's TextObfuscationMode).
enum class TextObfuscationMode {
    RevealLastTyped,  // dots, but the character just typed shows for a moment, as on phones
    Hidden,           // dots only
    Visible,          // the text itself (a "show password" button); still never copied
};

struct SecureTextFieldOptions {
    std::string label, placeholder;
    std::string prefix, suffix;
    Content leadingIcon, trailingIcon;  // e.g. an eye that switches textObfuscationMode
    std::string supportingText;
    bool isError = false;
    TextObfuscationMode textObfuscationMode = TextObfuscationMode::RevealLastTyped;
    std::string textObfuscationCharacter = "•";  // one character
    KeyboardOptions keyboardOptions{.autoCorrect = false, .keyboardType = KeyboardType::Password};
    KeyboardActions keyboardActions;
    std::function<void()> onSubmit;  // Enter / Done without its own action
    TextStyle textStyle;
    bool enabled = true;
    std::optional<MutableInteractionSource> interactionSource;
    std::optional<InputTransformation> inputTransformation;
    TextFieldColors colors;
    std::optional<Shape> shape;
    TextFieldLabelPosition labelPosition;
    std::function<void(const Content& innerTextField)> decorator;  // BasicSecureTextField's (see TextFieldOptions)
};
// Password fields (Compose's SecureTextField, OutlinedSecureTextField): one line, the text shown
// as dots and never copied or cut, a password keyboard without suggestions.
//   auto password = rememberTextFieldState();
//   SecureTextField(password, Modifier(), {.label = "Parola"});
void SecureTextField(const TextFieldState& state, const Modifier& modifier = {}, SecureTextFieldOptions options = {},
                     CallSite site = CallSite::current());
void OutlinedSecureTextField(const TextFieldState& state, const Modifier& modifier = {}, SecureTextFieldOptions options = {},
                             CallSite site = CallSite::current());
// The same without decoration (Compose's BasicSecureTextField): the dots alone, for a field
// of your own (the label and supporting text aside).
void BasicSecureTextField(const TextFieldState& state, const Modifier& modifier = {}, SecureTextFieldOptions options = {},
                          CallSite site = CallSite::current());

} // namespace kompoz
