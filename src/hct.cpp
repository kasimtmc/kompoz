// Material's HCT color space, the palettes Material You makes from one color, and the colors it
// picks from a picture, as Material Color Utilities (CAM16 under its default viewing conditions,
// tone as L*; QuantizerCelebi and Score).

#include <kompoz/layout.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <numbers>
#include <optional>
#include <unordered_map>
#include <vector>

namespace kompoz {

namespace {

constexpr double kPi = std::numbers::pi;

double sanitizeDegrees(double d) {
    d = std::fmod(d, 360.0);
    return d < 0 ? d + 360.0 : d;
}

// sRGB (0..1) <-> linear (0..100), XYZ (D65), L*.
double linearized(double c) { return 100.0 * (c <= 0.040449936 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4)); }
double delinearized(double c) {
    c /= 100.0;
    return c <= 0.0031308 ? c * 12.92 : 1.055 * std::pow(c, 1.0 / 2.4) - 0.055;
}
constexpr double kEpsilon = 216.0 / 24389.0, kKappa = 24389.0 / 27.0;
double labF(double t) { return t > kEpsilon ? std::cbrt(t) : (kKappa * t + 16.0) / 116.0; }
double labInvF(double ft) { return ft * ft * ft > kEpsilon ? ft * ft * ft : (116.0 * ft - 16.0) / kKappa; }
double lstarFromY(double y) { return 116.0 * labF(y / 100.0) - 16.0; }
double yFromLstar(double lstar) { return 100.0 * labInvF((lstar + 16.0) / 116.0); }

struct Xyz {
    double x, y, z;
};
Xyz xyzFromColor(Color c) {
    double r = linearized(c.r), g = linearized(c.g), b = linearized(c.b);
    return {0.41233895 * r + 0.35762064 * g + 0.18051042 * b, 0.2126 * r + 0.7152 * g + 0.0722 * b,
            0.01932141 * r + 0.11916382 * g + 0.95034478 * b};
}
// The color of those coordinates, each channel clipped to sRGB, and whether it had to be.
Color colorFromXyz(const Xyz& v, bool* clipped = nullptr) {
    double r = 3.2413774792388685 * v.x - 1.5376652402851851 * v.y - 0.49885366846268053 * v.z;
    double g = -0.9691452513005321 * v.x + 1.8758853451067872 * v.y + 0.04156585616912061 * v.z;
    double b = 0.05562093689691305 * v.x - 0.20395524564742123 * v.y + 1.0571799111220335 * v.z;
    auto channel = [&](double linear) {
        double c = delinearized(linear);
        if (clipped && (c < -1e-4 || c > 1 + 1e-4)) *clipped = true;
        return float(std::round(std::clamp(c, 0.0, 1.0) * 255.0) / 255.0);
    };
    return {channel(r), channel(g), channel(b), 1};
}

// CAM16's default viewing conditions (Material's): D65, a mid-grey (L* 50) background, average
// surround, adapting to 200/π × the luminance of L* 50.
struct ViewingConditions {
    double n, aw, nbb, ncb, c, nc, rgbD[3], fl, flRoot, z;
    ViewingConditions() {
        const double white[3] = {95.047, 100.0, 108.883};
        double adapting = 200.0 / kPi * yFromLstar(50.0) / 100.0, surround = 2.0;
        double rW = white[0] * 0.401288 + white[1] * 0.650173 + white[2] * -0.051461;
        double gW = white[0] * -0.250268 + white[1] * 1.204414 + white[2] * 0.045854;
        double bW = white[0] * -0.002079 + white[1] * 0.048952 + white[2] * 0.953127;
        double f = 0.8 + surround / 10.0;
        c = f >= 0.9 ? 0.59 + (0.69 - 0.59) * ((f - 0.9) * 10.0) : 0.525 + (0.59 - 0.525) * ((f - 0.8) * 10.0);
        double d = std::clamp(f * (1.0 - (1.0 / 3.6) * std::exp((-adapting - 42.0) / 92.0)), 0.0, 1.0);
        nc = f;
        rgbD[0] = d * (100.0 / rW) + 1.0 - d, rgbD[1] = d * (100.0 / gW) + 1.0 - d, rgbD[2] = d * (100.0 / bW) + 1.0 - d;
        double k = 1.0 / (5.0 * adapting + 1.0), k4 = k * k * k * k, k4F = 1.0 - k4;
        fl = k4 * adapting + 0.1 * k4F * k4F * std::cbrt(5.0 * adapting);
        flRoot = std::pow(fl, 0.25);
        n = yFromLstar(50.0) / white[1];
        z = 1.48 + std::sqrt(n);
        nbb = ncb = 0.725 / std::pow(n, 0.2);
        double w[3] = {rW, gW, bW}, a[3];
        for (int i = 0; i < 3; ++i) {
            double factor = std::pow(fl * rgbD[i] * w[i] / 100.0, 0.42);
            a[i] = 400.0 * factor / (factor + 27.13);
        }
        aw = (2.0 * a[0] + a[1] + 0.05 * a[2]) * nbb;
    }
};
const ViewingConditions& viewing() {
    static const ViewingConditions vc;
    return vc;
}

struct Cam16 {
    double hue, chroma, j, m;
    // CAM16-UCS coordinates, for distances.
    double jstar() const { return (1.0 + 100.0 * 0.007) * j / (1.0 + 0.007 * j); }
    double mstar() const { return 1.0 / 0.0228 * std::log1p(0.0228 * m); }
    double distance(const Cam16& o) const {
        double h1 = hue * kPi / 180.0, h2 = o.hue * kPi / 180.0;
        double dJ = jstar() - o.jstar(), dA = mstar() * std::cos(h1) - o.mstar() * std::cos(h2),
               dB = mstar() * std::sin(h1) - o.mstar() * std::sin(h2);
        return 1.41 * std::pow(std::sqrt(dJ * dJ + dA * dA + dB * dB), 0.63);
    }
};

Cam16 camFromXyz(const Xyz& v) {
    const ViewingConditions& vc = viewing();
    double rC = 0.401288 * v.x + 0.650173 * v.y - 0.051461 * v.z;
    double gC = -0.250268 * v.x + 1.204414 * v.y + 0.045854 * v.z;
    double bC = -0.002079 * v.x + 0.048952 * v.y + 0.953127 * v.z;
    double rgb[3] = {vc.rgbD[0] * rC, vc.rgbD[1] * gC, vc.rgbD[2] * bC}, adapted[3];
    for (int i = 0; i < 3; ++i) {
        double af = std::pow(vc.fl * std::abs(rgb[i]) / 100.0, 0.42);
        adapted[i] = std::copysign(1.0, rgb[i]) * 400.0 * af / (af + 27.13);
    }
    auto [rA, gA, bA] = adapted;
    double a = (11.0 * rA + -12.0 * gA + bA) / 11.0, b = (rA + gA - 2.0 * bA) / 9.0;
    double u = (20.0 * rA + 20.0 * gA + 21.0 * bA) / 20.0, p2 = (40.0 * rA + 20.0 * gA + bA) / 20.0;
    double hue = sanitizeDegrees(std::atan2(b, a) * 180.0 / kPi);
    double ac = p2 * vc.nbb;
    double j = 100.0 * std::pow(ac / vc.aw, vc.c * vc.z);
    double eHue = 0.25 * (std::cos(hue * kPi / 180.0 + 2.0) + 3.8);
    double p1 = 50000.0 / 13.0 * eHue * vc.nc * vc.ncb;
    double t = p1 * std::hypot(a, b) / (u + 0.305);
    double alpha = std::pow(t, 0.9) * std::pow(1.64 - std::pow(0.29, vc.n), 0.73);
    double chroma = alpha * std::sqrt(j / 100.0);
    return {hue, chroma, j, chroma * vc.flRoot};
}

// What CAM16 lightness `j`, `chroma` and `hue` are, before sRGB clips them.
Xyz xyzFromJch(double j, double chroma, double hue) {
    const ViewingConditions& vc = viewing();
    double alpha = chroma == 0.0 || j == 0.0 ? 0.0 : chroma / std::sqrt(j / 100.0);
    double t = std::pow(alpha / std::pow(1.64 - std::pow(0.29, vc.n), 0.73), 1.0 / 0.9);
    double hRad = hue * kPi / 180.0;
    double eHue = 0.25 * (std::cos(hRad + 2.0) + 3.8);
    double ac = vc.aw * std::pow(j / 100.0, 1.0 / vc.c / vc.z);
    double p1 = eHue * (50000.0 / 13.0) * vc.nc * vc.ncb, p2 = ac / vc.nbb;
    double hSin = std::sin(hRad), hCos = std::cos(hRad);
    double gamma = 23.0 * (p2 + 0.305) * t / (23.0 * p1 + 11.0 * t * hCos + 108.0 * t * hSin);
    double a = gamma * hCos, b = gamma * hSin;
    double adapted[3] = {(460.0 * p2 + 451.0 * a + 288.0 * b) / 1403.0, (460.0 * p2 - 891.0 * a - 261.0 * b) / 1403.0,
                         (460.0 * p2 - 220.0 * a - 6300.0 * b) / 1403.0};
    double f[3];
    for (int i = 0; i < 3; ++i) {
        double base = std::max(0.0, 27.13 * std::abs(adapted[i]) / (400.0 - std::abs(adapted[i])));
        f[i] = std::copysign(1.0, adapted[i]) * (100.0 / vc.fl) * std::pow(base, 1.0 / 0.42) / vc.rgbD[i];
    }
    return {1.86206786 * f[0] - 1.01125463 * f[1] + 0.14918677 * f[2], 0.38752654 * f[0] + 0.62144744 * f[1] - 0.00897398 * f[2],
            -0.01584150 * f[0] - 0.03412294 * f[1] + 1.04996444 * f[2]};
}

Color greyOfTone(double tone) {
    float v = float(std::round(std::clamp(delinearized(yFromLstar(tone)), 0.0, 1.0) * 255.0) / 255.0);
    return {v, v, v, 1};
}

// The color at `chroma` and `hue` whose tone is nearest `tone` (within 0.2), and as near that hue
// and chroma as sRGB lets it (within 1 of CAM16-UCS); by bisecting CAM16 lightness.
std::optional<Color> findByJ(double hue, double chroma, double tone) {
    double low = 0, high = 100, bestL = 1000, bestE = 1000;
    std::optional<Color> best;
    while (std::abs(low - high) > 0.01) {
        double mid = low + (high - low) / 2;
        Color clipped = colorFromXyz(xyzFromJch(mid, chroma, hue));
        double lstar = lstarFromY(xyzFromColor(clipped).y);
        double dL = std::abs(tone - lstar);
        if (dL < 0.2) {
            Cam16 cam = camFromXyz(xyzFromColor(clipped));
            double dE = cam.distance(camFromXyz(xyzFromJch(cam.j, cam.chroma, hue)));
            if (dE <= 1.0 && dE <= bestE) bestL = dL, bestE = dE, best = clipped;
        }
        if (bestL == 0 && bestE == 0) break;
        if (lstar < tone) low = mid;
        else high = mid;
    }
    return best;
}

} // namespace

Hct Hct::fromColor(Color c) {
    Xyz v = xyzFromColor(c);
    Cam16 cam = camFromXyz(v);
    return {float(cam.hue), float(cam.chroma), float(lstarFromY(v.y))};
}

Color Hct::toColor() const {
    double h = sanitizeDegrees(hue), lstar = tone;
    if (chroma < 1.0 || std::round(lstar) <= 0 || std::round(lstar) >= 100) return greyOfTone(lstar);
    // The most chroma up to the one asked for that this hue has at this tone.
    double high = chroma, mid = chroma, low = 0;
    bool first = true;
    std::optional<Color> answer;
    while (std::abs(low - high) >= 0.4) {
        std::optional<Color> possible = findByJ(h, mid, lstar);
        if (first) {
            if (possible) return *possible;
            first = false;
        } else if (possible) {
            answer = possible, low = mid;
        } else {
            high = mid;
        }
        mid = low + (high - low) / 2;
    }
    return answer ? *answer : greyOfTone(lstar);
}

TonalPalette TonalPalette::fromSeed(Color seed) {
    double hue = Hct::fromColor(seed).hue;
    auto palette = [](double h, double chroma) {
        std::array<Color, 13> tones;
        for (size_t i = 0; i < tones.size(); ++i) tones[i] = Hct{float(h), float(chroma), float(kTones[i])}.toColor();
        return tones;
    };
    TonalPalette p;
    p.accent1 = palette(hue, 36);
    p.accent2 = palette(hue, 16);
    p.accent3 = palette(sanitizeDegrees(hue + 60), 24);
    p.neutral1 = palette(hue, 6);
    p.neutral2 = palette(hue, 8);
    return p;
}

// ---- Colors from a picture (QuantizerCelebi, Score) -------------------------------------------

namespace {

using Rgb = uint32_t;  // 0xRRGGBB
int red(Rgb c) { return int(c >> 16) & 0xFF; }
int green(Rgb c) { return int(c >> 8) & 0xFF; }
int blue(Rgb c) { return int(c) & 0xFF; }
Color colorOf(Rgb c) { return {red(c) / 255.f, green(c) / 255.f, blue(c) / 255.f, 1}; }
Rgb rgbOf(Color c) {
    auto channel = [](float v) { return Rgb(std::lround(std::clamp(v, 0.f, 1.f) * 255.f)); };
    return channel(c.r) << 16 | channel(c.g) << 8 | channel(c.b);
}

// L*a*b* (D65), as the k-means measures colors.
struct Lab {
    double l = 0, a = 0, b = 0;
    double distance(const Lab& o) const { return (l - o.l) * (l - o.l) + (a - o.a) * (a - o.a) + (b - o.b) * (b - o.b); }  // squared
};
constexpr double kWhite[3] = {95.047, 100.0, 108.883};
Lab labOf(Rgb c) {
    Xyz v = xyzFromColor(colorOf(c));
    double fx = labF(v.x / kWhite[0]), fy = labF(v.y / kWhite[1]), fz = labF(v.z / kWhite[2]);
    return {116.0 * fy - 16.0, 500.0 * (fx - fy), 200.0 * (fy - fz)};
}
Rgb rgbOf(const Lab& lab) {
    double fy = (lab.l + 16.0) / 116.0, fx = lab.a / 500.0 + fy, fz = fy - lab.b / 200.0;
    return rgbOf(colorFromXyz({labInvF(fx) * kWhite[0], labInvF(fy) * kWhite[1], labInvF(fz) * kWhite[2]}));
}

// Wu's quantizer: boxes of a 32-level RGB cube cut where the variance falls most, up to maxColors.
class QuantizerWu {
public:
    std::vector<Rgb> quantize(const std::unordered_map<Rgb, uint32_t>& counts, int maxColors) {
        for (const auto& [c, n] : counts) {
            int i = index((red(c) >> 3) + 1, (green(c) >> 3) + 1, (blue(c) >> 3) + 1);
            weights_[i] += n;
            mr_[i] += int64_t(n) * red(c);
            mg_[i] += int64_t(n) * green(c);
            mb_[i] += int64_t(n) * blue(c);
            moments_[i] += double(n) * (red(c) * red(c) + green(c) * green(c) + blue(c) * blue(c));
        }
        computeMoments();
        std::vector<Box> boxes = createBoxes(maxColors);
        std::vector<Rgb> colors;
        for (const Box& box : boxes) {
            int64_t w = volume(box, weights_);
            if (w <= 0) continue;
            auto mean = [&](const std::vector<int64_t>& m) { return Rgb(std::clamp<int64_t>(volume(box, m) / w, 0, 255)); };
            colors.push_back(mean(mr_) << 16 | mean(mg_) << 8 | mean(mb_));
        }
        return colors;
    }

private:
    static constexpr int kSide = 33, kSize = kSide * kSide * kSide;
    enum class Axis { Red, Green, Blue };
    struct Box {
        int r0 = 0, r1 = 0, g0 = 0, g1 = 0, b0 = 0, b1 = 0, vol = 0;
    };
    static int index(int r, int g, int b) { return (r * kSide + g) * kSide + b; }

    std::vector<int64_t> weights_ = std::vector<int64_t>(kSize), mr_ = std::vector<int64_t>(kSize), mg_ = std::vector<int64_t>(kSize),
                         mb_ = std::vector<int64_t>(kSize);
    std::vector<double> moments_ = std::vector<double>(kSize);

    // Each cell becomes the sum of the cells at or below it on every axis.
    void computeMoments() {
        for (int r = 1; r < kSide; ++r) {
            std::vector<int64_t> area(kSide), areaR(kSide), areaG(kSide), areaB(kSide);
            std::vector<double> area2(kSide);
            for (int g = 1; g < kSide; ++g) {
                int64_t line = 0, lineR = 0, lineG = 0, lineB = 0;
                double line2 = 0;
                for (int b = 1; b < kSide; ++b) {
                    int i = index(r, g, b), below = index(r - 1, g, b);
                    line += weights_[i], lineR += mr_[i], lineG += mg_[i], lineB += mb_[i], line2 += moments_[i];
                    area[b] += line, areaR[b] += lineR, areaG[b] += lineG, areaB[b] += lineB, area2[b] += line2;
                    weights_[i] = weights_[below] + area[b];
                    mr_[i] = mr_[below] + areaR[b];
                    mg_[i] = mg_[below] + areaG[b];
                    mb_[i] = mb_[below] + areaB[b];
                    moments_[i] = moments_[below] + area2[b];
                }
            }
        }
    }

    template <class T>
    static T volume(const Box& c, const std::vector<T>& m) {
        return m[index(c.r1, c.g1, c.b1)] - m[index(c.r1, c.g1, c.b0)] - m[index(c.r1, c.g0, c.b1)] + m[index(c.r1, c.g0, c.b0)] -
               m[index(c.r0, c.g1, c.b1)] + m[index(c.r0, c.g1, c.b0)] + m[index(c.r0, c.g0, c.b1)] - m[index(c.r0, c.g0, c.b0)];
    }
    static int64_t bottom(const Box& c, Axis axis, const std::vector<int64_t>& m) {
        switch (axis) {
        case Axis::Red:
            return -m[index(c.r0, c.g1, c.b1)] + m[index(c.r0, c.g1, c.b0)] + m[index(c.r0, c.g0, c.b1)] - m[index(c.r0, c.g0, c.b0)];
        case Axis::Green:
            return -m[index(c.r1, c.g0, c.b1)] + m[index(c.r1, c.g0, c.b0)] + m[index(c.r0, c.g0, c.b1)] - m[index(c.r0, c.g0, c.b0)];
        default:
            return -m[index(c.r1, c.g1, c.b0)] + m[index(c.r1, c.g0, c.b0)] + m[index(c.r0, c.g1, c.b0)] - m[index(c.r0, c.g0, c.b0)];
        }
    }
    static int64_t top(const Box& c, Axis axis, int at, const std::vector<int64_t>& m) {
        switch (axis) {
        case Axis::Red:
            return m[index(at, c.g1, c.b1)] - m[index(at, c.g1, c.b0)] - m[index(at, c.g0, c.b1)] + m[index(at, c.g0, c.b0)];
        case Axis::Green:
            return m[index(c.r1, at, c.b1)] - m[index(c.r1, at, c.b0)] - m[index(c.r0, at, c.b1)] + m[index(c.r0, at, c.b0)];
        default:
            return m[index(c.r1, c.g1, at)] - m[index(c.r1, c.g0, at)] - m[index(c.r0, c.g1, at)] + m[index(c.r0, c.g0, at)];
        }
    }

    double variance(const Box& c) const {
        double r = double(volume(c, mr_)), g = double(volume(c, mg_)), b = double(volume(c, mb_));
        return volume(c, moments_) - (r * r + g * g + b * b) / double(volume(c, weights_));
    }

    // Where along `axis` cutting `c` leaves the two halves' means farthest apart; -1: nowhere.
    std::pair<int, double> maximize(const Box& c, Axis axis, int first, int last, int64_t wholeR, int64_t wholeG, int64_t wholeB,
                                    int64_t wholeW) const {
        int64_t bottomR = bottom(c, axis, mr_), bottomG = bottom(c, axis, mg_), bottomB = bottom(c, axis, mb_),
                bottomW = bottom(c, axis, weights_);
        double best = 0;
        int cut = -1;
        for (int i = first; i < last; ++i) {
            double halfR = double(bottomR + top(c, axis, i, mr_)), halfG = double(bottomG + top(c, axis, i, mg_)),
                   halfB = double(bottomB + top(c, axis, i, mb_)), halfW = double(bottomW + top(c, axis, i, weights_));
            if (halfW == 0) continue;
            double score = (halfR * halfR + halfG * halfG + halfB * halfB) / halfW;
            halfR = double(wholeR) - halfR, halfG = double(wholeG) - halfG, halfB = double(wholeB) - halfB, halfW = double(wholeW) - halfW;
            if (halfW == 0) continue;
            score += (halfR * halfR + halfG * halfG + halfB * halfB) / halfW;
            if (score > best) best = score, cut = i;
        }
        return {cut, best};
    }

    bool cut(Box& one, Box& two) const {
        int64_t wholeR = volume(one, mr_), wholeG = volume(one, mg_), wholeB = volume(one, mb_), wholeW = volume(one, weights_);
        auto [cutR, maxR] = maximize(one, Axis::Red, one.r0 + 1, one.r1, wholeR, wholeG, wholeB, wholeW);
        auto [cutG, maxG] = maximize(one, Axis::Green, one.g0 + 1, one.g1, wholeR, wholeG, wholeB, wholeW);
        auto [cutB, maxB] = maximize(one, Axis::Blue, one.b0 + 1, one.b1, wholeR, wholeG, wholeB, wholeW);
        Axis axis;
        if (maxR >= maxG && maxR >= maxB) {
            if (cutR < 0) return false;
            axis = Axis::Red;
        } else {
            axis = maxG >= maxR && maxG >= maxB ? Axis::Green : Axis::Blue;
        }
        two.r1 = one.r1, two.g1 = one.g1, two.b1 = one.b1;
        switch (axis) {
        case Axis::Red:
            one.r1 = cutR, two.r0 = one.r1, two.g0 = one.g0, two.b0 = one.b0;
            break;
        case Axis::Green:
            one.g1 = cutG, two.r0 = one.r0, two.g0 = one.g1, two.b0 = one.b0;
            break;
        case Axis::Blue:
            one.b1 = cutB, two.r0 = one.r0, two.g0 = one.g0, two.b0 = one.b1;
            break;
        }
        one.vol = (one.r1 - one.r0) * (one.g1 - one.g0) * (one.b1 - one.b0);
        two.vol = (two.r1 - two.r0) * (two.g1 - two.g0) * (two.b1 - two.b0);
        return true;
    }

    std::vector<Box> createBoxes(int maxColors) {
        std::vector<Box> boxes(static_cast<size_t>(maxColors));
        std::vector<double> variances(static_cast<size_t>(maxColors));
        boxes[0] = {0, kSide - 1, 0, kSide - 1, 0, kSide - 1, 0};
        int count = maxColors, next = 0;
        for (int i = 1; i < maxColors; ++i) {
            if (cut(boxes[size_t(next)], boxes[size_t(i)])) {
                variances[size_t(next)] = boxes[size_t(next)].vol > 1 ? variance(boxes[size_t(next)]) : 0;
                variances[size_t(i)] = boxes[size_t(i)].vol > 1 ? variance(boxes[size_t(i)]) : 0;
            } else {
                variances[size_t(next)] = 0;
                --i;
            }
            next = 0;
            double most = variances[0];
            for (int k = 1; k <= i; ++k)
                if (variances[size_t(k)] > most) most = variances[size_t(k)], next = k;
            if (most <= 0) {
                count = i + 1;
                break;
            }
        }
        boxes.resize(size_t(count));
        return boxes;
    }
};

// Weighted k-means in L*a*b*, starting from `start` (Wu's colors): the clusters and their pixels.
std::map<Rgb, uint32_t> quantizeWsmeans(const std::unordered_map<Rgb, uint32_t>& counts, const std::vector<Rgb>& start, int maxColors) {
    std::vector<Rgb> pixels;
    std::vector<Lab> points;
    std::vector<uint32_t> weights;
    for (const auto& [c, n] : counts) pixels.push_back(c);
    std::ranges::sort(pixels);  // a fixed order, whatever the hash map's
    for (Rgb c : pixels) points.push_back(labOf(c)), weights.push_back(counts.at(c));
    int clusterCount = std::min({maxColors, int(points.size()), int(start.size())});
    if (clusterCount == 0) return {};
    std::vector<Lab> clusters;
    for (int i = 0; i < clusterCount; ++i) clusters.push_back(labOf(start[size_t(i)]));
    // Every point starts in a pseudo-random cluster (Material Color Utilities' seeded random).
    uint32_t seed = 42688;
    std::vector<int> clusterOf(points.size());
    for (int& c : clusterOf) {
        seed = seed * 1103515245u + 12345u;
        c = int((seed >> 16) % uint32_t(clusterCount));
    }
    struct Neighbor {
        double distance;
        int index;
    };
    const size_t k = static_cast<size_t>(clusterCount);
    std::vector<std::vector<Neighbor>> nearest(k, std::vector<Neighbor>(k));
    std::vector<uint32_t> population(k);
    constexpr int kMaxIterations = 10;
    constexpr double kMinDeltaE = 3.0;
    for (int iteration = 0; iteration < kMaxIterations; ++iteration) {
        for (int i = 0; i < clusterCount; ++i) {
            nearest[size_t(i)][size_t(i)] = {0, i};
            for (int j = 0; j < i; ++j) {
                double d = clusters[size_t(i)].distance(clusters[size_t(j)]);
                nearest[size_t(j)][size_t(i)] = {d, i};
                nearest[size_t(i)][size_t(j)] = {d, j};
            }
        }
        for (auto& row : nearest) std::ranges::stable_sort(row, {}, &Neighbor::distance);
        bool moved = false;
        for (size_t i = 0; i < points.size(); ++i) {
            int previous = clusterOf[i];
            double previousDistance = points[i].distance(clusters[size_t(previous)]), best = previousDistance;
            int next = -1;
            for (int j = 0; j < clusterCount; ++j) {
                // As Material Color Utilities: clusters 4× (squared) farther from the old one than the point skipped.
                if (nearest[size_t(previous)][size_t(j)].distance >= 4 * previousDistance) continue;
                double d = points[i].distance(clusters[size_t(j)]);
                if (d < best) best = d, next = j;
            }
            if (next != -1 && std::abs(std::sqrt(best) - std::sqrt(previousDistance)) > kMinDeltaE) {
                moved = true;
                clusterOf[i] = next;
            }
        }
        if (!moved && iteration != 0) break;
        std::vector<Lab> sums(k);
        std::ranges::fill(population, 0);
        for (size_t i = 0; i < points.size(); ++i) {
            size_t c = size_t(clusterOf[i]);
            population[c] += weights[i];
            sums[c].l += points[i].l * weights[i], sums[c].a += points[i].a * weights[i], sums[c].b += points[i].b * weights[i];
        }
        for (size_t c = 0; c < size_t(clusterCount); ++c)
            clusters[c] = population[c] ? Lab{sums[c].l / population[c], sums[c].a / population[c], sums[c].b / population[c]} : Lab{};
    }
    std::map<Rgb, uint32_t> result;
    for (size_t c = 0; c < size_t(clusterCount); ++c)
        if (population[c]) result[rgbOf(clusters[c])] += population[c];
    return result;
}

} // namespace

std::vector<std::pair<Color, uint32_t>> quantizeColors(const ImageBitmap& image, int maxColors) {
    maxColors = std::min(maxColors, 256);
    if (image.empty() || maxColors <= 0) return {};
    std::unordered_map<Rgb, uint32_t> counts;
    const uint8_t* p = image.pixels();
    for (size_t i = 0, n = size_t(image.width()) * size_t(image.height()); i < n; ++i, p += 4)
        if (p[3] == 255) ++counts[Rgb(p[0]) << 16 | Rgb(p[1]) << 8 | p[2]];
    if (counts.empty()) return {};
    std::vector<Rgb> wu = QuantizerWu().quantize(counts, maxColors);
    std::vector<std::pair<Color, uint32_t>> out;
    for (const auto& [c, n] : quantizeWsmeans(counts, wu, maxColors)) out.push_back({colorOf(c), n});
    std::ranges::stable_sort(out, std::greater{}, &std::pair<Color, uint32_t>::second);
    return out;
}

std::vector<Color> seedColors(const std::vector<std::pair<Color, uint32_t>>& colors, int desired) {
    constexpr double kTargetChroma = 48, kWeightProportion = 0.7, kWeightChromaAbove = 0.3, kWeightChromaBelow = 0.1;
    constexpr double kCutoffChroma = 5, kCutoffExcitedProportion = 0.01;
    auto hueIndex = [](double h) { return int(sanitizeDegrees(h)) % 360; };
    std::vector<Hct> hcts;
    std::vector<double> huePopulation(360);
    double total = 0;
    for (const auto& [color, n] : colors) {
        Hct h = Hct::fromColor(color);
        hcts.push_back(h);
        huePopulation[size_t(hueIndex(std::floor(h.hue)))] += n;
        total += n;
    }
    // A hue counts with the 30° around it.
    std::vector<double> excited(360);
    if (total > 0)
        for (int hue = 0; hue < 360; ++hue)
            for (int i = hue - 14; i < hue + 16; ++i) excited[size_t(hueIndex(i))] += huePopulation[size_t(hue)] / total;
    struct Scored {
        Hct hct;
        double score;
    };
    std::vector<Scored> scored;
    for (const Hct& h : hcts) {
        double proportion = excited[size_t(hueIndex(std::round(h.hue)))];
        if (h.chroma < kCutoffChroma || proportion <= kCutoffExcitedProportion) continue;
        double chromaWeight = h.chroma < kTargetChroma ? kWeightChromaBelow : kWeightChromaAbove;
        scored.push_back({h, proportion * 100 * kWeightProportion + (h.chroma - kTargetChroma) * chromaWeight});
    }
    std::ranges::stable_sort(scored, std::greater{}, &Scored::score);
    // As far apart in hue as `desired` of them can be: 90° apart first, down to 15°.
    std::vector<Hct> chosen;
    for (int apart = 90; apart >= 15; --apart) {
        chosen.clear();
        for (const Scored& s : scored) {
            bool near = std::ranges::any_of(chosen, [&](const Hct& c) {
                double d = std::abs(s.hct.hue - c.hue);
                return std::min(d, 360 - d) < apart;
            });
            if (!near) chosen.push_back(s.hct);
            if (int(chosen.size()) >= desired) break;
        }
        if (int(chosen.size()) >= desired) break;
    }
    std::vector<Color> out;
    for (const Hct& h : chosen) out.push_back(h.toColor());
    if (out.empty()) out.push_back(Color::hex(0x4285F4));
    return out;
}

TonalPalette TonalPalette::fromImage(const ImageBitmap& image) {
    if (image.empty()) return fromSeed(Color::hex(0x4285F4));
    // Scaled down (each pixel the average of its block) to about 112 × 112, as Android does.
    int w = image.width(), h = image.height();
    int step = std::max(1, int(std::sqrt(double(w) * h / (112.0 * 112.0))));
    int sw = std::max(1, w / step), sh = std::max(1, h / step);
    std::vector<uint8_t> small(size_t(sw) * size_t(sh) * 4);
    const uint8_t* src = image.pixels();
    for (int y = 0; y < sh; ++y)
        for (int x = 0; x < sw; ++x) {
            uint32_t sum[4] = {};
            for (int dy = 0; dy < step; ++dy)
                for (int dx = 0; dx < step; ++dx) {
                    const uint8_t* q = src + (size_t(y * step + dy) * size_t(w) + size_t(x * step + dx)) * 4;
                    for (int k = 0; k < 4; ++k) sum[k] += q[k];
                }
            uint8_t* d = &small[(size_t(y) * size_t(sw) + size_t(x)) * 4];
            for (int k = 0; k < 4; ++k) d[k] = uint8_t(sum[k] / uint32_t(step * step));
        }
    return fromSeed(seedColors(quantizeColors(ImageBitmap::fromPixels(sw, sh, std::move(small)), 128)).front());
}

} // namespace kompoz
