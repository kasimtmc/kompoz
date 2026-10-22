#include <kompoz/graphics.hpp>

#include "curves.hpp"

#include <algorithm>
#include <cmath>
#include <mutex>
#include <numbers>

namespace kompoz {

namespace {
std::mutex gFamilyMutex;
std::vector<std::vector<FontFile>> gFamilies;  // id - 16
} // namespace

FontFamily FontFamily::fromFiles(std::vector<FontFile> files) {
    std::lock_guard lock(gFamilyMutex);
    for (size_t i = 0; i < gFamilies.size(); ++i)
        if (gFamilies[i] == files) return {uint16_t(16 + i)};
    gFamilies.push_back(std::move(files));
    return {uint16_t(16 + gFamilies.size() - 1)};
}

int FontWeight::closest(const std::vector<FontWeight>& available, FontWeight wanted) {
    // CSS Fonts 4, font-weight matching: how far down the order of preference each weight is.
    int w = wanted.weight;
    auto rank = [w](int a) {
        if (a == w) return 0;
        if (w >= 400 && w <= 500) {
            if (a > w && a <= 500) return a - w;          // up to 500 first
            if (a < w) return 1000 + (w - a);             // then lighter, the nearest first
            return 2000 + (a - w);                        // then heavier than 500
        }
        if (w < 400) return a < w ? w - a : 1000 + (a - w);  // lighter first
        return a > w ? a - w : 1000 + (w - a);               // heavier first
    };
    int best = -1;
    for (size_t i = 0; i < available.size(); ++i)
        if (best < 0 || rank(available[i].weight) < rank(available[size_t(best)].weight)) best = int(i);
    return best;
}

std::vector<FontVariation::Setting> FontVariation::settings(FontWeight weight, FontStyle style, std::vector<Setting> more) {
    std::vector<Setting> all{FontVariation::weight(weight.weight), italic(style == FontStyle::Italic ? 1.f : 0.f)};
    all.insert(all.end(), std::make_move_iterator(more.begin()), std::make_move_iterator(more.end()));
    return all;
}

std::vector<FontFile> FontFamily::files(uint16_t id) {
    std::lock_guard lock(gFamilyMutex);
    return id >= 16 && size_t(id - 16) < gFamilies.size() ? gFamilies[size_t(id - 16)] : std::vector<FontFile>{};
}

namespace {
std::mutex gFeaturesMutex;
std::vector<std::string> gFeatures;  // id - 1
} // namespace

uint16_t FontFeatures::id(const std::string& settings) {
    if (settings.empty()) return 0;
    std::lock_guard lock(gFeaturesMutex);
    for (size_t i = 0; i < gFeatures.size(); ++i)
        if (gFeatures[i] == settings) return uint16_t(i + 1);
    gFeatures.push_back(settings);
    return uint16_t(gFeatures.size());
}

std::string FontFeatures::settings(uint16_t id) {
    std::lock_guard lock(gFeaturesMutex);
    return id > 0 && size_t(id) <= gFeatures.size() ? gFeatures[size_t(id - 1)] : std::string();
}

std::vector<ColorStop> Brush::evenly(const std::vector<Color>& colors) {
    std::vector<ColorStop> stops;
    for (size_t i = 0; i < colors.size(); ++i)
        stops.push_back({colors.size() > 1 ? float(i) / float(colors.size() - 1) : 0.f, colors[i]});
    return stops;
}

Brush Brush::linearGradient(std::vector<Color> colors, Offset start, Offset end, TileMode tileMode) {
    Brush b = linearGradient(evenly(colors), start, end, tileMode);
    b.explicitStops_ = false;
    return b;
}

Brush Brush::linearGradient(std::vector<ColorStop> stops, Offset start, Offset end, TileMode tileMode) {
    Brush b(Kind::Linear, std::move(stops));
    b.start_ = start;
    b.end_ = end;
    b.tileMode_ = tileMode;
    return b;
}

Brush Brush::horizontalGradient(std::vector<Color> colors, float startX, float endX, TileMode tileMode) {
    return linearGradient(std::move(colors), {startX, 0}, {endX, 0}, tileMode);
}

Brush Brush::verticalGradient(std::vector<Color> colors, float startY, float endY, TileMode tileMode) {
    return linearGradient(std::move(colors), {0, startY}, {0, endY}, tileMode);
}

Brush Brush::radialGradient(std::vector<Color> colors, std::optional<Offset> center, float radius, TileMode tileMode) {
    Brush b = radialGradient(evenly(colors), center, radius, tileMode);
    b.explicitStops_ = false;
    return b;
}

Brush Brush::radialGradient(std::vector<ColorStop> stops, std::optional<Offset> center, float radius, TileMode tileMode) {
    Brush b(Kind::Radial, std::move(stops));
    b.hasCenter_ = center.has_value();
    b.start_ = center.value_or(Offset{});
    b.radius_ = radius;
    b.tileMode_ = tileMode;
    return b;
}

Brush Brush::sweepGradient(std::vector<Color> colors, std::optional<Offset> center) {
    Brush b = sweepGradient(evenly(colors), center);
    b.explicitStops_ = false;
    return b;
}

Brush Brush::sweepGradient(std::vector<ColorStop> stops, std::optional<Offset> center) {
    Brush b(Kind::Sweep, std::move(stops));
    b.hasCenter_ = center.has_value();
    b.start_ = center.value_or(Offset{});
    return b;
}

Brush Brush::faded(float alpha) const {
    Brush b = *this;
    if (alpha < 1)
        for (ColorStop& s : b.stops_) s.color = s.color.copy(s.color.alpha() * std::max(alpha, 0.f));
    if (alpha < 1 && kind_ == Kind::Shader) b.shaderAlpha_ *= std::max(alpha, 0.f);
    return b;
}

struct Brush::ShaderSource {
    std::function<Shader(Size)> make;  // a brush of its own
    std::optional<Size> intrinsicSize;
    std::optional<Brush> dst, src;  // or the two it composites
    BlendMode blendMode = BlendMode::SrcOver;
    mutable std::optional<Size> madeFor;  // the size `make` made `made` for (made again only for another, as Compose's)
    mutable Shader made;
    std::optional<Shader> fixed;  // the one shader whatever the size (Brush::shader(Shader)): what it is equal by
};

Brush Brush::shader(std::function<Shader(Size size)> createShader, std::optional<Size> intrinsicSize) {
    Brush b(Kind::Shader, {});
    b.shader_ = std::make_shared<const ShaderSource>(ShaderSource{std::move(createShader), intrinsicSize, std::nullopt, std::nullopt, BlendMode::SrcOver, {}, {}, std::nullopt});
    return b;
}

Brush Brush::shader(Shader shader) {
    Brush b(Kind::Shader, {});
    b.shader_ = std::make_shared<const ShaderSource>(
        ShaderSource{[shader](Size) { return shader; }, std::nullopt, std::nullopt, std::nullopt, BlendMode::SrcOver, {}, {}, shader});
    return b;
}

Brush Brush::composite(const Brush& dst, const Brush& src, BlendMode blendMode) {
    Brush b(Kind::Shader, {});
    b.shader_ = std::make_shared<const ShaderSource>(ShaderSource{{}, std::nullopt, dst, src, blendMode, {}, {}, std::nullopt});
    return b;
}

Brush Brush::withTransform(const std::optional<Matrix>& transform) const {
    Brush b = *this;
    b.transform_ = transform && !transform->isIdentity() && kind_ != Kind::Solid ? std::make_shared<const Matrix>(*transform) : nullptr;
    return b;
}

Size Brush::intrinsicSize() const {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    switch (kind_) {
    case Kind::Linear:
        return {std::isfinite(start_.x) && std::isfinite(end_.x) ? std::abs(start_.x - end_.x) : nan,
                std::isfinite(start_.y) && std::isfinite(end_.y) ? std::abs(start_.y - end_.y) : nan};
    case Kind::Radial: return std::isfinite(radius_) ? Size{radius_ * 2, radius_ * 2} : Size{nan, nan};
    case Kind::Shader: return shader_ && shader_->intrinsicSize ? *shader_->intrinsicSize : Size{nan, nan};
    default: return {nan, nan};
    }
}

namespace {

Shader gradientShader(Shader::Kind kind, std::vector<ColorStop> stops, Offset start, Offset end, float radius, TileMode tile);

} // namespace

Shader Brush::createShader(Size size) const {
    Shader s;
    Rect area{0, 0, size.width, size.height};
    switch (kind_) {
    case Kind::Solid: {  // (Compose's: a vertical gradient of the one color)
        Color c = stops_.empty() ? colors::Transparent : stops_.front().color;
        s = gradientShader(Shader::Kind::Linear, {{0, c}, {1, c}}, {0, 0}, {0, size.height}, 0, TileMode::Clamp);
        break;
    }
    case Kind::Linear: {
        Geometry g = geometry(area);
        s = gradientShader(Shader::Kind::Linear, stops_, g.start, g.end, 0, tileMode_);
        break;
    }
    case Kind::Radial: {
        Geometry g = geometry(area);
        s = gradientShader(Shader::Kind::Radial, stops_, g.start, {}, g.radius, tileMode_);
        break;
    }
    case Kind::Sweep: s = gradientShader(Shader::Kind::Sweep, stops_, geometry(area).start, {}, 0, TileMode::Clamp); break;
    case Kind::Shader:
        if (shader_->make) {
            if (shader_->madeFor != size) shader_->made = shader_->make(size), shader_->madeFor = size;
            s = shader_->made;
        } else s = CompositeShader(shader_->dst->createShader(size), shader_->src->createShader(size), shader_->blendMode);
        break;
    }
    return transform_ && s.data_ ? s.transformed(*transform_) : s;
}

Color Brush::colorAt(Offset p, const Rect& area) const {
    if (kind_ == Kind::Shader || (transform_ && kind_ != Kind::Solid)) {
        Color c = createShader({area.width, area.height}).colorAt({p.x - area.x, p.y - area.y});
        return c.copy(c.alpha() * shaderAlpha_);
    }
    return colorAtParameter(parameterAt(p, area));
}

bool operator==(const Brush& a, const Brush& b) {
    if (a.kind_ != b.kind_ || a.stops_ != b.stops_ || a.start_.x != b.start_.x || a.start_.y != b.start_.y || a.end_.x != b.end_.x ||
        a.end_.y != b.end_.y || a.hasCenter_ != b.hasCenter_ || a.radius_ != b.radius_ || a.tileMode_ != b.tileMode_ ||
        a.shaderAlpha_ != b.shaderAlpha_)
        return false;
    if ((a.transform_ != nullptr) != (b.transform_ != nullptr) || (a.transform_ && *a.transform_ != *b.transform_)) return false;
    if (a.shader_ == b.shader_) return true;
    if (!a.shader_ || !b.shader_) return false;
    // One shader whatever the size compares by it; composites by what they composite (Compose's
    // CompositeShaderBrush); brushes of your own only to themselves.
    if (a.shader_->fixed || b.shader_->fixed) return a.shader_->fixed == b.shader_->fixed;
    return !a.shader_->make && !b.shader_->make && a.shader_->dst == b.shader_->dst && a.shader_->src == b.shader_->src &&
           a.shader_->blendMode == b.shader_->blendMode;
}

namespace {

std::vector<Color> colorsOf(const std::vector<ColorStop>& stops) {
    std::vector<Color> out;
    for (const ColorStop& s : stops) out.push_back(s.color);
    return out;
}

// Two lists as long as the longer, mixed index by index (the shorter's last again past its end).
template <class T, class Mix>
std::vector<T> mixedLists(const std::vector<T>& a, const std::vector<T>& b, Mix mix) {
    std::vector<T> out;
    if (a.empty() || b.empty()) return a.empty() ? b : a;
    for (size_t i = 0; i < std::max(a.size(), b.size()); ++i) out.push_back(mix(a[std::min(i, a.size() - 1)], b[std::min(i, b.size() - 1)]));
    return out;
}

} // namespace

Brush lerp(const Brush& a, const Brush& b, float t) {
    if (a == b) return t < 0.5f ? a : b;
    // `from` mixed `u` of the way to `to`, if `from` can be (Compose's Interpolatable.lerp of each kind).
    auto mixed = [](const Brush& from, const Brush& to, float u) -> std::optional<Brush> {
        auto mixf = [u](float x, float y) { return x + (y - x) * u; };
        auto colorOf = [](const Brush& c) { return c.stops().empty() ? colors::Transparent : c.stops().front().color; };
        if (from.isSolid()) {
            if (to.isSolid()) return Brush::solid(lerp(colorOf(from), colorOf(to), u));
            return std::nullopt;
        }
        if (from.kind() != Brush::Kind::Linear && from.kind() != Brush::Kind::Radial && from.kind() != Brush::Kind::Sweep) return std::nullopt;
        Brush other = to;
        if (to.isSolid()) {  // a color: `from` in that color
            other = from;
            for (ColorStop& s : other.stops_) s.color = colorOf(to);
            other.transform_ = nullptr;
        } else if (to.kind() != from.kind()) {
            return std::nullopt;
        }
        std::vector<Color> colors = mixedLists(colorsOf(from.stops_), colorsOf(other.stops_), [&](Color x, Color y) { return lerp(x, y, u); });
        std::vector<ColorStop> stops;
        bool given = from.explicitStops_ && other.explicitStops_;
        if (given) {
            std::vector<float> fa, fb;
            for (const ColorStop& s : from.stops_) fa.push_back(s.offset);
            for (const ColorStop& s : other.stops_) fb.push_back(s.offset);
            std::vector<float> offsets = mixedLists(fa, fb, mixf);
            for (size_t i = 0; i < colors.size(); ++i) stops.push_back({offsets[std::min(i, offsets.size() - 1)], colors[i]});
        } else {
            stops = Brush::evenly(colors);
        }
        Brush out(from.kind(), std::move(stops));
        out.explicitStops_ = given;
        out.tileMode_ = u < 0.5f ? from.tileMode_ : other.tileMode_;
        auto finite = [](Offset o) { return std::isfinite(o.x) && std::isfinite(o.y); };
        auto safe = [&](Offset x, Offset y) { return finite(x) && finite(y) ? Offset{mixf(x.x, y.x), mixf(x.y, y.y)} : u < 0.5f ? x : y; };
        if (from.hasCenter_ == other.hasCenter_) out.start_ = safe(from.start_, other.start_), out.hasCenter_ = from.hasCenter_;  // (unset centers switch halfway)
        else out.start_ = u < 0.5f ? from.start_ : other.start_, out.hasCenter_ = u < 0.5f ? from.hasCenter_ : other.hasCenter_;
        out.end_ = safe(from.end_, other.end_);
        out.radius_ = std::isfinite(from.radius_) && std::isfinite(other.radius_) ? mixf(from.radius_, other.radius_) : u < 0.5f ? from.radius_ : other.radius_;
        return out;
    };
    if (std::optional<Brush> r = mixed(a, b, t)) return *r;
    if (std::optional<Brush> r = mixed(b, a, 1 - t)) return *r;
    return t < 0.5f ? a : b;
}

Brush::Geometry Brush::geometry(const Rect& area) const {
    auto resolve = [&](float v, float size) { return std::isinf(v) ? size : v; };
    Offset corner{area.x, area.y};
    Offset center = hasCenter_ ? start_ : Offset{area.width / 2, area.height / 2};
    switch (kind_) {
    case Kind::Linear:
        return {{corner.x + resolve(start_.x, area.width), corner.y + resolve(start_.y, area.height)},
                {corner.x + resolve(end_.x, area.width), corner.y + resolve(end_.y, area.height)}};
    case Kind::Radial:
        return {{corner.x + center.x, corner.y + center.y}, {}, std::isinf(radius_) ? std::min(area.width, area.height) / 2 : radius_};
    case Kind::Sweep: return {{corner.x + center.x, corner.y + center.y}};
    case Kind::Solid:
    case Kind::Shader: break;
    }
    return {};
}

float Brush::parameterAt(Offset p, const Rect& area) const {
    if (transform_) {  // where the transform takes p from, in what it paints
        Matrix back = *transform_;
        back.invert();
        Offset q = back.map(Offset{p.x - area.x, p.y - area.y});
        p = {area.x + q.x, area.y + q.y};
    }
    Geometry g = geometry(area);
    switch (kind_) {
    case Kind::Linear: {
        float dx = g.end.x - g.start.x, dy = g.end.y - g.start.y, len2 = dx * dx + dy * dy;
        return len2 > 0 ? ((p.x - g.start.x) * dx + (p.y - g.start.y) * dy) / len2 : 0;
    }
    case Kind::Radial: return g.radius > 0 ? std::hypot(p.x - g.start.x, p.y - g.start.y) / g.radius : 0;
    case Kind::Sweep: {
        float a = std::atan2(p.y - g.start.y, p.x - g.start.x);
        if (a < 0) a += 2 * std::numbers::pi_v<float>;
        return a / (2 * std::numbers::pi_v<float>);
    }
    case Kind::Solid:
    case Kind::Shader: break;
    }
    return 0;
}

namespace {
Color colorAlong(const std::vector<ColorStop>& stops, TileMode tile, float t);
} // namespace

Color Brush::colorAtParameter(float t) const {
    if (stops_.empty()) return colors::Transparent;
    if (kind_ == Kind::Solid || stops_.size() == 1) return stops_.front().color;
    return colorAlong(stops_, kind_ == Kind::Sweep ? TileMode::Clamp : tileMode_, t);
}

// --- Shaders ------------------------------------------------------------------------------------

struct Shader::Data {
    Kind kind = Kind::Linear;
    std::vector<ColorStop> stops;
    TileMode tile = TileMode::Clamp, tileY = TileMode::Clamp;
    Offset start, end;
    float radius = 0;
    ImageBitmap image;
    std::optional<Shader> dst, src;
    BlendMode blendMode = BlendMode::SrcOver;
    std::optional<Matrix> transform, back;  // and its inverse
};

namespace {

Shader gradientShader(Shader::Kind kind, std::vector<ColorStop> stops, Offset start, Offset end, float radius, TileMode tile) {
    auto d = std::make_shared<Shader::Data>();
    d->kind = kind, d->stops = std::move(stops), d->start = start, d->end = end, d->radius = radius, d->tile = tile;
    Shader s;
    s.data_ = std::move(d);
    return s;
}

std::vector<ColorStop> stopsOf(const std::vector<Color>& colors, const std::vector<float>& offsets) {
    std::vector<ColorStop> out;
    for (size_t i = 0; i < colors.size(); ++i)
        out.push_back({offsets.size() == colors.size() ? offsets[i] : colors.size() > 1 ? float(i) / float(colors.size() - 1) : 0.f, colors[i]});
    return out;
}

// t along stops past their ends as `tile` says (Decal: nothing).
Color colorAlong(const std::vector<ColorStop>& stops, TileMode tile, float t) {
    if (stops.empty()) return colors::Transparent;
    if (stops.size() == 1) return stops.front().color;
    switch (tile) {
    case TileMode::Clamp: t = std::clamp(t, 0.f, 1.f); break;
    case TileMode::Repeated: t -= std::floor(t); break;
    case TileMode::Mirror:
        t = std::fmod(std::abs(t), 2.f);
        if (t > 1) t = 2 - t;
        break;
    case TileMode::Decal:
        if (t < 0 || t > 1) return colors::Transparent;
        break;
    }
    if (t <= stops.front().offset) return stops.front().color;
    for (size_t i = 1; i < stops.size(); ++i) {
        const ColorStop &a = stops[i - 1], &b = stops[i];
        if (t <= b.offset) {
            float span = b.offset - a.offset;
            // (Skia's: each component along, in sRGB, not premultiplied)
            Color x = detail::inSrgb(a.color), y = detail::inSrgb(b.color);
            float u = span > 0 ? (t - a.offset) / span : 1.f;
            return {lerp(x.red(), y.red(), u), lerp(x.green(), y.green(), u), lerp(x.blue(), y.blue(), u), lerp(x.alpha(), y.alpha(), u)};
        }
    }
    return stops.back().color;
}

// Where index i of n falls past the image's edge as `tile` says; -1 for none (Decal).
int tiled(int i, int n, TileMode tile) {
    if (i >= 0 && i < n) return i;
    switch (tile) {
    case TileMode::Clamp: return std::clamp(i, 0, n - 1);
    case TileMode::Repeated: return ((i % n) + n) % n;
    case TileMode::Mirror: {
        int m = ((i % (2 * n)) + 2 * n) % (2 * n);
        return m < n ? m : 2 * n - 1 - m;
    }
    case TileMode::Decal: return -1;
    }
    return -1;
}

} // namespace

Shader LinearGradientShader(Offset from, Offset to, std::vector<Color> colors, std::vector<float> colorStops, TileMode tileMode) {
    return gradientShader(Shader::Kind::Linear, stopsOf(colors, colorStops), from, to, 0, tileMode);
}

Shader RadialGradientShader(Offset center, float radius, std::vector<Color> colors, std::vector<float> colorStops, TileMode tileMode) {
    return gradientShader(Shader::Kind::Radial, stopsOf(colors, colorStops), center, {}, radius, tileMode);
}

Shader SweepGradientShader(Offset center, std::vector<Color> colors, std::vector<float> colorStops) {
    return gradientShader(Shader::Kind::Sweep, stopsOf(colors, colorStops), center, {}, 0, TileMode::Clamp);
}

Shader ImageShader(ImageBitmap image, TileMode tileModeX, TileMode tileModeY) {
    auto d = std::make_shared<Shader::Data>();
    d->kind = Shader::Kind::Image, d->image = std::move(image), d->tile = tileModeX, d->tileY = tileModeY;
    Shader s;
    s.data_ = std::move(d);
    return s;
}

Shader CompositeShader(Shader dst, Shader src, BlendMode blendMode) {
    auto d = std::make_shared<Shader::Data>();
    d->kind = Shader::Kind::Composite, d->dst = std::move(dst), d->src = std::move(src), d->blendMode = blendMode;
    Shader s;
    s.data_ = std::move(d);
    return s;
}

Shader::Kind Shader::kind() const { return data_ ? data_->kind : Kind::Linear; }
const std::vector<ColorStop>& Shader::stops() const {
    static const std::vector<ColorStop> none;
    return data_ ? data_->stops : none;
}
TileMode Shader::tileMode() const { return data_ ? data_->tile : TileMode::Clamp; }
TileMode Shader::tileModeY() const { return data_ ? data_->tileY : TileMode::Clamp; }
Offset Shader::start() const { return data_ ? data_->start : Offset{}; }
Offset Shader::end() const { return data_ ? data_->end : Offset{}; }
float Shader::radius() const { return data_ ? data_->radius : 0; }
const ImageBitmap& Shader::image() const {
    static const ImageBitmap none;
    return data_ ? data_->image : none;
}
const Shader& Shader::dst() const {
    static const Shader none;
    return data_ && data_->dst ? *data_->dst : none;
}
const Shader& Shader::src() const {
    static const Shader none;
    return data_ && data_->src ? *data_->src : none;
}
BlendMode Shader::blendMode() const { return data_ ? data_->blendMode : BlendMode::SrcOver; }
const Matrix* Shader::transform() const { return data_ && data_->transform ? &*data_->transform : nullptr; }

Shader Shader::transformed(const Matrix& matrix) const {
    if (!data_ || matrix.isIdentity()) return *this;
    auto d = std::make_shared<Data>(*data_);
    Matrix m = matrix;
    if (d->transform) m = Matrix(*d->transform), m *= matrix;  // (its own first, then this one)
    d->transform = m;
    Matrix back = m;
    back.invert();
    d->back = back;
    Shader s;
    s.data_ = std::move(d);
    return s;
}

Color Shader::colorAt(Offset p) const {
    if (!data_) return colors::Transparent;
    const Data& d = *data_;
    if (d.back) p = d.back->map(p);
    switch (d.kind) {
    case Kind::Linear: {
        float dx = d.end.x - d.start.x, dy = d.end.y - d.start.y, len2 = dx * dx + dy * dy;
        return colorAlong(d.stops, d.tile, len2 > 0 ? ((p.x - d.start.x) * dx + (p.y - d.start.y) * dy) / len2 : 0);
    }
    case Kind::Radial: return colorAlong(d.stops, d.tile, d.radius > 0 ? std::hypot(p.x - d.start.x, p.y - d.start.y) / d.radius : 0);
    case Kind::Sweep: {
        float a = std::atan2(p.y - d.start.y, p.x - d.start.x);
        if (a < 0) a += 2 * std::numbers::pi_v<float>;
        return colorAlong(d.stops, TileMode::Clamp, a / (2 * std::numbers::pi_v<float>));
    }
    case Kind::Image: {  // bilinearly, from the four pixels round p (premultiplied), each tiled
        const ImageBitmap& img = d.image;
        if (img.empty()) return colors::Transparent;
        int w = img.width(), h = img.height();
        float u = p.x - 0.5f, v = p.y - 0.5f;
        int x0 = int(std::floor(u)), y0 = int(std::floor(v));
        float fx = u - float(x0), fy = v - float(y0);
        float sum[4] = {0, 0, 0, 0};
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i) {
                int x = tiled(x0 + i, w, d.tile), y = tiled(y0 + j, h, d.tileY);
                if (x < 0 || y < 0) continue;
                const uint8_t* q = img.pixels() + (size_t(y) * size_t(w) + size_t(x)) * 4;
                float wgt = (i ? fx : 1 - fx) * (j ? fy : 1 - fy), a = q[3] / 255.f;
                for (int c = 0; c < 3; ++c) sum[c] += q[c] / 255.f * a * wgt;
                sum[3] += a * wgt;
            }
        return sum[3] > 0 ? Color{sum[0] / sum[3], sum[1] / sum[3], sum[2] / sum[3], sum[3]} : colors::Transparent;
    }
    case Kind::Composite: return blend(d.src->colorAt(p), d.dst->colorAt(p), d.blendMode);
    }
    return colors::Transparent;
}

bool operator==(const Shader& a, const Shader& b) {
    if (a.data_ == b.data_) return true;
    if (!a.data_ || !b.data_) return false;
    const Shader::Data &x = *a.data_, &y = *b.data_;
    return x.kind == y.kind && x.stops == y.stops && x.tile == y.tile && x.tileY == y.tileY && x.start == y.start && x.end == y.end &&
           x.radius == y.radius && x.image == y.image && x.dst == y.dst && x.src == y.src && x.blendMode == y.blendMode &&
           x.transform == y.transform;
}

// --- Vertices ---------------------------------------------------------------------------------------

struct Vertices::Data {
    VertexMode mode;
    std::vector<Offset> positions, textureCoordinates;
    std::vector<Color> colors;
    std::vector<int> indices;
};

Vertices::Vertices(VertexMode vertexMode, std::vector<Offset> positions, std::vector<Offset> textureCoordinates, std::vector<Color> colors,
                   std::vector<int> indices) {
    auto d = std::make_shared<Data>();
    d->mode = vertexMode;
    if (textureCoordinates.size() != positions.size()) textureCoordinates.clear();
    if (colors.size() != positions.size()) colors.clear();
    d->positions = std::move(positions), d->textureCoordinates = std::move(textureCoordinates), d->colors = std::move(colors);
    d->indices = std::move(indices);
    data_ = std::move(d);
}

Vertices::Vertices(VertexMode vertexMode, const std::vector<float>& positions, const std::vector<float>& textureCoordinates,
                   const std::vector<uint32_t>& colors, const std::vector<int16_t>& indices)
    : Vertices(vertexMode, std::vector<Offset>{}, std::vector<Offset>{}, std::vector<Color>{}, std::vector<int>{}) {
    auto d = std::make_shared<Data>(*data_);
    for (size_t i = 0; i + 1 < positions.size(); i += 2) d->positions.push_back({positions[i], positions[i + 1]});
    if (textureCoordinates.size() == positions.size())
        for (size_t i = 0; i + 1 < textureCoordinates.size(); i += 2) d->textureCoordinates.push_back({textureCoordinates[i], textureCoordinates[i + 1]});
    if (colors.size() == d->positions.size())
        for (uint32_t c : colors) d->colors.push_back(Color::hex(c & 0xFFFFFF, float(c >> 24) / 255.f));
    for (int16_t i : indices) d->indices.push_back(i);
    data_ = std::move(d);
}

VertexMode Vertices::vertexMode() const { return data_->mode; }
const std::vector<Offset>& Vertices::positions() const { return data_->positions; }
const std::vector<Offset>& Vertices::textureCoordinates() const { return data_->textureCoordinates; }
const std::vector<Color>& Vertices::colors() const { return data_->colors; }
const std::vector<int>& Vertices::indices() const { return data_->indices; }

std::vector<std::array<int, 3>> Vertices::triangles() const {
    const Data& d = *data_;
    int n = d.indices.empty() ? int(d.positions.size()) : int(d.indices.size());
    auto at = [&](int i) { return d.indices.empty() ? i : d.indices[size_t(i)]; };
    std::vector<std::array<int, 3>> out;
    auto add = [&](int a, int b, int c) {
        std::array<int, 3> t{at(a), at(b), at(c)};
        for (int v : t)
            if (v < 0 || v >= int(d.positions.size())) return;
        out.push_back(t);
    };
    switch (d.mode) {
    case VertexMode::Triangles:
        for (int i = 0; i + 2 < n; i += 3) add(i, i + 1, i + 2);
        break;
    case VertexMode::TriangleStrip:
        for (int i = 0; i + 2 < n; ++i) add(i, i + 1, i + 2);
        break;
    case VertexMode::TriangleFan:
        for (int i = 1; i + 1 < n; ++i) add(0, i, i + 1);
        break;
    }
    return out;
}

Rect Vertices::bounds() const {
    const std::vector<Offset>& p = data_->positions;
    if (p.empty()) return {};
    float l = p[0].x, t = p[0].y, r = l, b = t;
    for (Offset q : p) l = std::min(l, q.x), t = std::min(t, q.y), r = std::max(r, q.x), b = std::max(b, q.y);
    return {l, t, r - l, b - t};
}

bool operator==(const Vertices& a, const Vertices& b) {
    if (a.data_ == b.data_) return true;
    const Vertices::Data &x = *a.data_, &y = *b.data_;
    return x.mode == y.mode && x.positions == y.positions && x.textureCoordinates == y.textureCoordinates && x.colors == y.colors &&
           x.indices == y.indices;
}

void ColorMatrix::setToSaturation(float s) {
    // Rec. 709 luminance, as Android's.
    const float r = 0.213f * (1 - s), g = 0.715f * (1 - s), b = 0.072f * (1 - s);
    values = {r + s, g, b, 0, 0, r, g + s, b, 0, 0, r, g, b + s, 0, 0, 0, 0, 0, 1, 0};
}

void ColorMatrix::setToScale(float red, float green, float blue, float alpha) {
    values = {red, 0, 0, 0, 0, 0, green, 0, 0, 0, 0, 0, blue, 0, 0, 0, 0, 0, alpha, 0};
}

ColorMatrix& ColorMatrix::timesAssign(const ColorMatrix& o) {
    std::array<float, 20> out{};
    for (int row = 0; row < 4; ++row) {
        for (int col = 0; col < 5; ++col) {
            float v = col == 4 ? values[size_t(row * 5 + 4)] : 0;
            for (int k = 0; k < 4; ++k) v += values[size_t(row * 5 + k)] * o.values[size_t(k * 5 + col)];
            out[size_t(row * 5 + col)] = v;
        }
    }
    values = out;
    return *this;
}

Color ColorMatrix::apply(Color c) const {
    c = detail::inSrgb(c);
    const float in[4] = {c.red(), c.green(), c.blue(), c.alpha()};
    float out[4];
    for (int row = 0; row < 4; ++row) {
        float v = values[size_t(row * 5 + 4)];
        for (int k = 0; k < 4; ++k) v += values[size_t(row * 5 + k)] * in[k];
        out[row] = std::clamp(v, 0.f, 1.f);
    }
    return {out[0], out[1], out[2], out[3]};
}

// ---- Matrix (Compose's, operation for operation) ---------------------------------------------

Offset Matrix::map(Offset p) const {
    const Matrix& m = *this;
    float inverse = 1 / (m(0, 3) * p.x + m(1, 3) * p.y + m(3, 3));
    float w = std::isfinite(inverse) ? inverse : 0;
    return {w * (m(0, 0) * p.x + m(1, 0) * p.y + m(3, 0)), w * (m(0, 1) * p.x + m(1, 1) * p.y + m(3, 1))};
}

Rect Matrix::map(const Rect& r) const {
    Offset a = map(Offset{r.x, r.y}), b = map(Offset{r.x, r.bottom()}), c = map(Offset{r.right(), r.y}), d = map(Offset{r.right(), r.bottom()});
    float l = std::min({a.x, b.x, c.x, d.x}), t = std::min({a.y, b.y, c.y, d.y});
    return {l, t, std::max({a.x, b.x, c.x, d.x}) - l, std::max({a.y, b.y, c.y, d.y}) - t};
}

Matrix& Matrix::timesAssign(const Matrix& o) {
    Matrix out;
    for (int row = 0; row < 4; ++row)
        for (int col = 0; col < 4; ++col)
            out(row, col) = (*this)(row, 0) * o(0, col) + (*this)(row, 1) * o(1, col) + (*this)(row, 2) * o(2, col) + (*this)(row, 3) * o(3, col);
    values = out.values;
    return *this;
}

void Matrix::invert() {
    const Matrix& m = *this;
    float a00 = m(0, 0), a01 = m(0, 1), a02 = m(0, 2), a03 = m(0, 3), a10 = m(1, 0), a11 = m(1, 1), a12 = m(1, 2), a13 = m(1, 3);
    float a20 = m(2, 0), a21 = m(2, 1), a22 = m(2, 2), a23 = m(2, 3), a30 = m(3, 0), a31 = m(3, 1), a32 = m(3, 2), a33 = m(3, 3);
    float b00 = a00 * a11 - a01 * a10, b01 = a00 * a12 - a02 * a10, b02 = a00 * a13 - a03 * a10, b03 = a01 * a12 - a02 * a11;
    float b04 = a01 * a13 - a03 * a11, b05 = a02 * a13 - a03 * a12, b06 = a20 * a31 - a21 * a30, b07 = a20 * a32 - a22 * a30;
    float b08 = a20 * a33 - a23 * a30, b09 = a21 * a32 - a22 * a31, b10 = a21 * a33 - a23 * a31, b11 = a22 * a33 - a23 * a32;
    float det = b00 * b11 - b01 * b10 + b02 * b09 + b03 * b08 - b04 * b07 + b05 * b06;
    if (det == 0) return;
    float k = 1 / det;
    values = {(a11 * b11 - a12 * b10 + a13 * b09) * k,  (-a01 * b11 + a02 * b10 - a03 * b09) * k,
              (a31 * b05 - a32 * b04 + a33 * b03) * k,  (-a21 * b05 + a22 * b04 - a23 * b03) * k,
              (-a10 * b11 + a12 * b08 - a13 * b07) * k, (a00 * b11 - a02 * b08 + a03 * b07) * k,
              (-a30 * b05 + a32 * b02 - a33 * b01) * k, (a20 * b05 - a22 * b02 + a23 * b01) * k,
              (a10 * b10 - a11 * b08 + a13 * b06) * k,  (-a00 * b10 + a01 * b08 - a03 * b06) * k,
              (a30 * b04 - a31 * b02 + a33 * b00) * k,  (-a20 * b04 + a21 * b02 - a23 * b00) * k,
              (-a10 * b09 + a11 * b07 - a12 * b06) * k, (a00 * b09 - a01 * b07 + a02 * b06) * k,
              (-a30 * b03 + a31 * b01 - a32 * b00) * k, (a20 * b03 - a21 * b01 + a22 * b00) * k};
}

void Matrix::reset() { *this = Matrix(); }

namespace {
// The sine and cosine of `degrees`, worked out in double as Compose does.
std::pair<float, float> sinCos(float degrees) {
    double r = double(degrees) * std::numbers::pi / 180.0;
    return {float(std::sin(r)), float(std::cos(r))};
}
} // namespace

void Matrix::rotateX(float degrees) {  // after: columns 1 and 2 mixed
    auto [s, c] = sinCos(degrees);
    for (int row = 0; row < 4; ++row) {
        float a1 = (*this)(row, 1), a2 = (*this)(row, 2);
        (*this)(row, 1) = a1 * c - a2 * s;
        (*this)(row, 2) = a1 * s + a2 * c;
    }
}

void Matrix::rotateY(float degrees) {  // after: columns 0 and 2 mixed
    auto [s, c] = sinCos(degrees);
    for (int row = 0; row < 4; ++row) {
        float a0 = (*this)(row, 0), a2 = (*this)(row, 2);
        (*this)(row, 0) = a0 * c + a2 * s;
        (*this)(row, 2) = -a0 * s + a2 * c;
    }
}

void Matrix::rotateZ(float degrees) {  // before: rows 0 and 1 mixed
    auto [s, c] = sinCos(degrees);
    for (int col = 0; col < 4; ++col) {
        float a0 = (*this)(0, col), a1 = (*this)(1, col);
        (*this)(0, col) = c * a0 + s * a1;
        (*this)(1, col) = -s * a0 + c * a1;
    }
}

void Matrix::scale(float x, float y, float z) {
    for (int col = 0; col < 4; ++col) (*this)(0, col) *= x, (*this)(1, col) *= y, (*this)(2, col) *= z;
}

void Matrix::translate(float x, float y, float z) {
    for (int col = 0; col < 4; ++col) (*this)(3, col) += (*this)(0, col) * x + (*this)(1, col) * y + (*this)(2, col) * z;
}

void Matrix::resetToPivotedTransform(float pivotX, float pivotY, float translationX, float translationY, float translationZ,
                                     float rotationX, float rotationY, float rotationZ, float scaleX, float scaleY, float scaleZ) {
    auto [sx, cx] = sinCos(rotationX);
    float v11 = cx, v12 = sx, v21 = -sx, v22 = cx;
    float v31 = translationY * cx - translationZ * sx, v32 = translationY * sx + translationZ * cx;
    auto [sy, cy] = sinCos(rotationY);
    float v00 = cy, v02 = -sy;
    float v10 = v12 * sy;
    v12 *= cy;
    float v20 = v22 * sy;
    v22 *= cy;
    float v30 = translationX * cy + v32 * sy;
    v32 = -translationX * sy + v32 * cy;
    auto [sz, cz] = sinCos(rotationZ);
    float a10 = v10;
    v10 = -sz * v00 + cz * v10;
    v00 = cz * v00 + sz * a10;
    float v01 = sz * v11;
    v11 *= cz;
    float a12 = v12;
    v12 = -sz * v02 + cz * a12;
    v02 = cz * v02 + sz * a12;
    v00 *= scaleX, v01 *= scaleX, v02 *= scaleX;
    v10 *= scaleY, v11 *= scaleY, v12 *= scaleY;
    v20 *= scaleZ, v21 *= scaleZ, v22 *= scaleZ;
    values = {v00, v01, v02, 0, v10, v11, v12, 0, v20, v21, v22, 0,
              -pivotX * v00 - pivotY * v10 + v30 + pivotX, -pivotX * v01 - pivotY * v11 + v31 + pivotY, -pivotX * v02 - pivotY * v12 + v32, 1};
}

ColorFilter ColorFilter::tint(Color color, BlendMode blendMode) {
    ColorFilter f;
    f.kind_ = Kind::Tint;
    f.color_ = color;
    f.mode_ = blendMode;
    return f;
}

ColorFilter ColorFilter::colorMatrix(const ColorMatrix& matrix) {
    ColorFilter f;
    f.kind_ = Kind::Matrix;
    f.matrix_ = matrix;
    return f;
}

ColorFilter ColorFilter::lighting(Color multiply, Color add) {
    ColorFilter f;
    f.kind_ = Kind::Lighting;
    f.color_ = multiply;
    f.add_ = add;
    return f;
}

Brush ColorFilter::apply(const Brush& brush) const {
    Brush filtered = brush;
    for (ColorStop& stop : filtered.stops_) stop.color = apply(stop.color);
    return filtered;
}

namespace {

// W3C's compositing spec's helpers for the non-separable modes, on straight colors.
struct RgbValue {
    float r, g, b;
};
float lum(RgbValue c) { return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b; }
RgbValue clipColor(RgbValue c) {
    float l = lum(c), n = std::min({c.r, c.g, c.b}), x = std::max({c.r, c.g, c.b});
    auto low = [&](float v) { return l + (v - l) * l / (l - n); };
    auto high = [&](float v) { return l + (v - l) * (1 - l) / (x - l); };
    if (n < 0 && l - n > 0) c = {low(c.r), low(c.g), low(c.b)};
    if (x > 1 && x - l > 0) c = {high(c.r), high(c.g), high(c.b)};
    return c;
}
RgbValue setLum(RgbValue c, float l) {
    float d = l - lum(c);
    return clipColor({c.r + d, c.g + d, c.b + d});
}
float sat(RgbValue c) { return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b}); }
RgbValue setSat(RgbValue c, float s) {
    float* v[3] = {&c.r, &c.g, &c.b};
    std::sort(std::begin(v), std::end(v), [](float* x, float* y) { return *x < *y; });  // min, mid, max
    if (*v[2] > *v[0]) {
        *v[1] = (*v[1] - *v[0]) * s / (*v[2] - *v[0]);
        *v[2] = s;
    } else {
        *v[1] = *v[2] = 0;
    }
    *v[0] = 0;
    return c;
}

// A separable mode's B(cs, cb), on straight channels.
float separable(BlendMode mode, float cs, float cb) {
    auto hardLight = [](float s, float b) { return s <= 0.5f ? b * 2 * s : b + (2 * s - 1) - b * (2 * s - 1); };
    switch (mode) {
    case BlendMode::Multiply: return cs * cb;
    case BlendMode::Screen: return cs + cb - cs * cb;
    case BlendMode::Overlay: return hardLight(cb, cs);
    case BlendMode::Darken: return std::min(cs, cb);
    case BlendMode::Lighten: return std::max(cs, cb);
    case BlendMode::ColorDodge: return cb <= 0 ? 0 : cs >= 1 ? 1 : std::min(1.f, cb / (1 - cs));
    case BlendMode::ColorBurn: return cb >= 1 ? 1 : cs <= 0 ? 0 : 1 - std::min(1.f, (1 - cb) / cs);
    case BlendMode::Hardlight: return hardLight(cs, cb);
    case BlendMode::Softlight: {
        if (cs <= 0.5f) return cb - (1 - 2 * cs) * cb * (1 - cb);
        float d = cb <= 0.25f ? ((16 * cb - 12) * cb + 4) * cb : std::sqrt(cb);
        return cb + (2 * cs - 1) * (d - cb);
    }
    case BlendMode::Difference: return std::abs(cs - cb);
    case BlendMode::Exclusion: return cs + cb - 2 * cs * cb;
    default: return cs;
    }
}

} // namespace

Color blend(Color source, Color destination, BlendMode mode) {
    source = detail::inSrgb(source), destination = detail::inSrgb(destination);
    float sa = source.alpha(), da = destination.alpha();
    float s[3] = {source.red() * sa, source.green() * sa, source.blue() * sa}, d[3] = {destination.red() * da, destination.green() * da, destination.blue() * da};
    float out[3], a = sa + da - sa * da;  // (the separable and non-separable modes' alpha)
    auto porterDuff = [&](float fs, float fd) {  // s fs + d fd
        for (int i = 0; i < 3; ++i) out[i] = s[i] * fs + d[i] * fd;
        a = sa * fs + da * fd;
    };
    switch (mode) {
    case BlendMode::Clear: porterDuff(0, 0); break;
    case BlendMode::Src: porterDuff(1, 0); break;
    case BlendMode::Dst: porterDuff(0, 1); break;
    case BlendMode::SrcOver: porterDuff(1, 1 - sa); break;
    case BlendMode::DstOver: porterDuff(1 - da, 1); break;
    case BlendMode::SrcIn: porterDuff(da, 0); break;
    case BlendMode::DstIn: porterDuff(0, sa); break;
    case BlendMode::SrcOut: porterDuff(1 - da, 0); break;
    case BlendMode::DstOut: porterDuff(0, 1 - sa); break;
    case BlendMode::SrcAtop: porterDuff(da, 1 - sa); break;
    case BlendMode::DstAtop: porterDuff(1 - da, sa); break;
    case BlendMode::Xor: porterDuff(1 - da, 1 - sa); break;
    case BlendMode::Plus:
        for (int i = 0; i < 3; ++i) out[i] = std::min(1.f, s[i] + d[i]);
        a = std::min(1.f, sa + da);
        break;
    case BlendMode::Modulate:
        for (int i = 0; i < 3; ++i) out[i] = s[i] * d[i];
        a = sa * da;
        break;
    case BlendMode::Hue:
    case BlendMode::Saturation:
    case BlendMode::Color:
    case BlendMode::Luminosity: {
        RgbValue cs{source.red(), source.green(), source.blue()}, cb{destination.red(), destination.green(), destination.blue()}, b{};
        switch (mode) {
        case BlendMode::Hue: b = setLum(setSat(cs, sat(cb)), lum(cb)); break;
        case BlendMode::Saturation: b = setLum(setSat(cb, sat(cs)), lum(cb)); break;
        case BlendMode::Color: b = setLum(cs, lum(cb)); break;
        default: b = setLum(cb, lum(cs)); break;
        }
        float bc[3] = {b.r, b.g, b.b};
        for (int i = 0; i < 3; ++i) out[i] = s[i] * (1 - da) + d[i] * (1 - sa) + sa * da * bc[i];
        break;
    }
    default: {  // separable: s (1 - da) + d (1 - sa) + sa da B(cs, cb)
        const float cs[3] = {source.red(), source.green(), source.blue()}, cb[3] = {destination.red(), destination.green(), destination.blue()};
        for (int i = 0; i < 3; ++i) out[i] = s[i] * (1 - da) + d[i] * (1 - sa) + sa * da * separable(mode, cs[i], cb[i]);
        break;
    }
    }
    if (a <= 0) return {0, 0, 0, 0};
    return {std::clamp(out[0] / a, 0.f, 1.f), std::clamp(out[1] / a, 0.f, 1.f), std::clamp(out[2] / a, 0.f, 1.f), std::min(a, 1.f)};
}

Color ColorFilter::apply(Color d) const {
    if (kind_ == Kind::Matrix) return matrix_.apply(d);
    d = detail::inSrgb(d);
    if (kind_ == Kind::Lighting) {
        Color color = detail::inSrgb(color_), add = detail::inSrgb(add_);
        return {std::clamp(d.red() * color.red() + add.red(), 0.f, 1.f), std::clamp(d.green() * color.green() + add.green(), 0.f, 1.f),
                std::clamp(d.blue() * color.blue() + add.blue(), 0.f, 1.f), d.alpha()};
    }
    if (mode_ == BlendMode::SrcIn && color_.alpha() * d.alpha() > 0) return detail::inSrgb(color_).copy(color_.alpha() * d.alpha());  // (exactly: the tint, as opaque as both)
    return blend(color_, d, mode_);  // the tint (the source) onto the pixel
}

ImageBitmap ColorFilter::apply(const ImageBitmap& image) const {
    if (image.empty()) return image;
    struct Entry {
        std::weak_ptr<const void> image;
        uint64_t id, version;  // (a bitmap drawn into since: filtered again)
        ColorFilter filter;
        ImageBitmap filtered;
    };
    static std::mutex mutex;
    static std::vector<Entry> cache;  // the latest last
    std::lock_guard lock(mutex);
    std::erase_if(cache, [](const Entry& e) { return e.image.expired(); });
    for (size_t i = 0; i < cache.size(); ++i)
        if (cache[i].id == image.id() && cache[i].version == image.version() && cache[i].filter == *this) {
            std::rotate(cache.begin() + std::ptrdiff_t(i), cache.begin() + std::ptrdiff_t(i) + 1, cache.end());
            return cache.back().filtered;
        }
    const uint8_t* in = image.pixels();
    std::vector<uint8_t> out(size_t(image.width()) * size_t(image.height()) * 4);
    for (size_t i = 0; i < out.size(); i += 4) {
        Color c = apply(Color{in[i] / 255.f, in[i + 1] / 255.f, in[i + 2] / 255.f, in[i + 3] / 255.f});
        out[i] = uint8_t(std::lround(c.red() * 255)), out[i + 1] = uint8_t(std::lround(c.green() * 255));
        out[i + 2] = uint8_t(std::lround(c.blue() * 255)), out[i + 3] = uint8_t(std::lround(c.alpha() * 255));
    }
    ImageBitmap filtered = ImageBitmap::fromPixels(image.width(), image.height(), std::move(out));
    cache.push_back({image.token(), image.id(), image.version(), *this, filtered});
    if (cache.size() > 32) cache.erase(cache.begin());
    return filtered;
}

RenderEffect BlurEffect(const std::optional<RenderEffect>& renderEffect, float radiusX, float radiusY, TileMode edgeTreatment) {
    RenderEffect e = renderEffect.value_or(RenderEffect());
    RenderEffect::Step s;
    s.radiusX = std::max(0.f, radiusX), s.radiusY = std::max(0.f, radiusY);
    s.edgeTreatment = edgeTreatment;
    e.steps_.push_back(s);
    return e;
}

RenderEffect OffsetEffect(const std::optional<RenderEffect>& renderEffect, Offset offset) {
    RenderEffect e = renderEffect.value_or(RenderEffect());
    RenderEffect::Step s;
    s.kind = RenderEffect::Step::Kind::Offset;
    s.offset = offset;
    e.steps_.push_back(s);
    return e;
}

Rect RenderEffect::outputBounds(const Rect& bounds) const {
    Rect r = bounds;
    for (const Step& s : steps_) {
        if (s.kind == Step::Kind::Offset) {
            r.x += s.offset.x, r.y += s.offset.y;
            continue;
        }
        if (s.progressive) {  // its taps reach as far as its widest radius
            float most = 0;
            for (const auto& [at, radius] : s.progressive->stops) most = std::max(most, radius);
            float by = most > 0 ? most + 1 : 0;
            r = {r.x - by, r.y - by, r.width + 2 * by, r.height + 2 * by};
            continue;
        }
        // Three sigmas (0.57735 r + 0.5 px), the half pixel taken as up to 2.5 dp (ldpi's 0.75 px a dp, rounded up).
        float x = s.radiusX > 0 ? 3 * 0.57735f * s.radiusX + 2.5f : 0, y = s.radiusY > 0 ? 3 * 0.57735f * s.radiusY + 2.5f : 0;
        r = {r.x - x, r.y - y, r.width + 2 * x, r.height + 2 * y};
    }
    return r;
}

BlurRadiusSpec::BlurRadiusSpec(Kind kind, std::vector<BlurStop> stops) : kind_(kind), stops_(std::move(stops)) {
    for (BlurStop& s : stops_) s.fraction = std::clamp(s.fraction, 0.f, 1.f), s.radius = std::max(0.f, s.radius);
    std::stable_sort(stops_.begin(), stops_.end(), [](const BlurStop& a, const BlurStop& b) { return a.fraction < b.fraction; });
}

BlurRadiusSpec BlurRadiusSpec::uniform(float radius) { return {Kind::Uniform, {{0, radius}}}; }
BlurRadiusSpec BlurRadiusSpec::verticalGradient(float startRadius, float endRadius) {
    return {Kind::Vertical, {{0, startRadius}, {1, endRadius}}};
}
BlurRadiusSpec BlurRadiusSpec::verticalGradient(std::vector<BlurStop> stops) { return {Kind::Vertical, std::move(stops)}; }
BlurRadiusSpec BlurRadiusSpec::horizontalGradient(float startRadius, float endRadius) {
    return {Kind::Horizontal, {{0, startRadius}, {1, endRadius}}};
}
BlurRadiusSpec BlurRadiusSpec::horizontalGradient(std::vector<BlurStop> stops) { return {Kind::Horizontal, std::move(stops)}; }
BlurRadiusSpec BlurRadiusSpec::linearGradient(Offset start, Offset end, float startRadius, float endRadius) {
    return linearGradient(start, end, {{0, startRadius}, {1, endRadius}});
}
BlurRadiusSpec BlurRadiusSpec::linearGradient(Offset start, Offset end, std::vector<BlurStop> stops) {
    BlurRadiusSpec s{Kind::Linear, std::move(stops)};
    s.start_ = start, s.end_ = end;
    return s;
}
BlurRadiusSpec BlurRadiusSpec::radialGradient(float startRadius, float endRadius, std::optional<Offset> center,
                                              std::optional<float> fallOffRadius) {
    return radialGradient({{0, startRadius}, {1, endRadius}}, center, fallOffRadius);
}
BlurRadiusSpec BlurRadiusSpec::radialGradient(std::vector<BlurStop> stops, std::optional<Offset> center, std::optional<float> fallOffRadius) {
    BlurRadiusSpec s{Kind::Radial, std::move(stops)};
    s.center_ = center;
    if (fallOffRadius && *fallOffRadius > 0 && std::isfinite(*fallOffRadius)) s.fallOff_ = fallOffRadius;
    return s;
}

BlurRadiusSpec BlurRadiusSpec::shader(float maxRadius, std::function<Shader(Density density, Size size)> block) {
    BlurRadiusSpec s{Kind::Shader, {{0, maxRadius}, {1, maxRadius}}};
    s.shaderBlock_ = std::make_shared<const std::function<Shader(Density, Size)>>(std::move(block));
    return s;
}

RenderEffect BlurRadiusSpec::createRenderEffect(Size size, TileMode edgeTreatment, Density density) const {
    TileMode edges = edgeTreatment == TileMode::Decal ? TileMode::Decal : TileMode::Clamp;
    if (kind_ == Kind::Uniform) return BlurEffect(stops_.front().radius, stops_.front().radius, edges);
    float most = 0;
    for (const BlurStop& s : stops_) most = std::max(most, s.radius);
    if (stops_.empty() || most <= 0) return OffsetEffect(0, 0);  // (blurs nothing: as Compose's, an effect that changes nothing)
    RenderEffect::Progressive p;
    for (const BlurStop& s : stops_) p.stops.emplace_back(s.fraction, s.radius);
    switch (kind_) {
    case Kind::Vertical: p.end = {0, size.height}; break;
    case Kind::Horizontal: p.end = {size.width, 0}; break;
    case Kind::Linear: p.start = start_, p.end = end_; break;
    case Kind::Shader: p.shader = (*shaderBlock_)(density, size); break;
    default:
        p.radial = true;
        p.start = center_.value_or(Offset{size.width / 2, size.height / 2});
        p.radius = fallOff_.value_or(std::min(size.width, size.height) / 2);
    }
    RenderEffect e;
    RenderEffect::Step s;
    s.kind = RenderEffect::Step::Kind::ProgressiveBlur;
    s.edgeTreatment = edges;
    s.progressive = std::move(p);
    e.steps_.push_back(std::move(s));
    return e;
}

void Path::moveTo(float x, float y) {
    if (!commands_.empty() && commands_.back().verb == PathCommand::Verb::Move) commands_.back().x = x, commands_.back().y = y;
    else commands_.push_back({PathCommand::Verb::Move, 0, 0, 0, 0, x, y});
    current_ = start_ = {x, y};
}

void Path::ensureMove() {
    if (commands_.empty()) moveTo(0, 0);
    else if (commands_.back().verb == PathCommand::Verb::Close) moveTo(start_.x, start_.y);
}

void Path::lineTo(float x, float y) {
    ensureMove();
    commands_.push_back({PathCommand::Verb::Line, 0, 0, 0, 0, x, y});
    current_ = {x, y};
}

void Path::quadraticTo(float x1, float y1, float x2, float y2) {
    ensureMove();
    commands_.push_back({PathCommand::Verb::Quad, x1, y1, 0, 0, x2, y2});
    current_ = {x2, y2};
}

void Path::conicTo(float x1, float y1, float x2, float y2, float w) {
    ensureMove();
    if (!(w > 0)) return lineTo(x2, y2);
    if (w == 1) return quadraticTo(x1, y1, x2, y2);
    if (!std::isfinite(w)) {  // (Skia's: two lines)
        lineTo(x1, y1);
        return lineTo(x2, y2);
    }
    commands_.push_back({PathCommand::Verb::Conic, x1, y1, 0, 0, x2, y2, w});
    current_ = {x2, y2};
}

void Path::cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
    ensureMove();
    commands_.push_back({PathCommand::Verb::Cubic, x1, y1, x2, y2, x3, y3});
    current_ = {x3, y3};
}

void Path::close() {
    if (commands_.empty() || commands_.back().verb == PathCommand::Verb::Close) return;
    commands_.push_back({PathCommand::Verb::Close});
    current_ = start_;
}

namespace {

struct Polyline {
    std::vector<Offset> points;
    bool closed = false;
};

float distance(Offset a, Offset b) { return std::hypot(b.x - a.x, b.y - a.y); }
Offset mix(Offset a, Offset b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }

// The path as polylines, curves cut into pieces of about a dp (at least four).
std::vector<Polyline> flatten(const Path& path) {
    std::vector<Polyline> out;
    Offset current, start;
    auto add = [&](Offset p) {
        if (out.empty() || out.back().closed) out.push_back({{current}, false});  // drawing on after close(): from its start
        out.back().points.push_back(p);
        current = p;
    };
    for (const PathCommand& c : path.commands()) {
        Offset end{c.x, c.y};
        switch (c.verb) {
        case PathCommand::Verb::Move:
            out.push_back({{end}, false});
            current = start = end;
            break;
        case PathCommand::Verb::Line: add(end); break;
        case PathCommand::Verb::Quad: {
            Offset p0 = current, p1{c.x1, c.y1};
            int n = std::clamp(int(std::ceil(distance(p0, p1) + distance(p1, end))), 4, 1024);
            for (int i = 1; i <= n; ++i) {
                float t = float(i) / float(n);
                add(mix(mix(p0, p1, t), mix(p1, end, t), t));
            }
            break;
        }
        case PathCommand::Verb::Conic: {
            detail::curves::Conic k{{current, {c.x1, c.y1}, end}, c.w};
            int n = std::clamp(int(std::ceil(distance(k.p[0], k.p[1]) + distance(k.p[1], end))), 4, 1024);
            for (int i = 1; i <= n; ++i) add(k.at(float(i) / float(n)));
            break;
        }
        case PathCommand::Verb::Cubic: {
            Offset p0 = current, p1{c.x1, c.y1}, p2{c.x2, c.y2};
            int n = std::clamp(int(std::ceil(distance(p0, p1) + distance(p1, p2) + distance(p2, end))), 4, 1024);
            for (int i = 1; i <= n; ++i) {
                float t = float(i) / float(n);
                Offset a = mix(p0, p1, t), b = mix(p1, p2, t), d = mix(p2, end, t);
                add(mix(mix(a, b, t), mix(b, d, t), t));
            }
            break;
        }
        case PathCommand::Verb::Close:
            if (!out.empty() && !out.back().closed) {
                Polyline& l = out.back();
                if (l.points.size() > 1 && distance(l.points.front(), l.points.back()) < 1e-4f) l.points.pop_back();
                l.closed = true;
            }
            current = start;
            break;
        }
    }
    return out;
}

} // namespace

PathEffect PathEffect::dashPathEffect(std::initializer_list<float> intervals, float phase) {
    PathEffect e;
    for (float v : intervals)
        if (e.count < int(e.intervals.size())) e.intervals[size_t(e.count++)] = std::max(0.f, v);
    e.count -= e.count % 2;  // on and off in pairs
    e.phase = phase;
    return e;
}

PathEffect PathEffect::cornerPathEffect(float radius) {
    PathEffect e;
    e.cornerRadius = std::max(0.f, radius);
    return e;
}

PathEffect PathEffect::stampedPathEffect(const Path& shape, float advance, float phase, StampedPathEffectStyle style) {
    PathEffect e;
    e.stamp = shape;
    e.advance = std::max(0.f, advance);
    e.stampStyle = style;
    // Where the first copy goes, as Skia's SkPath1DPathEffect works it out.
    if (e.advance > 0) {
        if (phase < 0) {
            phase = -phase;
            if (phase > e.advance) phase = std::fmod(phase, e.advance);
        } else {
            if (phase > e.advance) phase = std::fmod(phase, e.advance);
            phase = e.advance - phase;
        }
        if (phase >= e.advance) phase = 0;
    }
    e.phase = phase;
    return e;
}

PathEffect PathEffect::chainPathEffect(const PathEffect& outer, const PathEffect& inner) {
    PathEffect e = outer.count > 0 || outer.fills() ? outer : inner;
    e.cornerRadius = std::max(outer.cornerRadius, inner.cornerRadius);
    return e;
}

namespace {

// A polyline's points with how far along it each is: where a distance lands and which way the
// line goes there (past either end, on along the end segments).
struct Walk {
    std::vector<Offset> points;
    std::vector<float> at;  // distance of each point
    explicit Walk(std::vector<Offset> p) : points(std::move(p)) {
        at.resize(points.size());
        for (size_t i = 1; i < points.size(); ++i) at[i] = at[i - 1] + distance(points[i - 1], points[i]);
    }
    float length() const { return at.empty() ? 0 : at.back(); }
    // The segment a distance falls in (its start point's index).
    size_t segment(float d) const {
        size_t i = size_t(std::upper_bound(at.begin(), at.end(), d) - at.begin());
        return std::clamp(i, size_t(1), points.size() - 1) - 1;
    }
    Offset position(float d) const {
        size_t i = segment(d);
        float span = at[i + 1] - at[i];
        return span > 0 ? mix(points[i], points[i + 1], (d - at[i]) / span) : points[i];
    }
    Offset tangent(float d) const {
        size_t i = segment(d);
        float span = distance(points[i], points[i + 1]);
        return span > 0 ? Offset{(points[i + 1].x - points[i].x) / span, (points[i + 1].y - points[i].y) / span} : Offset{1, 0};
    }
};

// Copies of the stamp along each contour: moved to each spot (and turned, or bent along the path).
Path stampAlong(const Path& path, const PathEffect& e) {
    Path out;
    std::vector<Polyline> shape = flatten(*e.stamp);
    for (const Polyline& l : flatten(path)) {
        std::vector<Offset> p = l.points;
        if (l.closed && p.size() > 1) p.push_back(p.front());
        if (p.size() < 2) continue;
        Walk walk(std::move(p));
        for (float d = e.phase; d < walk.length(); d += e.advance) {
            Offset at = walk.position(d), dir = walk.tangent(d);
            auto place = [&](Offset s) -> Offset {
                switch (e.stampStyle) {
                case StampedPathEffectStyle::Translate: return {at.x + s.x, at.y + s.y};
                case StampedPathEffectStyle::Rotate: return {at.x + s.x * dir.x - s.y * dir.y, at.y + s.x * dir.y + s.y * dir.x};
                case StampedPathEffectStyle::Morph: {  // x along the path, y across it
                    Offset on = walk.position(d + s.x), t = walk.tangent(d + s.x);
                    return {on.x - s.y * t.y, on.y + s.y * t.x};
                }
                }
                return s;
            };
            bool bend = e.stampStyle == StampedPathEffectStyle::Morph;
            for (const Polyline& s : shape) {
                std::vector<Offset> points = s.points;
                if (s.closed && points.size() > 1) points.push_back(points.front());
                for (size_t i = 0; i < points.size(); ++i) {
                    // Bent: its edges cut into pieces of a dp or so, to follow the path's curve.
                    int pieces = bend && i > 0 ? std::max(1, int(std::ceil(distance(points[i - 1], points[i])))) : 1;
                    for (int k = 1; k <= pieces; ++k) {
                        Offset q = place(i == 0 ? points[0] : mix(points[i - 1], points[i], float(k) / float(pieces)));
                        i == 0 ? out.moveTo(q.x, q.y) : out.lineTo(q.x, q.y);
                    }
                }
                out.close();
            }
        }
    }
    return out;
}

} // namespace

Path PathEffect::apply(const Path& path, bool filled) const {
    Path shaped = path;
    if (cornerRadius > 0) {  // each corner cut back as far as the radius (half a side at most) and joined by a curve
        shaped = Path();
        for (const Polyline& l : flatten(path)) {
            const std::vector<Offset>& p = l.points;
            size_t n = p.size();
            if (n < 3) {
                for (size_t i = 0; i < n; ++i) i == 0 ? shaped.moveTo(p[i].x, p[i].y) : shaped.lineTo(p[i].x, p[i].y);
                if (l.closed) shaped.close();
                continue;
            }
            auto corner = [&](size_t i) {
                Offset at = p[i], before = p[(i + n - 1) % n], after = p[(i + 1) % n];
                float d1 = distance(before, at), d2 = distance(at, after);
                Offset a = d1 > 0 ? mix(at, before, std::min(cornerRadius, d1 / 2) / d1) : at;
                Offset b = d2 > 0 ? mix(at, after, std::min(cornerRadius, d2 / 2) / d2) : at;
                shaped.lineTo(a.x, a.y);
                shaped.quadraticTo(at.x, at.y, b.x, b.y);
            };
            if (l.closed) {  // from the middle of the first side, all the way round
                Offset m = mix(p[0], p[1], 0.5f);
                shaped.moveTo(m.x, m.y);
                for (size_t i = 1; i <= n; ++i) corner(i % n);
                shaped.lineTo(m.x, m.y);
                shaped.close();
            } else {
                shaped.moveTo(p[0].x, p[0].y);
                for (size_t i = 1; i + 1 < n; ++i) corner(i);
                shaped.lineTo(p[n - 1].x, p[n - 1].y);
            }
        }
    }
    if (fills()) return stampAlong(shaped, *this);
    if (filled) return shaped;
    float pattern = 0;
    for (int i = 0; i < count; ++i) pattern += intervals[size_t(i)];
    if (count < 2 || pattern <= 0) return shaped;
    Path dashed;
    for (const Polyline& l : flatten(shaped)) {
        std::vector<Offset> p = l.points;
        if (l.closed && p.size() > 1) p.push_back(p.front());
        int k = 0;
        float into = std::fmod(phase, pattern);
        if (into < 0) into += pattern;
        while (into >= intervals[size_t(k)]) {
            into -= intervals[size_t(k)];
            k = (k + 1) % count;
        }
        float left = intervals[size_t(k)] - into;  // of the interval it is in
        bool down = false;                         // drawing a dash
        for (size_t i = 1; i < p.size(); ++i) {
            float length = distance(p[i - 1], p[i]), done = 0;
            if (length <= 0) continue;
            while (done < length) {
                float step = std::min(left, length - done);
                if (k % 2 == 0) {
                    if (!down) {
                        Offset from = mix(p[i - 1], p[i], done / length);
                        dashed.moveTo(from.x, from.y);
                        down = true;
                    }
                    Offset to = mix(p[i - 1], p[i], (done + step) / length);
                    dashed.lineTo(to.x, to.y);
                }
                done += step;
                left -= step;
                if (left <= 0) {
                    k = (k + 1) % count;
                    left = intervals[size_t(k)];
                    if (k % 2 == 1) down = false;
                }
            }
        }
    }
    return dashed;
}

namespace {

bool combined(PathOperation op, bool a, bool b) {
    switch (op) {
    case PathOperation::Difference: return a && !b;
    case PathOperation::Intersect: return a && b;
    case PathOperation::Union: return a || b;
    case PathOperation::Xor: return a != b;
    case PathOperation::ReverseDifference: return b && !a;
    }
    return false;
}

} // namespace

bool Path::contains(Offset p) const { return detail::curves::contains(commands_, fillType, p); }

Path Path::fromCommands(std::vector<PathCommand> commands, FillType fillType) {
    Path p;
    p.fillType = fillType;
    p.commands_ = std::move(commands);
    for (const PathCommand& c : p.commands_) {
        if (c.verb == PathCommand::Verb::Move) p.start_ = {c.x, c.y};
        p.current_ = c.verb == PathCommand::Verb::Close ? p.start_ : Offset{c.x, c.y};
    }
    return p;
}

PathIterator Path::iterator() const { return PathIterator(*this); }

PathIterator Path::iterator(ConicEvaluation conicEvaluation, float tolerance) const { return PathIterator(*this, conicEvaluation, tolerance); }

PathIterator::PathIterator(const Path& path, ConicEvaluation conicEvaluation, float tolerance)
    : path_(path), evaluation_(conicEvaluation), tolerance_(tolerance) {}

int PathIterator::calculateSize(bool includeConvertedConics) const {
    int size = 0;
    Offset current;
    for (const PathCommand& c : path_.commands()) {
        if (c.verb == PathCommand::Verb::Conic && evaluation_ == ConicEvaluation::AsQuadratics && includeConvertedConics)
            size += 1 << detail::curves::Conic{{current, {c.x1, c.y1}, {c.x, c.y}}, c.w}.quadPow2(tolerance_);
        else ++size;
        if (c.verb != PathCommand::Verb::Close) current = {c.x, c.y};
    }
    return size;
}

bool PathIterator::hasNext() const { return !pending_.empty() || index_ < path_.commands().size(); }

PathSegment PathIterator::next() {
    using Type = PathSegment::Type;
    if (!pending_.empty()) {
        PathSegment s = std::move(pending_.front());
        pending_.erase(pending_.begin());
        return s;
    }
    if (index_ >= path_.commands().size()) return {};
    const PathCommand& c = path_.commands()[index_++];
    Offset end{c.x, c.y}, from = current_;
    if (c.verb != PathCommand::Verb::Close) current_ = end;
    switch (c.verb) {
    case PathCommand::Verb::Move: return {Type::Move, {end}, 0};
    case PathCommand::Verb::Line: return {Type::Line, {from, end}, 0};
    case PathCommand::Verb::Quad: return {Type::Quadratic, {from, {c.x1, c.y1}, end}, 0};
    case PathCommand::Verb::Cubic: return {Type::Cubic, {from, {c.x1, c.y1}, {c.x2, c.y2}, end}, 0};
    case PathCommand::Verb::Close: return {Type::Close, {}, 0};
    case PathCommand::Verb::Conic: {
        if (evaluation_ == ConicEvaluation::AsConic) return {Type::Conic, {from, {c.x1, c.y1}, end}, c.w};
        detail::curves::Conic conic{{from, {c.x1, c.y1}, end}, c.w};
        Offset pts[2 * (1 << detail::curves::kMaxConicToQuadPow2) + 1];
        int quads = conic.chopIntoQuadsPow2(pts, conic.quadPow2(tolerance_));
        for (int i = 0; i < quads; ++i) pending_.push_back({Type::Quadratic, {pts[2 * i], pts[2 * i + 1], pts[2 * i + 2]}, 0});
        return next();
    }
    }
    return {};
}

void PathHitTester::updatePath(const Path& path, float tolerance) {
    // Its conics as quadratics within the tolerance, as Compose's hit tester takes them.
    Path quads;
    quads.fillType = path.fillType;
    for (PathIterator it(path, ConicEvaluation::AsQuadratics, tolerance); it.hasNext();) {
        PathSegment s = it.next();
        switch (s.type) {
        case PathSegment::Type::Move: quads.moveTo(s.points[0].x, s.points[0].y); break;
        case PathSegment::Type::Line: quads.lineTo(s.points[1].x, s.points[1].y); break;
        case PathSegment::Type::Quadratic: quads.quadraticTo(s.points[1].x, s.points[1].y, s.points[2].x, s.points[2].y); break;
        case PathSegment::Type::Cubic:
            quads.cubicTo(s.points[1].x, s.points[1].y, s.points[2].x, s.points[2].y, s.points[3].x, s.points[3].y);
            break;
        case PathSegment::Type::Close: quads.close(); break;
        default: break;
        }
    }
    path_ = std::move(quads);
    bounds_ = path.bounds();
}

// As Compose's: inside its bounds (their far edges not), wound round by its segments as they are
// (an open contour not closed, a point on an edge not inside), by its fill type.
bool PathHitTester::contains(Offset position) const {
    if (path_.isEmpty() || !bounds_.contains(position)) return false;
    int w = detail::curves::winding(path_.commands(), position, false);
    return path_.fillType == Path::FillType::EvenOdd ? (w & 1) != 0 : w != 0;
}

namespace {

// Where along a segment from `from` (its points p[1..]) a projective map's w crosses `edge`: the
// roots of w's Bernstein form (a conic's numerator) in (0, 1), sorted.
std::vector<float> wCrossings(const PathCommand& c, Offset from, const std::array<float, 9>& m, float edge) {
    auto v = [&](float x, float y) { return m[6] * x + m[7] * y + m[8] - edge; };
    std::vector<float> out;
    float v0 = v(from.x, from.y), vEnd = v(c.x, c.y);
    switch (c.verb) {
    case PathCommand::Verb::Line:
        if ((v0 < 0) != (vEnd < 0) && v0 != vEnd) out.push_back(v0 / (v0 - vEnd));
        break;
    case PathCommand::Verb::Quad:
    case PathCommand::Verb::Conic: {
        float w = c.verb == PathCommand::Verb::Conic ? c.w : 1, v1 = v(c.x1, c.y1);
        float roots[2];
        int n = detail::curves::unitQuadRoots(v0 - 2 * w * v1 + vEnd, 2 * (w * v1 - v0), v0, roots);
        out.assign(roots, roots + n);
        break;
    }
    case PathCommand::Verb::Cubic: {
        float v1 = v(c.x1, c.y1), v2 = v(c.x2, c.y2);
        auto at = [&](float t) {
            float u = 1 - t;
            return v0 * u * u * u + 3 * v1 * u * u * t + 3 * v2 * u * t * t + vEnd * t * t * t;
        };
        const int steps = 32;
        for (int k = 0; k < steps; ++k) {
            float a = float(k) / steps, b = float(k + 1) / steps, fa = at(a), fb = at(b);
            if ((fa < 0) == (fb < 0) || fa == fb) continue;
            for (int i = 0; i < 40; ++i) {
                float mid = (a + b) / 2;
                ((at(mid) < 0) == (fa < 0) ? a : b) = mid;
            }
            out.push_back((a + b) / 2);
        }
        break;
    }
    default: break;
    }
    std::sort(out.begin(), out.end());
    return out;
}

// The piece of segment `c` (from `from`) between t0 and t1, appended to `p` (which is at its start).
void appendPiece(Path& p, const PathCommand& c, Offset from, float t0, float t1) {
    using namespace detail::curves;
    switch (c.verb) {
    case PathCommand::Verb::Line: {
        Offset e{from.x + (c.x - from.x) * t1, from.y + (c.y - from.y) * t1};
        p.lineTo(e.x, e.y);
        break;
    }
    case PathCommand::Verb::Quad:
    case PathCommand::Verb::Conic: {
        Conic k = Conic{{from, {c.x1, c.y1}, {c.x, c.y}}, c.verb == PathCommand::Verb::Conic ? c.w : 1}.between(t0, t1);
        p.conicTo(k.p[1].x, k.p[1].y, k.p[2].x, k.p[2].y, k.w);
        break;
    }
    case PathCommand::Verb::Cubic: {
        Offset src[4] = {from, {c.x1, c.y1}, {c.x2, c.y2}, {c.x, c.y}}, piece[4];
        chopCubicBetween(src, piece, t0, t1);
        p.cubicTo(piece[1].x, piece[1].y, piece[2].x, piece[2].y, piece[3].x, piece[3].y);
        break;
    }
    default: break;
    }
}

Offset pieceStart(const PathCommand& c, Offset from, float t) {
    using namespace detail::curves;
    switch (c.verb) {
    case PathCommand::Verb::Line: return {from.x + (c.x - from.x) * t, from.y + (c.y - from.y) * t};
    case PathCommand::Verb::Quad:
    case PathCommand::Verb::Conic: return Conic{{from, {c.x1, c.y1}, {c.x, c.y}}, c.verb == PathCommand::Verb::Conic ? c.w : 1}.at(t);
    case PathCommand::Verb::Cubic: {
        Offset src[4] = {from, {c.x1, c.y1}, {c.x2, c.y2}, {c.x, c.y}};
        return cubicAt(src, t);
    }
    default: return {c.x, c.y};
    }
}

// What of `path` is in front of the eye (w at least `edge`, Skia's kW0PlaneDistance): each
// segment cut where it crosses, the pieces behind left out, the gaps bridged by lines.
Path inFront(const Path& path, const std::array<float, 9>& m, float edge) {
    Path out;
    out.fillType = path.fillType;
    Offset current, start;
    bool begun = false;  // the contour has a piece in front so far
    auto front = [&](Offset q) { return m[6] * q.x + m[7] * q.y + m[8] >= edge; };
    for (const PathCommand& c : path.commands()) {
        switch (c.verb) {
        case PathCommand::Verb::Move:
            current = start = {c.x, c.y};
            begun = false;
            break;
        case PathCommand::Verb::Close:
            if (begun) out.close();
            current = start;
            begun = false;
            break;
        default: {
            std::vector<float> ts = wCrossings(c, current, m, edge);
            ts.insert(ts.begin(), 0.f);
            ts.push_back(1.f);
            for (size_t k = 0; k + 1 < ts.size(); ++k) {
                float t0 = ts[k], t1 = ts[k + 1];
                if (t1 - t0 <= 0 || !front(pieceStart(c, current, (t0 + t1) / 2))) continue;
                Offset s0 = pieceStart(c, current, t0);
                if (!begun) out.moveTo(s0.x, s0.y);
                else if (t0 > 0) out.lineTo(s0.x, s0.y);  // across what was behind (a piece from the start is where the pen is)
                begun = true;
                appendPiece(out, c, current, t0, t1);
            }
            current = {c.x, c.y};
        }
        }
    }
    return out;
}

} // namespace

namespace {

// A segment of a path (a contour's closing line too) and the point it starts from.
struct Segment {
    PathCommand c;
    Offset from;
};

std::vector<Segment> segmentsOf(const Path& path) {
    std::vector<Segment> out;
    Offset current, start;
    bool open = false;
    auto closeContour = [&] {
        if (open && (current.x != start.x || current.y != start.y)) out.push_back({{PathCommand::Verb::Line, 0, 0, 0, 0, start.x, start.y}, current});
        open = false;
        current = start;
    };
    for (const PathCommand& c : path.commands()) {
        if (c.verb == PathCommand::Verb::Move) {
            closeContour();
            current = start = {c.x, c.y};
        } else if (c.verb == PathCommand::Verb::Close) {
            closeContour();
        } else {
            out.push_back({c, current});
            current = {c.x, c.y};
            open = true;
        }
    }
    closeContour();
    return out;
}

// How many chords keep within `tolerance` of a segment (its second differences bound how far a
// chord strays: d / 8 / n^2 for a quadratic, 3 d / 4 / n^2 for a cubic).
int chordsFor(const Segment& s, float tolerance) {
    const PathCommand& c = s.c;
    auto dd = [](Offset a, Offset b, Offset e) { return std::hypot(a.x - 2 * b.x + e.x, a.y - 2 * b.y + e.y); };
    float n = 1;
    switch (c.verb) {
    case PathCommand::Verb::Quad:
    case PathCommand::Verb::Conic: n = std::sqrt(dd(s.from, {c.x1, c.y1}, {c.x, c.y}) / (8 * tolerance)); break;
    case PathCommand::Verb::Cubic:
        n = std::sqrt(0.75f * std::max(dd(s.from, {c.x1, c.y1}, {c.x2, c.y2}), dd({c.x1, c.y1}, {c.x2, c.y2}, {c.x, c.y})) / tolerance);
        break;
    default: break;
    }
    return std::clamp(int(std::ceil(n)), 1, 512);
}

} // namespace

// Done on the segments cut into fine chords (within 0.01 dp of the curves) that remember where
// along their segment they lie: every chord split where another crosses it, the pieces kept
// whose two sides differ in the result (by where the segments themselves pass, exactly),
// turned to have the inside on their left, chained into contours; then each run of pieces of
// one segment becomes that segment's piece (a curve's the curve between those two places).
bool Path::op(const Path& a, const Path& b, PathOperation operation) {
    std::vector<Segment> segs = segmentsOf(a), more = segmentsOf(b);
    segs.insert(segs.end(), more.begin(), more.end());
    struct Side {
        Offset from, to;
        int seg;
        float t0, t1;
    };
    std::vector<Side> sides;
    constexpr float kTolerance = 0.01f;
    for (size_t i = 0; i < segs.size(); ++i) {
        int n = chordsFor(segs[i], kTolerance);
        Offset prev = segs[i].from;
        for (int k = 1; k <= n; ++k) {
            float t = float(k) / float(n);
            Offset q = k == n ? Offset{segs[i].c.x, segs[i].c.y} : pieceStart(segs[i].c, segs[i].from, t);
            sides.push_back({prev, q, int(i), float(k - 1) / float(n), t});
            prev = q;
        }
    }
    std::vector<std::vector<float>> cuts(sides.size());  // where along each side (0..1) it is split
    auto cross = [](Offset u, Offset v) { return u.x * v.y - u.y * v.x; };
    constexpr float kEps = 1e-6f;
    for (size_t i = 0; i < sides.size(); ++i) {
        const Side& s = sides[i];
        Offset d1{s.to.x - s.from.x, s.to.y - s.from.y};
        for (size_t j = i + 1; j < sides.size(); ++j) {
            const Side& t = sides[j];
            if (std::max(s.from.x, s.to.x) < std::min(t.from.x, t.to.x) - kEps || std::max(t.from.x, t.to.x) < std::min(s.from.x, s.to.x) - kEps ||
                std::max(s.from.y, s.to.y) < std::min(t.from.y, t.to.y) - kEps || std::max(t.from.y, t.to.y) < std::min(s.from.y, s.to.y) - kEps)
                continue;
            Offset d2{t.to.x - t.from.x, t.to.y - t.from.y}, w{t.from.x - s.from.x, t.from.y - s.from.y};
            float denom = cross(d1, d2);
            float l1 = d1.x * d1.x + d1.y * d1.y, l2 = d2.x * d2.x + d2.y * d2.y;
            if (std::abs(denom) > 1e-9f * std::sqrt(l1 * l2)) {  // crossing lines
                float u = cross(w, d2) / denom, v = cross(w, d1) / denom;
                if (u > kEps && u < 1 - kEps && v > -kEps && v < 1 + kEps) cuts[i].push_back(u);
                if (v > kEps && v < 1 - kEps && u > -kEps && u < 1 + kEps) cuts[j].push_back(v);
            } else if (std::abs(cross(w, d1)) <= 1e-6f * std::sqrt(l1) + kEps) {  // on one line: each splits at the other's ends
                auto along = [](Offset p, Offset from, Offset d, float l) { return ((p.x - from.x) * d.x + (p.y - from.y) * d.y) / l; };
                for (Offset p : {t.from, t.to})
                    if (float u = l1 > 0 ? along(p, s.from, d1, l1) : 0; u > kEps && u < 1 - kEps) cuts[i].push_back(u);
                for (Offset p : {s.from, s.to})
                    if (float v = l2 > 0 ? along(p, t.from, d2, l2) : 0; v > kEps && v < 1 - kEps) cuts[j].push_back(v);
            }
        }
    }
    // The pieces whose two sides differ in the result, turned to have the inside on their left.
    auto inA = [&](Offset q) { return detail::curves::contains(a.commands_, a.fillType, q); };
    auto inB = [&](Offset q) { return detail::curves::contains(b.commands_, b.fillType, q); };
    std::vector<Side> kept;
    for (size_t i = 0; i < sides.size(); ++i) {
        std::vector<float>& c = cuts[i];
        std::sort(c.begin(), c.end());
        c.insert(c.begin(), 0.f);
        c.push_back(1.f);
        const Side& s = sides[i];
        for (size_t k = 0; k + 1 < c.size(); ++k) {
            if (c[k + 1] - c[k] < kEps) continue;
            Offset p = mix(s.from, s.to, c[k]), q = mix(s.from, s.to, c[k + 1]);
            float len = distance(p, q);
            if (len < 1e-5f) continue;
            float ta = s.t0 + (s.t1 - s.t0) * c[k], tb = s.t0 + (s.t1 - s.t0) * c[k + 1];
            // Either side of the segment itself, where it passes between the two ends.
            Offset mid = pieceStart(segs[size_t(s.seg)].c, segs[size_t(s.seg)].from, (ta + tb) / 2), normal{-(q.y - p.y) / len, (q.x - p.x) / len};
            float e = std::min(1e-3f, len * 0.25f);
            Offset left{mid.x + normal.x * e, mid.y + normal.y * e}, right{mid.x - normal.x * e, mid.y - normal.y * e};
            bool inLeft = combined(operation, inA(left), inB(left)), inRight = combined(operation, inA(right), inB(right));
            if (inLeft == inRight) continue;
            kept.push_back(inLeft ? Side{p, q, s.seg, ta, tb} : Side{q, p, s.seg, tb, ta});
        }
    }
    // The same piece twice (sides the two paths share): one is enough.
    auto same = [](Offset u, Offset v) { return std::abs(u.x - v.x) < 1e-4f && std::abs(u.y - v.y) < 1e-4f; };
    std::vector<Side> pieces;
    for (const Side& s : kept)
        if (std::none_of(pieces.begin(), pieces.end(), [&](const Side& t) { return same(s.from, t.from) && same(s.to, t.to); }))
            pieces.push_back(s);
    // Chained end to start into closed contours.
    reset();
    fillType = FillType::NonZero;
    std::vector<bool> used(pieces.size(), false);
    for (size_t first = 0; first < pieces.size(); ++first) {
        if (used[first]) continue;
        std::vector<size_t> loop{first};
        used[first] = true;
        Offset end = pieces[first].to, previous = pieces[first].from;
        while (!same(end, pieces[first].from)) {
            // Where pieces meet at a point (shapes touching at a corner), the one turning most
            // towards the inside keeps to the same shape: each one gets its own contour.
            size_t next = pieces.size();
            float bestTurn = -10;
            Offset in{end.x - previous.x, end.y - previous.y};
            for (size_t k = 0; k < pieces.size(); ++k) {
                if (used[k] || !same(pieces[k].from, end)) continue;
                Offset out{pieces[k].to.x - pieces[k].from.x, pieces[k].to.y - pieces[k].from.y};
                float turn = std::atan2(cross(in, out), in.x * out.x + in.y * out.y);
                if (turn > 3.14159f) turn = -turn;  // straight back: the last choice
                if (turn > bestTurn) bestTurn = turn, next = k;
            }
            if (next == pieces.size()) break;  // shouldn't happen: an open end
            previous = pieces[next].from;
            loop.push_back(next);
            used[next] = true;
            end = pieces[next].to;
        }
        if (loop.size() < 2) continue;
        // Started where a run of one segment's pieces starts (not in the middle of one, which would
        // split it in two).
        auto continues = [&](const Side& before, const Side& after) {
            return after.seg == before.seg && std::abs(after.t0 - before.t1) <= 1e-5f && (after.t1 > after.t0) == (before.t1 > before.t0);
        };
        for (size_t k = 0; k < loop.size(); ++k)
            if (!continues(pieces[loop[(k + loop.size() - 1) % loop.size()]], pieces[loop[k]])) {
                std::rotate(loop.begin(), loop.begin() + std::ptrdiff_t(k), loop.end());
                break;
            }
        // Runs of pieces of one segment, one after another along it: that segment's piece.
        moveTo(pieces[loop[0]].from.x, pieces[loop[0]].from.y);
        for (size_t k = 0; k < loop.size();) {
            const Side& s = pieces[loop[k]];
            float t0 = s.t0, t1 = s.t1;
            size_t r = k + 1;
            while (r < loop.size() && continues(pieces[loop[r - 1]], pieces[loop[r]])) t1 = pieces[loop[r++]].t1;
            const Segment& seg = segs[size_t(s.seg)];
            Offset to = pieces[loop[r - 1]].to;
            if (seg.c.verb == PathCommand::Verb::Line) {
                lineTo(to.x, to.y);
            } else {
                // The curve between the two places (turned round if run backwards), ending where the run does.
                Path piece;
                piece.moveTo(0, 0);
                appendPiece(piece, seg.c, seg.from, std::min(t0, t1), std::max(t0, t1));
                PathCommand c = piece.commands_.back();
                if (t1 < t0) {
                    if (c.verb == PathCommand::Verb::Cubic) std::swap(c.x1, c.x2), std::swap(c.y1, c.y2);
                }
                switch (c.verb) {
                case PathCommand::Verb::Quad: quadraticTo(c.x1, c.y1, to.x, to.y); break;
                case PathCommand::Verb::Conic: conicTo(c.x1, c.y1, to.x, to.y, c.w); break;
                case PathCommand::Verb::Cubic: cubicTo(c.x1, c.y1, c.x2, c.y2, to.x, to.y); break;
                default: lineTo(to.x, to.y); break;
                }
            }
            k = r;
        }
        close();
    }
    return !isEmpty();
}

Path Path::combine(PathOperation operation, const Path& a, const Path& b) {
    Path out;
    out.op(a, b, operation);
    return out;
}

void PathMeasure::setPath(const Path& path, bool forceClosed) {
    contours_.clear();
    current_ = 0;
    for (Polyline& l : flatten(path)) {
        Contour c;
        c.points = std::move(l.points);
        if ((l.closed || forceClosed) && c.points.size() > 1) c.points.push_back(c.points.front());
        float total = 0;
        c.lengths.push_back(0);
        for (size_t i = 1; i < c.points.size(); ++i) c.lengths.push_back(total += distance(c.points[i - 1], c.points[i]));
        if (total > 0) contours_.push_back(std::move(c));  // nothing to measure in a lone point
    }
}

float PathMeasure::length() const { return current_ < contours_.size() ? contours_[current_].lengths.back() : 0; }

bool PathMeasure::nextContour() {
    if (current_ + 1 >= contours_.size()) return false;
    ++current_;
    return true;
}

namespace {
// The segment of a contour a distance falls in (its end point's index) and how far into it.
std::pair<size_t, float> segmentAt(const std::vector<float>& lengths, float d) {
    size_t i = size_t(std::upper_bound(lengths.begin(), lengths.end(), d) - lengths.begin());
    i = std::clamp<size_t>(i, 1, lengths.size() - 1);
    float span = lengths[i] - lengths[i - 1];
    return {i, span > 0 ? (d - lengths[i - 1]) / span : 0};
}
} // namespace

Offset PathMeasure::getPosition(float d) const {
    if (current_ >= contours_.size()) return {};
    const Contour& c = contours_[current_];
    auto [i, t] = segmentAt(c.lengths, std::clamp(d, 0.f, c.lengths.back()));
    return mix(c.points[i - 1], c.points[i], t);
}

Offset PathMeasure::getTangent(float d) const {
    if (current_ >= contours_.size()) return {};
    const Contour& c = contours_[current_];
    size_t i = segmentAt(c.lengths, std::clamp(d, 0.f, c.lengths.back())).first;
    while (i + 1 < c.points.size() && distance(c.points[i - 1], c.points[i]) <= 0) ++i;
    float len = distance(c.points[i - 1], c.points[i]);
    return len > 0 ? Offset{(c.points[i].x - c.points[i - 1].x) / len, (c.points[i].y - c.points[i - 1].y) / len} : Offset{};
}

bool PathMeasure::getSegment(float start, float stop, Path& destination, bool startWithMoveTo) const {
    if (current_ >= contours_.size()) return false;
    const Contour& c = contours_[current_];
    start = std::clamp(start, 0.f, c.lengths.back());
    stop = std::clamp(stop, 0.f, c.lengths.back());
    if (start >= stop) return false;
    Offset from = getPosition(start);
    if (startWithMoveTo || destination.isEmpty()) destination.moveTo(from.x, from.y);
    else destination.lineTo(from.x, from.y);
    for (size_t i = 1; i < c.points.size(); ++i)  // the points between
        if (c.lengths[i] > start && c.lengths[i] < stop) destination.lineTo(c.points[i].x, c.points[i].y);
    Offset to = getPosition(stop);
    destination.lineTo(to.x, to.y);
    return true;
}

namespace {

constexpr float kRoot2Over2 = 0.707106781f;
constexpr float kNearlyZeroDp = 1.f / (1 << 12);  // (SK_ScalarNearlyZero)

// The four corners (top left, top right, bottom right, bottom left) or an oval's four extremes
// (top, right, bottom, left) from `start`, clockwise or not (Skia's SkPath_PointIterator).
struct Corners {
    std::array<Offset, 4> points;
    unsigned at, step;
    Corners(std::array<Offset, 4> pts, bool clockwise, unsigned start) : points(pts), at(start % 4), step(clockwise ? 1 : 3) {}
    Offset current() const { return points[at]; }
    Offset next() { return points[at = (at + step) % 4]; }
};
std::array<Offset, 4> cornersOf(const Rect& r) { return {Offset{r.x, r.y}, Offset{r.right(), r.y}, Offset{r.right(), r.bottom()}, Offset{r.x, r.bottom()}}; }
std::array<Offset, 4> extremesOf(const Rect& r) {
    float cx = r.x + r.width / 2, cy = r.y + r.height / 2;
    return {Offset{cx, r.y}, Offset{r.right(), cy}, Offset{cx, r.bottom()}, Offset{r.x, cy}};
}

void addRectFrom(Path& p, const Rect& r, bool clockwise, unsigned start) {
    Corners c(cornersOf(r), clockwise, start);
    Offset o = c.current();
    p.moveTo(o.x, o.y);
    for (int i = 0; i < 3; ++i) o = c.next(), p.lineTo(o.x, o.y);
    p.close();
}

void addOvalFrom(Path& p, const Rect& r, bool clockwise, unsigned start) {
    Corners oval(extremesOf(r), clockwise, start), rect(cornersOf(r), clockwise, start + (clockwise ? 0 : 1));
    Offset o = oval.current();
    p.moveTo(o.x, o.y);
    for (int i = 0; i < 4; ++i) {
        Offset control = rect.next(), end = oval.next();
        p.conicTo(control.x, control.y, end.x, end.y, kRoot2Over2);
    }
    p.close();
}

// Skia's SkRRect from a rect's corner radii: a corner with either radius 0 square, the radii
// fitted (all by one factor); what it is: a rect, an oval or a round rect.
enum class RRectKind { Rect, Oval, Rounded };
RRectKind kindOf(const RoundRect& f) {
    std::array<CornerRadius, 4> r{f.topLeft, f.topRight, f.bottomRight, f.bottomLeft};
    if (std::all_of(r.begin(), r.end(), [](CornerRadius c) { return c.x == 0 && c.y == 0; })) return RRectKind::Rect;
    if (std::all_of(r.begin(), r.end(), [&](CornerRadius c) { return c == r[0]; }) && r[0].x >= f.rect.width / 2 && r[0].y >= f.rect.height / 2)
        return RRectKind::Oval;
    return RRectKind::Rounded;
}

// Skia's round rect contour from point `start` of its eight (where each corner meets each
// side, from the top left one's top end, clockwise), clockwise or not: lines and quarter
// ellipses (conics), from a line (an odd start clockwise or an even one counterclockwise
// starts with a conic instead).
void addRRectFrom(Path& p, const RoundRect& f, bool clockwise, unsigned start) {
    const Rect& b = f.rect;
    float L = b.x, T = b.y, R = b.right(), B = b.bottom();
    std::array<Offset, 8> pts{Offset{L + f.topLeft.x, T},     Offset{R - f.topRight.x, T},    Offset{R, T + f.topRight.y},
                              Offset{R, B - f.bottomRight.y}, Offset{R - f.bottomRight.x, B}, Offset{L + f.bottomLeft.x, B},
                              Offset{L, B - f.bottomLeft.y},  Offset{L, T + f.topLeft.y}};
    unsigned at = start % 8, step = clockwise ? 1 : 7;
    auto next = [&] { return pts[at = (at + step) % 8]; };
    Corners rect(cornersOf(b), clockwise, start / 2 + (clockwise ? 0 : 1));
    bool conicFirst = (start & 1) == (clockwise ? 1u : 0u);
    Offset o = pts[at];
    p.moveTo(o.x, o.y);
    for (int i = 0; i < 4; ++i) {
        if (!conicFirst) o = next(), p.lineTo(o.x, o.y);
        Offset control = rect.next(), end = next();
        p.conicTo(control.x, control.y, end.x, end.y, kRoot2Over2);
        if (conicFirst && i < 3) o = next(), p.lineTo(o.x, o.y);  // (the last line: close())
    }
    p.close();
}

RoundRect skiaRoundRect(const RoundRect& rr) {
    auto square = [](CornerRadius c) { return c.x <= 0 || c.y <= 0 ? CornerRadius{} : c; };
    return RoundRect{rr.rect, square(rr.topLeft), square(rr.topRight), square(rr.bottomRight), square(rr.bottomLeft)}.fitted();
}

void addRoundRectFrom(Path& p, const RoundRect& rr, bool clockwise, unsigned start) {
    RoundRect f = skiaRoundRect(rr);
    switch (kindOf(f)) {  // (SkPathPriv::SimplifyRRect)
    case RRectKind::Rect: return addRectFrom(p, f.rect, clockwise, (start + 1) / 2);
    case RRectKind::Oval: return addOvalFrom(p, f.rect, clockwise, start / 2);
    case RRectKind::Rounded: return addRRectFrom(p, f, clockwise, start);
    }
}

float sinSnapped(float radians) {
    float v = std::sin(radians);
    return std::abs(v) <= kNearlyZeroDp ? 0 : v;
}
float cosSnapped(float radians) {
    float v = std::cos(radians);
    return std::abs(v) <= kNearlyZeroDp ? 0 : v;
}

} // namespace

void Path::addRect(const Rect& r, Direction direction) { addRectFrom(*this, r, direction == Direction::Clockwise, 0); }

void Path::addOval(const Rect& r, Direction direction) { addOvalFrom(*this, r, direction == Direction::Clockwise, 1); }

void Path::addRoundRect(const Rect& r, float radius) {
    CornerRadius c{radius, radius};
    addRoundRect(RoundRect{r, c, c, c, c});
}

void Path::addRoundRect(const RoundRect& rr, Direction direction) {
    bool clockwise = direction == Direction::Clockwise;
    addRoundRectFrom(*this, rr, clockwise, clockwise ? 6 : 7);
}

// As Skia's SkPathBuilder::arcTo: a lone point where it is one; the unit circle's arc from the
// start's direction to the end's as conics, scaled onto the oval; a sweep too small for those a
// line to its end.
void Path::arcTo(const Rect& oval, float startAngleDegrees, float sweepAngleDegrees, bool forceMoveTo) {
    if (oval.width < 0 || oval.height < 0) return;
    float startAngle = std::fmod(startAngleDegrees, 360.f), sweep = sweepAngleDegrees;
    if (commands_.empty()) forceMoveTo = true;
    auto addPoint = [&](Offset q) {
        if (forceMoveTo) moveTo(q.x, q.y);
        else if (std::abs(current_.x - q.x) > kNearlyZeroDp || std::abs(current_.y - q.y) > kNearlyZeroDp) lineTo(q.x, q.y);
    };
    float cx = oval.x + oval.width / 2, cy = oval.y + oval.height / 2;
    if (sweep == 0 && (startAngle == 0 || startAngle == 360)) {  // a lone point (moving in and out of ovals)
        Offset q{oval.right(), cy};
        return forceMoveTo ? moveTo(q.x, q.y) : lineTo(q.x, q.y);
    }
    if (oval.width == 0 && oval.height == 0) {
        Offset q{oval.right(), oval.y};
        return forceMoveTo ? moveTo(q.x, q.y) : lineTo(q.x, q.y);
    }
    constexpr float kRadians = std::numbers::pi_v<float> / 180;
    float startRad = startAngle * kRadians, stopRad = (startAngle + sweep) * kRadians;
    Offset startV{cosSnapped(startRad), sinSnapped(startRad)}, stopV{cosSnapped(stopRad), sinSnapped(stopRad)};
    if (startV.x == stopV.x && startV.y == stopV.y) {
        float sw = std::abs(sweep);
        if (sw < 360 && sw > 359) {  // nearly a turn: nudged back until they differ
            float delta = std::copysign(1.f / 512, sweep);
            do {
                stopRad -= delta;
                stopV = {cosSnapped(stopRad), sinSnapped(stopRad)};
            } while (startV.x == stopV.x && startV.y == stopV.y);
        }
    }
    bool clockwise = sweep > 0;
    float rx = oval.width / 2, ry = oval.height / 2;
    if (startV.x == stopV.x && startV.y == stopV.y) {  // too small a sweep: its end
        float end = (startAngle + sweep) * kRadians;
        return addPoint({cx + rx * std::cos(end), cy + ry * std::sin(end)});
    }
    detail::curves::Conic conics[5];
    int count = detail::curves::Conic::unitArc(startV, stopV, clockwise, {rx, 0, cx, 0, ry, cy}, conics);
    if (count == 0) return addPoint({rx * stopV.x + cx, ry * stopV.y + cy});
    addPoint(conics[0].p[0]);
    for (int i = 0; i < count; ++i) conicTo(conics[i].p[1].x, conics[i].p[1].y, conics[i].p[2].x, conics[i].p[2].y, conics[i].w);
}

void Path::arcToRad(const Rect& oval, float startAngleRadians, float sweepAngleRadians, bool forceMoveTo) {
    arcTo(oval, startAngleRadians * 180 / std::numbers::pi_v<float>, sweepAngleRadians * 180 / std::numbers::pi_v<float>, forceMoveTo);
}

// As Skia's: a turn or more from a quarter's angle is the whole oval, from that quarter.
void Path::addArc(const Rect& oval, float startAngleDegrees, float sweepAngleDegrees) {
    if (oval.width <= 0 || oval.height <= 0 || sweepAngleDegrees == 0) return;
    if (std::abs(sweepAngleDegrees) >= 360) {
        float quarters = startAngleDegrees / 90, whole = std::round(quarters);
        if (std::abs(quarters - whole) <= kNearlyZeroDp) {
            float index = std::fmod(whole + 1, 4.f);
            if (index < 0) index += 4;
            return addOvalFrom(*this, oval, sweepAngleDegrees > 0, unsigned(index));
        }
    }
    arcTo(oval, startAngleDegrees, sweepAngleDegrees, true);
}

void Path::addArcRad(const Rect& oval, float startAngleRadians, float sweepAngleRadians) {
    addArc(oval, startAngleRadians * 180 / std::numbers::pi_v<float>, sweepAngleRadians * 180 / std::numbers::pi_v<float>);
}

void Path::addPath(const Path& path, Offset offset) {
    Path moved = path;
    moved.translate(offset);
    for (const PathCommand& c : moved.commands_) commands_.push_back(c);
    if (!moved.commands_.empty()) {
        current_ = moved.current_;
        start_ = moved.start_;
    }
}

void Path::translate(Offset o) {
    for (PathCommand& c : commands_) {
        switch (c.verb) {
        case PathCommand::Verb::Cubic: c.x2 += o.x, c.y2 += o.y; [[fallthrough]];
        case PathCommand::Verb::Quad:
        case PathCommand::Verb::Conic: c.x1 += o.x, c.y1 += o.y; [[fallthrough]];
        case PathCommand::Verb::Move:
        case PathCommand::Verb::Line: c.x += o.x, c.y += o.y; break;
        case PathCommand::Verb::Close: break;
        }
    }
    current_ = {current_.x + o.x, current_.y + o.y};
    start_ = {start_.x + o.x, start_.y + o.y};
}

void Path::reset() {
    commands_.clear();
    current_ = start_ = {};
}

RoundRect RoundRect::fitted() const {
    auto positive = [](CornerRadius c) { return CornerRadius{std::max(0.f, c.x), std::max(0.f, c.y)}; };
    RoundRect f{rect, positive(topLeft), positive(topRight), positive(bottomRight), positive(bottomLeft)};
    // The largest factor that fits every side's two corners in it, as Android draws a RoundRect.
    float k = 1;
    auto fit = [&](float side, float a, float b) {
        if (a + b > side && a + b > 0) k = std::min(k, std::max(0.f, side) / (a + b));
    };
    fit(rect.width, f.topLeft.x, f.topRight.x);
    fit(rect.width, f.bottomLeft.x, f.bottomRight.x);
    fit(rect.height, f.topLeft.y, f.bottomLeft.y);
    fit(rect.height, f.topRight.y, f.bottomRight.y);
    if (k < 1)
        for (CornerRadius* c : {&f.topLeft, &f.topRight, &f.bottomRight, &f.bottomLeft}) c->x *= k, c->y *= k;
    return f;
}

bool RoundRect::circular() const {
    RoundRect f = fitted();
    return f.topLeft.x == f.topLeft.y && f.topRight.x == f.topRight.y && f.bottomRight.x == f.bottomRight.y && f.bottomLeft.x == f.bottomLeft.y;
}

CornerRadii RoundRect::radii() const {
    RoundRect f = fitted();
    return {f.topLeft.x, f.topRight.x, f.bottomRight.x, f.bottomLeft.x};
}

Path Outline::asPath() const {
    if (kind == Kind::Generic) return path;
    Path p;
    if (kind == Kind::Rectangle) p.addRect(roundRect.rect);
    else p.addRoundRect(roundRect);
    return p;
}

Rect Path::bounds() const {
    if (commands_.empty()) return {};
    float x0 = kInfinity, y0 = kInfinity, x1 = -kInfinity, y1 = -kInfinity;
    auto add = [&](float x, float y) {
        x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
    };
    for (const PathCommand& c : commands_) {
        switch (c.verb) {
        case PathCommand::Verb::Cubic: add(c.x2, c.y2); [[fallthrough]];
        case PathCommand::Verb::Quad:
        case PathCommand::Verb::Conic: add(c.x1, c.y1); [[fallthrough]];
        case PathCommand::Verb::Move:
        case PathCommand::Verb::Line: add(c.x, c.y); break;
        case PathCommand::Verb::Close: break;
        }
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

bool Path::isConvex() const { return detail::curves::convex(commands_); }

bool operator==(const Path& a, const Path& b) { return a.fillType == b.fillType && a.commands_ == b.commands_; }


// As Skia's SkPath::transform: affinely, every point; in perspective, first cut to what is in
// front of the eye, then quadratics and conics as conics (their weights through the map:
// exact), cubics in four (approximate), every point divided by its w.
void Path::transform(const Matrix& matrix) {
    if (matrix.isIdentity() || commands_.empty()) return;
    const auto& v = matrix.values;
    std::array<float, 9> m{v[Matrix::ScaleX], v[Matrix::SkewX],        v[Matrix::TranslateX], v[Matrix::SkewY],       v[Matrix::ScaleY],
                           v[Matrix::TranslateY], v[Matrix::Perspective0], v[Matrix::Perspective1], v[Matrix::Perspective2]};
    auto map = [&](float x, float y) {
        float w = m[6] * x + m[7] * y + m[8];
        return Offset{(m[0] * x + m[1] * y + m[2]) / w, (m[3] * x + m[4] * y + m[5]) / w};
    };
    bool perspective = m[6] != 0 || m[7] != 0 || m[8] != 1;
    if (!perspective) {
        for (PathCommand& c : commands_) {
            for (auto [x, y] : {std::pair{&c.x, &c.y}, std::pair{&c.x1, &c.y1}, std::pair{&c.x2, &c.y2}}) {
                Offset q = map(*x, *y);
                *x = q.x, *y = q.y;
            }
        }
        current_ = map(current_.x, current_.y), start_ = map(start_.x, start_.y);
        return;
    }
    constexpr float kW0PlaneDistance = 1.f / (1 << 14);
    Path src = inFront(*this, m, kW0PlaneDistance);
    FillType fill = fillType;
    reset();
    fillType = fill;
    Offset current, start;
    for (const PathCommand& c : src.commands_) {
        Offset end = map(c.x, c.y);
        switch (c.verb) {
        case PathCommand::Verb::Move: moveTo(end.x, end.y), current = start = {c.x, c.y}; continue;
        case PathCommand::Verb::Line: lineTo(end.x, end.y); break;
        case PathCommand::Verb::Quad:
        case PathCommand::Verb::Conic: {
            Offset pts[3] = {current, {c.x1, c.y1}, {c.x, c.y}};
            float w = detail::curves::Conic::transformedWeight(pts, c.verb == PathCommand::Verb::Conic ? c.w : 1, m);
            Offset control = map(c.x1, c.y1);
            conicTo(control.x, control.y, end.x, end.y, w);
            break;
        }
        case PathCommand::Verb::Cubic: {
            Offset src4[4] = {current, {c.x1, c.y1}, {c.x2, c.y2}, {c.x, c.y}}, halves[7], quarters[7];
            detail::curves::chopCubicAt(src4, halves, 0.5f);
            for (int h = 0; h < 2; ++h) {
                detail::curves::chopCubicAt(halves + 3 * h, quarters, 0.5f);
                for (int q = 0; q < 2; ++q) {
                    const Offset* k = quarters + 3 * q;
                    Offset a = map(k[1].x, k[1].y), b = map(k[2].x, k[2].y), e = map(k[3].x, k[3].y);
                    cubicTo(a.x, a.y, b.x, b.y, e.x, e.y);
                }
            }
            break;
        }
        case PathCommand::Verb::Close: close(), current = start; continue;
        }
        current = {c.x, c.y};
    }
}

// ---- Shapes ------------------------------------------------------------------------------------

Outline Shape::createOutline(Size size, LayoutDirection layoutDirection) const {
    Rect r{0, 0, size.width, size.height};
    bool rtl = layoutDirection == LayoutDirection::Rtl && !absolute;
    switch (kind) {
    case Kind::Rectangle: return Outline::Rectangle(r);
    case Kind::Rounded:
    case Kind::Circle: {
        CornerRadii c = radiiFor(r, layoutDirection == LayoutDirection::Rtl);
        if (c.max() <= 0) return Outline::Rectangle(r);
        return Outline::Rounded({r, c.topLeft, c.topRight, c.bottomRight, c.bottomLeft});
    }
    case Kind::Cut: {  // as rounded corners fit (a side's two cuts within the shorter side), cut straight
        CornerRadii c = cornersFor(r, rtl);
        float tl = c.topLeft, tr = c.topRight, br = c.bottomRight, bl = c.bottomLeft, w = r.width, h = r.height;
        Path path;
        path.moveTo(tl, 0);
        path.lineTo(w - tr, 0);
        path.lineTo(w, tr);
        path.lineTo(w, h - br);
        path.lineTo(w - br, h);
        path.lineTo(bl, h);
        path.lineTo(0, h - bl);
        path.lineTo(0, tl);
        path.close();
        return Outline::Generic(std::move(path));
    }
    case Kind::Outline: {
        if (!outline || outline->points.size() < 3) return Outline::Rectangle(r);
        Path path;
        bool first = true;
        for (Offset u : outline->points) {
            float x = (rtl ? 1 - u.x : u.x) * r.width, y = u.y * r.height;
            first ? path.moveTo(x, y) : path.lineTo(x, y);
            first = false;
        }
        path.close();
        return Outline::Generic(std::move(path));
    }
    case Kind::Generic: return generic ? (*generic)(size, layoutDirection) : Outline::Rectangle(r);
    }
    return Outline::Rectangle(r);
}

Shape GenericShape(std::function<void(Path& path, Size size, LayoutDirection layoutDirection)> builder) {
    return OutlineShape([builder = std::move(builder)](Size size, LayoutDirection direction) {
        Path path;
        builder(path, size, direction);
        return Outline::Generic(std::move(path));
    });
}

Shape OutlineShape(std::function<Outline(Size size, LayoutDirection layoutDirection)> createOutline) {
    Shape s{Shape::Kind::Generic};
    s.generic = std::make_shared<const std::function<Outline(Size, LayoutDirection)>>(std::move(createOutline));
    return s;
}

namespace {
// A shape's outline over `rect`, and whether it is drawn as a rounded rect (its corners circles).
struct Placed {
    Outline outline;
    bool rounded;
    CornerRadii radii;
};
Placed place(const Rect& rect, const Shape& shape, bool rtl) {
    Placed p{shape.createOutline({rect.width, rect.height}, rtl ? LayoutDirection::Rtl : LayoutDirection::Ltr), false, {}};
    p.rounded = p.outline.kind == Outline::Kind::Rectangle || (p.outline.kind == Outline::Kind::Rounded && p.outline.roundRect.circular());
    if (p.outline.kind == Outline::Kind::Rounded) p.radii = p.outline.roundRect.radii();
    return p;
}
// The path a stroke `width` wide goes along so that it lies inside the shape over `rect` (from
// the rect's top left): the shape made half the width smaller, or its rounded rect with the
// corners that much smaller.
Path strokedPath(const Rect& rect, const Shape& shape, bool rtl, float width) {
    float h = width / 2;
    Size inner{std::max(0.f, rect.width - width), std::max(0.f, rect.height - width)};
    Outline full = shape.createOutline({rect.width, rect.height}, rtl ? LayoutDirection::Rtl : LayoutDirection::Ltr);
    Path path;
    if (full.kind == Outline::Kind::Generic) {
        path = shape.createOutline(inner, rtl ? LayoutDirection::Rtl : LayoutDirection::Ltr).asPath();
        path.translate({h, h});
        return path;
    }
    RoundRect f = full.roundRect.fitted();
    auto less = [h](CornerRadius c) { return CornerRadius{std::max(0.f, c.x - h), std::max(0.f, c.y - h)}; };
    path.addRoundRect({{h, h, inner.width, inner.height}, less(f.topLeft), less(f.topRight), less(f.bottomRight), less(f.bottomLeft)});
    return path;
}
} // namespace

// As Compose's DropShadowPainter and InnerShadowPainter draw a path's shadow (blurred masks:
// Android's BlurMaskFilter, Android's sigma for the radius): the masks drawn opaque in layers that
// blur them (Decal), in one faded by the color's alpha.
void GraphicsContext::drawShadowPath(const Path& outline, Offset origin, const Shadow& shadow, bool inner) {
    if (shadow.color.alpha() <= 0 || outline.isEmpty()) return;
    Rect b = outline.bounds();
    b.x += origin.x, b.y += origin.y;
    Color opaque = shadow.color.copy(1);
    RenderEffect blur = BlurEffect(std::max(0.f, shadow.blurRadius), std::max(0.f, shadow.blurRadius), TileMode::Decal);
    float spread = std::max(0.f, shadow.spread);
    PathStroke band{2 * spread};  // (mitered, as a Paint's stroke is by default)
    Offset moved{origin.x + shadow.offset.x, origin.y + shadow.offset.y};
    if (!inner) {
        float grow = spread * band.miter;  // (a miter reaches out that far at most)
        Rect at{b.x + shadow.offset.x - grow, b.y + shadow.offset.y - grow, b.width + 2 * grow, b.height + 2 * grow};
        Rect reach = blur.outputBounds(at);
        if (quickReject(reach)) return;
        pushLayer(reach, CornerRadii(), shadow.color.alpha());
        pushEffectLayer(at, blur, 1);
        drawPath(outline, moved, Brush::solid(opaque), at, {});
        popLayer();
        if (spread > 0) {
            pushEffectLayer(at, blur, 1);
            drawPath(outline, moved, Brush::solid(opaque), at, band);
            popLayer();
        }
        popLayer();
        return;
    }
    if (quickReject(b)) return;
    // The color 2 dp past the bounds (a pixel and a half at the least): what cuts the edge's pixels
    // is the outline (the clip), not the fill's own edge.
    Rect at{b.x - 2, b.y - 2, b.width + 4, b.height + 4};
    pushClipPath(outline, origin);
    pushLayer(at, CornerRadii(), shadow.color.alpha());
    fillRoundRect(at, CornerRadii(), opaque);
    // Less the hole blurred.
    Rect hole{at.x + shadow.offset.x, at.y + shadow.offset.y, at.width, at.height};
    pushBlendMode(BlendMode::DstOut);
    pushEffectLayer(hole, blur, 1);
    drawPath(outline, moved, Brush::solid(opaque), hole, {});
    if (spread > 0) {
        pushBlendMode(BlendMode::Clear);
        drawPath(outline, moved, Brush::solid(opaque), hole, band);
        popBlendMode();
    }
    popLayer();
    popBlendMode();
    popLayer();
    popClip();
}

void GraphicsContext::fillShape(const Rect& rect, const Shape& shape, bool rtl, Color color) {
    Placed p = place(rect, shape, rtl);
    if (p.rounded) fillRoundRect(rect, p.radii, color);
    else drawPath(p.outline.asPath(), {rect.x, rect.y}, Brush::solid(color), rect, PathStroke{});
}

void GraphicsContext::fillShape(const Rect& rect, const Shape& shape, bool rtl, const Brush& brush, const Rect& brushArea) {
    Placed p = place(rect, shape, rtl);
    if (p.rounded) fillGradient(rect, p.radii, brush, brushArea);
    else drawPath(p.outline.asPath(), {rect.x, rect.y}, brush, brushArea, PathStroke{});
}

void GraphicsContext::strokeShape(const Rect& rect, const Shape& shape, bool rtl, float width, Color color) {
    Placed p = place(rect, shape, rtl);
    if (p.rounded) return strokeRoundRect(rect, p.radii, width, color);
    strokeShape(rect, shape, rtl, width, Brush::solid(color), rect);
}

void GraphicsContext::strokeShape(const Rect& rect, const Shape& shape, bool rtl, float width, const Brush& brush, const Rect& brushArea) {
    drawPath(strokedPath(rect, shape, rtl, width), {rect.x, rect.y}, brush, brushArea, PathStroke{.width = width, .join = StrokeJoin::Round});
}

void GraphicsContext::pushShapeLayer(const Rect& bounds, const Shape& shape, bool rtl, float alpha, const LayerTransform& transform) {
    Placed p = place(bounds, shape, rtl);
    if (p.rounded) pushLayer(bounds, p.radii, alpha, transform);
    else pushOutlineLayer(bounds, p.outline.asPath(), alpha, transform);
}

void GraphicsContext::pushClipShape(const Rect& rect, const Shape& shape, bool rtl, ClipOp op) {
    Placed p = place(rect, shape, rtl);
    if (p.rounded && op == ClipOp::Intersect && p.radii.max() <= 0) pushClip(rect);
    else if (p.rounded) pushClipRoundRect(rect, p.radii, op);
    else pushClipPath(p.outline.asPath(), {rect.x, rect.y}, op);
}

// As Compose draws a dropShadow or innerShadow: under it, from a bitmap as big as the shape out to
// its radius and spread, moved (what is past that cut off); painted with its color, or its brush
// over what it covers (the brush SrcIn the shadow's mask); at its alpha, by its blend mode.
void GraphicsContext::drawShapeShadow(const Rect& rect, const Shape& shape, bool rtl, const Shadow& shadow, bool inner) {
    Placed p = place(rect, shape, rtl);
    Shadow s = shadow;
    s.brush.reset(), s.alpha = 1, s.blendMode = BlendMode::SrcOver;
    float alpha = std::clamp(shadow.alpha, 0.f, 1.f);
    std::optional<Brush> brush = shadow.brush;
    if (brush && brush->isSolid()) s.color = brush->stops().front().color, brush.reset();
    auto draw = [&] {
        if (p.rounded) drawShadow(rect, p.radii, s, inner);
        else drawShadowPath(p.outline.asPath(), {rect.x, rect.y}, s, inner);
    };
    float out = std::max(0.f, shadow.blurRadius) + std::max(0.f, shadow.spread);
    Rect bitmap{rect.x - out + shadow.offset.x, rect.y - out + shadow.offset.y, rect.width + 2 * out, rect.height + 2 * out};
    if (!inner) pushClip(bitmap);
    if (shadow.blendMode != BlendMode::SrcOver) pushBlendMode(shadow.blendMode);
    if (brush) {
        Rect area = inner ? rect : bitmap;
        pushLayer(area, CornerRadii(), alpha);
        s.color = colors::White;
        draw();
        pushBlendMode(BlendMode::SrcIn);
        fillGradient(area, CornerRadii(), *brush, area);
        popBlendMode();
        popLayer();
    } else {
        s.color = s.color.copy(s.color.alpha() * alpha);
        draw();
    }
    if (shadow.blendMode != BlendMode::SrcOver) popBlendMode();
    if (!inner) popClip();
}

namespace {
// `path` scaled by `scale` about the origin and moved by `by`.
Path scaledPath(const Path& path, float scale, Offset by) {
    Path out;
    out.fillType = path.fillType;
    auto x = [&](float v) { return v * scale + by.x; };
    auto y = [&](float v) { return v * scale + by.y; };
    for (const PathCommand& c : path.commands()) {
        switch (c.verb) {
        case PathCommand::Verb::Move: out.moveTo(x(c.x), y(c.y)); break;
        case PathCommand::Verb::Line: out.lineTo(x(c.x), y(c.y)); break;
        case PathCommand::Verb::Quad: out.quadraticTo(x(c.x1), y(c.y1), x(c.x), y(c.y)); break;
        case PathCommand::Verb::Conic: out.conicTo(x(c.x1), y(c.y1), x(c.x), y(c.y), c.w); break;
        case PathCommand::Verb::Cubic: out.cubicTo(x(c.x1), y(c.y1), x(c.x2), y(c.y2), x(c.x), y(c.y)); break;
        case PathCommand::Verb::Close: out.close(); break;
        }
    }
    return out;
}
} // namespace

Rect ElevationShadow::reach(const Rect& bounds, const ShadowLight& light, Offset lightCenter) const {
    auto at = [&](float x, float y) { return plane[0] * x + plane[1] * y + plane[2]; };
    float z = std::max({at(bounds.x, bounds.y), at(bounds.right(), bounds.y), at(bounds.x, bounds.bottom()), at(bounds.right(), bounds.bottom())});
    float k = light.density, zp = z * k, lz = light.z * k;
    float ambient = std::min(zp / 2, 150.f) / k;
    Rect out{bounds.x - ambient, bounds.y - ambient, bounds.width + 2 * ambient, bounds.height + 2 * ambient};
    if (lz > zp && zp > 0) {
        float ratio = std::clamp(zp / (lz - zp), 0.f, 0.95f), scale = std::clamp(lz / (lz - zp), 1.f, 1.95f), blur = light.radius * ratio;
        Rect spot{bounds.x * scale - ratio * lightCenter.x - blur, bounds.y * scale - ratio * lightCenter.y - blur, bounds.width * scale + 2 * blur,
                  bounds.height * scale + 2 * blur};
        float l = std::min(out.x, spot.x), t = std::min(out.y, spot.y), r = std::max(out.right(), spot.right()), b = std::max(out.bottom(), spot.bottom());
        out = {l, t, r - l, b - t};
    }
    return {out.x - 1, out.y - 1, out.width + 2, out.height + 2};
}

// Skia's SkShadowUtils, when it blurs (SkDevice::drawShadow's last resort), in px: the ambient
// shadow the outline stroked and filled (stroked 0.5 (r - b) wide, r = z / 2 up to 150, b = 0.5 r
// (1 + z / 128)) and blurred by b; the spot shadow the outline cast from the light onto the plane
// under it (scaled by Lz / (Lz - z) about the light's foot) and blurred by its radius z / (Lz - z)
// times the light's. Both blurred as Android blurs (0.57735 r + 0.5 px), through drawShadow and
// drawShadowPath (a stroke and fill's spread as a shadow's).
void GraphicsContext::drawElevationShadow(const Outline& outline, Offset origin, const ElevationShadow& shadow) {
    float alpha = std::clamp(shadow.alpha, 0.f, 1.f), k = light.density;
    Rect b = outline.bounds();
    b.x += origin.x, b.y += origin.y;
    bool rounded = outline.kind == Outline::Kind::Rectangle || (outline.kind == Outline::Kind::Rounded && outline.roundRect.circular());
    CornerRadii radii = outline.kind == Outline::Kind::Rounded ? outline.roundRect.radii() : CornerRadii();
    auto cast = [&](Rect r, CornerRadii corners, Shadow s, float scale, Offset by) {
        if (rounded) {
            r = {r.x * scale + by.x, r.y * scale + by.y, r.width * scale, r.height * scale};
            drawShadow(r, corners.scaled(scale), s, false);
        } else {
            Path p = outline.asPath();
            p.translate(origin);
            drawShadowPath(scaledPath(p, scale, by), {0, 0}, s, false);
        }
    };
    float flat = shadow.plane[0] * origin.x + shadow.plane[1] * origin.y + shadow.plane[2];  // (at the outline's origin, as Skia's)
    if (Color c = shadow.ambientColor; c.alpha() * light.ambientAlpha * alpha > 0 && flat > 0) {
        float z = flat * k, r = std::min(z / 2, 150.f), oneOverA = 1 + z / 128, blur = 0.5f * r * oneOverA;
        float stroke = std::max(0.f, 0.5f * (r - blur));  // (a negative width strokes nothing)
        cast(b, radii, Shadow{c.copy(c.alpha() * light.ambientAlpha * alpha), {}, blur / k, stroke / 2 / k}, 1, {});
    }
    Offset center = b.center();
    float z = (shadow.plane[0] * center.x + shadow.plane[1] * center.y + shadow.plane[2]) * k, lz = light.z * k;
    if (Color c = shadow.spotColor; c.alpha() * light.spotAlpha * alpha > 0 && z > 0 && lz > z) {
        float ratio = std::clamp(z / (lz - z), 0.f, 0.95f), scale = std::clamp(lz / (lz - z), 1.f, 1.95f);
        Offset l = lightCenter();
        cast(b, radii, Shadow{c.copy(c.alpha() * light.spotAlpha * alpha), {}, light.radius * k * ratio / k, 0}, scale, {-ratio * l.x, -ratio * l.y});
    }
}

} // namespace kompoz
