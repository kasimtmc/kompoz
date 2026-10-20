#pragma once

// Platform-independent geometry, color and drawing interfaces.
// Every coordinate in Kompoz is in dp; backends convert to pixels.

#include <algorithm>
#include <array>
#include <initializer_list>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace kompoz {

inline constexpr float kInfinity = std::numeric_limits<float>::infinity();

struct Color {
    float r = 0, g = 0, b = 0, a = 1;

    static constexpr Color hex(uint32_t rgb, float alpha = 1.f) {
        return {((rgb >> 16) & 0xFF) / 255.f, ((rgb >> 8) & 0xFF) / 255.f, (rgb & 0xFF) / 255.f, alpha};
    }
    // Same as Compose's color.copy(alpha = ...)
    constexpr Color copy(float alpha) const { return {r, g, b, alpha}; }

    friend constexpr bool operator==(const Color&, const Color&) = default;
};

namespace colors {
inline constexpr Color Transparent{0, 0, 0, 0};
inline constexpr Color Black{0, 0, 0, 1};
inline constexpr Color White{1, 1, 1, 1};
} // namespace colors

constexpr float lerp(float a, float b, float t) { return a + (b - a) * t; }
constexpr Color lerp(const Color& a, const Color& b, float t) {
    return {lerp(a.r, b.r, t), lerp(a.g, b.g, t), lerp(a.b, b.b, t), lerp(a.a, b.a, t)};
}

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

// A font's weight, 1 to 1000 (Compose's FontWeight): Normal is 400, Medium 500, Bold 700.
// Text comes in the family's file nearest it, as CSS picks (FontWeight::closest), emboldened
// when that is too light for a bold weight (FontWeight::synthesizesBold).
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
    // dp: its glyphs' outlines drawn this wide (centered on them) instead of filled (a text
    // style's DrawStyle Stroke); color glyphs (emoji) stay as they are.
    float strokeWidth = 0;
    StrokeJoin strokeJoin = StrokeJoin::Miter;
    // Its glyphs stretched across (their advances too) and slanted: a point y above the baseline
    // moved y * skewX to the right (a text style's TextGeometricTransform).
    float scaleX = 1, skewX = 0;
    friend constexpr bool operator==(const Font&, const Font&) = default;
};

struct FontMetrics {
    float ascent = 0, descent = 0, lineHeight = 0;
};

// A vector path, e.g. a Material icon, in its own viewport coordinates (24x24 for icons).
// Filled with the non-zero rule, like Compose's PathParser and SVG.
struct PathCommand {
    enum class Verb { Move, Line, Quad, Cubic, Close };
    Verb verb;
    float x1 = 0, y1 = 0, x2 = 0, y2 = 0, x = 0, y = 0;  // control points (as needed) and end point
    friend constexpr bool operator==(const PathCommand&, const PathCommand&) = default;
};

struct VectorPath;
class Brush;

class ImageVector {
public:
    ImageVector() = default;
    // Parses SVG path data ("M19,13h-6v6h-2v-6H5v-2h6V5h2v6h6v2z").
    static ImageVector fromSvgPath(std::string_view pathData, float viewportWidth = 24, float viewportHeight = 24);
    class Builder;  // one of several paths in paints of their own (below)
    // Reads an Android vector drawable (Compose's ImageVector.vectorResource): <vector> (width,
    // height, viewport, autoMirrored, alpha), <group>s (rotation, pivot, scale, translate; nested)
    // and <path>s (pathData, fillColor, strokeColor, strokeWidth, their alphas, line cap, join,
    // miter limit, fillType, trimPathStart/End/Offset; aapt:attr <gradient>s: linear, radial,
    // sweep, their items or start, center and end colors, tile mode) and <clip-path>s (clipping
    // what comes after them in their group). Colors are #RGB, #ARGB, #RRGGBB or #AARRGGBB;
    // @color references are not read (black). An <animated-vector>'s drawable is read as it
    // starts. Empty if there is no <vector>.
    static ImageVector fromVectorXml(std::string_view xml);
    // Its paths in their own paints (a Builder's); none for a one-path one (fromSvgPath), drawn in
    // the one color it is given.
    const std::vector<VectorPath>& paths() const;
    // Its size where nothing else gives one (an Image's, a VectorPainter's), dp: the Builder's
    // default size, the viewport's for fromSvgPath.
    float defaultWidth() const;
    float defaultHeight() const;

    bool empty() const { return !data_; }
    const std::vector<PathCommand>& commands() const;
    float viewportWidth() const;
    float viewportHeight() const;
    uint64_t id() const;  // stable and unique: lets backends cache rasterized versions
    // The same vector flipped left to right (its own id; made once).
    ImageVector mirrored() const;
    // One that Icon draws mirrored in a right-to-left layout (Compose's autoMirror: a back
    // arrow points to the start), and whether it is one.
    ImageVector autoMirrored() const;
    bool autoMirror() const;

    friend bool operator==(const ImageVector& a, const ImageVector& b) { return a.data_ == b.data_; }

private:
    struct Data;
    std::shared_ptr<const Data> data_;
    // `brush` through x' = m0 x + m2 y + m4, y' = m1 x + m3 y + m5, its far edges and default
    // center taken on `viewport` first (a group's transform, a mirror). A sweep isn't turned.
    static Brush mapBrush(const Brush& brush, const std::array<float, 6>& m, Size viewport);
};

// Decoded RGBA pixels (8 bits per channel, straight alpha). Load one with imageResource() or
// build one in code with fromPixels().
class ImageBitmap {
public:
    ImageBitmap() = default;
    static ImageBitmap fromPixels(int width, int height, std::vector<uint8_t> rgba);

    bool empty() const { return !data_; }
    int width() const;
    int height() const;
    const uint8_t* pixels() const;
    uint64_t id() const;
    // Lets backends notice when the image is gone and free its texture.
    std::weak_ptr<const void> token() const { return data_; }

    friend bool operator==(const ImageBitmap& a, const ImageBitmap& b) { return a.data_ == b.data_; }

private:
    struct Data;
    std::shared_ptr<const Data> data_;
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

    // Its outline in `size` (dp, from the top left), mirrored right to left where it has a start
    // and an end (Compose's createOutline).
    Outline createOutline(Size size, LayoutDirection layoutDirection) const;
    // A rounded rect's corners for a rect, or a polygon's or path's (not one: those of a shape
    // drawn as a path).
    bool pathed() const { return kind == Kind::Outline || kind == Kind::Cut || kind == Kind::Generic; }
    constexpr float radiusFor(const Rect& r) const {  // one radius (corners of their own: the largest)
        float shorter = std::min(r.width, r.height);
        return kind == Kind::Circle    ? shorter / 2
               : kind == Kind::Cut     ? 0
               : perCorner             ? radiiFor(r).max()
               : kind == Kind::Rounded ? std::min(radius + percent / 100 * shorter, shorter / 2)
                                       : 0;
    }
    // Each corner's radius in `r`. As in Compose, a side's two corners together fit in the
    // shorter side (both shrink if they wouldn't).
    constexpr CornerRadii radiiFor(const Rect& r, bool rtl = false) const {
        if (!perCorner || kind != Kind::Rounded) return CornerRadii(radiusFor(r));
        return cornersFor(r, rtl && !absolute);
    }
    // Its own corners (`corners`) in `r`, a side's two fitting in the shorter side; right to left,
    // the start ones on the right.
    constexpr CornerRadii cornersFor(const Rect& r, bool rtl) const {
        float shorter = std::min(r.width, r.height);
        auto px = [shorter](CornerSize c) { return std::max(0.f, c.dp + c.percent / 100 * shorter); };
        float ts = px(corners[0]), te = px(corners[1]), be = px(corners[2]), bs = px(corners[3]);
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

class Brush {
public:
    enum class Kind { Solid, Linear, Radial, Sweep };

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

    // This gradient sx, sy times as large (a vector's viewport units on its bounds); "the far
    // edge" stays the far edge, a color stays as it is.
    Brush scaled(float sx, float sy) const;

    Kind kind() const { return kind_; }
    bool isSolid() const { return kind_ == Kind::Solid; }
    const std::vector<ColorStop>& stops() const { return stops_; }
    TileMode tileMode() const { return tileMode_; }
    // The color at absolute point `p` when painting `area` (absolute).
    Color colorAt(Offset p, const Rect& area) const { return colorAtParameter(parameterAt(p, area)); }
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

    friend bool operator==(const Brush& a, const Brush& b) {
        return a.kind_ == b.kind_ && a.stops_ == b.stops_ && a.start_.x == b.start_.x && a.start_.y == b.start_.y &&
               a.end_.x == b.end_.x && a.end_.y == b.end_.y && a.hasCenter_ == b.hasCenter_ && a.radius_ == b.radius_ &&
               a.tileMode_ == b.tileMode_;
    }

private:
    friend class ImageVector;  // a vector's groups and its mirror move its gradients
    friend class ColorFilter;  // a filter recolors its stops
    Brush(Kind kind, std::vector<ColorStop> stops) : kind_(kind), stops_(std::move(stops)) {}
    static std::vector<ColorStop> evenly(const std::vector<Color>& colors);
    Kind kind_;
    std::vector<ColorStop> stops_;
    Offset start_, end_;  // linear: endpoints; radial/sweep: start_ is the center
    bool hasCenter_ = false;
    float radius_ = kInfinity;
    TileMode tileMode_ = TileMode::Clamp;
};

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
    // The effect for a layer of `size` (dp), the pixels past its edges as `edgeTreatment` says
    // (Decal: none; any other: the edges', as Compose's takes them).
    RenderEffect createRenderEffect(Size size, TileMode edgeTreatment = TileMode::Clamp) const;
    friend bool operator==(const BlurRadiusSpec&, const BlurRadiusSpec&) = default;

private:
    enum class Kind : uint8_t { Uniform, Vertical, Horizontal, Linear, Radial };
    BlurRadiusSpec(Kind kind, std::vector<BlurStop> stops);
    Kind kind_;
    std::vector<BlurStop> stops_;  // (uniform: its radius, the one)
    Offset start_, end_;
    std::optional<Offset> center_;
    std::optional<float> fallOff_;
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

class Path {
public:
    enum class FillType { NonZero, EvenOdd };
    FillType fillType = FillType::NonZero;

    void moveTo(float x, float y);
    void lineTo(float x, float y);
    void relativeMoveTo(float dx, float dy) { moveTo(current_.x + dx, current_.y + dy); }
    void relativeLineTo(float dx, float dy) { lineTo(current_.x + dx, current_.y + dy); }
    void quadraticTo(float x1, float y1, float x2, float y2);
    void cubicTo(float x1, float y1, float x2, float y2, float x3, float y3);
    void relativeQuadraticTo(float dx1, float dy1, float dx2, float dy2) {
        quadraticTo(current_.x + dx1, current_.y + dy1, current_.x + dx2, current_.y + dy2);
    }
    void relativeCubicTo(float dx1, float dy1, float dx2, float dy2, float dx3, float dy3) {
        cubicTo(current_.x + dx1, current_.y + dy1, current_.x + dx2, current_.y + dy2, current_.x + dx3, current_.y + dy3);
    }
    // Part of the oval in `oval` (Compose's arcTo): from `startAngleDegrees` (0 at the right,
    // clockwise) through `sweepAngleDegrees`, joined to what is there by a line unless
    // forceMoveTo (or it is the first thing) starts it on its own.
    void arcTo(const Rect& oval, float startAngleDegrees, float sweepAngleDegrees, bool forceMoveTo);
    void addArc(const Rect& oval, float startAngleDegrees, float sweepAngleDegrees);  // a contour of its own
    void close();
    void addRect(const Rect& r);
    void addOval(const Rect& r);
    void addRoundRect(const Rect& r, float radius);
    void addRoundRect(const RoundRect& r);  // its corners quarter ellipses
    void addPath(const Path& path, Offset offset = {});  // its contours, moved by `offset`
    void translate(Offset offset);                       // every point moved
    void reset();

    bool isEmpty() const { return commands_.empty(); }
    Rect bounds() const;  // of all points, control points included
    const std::vector<PathCommand>& commands() const { return commands_; }
    // Whether a point is inside, by its fill type (open contours count as closed; curves as fine
    // polylines).
    bool contains(Offset p) const;
    // This path becomes `a` combined with `b` (each by its fill type), as closed polylines with
    // what is inside on their left (curves as fine polylines); false if nothing is left.
    bool op(const Path& a, const Path& b, PathOperation operation);
    static Path combine(PathOperation operation, const Path& a, const Path& b);
    friend Path operator+(const Path& a, const Path& b) { return combine(PathOperation::Union, a, b); }
    friend Path operator-(const Path& a, const Path& b) { return combine(PathOperation::Difference, a, b); }
    friend Path operator|(const Path& a, const Path& b) { return combine(PathOperation::Union, a, b); }
    friend Path operator&(const Path& a, const Path& b) { return combine(PathOperation::Intersect, a, b); }
    friend Path operator^(const Path& a, const Path& b) { return combine(PathOperation::Xor, a, b); }
    friend bool operator==(const Path& a, const Path& b);

private:
    std::vector<PathCommand> commands_;
    Offset current_, start_;
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

struct PathStroke {
    float width = 0;  // 0: fill
    StrokeCap cap = StrokeCap::Butt;
    StrokeJoin join = StrokeJoin::Miter;
    float miter = 4;
};

// One path of a vector in several paints (Compose's VectorPath), in viewport units: filled with
// `fill` (unset: not) and stroked with `stroke` `strokeLineWidth` wide (unset or 0: not). A
// gradient's offsets are in the drawn vector's dp, from its top left.
struct VectorPath {
    std::vector<PathCommand> commands;
    std::optional<Brush> fill = Brush::solid(colors::Black);
    float fillAlpha = 1;
    std::optional<Brush> stroke;
    float strokeAlpha = 1, strokeLineWidth = 0;
    StrokeCap strokeLineCap = StrokeCap::Butt;
    StrokeJoin strokeLineJoin = StrokeJoin::Miter;
    float strokeLineMiter = 4;
    Path::FillType pathFillType = Path::FillType::NonZero;
    std::vector<std::vector<PathCommand>> clipPaths;  // its groups' (viewport units, outermost first): drawn inside all of them
    // Only this piece of it drawn: from trimPathStart to trimPathEnd (fractions of its length,
    // every contour's together), both moved on by trimPathOffset, wrapping round its end.
    float trimPathStart = 0, trimPathEnd = 1, trimPathOffset = 0;
    static std::vector<PathCommand> parse(std::string_view pathData);  // SVG path data
    friend bool operator==(const VectorPath&, const VectorPath&) = default;
};

// A group of a vector's paths (Compose's ImageVector.Builder.addGroup): what is in it scaled and
// turned about its pivot, then moved, and clipped to its clip path (in the group's own units).
struct VectorGroup {
    float rotate = 0, pivotX = 0, pivotY = 0, scaleX = 1, scaleY = 1, translationX = 0, translationY = 0;
    std::string clipPathData;  // SVG path data; empty: not clipped
};

// Builds a vector of paths in paints of their own, drawn in order (Compose's ImageVector.Builder):
//   ImageVector badge = ImageVector::Builder(48, 48, 24, 24)
//       .path("M12,2a10,10 0 1,0 0.01,0z", Brush::solid(Color::hex(0x1E88E5)))
//       .path("M7,12l3,3l7,-7", std::nullopt, Brush::solid(colors::White), 2)
//       .build();
//   Image(VectorPainter(badge), "Onaylı");   // its own colors; Icon(badge, ...) tints it
// Paths can go in groups, nested, that move, scale, turn and clip them (their gradients and stroke
// widths too):
//   .group({.rotate = 45, .pivotX = 12, .pivotY = 12, .clipPathData = "M2,12a10,10 0 1,0 20,0a10,10 0 1,0 -20,0z"},
//          [](ImageVector::Builder& b) { b.path("M0,10h24v4h-24z", Brush::solid(colors::White)); })
class ImageVector::Builder {
public:
    Builder(float defaultWidth, float defaultHeight, float viewportWidth, float viewportHeight);
    Builder& path(VectorPath path);
    Builder& path(std::string_view pathData, std::optional<Brush> fill, std::optional<Brush> stroke = std::nullopt,
                  float strokeLineWidth = 0);
    // The paths after addGroup go in that group (inside the open ones) until its clearGroup.
    Builder& addGroup(const VectorGroup& group = {});
    Builder& clearGroup();
    template <class Content>
    Builder& group(const VectorGroup& group, Content&& content) {
        addGroup(group);
        content(*this);
        return clearGroup();
    }
    Builder& autoMirror(bool on = true);  // mirrored in a right-to-left layout (see ImageVector::autoMirrored)
    ImageVector build() const;

private:
    struct Group {
        std::array<float, 6> transform{1, 0, 0, 1, 0, 0};  // the open groups' together (see ImageVector::mapBrush)
        std::vector<std::vector<PathCommand>> clips;      // theirs, in viewport units
    };
    float defaultWidth_, defaultHeight_, viewportWidth_, viewportHeight_;
    bool autoMirror_ = false;
    std::vector<VectorPath> paths_;
    std::vector<Group> groups_;
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
namespace detail {
// Draws `vector` into `bounds`: a one-color one (or a tinted one without strokes) as the
// backend's cached raster in `tint` (black unset), one in several paints path by path, all in
// `tint` when one is given; `alpha` over it.
void drawImageVector(GraphicsContext& canvas, const ImageVector& vector, const Rect& bounds, std::optional<Color> tint, float alpha = 1);
} // namespace detail

// How stampedPathEffect lays each copy of its shape on the path: moved there, also turned the
// way the path goes, or bent along it.
enum class StampedPathEffectStyle { Translate, Rotate, Morph };

// What a stroke does to its path first (Compose's PathEffect): dashes (on, off, on… dp, from
// `phase` into the pattern, starting again on each contour), rounded corners, copies of a shape
// stamped along it (filled, instead of the stroke), or rounded corners and then one of the others
// (chainPathEffect).
struct PathEffect {
    std::array<float, 8> intervals{};
    int count = 0;
    float phase = 0;
    float cornerRadius = 0;
    std::optional<Path> stamp;  // its origin goes on the path
    float advance = 0;          // dp from one stamp to the next
    StampedPathEffectStyle stampStyle = StampedPathEffectStyle::Translate;

    static PathEffect dashPathEffect(std::initializer_list<float> intervals, float phase = 0);
    static PathEffect cornerPathEffect(float radius);
    // `shape` every `advance` dp along each contour; a phase of p puts the first one at advance - p
    // (as Skia's).
    static PathEffect stampedPathEffect(const Path& shape, float advance, float phase, StampedPathEffectStyle style);
    static PathEffect chainPathEffect(const PathEffect& outer, const PathEffect& inner);
    Path apply(const Path& path) const;  // the path the stroke then draws (or fills, stamped)
    bool fills() const { return stamp && advance > 0; }  // its path is filled, not stroked
    friend bool operator==(const PathEffect&, const PathEffect&) = default;
};

// Fill, or Stroke(width) to outline (dashed or with rounded corners through a PathEffect).
struct DrawStyle {
    float strokeWidth = 0;  // 0: fill
    StrokeCap cap = StrokeCap::Butt;
    StrokeJoin join = StrokeJoin::Miter;
    std::optional<PathEffect> pathEffect;
    constexpr bool isStroke() const { return strokeWidth > 0; }
    friend bool operator==(const DrawStyle&, const DrawStyle&) = default;
};
inline constexpr DrawStyle Fill{};
constexpr DrawStyle Stroke(float width, StrokeCap cap = StrokeCap::Butt, StrokeJoin join = StrokeJoin::Miter,
                           std::optional<PathEffect> pathEffect = {}) {
    return {width, cap, join, pathEffect};
}

// Lengths along a path (Compose's PathMeasure), contour by contour (curves measured as fine
// polylines): how long the current one is, where a distance along it lands and which way it
// goes there, and the piece between two distances.
class PathMeasure {
public:
    PathMeasure() = default;
    explicit PathMeasure(const Path& path, bool forceClosed = false) { setPath(path, forceClosed); }
    void setPath(const Path& path, bool forceClosed = false);
    float length() const;  // of the current contour (the first, until nextContour())
    Offset getPosition(float distance) const;
    Offset getTangent(float distance) const;  // a unit vector
    // Adds the piece from startDistance to stopDistance to `destination` (beginning with a
    // moveTo, or a lineTo from where it was); false if there is none.
    bool getSegment(float startDistance, float stopDistance, Path& destination, bool startWithMoveTo = true) const;
    bool nextContour();  // moves on to the next contour, if there is one

private:
    struct Contour {
        std::vector<Offset> points;
        std::vector<float> lengths;  // from the start to each point
    };
    std::vector<Contour> contours_;
    size_t current_ = 0;
};

// A shadow (Compose's Shadow, for text and, with spread, for dropShadow/innerShadow): its
// color, how far it is moved and how much it is blurred (dp; Compose's text shadows count
// pixels), and how much bigger (a drop shadow) or smaller (an inner one's hole) the shape is.
struct Shadow {
    Color color{0, 0, 0, 1};
    Offset offset;
    float blurRadius = 0;
    float spread = 0;  // not for text
    friend bool operator==(const Shadow& a, const Shadow& b) {
        return a.color == b.color && a.offset.x == b.offset.x && a.offset.y == b.offset.y && a.blurRadius == b.blurRadius &&
               a.spread == b.spread;
    }
};

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
    // Fills `vector` scaled to `bounds`.
    virtual void drawVector(const ImageVector& vector, const Rect& bounds, Color color) = 0;
    // Strokes part of the circle inscribed in `oval`. Angles in degrees, 0 = 3 o'clock, clockwise.
    virtual void drawArc(const Rect& oval, float startDegrees, float sweepDegrees, float strokeWidth, Color color,
                         bool roundCap) = 0;
    // Draws the `src` pixel rect of `image` into `dst`.
    virtual void drawImage(const ImageBitmap& image, const Rect& src, const Rect& dst, float alpha) = 0;
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
    // Fills (stroke.width == 0) or strokes `path` placed at `origin`.
    virtual void drawPath(const Path& path, Offset origin, const Brush& brush, const Rect& brushArea,
                          const PathStroke& stroke) = 0;
    // A rounded rect's shadow: under it (spread out, moved, blurred), or inside it (`inner`: the
    // color where a hole of the rect shrunk by spread and moved isn't, blurred, inside the rect).
    // The default draws a drop shadow as rings fading out, and no inner shadow.
    virtual void drawShadow(const Rect& rect, const CornerRadii& radii, const Shadow& shadow, bool inner) {
        if (inner || shadow.color.a <= 0) return;
        for (int i = 6; i >= 1; --i) {
            float t = float(i) / 6, grow = shadow.spread + shadow.blurRadius * t;
            Rect r{rect.x - grow + shadow.offset.x, rect.y - grow + shadow.offset.y, rect.width + 2 * grow, rect.height + 2 * grow};
            if (r.width > 0 && r.height > 0) fillRoundRect(r, radii.grown(grow), shadow.color.copy(shadow.color.a * (1.f - t + 1.f / 6) / 3.5f));
        }
    }
    // The same for a shape of any outline (`outline` from `origin`), as Compose draws one: under it,
    // the outline filled and stroked 2 * spread wide (mitered), each blurred, the two over each
    // other, moved; inside it, the color where a hole (the outline worn away by the spread, moved)
    // blurred isn't. The default draws it through effect layers (pushEffectLayer).
    virtual void drawShadowPath(const Path& outline, Offset origin, const Shadow& shadow, bool inner);
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
    void drawShapeShadow(const Rect& rect, const Shape& shape, bool rtl, const Shadow& shadow, bool inner);
    // Around what each layout node draws: a recording canvas lines frames up by them (see
    // display_list.hpp); others ignore them.
    virtual void beginNode(const void* node) { (void)node; }
    virtual void endNode() {}
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
