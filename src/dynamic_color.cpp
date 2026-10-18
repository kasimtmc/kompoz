// Material You: color schemes from the system's wallpaper palette, with Compose's tone mapping
// (material3 DynamicTonalPalette, the API 31 variant).

#include <kompoz/widgets.hpp>

#include <cmath>

namespace kompoz {

namespace {

// sRGB <-> CIELAB (D65). A palette tone is the L* of a color.
struct Lab {
    float l, a, b;
};

float toLinear(float c) { return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f); }
float fromLinear(float c) { return c <= 0.0031308f ? c * 12.92f : 1.055f * std::pow(c, 1 / 2.4f) - 0.055f; }

constexpr float kXn = 0.95047f, kYn = 1.f, kZn = 1.08883f;
constexpr float kDelta = 6.f / 29;

float labF(float t) { return t > kDelta * kDelta * kDelta ? std::cbrt(t) : t / (3 * kDelta * kDelta) + 4.f / 29; }
float labFInverse(float t) { return t > kDelta ? t * t * t : 3 * kDelta * kDelta * (t - 4.f / 29); }

Lab toLab(Color c) {
    float r = toLinear(c.r), g = toLinear(c.g), b = toLinear(c.b);
    float x = 0.4124564f * r + 0.3575761f * g + 0.1804375f * b;
    float y = 0.2126729f * r + 0.7151522f * g + 0.0721750f * b;
    float z = 0.0193339f * r + 0.1191920f * g + 0.9503041f * b;
    float fx = labF(x / kXn), fy = labF(y / kYn), fz = labF(z / kZn);
    return {116 * fy - 16, 500 * (fx - fy), 200 * (fy - fz)};
}

Color fromLab(Lab lab, float alpha) {
    float fy = (lab.l + 16) / 116, fx = fy + lab.a / 500, fz = fy - lab.b / 200;
    float x = kXn * labFInverse(fx), y = kYn * labFInverse(fy), z = kZn * labFInverse(fz);
    float r = 3.2404542f * x - 1.5371385f * y - 0.4985314f * z;
    float g = -0.9692660f * x + 1.8760108f * y + 0.0415560f * z;
    float b = 0.0556434f * x - 0.2040259f * y + 1.0572252f * z;
    auto channel = [](float v) { return std::clamp(fromLinear(std::clamp(v, 0.f, 1.f)), 0.f, 1.f); };
    return {channel(r), channel(g), channel(b), alpha};
}

} // namespace

// The system gives 13 tones; the others (98, 94, 87, 6, ...) take the nearest one's color at
// that lightness, as Compose does with setLuminance.
Color paletteTone(const std::array<Color, 13>& keyColor, float tone) {
    size_t nearest = 0;
    for (size_t i = 1; i < keyColor.size(); ++i)
        if (std::abs(float(TonalPalette::kTones[i]) - tone) < std::abs(float(TonalPalette::kTones[nearest]) - tone)) nearest = i;
    const Color& c = keyColor[nearest];
    if (float(TonalPalette::kTones[nearest]) == tone) return c;
    Lab lab = toLab(c);
    lab.l = tone;
    return fromLab(lab, c.a);
}

// The fixed colors, the same light or dark (Compose's): 90 and 80, on them 10 and 30.
template <class P, class S, class T>
void fixedColors(ColorScheme& s, P primary, S secondary, T tertiary) {
    s.primaryFixed = primary(90), s.primaryFixedDim = primary(80), s.onPrimaryFixed = primary(10), s.onPrimaryFixedVariant = primary(30);
    s.secondaryFixed = secondary(90), s.secondaryFixedDim = secondary(80), s.onSecondaryFixed = secondary(10),
    s.onSecondaryFixedVariant = secondary(30);
    s.tertiaryFixed = tertiary(90), s.tertiaryFixedDim = tertiary(80), s.onTertiaryFixed = tertiary(10), s.onTertiaryFixedVariant = tertiary(30);
}

ColorScheme dynamicLightColorScheme(const TonalPalette& p) {
    auto primary = [&](float t) { return paletteTone(p.accent1, t); };
    auto secondary = [&](float t) { return paletteTone(p.accent2, t); };
    auto tertiary = [&](float t) { return paletteTone(p.accent3, t); };
    auto neutral = [&](float t) { return paletteTone(p.neutral1, t); };
    auto neutralVariant = [&](float t) { return paletteTone(p.neutral2, t); };
    ColorScheme s = lightColorScheme();  // keeps the error colors
    s.primary = primary(40);
    s.onPrimary = primary(100);
    s.primaryContainer = primary(90);
    s.onPrimaryContainer = primary(10);
    s.inversePrimary = primary(80);
    s.secondary = secondary(40);
    s.onSecondary = secondary(100);
    s.secondaryContainer = secondary(90);
    s.onSecondaryContainer = secondary(10);
    s.tertiary = tertiary(40);
    s.onTertiary = tertiary(100);
    s.tertiaryContainer = tertiary(90);
    s.onTertiaryContainer = tertiary(10);
    s.background = neutral(98);
    s.onBackground = neutral(10);
    s.surface = neutral(98);
    s.onSurface = neutral(10);
    s.onSurfaceVariant = neutralVariant(30);
    s.inverseSurface = neutral(20);
    s.inverseOnSurface = neutral(95);
    s.outline = neutralVariant(50);
    s.outlineVariant = neutralVariant(80);
    s.surfaceContainerLow = neutral(96);
    s.surfaceContainer = neutral(94);
    s.surfaceContainerHigh = neutral(92);
    s.surfaceContainerHighest = neutral(90);
    s.surfaceVariant = neutralVariant(90);
    s.surfaceTint = s.primary;
    s.surfaceBright = neutral(98);
    s.surfaceDim = neutral(87);
    s.surfaceContainerLowest = neutral(100);
    fixedColors(s, primary, secondary, tertiary);
    return s;
}

ColorScheme dynamicDarkColorScheme(const TonalPalette& p) {
    auto primary = [&](float t) { return paletteTone(p.accent1, t); };
    auto secondary = [&](float t) { return paletteTone(p.accent2, t); };
    auto tertiary = [&](float t) { return paletteTone(p.accent3, t); };
    auto neutral = [&](float t) { return paletteTone(p.neutral1, t); };
    auto neutralVariant = [&](float t) { return paletteTone(p.neutral2, t); };
    ColorScheme s = darkColorScheme();
    s.primary = primary(80);
    s.onPrimary = primary(20);
    s.primaryContainer = primary(30);
    s.onPrimaryContainer = primary(90);
    s.inversePrimary = primary(40);
    s.secondary = secondary(80);
    s.onSecondary = secondary(20);
    s.secondaryContainer = secondary(30);
    s.onSecondaryContainer = secondary(90);
    s.tertiary = tertiary(80);
    s.onTertiary = tertiary(20);
    s.tertiaryContainer = tertiary(30);
    s.onTertiaryContainer = tertiary(90);
    s.background = neutral(6);
    s.onBackground = neutral(90);
    s.surface = neutral(6);
    s.onSurface = neutral(90);
    s.onSurfaceVariant = neutralVariant(80);
    s.inverseSurface = neutral(90);
    s.inverseOnSurface = neutral(20);
    s.outline = neutralVariant(60);
    s.outlineVariant = neutralVariant(30);
    s.surfaceContainerLow = neutral(10);
    s.surfaceContainer = neutral(12);
    s.surfaceContainerHigh = neutral(17);
    s.surfaceContainerHighest = neutral(22);
    s.surfaceVariant = neutralVariant(30);
    s.surfaceTint = s.primary;
    s.surfaceBright = neutral(24);
    s.surfaceDim = neutral(6);
    s.surfaceContainerLowest = neutral(4);
    fixedColors(s, primary, secondary, tertiary);
    return s;
}

bool isDynamicColorAvailable() { return LocalDynamicTonalPalette.current().has_value(); }

ColorScheme dynamicLightColorScheme() {
    const auto& palette = LocalDynamicTonalPalette.current();
    return palette ? dynamicLightColorScheme(*palette) : lightColorScheme();
}

ColorScheme dynamicDarkColorScheme() {
    const auto& palette = LocalDynamicTonalPalette.current();
    return palette ? dynamicDarkColorScheme(*palette) : darkColorScheme();
}

bool isWallpaperColorAvailable() { return LocalWallpaperTonalPalette.current().has_value(); }

ColorScheme wallpaperLightColorScheme() {
    const auto& palette = LocalWallpaperTonalPalette.current();
    return palette ? dynamicLightColorScheme(*palette) : lightColorScheme();
}

ColorScheme wallpaperDarkColorScheme() {
    const auto& palette = LocalWallpaperTonalPalette.current();
    return palette ? dynamicDarkColorScheme(*palette) : darkColorScheme();
}

} // namespace kompoz
