// Material's HCT color space and the palettes Material You makes from one color, as Material Color
// Utilities (CAM16 under its default viewing conditions, tone as L*).

#include <kompoz/layout.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <optional>

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

} // namespace kompoz
