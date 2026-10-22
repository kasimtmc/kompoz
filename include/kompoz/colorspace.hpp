#pragma once

// Color spaces (Compose's androidx.compose.ui.graphics.colorspace): RGB ones, CIE XYZ, CIE L*a*b* and
// Oklab, the connectors that turn a color of one into another, and the built-in ones whose ids a
// Color keeps. Matrices are 3x3, column-major, as Compose's.

#include <array>
#include <bit>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <string_view>

namespace kompoz {

class ColorSpace;
class Connector;

// How a color space's components describe a color (Compose's ColorModel).
enum class ColorModel { Rgb, Xyz, Lab, Cmyk };
constexpr int componentCount(ColorModel model) { return model == ColorModel::Cmyk ? 4 : 3; }
std::string_view toString(ColorModel model);  // "Rgb", "Xyz", "Lab", "Cmyk"

// How a connector treats colors the destination cannot show (Compose's RenderIntent).
enum class RenderIntent { Perceptual, Relative, Saturation, Absolute };
std::string_view toString(RenderIntent intent);  // "Perceptual", …

namespace detail {
// Equal as Kotlin compares a data class's floats (Float.compare: NaN equal to NaN, 0 not to -0).
constexpr bool sameFloat(float a, float b) { return std::bit_cast<uint32_t>(a) == std::bit_cast<uint32_t>(b) || (a != a && b != b); }
constexpr bool sameDouble(double a, double b) { return std::bit_cast<uint64_t>(a) == std::bit_cast<uint64_t>(b) || (a != a && b != b); }
inline constexpr double TypePQish = -2.0, TypeHLGish = -3.0;  // a TransferParameters' gamma for a PQ or HLG curve
class ConnectorImpl;
} // namespace detail

// A white point in CIE xyY, its Y 1 (Compose's WhitePoint).
struct WhitePoint {
    float x, y;
    constexpr WhitePoint(float x, float y) : x(x), y(y) {}
    constexpr WhitePoint(float x, float y, float z) : WhitePoint(x, y, z, x + y + z) {}  // from CIE XYZ
    constexpr std::array<float, 3> toXyz() const { return {x / y, 1.0f, (1.0f - x - y) / y}; }  // in CIE XYZ, Y 1
    friend constexpr bool operator==(const WhitePoint& a, const WhitePoint& b) {
        return detail::sameFloat(a.x, b.x) && detail::sameFloat(a.y, b.y);
    }

private:
    constexpr WhitePoint(float x, float y, float, float sum) : x(x / sum), y(y / sum) {}
};

// The CIE standard illuminants' white points (Compose's Illuminant).
namespace Illuminant {
inline constexpr WhitePoint A{0.44757f, 0.40745f};
inline constexpr WhitePoint B{0.34842f, 0.35161f};
inline constexpr WhitePoint C{0.31006f, 0.31616f};
inline constexpr WhitePoint D50{0.34567f, 0.35850f};
inline constexpr WhitePoint D55{0.33242f, 0.34743f};
inline constexpr WhitePoint D60{0.32168f, 0.33767f};
inline constexpr WhitePoint D65{0.31271f, 0.32902f};
inline constexpr WhitePoint D75{0.29902f, 0.31485f};
inline constexpr WhitePoint E{0.33333f, 0.33333f};
inline constexpr std::array<float, 3> D50Xyz{0.964212f, 1.0f, 0.825188f};  // D50 in CIE XYZ (internal in Compose)
} // namespace Illuminant

// An ICC parametric curve, type 4 (Compose's TransferParameters): the EOTF is (a·x + b)^gamma + e
// from d up, c·x + f below it. A gamma of detail::TypePQish or TypeHLGish makes it a PQ or HLG
// curve, its parameters packed as Skia packs them. Throws std::invalid_argument where Compose
// throws: a NaN, or (not PQ or HLG) d outside 0..1, a constant or decreasing curve.
struct TransferParameters {
    double gamma, a, b, c, d, e, f;

    constexpr TransferParameters(double gamma, double a, double b, double c, double d, double e = 0.0, double f = 0.0)
        : gamma(gamma), a(a), b(b), c(c), d(d), e(e), f(f) {
        if (a != a || b != b || c != c || d != d || e != e || f != f || gamma != gamma)
            throw std::invalid_argument("Parameters cannot be NaN");
        if (gamma == detail::TypePQish || gamma == detail::TypeHLGish) return;
        if (!(d >= 0.0 && d <= 1.0)) throw std::invalid_argument("Parameter d must be in the range [0..1], was " + std::to_string(d));
        if (d == 0.0 && (a == 0.0 || gamma == 0.0))
            throw std::invalid_argument("Parameter a or g is zero, the transfer function is constant");
        if (d >= 1.0 && c == 0.0) throw std::invalid_argument("Parameter c is zero, the transfer function is constant");
        if ((a == 0.0 || gamma == 0.0) && c == 0.0)
            throw std::invalid_argument("Parameter a or g is zero, and c is zero, the transfer function is constant");
        if (c < 0.0) throw std::invalid_argument("The transfer function must be increasing");
        if (a < 0.0 || gamma < 0.0) throw std::invalid_argument("The transfer function must be positive or increasing");
    }
    friend constexpr bool operator==(const TransferParameters& l, const TransferParameters& r) {
        using detail::sameDouble;
        return sameDouble(l.gamma, r.gamma) && sameDouble(l.a, r.a) && sameDouble(l.b, r.b) && sameDouble(l.c, r.c) &&
               sameDouble(l.d, r.d) && sameDouble(l.e, r.e) && sameDouble(l.f, r.f);
    }
};

// A chromatic adaptation transform, CIE XYZ to LMS, for the von Kries method (Compose's Adaptation).
class Adaptation {
public:
    static const Adaptation Bradford, VonKries, Ciecat02;
    constexpr const std::array<float, 9>& transform() const { return transform_; }  // (internal in Compose)
    constexpr std::string_view toString() const { return name_; }

private:
    constexpr Adaptation(std::array<float, 9> transform, std::string_view name) : transform_(transform), name_(name) {}
    std::array<float, 9> transform_;
    std::string_view name_;
};
inline constexpr Adaptation Adaptation::Bradford{
    {0.8951f, -0.7502f, 0.0389f, 0.2664f, 1.7135f, -0.0685f, -0.1614f, 0.0367f, 1.0296f}, "Bradford"};
inline constexpr Adaptation Adaptation::VonKries{
    {0.40024f, -0.22630f, 0.00000f, 0.70760f, 1.16532f, 0.00000f, -0.08081f, 0.04570f, 0.91822f}, "VonKries"};
inline constexpr Adaptation Adaptation::Ciecat02{
    {0.7328f, -0.7036f, 0.0030f, 0.4296f, 1.6975f, 0.0136f, -0.1624f, 0.0061f, 0.9834f}, "Ciecat02"};

namespace ColorSpaces {
// The built-in color space of an id, 0..19 (Compose's getColorSpace); std::out_of_range for another.
// It works at any time, static initialization included.
const ColorSpace& getColorSpace(int id);
} // namespace ColorSpaces

// A color space (Compose's ColorSpace): its components' model and range, and how they turn into
// CIE XYZ (D50) and back. Its id is the built-in space's (ColorSpaces), MinId for any other. Two are
// equal when they are of the same class with the same id, name and model (an Rgb's: also its
// white point, primaries, range and transfer parameters; with none on either side, whatever its
// functions, as Compose's; with them on one side only, the same functions).
class ColorSpace {
public:
    static constexpr int MinId = -1, MaxId = 63;  // (ids are kept in 6 bits)

    virtual ~ColorSpace() = default;

    const std::string& name() const { return name_; }
    ColorModel model() const { return model_; }
    int id() const { return id_; }  // (internal in Compose)
    int componentCount() const { return kompoz::componentCount(model_); }
    virtual bool isWideGamut() const = 0;
    virtual bool isSrgb() const { return false; }
    virtual float getMinValue(int component) const = 0;
    virtual float getMaxValue(int component) const = 0;

    // In CIE XYZ, and from it: a new array, or `v` turned in place and returned.
    std::array<float, 3> toXyz(float r, float g, float b) const;
    virtual std::array<float, 3>& toXyz(std::array<float, 3>& v) const = 0;
    std::array<float, 3> fromXyz(float x, float y, float z) const;
    virtual std::array<float, 3>& fromXyz(std::array<float, 3>& v) const = 0;

    // The connector from this space to `destination` (Compose's ColorSpace.connect): from sRGB to
    // sRGB, or a space to itself, the identity (its intent Relative). Two built-in spaces' is
    // made once and kept for good (Compose's cache, by ids and intent); others' are made anew.
    Connector connect(const ColorSpace& destination = ColorSpaces::getColorSpace(0), RenderIntent intent = RenderIntent::Perceptual) const;
    // This space with another white point (Compose's ColorSpace.adapt): an RGB one's colors adapted
    // to it, a new Rgb (id MinId). One of another model, or already of that white point, is this
    // space itself (not owned, as Compose returns the receiver: it must outlive the pointer).
    std::shared_ptr<const ColorSpace> adapt(const WhitePoint& whitePoint, const Adaptation& adaptation = Adaptation::Bradford) const;

    std::string toString() const;  // "name (id=…, model=…)"
    friend bool operator==(const ColorSpace& a, const ColorSpace& b) { return a.equals(b); }

protected:
    // Throws std::invalid_argument for an empty name or an id outside MinId..MaxId.
    ColorSpace(std::string name, ColorModel model, int id = MinId);
    ColorSpace(const ColorSpace&) = default;
    virtual bool equals(const ColorSpace& other) const;

private:
    std::string name_;
    ColorModel model_;
    int id_;
};

// Turns colors from one color space into another (Compose's Connector), from ColorSpace::connect.
// A handle: copies are the same connector, which refers to its two spaces (the built-in ones live
// for good; another must outlive it). A cached one lives for good.
class Connector {
public:
    const ColorSpace& source() const;
    const ColorSpace& destination() const;
    RenderIntent renderIntent() const;
    // A color of the source in the destination: a new array, or `v` turned in place and returned.
    std::array<float, 3> transform(float r, float g, float b) const;
    std::array<float, 3>& transform(std::array<float, 3>& v) const;
    friend bool operator==(const Connector& a, const Connector& b) { return a.impl_ == b.impl_; }  // the same connector

private:
    friend class ColorSpace;
    explicit Connector(std::shared_ptr<const detail::ConnectorImpl> impl) : impl_(std::move(impl)) {}
    std::shared_ptr<const detail::ConnectorImpl> impl_;
};

// An RGB color space (Compose's Rgb): three primaries and a white point (in CIE xyY, or the
// primaries in CIE XYZ: 6 or 9 floats), or the RGB to CIE XYZ matrix they make, and its transfer
// functions: the OETF (linear to encoded) and the EOTF (encoded to linear), given, from
// TransferParameters or a gamma. Its components are within min..max (0..1 unless given). Throws
// std::invalid_argument where Compose throws: an empty name, primaries of another size, min not
// under max, bad transfer parameters.
class Rgb final : public ColorSpace {
public:
    using DoubleFunction = std::function<double(double)>;

    Rgb(std::string name, const std::array<float, 9>& toXYZ, DoubleFunction oetf, DoubleFunction eotf);
    Rgb(std::string name, std::span<const float> primaries, WhitePoint whitePoint, DoubleFunction oetf, DoubleFunction eotf, float min,
        float max);
    Rgb(std::string name, const std::array<float, 9>& toXYZ, const TransferParameters& function);
    Rgb(std::string name, std::span<const float> primaries, WhitePoint whitePoint, const TransferParameters& function);
    Rgb(std::string name, const std::array<float, 9>& toXYZ, double gamma);
    Rgb(std::string name, std::span<const float> primaries, WhitePoint whitePoint, double gamma);
    // With an id (internal in Compose: the built-in spaces', 0..19).
    Rgb(std::string name, std::span<const float> primaries, WhitePoint whitePoint, const TransferParameters& function, int id);
    Rgb(std::string name, std::span<const float> primaries, WhitePoint whitePoint, double gamma, float min, float max, int id);
    Rgb(std::string name, std::span<const float> primaries, WhitePoint whitePoint, std::optional<std::array<float, 9>> transform,
        DoubleFunction oetf, DoubleFunction eotf, float min, float max, std::optional<TransferParameters> transferParameters, int id);
    // `colorSpace` with another white point and the matrix adapted to it (internal in Compose: adapt's).
    Rgb(const Rgb& colorSpace, const std::array<float, 9>& transform, WhitePoint whitePoint);

    const WhitePoint& whitePoint() const { return whitePoint_; }
    const std::array<float, 6>& primaries() const { return primaries_; }  // in CIE xyY
    const std::array<float, 9>& transform() const { return transform_; }  // RGB (linear) to CIE XYZ
    const std::array<float, 9>& inverseTransform() const { return inverseTransform_; }  // CIE XYZ to RGB (linear)
    std::array<float, 6> getPrimaries() const { return primaries_; }
    std::array<float, 9> getTransform() const { return transform_; }
    std::array<float, 9> getInverseTransform() const { return inverseTransform_; }
    // The transfer functions, the result (OETF) or the argument (EOTF) kept within min..max.
    const DoubleFunction& oetf() const { return oetf_; }
    const DoubleFunction& eotf() const { return eotf_; }
    const std::optional<TransferParameters>& transferParameters() const { return transferParameters_; }

    // A wide gamut: an area over 90% of NTSC 1953's holding all of sRGB's, or a range past 0..1 both ways.
    bool isWideGamut() const override { return isWideGamut_; }
    // sRGB: its id, or sRGB's primaries, white point and range, its transfer functions sRGB's within 1e-3.
    bool isSrgb() const override { return isSrgb_; }
    float getMinValue(int) const override { return min_; }
    float getMaxValue(int) const override { return max_; }

    // Through the EOTF (toLinear) or the OETF (fromLinear): a new array, or `v` in place.
    std::array<float, 3> toLinear(float r, float g, float b) const;
    std::array<float, 3>& toLinear(std::array<float, 3>& v) const;
    std::array<float, 3> fromLinear(float r, float g, float b) const;
    std::array<float, 3>& fromLinear(std::array<float, 3>& v) const;
    using ColorSpace::fromXyz;
    using ColorSpace::toXyz;
    std::array<float, 3>& toXyz(std::array<float, 3>& v) const override;
    std::array<float, 3>& fromXyz(std::array<float, 3>& v) const override;

protected:
    bool equals(const ColorSpace& other) const override;

private:
    void init(std::span<const float> primaries, const std::optional<std::array<float, 9>>& transform);
    double oetfFunc(double x) const;
    double eotfFunc(double x) const;

    WhitePoint whitePoint_;
    float min_, max_;
    std::optional<TransferParameters> transferParameters_;
    std::array<float, 6> primaries_;
    std::array<float, 9> transform_, inverseTransform_;
    std::shared_ptr<const DoubleFunction> oetfOrig_, eotfOrig_;  // shared by adapted copies (Compose's: the same objects)
    DoubleFunction oetf_, eotf_;
    bool isWideGamut_, isSrgb_;
};

// The built-in color spaces, by their ids (Compose's ColorSpaces: the ids are kept in colors).
namespace ColorSpaces {
inline const Rgb& Srgb = static_cast<const Rgb&>(getColorSpace(0));                // sRGB IEC61966-2.1
inline const Rgb& LinearSrgb = static_cast<const Rgb&>(getColorSpace(1));          // sRGB, linear
inline const Rgb& ExtendedSrgb = static_cast<const Rgb&>(getColorSpace(2));        // scRGB-nl IEC 61966-2-2:2003
inline const Rgb& LinearExtendedSrgb = static_cast<const Rgb&>(getColorSpace(3));  // scRGB IEC 61966-2-2:2003
inline const Rgb& Bt709 = static_cast<const Rgb&>(getColorSpace(4));
inline const Rgb& Bt2020 = static_cast<const Rgb&>(getColorSpace(5));
inline const Rgb& DciP3 = static_cast<const Rgb&>(getColorSpace(6));
inline const Rgb& DisplayP3 = static_cast<const Rgb&>(getColorSpace(7));
inline const Rgb& Ntsc1953 = static_cast<const Rgb&>(getColorSpace(8));
inline const Rgb& SmpteC = static_cast<const Rgb&>(getColorSpace(9));
inline const Rgb& AdobeRgb = static_cast<const Rgb&>(getColorSpace(10));
inline const Rgb& ProPhotoRgb = static_cast<const Rgb&>(getColorSpace(11));
inline const Rgb& Aces = static_cast<const Rgb&>(getColorSpace(12));
inline const Rgb& Acescg = static_cast<const Rgb&>(getColorSpace(13));
inline const ColorSpace& CieXyz = getColorSpace(14);                           // D50, -2..2
inline const ColorSpace& CieLab = getColorSpace(15);                           // D50, L 0..100, a and b -128..128
inline const Rgb& Unspecified = static_cast<const Rgb&>(getColorSpace(16));     // no color's (internal in Compose)
inline const Rgb& Bt2020Hlg = static_cast<const Rgb&>(getColorSpace(17));       // BT.2100, Hybrid Log Gamma
inline const Rgb& Bt2020Pq = static_cast<const Rgb&>(getColorSpace(18));        // BT.2100, Perceptual Quantizer
inline const ColorSpace& Oklab = getColorSpace(19);                            // D65, L 0..1, a and b -0.5..0.5

// The built-in RGB space whose matrix, adapted to D50, and transfer parameters are these within
// 1e-3 (Compose's match), or none.
const ColorSpace* match(const std::array<float, 9>& toXYZD50, const TransferParameters& function);
} // namespace ColorSpaces

} // namespace kompoz
