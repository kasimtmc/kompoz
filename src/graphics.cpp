#include <kompoz/graphics.hpp>

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
    return linearGradient(evenly(colors), start, end, tileMode);
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
    return radialGradient(evenly(colors), center, radius, tileMode);
}

Brush Brush::radialGradient(std::vector<ColorStop> stops, std::optional<Offset> center, float radius, TileMode tileMode) {
    Brush b(Kind::Radial, std::move(stops));
    b.hasCenter_ = center.has_value();
    b.start_ = center.value_or(Offset{});
    b.radius_ = radius;
    b.tileMode_ = tileMode;
    return b;
}

Brush Brush::sweepGradient(std::vector<Color> colors, std::optional<Offset> center) { return sweepGradient(evenly(colors), center); }

Brush Brush::sweepGradient(std::vector<ColorStop> stops, std::optional<Offset> center) {
    Brush b(Kind::Sweep, std::move(stops));
    b.hasCenter_ = center.has_value();
    b.start_ = center.value_or(Offset{});
    return b;
}

Brush Brush::scaled(float sx, float sy) const {
    Brush b = *this;  // an infinite end (the far edge) stays infinite
    b.start_ = {start_.x * sx, start_.y * sy};
    b.end_ = {end_.x * sx, end_.y * sy};
    b.radius_ = radius_ * std::sqrt(sx * sy);
    return b;
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
    case Kind::Solid: break;
    }
    return {};
}

float Brush::parameterAt(Offset p, const Rect& area) const {
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
    case Kind::Solid: break;
    }
    return 0;
}

Color Brush::colorAtParameter(float t) const {
    if (stops_.empty()) return colors::Transparent;
    if (kind_ == Kind::Solid || stops_.size() == 1) return stops_.front().color;
    switch (tileMode_) {
    case TileMode::Clamp: t = std::clamp(t, 0.f, 1.f); break;
    case TileMode::Repeated: t -= std::floor(t); break;
    case TileMode::Mirror: {
        t = std::fmod(std::abs(t), 2.f);
        if (t > 1) t = 2 - t;
        break;
    }
    case TileMode::Decal:
        if (t < 0 || t > 1) return colors::Transparent;
        break;
    }
    if (t <= stops_.front().offset) return stops_.front().color;
    for (size_t i = 1; i < stops_.size(); ++i) {
        const ColorStop &a = stops_[i - 1], &b = stops_[i];
        if (t <= b.offset) {
            float span = b.offset - a.offset;
            return lerp(a.color, b.color, span > 0 ? (t - a.offset) / span : 1.f);
        }
    }
    return stops_.back().color;
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
    const float in[4] = {c.r, c.g, c.b, c.a};
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
struct Rgb {
    float r, g, b;
};
float lum(Rgb c) { return 0.3f * c.r + 0.59f * c.g + 0.11f * c.b; }
Rgb clipColor(Rgb c) {
    float l = lum(c), n = std::min({c.r, c.g, c.b}), x = std::max({c.r, c.g, c.b});
    auto low = [&](float v) { return l + (v - l) * l / (l - n); };
    auto high = [&](float v) { return l + (v - l) * (1 - l) / (x - l); };
    if (n < 0 && l - n > 0) c = {low(c.r), low(c.g), low(c.b)};
    if (x > 1 && x - l > 0) c = {high(c.r), high(c.g), high(c.b)};
    return c;
}
Rgb setLum(Rgb c, float l) {
    float d = l - lum(c);
    return clipColor({c.r + d, c.g + d, c.b + d});
}
float sat(Rgb c) { return std::max({c.r, c.g, c.b}) - std::min({c.r, c.g, c.b}); }
Rgb setSat(Rgb c, float s) {
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
    float sa = source.a, da = destination.a;
    float s[3] = {source.r * sa, source.g * sa, source.b * sa}, d[3] = {destination.r * da, destination.g * da, destination.b * da};
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
        Rgb cs{source.r, source.g, source.b}, cb{destination.r, destination.g, destination.b}, b{};
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
        const float cs[3] = {source.r, source.g, source.b}, cb[3] = {destination.r, destination.g, destination.b};
        for (int i = 0; i < 3; ++i) out[i] = s[i] * (1 - da) + d[i] * (1 - sa) + sa * da * separable(mode, cs[i], cb[i]);
        break;
    }
    }
    if (a <= 0) return {0, 0, 0, 0};
    return {std::clamp(out[0] / a, 0.f, 1.f), std::clamp(out[1] / a, 0.f, 1.f), std::clamp(out[2] / a, 0.f, 1.f), std::min(a, 1.f)};
}

Color ColorFilter::apply(Color d) const {
    if (kind_ == Kind::Matrix) return matrix_.apply(d);
    if (kind_ == Kind::Lighting)
        return {std::clamp(d.r * color_.r + add_.r, 0.f, 1.f), std::clamp(d.g * color_.g + add_.g, 0.f, 1.f),
                std::clamp(d.b * color_.b + add_.b, 0.f, 1.f), d.a};
    return blend(color_, d, mode_);  // the tint (the source) onto the pixel
}

ImageBitmap ColorFilter::apply(const ImageBitmap& image) const {
    if (image.empty()) return image;
    struct Entry {
        std::weak_ptr<const void> image;
        uint64_t id;
        ColorFilter filter;
        ImageBitmap filtered;
    };
    static std::mutex mutex;
    static std::vector<Entry> cache;  // the latest last
    std::lock_guard lock(mutex);
    std::erase_if(cache, [](const Entry& e) { return e.image.expired(); });
    for (size_t i = 0; i < cache.size(); ++i)
        if (cache[i].id == image.id() && cache[i].filter == *this) {
            std::rotate(cache.begin() + std::ptrdiff_t(i), cache.begin() + std::ptrdiff_t(i) + 1, cache.end());
            return cache.back().filtered;
        }
    const uint8_t* in = image.pixels();
    std::vector<uint8_t> out(size_t(image.width()) * size_t(image.height()) * 4);
    for (size_t i = 0; i < out.size(); i += 4) {
        Color c = apply(Color{in[i] / 255.f, in[i + 1] / 255.f, in[i + 2] / 255.f, in[i + 3] / 255.f});
        out[i] = uint8_t(std::lround(c.r * 255)), out[i + 1] = uint8_t(std::lround(c.g * 255));
        out[i + 2] = uint8_t(std::lround(c.b * 255)), out[i + 3] = uint8_t(std::lround(c.a * 255));
    }
    ImageBitmap filtered = ImageBitmap::fromPixels(image.width(), image.height(), std::move(out));
    cache.push_back({image.token(), image.id(), *this, filtered});
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

RenderEffect BlurRadiusSpec::createRenderEffect(Size size, TileMode edgeTreatment) const {
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
    commands_.push_back({PathCommand::Verb::Move, 0, 0, 0, 0, x, y});
    current_ = start_ = {x, y};
}

void Path::lineTo(float x, float y) {
    if (commands_.empty()) moveTo(0, 0);
    commands_.push_back({PathCommand::Verb::Line, 0, 0, 0, 0, x, y});
    current_ = {x, y};
}

void Path::quadraticTo(float x1, float y1, float x2, float y2) {
    if (commands_.empty()) moveTo(0, 0);
    commands_.push_back({PathCommand::Verb::Quad, x1, y1, 0, 0, x2, y2});
    current_ = {x2, y2};
}

void Path::cubicTo(float x1, float y1, float x2, float y2, float x3, float y3) {
    if (commands_.empty()) moveTo(0, 0);
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

Path PathEffect::apply(const Path& path) const {
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

// Closed polygons of a path: its contours flattened (open ones closed).
std::vector<std::vector<Offset>> polygonsOf(const Path& path) {
    std::vector<std::vector<Offset>> out;
    for (const Polyline& l : flatten(path)) {
        std::vector<Offset> p = l.points;
        while (p.size() > 1 && distance(p.front(), p.back()) < 1e-6f) p.pop_back();  // closed: its start again
        if (p.size() >= 3) out.push_back(std::move(p));
    }
    return out;
}

// How many times the polygons wind round a point (crossing a ray to the right).
int windingOf(const std::vector<std::vector<Offset>>& polygons, Offset q) {
    int winding = 0;
    for (const auto& poly : polygons)
        for (size_t i = 0; i < poly.size(); ++i) {
            Offset a = poly[i], b = poly[(i + 1) % poly.size()];
            if ((a.y <= q.y) != (b.y <= q.y)) {
                float x = a.x + (q.y - a.y) / (b.y - a.y) * (b.x - a.x);
                if (x > q.x) winding += b.y > a.y ? 1 : -1;
            }
        }
    return winding;
}

bool insideOf(const std::vector<std::vector<Offset>>& polygons, Path::FillType fill, Offset q) {
    int w = windingOf(polygons, q);
    return fill == Path::FillType::EvenOdd ? (w & 1) != 0 : w != 0;
}

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

bool Path::contains(Offset p) const { return insideOf(polygonsOf(*this), fillType, p); }

bool Path::op(const Path& a, const Path& b, PathOperation operation) {
    auto pa = polygonsOf(a), pb = polygonsOf(b);
    // Every side of both, split where it meets another.
    struct Side {
        Offset from, to;
    };
    std::vector<Side> sides;
    for (const auto* set : {&pa, &pb})
        for (const auto& poly : *set)
            for (size_t i = 0; i < poly.size(); ++i) sides.push_back({poly[i], poly[(i + 1) % poly.size()]});
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
            Offset mid = mix(p, q, 0.5f), normal{-(q.y - p.y) / len, (q.x - p.x) / len};  // to the left
            float e = std::min(1e-3f, len * 0.25f);
            Offset left{mid.x + normal.x * e, mid.y + normal.y * e}, right{mid.x - normal.x * e, mid.y - normal.y * e};
            bool inLeft = combined(operation, insideOf(pa, a.fillType, left), insideOf(pb, b.fillType, left));
            bool inRight = combined(operation, insideOf(pa, a.fillType, right), insideOf(pb, b.fillType, right));
            if (inLeft == inRight) continue;
            kept.push_back(inLeft ? Side{p, q} : Side{q, p});
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
    for (size_t start = 0; start < pieces.size(); ++start) {
        if (used[start]) continue;
        std::vector<Offset> loop{pieces[start].from};
        used[start] = true;
        Offset end = pieces[start].to;
        Offset previous = pieces[start].from;
        while (!same(end, loop.front())) {
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
            loop.push_back(pieces[next].from);
            used[next] = true;
            end = pieces[next].to;
        }
        if (loop.size() < 3) continue;
        moveTo(loop[0].x, loop[0].y);
        for (size_t k = 1; k < loop.size(); ++k) lineTo(loop[k].x, loop[k].y);
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

void Path::addRect(const Rect& r) {
    moveTo(r.x, r.y);
    lineTo(r.right(), r.y);
    lineTo(r.right(), r.bottom());
    lineTo(r.x, r.bottom());
    close();
}

void Path::arcTo(const Rect& oval, float startAngleDegrees, float sweepAngleDegrees, bool forceMoveTo) {
    constexpr float kRadians = 3.14159265f / 180;
    float cx = oval.x + oval.width / 2, cy = oval.y + oval.height / 2, rx = oval.width / 2, ry = oval.height / 2;
    float a = startAngleDegrees * kRadians, sweep = std::clamp(sweepAngleDegrees, -360.f, 360.f) * kRadians;
    Offset start{cx + rx * std::cos(a), cy + ry * std::sin(a)};
    if (forceMoveTo || commands_.empty()) moveTo(start.x, start.y);
    else if (start.x != current_.x || start.y != current_.y) lineTo(start.x, start.y);
    // Cubic pieces of at most a quarter turn each (control points 4/3 tan(θ/4) along the tangents).
    int pieces = std::max(1, int(std::ceil(std::abs(sweep) / (3.14159265f / 2) - 1e-4f)));
    float step = sweep / float(pieces), k = 4.f / 3 * std::tan(step / 4);
    for (int i = 0; i < pieces; ++i, a += step) {
        float b = a + step;
        float x0 = cx + rx * std::cos(a), y0 = cy + ry * std::sin(a), x3 = cx + rx * std::cos(b), y3 = cy + ry * std::sin(b);
        cubicTo(x0 - k * rx * std::sin(a), y0 + k * ry * std::cos(a), x3 + k * rx * std::sin(b), y3 - k * ry * std::cos(b), x3, y3);
    }
}

void Path::addArc(const Rect& oval, float startAngleDegrees, float sweepAngleDegrees) {
    if (std::abs(sweepAngleDegrees) >= 360) return addOval(oval);
    arcTo(oval, startAngleDegrees, sweepAngleDegrees, true);
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
        case PathCommand::Verb::Quad: c.x1 += o.x, c.y1 += o.y; [[fallthrough]];
        case PathCommand::Verb::Move:
        case PathCommand::Verb::Line: c.x += o.x, c.y += o.y; break;
        case PathCommand::Verb::Close: break;
        }
    }
    current_ = {current_.x + o.x, current_.y + o.y};
    start_ = {start_.x + o.x, start_.y + o.y};
}

void Path::addOval(const Rect& r) {
    // Four cubic quarter arcs (the usual 0.5523 control-point factor).
    constexpr float k = 0.5522848f;
    float cx = r.x + r.width / 2, cy = r.y + r.height / 2, rx = r.width / 2, ry = r.height / 2;
    moveTo(cx + rx, cy);
    cubicTo(cx + rx, cy + ry * k, cx + rx * k, cy + ry, cx, cy + ry);
    cubicTo(cx - rx * k, cy + ry, cx - rx, cy + ry * k, cx - rx, cy);
    cubicTo(cx - rx, cy - ry * k, cx - rx * k, cy - ry, cx, cy - ry);
    cubicTo(cx + rx * k, cy - ry, cx + rx, cy - ry * k, cx + rx, cy);
    close();
}

void Path::addRoundRect(const Rect& r, float radius) {
    float rr = std::clamp(radius, 0.f, std::min(r.width, r.height) / 2);
    if (rr <= 0) return addRect(r);
    constexpr float k = 0.5522848f;
    float x0 = r.x, y0 = r.y, x1 = r.right(), y1 = r.bottom(), c = rr * (1 - k);
    moveTo(x0 + rr, y0);
    lineTo(x1 - rr, y0);
    cubicTo(x1 - c, y0, x1, y0 + c, x1, y0 + rr);
    lineTo(x1, y1 - rr);
    cubicTo(x1, y1 - c, x1 - c, y1, x1 - rr, y1);
    lineTo(x0 + rr, y1);
    cubicTo(x0 + c, y1, x0, y1 - c, x0, y1 - rr);
    lineTo(x0, y0 + rr);
    cubicTo(x0, y0 + c, x0 + c, y0, x0 + rr, y0);
    close();
}

void Path::addRoundRect(const RoundRect& rr) {
    RoundRect f = rr.fitted();
    const Rect& r = f.rect;
    if (f.topLeft == CornerRadius() && f.topRight == CornerRadius() && f.bottomRight == CornerRadius() && f.bottomLeft == CornerRadius())
        return addRect(r);
    constexpr float k = 1 - 0.5522848f;  // quarter ellipses as cubics
    float x0 = r.x, y0 = r.y, x1 = r.right(), y1 = r.bottom();
    CornerRadius tl = f.topLeft, tr = f.topRight, br = f.bottomRight, bl = f.bottomLeft;
    moveTo(x0 + tl.x, y0);
    lineTo(x1 - tr.x, y0);
    cubicTo(x1 - tr.x * k, y0, x1, y0 + tr.y * k, x1, y0 + tr.y);
    lineTo(x1, y1 - br.y);
    cubicTo(x1, y1 - br.y * k, x1 - br.x * k, y1, x1 - br.x, y1);
    lineTo(x0 + bl.x, y1);
    cubicTo(x0 + bl.x * k, y1, x0, y1 - bl.y * k, x0, y1 - bl.y);
    lineTo(x0, y0 + tl.y);
    cubicTo(x0, y0 + tl.y * k, x0 + tl.x * k, y0, x0 + tl.x, y0);
    close();
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
        case PathCommand::Verb::Quad: add(c.x1, c.y1); [[fallthrough]];
        case PathCommand::Verb::Move:
        case PathCommand::Verb::Line: add(c.x, c.y); break;
        case PathCommand::Verb::Close: break;
        }
    }
    return {x0, y0, x1 - x0, y1 - y0};
}

bool operator==(const Path& a, const Path& b) {
    if (a.fillType != b.fillType || a.commands_.size() != b.commands_.size()) return false;
    for (size_t i = 0; i < a.commands_.size(); ++i) {
        const PathCommand &p = a.commands_[i], &q = b.commands_[i];
        if (p.verb != q.verb || p.x1 != q.x1 || p.y1 != q.y1 || p.x2 != q.x2 || p.y2 != q.y2 || p.x != q.x || p.y != q.y)
            return false;
    }
    return true;
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

void GraphicsContext::drawShapeShadow(const Rect& rect, const Shape& shape, bool rtl, const Shadow& shadow, bool inner) {
    Placed p = place(rect, shape, rtl);
    if (p.rounded) drawShadow(rect, p.radii, shadow, inner);
    else drawShadowPath(p.outline.asPath(), {rect.x, rect.y}, shadow, inner);
}

} // namespace kompoz
