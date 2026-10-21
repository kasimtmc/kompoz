#pragma once

// The curve geometry paths are built on, ported from Skia's (SkGeometry, SkCubicClipper,
// SkPathPriv's winding and convexity), so that a Path is what an Android one is: quadratics,
// conics (rational quadratics: arcs and ovals exactly), cubics; their roots, chops, conversions,
// and whether a point is inside.

#include <kompoz/graphics.hpp>

#include <array>
#include <vector>

namespace kompoz::detail::curves {

// The roots of A t^2 + B t + C in (0, 1), in order, a double root once (Numerical Recipes').
int unitQuadRoots(float a, float b, float c, float roots[2]);

Offset quadAt(const Offset p[3], float t);
Offset quadTangent(const Offset p[3], float t);
// Split at t (0 < t < 1): dst[0..2] and dst[2..4].
void chopQuadAt(const Offset src[3], Offset dst[5], float t);
// Split at its y extremum if it has one (made flat there): 0 for one quad, 1 for two.
int chopQuadAtYExtrema(const Offset src[3], Offset dst[5]);

Offset cubicAt(const Offset p[4], float t);
Offset cubicTangent(const Offset p[4], float t);
void chopCubicAt(const Offset src[4], Offset dst[7], float t);
// The piece between t0 and t1.
void chopCubicBetween(const Offset src[4], Offset dst[4], float t0, float t1);
// Split at its y extrema: 0, 1 or 2 splits (dst[0..3], [3..6], [6..9]), monotonic in y.
int chopCubicAtYExtrema(const Offset src[4], Offset dst[10]);
// Where a cubic monotonic in y crosses y (by bisection, as Skia's).
bool chopMonoCubicAtY(const Offset p[4], float y, float* t);

// A rational quadratic: p[1] weighed by w (an arc of a circle's quarter: w = cos(45°)).
struct Conic {
    Offset p[3];
    float w = 1;
    Offset at(float t) const;
    Offset tangent(float t) const;
    bool chopAt(float t, Conic dst[2]) const;
    Conic between(float t1, float t2) const;  // the piece between t1 and t2
    void chop(Conic dst[2]) const;            // halves
    bool findYExtrema(float* t) const;
    bool chopAtYExtrema(Conic dst[2]) const;
    // How many times to halve it (a power of 2) for quadratics within `tolerance` of it, and the
    // quadratics: pts[0] then two points for each (2 * 2^pow2 + 1 points).
    int quadPow2(float tolerance) const;
    int chopIntoQuadsPow2(Offset pts[], int pow2) const;
    // An arc of the unit circle from the unit vector `start` to `stop` (clockwise or not) as at
    // most five conics, mapped by `m` (x' = m0 x + m1 y + m2, y' = m3 x + m4 y + m5): none if
    // they coincide.
    static int unitArc(Offset start, Offset stop, bool clockwise, const std::array<float, 6>& m, Conic dst[5]);
    // The weight of the conic `pts`, w, through a projective map (rows m0..m8).
    static float transformedWeight(const Offset pts[3], float w, const std::array<float, 9>& m);
};
inline constexpr int kMaxConicToQuadPow2 = 5;

// Whether `p` is inside `commands` by `fill`, its curves exact (Skia's SkPath::contains: a point
// on the outline is inside; open contours count as closed).
bool contains(const std::vector<PathCommand>& commands, Path::FillType fill, Offset p);

// How many times the edges wind round `p` (crossing a ray from it; points on them don't count),
// open contours closed by a line or not.
int winding(const std::vector<PathCommand>& commands, Offset p, bool closeOpen);

// Skia's convexity: one contour (trailing moves aside) that turns one way all round.
bool convex(const std::vector<PathCommand>& commands);

} // namespace kompoz::detail::curves
