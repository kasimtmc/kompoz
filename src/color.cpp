// Color's members that go through color spaces (Compose's Color.kt).

#include <kompoz/graphics.hpp>

#include <cstdint>
#include <stdexcept>
#include <string>

// Compose's float arithmetic as the JVM does it: no fused multiply-adds.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

namespace kompoz {

namespace {
// Compose's UncheckedColor: packed as given, nothing kept within a range (an sRGB one's channels
// only as Kotlin's toInt saturates them).
Color uncheckedColor(float red, float green, float blue, float alpha, const ColorSpace& colorSpace) {
    if (colorSpace.isSrgb()) {
        auto byte = [](float v) {  // (Kotlin's toInt: NaN 0, saturated)
            float x = v * 255.f + 0.5f;
            return uint32_t(x != x ? 0 : x >= 2147483648.f ? INT32_MAX : x <= -2147483648.f ? INT32_MIN : int32_t(x));
        };
        uint32_t argb = byte(alpha) << 24 | byte(red) << 16 | byte(green) << 8 | byte(blue);
        return Color(uint64_t(argb) << 32);
    }
    uint64_t r = detail::floatToHalf(red), g = detail::floatToHalf(green), b = detail::floatToHalf(blue);
    uint64_t a = uint64_t(int(std::max(0.f, std::min(alpha, 1.f)) * 1023.f + 0.5f));
    return Color(r << 48 | g << 32 | b << 16 | (a & 0x3ff) << 6 | (uint64_t(colorSpace.id()) & 0x3f));
}

float hsvToRgbComponent(int n, float h, float s, float v) {
    float k = std::fmod(float(n) + h / 60.f, 6.f);
    return v - (v * s * std::max(0.f, std::min({k, 4 - k, 1.f})));
}

float hslToRgbComponent(int n, float h, float s, float l) {
    float k = std::fmod(float(n) + h / 30.f, 12.f);
    float a = s * std::min(l, 1.f - l);
    return l - a * std::max(-1.f, std::min({k - 3, 9 - k, 1.f}));
}

float compositeComponent(float fgC, float bgC, float fgA, float bgA, float a) {
    return a == 0.f ? 0.f : ((fgC * fgA) + ((bgC * bgA) * (1.f - fgA))) / a;
}
} // namespace

Color::Color(float red, float green, float blue, float alpha, const ColorSpace& colorSpace) {
    if (colorSpace.isSrgb()) {
        *this = Color(red, green, blue, alpha);
        return;
    }
    if (colorSpace.componentCount() != 3) throw std::invalid_argument("Color only works with ColorSpaces with 3 components");
    int id = colorSpace.id();
    if (id == ColorSpace::MinId) throw std::invalid_argument("Unknown color space, please use a color space in ColorSpaces");
    uint64_t r = detail::floatToHalf(std::clamp(red, colorSpace.getMinValue(0), colorSpace.getMaxValue(0)));
    uint64_t g = detail::floatToHalf(std::clamp(green, colorSpace.getMinValue(1), colorSpace.getMaxValue(1)));
    uint64_t b = detail::floatToHalf(std::clamp(blue, colorSpace.getMinValue(2), colorSpace.getMaxValue(2)));
    uint64_t a = alpha != alpha ? 0 : uint64_t(int(std::clamp(alpha, 0.f, 1.f) * 1023.f + 0.5f));
    value_ = r << 48 | g << 32 | b << 16 | (a & 0x3ff) << 6 | (uint64_t(id) & 0x3f);
}

const ColorSpace& Color::colorSpace() const { return ColorSpaces::getColorSpace(int(value_ & 0x3f)); }

Color Color::convert(const ColorSpace& colorSpace) const {
    const ColorSpace& source = this->colorSpace();
    if (&source == &colorSpace) return *this;  // (the identity connector's)
    auto v = source.connect(colorSpace).transform(red(), green(), blue());
    return Color(v[0], v[1], v[2], alpha(), colorSpace);
}

Color Color::copy(float alpha, float red, float green, float blue) const { return Color(red, green, blue, alpha, colorSpace()); }

Color Color::hsv(float hue, float saturation, float value, float alpha) { return hsv(hue, saturation, value, alpha, ColorSpaces::Srgb); }

Color Color::hsv(float hue, float saturation, float value, float alpha, const Rgb& colorSpace) {
    if (!(hue >= 0 && hue <= 360 && saturation >= 0 && saturation <= 1 && value >= 0 && value <= 1))
        throw std::invalid_argument("HSV (" + std::to_string(hue) + ", " + std::to_string(saturation) + ", " + std::to_string(value) +
                                    ") must be in range (0..360, 0..1, 0..1)");
    return Color(hsvToRgbComponent(5, hue, saturation, value), hsvToRgbComponent(3, hue, saturation, value),
                 hsvToRgbComponent(1, hue, saturation, value), alpha, colorSpace);
}

Color Color::hsl(float hue, float saturation, float lightness, float alpha) { return hsl(hue, saturation, lightness, alpha, ColorSpaces::Srgb); }

Color Color::hsl(float hue, float saturation, float lightness, float alpha, const Rgb& colorSpace) {
    if (!(hue >= 0 && hue <= 360 && saturation >= 0 && saturation <= 1 && lightness >= 0 && lightness <= 1))
        throw std::invalid_argument("HSL (" + std::to_string(hue) + ", " + std::to_string(saturation) + ", " + std::to_string(lightness) +
                                    ") must be in range (0..360, 0..1, 0..1)");
    return Color(hslToRgbComponent(0, hue, saturation, lightness), hslToRgbComponent(8, hue, saturation, lightness),
                 hslToRgbComponent(4, hue, saturation, lightness), alpha, colorSpace);
}

Color Color::compositeOver(Color background) const {
    const ColorSpace& space = background.colorSpace();
    Color fg = convert(space);
    float bgA = background.alpha(), fgA = fg.alpha();
    float a = fgA + (bgA * (1.f - fgA));
    return uncheckedColor(compositeComponent(fg.red(), background.red(), fgA, bgA, a),
                          compositeComponent(fg.green(), background.green(), fgA, bgA, a),
                          compositeComponent(fg.blue(), background.blue(), fgA, bgA, a), a, space);
}

float Color::luminance() const {
    const ColorSpace& space = colorSpace();
    if (space.model() != ColorModel::Rgb)
        throw std::invalid_argument("The specified color must be encoded in an RGB color space. The supplied color space is " +
                                    std::string(toString(space.model())));
    const auto& eotf = static_cast<const Rgb&>(space).eotf();
    double r = eotf(double(red())), g = eotf(double(green())), b = eotf(double(blue()));
    return std::clamp(float((0.2126 * r) + (0.7152 * g) + (0.0722 * b)), 0.f, 1.f);
}

uint32_t Color::toArgb() const { return uint32_t(convert(ColorSpaces::Srgb).value_ >> 32); }

Color lerp(const Color& start, const Color& stop, float t) {
    const ColorSpace& oklab = ColorSpaces::Oklab;
    Color s = start.convert(oklab), e = stop.convert(oklab);
    t = std::clamp(t, 0.f, 1.f);  // (Compose's: an easing's overshoot kept within the colors)
    Color interpolated = uncheckedColor(lerp(s.red(), e.red(), t), lerp(s.green(), e.green(), t), lerp(s.blue(), e.blue(), t),
                                        lerp(s.alpha(), e.alpha(), t), oklab);
    return interpolated.convert(stop.colorSpace());
}

std::array<float, 4> detail::srgbComponents(Color c) {
    if (c.isSrgb()) return {c.red(), c.green(), c.blue(), c.alpha()};
    auto v = c.colorSpace().connect(ColorSpaces::ExtendedSrgb).transform(c.red(), c.green(), c.blue());
    return {v[0], v[1], v[2], c.alpha()};
}

Color detail::animatedColor(Color from, Color to, float fraction) {
    const ColorSpace& oklab = ColorSpaces::Oklab;
    Color s = from.convert(oklab), e = to.convert(oklab);
    // (Color keeps L within 0..1 and a, b within -0.5..0.5, as the converter coerces them.)
    return Color(lerp(s.red(), e.red(), fraction), lerp(s.green(), e.green(), fraction), lerp(s.blue(), e.blue(), fraction),
                 lerp(s.alpha(), e.alpha(), fraction), oklab)
        .convert(to.colorSpace());
}

} // namespace kompoz
