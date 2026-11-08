#pragma once

// Foundation layouts (Box, Row, Column, Text, Spacer) and a small Material-like component set.
// Arguments that are named in Kotlin are passed as designated initializers here:
//   Column(Modifier().fillMaxSize(), verticalArrangement = Arrangement::spacedBy(8), [=] { ... });

#include "animation.hpp"
#include "layout.hpp"
#include "text.hpp"
#include "named.hpp"
#include "runtime.hpp"

#include <functional>
#include <map>
#include <limits>
#include <optional>
#include <string>
#include <variant>

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
    // Compose's copy(primary = …) (below).
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    ColorScheme copy(A&&... args) const;
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
// Material You from the wallpaper itself, the way Android 12 makes it, also where the system
// doesn't (LocalWallpaperTonalPalette): Linux desktops (KDE Plasma; GNOME and the desktops that
// keep its settings, Cinnamon and MATE; Xfce), Windows and Android 8.1+. Followed as the wallpaper
// changes; none where it can't be read (the web), and none for a moment after the app starts,
// while it is read. Where there is none the schemes are the baseline ones.
bool isWallpaperColorAvailable();
ColorScheme wallpaperLightColorScheme();
ColorScheme wallpaperDarkColorScheme();
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
    // Compose's copy(headlineSmall = …) (below).
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    Typography copy(A&&... args) const;
};
// Compose's typography.copy(headlineSmall = …) (Typography(displayLarge = …) is the struct's own init).
template <class... A>
    requires(detail::anyNamedArgument<A...>)
Typography Typography::copy(A&&... args) const {
    namespace n = detail::names;
    using P = detail::Params<n::DisplayLarge, n::DisplayMedium, n::DisplaySmall, n::HeadlineLarge, n::HeadlineMedium, n::HeadlineSmall, n::TitleLarge, n::TitleMedium, n::BodyLarge, n::TitleSmall, n::BodyMedium, n::BodySmall, n::LabelLarge, n::LabelMedium, n::LabelSmall>;
    static_assert(P::check<A...>());
    Typography t = *this;
    P::set<n::DisplayLarge>(t.displayLarge, args...);
    P::set<n::DisplayMedium>(t.displayMedium, args...);
    P::set<n::DisplaySmall>(t.displaySmall, args...);
    P::set<n::HeadlineLarge>(t.headlineLarge, args...);
    P::set<n::HeadlineMedium>(t.headlineMedium, args...);
    P::set<n::HeadlineSmall>(t.headlineSmall, args...);
    P::set<n::TitleLarge>(t.titleLarge, args...);
    P::set<n::TitleMedium>(t.titleMedium, args...);
    P::set<n::BodyLarge>(t.bodyLarge, args...);
    P::set<n::TitleSmall>(t.titleSmall, args...);
    P::set<n::BodyMedium>(t.bodyMedium, args...);
    P::set<n::BodySmall>(t.bodySmall, args...);
    P::set<n::LabelLarge>(t.labelLarge, args...);
    P::set<n::LabelMedium>(t.labelMedium, args...);
    P::set<n::LabelSmall>(t.labelSmall, args...);
    return t;
}



// Compose's named forms: lightColorScheme(primary = …, onPrimary = …), darkColorScheme(…) and
// scheme.copy(primary = …): the baseline scheme (or this one) with those colors changed.
namespace detail {
template <class... A>
ColorScheme withColors(ColorScheme cs, A&&... args) {
    namespace n = detail::names;
    using P = detail::Params<n::Primary, n::OnPrimary, n::PrimaryContainer, n::OnPrimaryContainer, n::Background, n::OnBackground, n::Surface, n::OnSurface, n::SurfaceContainer, n::OnSurfaceVariant, n::Outline, n::OutlineVariant, n::Error, n::InverseSurface, n::InverseOnSurface, n::InversePrimary, n::SecondaryContainer, n::OnSecondaryContainer, n::Secondary, n::OnSecondary, n::Tertiary, n::OnTertiary, n::OnError, n::SurfaceContainerLow, n::SurfaceContainerHighest, n::TertiaryContainer, n::OnTertiaryContainer, n::ErrorContainer, n::OnErrorContainer, n::SurfaceVariant, n::SurfaceTint, n::Scrim, n::SurfaceBright, n::SurfaceDim, n::SurfaceContainerLowest, n::PrimaryFixed, n::PrimaryFixedDim, n::OnPrimaryFixed, n::OnPrimaryFixedVariant, n::SecondaryFixed, n::SecondaryFixedDim, n::OnSecondaryFixed, n::OnSecondaryFixedVariant, n::TertiaryFixed, n::TertiaryFixedDim, n::OnTertiaryFixed, n::OnTertiaryFixedVariant>;
    static_assert(P::check<A...>());
    P::set<n::Primary>(cs.primary, args...);
    P::set<n::OnPrimary>(cs.onPrimary, args...);
    P::set<n::PrimaryContainer>(cs.primaryContainer, args...);
    P::set<n::OnPrimaryContainer>(cs.onPrimaryContainer, args...);
    P::set<n::Background>(cs.background, args...);
    P::set<n::OnBackground>(cs.onBackground, args...);
    P::set<n::Surface>(cs.surface, args...);
    P::set<n::OnSurface>(cs.onSurface, args...);
    P::set<n::SurfaceContainer>(cs.surfaceContainer, args...);
    P::set<n::OnSurfaceVariant>(cs.onSurfaceVariant, args...);
    P::set<n::Outline>(cs.outline, args...);
    P::set<n::OutlineVariant>(cs.outlineVariant, args...);
    P::set<n::Error>(cs.error, args...);
    P::set<n::InverseSurface>(cs.inverseSurface, args...);
    P::set<n::InverseOnSurface>(cs.inverseOnSurface, args...);
    P::set<n::InversePrimary>(cs.inversePrimary, args...);
    P::set<n::SecondaryContainer>(cs.secondaryContainer, args...);
    P::set<n::OnSecondaryContainer>(cs.onSecondaryContainer, args...);
    P::set<n::Secondary>(cs.secondary, args...);
    P::set<n::OnSecondary>(cs.onSecondary, args...);
    P::set<n::Tertiary>(cs.tertiary, args...);
    P::set<n::OnTertiary>(cs.onTertiary, args...);
    P::set<n::OnError>(cs.onError, args...);
    P::set<n::SurfaceContainerLow>(cs.surfaceContainerLow, args...);
    P::set<n::SurfaceContainerHighest>(cs.surfaceContainerHighest, args...);
    P::set<n::TertiaryContainer>(cs.tertiaryContainer, args...);
    P::set<n::OnTertiaryContainer>(cs.onTertiaryContainer, args...);
    P::set<n::ErrorContainer>(cs.errorContainer, args...);
    P::set<n::OnErrorContainer>(cs.onErrorContainer, args...);
    P::set<n::SurfaceVariant>(cs.surfaceVariant, args...);
    P::set<n::SurfaceTint>(cs.surfaceTint, args...);
    P::set<n::Scrim>(cs.scrim, args...);
    P::set<n::SurfaceBright>(cs.surfaceBright, args...);
    P::set<n::SurfaceDim>(cs.surfaceDim, args...);
    P::set<n::SurfaceContainerLowest>(cs.surfaceContainerLowest, args...);
    P::set<n::PrimaryFixed>(cs.primaryFixed, args...);
    P::set<n::PrimaryFixedDim>(cs.primaryFixedDim, args...);
    P::set<n::OnPrimaryFixed>(cs.onPrimaryFixed, args...);
    P::set<n::OnPrimaryFixedVariant>(cs.onPrimaryFixedVariant, args...);
    P::set<n::SecondaryFixed>(cs.secondaryFixed, args...);
    P::set<n::SecondaryFixedDim>(cs.secondaryFixedDim, args...);
    P::set<n::OnSecondaryFixed>(cs.onSecondaryFixed, args...);
    P::set<n::OnSecondaryFixedVariant>(cs.onSecondaryFixedVariant, args...);
    P::set<n::TertiaryFixed>(cs.tertiaryFixed, args...);
    P::set<n::TertiaryFixedDim>(cs.tertiaryFixedDim, args...);
    P::set<n::OnTertiaryFixed>(cs.onTertiaryFixed, args...);
    P::set<n::OnTertiaryFixedVariant>(cs.onTertiaryFixedVariant, args...);
    return cs;
}
} // namespace detail
template <class... A>
    requires(detail::anyNamedArgument<A...>)
ColorScheme lightColorScheme(A&&... args) {
    return detail::withColors(lightColorScheme(), std::forward<A>(args)...);
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
ColorScheme darkColorScheme(A&&... args) {
    return detail::withColors(darkColorScheme(), std::forward<A>(args)...);
}
template <class... A>
    requires(detail::anyNamedArgument<A...>)
ColorScheme ColorScheme::copy(A&&... args) const {
    return detail::withColors(*this, std::forward<A>(args)...);
}

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

// Material 3's shape scale (Compose's Shapes), for the app's own components: shape =
// MaterialTheme.shapes.medium. Kompoz's components have Material's shapes built in.
struct Shapes {
    Shape extraSmall = RoundedCornerShape(4), small = RoundedCornerShape(8), medium = RoundedCornerShape(12), large = RoundedCornerShape(16),
          extraLarge = RoundedCornerShape(28);
    friend bool operator==(const Shapes&, const Shapes&) = default;
    // Compose's copy(medium = …).
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    Shapes copy(A&&... args) const {
        namespace n = detail::names;
        using P = detail::Params<n::ExtraSmall, n::Small, n::Medium, n::Large, n::ExtraLarge>;
        static_assert(P::check<A...>());
        Shapes s = *this;
        P::set<n::ExtraSmall>(s.extraSmall, args...);
        P::set<n::Small>(s.small, args...);
        P::set<n::Medium>(s.medium, args...);
        P::set<n::Large>(s.large, args...);
        P::set<n::ExtraLarge>(s.extraLarge, args...);
        return s;
    }
};
// (read as LocalShapes.current(): `shapes` is the buttons' named argument)
inline const CompositionLocal<Shapes> LocalShapes{Shapes{}};

// MaterialTheme(colorScheme, motionScheme, content): the motion left out stays the one around it
// (standard() at the top). Its content runs as a call's own, in its caller's scope.
namespace detail {
void MaterialTheme(const ColorScheme& colorScheme, const Content& content, CallSite site = CallSite::current());
void MaterialTheme(const ColorScheme& colorScheme, const MotionScheme& motionScheme, const Content& content,
                   CallSite site = CallSite::current());
// MaterialTheme(colorScheme, typography, shapes, motionScheme) { … }: a typography or shapes given go
// to everything inside (LocalTypography, LocalShapes), as Compose's.
struct MaterialThemeImpl {
    using P = Params<names::ColorScheme, names::MotionScheme, names::Typography, names::Shapes, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: MaterialTheme takes a block: its content");
        // (no colorScheme: the one around it, as Compose's default MaterialTheme.colorScheme)
        ColorScheme colorScheme = P::get<names::ColorScheme>(LocalColorScheme.current(), args...);
        Content content = P::plainBlock(std::forward<A>(args)...);
        if constexpr (P::given<names::Typography, A...>()) {
            Typography typography = P::arg<names::Typography>(args...);
            content = [typography, inner = std::move(content)] { CompositionLocalProvider({LocalTypography.provides(typography)}, inner); };
        }
        if constexpr (P::given<names::Shapes, A...>()) {
            Shapes shapes = P::arg<names::Shapes>(args...);
            content = [shapes, inner = std::move(content)] { CompositionLocalProvider({LocalShapes.provides(shapes)}, inner); };
        }
        if constexpr (P::given<names::MotionScheme, A...>())
            MaterialTheme(colorScheme, P::get<names::MotionScheme>(MotionScheme::standard(), args...), content, site);
        else MaterialTheme(colorScheme, content, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::MaterialThemeImpl> MaterialTheme;
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

namespace detail {
void LookaheadScope(const Modifier& modifier, const Content& content, CallSite site);
struct LookaheadScopeImpl {
    using P = Params<names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        LookaheadScope(P::get<names::Modifier>(Modifier{}, args...), P::plainBlock(std::forward<A>(args)...), site);
    }
};
} // namespace detail
// A Box whose content's animateBounds measure their places from it (Compose's LookaheadScope).
inline constexpr Composable<detail::LookaheadScopeImpl> LookaheadScope;

namespace detail {
// What the composables below make. The library's own composables call these, with content of
// theirs that isn't run in a scope of its own (it may refer to what is theirs).
void Box(const Content& content, CallSite site = CallSite::current());
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

struct BoxImpl {
    using P = Params<names::Modifier, names::ContentAlignment, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        detail::Box(P::get<names::Modifier>(Modifier{}, args...), P::get<names::ContentAlignment>(Alignment{Alignment::TopStart}, args...), P::block(std::forward<A>(args)...), site);
    }
};
struct ColumnImpl {
    using P = Params<names::Modifier, names::VerticalArrangement, names::HorizontalAlignment, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        detail::Column(P::get<names::Modifier>(Modifier{}, args...), {P::get<names::VerticalArrangement>(Arrangement{Arrangement::Top}, args...), P::get<names::HorizontalAlignment>(HorizontalAlignment{Alignment::Start}, args...)}, P::block(std::forward<A>(args)...), site);
    }
};
struct RowImpl {
    using P = Params<names::Modifier, names::HorizontalArrangement, names::VerticalAlignment, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        detail::Row(P::get<names::Modifier>(Modifier{}, args...), {P::get<names::HorizontalArrangement>(Arrangement{Arrangement::Start}, args...), P::get<names::VerticalAlignment>(VerticalAlignment{Alignment::Top}, args...)}, P::block(std::forward<A>(args)...), site);
    }
};
struct SpacerImpl {
    using P = Params<names::Modifier>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        detail::Spacer(P::get<names::Modifier>(Modifier{}, args...), site);
    }
};
} // namespace detail

// Its children on top of each other (Compose's Box): Box(modifier, contentAlignment) { … }.
inline constexpr Composable<detail::BoxImpl> Box;
// Its children one under the other: Column(modifier, verticalArrangement, horizontalAlignment) { … }.
inline constexpr Composable<detail::ColumnImpl> Column;
// Its children side by side: Row(modifier, horizontalArrangement, verticalAlignment) { … }.
inline constexpr Composable<detail::RowImpl> Row;
// Room taken by its modifier alone: Spacer(Modifier().height(8)).
inline constexpr Composable<detail::SpacerImpl> Spacer;

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
    // x from the start: a layout laid out right to left mirrors what it placed. Inside layout(width,
    // height) { … } (Compose's PlacementScope), from the left, as Compose's place is.
    void place(float x, float y, float zIndex = 0) const {
        bool fromLeft = node->rtl && detail::placementWidth >= 0;  // (mirrored back by the layout)
        node->place(fromLeft ? detail::placementWidth - x - width : x, y, zIndex);
    }
    // Compose's placeRelative: x from the start, mirrored right to left.
    void placeRelative(float x, float y, float zIndex = 0) const { node->place(x, y, zIndex); }
    // By name, as Compose's: placeRelative(x = 0, y = top), place(x = …, y = …, zIndex = …).
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    void place(A&&... args) const {
        namespace n = detail::names;
        using P = detail::Params<n::X, n::Y, n::ZIndex>;
        static_assert(P::check<A...>());
        place(float(P::arg<n::X>(args...)), float(P::arg<n::Y>(args...)), P::get<n::ZIndex>(0.f, args...));
    }
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    void placeRelative(A&&... args) const {
        namespace n = detail::names;
        using P = detail::Params<n::X, n::Y, n::ZIndex>;
        static_assert(P::check<A...>());
        placeRelative(float(P::arg<n::X>(args...)), float(P::arg<n::Y>(args...)), P::get<n::ZIndex>(0.f, args...));
    }
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
namespace detail {
// One node per child of `content` (each child's own nodes, as emitted).
void Layout(const Modifier& modifier, const Content& content, LayoutMeasurePolicy measurePolicy, CallSite site);
// Layout(content, modifier) { measurables, constraints -> layout(w, h) { … } } as Compose's (a
// MeasureScope the policy's receiver), or Layout(modifier, content, policy) with a LayoutMeasurePolicy.
struct LayoutImpl {
    static constexpr bool takesContentByName = true;  // (its content isn't its block: the policy is)
    using Policy = BlockWith<MeasureScope&, const std::vector<Measurable>&, const Constraints&>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (startsWith<Modifier, A...>()) {
            static_assert(sizeof...(A) == 3, "kompoz: Layout(modifier, content, measurePolicy)");
            auto all = std::forward_as_tuple(std::forward<A>(args)...);
            Layout(std::get<0>(all), Content(std::get<1>(all)), LayoutMeasurePolicy(std::get<2>(all)), site);
        } else {
            using P = Params<names::Content, names::Modifier, Policy>;
            static_assert(P::template check<A...>());
            static_assert(P::template blockGiven<A...>(), "kompoz: Layout(content, modifier) { measurables, constraints -> … }");
            auto&& policy = std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...));
            Layout(P::template get<names::Modifier>(Modifier{}, args...), Content(P::template arg<names::Content>(args...)),
                   [policy](const std::vector<Measurable>& children, const Constraints& c) mutable {
                       MeasureScope scope;
                       return Size(policy(scope, children, c));
                   },
                   site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::LayoutImpl> Layout;
namespace detail {
// A menu's rows, one under another: as wide as the widest (112 dp to `maxWidth`), each that wide.
LayoutMeasurePolicy menuRows(float maxWidth);
} // namespace detail

// A layout that composes some of its content while it measures, from what it knows then
// (Compose's SubcomposeLayout): subcompose(slotId, content) gives the slot's measurables — a
// slot that depends on another's size, or one picked by the constraints. A slot asked for the
// first time (or whose `dependency` changed) is composed there and then, as Compose's is, its
// measurables given to the same measure (the policy runs once more after, all of them composed);
// one no longer asked for leaves after the measure. Its content may run again later, in a
// composition: capture by value.
//   SubcomposeLayout(Modifier(), [](SubcomposeMeasureScope& s, const Constraints& c) {
//       auto main = s.subcompose("main", [] { Text("Başlık"); });
//       Size size = ...;  // measure and place them
//       for (auto& m : s.subcompose("badge", size, [size] { Badge(size); })) ...
//       return size;
//   });
class SubcomposeMeasureScope : public MeasureScope {  // (layout(w, h) { … } too)
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
namespace detail {
void SubcomposeLayout(const Modifier& modifier, SubcomposeMeasurePolicy measurePolicy, CallSite site);
struct SubcomposeLayoutImpl {
    using P = Params<names::Modifier, BlockWith<SubcomposeMeasureScope&, const Constraints&>>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: SubcomposeLayout(modifier) { constraints -> … }");
        auto&& policy = std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...));
        SubcomposeLayout(P::get<names::Modifier>(Modifier{}, args...),
                         [policy](SubcomposeMeasureScope& scope, const Constraints& c) mutable { return Size(policy(scope, c)); }, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::SubcomposeLayoutImpl> SubcomposeLayout;

// Rows (columns) that wrap to the next line when full, e.g. for chips.
struct FlowStyle {
    float horizontalSpacing = 0, verticalSpacing = 0;
    int maxItemsInEachLine = 0;  // 0: as many as fit
};
namespace detail {
void FlowRow(const Modifier& modifier, const FlowStyle& style, const Content& content, CallSite site);
void FlowColumn(const Modifier& modifier, const FlowStyle& style, const Content& content, CallSite site);
// FlowRow(modifier, horizontalArrangement, verticalArrangement, maxItemsInEachLine) { … }, as
// Compose's (the arrangements' spacing between items and lines), or FlowRow(modifier, FlowStyle{…}).
template <bool Row>
struct FlowImpl {
    using P = std::conditional_t<Row, Params<names::Modifier, names::HorizontalArrangement, names::VerticalArrangement, names::MaxItemsInEachLine, Block>,
                                 Params<names::Modifier, names::VerticalArrangement, names::HorizontalArrangement, names::MaxItemsInEachLine, Block>>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        constexpr bool styled = sizeof...(A) == 3 && std::is_same_v<std::remove_cvref_t<std::tuple_element_t<1, std::tuple<A..., void, void>>>, FlowStyle>;
        if constexpr (styled) {
            auto all = std::forward_as_tuple(std::forward<A>(args)...);
            (Row ? FlowRow : FlowColumn)(std::get<0>(all), std::get<1>(all), Content(std::get<2>(all)), site);
        } else {
            static_assert(P::template check<A...>());
            FlowStyle style;
            style.horizontalSpacing = P::template get<names::HorizontalArrangement>(Arrangement{}, args...).spacing;
            style.verticalSpacing = P::template get<names::VerticalArrangement>(Arrangement{}, args...).spacing;
            int max = P::template get<names::MaxItemsInEachLine>(0, args...);
            style.maxItemsInEachLine = max == std::numeric_limits<int>::max() ? 0 : max;
            (Row ? FlowRow : FlowColumn)(P::template get<names::Modifier>(Modifier{}, args...), style, P::block(std::forward<A>(args)...), site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::FlowImpl<true>> FlowRow;
inline constexpr Composable<detail::FlowImpl<false>> FlowColumn;

// A layout of CSS's flexbox (Compose 1.12's FlexBox, a superset of Row, Column, FlowRow and
// FlowColumn): its children along the main axis (direction), wrapping to more lines or not,
// spread along it (justifyContent), lined up across their line (alignItems; a child's own
// alignSelf), the lines spread across (alignContent, when wrapping), rowGap and columnGap
// between them. Each child takes its share with Modifier().flex({.grow, .shrink, .basis, ...}).
//   FlexBox(Modifier().fillMaxWidth(), FlexBoxConfig{.wrap = FlexWrap::Wrap, .justifyContent = FlexJustifyContent::SpaceBetween, .columnGap = 8}, [] {
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
// Compose's FlexBoxConfigScope: the receiver of FlexBox's config = { direction(…); gap(8.dp) }, run
// while it is laid out (a state read there lays it out again), `constraints` the box's.
class FlexBoxConfigScope {
public:
    explicit FlexBoxConfigScope(const Constraints& constraints) : constraints(constraints) {}
    Constraints constraints;
    void direction(FlexDirection value) { config_.direction = value; }
    void wrap(FlexWrap value) { config_.wrap = value; }
    void justifyContent(FlexJustifyContent value) { config_.justifyContent = value; }
    void alignItems(FlexAlignItems value) { config_.alignItems = value; }
    void alignItems(AlignmentLine line) { config_.alignItems = FlexAlignItems::Baseline, config_.baselineLine = line; }
    void alignContent(FlexAlignContent value) { config_.alignContent = value; }
    void rowGap(float value) { config_.rowGap = value; }
    void columnGap(float value) { config_.columnGap = value; }
    void gap(float all) { config_.rowGap = config_.columnGap = all; }
    void gap(float row, float column) { config_.rowGap = row, config_.columnGap = column; }
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    void gap(A&&... args) {  // gap(row = 8.dp, column = 16.dp)
        using P = detail::Params<detail::names::Row, detail::names::Column>;
        static_assert(P::check<A...>());
        gap(float(P::arg<detail::names::Row>(args...)), float(P::arg<detail::names::Column>(args...)));
    }
    const FlexBoxConfig& flexBoxConfig() const { return config_; }

private:
    FlexBoxConfig config_;
};
namespace detail {
void FlexBox(const Modifier& modifier, const FlexBoxConfig& config, const Content& content, CallSite site);
// The config by the constraints it is laid out with: wrap only when narrow, say.
void FlexBox(const Modifier& modifier, std::function<FlexBoxConfig(const Constraints&)> config, const Content& content, CallSite site);
// FlexBox(modifier, config) { … }: config a FlexBoxConfig, Compose's lambda on a FlexBoxConfigScope,
// or a function of the constraints giving a FlexBoxConfig.
struct FlexBoxImpl {
    using P = Params<names::Modifier, names::Config, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        Modifier modifier = P::get<names::Modifier>(Modifier{}, args...);
        if constexpr (!P::given<names::Config, A...>()) {
            FlexBox(modifier, FlexBoxConfig{}, P::block(std::forward<A>(args)...), site);
        } else {
            auto&& config = P::arg<names::Config>(args...);
            using C = std::remove_cvref_t<decltype(config)>;
            if constexpr (std::is_convertible_v<const C&, const FlexBoxConfig&>) {
                FlexBox(modifier, config, P::block(std::forward<A>(args)...), site);
            } else if constexpr (std::is_invocable_v<C&, FlexBoxConfigScope&>) {
                FlexBox(modifier, std::function<FlexBoxConfig(const Constraints&)>([config](const Constraints& c) mutable {
                            FlexBoxConfigScope scope(c);
                            config(scope);
                            return scope.flexBoxConfig();
                        }),
                        P::block(std::forward<A>(args)...), site);
            } else {
                FlexBox(modifier, std::function<FlexBoxConfig(const Constraints&)>(config), P::block(std::forward<A>(args)...), site);
            }
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::FlexBoxImpl> FlexBox;

// A layout of rows and columns (Compose 1.12's Grid, CSS's grid): the tracks it's configured
// with, more Auto ones as its children need; each child in the cells it asks for
// (Modifier().gridItem), else in the next free ones along the flow.
//   Grid(Modifier().fillMaxWidth(), GridConfig{.columns = {GridTrackSize::Fixed(72), GridTrackSize::Flex(1)}, .rowGap = 8, .columnGap = 8,
//                                    .areas = {{"başlık", 1, 1, 1, 2}}}, [] {
//       Text("Başlık", Modifier().gridItem("başlık"));
//       Icon(icons::Person, "");
//       Text("Ad");
//   });
// A track's size: Fixed dp; a Percentage of the grid after the gaps (the content's, unbounded);
// Flex: a share of the room left, by weight, from its min content (MinMax: from `minDp`);
// MinContent / MaxContent of its items; Auto: from min towards max content in the room left.
// A flexible track's share (Compose's Fr, 1.fr).
struct Fr {
    float weight = 1;
};
struct GridTrackSize {
    enum class Kind { Fixed, Percentage, Flex, MinContent, MaxContent, Auto, MinMax };
    Kind kind = Kind::Auto;
    float value = 0, weight = 0;  // dp, fraction or weight; MinMax: min dp and weight
    static constexpr GridTrackSize Fixed(float dp) { return {Kind::Fixed, dp < 0 ? 0 : dp}; }
    static constexpr GridTrackSize Percentage(float fraction) { return {Kind::Percentage, fraction < 0 ? 0 : fraction}; }
    static constexpr GridTrackSize Flex(float weight) { return {Kind::Flex, weight < 0 ? 0 : weight}; }
    static constexpr GridTrackSize MinMax(float minDp, float weight) { return {Kind::MinMax, minDp < 0 ? 0 : minDp, weight < 0 ? 0 : weight}; }
    static constexpr GridTrackSize Flex(Fr weight) { return Flex(weight.weight); }
    static constexpr GridTrackSize MinMax(float minDp, Fr weight) { return MinMax(minDp, weight.weight); }
    static const GridTrackSize MinContent, MaxContent, Auto;
    friend constexpr bool operator==(const GridTrackSize&, const GridTrackSize&) = default;
};
inline constexpr GridTrackSize GridTrackSize::MinContent{GridTrackSize::Kind::MinContent};
inline constexpr GridTrackSize GridTrackSize::MaxContent{GridTrackSize::Kind::MaxContent};
inline constexpr GridTrackSize GridTrackSize::Auto{};
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
// Compose's GridConfigurationScope: the receiver of Grid's config = { column(100.dp); column(1.fr);
// gap(8.dp) }, run while it is laid out, `constraints` the grid's. (A Dp is a float here, so a
// percentage track is GridTrackSize.Percentage(0.5f), not column(0.5f).)
class GridConfigurationScope {
public:
    explicit GridConfigurationScope(const Constraints& constraints) : constraints(constraints) {}
    Constraints constraints;
    GridFlow& flow = config_.flow;
    void column(float size) { config_.columns.push_back(GridTrackSize::Fixed(size)); }
    void column(Fr weight) { config_.columns.push_back(GridTrackSize::Flex(weight)); }
    void column(GridTrackSize size) { config_.columns.push_back(size); }
    void row(float size) { config_.rows.push_back(GridTrackSize::Fixed(size)); }
    void row(Fr weight) { config_.rows.push_back(GridTrackSize::Flex(weight)); }
    void row(GridTrackSize size) { config_.rows.push_back(size); }
    template <class... T>
    void columns(const T&... sizes) { (column(sizes), ...); }
    template <class... T>
    void rows(const T&... sizes) { (row(sizes), ...); }
    void rowGap(float gap) { config_.rowGap = gap; }
    void columnGap(float gap) { config_.columnGap = gap; }
    void gap(float all) { config_.rowGap = config_.columnGap = all; }
    void gap(float row, float column) { config_.rowGap = row, config_.columnGap = column; }
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    void gap(A&&... args) {  // gap(row = 8.dp, column = 16.dp)
        using P = detail::Params<detail::names::Row, detail::names::Column>;
        static_assert(P::check<A...>());
        gap(float(P::arg<detail::names::Row>(args...)), float(P::arg<detail::names::Column>(args...)));
    }
    GridTrackSize minmax(float min, Fr max) const { return GridTrackSize::MinMax(min, max); }
    void area(std::string id, int row = 0, int column = 0, int rowSpan = 1, int columnSpan = 1) {
        config_.areas.push_back({std::move(id), row, column, rowSpan, columnSpan});
    }
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    void area(std::string id, A&&... args) {  // area("header", row = 1, columnSpan = 2)
        namespace n = detail::names;
        using P = detail::Params<n::Row, n::Column, n::RowSpan, n::ColumnSpan>;
        static_assert(P::check<A...>());
        area(std::move(id), P::get<n::Row>(0, args...), P::get<n::Column>(0, args...), P::get<n::RowSpan>(1, args...),
             P::get<n::ColumnSpan>(1, args...));
    }
    const GridConfig& gridConfig() const { return config_; }
    GridConfigurationScope(const GridConfigurationScope&) = delete;  // (flow refers into it)

private:
    GridConfig config_;
};
namespace detail {
void Grid(const Modifier& modifier, const GridConfig& config, const Content& content, CallSite site);
// The config by the constraints it is laid out with: fewer columns when narrow, say.
void Grid(const Modifier& modifier, std::function<GridConfig(const Constraints&)> config, const Content& content, CallSite site);
// Grid(config, modifier) { … } as Compose's (or Grid(modifier, config) { … } with a GridConfig):
// config a GridConfig, Compose's lambda on a GridConfigurationScope, or a function of the constraints.
struct GridImpl {
    template <class... A>
    static void call(CallSite site, A&&... args) {
        using P = std::conditional_t<startsWith<Modifier, A...>(), Params<names::Modifier, names::Config, Block>,
                                     Params<names::Config, names::Modifier, Block>>;
        static_assert(P::template check<A...>());
        static_assert(P::template given<names::Config, A...>(), "kompoz: Grid needs its config");
        Modifier modifier = P::template get<names::Modifier>(Modifier{}, args...);
        auto&& config = P::template arg<names::Config>(args...);
        using C = std::remove_cvref_t<decltype(config)>;
        if constexpr (std::is_convertible_v<const C&, const GridConfig&>) {
            Grid(modifier, config, P::block(std::forward<A>(args)...), site);
        } else if constexpr (std::is_invocable_v<C&, GridConfigurationScope&>) {
            Grid(modifier, std::function<GridConfig(const Constraints&)>([config](const Constraints& c) mutable {
                     GridConfigurationScope scope(c);
                     config(scope);
                     return scope.gridConfig();
                 }),
                 P::block(std::forward<A>(args)...), site);
        } else {
            Grid(modifier, std::function<GridConfig(const Constraints&)>(config), P::block(std::forward<A>(args)...), site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::GridImpl> Grid;

// A Box whose content knows the constraints it is laid out with (to pick a layout by width): a
// SubcomposeLayout, as Compose's, its content composed as it is measured, with the constraints it
// gets (the first time too), and again when they change.
// Compose's BoxWithConstraintsScope: what its content has as its receiver, the constraints and their
// dp (if (maxWidth > 400.dp) …). A content taking the Constraints takes it too.
struct BoxWithConstraintsScope {
    Constraints constraints;
    float minWidth = 0, maxWidth = 0, minHeight = 0, maxHeight = 0;
    operator const Constraints&() const { return constraints; }
};
namespace detail {
void BoxWithConstraints(const Modifier& modifier, const std::function<void(const Constraints&)>& content, Alignment contentAlignment,
                        CallSite site);
struct BoxWithConstraintsImpl {
    using P = Params<names::Modifier, names::ContentAlignment, BlockWith<BoxWithConstraintsScope&>>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: BoxWithConstraints(modifier) { … }: its content is needed");
        auto&& content = std::get<sizeof...(A) - 1>(std::forward_as_tuple(std::forward<A>(args)...));
        BoxWithConstraints(P::get<names::Modifier>(Modifier{}, args...),
                           [content](const Constraints& c) mutable {
                               BoxWithConstraintsScope scope{c, c.minWidth, c.maxWidth, c.minHeight, c.maxHeight};
                               content(scope);
                           },
                           P::get<names::ContentAlignment>(Alignment{Alignment::TopStart}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::BoxWithConstraintsImpl> BoxWithConstraints;

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
namespace detail {
void BackHandler(bool enabled, std::function<void()> onBack, CallSite site = CallSite::current());
struct BackHandlerImpl {  // BackHandler(enabled = …) { … }, as Compose's
    template <class... A>
    static void call(CallSite site, A&&... args) {
        using P = Params<names::Enabled, Block>;
        static_assert(P::check<A...>());
        static_assert(P::blockGiven<A...>(), "kompoz: BackHandler(enabled) { … } takes what back does");
        BackHandler(P::get<names::Enabled>(true, args...), std::function<void()>(std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...))), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::BackHandlerImpl> BackHandler;
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

// Texts inside can be selected and copied, one selection across them all: a finger's long press
// selects a word, with handles to drag (from text to text) and a toolbar (copy, select all);
// the mouse drags a selection or double clicks a word, and Ctrl+C copies it. A press elsewhere
// in it clears it.
namespace detail {
void SelectionContainer(const Modifier& modifier, const Content& content, CallSite site);
struct SelectionContainerImpl {
    using P = Params<names::Modifier, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        SelectionContainer(P::get<names::Modifier>(Modifier{}, args...), P::plainBlock(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::SelectionContainerImpl> SelectionContainer;

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
// A composable in a text (Compose's InlineTextContent): the room the text keeps for it, and what
// goes there (told the alternate text).
struct InlineTextContent {
    Placeholder placeholder;
    std::function<void(const std::string& alternateText)> children;
};
// Text's inlineContent: the contents by their ids.
using InlineContents = std::map<std::string, InlineTextContent>;

class TextLayoutResult;
namespace detail {
using OnTextLayout = std::function<void(const TextLayoutResult&)>;
using InlineContents = kompoz::InlineContents;
// What Text makes; the library's own composables call these.
void Text(std::string text, CallSite site = CallSite::current());
void Text(std::string text, const TextStyle& style, CallSite site = CallSite::current());
void Text(std::string text, const Modifier& modifier, const TextStyle& style = {}, CallSite site = CallSite::current());
void Text(std::string text, const Modifier& modifier, const TextStyle& style, OnTextLayout onTextLayout, CallSite site = CallSite::current());
void Text(const AnnotatedString& text, const Modifier& modifier = {}, const TextStyle& style = {}, CallSite site = CallSite::current());
void Text(const AnnotatedString& text, const Modifier& modifier, const TextStyle& style, OnTextLayout onTextLayout, CallSite site = CallSite::current());
void Text(const AnnotatedString& text, const Modifier& modifier, const TextStyle& style,
          const InlineContents& inlineContent, OnTextLayout onTextLayout = {}, CallSite site = CallSite::current());

// Text's style: `style`, with what was given by name over it (as Compose's Text merges them; an
// empty optional leaves the style's).
template <class P, class... A>
TextStyle textStyleOf(A&&... args) {
    // (A theme's text style, typography().bodyMedium, is a Font: given as the style's font.)
    TextStyle s = std::visit([](const auto& v) {
        if constexpr (std::is_same_v<std::decay_t<decltype(v)>, Font>) return TextStyle{.font = v};
        else return TextStyle(v);
    }, P::template get<names::Style>(std::variant<TextStyle, Font>{}, args...));
    auto over = [](auto& field, const auto& value) {
        if (value) field = value;
    };
    if constexpr (P::template given<names::Color, A...>()) over(s.color, P::template get<names::Color>(std::optional<Color>{}, args...));
    if constexpr (P::template given<names::FontSize, A...>()) over(s.fontSize, P::template get<names::FontSize>(std::optional<TextUnit>{}, args...));
    if constexpr (P::template given<names::FontStyle, A...>()) over(s.fontStyle, P::template get<names::FontStyle>(std::optional<FontStyle>{}, args...));
    if constexpr (P::template given<names::FontWeight, A...>()) over(s.fontWeight, P::template get<names::FontWeight>(std::optional<FontWeight>{}, args...));
    if constexpr (P::template given<names::FontFamily, A...>()) over(s.fontFamily, P::template get<names::FontFamily>(std::optional<FontFamily>{}, args...));
    if constexpr (P::template given<names::LetterSpacing, A...>()) over(s.letterSpacing, P::template get<names::LetterSpacing>(std::optional<TextUnit>{}, args...));
    if constexpr (P::template given<names::TextDecoration, A...>())
        s.textDecoration = P::template get<names::TextDecoration>(TextDecoration::None, args...);
    if constexpr (P::template given<names::TextAlign, A...>()) s.textAlign = P::template get<names::TextAlign>(TextAlign::Start, args...);
    if constexpr (P::template given<names::LineHeight, A...>()) over(s.lineHeight, P::template get<names::LineHeight>(std::optional<TextUnit>{}, args...));
    if constexpr (P::template given<names::Overflow, A...>()) s.overflow = P::template get<names::Overflow>(TextOverflow::Clip, args...);
    if constexpr (P::template given<names::SoftWrap, A...>()) s.softWrap = P::template get<names::SoftWrap>(true, args...);
    if constexpr (P::template given<names::MaxLines, A...>()) s.maxLines = P::template get<names::MaxLines>(0, args...);
    if constexpr (P::template given<names::MinLines, A...>()) s.minLines = P::template get<names::MinLines>(1, args...);
    if constexpr (P::template given<names::AutoSize, A...>()) over(s.autoSize, P::template get<names::AutoSize>(std::optional<TextAutoSize>{}, args...));
    return s;
}

struct TextImpl {
    using Plain = Params<names::Text, names::Modifier, names::Color, names::FontSize, names::FontStyle, names::FontWeight, names::FontFamily, names::LetterSpacing, names::TextDecoration, names::TextAlign, names::LineHeight, names::Overflow, names::SoftWrap, names::MaxLines, names::MinLines, names::OnTextLayout, names::Style, names::AutoSize>;
    using Styled = Params<names::Text, names::Modifier, names::Color, names::FontSize, names::FontStyle, names::FontWeight, names::FontFamily, names::LetterSpacing, names::TextDecoration, names::TextAlign, names::LineHeight, names::Overflow, names::SoftWrap, names::MaxLines, names::MinLines, names::InlineContent, names::OnTextLayout, names::Style, names::AutoSize>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        // A String's form, or an AnnotatedString's (styles, inline content).
        if constexpr (std::is_same_v<std::remove_cvref_t<decltype(Plain::arg<names::Text>(args...))>, AnnotatedString>) {
            static_assert(Styled::check<A...>());
            const AnnotatedString& value = Styled::arg<names::Text>(args...);
            detail::Text(value, Styled::get<names::Modifier>(Modifier{}, args...), textStyleOf<Styled>(args...), Styled::get<names::InlineContent>(InlineContents{}, args...), Styled::get<names::OnTextLayout>(OnTextLayout{}, args...), site);
        } else {
            static_assert(Plain::check<A...>());
            static_assert(std::is_convertible_v<decltype(Plain::arg<names::Text>(args...)), std::string>, "kompoz: Text's text is a string or an AnnotatedString");
            detail::Text(std::string(Plain::arg<names::Text>(args...)), Plain::get<names::Modifier>(Modifier{}, args...), textStyleOf<Plain>(args...), Plain::get<names::OnTextLayout>(OnTextLayout{}, args...), site);
        }
    }
};
} // namespace detail

// Text (Compose's Text): Text("Merhaba", modifier, color, fontSize, …, style). An AnnotatedString
// has several styles in it (see text.hpp; its links are clickable), and an inlineContent: each
// appendInlineContent(id) of the text gets inlineContent[id] in a room of its placeholder's size,
// on its line as it says.
//   Text(buildAnnotatedString([](auto& b) { b.append("Hava "); b.appendInlineContent("güneş", "[güneş]"); b.append(" açık"); }),
//        inlineContent = InlineContents{{"güneş", {{.width = 1.2f, .height = 1.2f}, [](auto&) { Icon(Icons::Filled::Star, ""); }}}});
inline constexpr Composable<detail::TextImpl> Text;

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
    std::optional<Color> disabledContainerColor, disabledContentColor;  // unset: the theme's (onSurface at 12%, 38%)
    std::optional<MutableInteractionSource> interactionSource;  // told of presses, hovers and focus
    std::optional<ButtonShapes> shapes;                         // set: the corners morph while pressed
    std::optional<PaddingValues> contentPadding;                // unset: ButtonDefaults' for its kind
};

// A button's colors (Compose's ButtonColors: ButtonDefaults.textButtonColors(contentColor = …)),
// those left unset its kind's.
struct ButtonColors {
    std::optional<Color> containerColor, contentColor, disabledContainerColor, disabledContentColor;
    friend bool operator==(const ButtonColors&, const ButtonColors&) = default;
};
namespace detail {
inline void applyColors(ButtonStyle& style, const ButtonColors& colors) {
    if (colors.containerColor) style.containerColor = colors.containerColor;
    if (colors.contentColor) style.contentColor = colors.contentColor;
    if (colors.disabledContainerColor) style.disabledContainerColor = colors.disabledContainerColor;
    if (colors.disabledContentColor) style.disabledContentColor = colors.disabledContentColor;
}
} // namespace detail

// Compose 1.4's ButtonDefaults sizes: an icon before the label takes ButtonWithIconContentPadding
// (TextButtonWithIconContentPadding for a TextButton), IconSize big and IconSpacing from it.
namespace ButtonDefaults {
using buttonColors = ButtonColors;
using textButtonColors = ButtonColors;
using outlinedButtonColors = ButtonColors;
using filledTonalButtonColors = ButtonColors;
using elevatedButtonColors = ButtonColors;
inline const PaddingValues ContentPadding{24, 8, 24, 8};
inline const PaddingValues ButtonWithIconContentPadding{16, 8, 24, 8};
inline const PaddingValues TextButtonContentPadding{12, 8, 12, 8};
inline const PaddingValues TextButtonWithIconContentPadding{12, 8, 16, 8};
inline constexpr float MinWidth = 58, MinHeight = 40, IconSize = 18, IconSpacing = 8;
} // namespace ButtonDefaults

namespace detail {
void Button(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void Button(std::function<void()> onClick, const Modifier& modifier, const Content& content,
            CallSite site = CallSite::current());
void Button(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style, const Content& content,
            CallSite site = CallSite::current());
struct ButtonImpl {
    using P = Params<names::OnClick, names::Modifier, names::Enabled, names::Shapes, names::ContainerColor, names::ContentColor, names::ContentPadding, names::InteractionSource, names::Colors, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: Button takes a block: its content");
        ButtonStyle style;
        P::set<names::Enabled>(style.enabled, args...);
        P::set<names::Shapes>(style.shapes, args...);
        P::set<names::ContainerColor>(style.containerColor, args...);
        P::set<names::ContentColor>(style.contentColor, args...);
        P::set<names::ContentPadding>(style.contentPadding, args...);
        P::set<names::InteractionSource>(style.interactionSource, args...);
        if constexpr (P::given<names::Colors, A...>()) detail::applyColors(style, P::get<names::Colors>(ButtonColors{}, args...));
        detail::Button(std::move(onClick), P::get<names::Modifier>(Modifier{}, args...), style, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::ButtonImpl> Button;

namespace detail {
void OutlinedButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void OutlinedButton(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style,
                    const Content& content, CallSite site = CallSite::current());
struct OutlinedButtonImpl {
    using P = Params<names::OnClick, names::Modifier, names::Enabled, names::Shapes, names::ContainerColor, names::ContentColor, names::ContentPadding, names::InteractionSource, names::Colors, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: OutlinedButton takes a block: its content");
        ButtonStyle style;
        P::set<names::Enabled>(style.enabled, args...);
        P::set<names::Shapes>(style.shapes, args...);
        P::set<names::ContainerColor>(style.containerColor, args...);
        P::set<names::ContentColor>(style.contentColor, args...);
        P::set<names::ContentPadding>(style.contentPadding, args...);
        P::set<names::InteractionSource>(style.interactionSource, args...);
        if constexpr (P::given<names::Colors, A...>()) detail::applyColors(style, P::get<names::Colors>(ButtonColors{}, args...));
        detail::OutlinedButton(std::move(onClick), P::get<names::Modifier>(Modifier{}, args...), style, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::OutlinedButtonImpl> OutlinedButton;

namespace detail {
void FilledTonalButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void FilledTonalButton(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style,
                       const Content& content, CallSite site = CallSite::current());
struct FilledTonalButtonImpl {
    using P = Params<names::OnClick, names::Modifier, names::Enabled, names::Shapes, names::ContainerColor, names::ContentColor, names::ContentPadding, names::InteractionSource, names::Colors, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: FilledTonalButton takes a block: its content");
        ButtonStyle style;
        P::set<names::Enabled>(style.enabled, args...);
        P::set<names::Shapes>(style.shapes, args...);
        P::set<names::ContainerColor>(style.containerColor, args...);
        P::set<names::ContentColor>(style.contentColor, args...);
        P::set<names::ContentPadding>(style.contentPadding, args...);
        P::set<names::InteractionSource>(style.interactionSource, args...);
        if constexpr (P::given<names::Colors, A...>()) detail::applyColors(style, P::get<names::Colors>(ButtonColors{}, args...));
        detail::FilledTonalButton(std::move(onClick), P::get<names::Modifier>(Modifier{}, args...), style, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::FilledTonalButtonImpl> FilledTonalButton;

namespace detail {
void ElevatedButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void ElevatedButton(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style,
                    const Content& content, CallSite site = CallSite::current());
struct ElevatedButtonImpl {
    using P = Params<names::OnClick, names::Modifier, names::Enabled, names::Shapes, names::ContainerColor, names::ContentColor, names::ContentPadding, names::InteractionSource, names::Colors, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: ElevatedButton takes a block: its content");
        ButtonStyle style;
        P::set<names::Enabled>(style.enabled, args...);
        P::set<names::Shapes>(style.shapes, args...);
        P::set<names::ContainerColor>(style.containerColor, args...);
        P::set<names::ContentColor>(style.contentColor, args...);
        P::set<names::ContentPadding>(style.contentPadding, args...);
        P::set<names::InteractionSource>(style.interactionSource, args...);
        if constexpr (P::given<names::Colors, A...>()) detail::applyColors(style, P::get<names::Colors>(ButtonColors{}, args...));
        detail::ElevatedButton(std::move(onClick), P::get<names::Modifier>(Modifier{}, args...), style, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::ElevatedButtonImpl> ElevatedButton;

namespace detail {
void TextButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void TextButton(std::function<void()> onClick, const Modifier& modifier, const ButtonStyle& style,
                const Content& content, CallSite site = CallSite::current());
struct TextButtonImpl {
    using P = Params<names::OnClick, names::Modifier, names::Enabled, names::Shapes, names::ContainerColor, names::ContentColor, names::ContentPadding, names::InteractionSource, names::Colors, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: TextButton takes a block: its content");
        ButtonStyle style;
        P::set<names::Enabled>(style.enabled, args...);
        P::set<names::Shapes>(style.shapes, args...);
        P::set<names::ContainerColor>(style.containerColor, args...);
        P::set<names::ContentColor>(style.contentColor, args...);
        P::set<names::ContentPadding>(style.contentPadding, args...);
        P::set<names::InteractionSource>(style.interactionSource, args...);
        if constexpr (P::given<names::Colors, A...>()) detail::applyColors(style, P::get<names::Colors>(ButtonColors{}, args...));
        detail::TextButton(std::move(onClick), P::get<names::Modifier>(Modifier{}, args...), style, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::TextButtonImpl> TextButton;

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

// Loads an image asset once ("images/photo.png"), like painterResource: JPEG, PNG, WebP, BMP
// (on the web PNG and BMP: the browser decodes the others asynchronously, use AsyncImage). The
// same path gives the same bitmap wherever it is asked for, while the host keeps it (as Android's
// resources): draw into a copy of it, not into it.
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
// vector::Group and vector::Path in `content` (given the viewport's size) make its tree; what it
// reads (state, animations) makes it again when it changes (the composable that remembers it
// recomposes); made the same, it stays the same vector (an equal painter, its bitmaps kept).
// Without a viewport, its default size in pixels (LocalDensity), as Compose's; its name
// "VectorRootGroup" unless given.
//   float drawn = animateFloatAsState(done.get() ? 1.f : 0.f, tween(600));
//   Painter check = rememberVectorPainter(48, 48, 24, 24, [&](float, float) {
//       vector::Path({.pathData = addPathNodes("M5,12l5,5l9,-10"), .stroke = Brush::solid(color), .strokeLineWidth = 2.5f,
//                     .trimPathEnd = drawn});
//   });
Painter rememberVectorPainter(float defaultWidth, float defaultHeight, const std::function<void(float viewportWidth, float viewportHeight)>& content,
                              CallSite site = CallSite::current());
Painter rememberVectorPainter(float defaultWidth, float defaultHeight, float viewportWidth, float viewportHeight,
                              const std::function<void(float viewportWidth, float viewportHeight)>& content, CallSite site = CallSite::current());
Painter rememberVectorPainter(float defaultWidth, float defaultHeight, float viewportWidth, float viewportHeight, std::string name,
                              std::optional<Color> tintColor, BlendMode tintBlendMode, bool autoMirror,
                              const std::function<void(float viewportWidth, float viewportHeight)>& content, CallSite site = CallSite::current());

// A vector's content, as Compose's androidx.compose.ui.graphics.vector composables make it.
namespace vector {
// A group in it (Compose's Group): `group`'s name, transform and clip (not its children), what
// `content` makes inside it.
void Group(const VectorGroup& group, const std::function<void()>& content, CallSite site = CallSite::current());
// A path in it (Compose's Path).
void Path(const VectorPath& path);

// What a VectorConfig can draw a group or a path with in place of its own value (Compose's
// VectorProperty): each of its own type.
template <class T>
struct VectorPropertyOf {
    int index;
    friend constexpr bool operator==(VectorPropertyOf, VectorPropertyOf) = default;
};
struct VectorProperty {
    static constexpr VectorPropertyOf<float> Rotation{0}, PivotX{1}, PivotY{2}, ScaleX{3}, ScaleY{4}, TranslateX{5}, TranslateY{6};
    static constexpr VectorPropertyOf<std::vector<PathNode>> PathData{7};  // a path's, or a group's clip path
    static constexpr VectorPropertyOf<std::optional<Brush>> Fill{8}, Stroke{10};
    static constexpr VectorPropertyOf<float> FillAlpha{9}, StrokeLineWidth{11}, StrokeAlpha{12}, TrimPathStart{13}, TrimPathEnd{14},
        TrimPathOffset{15};
};
// The values a group or a path is drawn with (Compose's VectorConfig): its own, but for those a
// config of your own gives (override getOrDefault; `using VectorConfig::getOrDefault;` keeps the others).
class VectorConfig {
public:
    virtual ~VectorConfig() = default;
    virtual float getOrDefault(VectorPropertyOf<float> property, float defaultValue) const;
    virtual std::vector<PathNode> getOrDefault(VectorPropertyOf<std::vector<PathNode>> property, std::vector<PathNode> defaultValue) const;
    virtual std::optional<Brush> getOrDefault(VectorPropertyOf<std::optional<Brush>> property, std::optional<Brush> defaultValue) const;
};
// `group`'s paths and groups as vector::Path and vector::Group (Compose's RenderVectorGroup),
// each drawn with the config of its name in `configs` (a path's cap, join, miter and fill type,
// a group's name, its own):
//   Image(rememberVectorPainter(24, 24, 24, 24, [&](float, float) { vector::RenderVectorGroup(icon.root(), {{"spin", spinning}}); }), "");
void RenderVectorGroup(const VectorGroup& group, const std::map<std::string, std::shared_ptr<const VectorConfig>>& configs = {},
                       CallSite site = CallSite::current());
} // namespace vector

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
namespace detail {
// A content description as given: a text, or none (Kotlin's null, kompozc's kt::null).
template <class T>
std::string descriptionOf(T&& text) {
    if constexpr (requires { std::remove_cvref_t<T>::kompozNull; }) return {};
    else return std::string(std::forward<T>(text));
}
} // namespace detail
namespace detail {
void Image(const ImageBitmap& bitmap, std::string contentDescription, const Modifier& modifier = {},
           ContentScale contentScale = ContentScale::Fit, float alpha = 1, std::optional<ColorFilter> colorFilter = {},
           CallSite site = CallSite::current());
void Image(const Painter& painter, std::string contentDescription, const Modifier& modifier = {},
           Alignment alignment = Alignment::Center, ContentScale contentScale = ContentScale::Fit, float alpha = 1,
           std::optional<ColorFilter> colorFilter = {}, CallSite site = CallSite::current());
struct ImageImpl {
    using OfBitmap = Params<names::Bitmap, names::ContentDescription, names::Modifier, names::ContentScale, names::Alpha, names::ColorFilter>;
    using OfPainter = Params<names::Painter, names::ContentDescription, names::Modifier, names::Alignment, names::ContentScale, names::Alpha, names::ColorFilter>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (OfBitmap::form<names::Bitmap, ImageBitmap, A...>()) {
            using P = OfBitmap;
            static_assert(P::check<A...>());
            const ImageBitmap& bitmap = P::arg<names::Bitmap>(args...);
            detail::Image(bitmap, descriptionOf(P::arg<names::ContentDescription>(args...)), P::get<names::Modifier>(Modifier{}, args...),
                          P::get<names::ContentScale>(ContentScale::Fit, args...), P::get<names::Alpha>(1.f, args...),
                          P::get<names::ColorFilter>(std::optional<ColorFilter>{}, args...), site);
        } else {
            using P = OfPainter;
            static_assert(P::check<A...>());
            const Painter& painter = P::arg<names::Painter>(args...);
            detail::Image(painter, descriptionOf(P::arg<names::ContentDescription>(args...)), P::get<names::Modifier>(Modifier{}, args...),
                          P::get<names::Alignment>(Alignment{Alignment::Center}, args...), P::get<names::ContentScale>(ContentScale::Fit, args...),
                          P::get<names::Alpha>(1.f, args...), P::get<names::ColorFilter>(std::optional<ColorFilter>{}, args...), site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::ImageImpl> Image;

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
namespace detail {
void AsyncImage(std::string model, std::string contentDescription, const Modifier& modifier, const AsyncImageOptions& options, CallSite site);
// AsyncImage(model, contentDescription, modifier, contentScale = …, alpha = …, colorFilter = …, onState = …,
// placeholder = { }, error = { }, crossfadeMillis = …) as Coil's, or with an AsyncImageOptions after the modifier.
struct AsyncImageImpl {
    using P = Params<names::Model, names::ContentDescription, names::Modifier, names::Options, names::ContentScale, names::Alpha,
                     names::ColorFilter, names::OnState, names::Placeholder, names::Error, names::CrossfadeMillis>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        AsyncImageOptions options = P::get<names::Options>(AsyncImageOptions{}, args...);
        P::set<names::ContentScale>(options.contentScale, args...);
        P::set<names::Alpha>(options.alpha, args...);
        P::set<names::ColorFilter>(options.colorFilter, args...);
        P::set<names::OnState>(options.onState, args...);
        P::set<names::Placeholder>(options.placeholder, args...);
        P::set<names::Error>(options.error, args...);
        P::set<names::CrossfadeMillis>(options.crossfadeMillis, args...);
        std::string description;
        if constexpr (P::given<names::ContentDescription, A...>()) description = descriptionOf(P::arg<names::ContentDescription>(args...));
        AsyncImage(std::string(P::arg<names::Model>(args...)), std::move(description), P::get<names::Modifier>(Modifier{}, args...), options, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::AsyncImageImpl> AsyncImage;

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
// A Painter as an icon: its own size (24 dp if it has none), tinted the same way (Compose's
// Icon(painter); a bitmap turns into its shape in the tint).
namespace detail {
void Icon(const ImageVector& icon, std::string contentDescription, const Modifier& modifier = {},
          std::optional<Color> tint = {}, CallSite site = CallSite::current());
void Icon(const Painter& painter, std::string contentDescription, const Modifier& modifier = {},
          std::optional<Color> tint = {}, CallSite site = CallSite::current());
struct IconImpl {
    using OfVector = Params<names::ImageVector, names::ContentDescription, names::Modifier, names::Tint>;
    using OfPainter = Params<names::Painter, names::ContentDescription, names::Modifier, names::Tint>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (OfPainter::form<names::Painter, Painter, A...>() && !OfVector::form<names::ImageVector, ImageVector, A...>()) {
            using P = OfPainter;
            static_assert(P::check<A...>());
            const Painter& painter = P::arg<names::Painter>(args...);
            detail::Icon(painter, descriptionOf(P::arg<names::ContentDescription>(args...)), P::get<names::Modifier>(Modifier{}, args...),
                         P::get<names::Tint>(std::optional<Color>{}, args...), site);
        } else {
            using P = OfVector;
            static_assert(P::check<A...>());
            const ImageVector& icon = P::arg<names::ImageVector>(args...);
            detail::Icon(icon, descriptionOf(P::arg<names::ContentDescription>(args...)), P::get<names::Modifier>(Modifier{}, args...),
                         P::get<names::Tint>(std::optional<Color>{}, args...), site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::IconImpl> Icon;

struct IconButtonStyle {
    bool enabled = true;
    std::optional<Color> containerColor;
    std::optional<Color> contentColor;
    std::optional<MutableInteractionSource> interactionSource;  // told of presses, hovers and focus
};

namespace detail {
void IconButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void IconButton(std::function<void()> onClick, const Modifier& modifier, const IconButtonStyle& style,
                const Content& content, CallSite site = CallSite::current());
struct IconButtonImpl {
    using P = Params<names::OnClick, names::Modifier, names::Enabled, names::ContainerColor, names::ContentColor, names::InteractionSource, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: IconButton takes a block: its content");
        IconButtonStyle style;
        P::set<names::Enabled>(style.enabled, args...);
        P::set<names::ContainerColor>(style.containerColor, args...);
        P::set<names::ContentColor>(style.contentColor, args...);
        P::set<names::InteractionSource>(style.interactionSource, args...);
        detail::IconButton(std::move(onClick), P::get<names::Modifier>(Modifier{}, args...), style, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::IconButtonImpl> IconButton;
namespace detail {
void FilledIconButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void FilledIconButton(std::function<void()> onClick, const Modifier& modifier, const IconButtonStyle& style,
                      const Content& content, CallSite site = CallSite::current());
struct FilledIconButtonImpl {
    using P = Params<names::OnClick, names::Modifier, names::Enabled, names::ContainerColor, names::ContentColor, names::InteractionSource, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: FilledIconButton takes a block: its content");
        IconButtonStyle style;
        P::set<names::Enabled>(style.enabled, args...);
        P::set<names::ContainerColor>(style.containerColor, args...);
        P::set<names::ContentColor>(style.contentColor, args...);
        P::set<names::InteractionSource>(style.interactionSource, args...);
        detail::FilledIconButton(std::move(onClick), P::get<names::Modifier>(Modifier{}, args...), style, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::FilledIconButtonImpl> FilledIconButton;
namespace detail {
void FilledTonalIconButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void FilledTonalIconButton(std::function<void()> onClick, const Modifier& modifier, const IconButtonStyle& style,
                           const Content& content, CallSite site = CallSite::current());
struct FilledTonalIconButtonImpl {
    using P = Params<names::OnClick, names::Modifier, names::Enabled, names::ContainerColor, names::ContentColor, names::InteractionSource, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: FilledTonalIconButton takes a block: its content");
        IconButtonStyle style;
        P::set<names::Enabled>(style.enabled, args...);
        P::set<names::ContainerColor>(style.containerColor, args...);
        P::set<names::ContentColor>(style.contentColor, args...);
        P::set<names::InteractionSource>(style.interactionSource, args...);
        detail::FilledTonalIconButton(std::move(onClick), P::get<names::Modifier>(Modifier{}, args...), style, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::FilledTonalIconButtonImpl> FilledTonalIconButton;
// A 1dp outline around a transparent one (`containerColor` fills it; the outline stays).
namespace detail {
void OutlinedIconButton(std::function<void()> onClick, const Content& content, CallSite site = CallSite::current());
void OutlinedIconButton(std::function<void()> onClick, const Modifier& modifier, const IconButtonStyle& style,
                        const Content& content, CallSite site = CallSite::current());
struct OutlinedIconButtonImpl {
    using P = Params<names::OnClick, names::Modifier, names::Enabled, names::ContainerColor, names::ContentColor, names::InteractionSource, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: OutlinedIconButton takes a block: its content");
        IconButtonStyle style;
        P::set<names::Enabled>(style.enabled, args...);
        P::set<names::ContainerColor>(style.containerColor, args...);
        P::set<names::ContentColor>(style.contentColor, args...);
        P::set<names::InteractionSource>(style.interactionSource, args...);
        detail::OutlinedIconButton(std::move(onClick), P::get<names::Modifier>(Modifier{}, args...), style, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::OutlinedIconButtonImpl> OutlinedIconButton;

// Icon buttons that are on or off (Compose's IconToggleButton family): a tap calls
// onCheckedChange(!checked), and `checked` picks the colors — a star that turns primary, a
// filled one that goes from a gray container to a primary one, an outlined one that loses its
// outline for a dark (inverse surface) container. Read as a checkbox by accessibility services.
//
//   IconToggleButton(liked.get(), [liked](bool on) { liked.set(on); }, [=] {
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
namespace detail {
void IconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content,
                      CallSite site = CallSite::current());
void IconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                      const IconToggleButtonStyle& style, const Content& content, CallSite site = CallSite::current());
struct IconToggleButtonImpl {
    using P = Params<names::Checked, names::OnCheckedChange, names::Modifier, names::Enabled, names::ContainerColor, names::ContentColor, names::CheckedContainerColor, names::CheckedContentColor, names::InteractionSource, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        bool checked = P::arg<names::Checked>(args...);
        std::function<void(bool)> onCheckedChange = P::arg<names::OnCheckedChange>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: IconToggleButton takes a block: its content");
        IconToggleButtonStyle style;
        P::set<names::Enabled>(style.enabled, args...);
        P::set<names::ContainerColor>(style.containerColor, args...);
        P::set<names::ContentColor>(style.contentColor, args...);
        P::set<names::CheckedContainerColor>(style.checkedContainerColor, args...);
        P::set<names::CheckedContentColor>(style.checkedContentColor, args...);
        P::set<names::InteractionSource>(style.interactionSource, args...);
        detail::IconToggleButton(checked, std::move(onCheckedChange), P::get<names::Modifier>(Modifier{}, args...), style, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::IconToggleButtonImpl> IconToggleButton;
namespace detail {
void FilledIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content,
                            CallSite site = CallSite::current());
void FilledIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                            const IconToggleButtonStyle& style, const Content& content, CallSite site = CallSite::current());
struct FilledIconToggleButtonImpl {
    using P = Params<names::Checked, names::OnCheckedChange, names::Modifier, names::Enabled, names::ContainerColor, names::ContentColor, names::CheckedContainerColor, names::CheckedContentColor, names::InteractionSource, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        bool checked = P::arg<names::Checked>(args...);
        std::function<void(bool)> onCheckedChange = P::arg<names::OnCheckedChange>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: FilledIconToggleButton takes a block: its content");
        IconToggleButtonStyle style;
        P::set<names::Enabled>(style.enabled, args...);
        P::set<names::ContainerColor>(style.containerColor, args...);
        P::set<names::ContentColor>(style.contentColor, args...);
        P::set<names::CheckedContainerColor>(style.checkedContainerColor, args...);
        P::set<names::CheckedContentColor>(style.checkedContentColor, args...);
        P::set<names::InteractionSource>(style.interactionSource, args...);
        detail::FilledIconToggleButton(checked, std::move(onCheckedChange), P::get<names::Modifier>(Modifier{}, args...), style, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::FilledIconToggleButtonImpl> FilledIconToggleButton;
namespace detail {
void FilledTonalIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content,
                                 CallSite site = CallSite::current());
void FilledTonalIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                                 const IconToggleButtonStyle& style, const Content& content,
                                 CallSite site = CallSite::current());
struct FilledTonalIconToggleButtonImpl {
    using P = Params<names::Checked, names::OnCheckedChange, names::Modifier, names::Enabled, names::ContainerColor, names::ContentColor, names::CheckedContainerColor, names::CheckedContentColor, names::InteractionSource, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        bool checked = P::arg<names::Checked>(args...);
        std::function<void(bool)> onCheckedChange = P::arg<names::OnCheckedChange>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: FilledTonalIconToggleButton takes a block: its content");
        IconToggleButtonStyle style;
        P::set<names::Enabled>(style.enabled, args...);
        P::set<names::ContainerColor>(style.containerColor, args...);
        P::set<names::ContentColor>(style.contentColor, args...);
        P::set<names::CheckedContainerColor>(style.checkedContainerColor, args...);
        P::set<names::CheckedContentColor>(style.checkedContentColor, args...);
        P::set<names::InteractionSource>(style.interactionSource, args...);
        detail::FilledTonalIconToggleButton(checked, std::move(onCheckedChange), P::get<names::Modifier>(Modifier{}, args...), style, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::FilledTonalIconToggleButtonImpl> FilledTonalIconToggleButton;
namespace detail {
void OutlinedIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Content& content,
                              CallSite site = CallSite::current());
void OutlinedIconToggleButton(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier,
                              const IconToggleButtonStyle& style, const Content& content,
                              CallSite site = CallSite::current());
struct OutlinedIconToggleButtonImpl {
    using P = Params<names::Checked, names::OnCheckedChange, names::Modifier, names::Enabled, names::ContainerColor, names::ContentColor, names::CheckedContainerColor, names::CheckedContentColor, names::InteractionSource, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        bool checked = P::arg<names::Checked>(args...);
        std::function<void(bool)> onCheckedChange = P::arg<names::OnCheckedChange>(args...);
        static_assert(P::blockGiven<A...>(), "kompoz: OutlinedIconToggleButton takes a block: its content");
        IconToggleButtonStyle style;
        P::set<names::Enabled>(style.enabled, args...);
        P::set<names::ContainerColor>(style.containerColor, args...);
        P::set<names::ContentColor>(style.contentColor, args...);
        P::set<names::CheckedContainerColor>(style.checkedContainerColor, args...);
        P::set<names::CheckedContentColor>(style.checkedContentColor, args...);
        P::set<names::InteractionSource>(style.interactionSource, args...);
        detail::OutlinedIconToggleButton(checked, std::move(onCheckedChange), P::get<names::Modifier>(Modifier{}, args...), style, P::block(std::forward<A>(args)...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::OutlinedIconToggleButtonImpl> OutlinedIconToggleButton;

namespace detail {
void CircularProgressIndicator(const Modifier& modifier, std::optional<Color> color, float strokeWidth, CallSite site);
void CircularProgressIndicator(float progress, const Modifier& modifier, std::optional<Color> color, float strokeWidth, CallSite site);
void LinearProgressIndicator(const Modifier& modifier, std::optional<Color> color, CallSite site);
void LinearProgressIndicator(float progress, const Modifier& modifier, std::optional<Color> color, CallSite site);
// Indeterminate (spinning) and determinate (0..1) progress, Material 3 style: (modifier, color,
// strokeWidth) spinning, or (progress, modifier, color, strokeWidth) with Compose's `progress`
// a value or a lambda read in composition ({ progress }).
template <bool Circular>
struct ProgressIndicatorImpl {
    using P = Params<names::Progress, names::Modifier, names::Color, names::StrokeWidth>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        constexpr bool spinning = !P::given<names::Progress, A...>() || startsWith<Modifier, A...>();
        if constexpr (spinning) {
            using S = Params<names::Modifier, names::Color, names::StrokeWidth>;
            static_assert(S::check<A...>());
            Modifier modifier = S::get<names::Modifier>(Modifier{}, args...);
            std::optional<Color> color = S::get<names::Color>(std::optional<Color>(), args...);
            if constexpr (Circular) CircularProgressIndicator(modifier, color, S::get<names::StrokeWidth>(4.f, args...), site);
            else LinearProgressIndicator(modifier, color, site);
        } else {
            static_assert(P::check<A...>());
            auto&& given = P::arg<names::Progress>(args...);
            float progress;
            if constexpr (std::is_invocable_v<decltype(given)&>) progress = float(given());
            else progress = float(given);
            Modifier modifier = P::get<names::Modifier>(Modifier{}, args...);
            std::optional<Color> color = P::get<names::Color>(std::optional<Color>(), args...);
            if constexpr (Circular) CircularProgressIndicator(progress, modifier, color, P::get<names::StrokeWidth>(4.f, args...), site);
            else LinearProgressIndicator(progress, modifier, color, site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::ProgressIndicatorImpl<true>> CircularProgressIndicator;
inline constexpr Composable<detail::ProgressIndicatorImpl<false>> LinearProgressIndicator;

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

// A card's colors (Compose's CardColors, CardDefaults.cardColors(containerColor = …)): those
// left unset are its kind's, from the theme; its content's color goes with its container's
// (contentColorFor), its disabled ones are those at 38%.
struct CardColors {
    std::optional<Color> containerColor, contentColor, disabledContainerColor, disabledContentColor;
    friend bool operator==(const CardColors&, const CardColors&) = default;
};
namespace CardDefaults {
using cardColors = CardColors;
using elevatedCardColors = CardColors;
using outlinedCardColors = CardColors;
} // namespace CardDefaults
// What a card's shape and colors are, and whether a clickable one can be clicked.
struct CardStyle {
    std::optional<Shape> shape;  // unset: 12 dp corners
    CardColors colors;
    bool enabled = true;
};

// Clickable cards (Compose's Card(onClick = ...)): the ripple keeps to their shape.
namespace detail {
void Card(const Modifier& modifier, const CardStyle& style, const Content& content, CallSite site = CallSite::current());
void Card(std::function<void()> onClick, const Modifier& modifier, const CardStyle& style, const Content& content, CallSite site = CallSite::current());
struct CardImpl {
    using P = Params<names::Modifier, names::Shape, names::Colors, Block>;
    using Clickable = Params<names::OnClick, names::Modifier, names::Enabled, names::Shape, names::Colors, Block>;
    template <class Q, class... A>
    static CardStyle style(A&... args) {
        CardStyle style;
        Q::template set<names::Shape>(style.shape, args...);
        Q::template set<names::Colors>(style.colors, args...);
        if constexpr (oneOfParams<names::Enabled, Q>) Q::template set<names::Enabled>(style.enabled, args...);
        return style;
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (countOf<names::OnClick, A...> > 0 || startsWithClick<A...>()) {  // a clickable one
            static_assert(Clickable::check<A...>());
            detail::Card(std::function<void()>(Clickable::arg<names::OnClick>(args...)), Clickable::get<names::Modifier>(Modifier{}, args...),
                         style<Clickable>(args...), Clickable::block(std::forward<A>(args)...), site);
        } else {
            static_assert(P::check<A...>());
            detail::Card(P::get<names::Modifier>(Modifier{}, args...), style<P>(args...), P::block(std::forward<A>(args)...), site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::CardImpl> Card;
namespace detail {
void ElevatedCard(const Modifier& modifier, const CardStyle& style, const Content& content, CallSite site = CallSite::current());
void ElevatedCard(std::function<void()> onClick, const Modifier& modifier, const CardStyle& style, const Content& content, CallSite site = CallSite::current());
struct ElevatedCardImpl {
    using P = Params<names::Modifier, names::Shape, names::Colors, Block>;
    using Clickable = Params<names::OnClick, names::Modifier, names::Enabled, names::Shape, names::Colors, Block>;
    template <class Q, class... A>
    static CardStyle style(A&... args) {
        CardStyle style;
        Q::template set<names::Shape>(style.shape, args...);
        Q::template set<names::Colors>(style.colors, args...);
        if constexpr (oneOfParams<names::Enabled, Q>) Q::template set<names::Enabled>(style.enabled, args...);
        return style;
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (countOf<names::OnClick, A...> > 0 || startsWithClick<A...>()) {  // a clickable one
            static_assert(Clickable::check<A...>());
            detail::ElevatedCard(std::function<void()>(Clickable::arg<names::OnClick>(args...)), Clickable::get<names::Modifier>(Modifier{}, args...),
                         style<Clickable>(args...), Clickable::block(std::forward<A>(args)...), site);
        } else {
            static_assert(P::check<A...>());
            detail::ElevatedCard(P::get<names::Modifier>(Modifier{}, args...), style<P>(args...), P::block(std::forward<A>(args)...), site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::ElevatedCardImpl> ElevatedCard;
namespace detail {
void OutlinedCard(const Modifier& modifier, const CardStyle& style, const Content& content, CallSite site = CallSite::current());
void OutlinedCard(std::function<void()> onClick, const Modifier& modifier, const CardStyle& style, const Content& content, CallSite site = CallSite::current());
struct OutlinedCardImpl {
    using P = Params<names::Modifier, names::Shape, names::Colors, Block>;
    using Clickable = Params<names::OnClick, names::Modifier, names::Enabled, names::Shape, names::Colors, Block>;
    template <class Q, class... A>
    static CardStyle style(A&... args) {
        CardStyle style;
        Q::template set<names::Shape>(style.shape, args...);
        Q::template set<names::Colors>(style.colors, args...);
        if constexpr (oneOfParams<names::Enabled, Q>) Q::template set<names::Enabled>(style.enabled, args...);
        return style;
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {
        if constexpr (countOf<names::OnClick, A...> > 0 || startsWithClick<A...>()) {  // a clickable one
            static_assert(Clickable::check<A...>());
            detail::OutlinedCard(std::function<void()>(Clickable::arg<names::OnClick>(args...)), Clickable::get<names::Modifier>(Modifier{}, args...),
                         style<Clickable>(args...), Clickable::block(std::forward<A>(args)...), site);
        } else {
            static_assert(P::check<A...>());
            detail::OutlinedCard(P::get<names::Modifier>(Modifier{}, args...), style<P>(args...), P::block(std::forward<A>(args)...), site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::OutlinedCardImpl> OutlinedCard;

namespace detail {
void Surface(const Modifier& modifier, const Content& content, CallSite site = CallSite::current());
void Surface(const Modifier& modifier, Color color, Color contentColor, const Content& content,
             CallSite site = CallSite::current());
// With a shape: its color in that shape, the content clipped to it (Compose's Surface(shape = …)).
void Surface(const Modifier& modifier, Color color, Color contentColor, const Shape& shape, const Content& content,
             CallSite site = CallSite::current());
struct SurfaceImpl {
    using P = Params<names::Modifier, names::Shape, names::Color, names::ContentColor, Block>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        Modifier modifier = P::get<names::Modifier>(Modifier{}, args...);
        if constexpr (!P::given<names::Color, A...>() && !P::given<names::ContentColor, A...>() && !P::given<names::Shape, A...>()) {
            detail::Surface(modifier, P::block(std::forward<A>(args)...), site);
        } else {
            Color color = P::get<names::Color>(colorScheme().surface, args...);
            Color content = P::get<names::ContentColor>(contentColorFor(color), args...);
            if constexpr (P::given<names::Shape, A...>())
                detail::Surface(modifier, color, content, P::get<names::Shape>(Shape(RectangleShape), args...), P::block(std::forward<A>(args)...), site);
            else detail::Surface(modifier, color, content, P::block(std::forward<A>(args)...), site);
        }
    }
};
} // namespace detail
inline constexpr Composable<detail::SurfaceImpl> Surface;

namespace detail {
void HorizontalDivider(const Modifier& modifier, float thickness, std::optional<Color> color, CallSite site);
void VerticalDivider(const Modifier& modifier, float thickness, std::optional<Color> color, CallSite site);
// HorizontalDivider(modifier, thickness = 1.dp, color = …) and VerticalDivider's, as Compose's
// (unset color: the theme's outlineVariant).
template <bool Horizontal>
struct DividerImpl {
    using P = Params<names::Modifier, names::Thickness, names::Color>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::template check<A...>());
        (Horizontal ? HorizontalDivider : VerticalDivider)(P::template get<names::Modifier>(Modifier{}, args...),
                                                         P::template get<names::Thickness>(1.f, args...),
                                                         P::template get<names::Color>(std::optional<Color>(), args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::DividerImpl<true>> HorizontalDivider;
inline constexpr Composable<detail::DividerImpl<false>> VerticalDivider;

namespace detail {
void Checkbox(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier = {},
              bool enabled = true, std::optional<MutableInteractionSource> interactionSource = {}, CallSite site = CallSite::current());
struct CheckboxImpl {
    using P = Params<names::Checked, names::OnCheckedChange, names::Modifier, names::Enabled, names::InteractionSource>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        bool checked = P::arg<names::Checked>(args...);
        std::function<void(bool)> onCheckedChange = P::arg<names::OnCheckedChange>(args...);
        detail::Checkbox(checked, std::move(onCheckedChange), P::get<names::Modifier>(Modifier{}, args...), P::get<names::Enabled>(true, args...),
            P::get<names::InteractionSource>(std::optional<MutableInteractionSource>{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::CheckboxImpl> Checkbox;
// A checkbox that can also be partly checked (Compose's TriStateCheckbox): a parent of
// checkboxes some of which are checked shows Indeterminate (a dash).
namespace detail {
void TriStateCheckbox(ToggleableState state, std::function<void()> onClick, const Modifier& modifier = {}, bool enabled = true,
                      std::optional<MutableInteractionSource> interactionSource = {}, CallSite site = CallSite::current());
struct TriStateCheckboxImpl {
    using P = Params<names::State, names::OnClick, names::Modifier, names::Enabled, names::InteractionSource>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        ToggleableState state = P::arg<names::State>(args...);
        std::function<void()> onClick = P::arg<names::OnClick>(args...);
        detail::TriStateCheckbox(state, std::move(onClick), P::get<names::Modifier>(Modifier{}, args...), P::get<names::Enabled>(true, args...),
            P::get<names::InteractionSource>(std::optional<MutableInteractionSource>{}, args...), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::TriStateCheckboxImpl> TriStateCheckbox;

// thumbContent: an icon in the thumb (16 dp; onPrimaryContainer when on, surfaceContainerHighest
// when off), which keeps the thumb at its large size. The thumb grows to 28 dp while held.
struct SwitchOptions {
    bool enabled = true;
    std::optional<MutableInteractionSource> interactionSource;
    Content thumbContent;
};
namespace detail {
void Switch(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier = {},
            bool enabled = true, std::optional<MutableInteractionSource> interactionSource = {}, CallSite site = CallSite::current());
void Switch(bool checked, std::function<void(bool)> onCheckedChange, const Modifier& modifier, const SwitchOptions& options,
            CallSite site = CallSite::current());
struct SwitchImpl {
    using P = Params<names::Checked, names::OnCheckedChange, names::Modifier, names::ThumbContent, names::Enabled, names::InteractionSource>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        bool checked = P::arg<names::Checked>(args...);
        std::function<void(bool)> onCheckedChange = P::arg<names::OnCheckedChange>(args...);
        SwitchOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::ThumbContent>(options.thumbContent, args...);
        detail::Switch(checked, std::move(onCheckedChange), P::get<names::Modifier>(Modifier{}, args...), options, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::SwitchImpl> Switch;

// What the keyboard's action key does; unset: Next/Previous move the focus, Done hides the
// keyboard (or runs the field's onSubmit).
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
//   TextField(state, Modifier(), inputTransformation = InputTransformation::maxLength(11));
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
//   TextField(phone, Modifier(), outputTransformation = OutputTransformation{[](TextFieldBuffer& b) {
//       for (int at : {8, 6, 3}) if (b.length() > at) b.insert(at, " ");  // 555 123 45 67
//   }});
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
//   TextField(v, set, textFieldColors = TextFieldColors{.focusedContainerColor = cs.primaryContainer, .cursorColor = cs.tertiary});
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
// Compose's TextFieldDefaults.colors(focusedContainerColor = …, …): a TextFieldColors, those left
// out the theme's (OutlinedTextFieldDefaults.colors for an outlined field).
namespace TextFieldDefaults {
using colors = TextFieldColors;
} // namespace TextFieldDefaults
namespace OutlinedTextFieldDefaults {
using colors = TextFieldColors;
} // namespace OutlinedTextFieldDefaults

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
namespace detail {
// A text field's label, placeholder or supporting text given by name: Compose's composable
// (anything callable) into `content`, else a string into `text`.
template <class Tag, class... A>
void textOrContent(std::string& text, Content& content, A&&... args) {
    (
        [&] {
            if constexpr (std::is_same_v<TagOf<A>, Tag>) {
                if constexpr (std::is_invocable_v<decltype((args.value))&>) content = Content(args.value);
                else text = std::string(args.value);
            }
        }(),
        ...);
}
} // namespace detail

struct TextFieldLineLimits {
    bool singleLine = false;
    int minHeightInLines = 1, maxHeightInLines = std::numeric_limits<int>::max();
    static const TextFieldLineLimits SingleLine, Default;  // Default: MultiLine()
    static TextFieldLineLimits MultiLine(int minHeightInLines = 1, int maxHeightInLines = std::numeric_limits<int>::max()) {
        return {false, minHeightInLines, maxHeightInLines};
    }
    // Compose's MultiLine(maxHeightInLines = 4).
    template <class... A>
        requires(detail::anyNamedArgument<A...>)
    static TextFieldLineLimits MultiLine(A&&... args) {
        namespace n = detail::names;
        using P = detail::Params<n::MinHeightInLines, n::MaxHeightInLines>;
        static_assert(P::check<A...>());
        return MultiLine(P::get<n::MinHeightInLines>(1, args...), P::get<n::MaxHeightInLines>(std::numeric_limits<int>::max(), args...));
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
    // Compose's composable forms of the label, the placeholder and the supporting text (label = {
    // Text("…") }): composed in their places, in the label's animated style, the placeholder's color
    // and the supporting text's; set in place of the strings.
    Content labelContent, placeholderContent, supportingTextContent;
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
    //   BasicTextField(query, Modifier(), decorator = [&](const Content& innerTextField) {
    //       Row(Modifier().border(1, cs.outline, CircleShape).padding(12, 8), verticalAlignment = Alignment::CenterVertically, [=] {
    //           Icon(Icons::Filled::Search, ""); Box(Modifier().weight(1).padding(8, 0, 0, 0), innerTextField);
    //       });
    //   });
    std::function<void(const Content& innerTextField)> decorator;
};

// Text fields with hoisted state, like Compose's TextField(value, onValueChange): filled
// (TextField) and outlined. Tap to place the caret; with a hardware keyboard the arrows,
// Home/End, Delete and Ctrl+A/C/X/V work too.
// Editable text without decoration (Compose's BasicTextField): no container, padding or label;
// the placeholder shows while it is empty. For building other fields (a search bar's).

// The text of a field kept in a state object instead of hoisted (Compose's TextFieldState):
//   auto notes = rememberTextFieldState();
//   OutlinedTextField(notes, Modifier().fillMaxWidth(), label = "Not", maxLines = 5);
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
//   TextButton([notes] { notes.undoState().undo(); }, Modifier(), enabled = notes.undoState().canUndo(), ...);
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
namespace detail {
void TextField(const std::string& value, std::function<void(std::string)> onValueChange,
               const Modifier& modifier = {}, TextFieldOptions options = {}, CallSite site = CallSite::current());
void TextField(const TextFieldState& state, const Modifier& modifier = {}, TextFieldOptions options = {},
               CallSite site = CallSite::current());
struct TextFieldImpl {
    template <class... A>
    static void valueForm(CallSite site, A&&... args) {
        using P = Params<names::Value, names::OnValueChange, names::Modifier, names::Enabled, names::ReadOnly, names::TextStyle, names::Label, names::Placeholder, names::LeadingIcon, names::TrailingIcon, names::Prefix, names::Suffix, names::SupportingText, names::IsError, names::VisualTransformation, names::KeyboardOptions, names::KeyboardActions, names::SingleLine, names::MaxLines, names::MinLines, names::InteractionSource, names::Shape, names::TextFieldColors, names::Colors, names::LineLimits, names::OnSubmit, names::InputTransformation, names::OutputTransformation, names::LabelPosition>;
        static_assert(P::check<A...>());
        const std::string& value = P::arg<names::Value>(args...);
        std::function<void(std::string)> onValueChange = P::arg<names::OnValueChange>(args...);
        TextFieldOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::ReadOnly>(options.readOnly, args...);
        P::set<names::TextStyle>(options.textStyle, args...);
        detail::textOrContent<names::Label>(options.label, options.labelContent, args...);
        detail::textOrContent<names::Placeholder>(options.placeholder, options.placeholderContent, args...);
        P::set<names::LeadingIcon>(options.leadingIcon, args...);
        P::set<names::TrailingIcon>(options.trailingIcon, args...);
        P::set<names::Prefix>(options.prefix, args...);
        P::set<names::Suffix>(options.suffix, args...);
        detail::textOrContent<names::SupportingText>(options.supportingText, options.supportingTextContent, args...);
        P::set<names::IsError>(options.isError, args...);
        P::set<names::VisualTransformation>(options.visualTransformation, args...);
        P::set<names::KeyboardOptions>(options.keyboardOptions, args...);
        P::set<names::KeyboardActions>(options.keyboardActions, args...);
        P::set<names::SingleLine>(options.singleLine, args...);
        P::set<names::MaxLines>(options.maxLines, args...);
        P::set<names::MinLines>(options.minLines, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::Shape>(options.shape, args...);
        P::set<names::TextFieldColors>(options.colors, args...);
        P::set<names::Colors>(options.colors, args...);
        P::set<names::LineLimits>(options.lineLimits, args...);
        P::set<names::OnSubmit>(options.onSubmit, args...);
        P::set<names::InputTransformation>(options.inputTransformation, args...);
        P::set<names::OutputTransformation>(options.outputTransformation, args...);
        P::set<names::LabelPosition>(options.labelPosition, args...);
        TextField(value, std::move(onValueChange), P::get<names::Modifier>(Modifier{}, args...), std::move(options), site);
    }
    template <class... A>
    static void stateForm(CallSite site, A&&... args) {
        using P = Params<names::State, names::Modifier, names::Enabled, names::ReadOnly, names::InputTransformation, names::TextStyle, names::LabelPosition, names::Label, names::Placeholder, names::LeadingIcon, names::TrailingIcon, names::Prefix, names::Suffix, names::SupportingText, names::IsError, names::KeyboardOptions, names::KeyboardActions, names::LineLimits, names::Shape, names::TextFieldColors, names::Colors, names::InteractionSource, names::OutputTransformation, names::SingleLine, names::MaxLines, names::MinLines, names::VisualTransformation, names::OnSubmit>;
        static_assert(P::check<A...>());
        const TextFieldState& state = P::arg<names::State>(args...);
        TextFieldOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::ReadOnly>(options.readOnly, args...);
        P::set<names::InputTransformation>(options.inputTransformation, args...);
        P::set<names::TextStyle>(options.textStyle, args...);
        P::set<names::LabelPosition>(options.labelPosition, args...);
        detail::textOrContent<names::Label>(options.label, options.labelContent, args...);
        detail::textOrContent<names::Placeholder>(options.placeholder, options.placeholderContent, args...);
        P::set<names::LeadingIcon>(options.leadingIcon, args...);
        P::set<names::TrailingIcon>(options.trailingIcon, args...);
        P::set<names::Prefix>(options.prefix, args...);
        P::set<names::Suffix>(options.suffix, args...);
        detail::textOrContent<names::SupportingText>(options.supportingText, options.supportingTextContent, args...);
        P::set<names::IsError>(options.isError, args...);
        P::set<names::KeyboardOptions>(options.keyboardOptions, args...);
        P::set<names::KeyboardActions>(options.keyboardActions, args...);
        P::set<names::LineLimits>(options.lineLimits, args...);
        P::set<names::Shape>(options.shape, args...);
        P::set<names::TextFieldColors>(options.colors, args...);
        P::set<names::Colors>(options.colors, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::OutputTransformation>(options.outputTransformation, args...);
        P::set<names::SingleLine>(options.singleLine, args...);
        P::set<names::MaxLines>(options.maxLines, args...);
        P::set<names::MinLines>(options.minLines, args...);
        P::set<names::VisualTransformation>(options.visualTransformation, args...);
        P::set<names::OnSubmit>(options.onSubmit, args...);
        TextField(state, P::get<names::Modifier>(Modifier{}, args...), std::move(options), site);
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {  // the value's form, or the state's
        if constexpr (countOf<names::State, A...> > 0 || startsWith<TextFieldState, A...>()) stateForm(site, std::forward<A>(args)...);
        else valueForm(site, std::forward<A>(args)...);
    }
};
} // namespace detail
inline constexpr Composable<detail::TextFieldImpl> TextField;
namespace detail {
void BasicTextField(const std::string& value, std::function<void(std::string)> onValueChange,
                    const Modifier& modifier = {}, TextFieldOptions options = {}, CallSite site = CallSite::current());
void BasicTextField(const TextFieldState& state, const Modifier& modifier = {}, TextFieldOptions options = {},
                    CallSite site = CallSite::current());
struct BasicTextFieldImpl {
    template <class... A>
    static void valueForm(CallSite site, A&&... args) {
        using P = Params<names::Value, names::OnValueChange, names::Modifier, names::Enabled, names::ReadOnly, names::TextStyle, names::KeyboardOptions, names::KeyboardActions, names::SingleLine, names::MaxLines, names::MinLines, names::VisualTransformation, names::InteractionSource, names::Decorator, names::Placeholder, names::OnSubmit, names::LineLimits, names::InputTransformation, names::OutputTransformation, names::TextFieldColors, names::Colors>;
        static_assert(P::check<A...>());
        const std::string& value = P::arg<names::Value>(args...);
        std::function<void(std::string)> onValueChange = P::arg<names::OnValueChange>(args...);
        TextFieldOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::ReadOnly>(options.readOnly, args...);
        P::set<names::TextStyle>(options.textStyle, args...);
        P::set<names::KeyboardOptions>(options.keyboardOptions, args...);
        P::set<names::KeyboardActions>(options.keyboardActions, args...);
        P::set<names::SingleLine>(options.singleLine, args...);
        P::set<names::MaxLines>(options.maxLines, args...);
        P::set<names::MinLines>(options.minLines, args...);
        P::set<names::VisualTransformation>(options.visualTransformation, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::Decorator>(options.decorator, args...);
        detail::textOrContent<names::Placeholder>(options.placeholder, options.placeholderContent, args...);
        P::set<names::OnSubmit>(options.onSubmit, args...);
        P::set<names::LineLimits>(options.lineLimits, args...);
        P::set<names::InputTransformation>(options.inputTransformation, args...);
        P::set<names::OutputTransformation>(options.outputTransformation, args...);
        P::set<names::TextFieldColors>(options.colors, args...);
        P::set<names::Colors>(options.colors, args...);
        BasicTextField(value, std::move(onValueChange), P::get<names::Modifier>(Modifier{}, args...), std::move(options), site);
    }
    template <class... A>
    static void stateForm(CallSite site, A&&... args) {
        using P = Params<names::State, names::Modifier, names::Enabled, names::ReadOnly, names::InputTransformation, names::TextStyle, names::KeyboardOptions, names::KeyboardActions, names::LineLimits, names::InteractionSource, names::Decorator, names::OutputTransformation, names::SingleLine, names::MaxLines, names::MinLines, names::VisualTransformation, names::Placeholder, names::OnSubmit, names::TextFieldColors, names::Colors>;
        static_assert(P::check<A...>());
        const TextFieldState& state = P::arg<names::State>(args...);
        TextFieldOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::ReadOnly>(options.readOnly, args...);
        P::set<names::InputTransformation>(options.inputTransformation, args...);
        P::set<names::TextStyle>(options.textStyle, args...);
        P::set<names::KeyboardOptions>(options.keyboardOptions, args...);
        P::set<names::KeyboardActions>(options.keyboardActions, args...);
        P::set<names::LineLimits>(options.lineLimits, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::Decorator>(options.decorator, args...);
        P::set<names::OutputTransformation>(options.outputTransformation, args...);
        P::set<names::SingleLine>(options.singleLine, args...);
        P::set<names::MaxLines>(options.maxLines, args...);
        P::set<names::MinLines>(options.minLines, args...);
        P::set<names::VisualTransformation>(options.visualTransformation, args...);
        detail::textOrContent<names::Placeholder>(options.placeholder, options.placeholderContent, args...);
        P::set<names::OnSubmit>(options.onSubmit, args...);
        P::set<names::TextFieldColors>(options.colors, args...);
        P::set<names::Colors>(options.colors, args...);
        BasicTextField(state, P::get<names::Modifier>(Modifier{}, args...), std::move(options), site);
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {  // the value's form, or the state's
        if constexpr (countOf<names::State, A...> > 0 || startsWith<TextFieldState, A...>()) stateForm(site, std::forward<A>(args)...);
        else valueForm(site, std::forward<A>(args)...);
    }
};
} // namespace detail
inline constexpr Composable<detail::BasicTextFieldImpl> BasicTextField;
namespace detail {
void OutlinedTextField(const std::string& value, std::function<void(std::string)> onValueChange,
                       const Modifier& modifier = {}, TextFieldOptions options = {}, CallSite site = CallSite::current());
void OutlinedTextField(const TextFieldState& state, const Modifier& modifier = {}, TextFieldOptions options = {},
                       CallSite site = CallSite::current());
struct OutlinedTextFieldImpl {
    template <class... A>
    static void valueForm(CallSite site, A&&... args) {
        using P = Params<names::Value, names::OnValueChange, names::Modifier, names::Enabled, names::ReadOnly, names::TextStyle, names::Label, names::Placeholder, names::LeadingIcon, names::TrailingIcon, names::Prefix, names::Suffix, names::SupportingText, names::IsError, names::VisualTransformation, names::KeyboardOptions, names::KeyboardActions, names::SingleLine, names::MaxLines, names::MinLines, names::InteractionSource, names::Shape, names::TextFieldColors, names::Colors, names::LineLimits, names::OnSubmit, names::InputTransformation, names::OutputTransformation, names::LabelPosition>;
        static_assert(P::check<A...>());
        const std::string& value = P::arg<names::Value>(args...);
        std::function<void(std::string)> onValueChange = P::arg<names::OnValueChange>(args...);
        TextFieldOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::ReadOnly>(options.readOnly, args...);
        P::set<names::TextStyle>(options.textStyle, args...);
        detail::textOrContent<names::Label>(options.label, options.labelContent, args...);
        detail::textOrContent<names::Placeholder>(options.placeholder, options.placeholderContent, args...);
        P::set<names::LeadingIcon>(options.leadingIcon, args...);
        P::set<names::TrailingIcon>(options.trailingIcon, args...);
        P::set<names::Prefix>(options.prefix, args...);
        P::set<names::Suffix>(options.suffix, args...);
        detail::textOrContent<names::SupportingText>(options.supportingText, options.supportingTextContent, args...);
        P::set<names::IsError>(options.isError, args...);
        P::set<names::VisualTransformation>(options.visualTransformation, args...);
        P::set<names::KeyboardOptions>(options.keyboardOptions, args...);
        P::set<names::KeyboardActions>(options.keyboardActions, args...);
        P::set<names::SingleLine>(options.singleLine, args...);
        P::set<names::MaxLines>(options.maxLines, args...);
        P::set<names::MinLines>(options.minLines, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::Shape>(options.shape, args...);
        P::set<names::TextFieldColors>(options.colors, args...);
        P::set<names::Colors>(options.colors, args...);
        P::set<names::LineLimits>(options.lineLimits, args...);
        P::set<names::OnSubmit>(options.onSubmit, args...);
        P::set<names::InputTransformation>(options.inputTransformation, args...);
        P::set<names::OutputTransformation>(options.outputTransformation, args...);
        P::set<names::LabelPosition>(options.labelPosition, args...);
        OutlinedTextField(value, std::move(onValueChange), P::get<names::Modifier>(Modifier{}, args...), std::move(options), site);
    }
    template <class... A>
    static void stateForm(CallSite site, A&&... args) {
        using P = Params<names::State, names::Modifier, names::Enabled, names::ReadOnly, names::InputTransformation, names::TextStyle, names::LabelPosition, names::Label, names::Placeholder, names::LeadingIcon, names::TrailingIcon, names::Prefix, names::Suffix, names::SupportingText, names::IsError, names::KeyboardOptions, names::KeyboardActions, names::LineLimits, names::Shape, names::TextFieldColors, names::Colors, names::InteractionSource, names::OutputTransformation, names::SingleLine, names::MaxLines, names::MinLines, names::VisualTransformation, names::OnSubmit>;
        static_assert(P::check<A...>());
        const TextFieldState& state = P::arg<names::State>(args...);
        TextFieldOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::ReadOnly>(options.readOnly, args...);
        P::set<names::InputTransformation>(options.inputTransformation, args...);
        P::set<names::TextStyle>(options.textStyle, args...);
        P::set<names::LabelPosition>(options.labelPosition, args...);
        detail::textOrContent<names::Label>(options.label, options.labelContent, args...);
        detail::textOrContent<names::Placeholder>(options.placeholder, options.placeholderContent, args...);
        P::set<names::LeadingIcon>(options.leadingIcon, args...);
        P::set<names::TrailingIcon>(options.trailingIcon, args...);
        P::set<names::Prefix>(options.prefix, args...);
        P::set<names::Suffix>(options.suffix, args...);
        detail::textOrContent<names::SupportingText>(options.supportingText, options.supportingTextContent, args...);
        P::set<names::IsError>(options.isError, args...);
        P::set<names::KeyboardOptions>(options.keyboardOptions, args...);
        P::set<names::KeyboardActions>(options.keyboardActions, args...);
        P::set<names::LineLimits>(options.lineLimits, args...);
        P::set<names::Shape>(options.shape, args...);
        P::set<names::TextFieldColors>(options.colors, args...);
        P::set<names::Colors>(options.colors, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::OutputTransformation>(options.outputTransformation, args...);
        P::set<names::SingleLine>(options.singleLine, args...);
        P::set<names::MaxLines>(options.maxLines, args...);
        P::set<names::MinLines>(options.minLines, args...);
        P::set<names::VisualTransformation>(options.visualTransformation, args...);
        P::set<names::OnSubmit>(options.onSubmit, args...);
        OutlinedTextField(state, P::get<names::Modifier>(Modifier{}, args...), std::move(options), site);
    }
    template <class... A>
    static void call(CallSite site, A&&... args) {  // the value's form, or the state's
        if constexpr (countOf<names::State, A...> > 0 || startsWith<TextFieldState, A...>()) stateForm(site, std::forward<A>(args)...);
        else valueForm(site, std::forward<A>(args)...);
    }
};
} // namespace detail
inline constexpr Composable<detail::OutlinedTextFieldImpl> OutlinedTextField;

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
    Content labelContent, placeholderContent, supportingTextContent;  // Compose's composable forms (TextFieldOptions')
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
//   SecureTextField(password, Modifier(), label = "Parola");
namespace detail {
void SecureTextField(const TextFieldState& state, const Modifier& modifier = {}, SecureTextFieldOptions options = {},
                     CallSite site = CallSite::current());
struct SecureTextFieldImpl {
    template <class... A>
    static void call(CallSite site, A&&... args) {
        using P = Params<names::State, names::Modifier, names::Enabled, names::TextStyle, names::LabelPosition, names::Label, names::Placeholder, names::LeadingIcon, names::TrailingIcon, names::Prefix, names::Suffix, names::SupportingText, names::IsError, names::InputTransformation, names::TextObfuscationMode, names::TextObfuscationCharacter, names::KeyboardOptions, names::KeyboardActions, names::Shape, names::TextFieldColors, names::InteractionSource, names::OnSubmit>;
        static_assert(P::check<A...>());
        const TextFieldState& state = P::arg<names::State>(args...);
        SecureTextFieldOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::TextStyle>(options.textStyle, args...);
        P::set<names::LabelPosition>(options.labelPosition, args...);
        detail::textOrContent<names::Label>(options.label, options.labelContent, args...);
        detail::textOrContent<names::Placeholder>(options.placeholder, options.placeholderContent, args...);
        P::set<names::LeadingIcon>(options.leadingIcon, args...);
        P::set<names::TrailingIcon>(options.trailingIcon, args...);
        P::set<names::Prefix>(options.prefix, args...);
        P::set<names::Suffix>(options.suffix, args...);
        detail::textOrContent<names::SupportingText>(options.supportingText, options.supportingTextContent, args...);
        P::set<names::IsError>(options.isError, args...);
        P::set<names::InputTransformation>(options.inputTransformation, args...);
        P::set<names::TextObfuscationMode>(options.textObfuscationMode, args...);
        P::set<names::TextObfuscationCharacter>(options.textObfuscationCharacter, args...);
        P::set<names::KeyboardOptions>(options.keyboardOptions, args...);
        P::set<names::KeyboardActions>(options.keyboardActions, args...);
        P::set<names::Shape>(options.shape, args...);
        P::set<names::TextFieldColors>(options.colors, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::OnSubmit>(options.onSubmit, args...);
        SecureTextField(state, P::get<names::Modifier>(Modifier{}, args...), std::move(options), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::SecureTextFieldImpl> SecureTextField;
namespace detail {
void OutlinedSecureTextField(const TextFieldState& state, const Modifier& modifier = {}, SecureTextFieldOptions options = {},
                             CallSite site = CallSite::current());
struct OutlinedSecureTextFieldImpl {
    template <class... A>
    static void call(CallSite site, A&&... args) {
        using P = Params<names::State, names::Modifier, names::Enabled, names::TextStyle, names::LabelPosition, names::Label, names::Placeholder, names::LeadingIcon, names::TrailingIcon, names::Prefix, names::Suffix, names::SupportingText, names::IsError, names::InputTransformation, names::TextObfuscationMode, names::TextObfuscationCharacter, names::KeyboardOptions, names::KeyboardActions, names::Shape, names::TextFieldColors, names::InteractionSource, names::OnSubmit>;
        static_assert(P::check<A...>());
        const TextFieldState& state = P::arg<names::State>(args...);
        SecureTextFieldOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::TextStyle>(options.textStyle, args...);
        P::set<names::LabelPosition>(options.labelPosition, args...);
        detail::textOrContent<names::Label>(options.label, options.labelContent, args...);
        detail::textOrContent<names::Placeholder>(options.placeholder, options.placeholderContent, args...);
        P::set<names::LeadingIcon>(options.leadingIcon, args...);
        P::set<names::TrailingIcon>(options.trailingIcon, args...);
        P::set<names::Prefix>(options.prefix, args...);
        P::set<names::Suffix>(options.suffix, args...);
        detail::textOrContent<names::SupportingText>(options.supportingText, options.supportingTextContent, args...);
        P::set<names::IsError>(options.isError, args...);
        P::set<names::InputTransformation>(options.inputTransformation, args...);
        P::set<names::TextObfuscationMode>(options.textObfuscationMode, args...);
        P::set<names::TextObfuscationCharacter>(options.textObfuscationCharacter, args...);
        P::set<names::KeyboardOptions>(options.keyboardOptions, args...);
        P::set<names::KeyboardActions>(options.keyboardActions, args...);
        P::set<names::Shape>(options.shape, args...);
        P::set<names::TextFieldColors>(options.colors, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::OnSubmit>(options.onSubmit, args...);
        OutlinedSecureTextField(state, P::get<names::Modifier>(Modifier{}, args...), std::move(options), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::OutlinedSecureTextFieldImpl> OutlinedSecureTextField;
// The same without decoration (Compose's BasicSecureTextField): the dots alone, for a field
// of your own (the label and supporting text aside).
namespace detail {
void BasicSecureTextField(const TextFieldState& state, const Modifier& modifier = {}, SecureTextFieldOptions options = {},
                          CallSite site = CallSite::current());
struct BasicSecureTextFieldImpl {
    template <class... A>
    static void call(CallSite site, A&&... args) {
        using P = Params<names::State, names::Modifier, names::Enabled, names::InputTransformation, names::TextStyle, names::InteractionSource, names::Decorator, names::TextObfuscationMode, names::TextObfuscationCharacter, names::KeyboardOptions, names::KeyboardActions, names::Placeholder, names::OnSubmit, names::TextFieldColors>;
        static_assert(P::check<A...>());
        const TextFieldState& state = P::arg<names::State>(args...);
        SecureTextFieldOptions options;
        P::set<names::Enabled>(options.enabled, args...);
        P::set<names::InputTransformation>(options.inputTransformation, args...);
        P::set<names::TextStyle>(options.textStyle, args...);
        P::set<names::InteractionSource>(options.interactionSource, args...);
        P::set<names::Decorator>(options.decorator, args...);
        P::set<names::TextObfuscationMode>(options.textObfuscationMode, args...);
        P::set<names::TextObfuscationCharacter>(options.textObfuscationCharacter, args...);
        P::set<names::KeyboardOptions>(options.keyboardOptions, args...);
        P::set<names::KeyboardActions>(options.keyboardActions, args...);
        detail::textOrContent<names::Placeholder>(options.placeholder, options.placeholderContent, args...);
        P::set<names::OnSubmit>(options.onSubmit, args...);
        P::set<names::TextFieldColors>(options.colors, args...);
        BasicSecureTextField(state, P::get<names::Modifier>(Modifier{}, args...), std::move(options), site);
    }
};
} // namespace detail
inline constexpr Composable<detail::BasicSecureTextFieldImpl> BasicSecureTextField;

} // namespace kompoz
