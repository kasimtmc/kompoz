#include <kompoz/colorspace.hpp>

#include <algorithm>
#include <cmath>
#include <mutex>
#include <typeinfo>
#include <unordered_map>

// Compose's float and double arithmetic as the JVM does it: no fused multiply-adds.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

namespace kompoz {

namespace {

using Matrix = std::array<float, 9>;
using Float3 = std::array<float, 3>;
using DoubleFunction = Rgb::DoubleFunction;

constexpr std::array<float, 6> SrgbPrimaries{0.640f, 0.330f, 0.300f, 0.600f, 0.150f, 0.060f};
constexpr std::array<float, 6> Ntsc1953Primaries{0.67f, 0.33f, 0.21f, 0.71f, 0.14f, 0.08f};
constexpr std::array<float, 6> Bt2020Primaries{0.708f, 0.292f, 0.170f, 0.797f, 0.131f, 0.046f};
constexpr TransferParameters SrgbTransferParameters(2.4, 1 / 1.055, 0.055 / 1.055, 1 / 12.92, 0.04045);
constexpr TransferParameters NoneTransferParameters(2.2, 1 / 1.055, 0.055 / 1.055, 1 / 12.92, 0.04045);
// HLG and PQ, with an SDR white point of 203 nits.
constexpr TransferParameters Bt2020HlgTransferParameters(detail::TypeHLGish, 2.0, 2.0, 1 / 0.17883277, 0.28466892, 0.55991073, -0.685490157);
constexpr TransferParameters Bt2020PqTransferParameters(detail::TypePQish, -1.555223, 1.860454, 32 / 2523.0, 2413 / 128.0, -2392 / 128.0,
                                                        8192 / 1305.0);

// Kotlin's Float.fastCoerceIn and Double.coerceIn (a NaN as it is).
constexpr float fastCoerceIn(float x, float min, float max) {
    x = x < min ? min : x;
    return x > max ? max : x;
}
constexpr double coerceIn(double x, double min, double max) { return x < min ? min : x > max ? max : x; }

// Compose's fastCbrt: a first guess from the float's bits, then two Newton-Raphson steps.
float fastCbrt(float x) {
    int64_t v = int64_t(std::bit_cast<int32_t>(x)) & 0x1ffffffffLL;
    float estimate = std::bit_cast<float>(uint32_t(0x2a510554) + uint32_t(v / 3));
    estimate -= (estimate - x / (estimate * estimate)) * (1.0f / 3.0f);
    estimate -= (estimate - x / (estimate * estimate)) * (1.0f / 3.0f);
    return estimate;
}

constexpr Matrix inverse3x3(const Matrix& m) {
    float a = m[0], b = m[3], c = m[6], d = m[1], e = m[4], f = m[7], g = m[2], h = m[5], i = m[8];
    float xA = e * i - f * h, xB = f * g - d * i, xC = d * h - e * g;
    float det = a * xA + b * xB + c * xC;
    return {xA / det,
            xB / det,
            xC / det,
            (c * h - b * i) / det,
            (a * i - c * g) / det,
            (b * g - a * h) / det,
            (b * f - c * e) / det,
            (c * d - a * f) / det,
            (a * e - b * d) / det};
}

constexpr Matrix mul3x3(const Matrix& lhs, const Matrix& rhs) {
    return {lhs[0] * rhs[0] + lhs[3] * rhs[1] + lhs[6] * rhs[2], lhs[1] * rhs[0] + lhs[4] * rhs[1] + lhs[7] * rhs[2],
            lhs[2] * rhs[0] + lhs[5] * rhs[1] + lhs[8] * rhs[2], lhs[0] * rhs[3] + lhs[3] * rhs[4] + lhs[6] * rhs[5],
            lhs[1] * rhs[3] + lhs[4] * rhs[4] + lhs[7] * rhs[5], lhs[2] * rhs[3] + lhs[5] * rhs[4] + lhs[8] * rhs[5],
            lhs[0] * rhs[6] + lhs[3] * rhs[7] + lhs[6] * rhs[8], lhs[1] * rhs[6] + lhs[4] * rhs[7] + lhs[7] * rhs[8],
            lhs[2] * rhs[6] + lhs[5] * rhs[7] + lhs[8] * rhs[8]};
}

// `rhs` times the matrix, in place.
constexpr Float3& mul3x3Float3(const Matrix& lhs, Float3& rhs) {
    float r0 = rhs[0], r1 = rhs[1], r2 = rhs[2];
    rhs[0] = lhs[0] * r0 + lhs[3] * r1 + lhs[6] * r2;
    rhs[1] = lhs[1] * r0 + lhs[4] * r1 + lhs[7] * r2;
    rhs[2] = lhs[2] * r0 + lhs[5] * r1 + lhs[8] * r2;
    return rhs;
}

// A diagonal matrix (its diagonal) times a matrix.
constexpr Matrix mul3x3Diag(const Float3& lhs, const Matrix& rhs) {
    return {lhs[0] * rhs[0], lhs[1] * rhs[1], lhs[2] * rhs[2], lhs[0] * rhs[3], lhs[1] * rhs[4],
            lhs[2] * rhs[5], lhs[0] * rhs[6], lhs[1] * rhs[7], lhs[2] * rhs[8]};
}

// The von Kries transform from one white point (CIE XYZ) to another. As Compose's, it leaves the two
// white points in LMS (Compose's RgbConnector goes on with them).
constexpr Matrix chromaticAdaptation(const Matrix& matrix, Float3& srcWhitePoint, Float3& dstWhitePoint) {
    const Float3& srcLMS = mul3x3Float3(matrix, srcWhitePoint);
    const Float3& dstLMS = mul3x3Float3(matrix, dstWhitePoint);
    Float3 lms{dstLMS[0] / srcLMS[0], dstLMS[1] / srcLMS[1], dstLMS[2] / srcLMS[2]};
    return mul3x3(inverse3x3(matrix), mul3x3Diag(lms, matrix));
}

bool compare(const TransferParameters& a, const std::optional<TransferParameters>& b) {
    return b && std::abs(a.a - b->a) < 1e-3 && std::abs(a.b - b->b) < 1e-3 && std::abs(a.c - b->c) < 1e-3 &&
           std::abs(a.d - b->d) < 2e-3 &&  // (variations in sRGB's OETF and EOTF)
           std::abs(a.e - b->e) < 1e-3 && std::abs(a.f - b->f) < 1e-3 && std::abs(a.gamma - b->gamma) < 1e-3;
}

bool compare(const WhitePoint& a, const WhitePoint& b) {
    if (&a == &b) return true;
    return std::abs(a.x - b.x) < 1e-3f && std::abs(a.y - b.y) < 1e-3f;
}

bool compare(std::span<const float> a, std::span<const float> b) {
    if (a.data() == b.data()) return true;
    for (size_t i = 0; i < a.size(); ++i)
        if (!detail::sameFloat(a[i], b[i]) && std::abs(a[i] - b[i]) > 1e-3f) return false;
    return true;
}

// Piecewise gamma responses and their reciprocals; the abs ones sign(x)·f(|x|), for spaces that
// allow negative values.
double rcpResponse(double x, double a, double b, double c, double d, double g) {
    return x >= d * c ? (std::pow(x, 1.0 / g) - b) / a : x / c;
}
double response(double x, double a, double b, double c, double d, double g) { return x >= d ? std::pow(a * x + b, g) : c * x; }
double rcpResponse(double x, double a, double b, double c, double d, double e, double f, double g) {
    return x >= d * c ? (std::pow(x - e, 1.0 / g) - b) / a : (x - f) / c;
}
double response(double x, double a, double b, double c, double d, double e, double f, double g) {
    return x >= d ? std::pow(a * x + b, g) + e : c * x + f;
}
double absRcpResponse(double x, double a, double b, double c, double d, double g) {
    return std::copysign(rcpResponse(x < 0.0 ? -x : x, a, b, c, d, g), x);
}
double absResponse(double x, double a, double b, double c, double d, double g) {
    return std::copysign(response(x < 0.0 ? -x : x, a, b, c, d, g), x);
}

// HLG and PQ (SMPTE ST 2084), their parameters packed as Skia packs them.
double transferHlgOetf(const TransferParameters& params, double x) {
    double sign = x < 0 ? -1.0 : 1.0, absX = x * sign;
    double R = 1.0 / params.a, G = 1.0 / params.b, a = 1.0 / params.c, b = params.d, c = params.e, K = params.f + 1.0;
    absX /= K;
    return sign * (absX <= 1 ? R * std::pow(absX, G) : a * std::log(absX - b) + c);
}
double transferHlgEotf(const TransferParameters& params, double x) {
    double sign = x < 0 ? -1.0 : 1.0, absX = x * sign;
    double R = params.a, G = params.b, a = params.c, b = params.d, c = params.e, K = params.f + 1.0;
    return K * sign * (absX * R <= 1 ? std::pow(absX * R, G) : std::exp((absX - c) * a) + b);
}
double transferSt2048Oetf(const TransferParameters& params, double x) {
    double sign = x < 0 ? -1.0 : 1.0, absX = x * sign;
    double a = -params.a, b = params.d, c = 1.0 / params.f, d = params.b, e = -params.e, f = 1.0 / params.c;
    double tmp = std::max(a + b * std::pow(absX, c), 0.0);
    return sign * std::pow(tmp / (d + e * std::pow(absX, c)), f);
}
double transferSt2048Eotf(const TransferParameters& pq, double x) {
    double sign = x < 0 ? -1.0 : 1.0, absX = x * sign;
    double tmp = pq.a + pq.b * std::pow(absX, pq.c);
    tmp = tmp < 0.0 ? 0.0 : tmp;
    return sign * std::pow(tmp / (pq.d + pq.e * std::pow(absX, pq.c)), pq.f);
}

DoubleFunction generateOetf(const TransferParameters& function) {
    if (function.gamma == detail::TypeHLGish) return [function](double x) { return transferHlgOetf(function, x); };
    if (function.gamma == detail::TypePQish) return [function](double x) { return transferSt2048Oetf(function, x); };
    if (function.e == 0.0 && function.f == 0.0)
        return [function](double x) { return rcpResponse(x, function.a, function.b, function.c, function.d, function.gamma); };
    return [function](double x) {
        return rcpResponse(x, function.a, function.b, function.c, function.d, function.e, function.f, function.gamma);
    };
}

DoubleFunction generateEotf(const TransferParameters& function) {
    if (function.gamma == detail::TypeHLGish) return [function](double x) { return transferHlgEotf(function, x); };
    if (function.gamma == detail::TypePQish) return [function](double x) { return transferSt2048Eotf(function, x); };
    if (function.e == 0.0 && function.f == 0.0)
        return [function](double x) { return response(x, function.a, function.b, function.c, function.d, function.gamma); };
    return [function](double x) { return response(x, function.a, function.b, function.c, function.d, function.e, function.f, function.gamma); };
}

std::array<float, 6> computePrimaries(const Matrix& toXYZ) {
    Float3 r{1.0f, 0.0f, 0.0f}, g{0.0f, 1.0f, 0.0f}, b{0.0f, 0.0f, 1.0f};
    mul3x3Float3(toXYZ, r);
    mul3x3Float3(toXYZ, g);
    mul3x3Float3(toXYZ, b);
    float rSum = r[0] + r[1] + r[2], gSum = g[0] + g[1] + g[2], bSum = b[0] + b[1] + b[2];
    return {r[0] / rSum, r[1] / rSum, g[0] / gSum, g[1] / gSum, b[0] / bSum, b[1] / bSum};
}

WhitePoint computeWhitePoint(const Matrix& toXYZ) {
    Float3 w{1.0f, 1.0f, 1.0f};
    mul3x3Float3(toXYZ, w);
    float sum = w[0] + w[1] + w[2];
    return WhitePoint(w[0] / sum, w[1] / sum);
}

std::array<float, 6> xyPrimaries(std::span<const float> primaries) {
    std::array<float, 6> xy{};
    if (primaries.size() == 9) {  // CIE XYZ to xyY
        for (size_t i = 0; i < 3; ++i) {
            float sum = primaries[i * 3] + primaries[i * 3 + 1] + primaries[i * 3 + 2];
            xy[i * 2] = primaries[i * 3] / sum;
            xy[i * 2 + 1] = primaries[i * 3 + 1] / sum;
        }
    } else {
        std::copy_n(primaries.begin(), 6, xy.begin());
    }
    return xy;
}

Matrix computeXYZMatrix(const std::array<float, 6>& primaries, const WhitePoint& whitePoint) {
    float rx = primaries[0], ry = primaries[1], gx = primaries[2], gy = primaries[3], bx = primaries[4], by = primaries[5];
    float wx = whitePoint.x, wy = whitePoint.y;
    float oneRxRy = (1 - rx) / ry, oneGxGy = (1 - gx) / gy, oneBxBy = (1 - bx) / by, oneWxWy = (1 - wx) / wy;
    float rxRy = rx / ry, gxGy = gx / gy, bxBy = bx / by, wxWy = wx / wy;
    float byNumerator = (oneWxWy - oneRxRy) * (gxGy - rxRy) - (wxWy - rxRy) * (oneGxGy - oneRxRy);
    float byDenominator = (oneBxBy - oneRxRy) * (gxGy - rxRy) - (bxBy - rxRy) * (oneGxGy - oneRxRy);
    float bY = byNumerator / byDenominator;
    float gY = (wxWy - rxRy - bY * (bxBy - rxRy)) / (gxGy - rxRy);
    float rY = 1.0f - gY - bY;
    float rYRy = rY / ry, gYGy = gY / gy, bYBy = bY / by;
    return {rYRy * rx, rY, rYRy * (1.0f - rx - ry), gYGy * gx, gY, gYGy * (1.0f - gx - gy), bYBy * bx, bY, bYBy * (1.0f - bx - by)};
}

float area(const std::array<float, 6>& primaries) {
    float rx = primaries[0], ry = primaries[1], gx = primaries[2], gy = primaries[3], bx = primaries[4], by = primaries[5];
    float det = rx * gy + ry * bx + gx * by - gy * bx - ry * gx - rx * by;
    float r = 0.5f * det;
    return r < 0.0f ? -r : r;
}

float cross(float ax, float ay, float bx, float by) { return ax * by - ay * bx; }

// Whether the triangle p1 holds the triangle p2.
bool contains(const std::array<float, 6>& p1, const std::array<float, 6>& p2) {
    // p1's vertices with p2's as the origin
    std::array<float, 6> p0{p1[0] - p2[0], p1[1] - p2[1], p1[2] - p2[2], p1[3] - p2[3], p1[4] - p2[4], p1[5] - p2[5]};
    if (cross(p0[0], p0[1], p2[0] - p2[4], p2[1] - p2[5]) < 0 || cross(p2[0] - p2[2], p2[1] - p2[3], p0[0], p0[1]) < 0) return false;
    if (cross(p0[2], p0[3], p2[2] - p2[0], p2[3] - p2[1]) < 0 || cross(p2[2] - p2[4], p2[3] - p2[5], p0[2], p0[3]) < 0) return false;
    return !(cross(p0[4], p0[5], p2[4] - p2[2], p2[5] - p2[3]) < 0 || cross(p2[4] - p2[0], p2[5] - p2[1], p0[4], p0[5]) < 0);
}

bool isWideGamut(const std::array<float, 6>& primaries, float min, float max) {
    return (area(primaries) / area(Ntsc1953Primaries) > 0.9f && contains(primaries, SrgbPrimaries)) || (min < 0.0f && max > 1.0f);
}

// sRGB, made apart from the others: whether another RGB space is sRGB is measured against it.
const Rgb& srgb() {
    static const Rgb space("sRGB IEC61966-2.1", SrgbPrimaries, Illuminant::D65, SrgbTransferParameters, 0);
    return space;
}

// CIE XYZ, D50 (Compose's Xyz).
class Xyz final : public ColorSpace {
public:
    Xyz(std::string name, int id) : ColorSpace(std::move(name), ColorModel::Xyz, id) {}
    bool isWideGamut() const override { return true; }
    float getMinValue(int) const override { return -2.0f; }
    float getMaxValue(int) const override { return 2.0f; }
    using ColorSpace::fromXyz;
    using ColorSpace::toXyz;
    Float3& toXyz(Float3& v) const override { return clamp(v); }
    Float3& fromXyz(Float3& v) const override { return clamp(v); }

private:
    static Float3& clamp(Float3& v) {
        for (float& c : v) c = fastCoerceIn(c, -2.0f, 2.0f);
        return v;
    }
};

// CIE L*a*b*, through CIE XYZ D50 (Compose's Lab).
class Lab final : public ColorSpace {
public:
    Lab(std::string name, int id) : ColorSpace(std::move(name), ColorModel::Lab, id) {}
    bool isWideGamut() const override { return true; }
    float getMinValue(int component) const override { return component == 0 ? 0.0f : -128.0f; }
    float getMaxValue(int component) const override { return component == 0 ? 100.0f : 128.0f; }
    using ColorSpace::fromXyz;
    using ColorSpace::toXyz;

    Float3& toXyz(Float3& v) const override {
        v[0] = fastCoerceIn(v[0], 0.0f, 100.0f);
        v[1] = fastCoerceIn(v[1], -128.0f, 128.0f);
        v[2] = fastCoerceIn(v[2], -128.0f, 128.0f);
        float fy = (v[0] + 16.0f) / 116.0f, fx = fy + (v[1] * 0.002f), fz = fy - (v[2] * 0.005f);
        float x = fx > D ? fx * fx * fx : (1.0f / B) * (fx - C);
        float y = fy > D ? fy * fy * fy : (1.0f / B) * (fy - C);
        float z = fz > D ? fz * fz * fz : (1.0f / B) * (fz - C);
        v[0] = x * Illuminant::D50Xyz[0];
        v[1] = y * Illuminant::D50Xyz[1];
        v[2] = z * Illuminant::D50Xyz[2];
        return v;
    }

    Float3& fromXyz(Float3& v) const override {
        float x = v[0] / Illuminant::D50Xyz[0], y = v[1] / Illuminant::D50Xyz[1], z = v[2] / Illuminant::D50Xyz[2];
        float fx = x > A ? cbrt(x) : B * x + C, fy = y > A ? cbrt(y) : B * y + C, fz = z > A ? cbrt(z) : B * z + C;
        float l = 116.0f * fy - 16.0f, a = 500.0f * (fx - fy), b = 200.0f * (fy - fz);
        v[0] = fastCoerceIn(l, 0.0f, 100.0f);
        v[1] = fastCoerceIn(a, -128.0f, 128.0f);
        v[2] = fastCoerceIn(b, -128.0f, 128.0f);
        return v;
    }

private:
    static constexpr float A = 216.0f / 24389.0f, B = 841.0f / 108.0f, C = 4.0f / 29.0f, D = 6.0f / 29.0f;
    static float cbrt(float x) { return float(std::cbrt(double(x))); }  // (Kotlin's cbrt of a Float)
};

// Oklab, D65, through CIE XYZ D50 (Compose's Oklab).
class Oklab final : public ColorSpace {
public:
    Oklab(std::string name, int id) : ColorSpace(std::move(name), ColorModel::Lab, id) {}
    bool isWideGamut() const override { return true; }
    float getMinValue(int component) const override { return component == 0 ? 0.0f : -0.5f; }
    float getMaxValue(int component) const override { return component == 0 ? 1.0f : 0.5f; }
    using ColorSpace::fromXyz;
    using ColorSpace::toXyz;

    Float3& toXyz(Float3& v) const override {
        v[0] = fastCoerceIn(v[0], 0.0f, 1.0f);
        v[1] = fastCoerceIn(v[1], -0.5f, 0.5f);
        v[2] = fastCoerceIn(v[2], -0.5f, 0.5f);
        mul3x3Float3(InverseM2, v);
        for (float& c : v) c = c * c * c;
        return mul3x3Float3(InverseM1, v);
    }

    Float3& fromXyz(Float3& v) const override {
        mul3x3Float3(M1, v);
        for (float& c : v) c = fastCbrt(c);
        return mul3x3Float3(M2, v);
    }

private:
    // Before the nonlinear step: D50 to D65 (Bradford), then XYZ to LMS.
    static constexpr Matrix M1 = [] {
        Float3 d50 = Illuminant::D50.toXyz(), d65 = Illuminant::D65.toXyz();
        return mul3x3(Matrix{0.8189330101f, 0.0329845436f, 0.0482003018f, 0.3618667424f, 0.9293118715f, 0.2643662691f, -0.1288597137f,
                             0.0361456387f, 0.6338517070f},
                      chromaticAdaptation(Adaptation::Bradford.transform(), d50, d65));
    }();
    // After it.
    static constexpr Matrix M2{0.2104542553f,  1.9779984951f, 0.0259040371f, 0.7936177850f, -2.4285922050f,
                               0.7827717662f, -0.0040720468f, 0.4505937099f, -0.8086757660f};
    static constexpr Matrix InverseM1 = inverse3x3(M1);
    static constexpr Matrix InverseM2 = inverse3x3(M2);
};

} // namespace

std::string_view toString(ColorModel model) {
    switch (model) {
    case ColorModel::Rgb: return "Rgb";
    case ColorModel::Xyz: return "Xyz";
    case ColorModel::Lab: return "Lab";
    case ColorModel::Cmyk: return "Cmyk";
    }
    return "Unknown";
}

std::string_view toString(RenderIntent intent) {
    switch (intent) {
    case RenderIntent::Perceptual: return "Perceptual";
    case RenderIntent::Relative: return "Relative";
    case RenderIntent::Saturation: return "Saturation";
    case RenderIntent::Absolute: return "Absolute";
    }
    return "Unknown";
}

// ColorSpace

ColorSpace::ColorSpace(std::string name, ColorModel model, int id) : name_(std::move(name)), model_(model), id_(id) {
    if (name_.empty()) throw std::invalid_argument("The name of a color space cannot be null and must contain at least 1 character");
    if (id < MinId || id > MaxId)
        throw std::invalid_argument("The id must be between " + std::to_string(MinId) + " and " + std::to_string(MaxId));
}

std::array<float, 3> ColorSpace::toXyz(float r, float g, float b) const {
    Float3 v{r, g, b};
    return toXyz(v);
}

std::array<float, 3> ColorSpace::fromXyz(float x, float y, float z) const {
    Float3 v{x, y, z};
    return fromXyz(v);
}

std::string ColorSpace::toString() const {
    return name_ + " (id=" + std::to_string(id_) + ", model=" + std::string(kompoz::toString(model_)) + ")";
}

bool ColorSpace::equals(const ColorSpace& other) const {
    if (this == &other) return true;
    if (typeid(*this) != typeid(other)) return false;
    return id_ == other.id_ && name_ == other.name_ && model_ == other.model_;
}

namespace {
// Not owned: what Compose hands back as it is.
std::shared_ptr<const ColorSpace> unowned(const ColorSpace* space) {
    return std::shared_ptr<const ColorSpace>(std::shared_ptr<const ColorSpace>(), space);
}
} // namespace

std::shared_ptr<const ColorSpace> ColorSpace::adapt(const WhitePoint& whitePoint, const Adaptation& adaptation) const {
    if (model_ == ColorModel::Rgb) {
        const auto& rgb = dynamic_cast<const Rgb&>(*this);
        if (compare(rgb.whitePoint(), whitePoint)) return unowned(this);
        Float3 xyz = whitePoint.toXyz(), source = rgb.whitePoint().toXyz();
        Matrix adaptationTransform = chromaticAdaptation(adaptation.transform(), source, xyz);
        return std::make_shared<const Rgb>(rgb, mul3x3(adaptationTransform, rgb.transform()), whitePoint);
    }
    return unowned(this);
}

// Connectors

namespace detail {
class ConnectorImpl {
public:
    ConnectorImpl(const ColorSpace& source, const ColorSpace& destination, RenderIntent intent)
        : source(source), destination(destination), renderIntent(intent) {}
    virtual ~ConnectorImpl() = default;
    virtual Float3& transform(Float3& v) const = 0;

    const ColorSpace& source;
    const ColorSpace& destination;
    RenderIntent renderIntent;
};
} // namespace detail

namespace {

// Any two spaces, through CIE XYZ D50 (Compose's Connector): an RGB one adapted to D50 first.
class XyzConnector final : public detail::ConnectorImpl {
public:
    XyzConnector(const ColorSpace& source, const ColorSpace& destination, RenderIntent intent)
        : ConnectorImpl(source, destination, intent),
          transformSource_(source.model() == ColorModel::Rgb ? source.adapt(Illuminant::D50) : unowned(&source)),
          transformDestination_(destination.model() == ColorModel::Rgb ? destination.adapt(Illuminant::D50) : unowned(&destination)),
          transform_(computeTransform(source, destination, intent)) {}

    Float3& transform(Float3& v) const override {
        Float3& xyz = transformSource_->toXyz(v);
        if (transform_) {
            xyz[0] *= (*transform_)[0];
            xyz[1] *= (*transform_)[1];
            xyz[2] *= (*transform_)[2];
        }
        return transformDestination_->fromXyz(xyz);
    }

private:
    // Absolute, between an RGB space and another: from one white point to the other.
    static std::optional<Float3> computeTransform(const ColorSpace& source, const ColorSpace& destination, RenderIntent intent) {
        if (intent != RenderIntent::Absolute) return std::nullopt;
        bool srcRGB = source.model() == ColorModel::Rgb, dstRGB = destination.model() == ColorModel::Rgb;
        if (srcRGB && dstRGB) return std::nullopt;
        if (srcRGB || dstRGB) {
            const auto& rgb = dynamic_cast<const Rgb&>(srcRGB ? source : destination);
            Float3 srcXYZ = srcRGB ? rgb.whitePoint().toXyz() : Illuminant::D50Xyz;
            Float3 dstXYZ = dstRGB ? rgb.whitePoint().toXyz() : Illuminant::D50Xyz;
            return Float3{srcXYZ[0] / dstXYZ[0], srcXYZ[1] / dstXYZ[1], srcXYZ[2] / dstXYZ[2]};
        }
        return std::nullopt;
    }

    std::shared_ptr<const ColorSpace> transformSource_, transformDestination_;
    std::optional<Float3> transform_;
};

// Two RGB spaces: linear, one matrix, encoded (Compose's Connector.RgbConnector).
class RgbConnector final : public detail::ConnectorImpl {
public:
    RgbConnector(const Rgb& source, const Rgb& destination, RenderIntent intent)
        : ConnectorImpl(source, destination, intent), source_(source), destination_(destination),
          transform_(computeTransform(source, destination, intent)) {}

    Float3& transform(Float3& v) const override {
        source_.toLinear(v);
        mul3x3Float3(transform_, v);
        return destination_.fromLinear(v);
    }

private:
    static Matrix computeTransform(const Rgb& source, const Rgb& destination, RenderIntent intent) {
        if (compare(source.whitePoint(), destination.whitePoint())) return mul3x3(destination.inverseTransform(), source.transform());
        // Through CIE XYZ D50.
        Matrix transform = source.transform(), inverseTransform = destination.inverseTransform();
        Float3 srcXYZ = source.whitePoint().toXyz(), dstXYZ = destination.whitePoint().toXyz();
        if (!compare(source.whitePoint(), Illuminant::D50)) {
            Float3 d50 = Illuminant::D50Xyz;
            transform = mul3x3(chromaticAdaptation(Adaptation::Bradford.transform(), srcXYZ, d50), source.transform());
        }
        if (!compare(destination.whitePoint(), Illuminant::D50)) {
            Float3 d50 = Illuminant::D50Xyz;
            inverseTransform = inverse3x3(mul3x3(chromaticAdaptation(Adaptation::Bradford.transform(), dstXYZ, d50), destination.transform()));
        }
        // (srcXYZ and dstXYZ in LMS when adapted above, as Compose's.)
        if (intent == RenderIntent::Absolute)
            transform = mul3x3Diag({srcXYZ[0] / dstXYZ[0], srcXYZ[1] / dstXYZ[1], srcXYZ[2] / dstXYZ[2]}, transform);
        return mul3x3(inverseTransform, transform);
    }

    const Rgb& source_;
    const Rgb& destination_;
    Matrix transform_;
};

// A space to itself (Compose's Connector.identity): colors as they are.
class IdentityConnector final : public detail::ConnectorImpl {
public:
    explicit IdentityConnector(const ColorSpace& space) : ConnectorImpl(space, space, RenderIntent::Relative) {}
    Float3& transform(Float3& v) const override { return v; }
};

std::shared_ptr<const detail::ConnectorImpl> createConnector(const ColorSpace& source, const ColorSpace& destination, RenderIntent intent) {
    if (&source == &destination) return std::make_shared<const IdentityConnector>(source);
    if (source.model() == ColorModel::Rgb && destination.model() == ColorModel::Rgb)
        return std::make_shared<const RgbConnector>(dynamic_cast<const Rgb&>(source), dynamic_cast<const Rgb&>(destination), intent);
    return std::make_shared<const XyzConnector>(source, destination, intent);
}

// The built-in spaces' connectors, by connectorKey, kept for good (Compose's Connectors).
struct ConnectorCache {
    std::mutex mutex;
    std::unordered_map<int, std::shared_ptr<const detail::ConnectorImpl>> connectors;
};

int connectorKey(int src, int dst, RenderIntent intent) { return src | dst << 6 | int(intent) << 12; }

ConnectorCache& connectorCache() {
    static ConnectorCache& cache = *[] {
        auto* c = new ConnectorCache;
        const ColorSpace &s = ColorSpaces::getColorSpace(0), &o = ColorSpaces::getColorSpace(19);
        c->connectors[connectorKey(0, 0, RenderIntent::Perceptual)] = std::make_shared<const IdentityConnector>(s);
        c->connectors[connectorKey(0, 19, RenderIntent::Perceptual)] = std::make_shared<const XyzConnector>(s, o, RenderIntent::Perceptual);
        c->connectors[connectorKey(19, 0, RenderIntent::Perceptual)] = std::make_shared<const XyzConnector>(o, s, RenderIntent::Perceptual);
        return c;
    }();
    return cache;
}

} // namespace

Connector ColorSpace::connect(const ColorSpace& destination, RenderIntent intent) const {
    if ((id_ | destination.id_) < 0) return Connector(createConnector(*this, destination, intent));  // a user's space: not cached
    ConnectorCache& cache = connectorCache();
    std::lock_guard lock(cache.mutex);
    auto& connector = cache.connectors[connectorKey(id_, destination.id_, intent)];
    if (!connector) connector = createConnector(*this, destination, intent);
    // Not owned: the cache keeps it for good.
    return Connector(std::shared_ptr<const detail::ConnectorImpl>(std::shared_ptr<const detail::ConnectorImpl>(), connector.get()));
}

const ColorSpace& Connector::source() const { return impl_->source; }
const ColorSpace& Connector::destination() const { return impl_->destination; }
RenderIntent Connector::renderIntent() const { return impl_->renderIntent; }

std::array<float, 3> Connector::transform(float r, float g, float b) const {
    Float3 v{r, g, b};
    return impl_->transform(v);
}

std::array<float, 3>& Connector::transform(std::array<float, 3>& v) const { return impl_->transform(v); }

// Rgb

Rgb::Rgb(std::string name, const std::array<float, 9>& toXYZ, DoubleFunction oetf, DoubleFunction eotf)
    : Rgb(std::move(name), computePrimaries(toXYZ), computeWhitePoint(toXYZ), std::nullopt, std::move(oetf), std::move(eotf), 0.0f, 1.0f,
          std::nullopt, MinId) {}

Rgb::Rgb(std::string name, std::span<const float> primaries, WhitePoint whitePoint, DoubleFunction oetf, DoubleFunction eotf, float min,
         float max)
    : Rgb(std::move(name), primaries, whitePoint, std::nullopt, std::move(oetf), std::move(eotf), min, max, std::nullopt, MinId) {}

Rgb::Rgb(std::string name, const std::array<float, 9>& toXYZ, const TransferParameters& function)
    : Rgb(std::move(name), computePrimaries(toXYZ), computeWhitePoint(toXYZ), function, MinId) {}

Rgb::Rgb(std::string name, std::span<const float> primaries, WhitePoint whitePoint, const TransferParameters& function)
    : Rgb(std::move(name), primaries, whitePoint, function, MinId) {}

Rgb::Rgb(std::string name, const std::array<float, 9>& toXYZ, double gamma)
    : Rgb(std::move(name), computePrimaries(toXYZ), computeWhitePoint(toXYZ), gamma, 0.0f, 1.0f, MinId) {}

Rgb::Rgb(std::string name, std::span<const float> primaries, WhitePoint whitePoint, double gamma)
    : Rgb(std::move(name), primaries, whitePoint, gamma, 0.0f, 1.0f, MinId) {}

Rgb::Rgb(std::string name, std::span<const float> primaries, WhitePoint whitePoint, const TransferParameters& function, int id)
    : Rgb(std::move(name), primaries, whitePoint, std::nullopt, generateOetf(function), generateEotf(function), 0.0f, 1.0f, function, id) {}

Rgb::Rgb(std::string name, std::span<const float> primaries, WhitePoint whitePoint, double gamma, float min, float max, int id)
    : Rgb(std::move(name), primaries, whitePoint, std::nullopt,
          gamma == 1.0 ? DoubleFunction([](double x) { return x; })
                       : DoubleFunction([gamma](double x) { return std::pow(x < 0.0 ? 0.0 : x, 1.0 / gamma); }),
          gamma == 1.0 ? DoubleFunction([](double x) { return x; })
                       : DoubleFunction([gamma](double x) { return std::pow(x < 0.0 ? 0.0 : x, gamma); }),
          min, max, TransferParameters(gamma, 1.0, 0.0, 0.0, 0.0), id) {}

Rgb::Rgb(std::string name, std::span<const float> primaries, WhitePoint whitePoint, std::optional<std::array<float, 9>> transform,
         DoubleFunction oetf, DoubleFunction eotf, float min, float max, std::optional<TransferParameters> transferParameters, int id)
    : ColorSpace(std::move(name), ColorModel::Rgb, id), whitePoint_(whitePoint), min_(min), max_(max), transferParameters_(transferParameters),
      oetfOrig_(std::make_shared<const DoubleFunction>(std::move(oetf))), eotfOrig_(std::make_shared<const DoubleFunction>(std::move(eotf))) {
    init(primaries, transform);
}

Rgb::Rgb(const Rgb& colorSpace, const std::array<float, 9>& transform, WhitePoint whitePoint)
    : ColorSpace(colorSpace.name(), ColorModel::Rgb, MinId), whitePoint_(whitePoint), min_(colorSpace.min_), max_(colorSpace.max_),
      transferParameters_(colorSpace.transferParameters_), oetfOrig_(colorSpace.oetfOrig_), eotfOrig_(colorSpace.eotfOrig_) {
    init(colorSpace.primaries_, transform);
}

void Rgb::init(std::span<const float> primaries, const std::optional<std::array<float, 9>>& transform) {
    if (primaries.size() != 6 && primaries.size() != 9)
        throw std::invalid_argument("The color space's primaries must be defined as an array of 6 floats in xyY or 9 floats in XYZ");
    if (min_ >= max_)
        throw std::invalid_argument("Invalid range: min=" + std::to_string(min_) + ", max=" + std::to_string(max_) +
                                    "; min must be strictly < max");
    primaries_ = xyPrimaries(primaries);
    transform_ = transform ? *transform : computeXYZMatrix(primaries_, whitePoint_);
    inverseTransform_ = inverse3x3(transform_);
    isWideGamut_ = kompoz::isWideGamut(primaries_, min_, max_);
    isSrgb_ = [&] {
        if (id() == 0) return true;
        if (!compare(primaries_, SrgbPrimaries) || !compare(whitePoint_, Illuminant::D65)) return false;
        if (min_ != 0.0f || max_ != 1.0f) return false;
        const Rgb& s = srgb();  // (made already: this is not sRGB itself)
        for (double x = 0.0; x <= 1.0; x += 1 / 255.0) {
            if (!(std::abs((*oetfOrig_)(x) - (*s.oetfOrig_)(x)) <= 1e-3)) return false;
            if (!(std::abs((*eotfOrig_)(x) - (*s.eotfOrig_)(x)) <= 1e-3)) return false;
        }
        return true;
    }();
    oetf_ = [f = oetfOrig_, min = double(min_), max = double(max_)](double x) { return coerceIn((*f)(x), min, max); };
    eotf_ = [f = eotfOrig_, min = double(min_), max = double(max_)](double x) { return (*f)(coerceIn(x, min, max)); };
}

double Rgb::oetfFunc(double x) const { return coerceIn((*oetfOrig_)(x), double(min_), double(max_)); }
double Rgb::eotfFunc(double x) const { return (*eotfOrig_)(coerceIn(x, double(min_), double(max_))); }

std::array<float, 3> Rgb::toLinear(float r, float g, float b) const {
    Float3 v{r, g, b};
    return toLinear(v);
}

std::array<float, 3>& Rgb::toLinear(std::array<float, 3>& v) const {
    for (float& c : v) c = float(eotfFunc(double(c)));
    return v;
}

std::array<float, 3> Rgb::fromLinear(float r, float g, float b) const {
    Float3 v{r, g, b};
    return fromLinear(v);
}

std::array<float, 3>& Rgb::fromLinear(std::array<float, 3>& v) const {
    for (float& c : v) c = float(oetfFunc(double(c)));
    return v;
}

std::array<float, 3>& Rgb::toXyz(std::array<float, 3>& v) const { return mul3x3Float3(transform_, toLinear(v)); }

std::array<float, 3>& Rgb::fromXyz(std::array<float, 3>& v) const { return fromLinear(mul3x3Float3(inverseTransform_, v)); }

bool Rgb::equals(const ColorSpace& other) const {
    if (this == &other) return true;
    if (!ColorSpace::equals(other)) return false;
    const auto& rgb = static_cast<const Rgb&>(other);
    if (!detail::sameFloat(rgb.min_, min_) || !detail::sameFloat(rgb.max_, max_)) return false;
    if (whitePoint_ != rgb.whitePoint_) return false;
    for (size_t i = 0; i < 6; ++i)
        if (!detail::sameFloat(primaries_[i], rgb.primaries_[i])) return false;
    if (transferParameters_) return transferParameters_ == rgb.transferParameters_;
    if (!rgb.transferParameters_) return true;
    return oetfOrig_ == rgb.oetfOrig_ && eotfOrig_ == rgb.eotfOrig_;  // (the same functions, as Compose compares them)
}

// ColorSpaces

const ColorSpace& ColorSpaces::getColorSpace(int id) {
    // In the order of their ids; made once, kept for good.
    static const std::array<const ColorSpace*, 20> spaces{
        &srgb(),
        new Rgb("sRGB IEC61966-2.1 (Linear)", SrgbPrimaries, Illuminant::D65, 1.0, 0.0f, 1.0f, 1),
        new Rgb("scRGB-nl IEC 61966-2-2:2003", SrgbPrimaries, Illuminant::D65, std::nullopt,
                [](double x) { return absRcpResponse(x, 1 / 1.055, 0.055 / 1.055, 1 / 12.92, 0.04045, 2.4); },
                [](double x) { return absResponse(x, 1 / 1.055, 0.055 / 1.055, 1 / 12.92, 0.04045, 2.4); }, -0.799f, 2.399f,
                SrgbTransferParameters, 2),
        new Rgb("scRGB IEC 61966-2-2:2003", SrgbPrimaries, Illuminant::D65, 1.0, -0.5f, 7.499f, 3),
        new Rgb("Rec. ITU-R BT.709-5", std::array{0.640f, 0.330f, 0.300f, 0.600f, 0.150f, 0.060f}, Illuminant::D65,
                TransferParameters(1 / 0.45, 1 / 1.099, 0.099 / 1.099, 1 / 4.5, 0.081), 4),
        new Rgb("Rec. ITU-R BT.2020-1", std::array{0.708f, 0.292f, 0.170f, 0.797f, 0.131f, 0.046f}, Illuminant::D65,
                TransferParameters(1 / 0.45, 1 / 1.0993, 0.0993 / 1.0993, 1 / 4.5, 0.08145), 5),
        new Rgb("SMPTE RP 431-2-2007 DCI (P3)", std::array{0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f}, WhitePoint(0.314f, 0.351f), 2.6,
                0.0f, 1.0f, 6),
        new Rgb("Display P3", std::array{0.680f, 0.320f, 0.265f, 0.690f, 0.150f, 0.060f}, Illuminant::D65, SrgbTransferParameters, 7),
        new Rgb("NTSC (1953)", Ntsc1953Primaries, Illuminant::C, TransferParameters(1 / 0.45, 1 / 1.099, 0.099 / 1.099, 1 / 4.5, 0.081), 8),
        new Rgb("SMPTE-C RGB", std::array{0.630f, 0.340f, 0.310f, 0.595f, 0.155f, 0.070f}, Illuminant::D65,
                TransferParameters(1 / 0.45, 1 / 1.099, 0.099 / 1.099, 1 / 4.5, 0.081), 9),
        new Rgb("Adobe RGB (1998)", std::array{0.64f, 0.33f, 0.21f, 0.71f, 0.15f, 0.06f}, Illuminant::D65, 2.2, 0.0f, 1.0f, 10),
        new Rgb("ROMM RGB ISO 22028-2:2013", std::array{0.7347f, 0.2653f, 0.1596f, 0.8404f, 0.0366f, 0.0001f}, Illuminant::D50,
                TransferParameters(1.8, 1.0, 0.0, 1 / 16.0, 0.031248), 11),
        new Rgb("SMPTE ST 2065-1:2012 ACES", std::array{0.73470f, 0.26530f, 0.0f, 1.0f, 0.00010f, -0.0770f}, Illuminant::D60, 1.0, -65504.0f,
                65504.0f, 12),
        new Rgb("Academy S-2014-004 ACEScg", std::array{0.713f, 0.293f, 0.165f, 0.830f, 0.128f, 0.044f}, Illuminant::D60, 1.0, -65504.0f,
                65504.0f, 13),
        new Xyz("Generic XYZ", 14),
        new Lab("Generic L*a*b*", 15),
        new Rgb("None", SrgbPrimaries, Illuminant::D65, NoneTransferParameters, 16),
        new Rgb("Hybrid Log Gamma encoding", Bt2020Primaries, Illuminant::D65, std::nullopt,
                [](double x) { return transferHlgOetf(Bt2020HlgTransferParameters, x); },
                [](double x) { return transferHlgEotf(Bt2020HlgTransferParameters, x); }, 0.0f, 1.0f, Bt2020HlgTransferParameters, 17),
        new Rgb("Perceptual Quantizer encoding", Bt2020Primaries, Illuminant::D65, std::nullopt,
                [](double x) { return transferSt2048Oetf(Bt2020PqTransferParameters, x); },
                [](double x) { return transferSt2048Eotf(Bt2020PqTransferParameters, x); }, 0.0f, 1.0f, Bt2020PqTransferParameters, 18),
        new kompoz::Oklab("Oklab", 19),  // (the class, not ColorSpaces::Oklab)
    };
    if (id < 0 || size_t(id) >= spaces.size()) throw std::out_of_range("No color space of id " + std::to_string(id));
    return *spaces[size_t(id)];
}

const ColorSpace* ColorSpaces::match(const std::array<float, 9>& toXYZD50, const TransferParameters& function) {
    for (int id = 0; id < 20; ++id) {
        const ColorSpace& colorSpace = getColorSpace(id);
        if (colorSpace.model() != ColorModel::Rgb) continue;
        std::shared_ptr<const ColorSpace> adapted = colorSpace.adapt(Illuminant::D50);
        const auto& rgb = dynamic_cast<const Rgb&>(*adapted);
        if (compare(toXYZD50, rgb.transform()) && compare(function, rgb.transferParameters())) return &colorSpace;
    }
    return nullptr;
}

} // namespace kompoz
