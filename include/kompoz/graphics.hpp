#pragma once

// Platform-independent geometry, color and drawing interfaces.
// Every coordinate in Kompoz is in dp; backends convert to pixels.

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <initializer_list>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

#include <kompoz/colorspace.hpp>

namespace kompoz {

inline constexpr float kInfinity = std::numeric_limits<float>::infinity();

namespace detail {
// Compose's floatToHalf and halfToFloat (its Float16's), bit for bit.
constexpr uint16_t floatToHalf(float f) {
    uint32_t bits = std::bit_cast<uint32_t>(f), s = bits >> 31;
    int e = int(bits >> 23 & 0xff);
    uint32_t m = bits & 0x7fffff, outE = 0, outM = 0;
    if (e == 0xff) {  // infinite or NaN
        outE = 0x1f;
        outM = m != 0 ? 0x200 : 0;
    } else {
        e = e - 127 + 15;
        if (e >= 0x1f) {  // overflow (as Compose writes it)
            outE = 0x31;
        } else if (e <= 0) {  // underflow
            if (e >= -10) {  // a normal float under a half's smallest normal: a subnormal half
                m = (m | 0x800000) >> (1 - e);
                if (m & 0x1000) m += 0x2000;
                outM = m >> 13;
            }
        } else {
            outE = uint32_t(e);
            outM = m >> 13;
            if (m & 0x1000) return uint16_t(((outE << 10 | outM) + 1) | s << 15);  // (a half up)
        }
    }
    return uint16_t(s << 15 | outE << 10 | outM);
}
constexpr float halfToFloat(uint16_t h) {
    uint32_t bits = h, s = bits & 0x8000, e = bits >> 10 & 0x1f, m = bits & 0x3ff, outE = 0, outM = 0;
    if (e == 0) {  // subnormal or 0
        if (m != 0) {
            float o = std::bit_cast<float>(uint32_t(126 << 23) + m) - std::bit_cast<float>(uint32_t(126 << 23));
            return s == 0 ? o : -o;
        }
    } else {
        outM = m << 13;
        if (e == 0x1f) {  // infinite or NaN
            outE = 0xff;
            if (outM != 0) outM |= 0x400000;
        } else {
            outE = e - 15 + 127;
        }
    }
    return std::bit_cast<float>(s << 16 | outE << 23 | outM);
}
// A channel 0..1 in 8 bits, as Compose packs an sRGB color's (NaN as 0).
constexpr uint64_t byteOf(float v) { return v != v ? 0 : uint64_t(int(std::clamp(v, 0.f, 1.f) * 255.f + 0.5f)); }
} // namespace detail

// A color (Compose's Color): 64 bits, packed as Compose packs them. One in sRGB keeps 8 bits a
// channel, 0..1 (its ARGB in the upper 32 bits); one in another color space keeps its
// components as half floats (each within the space's range), its alpha in 10 bits and the
// space's id (ColorSpaces). red(), green(), blue() are the components in its color space (an
// Lab or Oklab one's L, a, b), alpha() its alpha.
class Color {
public:
    constexpr Color() = default;  // opaque black (Kompoz's: Compose's Color has none)
    explicit constexpr Color(uint64_t value) : value_(value) {}  // Compose's Color(value)
    // In sRGB (Compose's Color(red, green, blue, alpha)).
    constexpr Color(float red, float green, float blue, float alpha = 1)
        : value_((detail::byteOf(alpha) << 24 | detail::byteOf(red) << 16 | detail::byteOf(green) << 8 | detail::byteOf(blue)) << 32) {}
    // In `colorSpace`, each component kept within its range (one of three components, of ColorSpaces).
    Color(float red, float green, float blue, float alpha, const ColorSpace& colorSpace);
    static constexpr Color hex(uint32_t rgb, float alpha = 1) {  // 0xRRGGBB, in sRGB
        return Color((detail::byteOf(alpha) << 24 | (rgb & 0xFFFFFFu)) << 32);
    }
    static constexpr Color argb(uint32_t argb) { return Color(uint64_t(argb) << 32); }  // 0xAARRGGBB (Compose's Color(color: Int))
    // From hue (0..360), saturation and value or lightness (0..1), in an RGB color space (Compose's
    // Color.hsv, Color.hsl).
    static Color hsv(float hue, float saturation, float value, float alpha = 1);
    static Color hsv(float hue, float saturation, float value, float alpha, const Rgb& colorSpace);
    static Color hsl(float hue, float saturation, float lightness, float alpha = 1);
    static Color hsl(float hue, float saturation, float lightness, float alpha, const Rgb& colorSpace);

    constexpr uint64_t value() const { return value_; }
    constexpr bool isSrgb() const { return (value_ & 0x3f) == 0; }  // (sRGB's id: 0)
    constexpr float red() const { return isSrgb() ? float(value_ >> 48 & 0xff) / 255.f : detail::halfToFloat(uint16_t(value_ >> 48)); }
    constexpr float green() const { return isSrgb() ? float(value_ >> 40 & 0xff) / 255.f : detail::halfToFloat(uint16_t(value_ >> 32)); }
    constexpr float blue() const { return isSrgb() ? float(value_ >> 32 & 0xff) / 255.f : detail::halfToFloat(uint16_t(value_ >> 16)); }
    constexpr float alpha() const { return isSrgb() ? float(value_ >> 56 & 0xff) / 255.f : float(value_ >> 6 & 0x3ff) / 1023.f; }
    const ColorSpace& colorSpace() const;
    // The same color in `colorSpace` (Compose's convert: through the spaces' connector,
    // perceptually).
    Color convert(const ColorSpace& colorSpace) const;
    // With another alpha, its components as they are (Compose's copy(alpha = …)).
    constexpr Color copy(float alpha) const {
        if (isSrgb()) return Color((value_ & ~(uint64_t(0xff) << 56)) | detail::byteOf(alpha) << 56);
        uint64_t a = alpha != alpha ? 0 : uint64_t(int(std::clamp(alpha, 0.f, 1.f) * 1023.f + 0.5f));
        return Color((value_ & ~(uint64_t(0x3ff) << 6)) | a << 6);
    }
    Color copy(float alpha, float red, float green, float blue) const;  // in its color space
    // This over `background`, in the background's color space (Compose's compositeOver).
    Color compositeOver(Color background) const;
    // Its relative luminance, 0..1 (Compose's luminance: an RGB color space's).
    float luminance() const;
    uint32_t toArgb() const;  // in sRGB, 0xAARRGGBB (Compose's toArgb)
    constexpr bool isSpecified() const { return value_ != 0x10; }  // (Compose's: not Unspecified)
    constexpr bool isUnspecified() const { return value_ == 0x10; }

    friend constexpr bool operator==(const Color&, const Color&) = default;

private:
    uint64_t value_ = uint64_t(0xFF000000u) << 32;
};

// Compose's Color.Black, … (Unspecified: no color, in ColorSpaces::Unspecified).
namespace colors {
inline constexpr Color Transparent = Color::argb(0x00000000);
inline constexpr Color Black = Color::argb(0xFF000000);
inline constexpr Color DarkGray = Color::argb(0xFF444444);
inline constexpr Color Gray = Color::argb(0xFF888888);
inline constexpr Color LightGray = Color::argb(0xFFCCCCCC);
inline constexpr Color White = Color::argb(0xFFFFFFFF);
inline constexpr Color Red = Color::argb(0xFFFF0000);
inline constexpr Color Green = Color::argb(0xFF00FF00);
inline constexpr Color Blue = Color::argb(0xFF0000FF);
inline constexpr Color Yellow = Color::argb(0xFFFFFF00);
inline constexpr Color Cyan = Color::argb(0xFF00FFFF);
inline constexpr Color Magenta = Color::argb(0xFFFF00FF);
inline constexpr Color Unspecified = Color(uint64_t(0x10));
} // namespace colors

constexpr float lerp(float a, float b, float t) { return a + (b - a) * t; }
// Between two colors, in Oklab, in `stop`'s color space (Compose's lerp: `t` within 0..1).
Color lerp(const Color& start, const Color& stop, float t);
namespace detail {
// `fraction` of the way from `from` to `to` as Compose animates a color (its ColorVectorConverter):
// in Oklab, each component as far along (past 0..1 too, as a spring overshoots it), kept within
// Oklab's range and alpha within 0..1, in `to`'s color space.
Color animatedColor(Color from, Color to, float fraction);
// The color in sRGB, as it is drawn (to an sRGB surface, as Compose's to Android's), 8 bits a
// channel; and its components so, red, green, blue and alpha, not rounded to 8 bits (as Skia
// draws a paint's color): another space's in extended sRGB (past 0..1 where it is past sRGB).
inline Color inSrgb(Color c) { return c.isSrgb() ? c : c.convert(ColorSpaces::Srgb); }
std::array<float, 4> srgbComponents(Color c);
} // namespace detail

struct Offset {
    float x = 0, y = 0;
    friend constexpr bool operator==(const Offset&, const Offset&) = default;
};

struct Size {
    float width = 0, height = 0;
    friend constexpr bool operator==(const Size&, const Size&) = default;
};
constexpr Offset lerp(Offset a, Offset b, float t) { return {lerp(a.x, b.x, t), lerp(a.y, b.y, t)}; }
constexpr Size lerp(Size a, Size b, float t) { return {lerp(a.width, b.width, t), lerp(a.height, b.height, t)}; }

struct Rect {
    float x = 0, y = 0, width = 0, height = 0;

    constexpr float right() const { return x + width; }
    constexpr float bottom() const { return y + height; }
    constexpr Offset center() const { return {x + width / 2, y + height / 2}; }
    constexpr bool contains(Offset p) const { return p.x >= x && p.y >= y && p.x < right() && p.y < bottom(); }
    constexpr Rect intersect(const Rect& o) const {
        float l = std::max(x, o.x), t = std::max(y, o.y);
        float r = std::min(right(), o.right()), b = std::min(bottom(), o.bottom());
        return {l, t, std::max(0.f, r - l), std::max(0.f, b - t)};
    }
    friend constexpr bool operator==(const Rect&, const Rect&) = default;
};

// The px in a dp, and how much bigger sp are than dp (Compose's Density; LocalDensity).
struct Density {
    float density = 1, fontScale = 1;
    constexpr float toPx(float dp) const { return dp * density; }
    constexpr float toDp(float px) const { return px / density; }
    // Compose's Dp.roundToPx: the nearest whole px (a half up); an infinite one as it is.
    float roundToPx(float dp) const {
        float px = dp * density;
        return std::isinf(px) ? px : std::floor(px + 0.5f);
    }
    constexpr float spToDp(float sp) const { return sp * fontScale; }
    friend constexpr bool operator==(const Density&, const Density&) = default;
};

// A font's weight, 1 to 1000 (Compose's FontWeight): Normal is 400, Medium 500, Bold 700.
// Text comes in the family's file nearest it, as CSS picks (FontWeight::closest), emboldened
// when that is too light for a bold weight (FontWeight::synthesizesBold).
// Whole-pixel positions and sizes (Compose's IntOffset, IntSize): an image's pixels. Each turns
// into its dp counterpart where one is wanted.
struct IntOffset {
    int x = 0, y = 0;
    constexpr operator Offset() const { return {float(x), float(y)}; }
    friend constexpr bool operator==(const IntOffset&, const IntOffset&) = default;
};
struct IntSize {
    int width = 0, height = 0;
    constexpr operator Size() const { return {float(width), float(height)}; }
    friend constexpr bool operator==(const IntSize&, const IntSize&) = default;
};

// How an image is sampled where it is drawn bigger or smaller than its pixels (Compose's
// FilterQuality): the nearest pixel (None), between the four nearest (Low, bilinear), from
// copies halved and halved again as small as it is drawn (Medium: mipmaps), or a cubic of the
// sixteen nearest (High: Mitchell's).
enum class FilterQuality { None, Low, Medium, High };
inline constexpr FilterQuality DefaultFilterQuality = FilterQuality::Low;

struct FontWeight {
    int weight = 400;
    static const FontWeight Thin, ExtraLight, Light, Normal, Medium, SemiBold, Bold, ExtraBold, Black;
    static const FontWeight W100, W200, W300, W400, W500, W600, W700, W800, W900;
    // Of the weights there are, the one CSS takes for `wanted` (its index; -1 if none): 400
    // tries 500 first and 500 tries 400, then lighter ones; lighter weights go lighter first,
    // heavier ones heavier.
    static int closest(const std::vector<FontWeight>& available, FontWeight wanted);
    // Whether a face of weight `actual` is emboldened for `wanted` (Android's rule: a weight of
    // 600 and up, 200 or more heavier than the face).
    static constexpr bool synthesizesBold(FontWeight wanted, FontWeight actual) {
        return wanted.weight >= 600 && wanted.weight - actual.weight >= 200;
    }
    friend constexpr auto operator<=>(const FontWeight&, const FontWeight&) = default;
};
inline constexpr FontWeight FontWeight::Thin{100};
inline constexpr FontWeight FontWeight::ExtraLight{200};
inline constexpr FontWeight FontWeight::Light{300};
inline constexpr FontWeight FontWeight::Normal{400};
inline constexpr FontWeight FontWeight::Medium{500};
inline constexpr FontWeight FontWeight::SemiBold{600};
inline constexpr FontWeight FontWeight::Bold{700};
inline constexpr FontWeight FontWeight::ExtraBold{800};
inline constexpr FontWeight FontWeight::Black{900};
inline constexpr FontWeight FontWeight::W100{100};
inline constexpr FontWeight FontWeight::W200{200};
inline constexpr FontWeight FontWeight::W300{300};
inline constexpr FontWeight FontWeight::W400{400};
inline constexpr FontWeight FontWeight::W500{500};
inline constexpr FontWeight FontWeight::W600{600};
inline constexpr FontWeight FontWeight::W700{700};
inline constexpr FontWeight FontWeight::W800{800};
inline constexpr FontWeight FontWeight::W900{900};

enum class FontStyle { Normal, Italic };  // italic is slanted by the backend if the font has no italic face

// A typeface. The generic families map to system fonts where the platform has them (and
// fall back to the bundled sans-serif); fromFiles() makes one from font files:
//   static const FontFamily Lora = FontFamily::fromFiles({{"fonts/Lora-Regular.ttf"},
//                                                         {"fonts/Lora-Bold.ttf", FontWeight::Bold}});
// A weight without a file of its own comes in the nearest one (bold ones emboldened); a missing
// italic is slanted. Characters a font lacks (symbols, emoji, CJK) come from system fallback
// fonts.
// A variable font's axis settings (Compose's FontVariation): which instance of it a FontFile is.
namespace FontVariation {
struct Setting {
    std::string axis;  // its four-letter tag: "wght", "wdth", "slnt", "ital", "opsz", "GRAD", or the font's own
    float value = 0;
    friend bool operator==(const Setting&, const Setting&) = default;
};
inline Setting weight(int weight) { return {"wght", float(weight)}; }  // 1..1000
inline Setting width(float percent) { return {"wdth", percent}; }      // 100: normal
inline Setting slant(float degrees) { return {"slnt", degrees}; }      // -90..90, leaning right below 0
inline Setting italic(float amount) { return {"ital", amount}; }       // 0..1
inline Setting grade(int grade) { return {"GRAD", float(grade)}; }
inline Setting opticalSizing(float sp) { return {"opsz", sp}; }
// A weight and a style as the wght and ital axes, then the others (Compose's
// FontVariation.Settings(weight, style, ...)).
std::vector<Setting> settings(FontWeight weight, FontStyle style, std::vector<Setting> more = {});
} // namespace FontVariation

struct FontFile {
    std::string path;  // an asset ("fonts/x.ttf") or an absolute path
    FontWeight weight = FontWeight::Normal;
    FontStyle style = FontStyle::Normal;
    // A variable font's instance: its axes set so (the others at their defaults). The same file
    // can come several times in a family, as several weights:
    //   FontFamily::fromFiles({{"fonts/Flex.ttf", FontWeight::Light, FontStyle::Normal, {FontVariation::weight(300)}},
    //                          {"fonts/Flex.ttf", FontWeight::Bold, FontStyle::Normal, {FontVariation::weight(700)}}});
    std::vector<FontVariation::Setting> variationSettings;
    friend bool operator==(const FontFile&, const FontFile&) = default;
};

struct FontFamily {
    uint16_t id = 0;
    static const FontFamily Default, SansSerif, Serif, Monospace;
    static FontFamily fromFiles(std::vector<FontFile> files);  // the same files give the same family
    static std::vector<FontFile> files(uint16_t id);           // empty for the generic families
    friend constexpr bool operator==(const FontFamily&, const FontFamily&) = default;
};
inline constexpr FontFamily FontFamily::Default{0};
inline constexpr FontFamily FontFamily::SansSerif{1};
inline constexpr FontFamily FontFamily::Serif{2};
inline constexpr FontFamily FontFamily::Monospace{3};

// OpenType feature settings (CSS font-feature-settings: "tnum", "smcp, frac", "\"liga\" 0"),
// kept by a number for Font: the same text gives the same number, 0 is none.
struct FontFeatures {
    static uint16_t id(const std::string& settings);
    static std::string settings(uint16_t id);
};

// How a paragraph's line height (Font::lineHeight) shares what it adds to a line or takes from
// it between the line's top and bottom, and whether the text's first top and last bottom get
// their share (Compose's LineHeightStyle; Default: proportional, both trimmed).
struct LineHeightStyle {
    // The part of it that goes above the line: Top 0 (all below), Center a half, Bottom all;
    // Proportional as the line's ascent and descent stand.
    struct Alignment {
        static constexpr float Top = 0, Center = 0.5f, Bottom = 1, Proportional = -1;
    };
    // FirstLineTop: nothing above the first line, LastLineBottom: nothing below the last.
    enum class Trim : uint8_t { Both, FirstLineTop, LastLineBottom, None };
    // Fixed: every line that tall; Minimum: never shorter than its font makes it.
    enum class Mode : uint8_t { Fixed, Minimum };
    float alignment = Alignment::Proportional;
    Trim trim = Trim::Both;
    Mode mode = Mode::Fixed;
    friend constexpr bool operator==(const LineHeightStyle&, const LineHeightStyle&) = default;
};

enum class StrokeCap { Butt, Round, Square };
enum class StrokeJoin { Miter, Round, Bevel };

struct Font {
    float size = 16;  // sp: scaled by the system font size (LocalDensity::fontScale)
    FontWeight weight = FontWeight::Normal;
    FontStyle style = FontStyle::Normal;
    uint16_t family = 0;  // FontFamily::id
    float letterSpacing = 0;  // sp added after every character (none in cursive scripts: Arabic, Devanagari…)
    float lineHeight = 0;     // sp from a line's baseline to the next's in a paragraph; 0: the font's own
    uint16_t features = 0;    // FontFeatures::id: what the shaper turns on and off
    LineHeightStyle lineHeightStyle;  // where lineHeight puts the room it adds or takes
    // Its glyphs' outlines drawn instead of filled (a text style's DrawStyle Stroke): strokeWidth
    // dp wide, centered on them (0: a hairline, one pixel), their corners joined (mitered up to
    // strokeMiter times the width); color glyphs (emoji) stay as they are.
    bool stroked = false;
    float strokeWidth = 0;
    StrokeJoin strokeJoin = StrokeJoin::Miter;
    float strokeMiter = 4;
    // Its glyphs stretched across (their advances too) and slanted: a point y above the baseline
    // moved y * skewX to the right (a text style's TextGeometricTransform).
    float scaleX = 1, skewX = 0;
    friend constexpr bool operator==(const Font&, const Font&) = default;
};

struct FontMetrics {
    float ascent = 0, descent = 0, lineHeight = 0;
};

// One step of a path: a move, a line, a quadratic, a conic (a quadratic whose control point is
// weighed by w: arcs and ovals, exactly), a cubic, or the close of a contour.
struct PathCommand {
    enum class Verb { Move, Line, Quad, Conic, Cubic, Close };
    Verb verb;
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0, x = 0, y = 0;  // control points (as needed) and end point
    float w = 1;                                         // a conic's weight
    friend constexpr bool operator==(const PathCommand&, const PathCommand&) = default;
};

class Brush;

// How a bitmap keeps its pixels (Compose's ImageBitmapConfig, Android's Bitmap.Config): 8 bits a
// channel (Argb8888; Gpu: the same, kept by the GPU), alpha alone (Alpha8: drawn in a paint's
// color as a mask), 5, 6 and 5 bits of red, green and blue and no alpha (Rgb565), half floats
// (F16: more than 8 bits, and past 0..1).
enum class ImageBitmapConfig { Argb8888, Alpha8, Rgb565, F16, Gpu };

struct PixelMap;

// A bitmap (Compose's ImageBitmap): decoded pixels (imageResource(), fromPixels()), or pixels to
// draw into (ImageBitmap(width, height, config), then graphics::Canvas(bitmap)). Copies are the
// same bitmap: what is drawn into one shows in all. Drawing into it is done by the platform's
// renderer (on the UI thread) as the canvas goes; without one (tests without a platform) what is
// drawn into it waits for one.
class ImageBitmap {
public:
    ImageBitmap() = default;
    // Its pixels clear (black and opaque where it has no alpha: Rgb565, or hasAlpha false, as
    // Android clears an opaque bitmap), `config` as it keeps them. Its color space is the one it
    // says it is in (Compose's colorSpace; another than a built-in one must outlive it); its
    // pixels are kept in sRGB, as they are shown, while what is drawn is drawn in sRGB.
    ImageBitmap(int width, int height, ImageBitmapConfig config = ImageBitmapConfig::Argb8888, bool hasAlpha = true,
                const ColorSpace& colorSpace = ColorSpaces::Srgb);
    static ImageBitmap fromPixels(int width, int height, std::vector<uint8_t> rgba);  // straight RGBA, Argb8888

    bool empty() const { return !data_; }
    int width() const;
    int height() const;
    ImageBitmapConfig config() const;
    bool hasAlpha() const;  // (Alpha8: yes; Rgb565: no)
    const ColorSpace& colorSpace() const;  // (sRGB unless made in another)
    // Its pixels as straight RGBA, 8 bits a channel (Alpha8's black; F16's kept to 0..1): what it
    // holds now (read back from the renderer where only it has them).
    const uint8_t* pixels() const;
    // `width` x `height` of them from (startX, startY) as ARGB (Compose's readPixels), into
    // `buffer` from `bufferOffset`, rows `stride` apart (unset: width); outside it, nothing.
    void readPixels(uint32_t* buffer, int startX = 0, int startY = 0, std::optional<int> width = {}, std::optional<int> height = {},
                    int bufferOffset = 0, std::optional<int> stride = {}) const;
    PixelMap toPixelMap(int startX = 0, int startY = 0, std::optional<int> width = {}, std::optional<int> height = {}) const;
    void prepareToDraw() const {}  // (Android's hint to upload it now: the renderer does when it is drawn)
    uint64_t id() const;
    uint64_t version() const;  // what it holds: another each time it is drawn into
    // Lets backends notice when the image is gone and free its texture.
    std::weak_ptr<const void> token() const { return data_; }

    friend bool operator==(const ImageBitmap& a, const ImageBitmap& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};

// Pixels read from a bitmap (Compose's PixelMap): (x, y) its color.
struct PixelMap {
    std::vector<uint32_t> buffer;  // ARGB
    int width = 0, height = 0, bufferOffset = 0, stride = 0;
    Color operator()(int x, int y) const;
};

// How an image (or a Painter) fills a space of another shape (Compose's ContentScale): all of
// it inside and as big as fits (Fit), covering it, cut at the sides (Crop), stretched
// (FillBounds), as it is unless too big (Inside), as it is (None), or the width or height
// filled.
enum class ContentScale { Fit, Crop, FillBounds, Inside, None, FillWidth, FillHeight };

// The size an image is wanted at, in pixels (0: any), as Coil's size and scale: a bigger image
// may be decoded smaller, by a power of two, but never below it — still filling it one way
// (fill = false, as a fitted image does) or both ways (fill = true, as a cropped one).
struct ImageRequestSize {
    int width = 0, height = 0;
    bool fill = false;
    bool any() const { return width <= 0 && height <= 0; }
    friend bool operator==(const ImageRequestSize&, const ImageRequestSize&) = default;
};
// How much a width x height image can be scaled down for `size`: 1, 2, 4, ... And at least
// so far that it is at most maxImageDimension pixels each way, as a texture can hold.
inline constexpr int maxImageDimension = 4096;
int imageSampleSize(int width, int height, const ImageRequestSize& size);
// Whether a width x height image is big enough for `size` (never for any size: that one
// wants the whole image).
bool imageCovers(int width, int height, const ImageRequestSize& size);
// The image 1/sample the size, each pixel the average of the ones it covers (weighted by
// their alpha): for decoders that can't scale down as they decode.
ImageBitmap scaleDown(const ImageBitmap& image, int sample);

// Each corner's radius as drawn (left and right on the screen, dp or px): a rounded rect whose
// corners differ. One radius converts to four equal ones.
struct CornerRadii {
    float topLeft = 0, topRight = 0, bottomRight = 0, bottomLeft = 0;
    constexpr CornerRadii() = default;
    constexpr CornerRadii(float all) : topLeft(all), topRight(all), bottomRight(all), bottomLeft(all) {}
    constexpr CornerRadii(float tl, float tr, float br, float bl) : topLeft(tl), topRight(tr), bottomRight(br), bottomLeft(bl) {}
    constexpr bool uniform() const { return topLeft == topRight && topRight == bottomRight && bottomRight == bottomLeft; }
    constexpr float max() const { return std::max(std::max(topLeft, topRight), std::max(bottomRight, bottomLeft)); }
    constexpr CornerRadii scaled(float k) const { return {topLeft * k, topRight * k, bottomRight * k, bottomLeft * k}; }
    constexpr CornerRadii grown(float d) const { return {topLeft + d, topRight + d, bottomRight + d, bottomLeft + d}; }
    friend constexpr bool operator==(const CornerRadii&, const CornerRadii&) = default;
};

// A corner's size: dp, plus a share of the shorter side (Compose's CornerSize(dp) / CornerSize(percent)).
struct CornerSize {
    float dp = 0, percent = 0;
    friend constexpr bool operator==(const CornerSize&, const CornerSize&) = default;
};
constexpr CornerSize CornerPercent(float percent) { return {0, percent}; }

class Path;
struct ShapeOutline;
struct Outline;

// Which way layouts go (see LocalLayoutDirection in layout.hpp).
enum class LayoutDirection { Ltr, Rtl };

// What backgrounds, borders, clips and shadows take the form of (Compose's Shape): in a given
// size and layout direction it makes an Outline (createOutline). RectangleShape, CircleShape,
// RoundedCornerShape(16), RoundedCornerShape(16, 16, 0, 0) for corners of their own,
// CutCornerShape(8) for corners cut straight, GenericShape(builder) for any path,
// OutlineShape(createOutline) for any outline, OutlineShape(points) for a polygon in a unit square.
struct Shape {
    enum class Kind { Rectangle, Rounded, Circle, Outline, Cut, Generic };
    Kind kind = Kind::Rectangle;
    float radius = 0;   // dp, for Rounded
    float percent = 0;  // and a share of the shorter side on top (0..50)
    // Corners of their own (`perCorner`; radius and percent are then unused): topStart, topEnd,
    // bottomEnd, bottomStart — the start is the right in right-to-left layouts.
    bool perCorner = false;
    std::array<CornerSize, 4> corners{};
    const ShapeOutline* outline = nullptr;  // Kind::Outline: a polygon in a unit square
    // Corners named left and right on the screen, not mirrored right to left (Compose's
    // AbsoluteRoundedCornerShape, AbsoluteCutCornerShape): `corners` are then topLeft, topRight,
    // bottomRight, bottomLeft.
    bool absolute = false;
    // Kind::Generic: what makes its outline in a size and direction (GenericShape, OutlineShape).
    std::shared_ptr<const std::function<Outline(Size, LayoutDirection)>> generic;
    // Corners that move (a morphing shape's, as a pressed button's): what they are now, read in
    // place of `corners` where the outline is made (laid out, drawn: those follow them).
    std::shared_ptr<const std::function<std::array<CornerSize, 4>()>> animatedCorners;

    // Its outline in `size` (dp, from the top left), mirrored right to left where it has a start
    // and an end (Compose's createOutline).
    Outline createOutline(Size size, LayoutDirection layoutDirection) const;
    // A rounded rect's corners for a rect, or a polygon's or path's (not one: those of a shape
    // drawn as a path).
    bool pathed() const { return kind == Kind::Outline || kind == Kind::Cut || kind == Kind::Generic; }
    float radiusFor(const Rect& r) const {  // one radius (corners of their own: the largest)
        float shorter = std::min(r.width, r.height);
        return kind == Kind::Circle    ? shorter / 2
               : kind == Kind::Cut     ? 0
               : perCorner             ? radiiFor(r).max()
               : kind == Kind::Rounded ? std::min(radius + percent / 100 * shorter, shorter / 2)
                                       : 0;
    }
    // Each corner's radius in `r`. As in Compose, a side's two corners together fit in the
    // shorter side (both shrink if they wouldn't).
    CornerRadii radiiFor(const Rect& r, bool rtl = false) const {
        if (!perCorner || kind != Kind::Rounded) return CornerRadii(radiusFor(r));
        return cornersFor(r, rtl && !absolute);
    }
    // Its own corners (`corners`) in `r`, a side's two fitting in the shorter side; right to left,
    // the start ones on the right.
    CornerRadii cornersFor(const Rect& r, bool rtl) const {
        float shorter = std::min(r.width, r.height);
        auto px = [shorter](CornerSize c) { return std::max(0.f, c.dp + c.percent / 100 * shorter); };
        std::array<CornerSize, 4> at = animatedCorners ? (*animatedCorners)() : corners;
        float ts = px(at[0]), te = px(at[1]), be = px(at[2]), bs = px(at[3]);
        if (ts + bs > shorter) {
            float k = shorter / (ts + bs);
            ts *= k, bs *= k;
        }
        if (te + be > shorter) {
            float k = shorter / (te + be);
            te *= k, be *= k;
        }
        return rtl ? CornerRadii{te, ts, bs, be} : CornerRadii{ts, te, be, bs};
    }
    friend bool operator==(const Shape&, const Shape&) = default;
};
inline const Shape RectangleShape{Shape::Kind::Rectangle, 0};
inline const Shape CircleShape{Shape::Kind::Circle, 0};
inline Shape RoundedCornerShape(float radius) { return {Shape::Kind::Rounded, radius}; }
inline Shape RoundedCornerShape(float topStart, float topEnd, float bottomEnd, float bottomStart) {
    return {Shape::Kind::Rounded, 0, 0, true, {CornerSize{topStart}, CornerSize{topEnd}, CornerSize{bottomEnd}, CornerSize{bottomStart}}};
}
inline Shape RoundedCornerShape(CornerSize topStart, CornerSize topEnd, CornerSize bottomEnd, CornerSize bottomStart) {
    return {Shape::Kind::Rounded, 0, 0, true, {topStart, topEnd, bottomEnd, bottomStart}};
}
// Corners `percent` of the shorter side (Compose's RoundedCornerShape(percent = 50) is a pill).
inline Shape RoundedCornerShapePercent(float percent) { return {Shape::Kind::Rounded, 0, percent}; }
// Rounded corners named left and right, the same right to left (Compose's AbsoluteRoundedCornerShape).
inline Shape AbsoluteRoundedCornerShape(CornerSize topLeft, CornerSize topRight, CornerSize bottomRight, CornerSize bottomLeft) {
    Shape s{Shape::Kind::Rounded, 0, 0, true, {topLeft, topRight, bottomRight, bottomLeft}};
    s.absolute = true;
    return s;
}
inline Shape AbsoluteRoundedCornerShape(float topLeft, float topRight, float bottomRight, float bottomLeft) {
    return AbsoluteRoundedCornerShape(CornerSize{topLeft}, CornerSize{topRight}, CornerSize{bottomRight}, CornerSize{bottomLeft});
}
inline Shape AbsoluteRoundedCornerShape(float all) { return AbsoluteRoundedCornerShape(all, all, all, all); }
// Corners cut straight across, as far along each side as the size (Compose's CutCornerShape): one
// size, a share of the shorter side, or each its own (topStart, topEnd, bottomEnd, bottomStart).
inline Shape CutCornerShape(CornerSize topStart, CornerSize topEnd, CornerSize bottomEnd, CornerSize bottomStart) {
    return {Shape::Kind::Cut, 0, 0, true, {topStart, topEnd, bottomEnd, bottomStart}};
}
inline Shape CutCornerShape(float topStart, float topEnd, float bottomEnd, float bottomStart) {
    return CutCornerShape(CornerSize{topStart}, CornerSize{topEnd}, CornerSize{bottomEnd}, CornerSize{bottomStart});
}
inline Shape CutCornerShape(float all) { return CutCornerShape(all, all, all, all); }
inline Shape CutCornerShapePercent(float percent) {
    return CutCornerShape(CornerPercent(percent), CornerPercent(percent), CornerPercent(percent), CornerPercent(percent));
}
// The same with corners named left and right (Compose's AbsoluteCutCornerShape).
inline Shape AbsoluteCutCornerShape(CornerSize topLeft, CornerSize topRight, CornerSize bottomRight, CornerSize bottomLeft) {
    Shape s = CutCornerShape(topLeft, topRight, bottomRight, bottomLeft);
    s.absolute = true;
    return s;
}
inline Shape AbsoluteCutCornerShape(float topLeft, float topRight, float bottomRight, float bottomLeft) {
    return AbsoluteCutCornerShape(CornerSize{topLeft}, CornerSize{topRight}, CornerSize{bottomRight}, CornerSize{bottomLeft});
}
// A polygon in a unit square (0..1 both ways), stretched over the bounds it fills: points around
// it, fine enough to look smooth there (Material's shapes are made of these). Keep it alive (a
// static) while shapes refer to it.
struct ShapeOutline {
    std::vector<Offset> points;
};
inline Shape OutlineShape(const ShapeOutline& outline) {
    Shape s{Shape::Kind::Outline};
    s.outline = &outline;
    return s;
}

// The shape `t` of the way from `a` to `b` (corners that morph): radii and shares blended,
// corner by corner when either has corners of its own. Outlines switch half way.
inline Shape lerp(const Shape& a, const Shape& b, float t) {
    if (a.pathed() || b.pathed() || a.absolute != b.absolute) return t < 0.5f ? a : b;
    auto rounded = [](const Shape& s) {
        return s.kind == Shape::Kind::Circle ? Shape{Shape::Kind::Rounded, 0, 50} : s.kind == Shape::Kind::Rounded ? s : Shape{Shape::Kind::Rounded, 0, 0};
    };
    Shape x = rounded(a), y = rounded(b);
    if (!x.perCorner && !y.perCorner)
        return {Shape::Kind::Rounded, x.radius + (y.radius - x.radius) * t, x.percent + (y.percent - x.percent) * t};
    auto corner = [](const Shape& s, size_t i) { return s.perCorner ? s.corners[i] : CornerSize{s.radius, s.percent}; };
    Shape out{Shape::Kind::Rounded, 0, 0, true};
    for (size_t i = 0; i < 4; ++i) {
        CornerSize p = corner(x, i), q = corner(y, i);
        out.corners[i] = {p.dp + (q.dp - p.dp) * t, p.percent + (q.percent - p.percent) * t};
    }
    return out;
}

// A color, or a gradient across an area (Compose's Brush). Gradient coordinates are local to
// the area being painted; kInfinity in an end point means "the area's far edge".
struct ColorStop {
    float offset = 0;  // 0..1
    Color color;
    friend constexpr bool operator==(const ColorStop&, const ColorStop&) = default;
};

// How a gradient goes on past its ends (Compose's TileMode): its end colors (Clamp), over again
// (Repeated), back and forth (Mirror), or not at all (Decal: transparent there).
enum class TileMode { Clamp, Repeated, Mirror, Decal };

class ColorFilter;
class Shader;
class Matrix;
enum class BlendMode;

// What paints: a color, a gradient, or a shader (Compose's Brush: SolidColor, the gradients, a
// ShaderBrush of your own, Brush.composite), its gradient or shader through a transform if it has
// one. A gradient spreads over the drawing it paints (its area); a shader is made for that size.
class Brush {
public:
    enum class Kind { Solid, Linear, Radial, Sweep, Shader };

    static Brush solid(Color color) { return Brush(Kind::Solid, {{0, color}}); }
    static Brush linearGradient(std::vector<Color> colors, Offset start = {0, 0}, Offset end = {kInfinity, kInfinity},
                                TileMode tileMode = TileMode::Clamp);
    static Brush linearGradient(std::vector<ColorStop> stops, Offset start, Offset end, TileMode tileMode = TileMode::Clamp);
    static Brush horizontalGradient(std::vector<Color> colors, float startX = 0, float endX = kInfinity, TileMode tileMode = TileMode::Clamp);
    static Brush verticalGradient(std::vector<Color> colors, float startY = 0, float endY = kInfinity, TileMode tileMode = TileMode::Clamp);
    // Defaults: centered, reaching the nearer edge.
    static Brush radialGradient(std::vector<Color> colors, std::optional<Offset> center = {}, float radius = kInfinity,
                                TileMode tileMode = TileMode::Clamp);
    static Brush radialGradient(std::vector<ColorStop> stops, std::optional<Offset> center = {}, float radius = kInfinity,
                                TileMode tileMode = TileMode::Clamp);
    // Clockwise from 3 o'clock.
    static Brush sweepGradient(std::vector<Color> colors, std::optional<Offset> center = {});
    static Brush sweepGradient(std::vector<ColorStop> stops, std::optional<Offset> center = {});
    // A brush of your own (Compose's ShaderBrush): `createShader` makes its shader for the size of
    // what it paints (in its dp, from its top left), once for each size; its intrinsic size, if
    // it has one. Equal only to itself (and its copies).
    static Brush shader(std::function<Shader(Size size)> createShader, std::optional<Size> intrinsicSize = {});
    static Brush shader(Shader shader);  // the same shader whatever the size (Compose's ShaderBrush(shader)); equal by it
    // `src` composited onto `dst` by `blendMode`, pixel by pixel (Compose's Brush.composite: each
    // made a shader, a color as a gradient of one color).
    static Brush composite(const Brush& dst, const Brush& src, BlendMode blendMode);

    // Each of its colors `alpha` times as opaque (as a paint's alpha fades what it paints).
    Brush faded(float alpha) const;
    // This brush with its gradient or shader through `transform` (Compose's ShaderBrush.transform:
    // what it paints at p, it paints at transform(p), in the dp of what it paints from its top
    // left; nothing for a color). Unset: none.
    Brush withTransform(const std::optional<Matrix>& transform) const;
    const Matrix* transform() const { return transform_.get(); }
    float shaderAlpha() const { return shaderAlpha_; }  // a shader brush faded: how opaque what it paints is
    // Its size where it has one (Compose's intrinsicSize): a linear gradient's from its start to
    // its end where those are given (NaN across or down where not), a radial one's its
    // diameter; NaN both ways for the others.
    Size intrinsicSize() const;
    // The shader it paints `size` with (Compose's ShaderBrush.createShader), through its transform;
    // a color's: a gradient of that color.
    Shader createShader(Size size) const;

    Kind kind() const { return kind_; }
    bool isSolid() const { return kind_ == Kind::Solid; }
    const std::vector<ColorStop>& stops() const { return stops_; }
    TileMode tileMode() const { return tileMode_; }
    // The color at absolute point `p` when painting `area` (absolute), through its transform.
    Color colorAt(Offset p, const Rect& area) const;
    // Where `p` is along the gradient before its tile mode: 0 at its start, 1 at its end (a
    // sweep's: 0..1 round its center).
    float parameterAt(Offset p, const Rect& area) const;
    // Where it lies when painting `area` (absolute), the far edges and the default centers
    // resolved: a linear one from `start` to `end`; a radial one round `start` out to `radius`; a
    // sweep round `start` (what a backend draws it from).
    struct Geometry {
        Offset start, end;
        float radius = 0;
    };
    Geometry geometry(const Rect& area) const;
    // The color that far along it, its tile mode applied.
    Color colorAtParameter(float t) const;

    friend bool operator==(const Brush& a, const Brush& b);
    struct ShaderSource;  // a shader brush's: its maker, or the two it composites
    friend Brush lerp(const Brush& a, const Brush& b, float t);

private:
    friend class ColorFilter;  // a filter recolors its stops
    Brush(Kind kind, std::vector<ColorStop> stops) : kind_(kind), stops_(std::move(stops)) {}
    static std::vector<ColorStop> evenly(const std::vector<Color>& colors);
    Kind kind_;
    std::vector<ColorStop> stops_;
    Offset start_, end_;  // linear: endpoints; radial/sweep: start_ is the center
    bool hasCenter_ = false;
    float radius_ = kInfinity;
    TileMode tileMode_ = TileMode::Clamp;
    bool explicitStops_ = true;  // its stops given, not its colors spread evenly (how two are interpolated)
    float shaderAlpha_ = 1;      // a shader brush's paint alpha (a gradient's goes into its stops)
    std::shared_ptr<const ShaderSource> shader_;
    std::shared_ptr<const Matrix> transform_;
    Color unshaded(Offset p, const Rect& area) const;  // a gradient's color there, no transform
};
// Between two brushes (Compose's Interpolatable lerp of Brush): two gradients of a kind mix their
// colors, stops and geometry (a tile mode switching halfway); a color with a gradient, as that
// gradient in that color; colors mix; anything else switches halfway.
Brush lerp(const Brush& a, const Brush& b, float t);

// A border's width and paint (Compose's BorderStroke): a color or a brush, for Modifier::border
// (a gradient border round a card).
struct BorderStroke {
    float width = 1;
    Color color = colors::Black;
    std::optional<Brush> brush;  // set: paints it instead of the color
    BorderStroke(float w, Color c) : width(w), color(c) {}
    BorderStroke(float w, Brush b) : width(w), brush(std::move(b)) {}
    friend bool operator==(const BorderStroke&, const BorderStroke&) = default;
};

// How a drawing (the source) goes onto what is there (the destination), Compose's BlendMode: the
// Porter-Duff ones, Modulate and Plus, the separable ones and the non-separable ones (hue,
// saturation, color, luminosity), as W3C's compositing spec and Skia define them. Also how a
// ColorFilter::tint's color goes with the pixels it paints over.
enum class BlendMode { Src, SrcOver, SrcIn, SrcAtop, DstIn, Modulate, Multiply, Screen, Darken, Lighten, Plus,
                       Clear, Dst, DstOver, SrcOut, DstOut, DstAtop, Xor, Overlay, ColorDodge, ColorBurn, Hardlight,
                       Softlight, Difference, Exclusion, Hue, Saturation, Color, Luminosity };

// `source` drawn onto `destination` with `mode` (both straight colors, as is the result).
Color blend(Color source, Color destination, BlendMode mode);

// What paints a shape pixel by pixel (Compose's Shader): a gradient, an image (tiled), or two
// shaders composited; in the dp of what it paints (an image's pixel a dp), through a transform
// if it has one. Made by LinearGradientShader, RadialGradientShader, SweepGradientShader,
// ImageShader and CompositeShader; painted by Brush::shader.
class Shader {
public:
    enum class Kind { Linear, Radial, Sweep, Image, Composite };
    Kind kind() const;
    // A gradient's stops and tile mode; where it lies: a linear one from `start` to `end`, a
    // radial one round `start` out to `radius`, a sweep round `start`.
    const std::vector<ColorStop>& stops() const;
    TileMode tileMode() const;  // (an image's across)
    TileMode tileModeY() const;  // an image's down
    Offset start() const;
    Offset end() const;
    float radius() const;
    const ImageBitmap& image() const;
    // A composite's two (Compose's dst and src) and how the second goes onto the first.
    const Shader& dst() const;
    const Shader& src() const;
    BlendMode blendMode() const;
    // Through `matrix` (what it painted at p it paints at matrix(p)), after any it has.
    Shader transformed(const Matrix& matrix) const;
    const Matrix* transform() const;
    // Its color at `p` (straight): an image's bilinearly from its pixels, as Compose's default
    // sampling does.
    Color colorAt(Offset p) const;
    friend bool operator==(const Shader& a, const Shader& b);

    struct Data;
    std::shared_ptr<const Data> data_;
};
// A linear gradient from `from` to `to`: `colors` evenly, or at `colorStops` (each 0..1) where given.
Shader LinearGradientShader(Offset from, Offset to, std::vector<Color> colors, std::vector<float> colorStops = {},
                            TileMode tileMode = TileMode::Clamp);
Shader RadialGradientShader(Offset center, float radius, std::vector<Color> colors, std::vector<float> colorStops = {},
                            TileMode tileMode = TileMode::Clamp);
Shader SweepGradientShader(Offset center, std::vector<Color> colors, std::vector<float> colorStops = {});  // clockwise from 3 o'clock
// `image` from its top left, past its edges as the tile modes say, across and down.
Shader ImageShader(ImageBitmap image, TileMode tileModeX = TileMode::Clamp, TileMode tileModeY = TileMode::Clamp);
// `src` onto `dst` by `blendMode`, pixel by pixel.
Shader CompositeShader(Shader dst, Shader src, BlendMode blendMode);

// How Vertices make triangles (Compose's VertexMode): each three in turn, each three running
// along a strip (each one with the two before it), or a fan round the first.
enum class VertexMode { Triangles, TriangleStrip, TriangleFan };

// A triangle mesh for Canvas::drawVertices (Compose's Vertices): where each vertex is, where it
// samples the paint's shader (its texture coordinate) and its color, in the order `indices` give
// (none: as they come). Compose throws where the lengths don't match or an index is out of range;
// here texture coordinates or colors that don't match the positions are left out (the shader
// sampled where each pixel is; the paint as it is), and a triangle with an index out of range is.
class Vertices {
public:
    Vertices(VertexMode vertexMode, std::vector<Offset> positions, std::vector<Offset> textureCoordinates, std::vector<Color> colors,
             std::vector<int> indices = {});
    // As arrays (Compose's other constructor): x, y pairs, and colors as ARGB.
    Vertices(VertexMode vertexMode, const std::vector<float>& positions, const std::vector<float>& textureCoordinates,
             const std::vector<uint32_t>& colors, const std::vector<int16_t>& indices = {});

    VertexMode vertexMode() const;
    const std::vector<Offset>& positions() const;
    const std::vector<Offset>& textureCoordinates() const;  // (empty if left out)
    const std::vector<Color>& colors() const;               // (empty if left out)
    const std::vector<int>& indices() const;
    // Its triangles, each three indices into positions, in the order they are drawn.
    std::vector<std::array<int, 3>> triangles() const;
    Rect bounds() const;  // its positions'
    friend bool operator==(const Vertices& a, const Vertices& b);

    struct Data;
    std::shared_ptr<const Data> data_;
};

// A 4x5 matrix over a color's red, green, blue and alpha (0..1), the fifth column added after
// (Compose's ColorMatrix, row by row).
struct ColorMatrix {
    std::array<float, 20> values{1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 1, 0};
    void reset() { *this = {}; }
    void setToSaturation(float saturation);  // 0: gray, 1: as it is
    void setToScale(float red, float green, float blue, float alpha);
    ColorMatrix& timesAssign(const ColorMatrix& other);  // this, after `other`'s change
    Color apply(Color c) const;
    friend bool operator==(const ColorMatrix&, const ColorMatrix&) = default;
};

// What a drawn image's pixels turn into (Compose's ColorFilter): a color blended over them
// (tint: SrcIn keeps their shape in that color), a color matrix (gray, sepia), or a light
// that multiplies and adds.
class ColorFilter {
public:
    static ColorFilter tint(Color color, BlendMode blendMode = BlendMode::SrcIn);
    static ColorFilter colorMatrix(const ColorMatrix& matrix);
    static ColorFilter lighting(Color multiply, Color add);
    Color apply(Color pixel) const;
    // A brush filtered: each of its colors (a gradient's stops) through it.
    Brush apply(const Brush& brush) const;
    // A filtered copy of the image (its own id), made once for each image and filter and kept
    // while the image lives (the last few).
    ImageBitmap apply(const ImageBitmap& image) const;
    friend bool operator==(const ColorFilter&, const ColorFilter&) = default;

    // What it is, for a backend that applies it per pixel: a tint's color() and blendMode(), a
    // matrix(), a lighting's color() (multiply) and add().
    enum class Kind { Tint, Matrix, Lighting };
    Kind kind() const { return kind_; }
    Color color() const { return color_; }
    Color add() const { return add_; }
    BlendMode blendMode() const { return mode_; }
    const ColorMatrix& matrix() const { return matrix_; }

private:
    Kind kind_ = Kind::Tint;
    Color color_, add_;
    BlendMode mode_ = BlendMode::SrcIn;
    ColorMatrix matrix_;
};

// What a layer's pixels go through as it is composited (Compose's RenderEffect, set on a
// graphicsLayer): steps done one after another, each on what the one before it made, the
// innermost (the input) first. BlurEffect blurs them by a radius across and one down in dp, as
// Android does (a Gaussian of sigma 0.57735 r + 0.5 px), the pixels past the edges of what it
// blurs taken as `edgeTreatment` says (Clamp: the edge's), and spreads them out three sigmas past
// those edges; OffsetEffect moves them.
class RenderEffect {
public:
    // A progressive blur's: how far it blurs each pixel (BlurRadiusSpec::createRenderEffect).
    // Along a line from `start` to `end`, or (radial) out from `start` to `radius`, dp from the
    // layer's top left; the radius there (dp) between `stops` (fraction along it, radius).
    struct Progressive {
        bool radial = false;
        Offset start, end;
        float radius = 0;
        std::vector<std::pair<float, float>> stops;
        // Or a mask (BlurRadiusSpec::shader): each pixel's radius the widest stop's times the
        // shader's alpha there (dp from the layer's top left), kept to 0..1.
        std::optional<Shader> shader;
        friend bool operator==(const Progressive&, const Progressive&) = default;
    };
    struct Step {
        enum class Kind : uint8_t { Blur, Offset, ProgressiveBlur };
        Kind kind = Kind::Blur;
        float radiusX = 0, radiusY = 0;  // Blur
        TileMode edgeTreatment = TileMode::Clamp;  // Blur; ProgressiveBlur: Decal or not
        Offset offset;  // Offset
        std::optional<Progressive> progressive;  // ProgressiveBlur
        friend bool operator==(const Step&, const Step&) = default;
    };
    const std::vector<Step>& steps() const { return steps_; }
    // Where what it makes of content within `bounds` may reach (dp; a little further).
    Rect outputBounds(const Rect& bounds) const;
    friend bool operator==(const RenderEffect&, const RenderEffect&) = default;

private:
    friend RenderEffect BlurEffect(const std::optional<RenderEffect>&, float, float, TileMode);
    friend RenderEffect OffsetEffect(const std::optional<RenderEffect>&, Offset);
    friend class BlurRadiusSpec;
    std::vector<Step> steps_;
};
// `renderEffect` blurred (none: the layer's pixels). A radius of 0 leaves that direction as it is.
RenderEffect BlurEffect(const std::optional<RenderEffect>& renderEffect, float radiusX, float radiusY,
                        TileMode edgeTreatment = TileMode::Clamp);
inline RenderEffect BlurEffect(const std::optional<RenderEffect>& renderEffect, float radius) {
    return BlurEffect(renderEffect, radius, radius);
}
inline RenderEffect BlurEffect(float radiusX, float radiusY, TileMode edgeTreatment = TileMode::Clamp) {
    return BlurEffect(std::nullopt, radiusX, radiusY, edgeTreatment);
}
// `renderEffect` moved by `offset` dp.
RenderEffect OffsetEffect(const std::optional<RenderEffect>& renderEffect, Offset offset);
inline RenderEffect OffsetEffect(float offsetX, float offsetY) { return OffsetEffect(std::nullopt, {offsetX, offsetY}); }

// A radius along a progressive blur (Compose's BlurStop): `fraction` of the way along it (0..1),
// `radius` dp.
struct BlurStop {
    float fraction = 0, radius = 0;
    friend bool operator==(const BlurStop&, const BlurStop&) = default;
};

// How much a blur blurs where (Compose's BlurRadiusSpec, 1.13): the same everywhere (uniform: a
// BlurEffect), or progressively: from one radius to another (or through stops, sorted by their
// fractions) down, across, along a line, or out from a center. Lengths are dp from the layer's top
// left. A progressive one blurs as Compose's does (its own Gaussian of sigma r / 2 px, out to r
// px, no more than 150 px), across then down, each pixel by its radius. Radii below 0 are taken
// as 0, fractions kept to 0..1, a fall-off radius that isn't positive as unset.
class BlurRadiusSpec {
public:
    static BlurRadiusSpec uniform(float radius);
    static BlurRadiusSpec verticalGradient(float startRadius, float endRadius);  // top to bottom
    static BlurRadiusSpec verticalGradient(std::vector<BlurStop> stops);
    static BlurRadiusSpec horizontalGradient(float startRadius, float endRadius);  // left to right
    static BlurRadiusSpec horizontalGradient(std::vector<BlurStop> stops);
    static BlurRadiusSpec linearGradient(Offset start, Offset end, float startRadius, float endRadius);
    static BlurRadiusSpec linearGradient(Offset start, Offset end, std::vector<BlurStop> stops);
    // From `center` (unset: the middle) out to `fallOffRadius` (unset: half the shorter side).
    static BlurRadiusSpec radialGradient(float startRadius, float endRadius, std::optional<Offset> center = {},
                                         std::optional<float> fallOffRadius = {});
    static BlurRadiusSpec radialGradient(std::vector<BlurStop> stops, std::optional<Offset> center = {},
                                         std::optional<float> fallOffRadius = {});
    // Each pixel by `maxRadius` times the alpha of the shader `block` makes for the layer (its
    // size in dp, and the density), in its dp from its top left (Compose's BlurRadiusSpec.shader;
    // the shader made each time the effect is). Equal only to itself.
    static BlurRadiusSpec shader(float maxRadius, std::function<Shader(Density density, Size size)> block);
    // The effect for a layer of `size` (dp), the pixels past its edges as `edgeTreatment` says
    // (Decal: none; any other: the edges', as Compose's takes them).
    RenderEffect createRenderEffect(Size size, TileMode edgeTreatment = TileMode::Clamp, Density density = {}) const;
    friend bool operator==(const BlurRadiusSpec&, const BlurRadiusSpec&) = default;

private:
    enum class Kind : uint8_t { Uniform, Vertical, Horizontal, Linear, Radial, Shader };
    BlurRadiusSpec(Kind kind, std::vector<BlurStop> stops);
    Kind kind_;
    std::vector<BlurStop> stops_;  // (uniform: its radius, the one; a shader's: its maximum)
    Offset start_, end_;
    std::optional<Offset> center_;
    std::optional<float> fallOff_;
    std::shared_ptr<const std::function<Shader(Density, Size)>> shaderBlock_;
};

// How Path::op combines two shapes (Compose's PathOperation): what is in either, in both, in the
// first but not the second (or the other way round), or in exactly one.
enum class PathOperation { Difference, Intersect, Union, Xor, ReverseDifference };

// A shape built from lines and curves (Compose's Path), drawn with DrawScope::drawPath.
// A corner's radius across and down (Compose's CornerRadius): a quarter ellipse, a circle's if
// the two are equal.
struct CornerRadius {
    float x = 0, y = 0;
    constexpr CornerRadius() = default;
    constexpr CornerRadius(float radius) : x(radius), y(radius) {}
    constexpr CornerRadius(float x, float y) : x(x), y(y) {}
    friend constexpr bool operator==(const CornerRadius&, const CornerRadius&) = default;
};

// A rect with corners of their own (Compose's RoundRect). A side's two corners that don't fit
// in it shrink together (all four by the same factor, as Android draws one).
struct RoundRect {
    Rect rect;
    CornerRadius topLeft, topRight, bottomRight, bottomLeft;
    // Each corner a quarter circle (as drawn: after they are fitted), and their radii.
    bool circular() const;
    CornerRadii radii() const;
    // The corners shrunk to fit.
    RoundRect fitted() const;
    friend constexpr bool operator==(const RoundRect&, const RoundRect&) = default;
};

class Matrix;
class PathIterator;
// How a path's iterator gives its conics (PathIterator::ConicEvaluation).
enum class ConicEvaluation { AsConic, AsQuadratics };

// As Compose's (an Android Path, which is Skia's SkPath): ovals, round rects and arcs are made of
// conics (quarter circles exactly), a shape starts where Skia's does and goes round as asked,
// drawing after close() starts a new contour from the last one's start.
class Path {
public:
    enum class FillType { NonZero, EvenOdd };
    // Which way a shape added whole goes round (Compose's Path.Direction): its winding.
    enum class Direction { CounterClockwise, Clockwise };
    FillType fillType = FillType::NonZero;

    void moveTo(float x, float y);  // (one right after another takes its place)
    void lineTo(float x, float y);
    // From the last point (after close(), from the contour's start).
    void relativeMoveTo(float dx, float dy) { moveTo(current_.x + dx, current_.y + dy); }
    void relativeLineTo(float dx, float dy) { lineTo(current_.x + dx, current_.y + dy); }
    void quadraticTo(float x1, float y1, float x2, float y2);
    // A conic to (x2, y2), its control point (x1, y1) weighed by w (Skia's conicTo: 1 is a
    // quadratic, cos(θ / 2) a circle's arc of θ; 0 or less a line).
    void conicTo(float x1, float y1, float x2, float y2, float w);
    void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3);
    void relativeQuadraticTo(float dx1, float dy1, float dx2, float dy2) {
        quadraticTo(current_.x + dx1, current_.y + dy1, current_.x + dx2, current_.y + dy2);
    }
    void relativeConicTo(float dx1, float dy1, float dx2, float dy2, float w) {
        conicTo(current_.x + dx1, current_.y + dy1, current_.x + dx2, current_.y + dy2, w);
    }
    void relativeCubicTo(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3) {
        cubicTo(current_.x + dx1, current_.y + dy1, current_.x + dx2, current_.y + dy2, current_.x + dx3, current_.y + dy3);
    }
    // Part of the oval in `oval` (Compose's arcTo): from `startAngleDegrees` (0 at the right,
    // clockwise) through `sweepAngleDegrees` (a turn at most), joined to what is there by a line
    // unless forceMoveTo (or it is the first thing) starts it on its own; as conics.
    void arcTo(const Rect& oval, float startAngleDegrees, float sweepAngleDegrees, bool forceMoveTo);
    void arcToRad(const Rect& oval, float startAngleRadians, float sweepAngleRadians, bool forceMoveTo);
    // A contour of its own (a turn or more from a quarter's angle: the whole oval).
    void addArc(const Rect& oval, float startAngleDegrees, float sweepAngleDegrees);
    void addArcRad(const Rect& oval, float startAngleRadians, float sweepAngleRadians);
    void close();
    // Whole shapes as contours of their own, going round as `direction` says: a rect from its
    // top left, an oval from its right, a round rect from where its bottom left corner meets
    // its left side (Skia's starts).
    void addRect(const Rect& r, Direction direction = Direction::CounterClockwise);
    void addOval(const Rect& r, Direction direction = Direction::CounterClockwise);
    void addRoundRect(const Rect& r, float radius);  // (counterclockwise, corners of one radius)
    void addRoundRect(const RoundRect& r, Direction direction = Direction::CounterClockwise);  // its corners quarter ellipses
    void addPath(const Path& path, Offset offset = {});  // its contours, moved by `offset`
    void translate(Offset offset);                       // every point moved
    // Every point through `matrix`'s plane (as Skia's SkPath::transform): with a perspective,
    // quadratics and conics become conics (exactly), cubics are split, what passes behind the
    // eye is cut off.
    void transform(const Matrix& matrix);
    void reset();
    void rewind() { reset(); }  // (Compose's: emptied, its memory kept)
    // A path of these commands as they are (a vector's).
    static Path fromCommands(std::vector<PathCommand> commands, FillType fillType = FillType::NonZero);

    bool isEmpty() const { return commands_.empty(); }
    // One contour that turns one way all round (Skia's: lines and points are convex too).
    bool isConvex() const;
    Rect bounds() const;  // of all points, control points included
    const std::vector<PathCommand>& commands() const { return commands_; }
    // Its segments (Compose's Path.iterator()): conics as conics, or as quadratics within
    // `tolerance` dp of them.
    PathIterator iterator() const;
    PathIterator iterator(ConicEvaluation conicEvaluation, float tolerance = 0.25f) const;
    // Whether a point is inside, by its fill type, its curves exact (Skia's contains: open
    // contours count as closed; a point on the outline is inside).
    bool contains(Offset p) const;
    // This path becomes `a` combined with `b` (each by its fill type): closed contours with what
    // is inside on their left, made of the pieces of a's and b's segments that bound the result
    // (a curve's piece the same curve); false if nothing is left.
    bool op(const Path& a, const Path& b, PathOperation operation);
    static Path combine(PathOperation operation, const Path& a, const Path& b);
    friend Path operator+(const Path& a, const Path& b) { return combine(PathOperation::Union, a, b); }
    friend Path operator-(const Path& a, const Path& b) { return combine(PathOperation::Difference, a, b); }
    friend Path operator|(const Path& a, const Path& b) { return combine(PathOperation::Union, a, b); }
    friend Path operator&(const Path& a, const Path& b) { return combine(PathOperation::Intersect, a, b); }
    friend Path operator^(const Path& a, const Path& b) { return combine(PathOperation::Xor, a, b); }
    friend bool operator==(const Path& a, const Path& b);

private:
    void ensureMove();  // a move to the last start after close() (or to the origin), before drawing on
    std::vector<PathCommand> commands_;
    Offset current_, start_;
};

// One segment of a path as its iterator gives it (Compose's PathSegment): its points, the
// point it starts from first (a move's: its point; a close's: none), and a conic's weight.
struct PathSegment {
    enum class Type { Move, Line, Quadratic, Conic, Cubic, Close, Done };
    Type type = Type::Done;
    std::vector<Offset> points;
    float weight = 0;
    friend bool operator==(const PathSegment&, const PathSegment&) = default;
};

// A path's segments in order (Compose's PathIterator): conics as they are (AsConic), or each as
// quadratics within `tolerance` dp of it (AsQuadratics, the default: as Skia approximates them).
//   for (PathIterator it = path.iterator(); it.hasNext();) { PathSegment s = it.next(); ... }
class PathIterator {
public:
    using ConicEvaluation = kompoz::ConicEvaluation;
    explicit PathIterator(const Path& path, ConicEvaluation conicEvaluation = ConicEvaluation::AsQuadratics, float tolerance = 0.25f);
    const Path& path() const { return path_; }
    ConicEvaluation conicEvaluation() const { return evaluation_; }
    float tolerance() const { return tolerance_; }
    // How many segments next() gives in all (conics as the quadratics they become, unless
    // includeConvertedConics is false: then a conic counts once).
    int calculateSize(bool includeConvertedConics = true) const;
    bool hasNext() const;
    PathSegment next();  // Done when there are no more

private:
    Path path_;
    ConicEvaluation evaluation_;
    float tolerance_;
    size_t index_ = 0;
    Offset current_;
    std::vector<PathSegment> pending_;  // a conic's quadratics yet to give
};

// Tests points against a path again and again (Compose's PathHitTester), its conics as
// quadratics within `tolerance` dp.
class PathHitTester {
public:
    PathHitTester() = default;
    explicit PathHitTester(const Path& path, float tolerance = 0.5f) { updatePath(path, tolerance); }
    void updatePath(const Path& path, float tolerance = 0.5f);
    bool contains(Offset position) const;

private:
    Path path_;
    Rect bounds_;
};

// What a Shape is in a size (Compose's Outline): a rect, a rounded rect, or any path, in dp from
// the top left of what it is for. Backgrounds, borders, clips and shadows draw the first two as
// themselves and the third as a path.
struct Outline {
    enum class Kind { Rectangle, Rounded, Generic };
    Kind kind = Kind::Rectangle;
    RoundRect roundRect;  // a Rectangle's rect is roundRect.rect, its corners none
    Path path;            // Generic's
    static Outline Rectangle(const Rect& rect) { return {Kind::Rectangle, {rect}, {}}; }
    static Outline Rounded(const RoundRect& roundRect) { return {Kind::Rounded, roundRect, {}}; }
    static Outline Generic(Path path) { return {Kind::Generic, {path.bounds()}, std::move(path)}; }
    Rect bounds() const { return roundRect.rect; }
    // As a path (a rect's or rounded rect's outline, or the path).
    Path asPath() const;
    friend bool operator==(const Outline&, const Outline&) = default;
};

// A shape made by a path built in its size and direction (Compose's GenericShape):
//   GenericShape([](Path& p, Size size, LayoutDirection) { p.moveTo(size.width / 2, 0); ... })
Shape GenericShape(std::function<void(Path& path, Size size, LayoutDirection layoutDirection)> builder);
// A shape whose outline is made as `createOutline` says (Compose's Shape implemented).
Shape OutlineShape(std::function<Outline(Size size, LayoutDirection layoutDirection)> createOutline);

struct PathStroke;
namespace detail {
class ContourMeasure;
}

// How stampedPathEffect lays each copy of its shape on the path (Skia's SkPath1DPathEffect
// styles): moved there, also turned the way the path goes, or bent along it.
enum class StampedPathEffectStyle { Translate, Rotate, Morph };

// What a path goes through before it is drawn (Compose's PathEffect, Skia's): dashes, rounded
// corners, copies of a shape stamped along it, or one effect after another. Applied as the path
// is drawn, at the pixels it is drawn at (as Skia applies a paint's effect). One made of what
// Skia refuses (dashes of an odd count or none, a negative interval, nothing on; a corner of no
// radius; a stamp of no advance or an empty shape) is none, as Skia's: it leaves the path as it is.
// Equal when made the same way.
class PathEffect {
public:
    PathEffect() = default;  // none
    // On, off, on… (in pairs, an odd last one left out as Android leaves it; none negative, not all
    // 0), `phase` into the pattern (its length's remainder; negative: from its end), starting
    // again on each contour; a closed one joins its last dash to its first. Strokes only: a fill
    // is drawn as it is.
    static PathEffect dashPathEffect(std::span<const float> intervals, float phase = 0);
    static PathEffect dashPathEffect(std::initializer_list<float> intervals, float phase = 0) {
        return dashPathEffect(std::span<const float>(intervals.begin(), intervals.size()), phase);
    }
    // Each corner between two lines cut back by `radius` (half each line at most) and joined by a
    // quadratic through it; curves as they are.
    static PathEffect cornerPathEffect(float radius);
    // `shape` every `advance` along each contour, filled (the stroke becomes a fill); a phase of
    // p puts the first one at advance - p.
    static PathEffect stampedPathEffect(const Path& shape, float advance, float phase, StampedPathEffectStyle style);
    // `inner`'s effect, then `outer`'s on what it made.
    static PathEffect chainPathEffect(const PathEffect& outer, const PathEffect& inner);

    bool isNone() const { return !node_; }
    // What the effect makes of `src` drawn with `stroke` at `resScale` pixels a unit (Skia's
    // filterPath): into `dst`, `stroke` changed if it draws the result otherwise (a stamp's, a
    // dashed line's, filled); false if it leaves the path as it is.
    bool filterPath(Path& dst, const Path& src, PathStroke& stroke, float resScale = 1) const;

    friend bool operator==(const PathEffect& a, const PathEffect& b);

    struct Node;

private:
    std::shared_ptr<const Node> node_;
};

struct PathStroke {
    float width = 0;  // 0: fill (unless a hairline)
    StrokeCap cap = StrokeCap::Butt;
    StrokeJoin join = StrokeJoin::Miter;
    float miter = 4;
    bool hairline = false;  // stroked one pixel wide, whatever the transform (the width unused)
    std::optional<PathEffect> pathEffect;  // what the path goes through first, filled or stroked
    bool strokes() const { return hairline || width > 0; }
};

namespace detail {
// The scale a matrix draws strokes and path effects at (Skia's ComputeResScaleForStroking): its
// larger column's length (1 if none).
float resScaleOf(float scaleX, float skewY, float skewX, float scaleY);
// `stroke`'s path effect on `path` at `resScale` (as Skia's FillPathWithPaint applies it): the
// path to draw in `effected` (or `path` itself, returned) and how, its effect gone.
const Path& pathEffected(const Path& path, PathStroke& stroke, float resScale, Path& effected);
} // namespace detail

// Lengths along a path (Compose's PathMeasure, Android's: Skia's SkPathMeasure), contour by
// contour (those of no length skipped), its curves measured as Skia measures them: cut in halves
// until each piece is within half a unit of a line (half a unit over `resScale`), positions and
// tangents on the curves themselves, pieces of them as curves.
class PathMeasure {
public:
    PathMeasure() = default;
    explicit PathMeasure(const Path& path, bool forceClosed = false, float resScale = 1) { setPath(path, forceClosed, resScale); }
    void setPath(const Path& path, bool forceClosed = false, float resScale = 1);
    float length() const;  // of the current contour (the first, until nextContour()); 0 if none
    bool isClosed() const;  // the current contour ends with a close (or forceClosed)
    // Where `distance` (kept within 0..length) lands, and the unit vector the contour goes there;
    // NaN, NaN if there is no contour (Compose's Offset.Unspecified).
    Offset getPosition(float distance) const;
    Offset getTangent(float distance) const;
    // Adds the piece from startDistance to stopDistance (kept within 0..length; false unless start
    // is under stop) to `destination`: its curves cut at those places, beginning with a moveTo
    // (else going on from where it was).
    bool getSegment(float startDistance, float stopDistance, Path& destination, bool startWithMoveTo = true) const;
    bool nextContour();  // moves on to the next contour, if there is one

private:
    std::vector<std::shared_ptr<const detail::ContourMeasure>> contours_;
    size_t current_ = 0;
};

class DrawScope;
class ImageVector;
namespace detail {
// Draws `vector` into all of `scope`, faded by `alpha`, as Compose's VectorPainter does: into a
// bitmap of the scope's size in pixels (kept by the vector for that size, density and config),
// drawn there through `colorFilter`, else its own tint, else (all of it in one color) in that
// color; mirrored in a right-to-left scope if it mirrors.
void drawVector(DrawScope& scope, const ImageVector& vector, float alpha, const std::optional<ColorFilter>& colorFilter);
// The vector drawn into `bitmap` (its id) by drawVector, 0 if none (for tests).
uint64_t vectorDrawnInto(const ImageBitmap& bitmap);
} // namespace detail

// One command of a vector's path data (Compose's PathNode), as SVG path data has them: moves,
// lines, cubics, quadratics (their reflective ones mirror the last control point) and elliptical
// arcs, to a point or relative to the current one. Its numbers are in `values` in the order
// Compose's constructor takes them (an arc's: its two radii, its turn in degrees and where it
// ends; its two flags apart).
struct PathNode {
    enum class Kind : uint8_t {
        Close, RelativeMoveTo, MoveTo, RelativeLineTo, LineTo, RelativeHorizontalTo, HorizontalTo, RelativeVerticalTo, VerticalTo,
        RelativeCurveTo, CurveTo, RelativeReflectiveCurveTo, ReflectiveCurveTo, RelativeQuadTo, QuadTo, RelativeReflectiveQuadTo,
        ReflectiveQuadTo, RelativeArcTo, ArcTo
    };
    Kind kind = Kind::Close;
    std::array<float, 6> values{};
    bool isMoreThanHalf = false, isPositiveArc = false;  // an arc's

    bool isCurve() const;  // a cubic (what a reflective cubic after it mirrors)
    bool isQuad() const;   // a quadratic (what a reflective quadratic after it mirrors)

    static PathNode Close() { return {}; }
    static PathNode RelativeMoveTo(float dx, float dy) { return {Kind::RelativeMoveTo, {dx, dy}}; }
    static PathNode MoveTo(float x, float y) { return {Kind::MoveTo, {x, y}}; }
    static PathNode RelativeLineTo(float dx, float dy) { return {Kind::RelativeLineTo, {dx, dy}}; }
    static PathNode LineTo(float x, float y) { return {Kind::LineTo, {x, y}}; }
    static PathNode RelativeHorizontalTo(float dx) { return {Kind::RelativeHorizontalTo, {dx}}; }
    static PathNode HorizontalTo(float x) { return {Kind::HorizontalTo, {x}}; }
    static PathNode RelativeVerticalTo(float dy) { return {Kind::RelativeVerticalTo, {dy}}; }
    static PathNode VerticalTo(float y) { return {Kind::VerticalTo, {y}}; }
    static PathNode RelativeCurveTo(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3) {
        return {Kind::RelativeCurveTo, {dx1, dy1, dx2, dy2, dx3, dy3}};
    }
    static PathNode CurveTo(float x1, float y1, float x2, float y2, float x3, float y3) { return {Kind::CurveTo, {x1, y1, x2, y2, x3, y3}}; }
    static PathNode RelativeReflectiveCurveTo(float dx1, float dy1, float dx2, float dy2) {
        return {Kind::RelativeReflectiveCurveTo, {dx1, dy1, dx2, dy2}};
    }
    static PathNode ReflectiveCurveTo(float x1, float y1, float x2, float y2) { return {Kind::ReflectiveCurveTo, {x1, y1, x2, y2}}; }
    static PathNode RelativeQuadTo(float dx1, float dy1, float dx2, float dy2) { return {Kind::RelativeQuadTo, {dx1, dy1, dx2, dy2}}; }
    static PathNode QuadTo(float x1, float y1, float x2, float y2) { return {Kind::QuadTo, {x1, y1, x2, y2}}; }
    static PathNode RelativeReflectiveQuadTo(float dx, float dy) { return {Kind::RelativeReflectiveQuadTo, {dx, dy}}; }
    static PathNode ReflectiveQuadTo(float x, float y) { return {Kind::ReflectiveQuadTo, {x, y}}; }
    static PathNode RelativeArcTo(float horizontalEllipseRadius, float verticalEllipseRadius, float theta, bool isMoreThanHalf,
                                  bool isPositiveArc, float arcStartDx, float arcStartDy) {
        return {Kind::RelativeArcTo, {horizontalEllipseRadius, verticalEllipseRadius, theta, arcStartDx, arcStartDy}, isMoreThanHalf, isPositiveArc};
    }
    static PathNode ArcTo(float horizontalEllipseRadius, float verticalEllipseRadius, float theta, bool isMoreThanHalf, bool isPositiveArc,
                          float arcStartX, float arcStartY) {
        return {Kind::ArcTo, {horizontalEllipseRadius, verticalEllipseRadius, theta, arcStartX, arcStartY}, isMoreThanHalf, isPositiveArc};
    }
    friend bool operator==(const PathNode&, const PathNode&) = default;
};

// SVG path data ("M19,13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z") as nodes, read as Compose's PathParser
// reads it: a command letter and as many sets of numbers as follow it (a move's after the first
// are lines; numbers left over are dropped), an arc's flags a single 0 or 1 each, what can't be
// read skipped to the next command letter.
std::vector<PathNode> addPathNodes(std::string_view pathData);
// The path the nodes draw (Compose's List<PathNode>.toPath): into `target` (emptied, its fill
// type kept), or a new one. An arc becomes cubics of at most an eighth of a turn, as Compose's
// (made bigger when its radii can't reach its end; a line where a radius is 0).
void toPath(const std::vector<PathNode>& nodes, Path& target);
Path toPath(const std::vector<PathNode>& nodes);

// Compose's PathParser: SVG path data read into nodes it keeps.
class PathParser {
public:
    PathParser& parsePathString(std::string_view pathData);  // its nodes, in place of what it held
    // `pathData`'s nodes added to `nodes`, which it returns.
    std::vector<PathNode>& pathStringToNodes(std::string_view pathData, std::vector<PathNode>& nodes);
    PathParser& addPathNodes(const std::vector<PathNode>& nodes);
    const std::vector<PathNode>& toNodes() const { return nodes_; }
    Path toPath() const;
    void clear() { nodes_.clear(); }

private:
    std::vector<PathNode> nodes_;
};

// Path data made by calls (Compose's PathBuilder):
//   std::vector<PathNode> tick = PathData([](PathBuilder& p) { p.moveTo(5, 12).lineToRelative(5, 5).lineToRelative(9, -10); });
class PathBuilder {
public:
    const std::vector<PathNode>& nodes() const { return nodes_; }
    PathBuilder& close() { return add(PathNode::Close()); }
    PathBuilder& moveTo(float x, float y) { return add(PathNode::MoveTo(x, y)); }
    PathBuilder& moveToRelative(float dx, float dy) { return add(PathNode::RelativeMoveTo(dx, dy)); }
    PathBuilder& lineTo(float x, float y) { return add(PathNode::LineTo(x, y)); }
    PathBuilder& lineToRelative(float dx, float dy) { return add(PathNode::RelativeLineTo(dx, dy)); }
    PathBuilder& horizontalLineTo(float x) { return add(PathNode::HorizontalTo(x)); }
    PathBuilder& horizontalLineToRelative(float dx) { return add(PathNode::RelativeHorizontalTo(dx)); }
    PathBuilder& verticalLineTo(float y) { return add(PathNode::VerticalTo(y)); }
    PathBuilder& verticalLineToRelative(float dy) { return add(PathNode::RelativeVerticalTo(dy)); }
    PathBuilder& curveTo(float x1, float y1, float x2, float y2, float x3, float y3) { return add(PathNode::CurveTo(x1, y1, x2, y2, x3, y3)); }
    PathBuilder& curveToRelative(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3) {
        return add(PathNode::RelativeCurveTo(dx1, dy1, dx2, dy2, dx3, dy3));
    }
    PathBuilder& reflectiveCurveTo(float x1, float y1, float x2, float y2) { return add(PathNode::ReflectiveCurveTo(x1, y1, x2, y2)); }
    PathBuilder& reflectiveCurveToRelative(float dx1, float dy1, float dx2, float dy2) {
        return add(PathNode::RelativeReflectiveCurveTo(dx1, dy1, dx2, dy2));
    }
    PathBuilder& quadTo(float x1, float y1, float x2, float y2) { return add(PathNode::QuadTo(x1, y1, x2, y2)); }
    PathBuilder& quadToRelative(float dx1, float dy1, float dx2, float dy2) { return add(PathNode::RelativeQuadTo(dx1, dy1, dx2, dy2)); }
    PathBuilder& reflectiveQuadTo(float x1, float y1) { return add(PathNode::ReflectiveQuadTo(x1, y1)); }
    PathBuilder& reflectiveQuadToRelative(float dx1, float dy1) { return add(PathNode::RelativeReflectiveQuadTo(dx1, dy1)); }
    PathBuilder& arcTo(float horizontalEllipseRadius, float verticalEllipseRadius, float theta, bool isMoreThanHalf, bool isPositiveArc,
                       float x1, float y1) {
        return add(PathNode::ArcTo(horizontalEllipseRadius, verticalEllipseRadius, theta, isMoreThanHalf, isPositiveArc, x1, y1));
    }
    PathBuilder& arcToRelative(float a, float b, float theta, bool isMoreThanHalf, bool isPositiveArc, float dx1, float dy1) {
        return add(PathNode::RelativeArcTo(a, b, theta, isMoreThanHalf, isPositiveArc, dx1, dy1));
    }

private:
    PathBuilder& add(PathNode node) {
        nodes_.push_back(node);
        return *this;
    }
    std::vector<PathNode> nodes_;
};
template <class Block>
std::vector<PathNode> PathData(Block&& block) {
    PathBuilder builder;
    block(builder);
    return builder.nodes();
}

// One path of a vector (Compose's VectorPath), in the vector's viewport units: its data, filled
// with `fill` (unset: not) and stroked with `stroke` (unset: not; a width of 0 is a hairline),
// each at its alpha, only the piece from trimPathStart to trimPathEnd drawn (fractions of its
// first contour's length, both moved on by trimPathOffset, wrapping round its end: as Android's).
// Gradients are in the units it is drawn in, where a far edge is the vector's drawn size.
struct VectorPath {
    std::string name;
    std::vector<PathNode> pathData;
    Path::FillType pathFillType = Path::FillType::NonZero;
    std::optional<Brush> fill;
    float fillAlpha = 1;
    std::optional<Brush> stroke;
    float strokeAlpha = 1, strokeLineWidth = 0;
    StrokeCap strokeLineCap = StrokeCap::Butt;
    StrokeJoin strokeLineJoin = StrokeJoin::Miter;
    float strokeLineMiter = 4;
    float trimPathStart = 0, trimPathEnd = 1, trimPathOffset = 0;
    friend bool operator==(const VectorPath&, const VectorPath&) = default;
};

class VectorNode;
// A group of a vector's paths and groups (Compose's VectorGroup): what is in it scaled and
// turned (degrees, clockwise) about its pivot, then moved, and clipped to its clip path (in its
// own units), all in its parent's units.
struct VectorGroup {
    std::string name;
    float rotation = 0, pivotX = 0, pivotY = 0, scaleX = 1, scaleY = 1, translationX = 0, translationY = 0;
    std::vector<PathNode> clipPathData;  // empty: not clipped
    std::vector<VectorNode> children;    // drawn in order
    size_t size() const;
    const VectorNode& operator[](size_t index) const;
    std::vector<VectorNode>::const_iterator begin() const;
    std::vector<VectorNode>::const_iterator end() const;
    friend bool operator==(const VectorGroup& a, const VectorGroup& b);
};
// What a group holds (Compose's VectorNode): a path or a group.
class VectorNode {
public:
    VectorNode(VectorPath path) : node_(std::move(path)) {}
    VectorNode(VectorGroup group) : node_(std::move(group)) {}
    const VectorPath* path() const { return std::get_if<VectorPath>(&node_); }
    const VectorGroup* group() const { return std::get_if<VectorGroup>(&node_); }
    friend bool operator==(const VectorNode& a, const VectorNode& b) { return a.node_ == b.node_; }

private:
    std::variant<VectorPath, VectorGroup> node_;
};
inline size_t VectorGroup::size() const { return children.size(); }  // (here: VectorNode complete)
inline const VectorNode& VectorGroup::operator[](size_t index) const { return children[index]; }
inline std::vector<VectorNode>::const_iterator VectorGroup::begin() const { return children.begin(); }
inline std::vector<VectorNode>::const_iterator VectorGroup::end() const { return children.end(); }

// A vector image (Compose's ImageVector): a tree of paths and groups in a viewport of its own
// (24 x 24 for Material icons), its size where nothing else gives one (dp), the color it is
// tinted with and how (unset: it draws in its own colors), and whether it is drawn mirrored in a
// right-to-left layout (a back arrow points to the start). Equal by all of these (not its id).
// A painter (VectorPainter, Icon) draws it into a bitmap it keeps for its size and draws that, as
// Compose does: a vector all in one color (and drawn tinted, or in that color) as an Alpha8
// bitmap.
class ImageVector {
public:
    ImageVector() = default;
    // One path of SVG path data filled black (as Compose's Material icons are), its default size
    // its viewport's.
    static ImageVector fromSvgPath(std::string_view pathData, float viewportWidth = 24, float viewportHeight = 24);
    // Reads an Android vector drawable as Compose's ImageVector.vectorResource does: <vector>
    // (width, height, viewport, autoMirrored, tint, tintMode, alpha), <group>s (name, rotation,
    // pivot, scale, translate; nested) and <path>s (name, pathData, fillColor, strokeColor,
    // strokeWidth (1 unset), their alphas, line cap, join, miter limit, fillType, trimPathStart/
    // End/Offset; aapt:attr <gradient>s: linear, radial, sweep, their items or start, center and
    // end colors, tile mode) and <clip-path>s (clipping what comes after them in their group).
    // Colors are #RGB, #ARGB, #RRGGBB or #AARRGGBB; @color references are not read (black). The
    // vector's alpha fades its paths' fills and strokes. An <animated-vector>'s drawable is read
    // as it starts. Empty if there is no <vector>.
    static ImageVector fromVectorXml(std::string_view xml);
    class Builder;

    bool empty() const { return !data_; }
    const std::string& name() const;
    float defaultWidth() const;
    float defaultHeight() const;
    float viewportWidth() const;
    float viewportHeight() const;
    const VectorGroup& root() const;
    std::optional<Color> tintColor() const;
    BlendMode tintBlendMode() const;
    bool autoMirror() const;
    uint64_t id() const;  // unique to each one built: the bitmaps drawn of it are kept by it

    friend bool operator==(const ImageVector& a, const ImageVector& b);

    struct Data;

private:
    std::shared_ptr<const Data> data_;
    friend void detail::drawVector(DrawScope& scope, const ImageVector& vector, float alpha, const std::optional<ColorFilter>& colorFilter);
};

// Builds a vector (Compose's ImageVector.Builder): paths drawn in order, in groups (nested) that
// move, scale, turn and clip what is in them.
//   ImageVector badge = ImageVector::Builder(48, 48, 24, 24)
//       .addPath({.pathData = addPathNodes("M12,2a10,10 0 1,0 0.01,0z"), .fill = Brush::solid(Color::hex(0x1E88E5))})
//       .group({.rotation = 45, .pivotX = 12, .pivotY = 12}, [](ImageVector::Builder& b) {
//           b.path({.stroke = Brush::solid(colors::White), .strokeLineWidth = 2},
//                  [](PathBuilder& p) { p.moveTo(7, 12).lineTo(17, 12); });
//       })
//       .build();
//   Image(rememberVectorPainter(badge), "Onaylı");   // its own colors; Icon(badge, ...) tints it
class ImageVector::Builder {
public:
    Builder(float defaultWidth, float defaultHeight, float viewportWidth, float viewportHeight, std::optional<Color> tintColor = {},
            BlendMode tintBlendMode = BlendMode::SrcIn, bool autoMirror = false)
        : Builder({}, defaultWidth, defaultHeight, viewportWidth, viewportHeight, tintColor, tintBlendMode, autoMirror) {}
    Builder(std::string name, float defaultWidth, float defaultHeight, float viewportWidth, float viewportHeight,
            std::optional<Color> tintColor = {}, BlendMode tintBlendMode = BlendMode::SrcIn, bool autoMirror = false);
    // What is added after it goes in `group` (after its children, if it has any), inside the
    // open ones, until its clearGroup.
    Builder& addGroup(VectorGroup group = {});
    Builder& clearGroup();
    Builder& addPath(VectorPath path);
    template <class Content>
    Builder& group(VectorGroup group, Content&& content) {
        addGroup(std::move(group));
        content(*this);
        return clearGroup();
    }
    // A path of what `pathBuilder` makes, in the paints of `path` (Compose's Builder.path).
    template <class Block>
    Builder& path(VectorPath path, Block&& pathBuilder) {
        path.pathData = PathData(std::forward<Block>(pathBuilder));
        return addPath(std::move(path));
    }
    ImageVector build();  // (the groups still open closed)

private:
    std::string name_;
    float defaultWidth_, defaultHeight_, viewportWidth_, viewportHeight_;
    std::optional<Color> tintColor_;
    BlendMode tintBlendMode_;
    bool autoMirror_;
    std::vector<VectorGroup> groups_{VectorGroup{}};  // the root, then the open groups
};

// An animated vector drawable (Compose's AnimatedImageVector): a vector and how its named
// groups', paths' and its own properties change over time. rememberAnimatedVectorPainter draws it.
class AnimatedImageVector {
public:
    AnimatedImageVector() = default;
    // Reads an Android <animated-vector> in one file (as Android Studio and Shape Shifter write
    // it): its <vector> in <aapt:attr name="android:drawable">, each <target android:name>'s
    // animation in <aapt:attr name="android:animation">: <objectAnimator>s (a propertyName or
    // <propertyValuesHolder>s; valueFrom, else the element's own value, and valueTo; valueType
    // float, color or path; duration, startOffset, repeatCount, repeatMode, Android's
    // interpolators: linear, accelerate, decelerate, accelerate_decelerate, fast_out_slow_in,
    // fast_out_linear_in, linear_out_slow_in) in <set>s together or sequentially. Animated:
    // groups' rotation, pivot, scale, translation; paths' pathData (morphed between paths of the
    // same commands, else switched halfway), fill and stroke colors and alphas, strokeWidth,
    // trimPath*; the vector's alpha. Not read: animators in files of their own (@animator/…),
    // keyframes, path interpolators; an infinite repeat plays once. Empty if it isn't one.
    static AnimatedImageVector fromAnimatedVectorXml(std::string_view xml);
    bool empty() const { return !data_; }
    int totalDurationMillis() const;
    ImageVector imageVector() const;           // as it starts
    ImageVector vectorAt(float millis) const;  // that far into its animations
    friend bool operator==(const AnimatedImageVector& a, const AnimatedImageVector& b) { return a.data_ == b.data_; }

private:
    struct Data;
    std::shared_ptr<const Data> data_;
};

class GraphicsContext;

// How a shape is drawn (Compose's DrawStyle): filled (Fill), or outlined (Stroke): `width` dp
// wide, centered on its outline (0: a hairline, one pixel wide whatever the transform), its
// corners joined (a miter cut off past `miter` times the width), its ends capped, its path
// through a PathEffect first (dashed, rounded, stamped).
struct DrawStyle {
    bool stroke = false;
    float width = 0;
    float miter = 4;
    StrokeCap cap = StrokeCap::Butt;
    StrokeJoin join = StrokeJoin::Miter;
    std::optional<PathEffect> pathEffect;
    constexpr bool isStroke() const { return stroke; }
    // As a path's stroke (none for Fill), with its path effect.
    PathStroke pathStroke() const {
        return stroke ? PathStroke{width, cap, join, miter, width <= 0, pathEffect} : PathStroke{.pathEffect = pathEffect};
    }
    friend bool operator==(const DrawStyle&, const DrawStyle&) = default;
};
inline const DrawStyle Fill{};  // (not constexpr: a path effect holds a shared pointer)
constexpr DrawStyle Stroke(float width = 0, float miter = 4, StrokeCap cap = StrokeCap::Butt, StrokeJoin join = StrokeJoin::Miter,
                           std::optional<PathEffect> pathEffect = {}) {
    return {true, width, miter, cap, join, pathEffect};
}

// A shadow (Compose's Shadow, for text and, with spread, for dropShadow/innerShadow): its
// color, how far it is moved and how much it is blurred (dp; Compose's text shadows count
// pixels), and how much bigger (a drop shadow) or smaller (an inner one's hole) the shape is.
struct Shadow {
    Color color{0, 0, 0, 1};
    Offset offset;
    float blurRadius = 0;
    float spread = 0;  // not for text
    // Not for text either (Compose 1.9's dropShadow and innerShadow): a brush painting it instead
    // of the color (spread over what it covers), its alpha, how it goes onto what is under it.
    std::optional<Brush> brush;
    float alpha = 1;
    BlendMode blendMode = BlendMode::SrcOver;
    friend bool operator==(const Shadow& a, const Shadow& b) {
        return a.color == b.color && a.offset.x == b.offset.x && a.offset.y == b.offset.y && a.blurRadius == b.blurRadius &&
               a.spread == b.spread && a.brush == b.brush && a.alpha == b.alpha && a.blendMode == b.blendMode;
    }
};

// Where elevation shadows' light is, as Android's theme has it: above the window's top middle
// (`center`, the window's dp), `z` up, `radius` across, the ambient and spot shadows at those
// alphas; `density` px a dp (Android works shadows out in px). The platform sets the window's.
struct ShadowLight {
    Offset center;
    float z = 500, radius = 800;  // dp
    float ambientAlpha = 0.039f, spotAlpha = 0.19f;
    float density = 1;
    friend bool operator==(const ShadowLight&, const ShadowLight&) = default;
};

// An elevation shadow (graphicsLayer's shadowElevation): how high its caster is at each point (z =
// plane[0] x + plane[1] y + plane[2], dp, x and y the drawing's dp: flat but for a layer turned in
// 3D), its two colors (at their alphas times the light's), and the caster's alpha (below 1 it is
// seen through, so its shadow shows under it too).
struct ElevationShadow {
    std::array<float, 3> plane{0, 0, 0};
    Color ambientColor{0, 0, 0, 1}, spotColor{0, 0, 0, 1};
    float alpha = 1;
    // Where it may reach for a caster over `bounds` (dp), the light's center there `lightCenter`
    // (as Skia's SkDrawShadowMetrics::GetLocalBounds works it out, before any transform).
    Rect reach(const Rect& bounds, const ShadowLight& light, Offset lightCenter) const;
    friend bool operator==(const ElevationShadow&, const ElevationShadow&) = default;
};
inline constexpr Color DefaultShadowColor{0, 0, 0, 1};  // Compose's

// How a clip clips (Compose's ClipOp): to what is inside its shape, or to what is outside it.
enum class ClipOp { Intersect, Difference };

// A transform of points (Compose's Matrix): 4x4, `values[row * 4 + column]`, a point taken as the
// row (x, y, z, 1) times it, so row 3 is the translation and column 3 the perspective. As in
// Compose, translate, scale and rotateZ come before what the matrix did already (as a Canvas's
// do), rotateX and rotateY after it. Drawn through (DrawTransform::transform), it acts in the
// plane: its z row and column are left out, as Android leaves them out.
class Matrix {
public:
    static constexpr int ScaleX = 0, SkewY = 1, Perspective0 = 3, SkewX = 4, ScaleY = 5, Perspective1 = 7, ScaleZ = 10,
                         TranslateX = 12, TranslateY = 13, TranslateZ = 14, Perspective2 = 15;
    std::array<float, 16> values{1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};

    float operator()(int row, int column) const { return values[size_t(row * 4 + column)]; }
    float& operator()(int row, int column) { return values[size_t(row * 4 + column)]; }
    // Where `point` goes (divided by its w; where that is 0, the origin).
    Offset map(Offset point) const;
    // The bounds of where the rect's corners go.
    Rect map(const Rect& rect) const;
    // This, then `m`.
    Matrix& timesAssign(const Matrix& m);
    Matrix& operator*=(const Matrix& m) { return timesAssign(m); }
    void invert();  // left as it is if it has no inverse
    void reset();
    void rotateX(float degrees);
    void rotateY(float degrees);
    void rotateZ(float degrees);  // clockwise on the screen
    void scale(float x = 1, float y = 1, float z = 1);
    void translate(float x = 0, float y = 0, float z = 0);
    // Scaled, then turned about x, y and z, then moved, all about the pivot (graphicsLayer's).
    void resetToPivotedTransform(float pivotX = 0, float pivotY = 0, float translationX = 0, float translationY = 0,
                                 float translationZ = 0, float rotationX = 0, float rotationY = 0, float rotationZ = 0,
                                 float scaleX = 1, float scaleY = 1, float scaleZ = 1);
    bool isIdentity() const { return *this == Matrix(); }
    friend bool operator==(const Matrix&, const Matrix&) = default;
};

// How a layer is composited (graphicsLayer): scaled and rotated around `origin` (a fraction
// of its size), then moved.
struct LayerTransform {
    float scaleX = 1, scaleY = 1, rotationZ = 0;  // degrees, clockwise
    float translationX = 0, translationY = 0;     // dp
    Offset origin{0.5f, 0.5f};
    constexpr bool identity() const {
        return scaleX == 1 && scaleY == 1 && rotationZ == 0 && translationX == 0 && translationY == 0;
    }
};

// Implemented by each platform backend.
class TextMeasurer {
public:
    virtual ~TextMeasurer() = default;
    virtual float measureWidth(std::string_view utf8, const Font& font) = 0;
    // What each cluster of utf8, laid out as one run, adds to its width (dp), at the cluster's
    // first byte (0 at the others): a stretch between clusters is as wide as its sum, shaped as a
    // part of the whole (kerning, ligatures, letter spacing). Line breaking measures with it,
    // laying the run out once. By default, measureWidth of each prefix.
    virtual std::vector<float> clusterAdvances(std::string_view utf8, const Font& font);
    virtual FontMetrics metrics(const Font& font) = 0;
    // How much bigger sp are than dp here (the system font size): sizes in a Font get it.
    virtual float fontScale() const { return 1; }
};

// The minimal drawing surface a platform backend has to provide (draw with DrawScope in app code).
class GraphicsContext {
public:
    virtual ~GraphicsContext() = default;
    // Rounded rects: one radius for all corners, or each its own (CornerRadii).
    virtual void fillRoundRect(const Rect& rect, const CornerRadii& radii, Color color) = 0;
    virtual void strokeRoundRect(const Rect& rect, const CornerRadii& radii, float width, Color color) = 0;
    // A line `width` dp wide, its ends square (half its width past them).
    virtual void drawLine(Offset from, Offset to, float width, Color color) = 0;
    // `rtl`: the paragraph goes right to left, which orders its stretches of either direction
    // (see bidi.hpp); either way the text starts at baseline.x and goes right.
    virtual void drawText(std::string_view utf8, Offset baseline, const Font& font, Color color, bool rtl = false) = 0;
    // Text painted with `brush` spread over `brushArea` (a gradient across a paragraph). The
    // default paints it in the color at the area's center.
    virtual void drawTextBrush(std::string_view utf8, Offset baseline, const Font& font, const Brush& brush, const Rect& brushArea,
                               bool rtl = false) {
        drawText(utf8, baseline, font, brush.colorAt(brushArea.center(), brushArea), rtl);
    }
    // Text's shadow: its glyphs (not color emoji) in `color`, blurred by `blurRadius` dp. The
    // default draws them sharp.
    virtual void drawTextShadow(std::string_view utf8, Offset baseline, const Font& font, Color color, float blurRadius,
                                bool rtl = false) {
        (void)blurRadius;
        drawText(utf8, baseline, font, color, rtl);
    }
    // Strokes part of the circle inscribed in `oval`. Angles in degrees, 0 = 3 o'clock, clockwise.
    virtual void drawArc(const Rect& oval, float startDegrees, float sweepDegrees, float strokeWidth, Color color,
                         bool roundCap) = 0;
    // Draws the `src` pixel rect of `image` into `dst`, sampled as `quality` says (where the
    // backend can: SDL's renderer samples Medium and High as Low).
    virtual void drawImage(const ImageBitmap& image, const Rect& src, const Rect& dst, float alpha,
                           FilterQuality quality = DefaultFilterQuality) = 0;
    // An image as a mask (an Alpha8 bitmap, as Android draws one): `brush` (spread over
    // brushArea) where the `src` pixels of `image` go in `dst`, as opaque as they are, faded by
    // `alpha`. The default draws the brush in a layer and the image over it by DstIn.
    virtual void drawImageMask(const ImageBitmap& image, const Rect& src, const Rect& dst, const Brush& brush, const Rect& brushArea,
                               float alpha, FilterQuality quality = DefaultFilterQuality);
    // Everything drawn until popLayer() goes to an offscreen layer that is then composited
    // into `bounds`, clipped to a rounded rect of `radii` and faded by `alpha`.
    virtual void pushLayer(const Rect& bounds, const CornerRadii& radii, float alpha, const LayerTransform& transform = {}) = 0;
    virtual void popLayer() = 0;
    virtual void pushClip(const Rect& rect) = 0;
    // Draws until popClip() only inside a rounded rect, or a path placed at `origin` (by its fill
    // type), or with ClipOp::Difference only outside it; within the clips already set, their
    // edges anti-aliased (Compose's Canvas.clipRect, clipRoundRect and clipPath).
    virtual void pushClipRoundRect(const Rect& rect, const CornerRadii& radii, ClipOp op = ClipOp::Intersect) = 0;
    virtual void pushClipPath(const Path& path, Offset origin, ClipOp op = ClipOp::Intersect) = 0;
    virtual void popClip() = 0;
    // Everything drawn until popTranslation() moves by `by` dp, without a layer: clips set
    // before stay where they are, those set inside move along.
    virtual void pushTranslation(Offset by) = 0;
    virtual void popTranslation() = 0;
    // Everything drawn until popTransform() goes through `matrix` (dp to dp, from the drawing's
    // own coordinates to those outside it), as through Compose's Canvas.concat: clips set before
    // stay where they are, those set inside are transformed with what they clip. Nothing is
    // clipped to where the drawing was.
    virtual void pushTransform(const Matrix& matrix) = 0;
    virtual void popTransform() = 0;
    // A layer (popLayer ends it) whose content, drawn within `bounds` and cut to them, goes
    // through `effect` as it is composited (blurred, moved, as far past the bounds as that takes
    // it), faded by `alpha`; clips set around it cut what shows. The default composites the
    // content as it is.
    virtual void pushEffectLayer(const Rect& bounds, const RenderEffect& effect, float alpha) {
        (void)effect;
        pushLayer(bounds, CornerRadii(), alpha);
    }
    // What is drawn so far under `bounds` (into the same target: in an offscreen layer, into the
    // layer), and around them as far as `effect` reaches (past that as its edge treatment says),
    // through the effect, drawn back in place of it, mixed with it by `alpha`: a backdrop blur
    // (frosted glass). Clips set around it cut what shows. The default draws nothing.
    virtual void drawBackdrop(const Rect& bounds, const RenderEffect& effect, float alpha) { (void)bounds, (void)effect, (void)alpha; }
    // Draws in between go on with `mode` instead of SrcOver (where the backend can: see
    // DrawScope's blendMode).
    virtual void pushBlendMode(BlendMode mode) { (void)mode; }
    virtual void popBlendMode() {}
    // Draws in between not anti-aliased (`on` false: a Paint's isAntiAlias): each pixel all in
    // or all out, as Skia decides it: its middle inside the shape (a path's winding there), a
    // hairline a pixel each step along it (where the backend can; the default draws as it is).
    virtual void pushAntiAlias(bool on) { (void)on; }
    virtual void popAntiAlias() {}
    // Draws in between have their colors (their paint's, an image's pixels) through `filter`
    // before they go on, an inner filter's first (Compose's ColorFilter on a Paint, an Image, an
    // Icon). Backends apply it; one that draws nothing itself records it.
    virtual void pushColorFilter(const ColorFilter& filter) = 0;
    virtual void popColorFilter() = 0;
    // True if nothing drawn inside `rect` could show: it lies wholly outside the current clip
    // (or layer). Content that clips itself to such a rect isn't drawn at all.
    virtual bool quickReject(const Rect& rect) const {
        (void)rect;
        return false;
    }
    // Fills a rounded rect with `brush`, its gradient spread over `brushArea`. The default paints
    // the color at the rect's center; backends interpolate the gradient.
    virtual void fillGradient(const Rect& rect, const CornerRadii& radii, const Brush& brush, const Rect& brushArea) {
        fillRoundRect(rect, radii, brush.colorAt(rect.center(), brushArea));
    }
    // Fills (no stroke) or strokes `path` placed at `origin`.
    // A triangle mesh (Compose's Canvas.drawVertices, Skia's): its triangles placed at `origin`,
    // filled without anti-aliasing, as Skia fills them; each pixel the brush (spread over
    // brushArea, as everywhere) where its texture coordinate is (from `origin` too; none: where
    // the pixel is), or the brush's color made opaque; that through the vertex colors (worked out
    // between them, premultiplied) by `blendMode`, the brush the source and the colors the
    // destination (no colors: the brush as it is); then faded by `alpha`. Triangles that overlap
    // each go on over the ones before.
    virtual void drawVertices(const Vertices& vertices, Offset origin, BlendMode blendMode, const Brush& brush, const Rect& brushArea,
                              float alpha) = 0;
    virtual void drawPath(const Path& path, Offset origin, const Brush& brush, const Rect& brushArea,
                          const PathStroke& stroke) = 0;
    // A rounded rect's shadow: under it (spread out, moved, blurred), or inside it (`inner`: the
    // color where a hole of the rect shrunk by spread and moved isn't, blurred, inside the rect).
    // The default draws a drop shadow as rings fading out, and no inner shadow.
    virtual void drawShadow(const Rect& rect, const CornerRadii& radii, const Shadow& shadow, bool inner) {
        if (inner || shadow.color.alpha() <= 0) return;
        for (int i = 6; i >= 1; --i) {
            float t = float(i) / 6, grow = shadow.spread + shadow.blurRadius * t;
            Rect r{rect.x - grow + shadow.offset.x, rect.y - grow + shadow.offset.y, rect.width + 2 * grow, rect.height + 2 * grow};
            if (r.width > 0 && r.height > 0) fillRoundRect(r, radii.grown(grow), shadow.color.copy(shadow.color.alpha() * (1.f - t + 1.f / 6) / 3.5f));
        }
    }
    // The same for a shape of any outline (`outline` from `origin`), as Compose draws one: under it,
    // the outline filled and stroked 2 * spread wide (mitered), each blurred, the two over each
    // other, moved; inside it, the color where a hole (the outline worn away by the spread, moved)
    // blurred isn't. The default draws it through effect layers (pushEffectLayer).
    virtual void drawShadowPath(const Path& outline, Offset origin, const Shadow& shadow, bool inner);
    // The light elevation shadows have (set by the platform), and its center in the dp drawn in
    // now: as given (the window's) less what has moved the drawing (a backend that knows its
    // translations and layers says so; the default takes the drawing's dp for the window's).
    ShadowLight light;
    virtual Offset lightCenter() const { return light.center; }
    // An elevation shadow of `outline` (from `origin`) as Android draws one (a RenderNode's, by
    // Skia's SkShadowUtils): an ambient shadow all round and a spot shadow cast from the light.
    // The default draws them as Skia does when it can do nothing better (blurred: the outline
    // spread and blurred by the elevation, and cast from the light and blurred), through
    // drawShadow and drawShadowPath, in the dp drawn in (lightCenter()).
    virtual void drawElevationShadow(const Outline& outline, Offset origin, const ElevationShadow& shadow);
    // A layer clipped to `outline` (from the bounds' top left, any path by its fill type) rather
    // than a rounded rect. The default clips to the bounds.
    virtual void pushOutlineLayer(const Rect& bounds, const Path& outline, float alpha, const LayerTransform& transform = {}) {
        (void)outline;
        pushLayer(bounds, CornerRadii(), alpha, transform);
    }

    // Any Shape over a rect, by its outline there: a rect or a rounded rect whose corners are
    // circles drawn as such, any other as a path. Strokes lie inside the bounds; a clip
    // (pushClipShape) is popped by popClip().
    void fillShape(const Rect& rect, const Shape& shape, bool rtl, Color color);
    void fillShape(const Rect& rect, const Shape& shape, bool rtl, const Brush& brush, const Rect& brushArea);
    void strokeShape(const Rect& rect, const Shape& shape, bool rtl, float width, Color color);
    void strokeShape(const Rect& rect, const Shape& shape, bool rtl, float width, const Brush& brush, const Rect& brushArea);
    void pushShapeLayer(const Rect& bounds, const Shape& shape, bool rtl, float alpha, const LayerTransform& transform = {});
    void pushClipShape(const Rect& rect, const Shape& shape, bool rtl, ClipOp op = ClipOp::Intersect);
    // A shape's shadow as Compose's dropShadow and innerShadow draw it (cut where its bitmap ends,
    // in its blend mode, faded by its alpha), through drawShadow or drawShadowPath; a brush over
    // what it covers in a layer (SrcIn). A backend may draw a brushed one its own way.
    virtual void drawShapeShadow(const Rect& rect, const Shape& shape, bool rtl, const Shadow& shadow, bool inner);
    // Around what each layout node draws: a recording canvas lines frames up by them (see
    // display_list.hpp); others ignore them.
    virtual void beginNode(const void* node) { (void)node; }
    virtual void endNode() {}
    // Whether anything the node last ended (endNode) drew, with those in it, can show: false only
    // where the canvas knows all of it was clipped away (a recording canvas: scrolled out of view).
    virtual bool endedNodeShows() const { return true; }
    // Asked as a node is about to be drawn (before beginNode), where the canvas keeps what nodes
    // drew from one frame to the next (a DisplayList's frames, keepNodes): Kept, it drew again
    // what the node drew last frame, moved from where it was to `origin` (its top left),
    // as the node would ("unchanged": nothing that it and those in it draw from changed), and the
    // node draws nothing; Fresh, the node is drawn, into the frame kept; Other: it is drawn, but
    // not into what is kept (any other canvas, or a node drawn a second time in a frame).
    enum class NodeRecording : uint8_t { Other, Fresh, Kept };
    virtual NodeRecording recordNode(const void* node, Offset origin, bool unchanged) {
        (void)node, (void)origin, (void)unchanged;
        return NodeRecording::Other;
    }
    // What is drawn into a layer, where the canvas keeps layers' pixels from one frame to the next
    // (a recording replayed tells it which layer comes next): as usual, all of it (even what is
    // outside the area replayed: the canvas keeps it), or nothing (it has the pixels already).
    enum class LayerContent : uint8_t { Draw, DrawAll, Kept };
    // The next layer pushed is the one known as `key`; `same`: what is drawn in it is what was
    // drawn in it last frame (but for where it is), so the pixels it had then do.
    virtual void keyLayer(uint64_t key, bool same) {
        (void)key;
        (void)same;
    }
    // How to draw into the layer just pushed.
    virtual LayerContent layerContent() const { return LayerContent::Draw; }
};

namespace detail {
// Draws within its scope go through `filter` (if there is one): pushColorFilter, then popColorFilter.
struct Filtering {
    GraphicsContext& canvas;
    bool on;
    Filtering(GraphicsContext& context, const std::optional<ColorFilter>& filter) : canvas(context), on(filter.has_value()) {
        if (on) canvas.pushColorFilter(*filter);
    }
    ~Filtering() {
        if (on) canvas.popColorFilter();
    }
    Filtering(const Filtering&) = delete;
    Filtering& operator=(const Filtering&) = delete;
};
} // namespace detail

namespace utf8 {

// Decodes the code point starting at byte `i` and advances `i`. Invalid input decodes as U+FFFD.
inline char32_t next(std::string_view s, size_t& i) {
    auto byte = [&](size_t k) { return static_cast<unsigned char>(s[k]); };
    unsigned char c = byte(i);
    int len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xE ? 3 : (c >> 3) == 0x1E ? 4 : 0;
    if (len == 0 || i + len > s.size()) {
        ++i;
        return 0xFFFD;
    }
    char32_t cp = len == 1 ? c : c & (0x7F >> len);
    for (int k = 1; k < len; ++k) {
        unsigned char cc = byte(i + k);
        if ((cc & 0xC0) != 0x80) {
            ++i;
            return 0xFFFD;
        }
        cp = (cp << 6) | (cc & 0x3F);
    }
    i += len;
    return cp;
}

// Appends code point `c`, encoded.
inline void append(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += char(c);
    } else if (c < 0x800) {
        out += char(0xC0 | (c >> 6));
        out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += char(0xE0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    } else {
        out += char(0xF0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 0x3F));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    }
}

// Byte length of `s` without its last code point.
inline size_t dropLast(std::string_view s) {
    if (s.empty()) return 0;
    size_t i = s.size() - 1;
    while (i > 0 && (static_cast<unsigned char>(s[i]) & 0xC0) == 0x80) --i;
    return i;
}

// Grapheme clusters, as far as editing and drawing need them: a character with the combining
// marks and variation selectors after it, an emoji with its skin tone, emoji joined by ZWJ
// (a family), a flag (two regional indicators), a keycap (digit, U+FE0F, U+20E3) or a tag
// sequence (a black flag and tags). The caret moves over a cluster whole, and the renderer
// draws an emoji one as one glyph.
inline bool extendsCluster(char32_t c) {
    return (c >= 0x0300 && c <= 0x036F) || (c >= 0x0483 && c <= 0x0489) || (c >= 0x0591 && c <= 0x05BD) ||
           (c >= 0x0610 && c <= 0x061A) || (c >= 0x064B && c <= 0x065F) || c == 0x0670 || (c >= 0x0900 && c <= 0x0903) ||
           (c >= 0x093A && c <= 0x094F) || (c >= 0x0E31 && c <= 0x0E3A && c != 0x0E32 && c != 0x0E33) ||
           (c >= 0x0E47 && c <= 0x0E4E) || (c >= 0x1AB0 && c <= 0x1AFF) || (c >= 0x1DC0 && c <= 0x1DFF) || c == 0x200C ||
           (c >= 0x20D0 && c <= 0x20FF) || (c >= 0xFE00 && c <= 0xFE0F) || (c >= 0xFE20 && c <= 0xFE2F) ||
           (c >= 0x1F3FB && c <= 0x1F3FF) || (c >= 0xE0020 && c <= 0xE007F) || (c >= 0xE0100 && c <= 0xE01EF);
}
inline bool isRegionalIndicator(char32_t c) { return c >= 0x1F1E6 && c <= 0x1F1FF; }
// Characters shown as emoji even without U+FE0F (Unicode's Emoji_Presentation, 16.0): 👍,
// ⌚, 🇹; not ❤ or ☺, which are text unless U+FE0F follows.
inline bool isEmojiPresentation(char32_t c) {
    static constexpr char32_t ranges[][2] = {
        {0x231A, 0x231B},   {0x23E9, 0x23EC},   {0x23F0, 0x23F0},   {0x23F3, 0x23F3},   {0x25FD, 0x25FE},   {0x2614, 0x2615},
        {0x2648, 0x2653},   {0x267F, 0x267F},   {0x2693, 0x2693},   {0x26A1, 0x26A1},   {0x26AA, 0x26AB},   {0x26BD, 0x26BE},
        {0x26C4, 0x26C5},   {0x26CE, 0x26CE},   {0x26D4, 0x26D4},   {0x26EA, 0x26EA},   {0x26F2, 0x26F3},   {0x26F5, 0x26F5},
        {0x26FA, 0x26FA},   {0x26FD, 0x26FD},   {0x2705, 0x2705},   {0x270A, 0x270B},   {0x2728, 0x2728},   {0x274C, 0x274C},
        {0x274E, 0x274E},   {0x2753, 0x2755},   {0x2757, 0x2757},   {0x2795, 0x2797},   {0x27B0, 0x27B0},   {0x27BF, 0x27BF},
        {0x2B1B, 0x2B1C},   {0x2B50, 0x2B50},   {0x2B55, 0x2B55},   {0x1F004, 0x1F004}, {0x1F0CF, 0x1F0CF}, {0x1F18E, 0x1F18E},
        {0x1F191, 0x1F19A}, {0x1F1E6, 0x1F1FF}, {0x1F201, 0x1F201}, {0x1F21A, 0x1F21A}, {0x1F22F, 0x1F22F}, {0x1F232, 0x1F236},
        {0x1F238, 0x1F23A}, {0x1F250, 0x1F251}, {0x1F300, 0x1F320}, {0x1F32D, 0x1F335}, {0x1F337, 0x1F37C}, {0x1F37E, 0x1F393},
        {0x1F3A0, 0x1F3CA}, {0x1F3CF, 0x1F3D3}, {0x1F3E0, 0x1F3F0}, {0x1F3F4, 0x1F3F4}, {0x1F3F8, 0x1F43E}, {0x1F440, 0x1F440},
        {0x1F442, 0x1F4FC}, {0x1F4FF, 0x1F53D}, {0x1F54B, 0x1F54E}, {0x1F550, 0x1F567}, {0x1F57A, 0x1F57A}, {0x1F595, 0x1F596},
        {0x1F5A4, 0x1F5A4}, {0x1F5FB, 0x1F64F}, {0x1F680, 0x1F6C5}, {0x1F6CC, 0x1F6CC}, {0x1F6D0, 0x1F6D2}, {0x1F6D5, 0x1F6D7},
        {0x1F6DC, 0x1F6DF}, {0x1F6EB, 0x1F6EC}, {0x1F6F4, 0x1F6FC}, {0x1F7E0, 0x1F7EB}, {0x1F7F0, 0x1F7F0}, {0x1F90C, 0x1F93A},
        {0x1F93C, 0x1F945}, {0x1F947, 0x1F9FF}, {0x1FA70, 0x1FA7C}, {0x1FA80, 0x1FA89}, {0x1FA8F, 0x1FAC6}, {0x1FACE, 0x1FADC},
        {0x1FADF, 0x1FAE9}, {0x1FAF0, 0x1FAF8},
    };
    if (c < 0x231A) return false;
    const auto* end = std::end(ranges);
    const auto* it = std::upper_bound(std::begin(ranges), end, c, [](char32_t v, const char32_t (&r)[2]) { return v < r[0]; });
    return it != std::begin(ranges) && c <= (*(it - 1))[1];
}

// The byte after the cluster that starts at byte `i`.
inline size_t nextCluster(std::string_view s, size_t i) {
    if (i >= s.size()) return s.size();
    size_t j = i;
    char32_t first = next(s, j);
    if (first == '\r' && j < s.size() && s[j] == '\n') return j + 1;
    if (isRegionalIndicator(first) && j < s.size()) {  // a flag is a pair
        size_t k = j;
        if (isRegionalIndicator(next(s, k))) j = k;
    }
    while (j < s.size()) {
        size_t k = j;
        char32_t c = next(s, k);
        if (c == 0x200D) {  // a joiner takes the next character in
            j = k;
            if (j < s.size()) next(s, j);
        } else if (extendsCluster(c)) {
            j = k;
        } else {
            break;
        }
    }
    return j;
}

// Where the cluster that ends at byte `i` (or holds it) starts.
inline size_t previousCluster(std::string_view s, size_t i) {
    i = std::min(i, s.size());
    for (size_t start = 0; start < i;) {
        size_t end = nextCluster(s, start);
        if (end >= i) return start;
        start = end;
    }
    return 0;
}

} // namespace utf8
} // namespace kompoz
