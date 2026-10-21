#include "curves.hpp"

#include <algorithm>
#include <cmath>
#include <utility>

namespace kompoz::detail::curves {

namespace {

Offset operator+(Offset a, Offset b) { return {a.x + b.x, a.y + b.y}; }
Offset operator-(Offset a, Offset b) { return {a.x - b.x, a.y - b.y}; }
Offset operator*(Offset a, float s) { return {a.x * s, a.y * s}; }
float dot(Offset a, Offset b) { return a.x * b.x + a.y * b.y; }
float cross(Offset a, Offset b) { return a.x * b.y - a.y * b.x; }
Offset along(Offset a, Offset b, float t) { return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; }
bool same(Offset a, Offset b) { return a.x == b.x && a.y == b.y; }
bool finite(Offset p) { return std::isfinite(p.x) && std::isfinite(p.y); }
constexpr float kNearlyZero = 1.f / (1 << 12);  // SK_ScalarNearlyZero
bool nearlyEqual(float a, float b) { return std::abs(a - b) <= kNearlyZero; }
// Points equal within a tolerance (SkPointPriv::EqualsWithinTolerance).
bool equalWithin(Offset a, Offset b) { return std::abs(a.x - b.x) <= kNearlyZero && std::abs(a.y - b.y) <= kNearlyZero; }

bool notMonotonic(float a, float b, float c) {
    float ab = a - b, bc = b - c;
    if (ab < 0) bc = -bc;
    return ab == 0 || bc < 0;
}

// numer / denom where it lies in (0, 1).
int unitDivide(float numer, float denom, float* ratio) {
    if (numer < 0) numer = -numer, denom = -denom;
    if (denom == 0 || numer == 0 || numer >= denom) return 0;
    float r = numer / denom;
    if (std::isnan(r) || r == 0) return 0;
    *ratio = r;
    return 1;
}

} // namespace

int unitQuadRoots(float a, float b, float c, float roots[2]) {
    if (a == 0) return unitDivide(-c, b, roots);
    double dr = double(b) * b - 4 * double(a) * c;
    if (dr < 0) return 0;
    float r = float(std::sqrt(dr));
    if (!std::isfinite(r)) return 0;
    float q = b < 0 ? -(b - r) / 2 : -(b + r) / 2;
    float* out = roots;
    out += unitDivide(q, a, out);
    out += unitDivide(c, q, out);
    if (out - roots == 2) {
        if (roots[0] > roots[1]) std::swap(roots[0], roots[1]);
        else if (roots[0] == roots[1]) --out;  // a double root once
    }
    return int(out - roots);
}

Offset quadAt(const Offset p[3], float t) {
    Offset a = along(p[0], p[1], t), b = along(p[1], p[2], t);
    return along(a, b, t);
}

Offset quadTangent(const Offset p[3], float t) {
    if ((t == 0 && same(p[0], p[1])) || (t == 1 && same(p[1], p[2]))) return p[2] - p[0];
    Offset b = p[1] - p[0], a = p[2] - p[1] - b;
    Offset d = a * t + b;
    return d + d;
}

void chopQuadAt(const Offset src[3], Offset dst[5], float t) {
    Offset p01 = along(src[0], src[1], t), p12 = along(src[1], src[2], t);
    dst[0] = src[0], dst[1] = p01, dst[2] = along(p01, p12, t), dst[3] = p12, dst[4] = src[2];
}

int chopQuadAtYExtrema(const Offset src[3], Offset dst[5]) {
    float a = src[0].y, b = src[1].y, c = src[2].y;
    if (notMonotonic(a, b, c)) {
        float t;
        if (unitDivide(a - b, a - b - b + c, &t)) {
            chopQuadAt(src, dst, t);
            dst[1].y = dst[3].y = dst[2].y;  // flat at the extremum
            return 1;
        }
        b = std::abs(a - b) < std::abs(b - c) ? a : c;  // (underflow: made monotonic)
    }
    dst[0] = {src[0].x, a}, dst[1] = {src[1].x, b}, dst[2] = {src[2].x, c};
    return 0;
}

Offset cubicAt(const Offset p[4], float t) {
    Offset ab = along(p[0], p[1], t), bc = along(p[1], p[2], t), cd = along(p[2], p[3], t);
    return along(along(ab, bc, t), along(bc, cd, t), t);
}

Offset cubicTangent(const Offset p[4], float t) {
    if ((t == 0 && same(p[0], p[1])) || (t == 1 && same(p[2], p[3]))) {
        Offset d = t == 0 ? p[2] - p[0] : p[3] - p[1];
        return d.x == 0 && d.y == 0 ? p[3] - p[0] : d;
    }
    Offset a = p[3] + (p[1] - p[2]) * 3 - p[0], b = (p[2] - p[1] * 2 + p[0]) * 2, c = p[1] - p[0];
    return (a * t + b) * t + c;
}

void chopCubicAt(const Offset in[4], Offset dst[7], float t) {
    const Offset src[4] = {in[0], in[1], in[2], in[3]};  // (dst may be where in is)
    if (t == 1) {
        std::copy(src, src + 4, dst);
        dst[4] = dst[5] = dst[6] = src[3];
        return;
    }
    auto mix = [t](Offset a, Offset b) { return (b - a) * t + a; };
    Offset ab = mix(src[0], src[1]), bc = mix(src[1], src[2]), cd = mix(src[2], src[3]);
    Offset abc = mix(ab, bc), bcd = mix(bc, cd);
    dst[0] = src[0], dst[1] = ab, dst[2] = abc, dst[3] = mix(abc, bcd), dst[4] = bcd, dst[5] = cd, dst[6] = src[3];
}

void chopCubicBetween(const Offset src[4], Offset dst[4], float t0, float t1) {
    Offset tail[7];
    if (t0 > 0) {
        chopCubicAt(src, tail, t0);
        src = tail + 3;
    }
    float t = t0 < 1 ? (t1 - t0) / (1 - t0) : 1;
    if (t >= 1) return std::copy(src, src + 4, dst), void();
    Offset head[7];
    chopCubicAt(src, head, std::clamp(t, 0.f, 1.f));
    std::copy(head, head + 4, dst);
}

namespace {

int cubicExtrema(float a, float b, float c, float d, float t[2]) {
    return unitQuadRoots(d - a + 3 * (b - c), 2 * (a - b - b + c), b - a, t);
}

} // namespace

int chopCubicAtYExtrema(const Offset src[4], Offset dst[10]) {
    float t[2];
    int roots = cubicExtrema(src[0].y, src[1].y, src[2].y, src[3].y, t);
    if (roots == 0) {
        std::copy(src, src + 4, dst);
        return 0;
    }
    chopCubicAt(src, dst, t[0]);
    if (roots == 2) chopCubicAt(dst + 3, dst + 3, std::clamp((t[1] - t[0]) / (1 - t[0]), 0.f, 1.f));
    dst[2].y = dst[4].y = dst[3].y;  // flat at each extremum
    if (roots == 2) dst[5].y = dst[7].y = dst[6].y;
    return roots;
}

bool chopMonoCubicAtY(const Offset p[4], float y, float* t) {
    float c[4] = {p[0].y - y, p[1].y - y, p[2].y - y, p[3].y - y};
    float neg, pos;
    if (c[0] < 0) {
        if (c[3] < 0) return false;
        neg = 0, pos = 1;
    } else if (c[0] > 0) {
        if (c[3] > 0) return false;
        neg = 1, pos = 0;
    } else {
        *t = 0;
        return true;
    }
    auto interp = [](float a, float b, float s) { return a + (b - a) * s; };
    const float tolerance = 1.f / 65536;
    do {
        float mid = (pos + neg) / 2;
        float y01 = interp(c[0], c[1], mid), y12 = interp(c[1], c[2], mid), y23 = interp(c[2], c[3], mid);
        float y0123 = interp(interp(y01, y12, mid), interp(y12, y23, mid), mid);
        if (y0123 == 0) {
            *t = mid;
            return true;
        }
        (y0123 < 0 ? neg : pos) = mid;
    } while (std::abs(pos - neg) > tolerance);
    *t = (neg + pos) / 2;
    return true;
}

// --- Conics ---------------------------------------------------------------------------------

Offset Conic::at(float t) const {
    // (p0 (1-t)^2 + 2 w p1 t (1-t) + p2 t^2) / ((1-t)^2 + 2 w t (1-t) + t^2)
    Offset n = p[0] * ((1 - t) * (1 - t)) + p[1] * (2 * w * t * (1 - t)) + p[2] * (t * t);
    float d = (1 - t) * (1 - t) + 2 * w * t * (1 - t) + t * t;
    return n * (1 / d);
}

Offset Conic::tangent(float t) const {
    if ((t == 0 && same(p[0], p[1])) || (t == 1 && same(p[1], p[2]))) return p[2] - p[0];
    Offset p20 = p[2] - p[0], p10 = p[1] - p[0];
    Offset c = p10 * w, a = p20 * w - p20, b = p20 - c - c;
    return (a * t + b) * t + c;
}

bool Conic::chopAt(float t, Conic dst[2]) const {
    // In homogeneous coordinates: (p0, 1), (w p1, w), (p2, 1), split there as a quadratic.
    struct P3 {
        float x, y, z;
    };
    P3 a{p[0].x, p[0].y, 1}, b{p[1].x * w, p[1].y * w, w}, c{p[2].x, p[2].y, 1};
    auto mix = [t](P3 u, P3 v) { return P3{u.x + (v.x - u.x) * t, u.y + (v.y - u.y) * t, u.z + (v.z - u.z) * t}; };
    P3 ab = mix(a, b), bc = mix(b, c), abc = mix(ab, bc);
    auto down = [](P3 q) { return Offset{q.x / q.z, q.y / q.z}; };
    dst[0].p[0] = p[0], dst[0].p[1] = down(ab), dst[0].p[2] = down(abc);
    dst[1].p[0] = dst[0].p[2], dst[1].p[1] = down(bc), dst[1].p[2] = p[2];
    float root = std::sqrt(abc.z);
    dst[0].w = ab.z / root, dst[1].w = bc.z / root;
    for (int i = 0; i < 2; ++i)
        for (const Offset& q : dst[i].p)
            if (!finite(q)) return false;
    return std::isfinite(dst[0].w) && std::isfinite(dst[1].w);
}

Conic Conic::between(float t1, float t2) const {
    if (t1 == 0 && t2 == 1) return *this;
    if (t1 == 0 || t2 == 1) {
        Conic pair[2];
        if (chopAt(t1 != 0 ? t1 : t2, pair)) return pair[t1 != 0 ? 1 : 0];
    }
    // Numerators and denominators at t1, the middle and t2 (Skia's SkConic::chopAt(t1, t2)).
    auto numer = [&](float t) { return p[0] * ((1 - t) * (1 - t)) + p[1] * (2 * w * t * (1 - t)) + p[2] * (t * t); };
    auto denom = [&](float t) { return (1 - t) * (1 - t) + 2 * w * t * (1 - t) + t * t; };
    float mid = (t1 + t2) / 2;
    Offset aXY = numer(t1), dXY = numer(mid), cXY = numer(t2);
    float aZ = denom(t1), dZ = denom(mid), cZ = denom(t2);
    Offset bXY = dXY * 2 - (aXY + cXY) * 0.5f;
    float bZ = dZ * 2 - (aZ + cZ) * 0.5f;
    Conic out;
    out.p[0] = aXY * (1 / aZ), out.p[1] = bXY * (1 / bZ), out.p[2] = cXY * (1 / cZ);
    out.w = bZ / std::sqrt(aZ * cZ);
    return out;
}

void Conic::chop(Conic dst[2]) const {
    float scale = 1 / (1 + w);
    Offset t0 = p[0] * scale, t1 = p[1] * (w * scale), t2 = p[2] * scale;
    Offset q1 = t0 + t1, q3 = t1 + t2, q2 = t0 * 0.5f + t1 + t2 * 0.5f;
    dst[0].p[0] = p[0], dst[0].p[1] = q1, dst[0].p[2] = q2;
    dst[1].p[0] = q2, dst[1].p[1] = q3, dst[1].p[2] = p[2];
    dst[0].w = dst[1].w = std::sqrt(0.5f + w * 0.5f);
}

bool Conic::findYExtrema(float* t) const {
    // The derivative's numerator (its magnitude disregarded) as a quadratic.
    float p20 = p[2].y - p[0].y, p10 = p[1].y - p[0].y, wp10 = w * p10;
    float roots[2];
    if (unitQuadRoots(w * p20 - p20, p20 - 2 * wp10, wp10, roots) == 1) {
        *t = roots[0];
        return true;
    }
    return false;
}

bool Conic::chopAtYExtrema(Conic dst[2]) const {
    float t;
    if (!findYExtrema(&t) || !chopAt(t, dst)) return false;
    float y = dst[0].p[2].y;  // flat at the extremum
    dst[0].p[1].y = dst[1].p[0].y = dst[1].p[1].y = y;
    return true;
}

int Conic::quadPow2(float tolerance) const {
    if (tolerance < 0 || !std::isfinite(tolerance) || !finite(p[0]) || !finite(p[1]) || !finite(p[2]) || w < 0 || !std::isfinite(w)) return 0;
    // Floater's error for the quadratic through the same points.
    float a = w - 1, k = a / (4 * (2 + a));
    float x = k * (p[0].x - 2 * p[1].x + p[2].x), y = k * (p[0].y - 2 * p[1].y + p[2].y);
    float error = std::sqrt(x * x + y * y);
    int pow2 = 0;
    for (; pow2 < kMaxConicToQuadPow2; ++pow2) {
        if (error <= tolerance) break;
        error *= 0.25f;
    }
    return pow2;
}

namespace {

bool between(float a, float b, float c) { return (a - b) * (c - b) <= 0; }

Offset* subdivide(const Conic& src, Offset pts[], int level) {
    if (level == 0) {
        pts[0] = src.p[1], pts[1] = src.p[2];
        return pts + 2;
    }
    Conic dst[2];
    src.chop(dst);
    float startY = src.p[0].y, endY = src.p[2].y;
    if (between(startY, src.p[1].y, endY)) {  // keep the halves monotonic in y if it was
        float midY = dst[0].p[2].y;
        if (!between(startY, midY, endY)) dst[0].p[2].y = dst[1].p[0].y = std::abs(midY - startY) < std::abs(midY - endY) ? startY : endY;
        if (!between(startY, dst[0].p[1].y, dst[0].p[2].y)) dst[0].p[1].y = startY;
        if (!between(dst[1].p[0].y, dst[1].p[1].y, endY)) dst[1].p[1].y = endY;
    }
    --level;
    pts = subdivide(dst[0], pts, level);
    return subdivide(dst[1], pts, level);
}

} // namespace

int Conic::chopIntoQuadsPow2(Offset pts[], int pow2) const {
    if (w < 0 || !std::isfinite(w)) pow2 = 0;
    pts[0] = p[0];
    bool done = false;
    if (pow2 == kMaxConicToQuadPow2) {  // an extreme weight: two lines, if the first chop makes them
        Conic dst[2];
        chop(dst);
        if (equalWithin(dst[0].p[1], dst[0].p[2]) && equalWithin(dst[1].p[0], dst[1].p[1])) {
            pts[1] = pts[2] = pts[3] = dst[0].p[1];
            pts[4] = dst[1].p[2];
            pow2 = 1;
            done = true;
        }
    }
    if (!done) subdivide(*this, pts + 1, pow2);
    int count = 2 * (1 << pow2) + 1;
    bool allFinite = true;
    for (int i = 0; i < count; ++i) allFinite = allFinite && finite(pts[i]);
    if (!allFinite)
        for (int i = 1; i < count - 1; ++i) pts[i] = p[1];
    return 1 << pow2;
}

int Conic::unitArc(Offset start, Offset stop, bool clockwise, const std::array<float, 6>& m, Conic dst[5]) {
    float x = dot(start, stop), y = cross(start, stop), absY = std::abs(y);
    if (absY <= kNearlyZero && x > 0 && ((y >= 0 && clockwise) || (y <= 0 && !clockwise))) return 0;
    if (!clockwise) y = -y;
    // One conic a quadrant, then what is left of it (less than 90 degrees).
    int quadrant = 0;
    if (y == 0) quadrant = 2;
    else if (x == 0) quadrant = y > 0 ? 1 : 3;
    else {
        if (y < 0) quadrant += 2;
        if ((x < 0) != (y < 0)) quadrant += 1;
    }
    const Offset quadrantPts[] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};
    const float quadrantWeight = 0.707106781f;
    int count = quadrant;
    for (int i = 0; i < count; ++i) dst[i] = {{quadrantPts[i * 2], quadrantPts[i * 2 + 1], quadrantPts[(i * 2 + 2) % 8]}, quadrantWeight};
    Offset final{x, y};
    Offset lastQ = quadrantPts[quadrant * 2];
    float d = dot(lastQ, final);
    if (std::isnan(d)) return 0;
    if (d < 1) {
        Offset offCurve{lastQ.x + x, lastQ.y + y};
        float cosHalf = std::sqrt((1 + d) / 2);
        float len = std::hypot(offCurve.x, offCurve.y);
        if (len > 0) offCurve = offCurve * (1 / cosHalf / len);
        if (!equalWithin(lastQ, offCurve)) dst[count++] = {{lastQ, offCurve, final}, cosHalf};
    }
    // Turned to start at `start` (mirrored in y going counterclockwise), then mapped.
    float s = start.y, c = start.x;
    for (int i = 0; i < count; ++i)
        for (Offset& q : dst[i].p) {
            float qy = clockwise ? q.y : -q.y;
            Offset r{c * q.x - s * qy, s * q.x + c * qy};
            q = {m[0] * r.x + m[1] * r.y + m[2], m[3] * r.x + m[4] * r.y + m[5]};
        }
    return count;
}

float Conic::transformedWeight(const Offset pts[3], float w, const std::array<float, 9>& m) {
    auto z = [&](Offset q, float qw) { return double(m[6]) * q.x * qw + double(m[7]) * q.y * qw + double(m[8]) * qw; };
    double w0 = z(pts[0], 1), w1 = z(pts[1], w), w2 = z(pts[2], 1);
    return float(std::sqrt(w1 * w1 / (w0 * w2)));
}

// --- Winding (Skia's SkPathPriv::Contains) ------------------------------------------------

namespace {

bool onCurve(float x, float y, Offset start, Offset end) {
    if (start.y == end.y) return between(start.x, x, end.x) && x != end.x;
    return x == start.x && y == start.y;
}

int windingMonoCubic(const Offset p[4], float x, float y, int* onCount) {
    float y0 = p[0].y, y3 = p[3].y;
    int dir = 1;
    if (y0 > y3) std::swap(y0, y3), dir = -1;
    if (y < y0 || y > y3) return 0;
    if (onCurve(x, y, p[0], p[3])) return ++*onCount, 0;
    if (y == y3) return 0;
    float lo = std::min({p[0].x, p[1].x, p[2].x, p[3].x}), hi = std::max({p[0].x, p[1].x, p[2].x, p[3].x});
    if (x < lo) return 0;
    if (x > hi) return dir;
    float t;
    if (!chopMonoCubicAtY(p, y, &t)) return 0;
    float xt = cubicAt(p, t).x;
    if (nearlyEqual(xt, x) && (x != p[3].x || y != p[3].y)) return ++*onCount, 0;
    return xt < x ? dir : 0;
}

int windingCubic(const Offset p[4], float x, float y, int* onCount) {
    Offset dst[10];
    int n = chopCubicAtYExtrema(p, dst);
    int w = 0;
    for (int i = 0; i <= n; ++i) w += windingMonoCubic(dst + i * 3, x, y, onCount);
    return w;
}

double conicNumerator(float p0, float p1, float p2, float w, float t) {
    float c = p0, a = p2 - 2 * p1 * w + c, b = 2 * (p1 * w - c);
    return (a * t + b) * t + c;
}
double conicDenominator(float w, float t) {
    float b = 2 * (w - 1), a = -b;
    return (a * t + b) * t + 1;
}

int windingMonoConic(const Conic& c, float x, float y, int* onCount) {
    const Offset* p = c.p;
    float y0 = p[0].y, y2 = p[2].y;
    int dir = 1;
    if (y0 > y2) std::swap(y0, y2), dir = -1;
    if (y < y0 || y > y2) return 0;
    if (onCurve(x, y, p[0], p[2])) return ++*onCount, 0;
    if (y == y2) return 0;
    float roots[2];
    float a = p[2].y, b = p[1].y * c.w - y * c.w + y, cc = p[0].y;
    a += cc - 2 * b;
    b -= cc;
    cc -= y;
    int n = unitQuadRoots(a, 2 * b, cc, roots);
    float xt = n == 0 ? p[1 - dir].x : float(conicNumerator(p[0].x, p[1].x, p[2].x, c.w, roots[0]) / conicDenominator(c.w, roots[0]));
    if (nearlyEqual(xt, x) && (x != p[2].x || y != p[2].y)) return ++*onCount, 0;
    return xt < x ? dir : 0;
}

bool monoQuad(float y0, float y1, float y2) {
    if (y0 == y1) return true;
    return y0 < y1 ? y1 <= y2 : y1 >= y2;
}

int windingConic(const Offset p[3], float w, float x, float y, int* onCount) {
    Conic c{{p[0], p[1], p[2]}, w};
    Conic chopped[2];
    bool mono = monoQuad(p[0].y, p[1].y, p[2].y) || !c.chopAtYExtrema(chopped);
    int wind = windingMonoConic(mono ? c : chopped[0], x, y, onCount);
    if (!mono) wind += windingMonoConic(chopped[1], x, y, onCount);
    return wind;
}

int windingMonoQuad(const Offset p[3], float x, float y, int* onCount) {
    float y0 = p[0].y, y2 = p[2].y;
    int dir = 1;
    if (y0 > y2) std::swap(y0, y2), dir = -1;
    if (y < y0 || y > y2) return 0;
    if (onCurve(x, y, p[0], p[2])) return ++*onCount, 0;
    if (y == y2) return 0;
    float roots[2];
    int n = unitQuadRoots(p[0].y - 2 * p[1].y + p[2].y, 2 * (p[1].y - p[0].y), p[0].y - y, roots);
    float xt;
    if (n == 0) {
        xt = p[1 - dir].x;
    } else {
        float t = roots[0], c = p[0].x, a = p[2].x - 2 * p[1].x + c, b = 2 * (p[1].x - c);
        xt = (a * t + b) * t + c;
    }
    if (nearlyEqual(xt, x) && (x != p[2].x || y != p[2].y)) return ++*onCount, 0;
    return xt < x ? dir : 0;
}

int windingQuad(const Offset p[3], float x, float y, int* onCount) {
    Offset dst[5];
    const Offset* q = p;
    int n = 0;
    if (!monoQuad(p[0].y, p[1].y, p[2].y)) {
        n = chopQuadAtYExtrema(p, dst);
        q = dst;
    }
    int w = windingMonoQuad(q, x, y, onCount);
    if (n > 0) w += windingMonoQuad(q + 2, x, y, onCount);
    return w;
}

int windingLine(const Offset p[2], float x, float y, int* onCount) {
    float x0 = p[0].x, y0 = p[0].y, x1 = p[1].x, y1 = p[1].y, dy = y1 - y0;
    int dir = 1;
    if (y0 > y1) std::swap(y0, y1), dir = -1;
    if (y < y0 || y > y1) return 0;
    if (onCurve(x, y, p[0], p[1])) return ++*onCount, 0;
    if (y == y1) return 0;
    float c = (x1 - x0) * (y - p[0].y) - dy * (x - x0);
    if (c == 0) {
        if (x != x1 || y != p[1].y) ++*onCount;
        dir = 0;
    } else if ((c > 0 ? 1 : -1) == dir) {
        dir = 0;
    }
    return dir;
}

void tangentCubic(const Offset p[4], float x, float y, std::vector<Offset>& tangents) {
    if (!between(p[0].y, y, p[1].y) && !between(p[1].y, y, p[2].y) && !between(p[2].y, y, p[3].y)) return;
    if (!between(p[0].x, x, p[1].x) && !between(p[1].x, x, p[2].x) && !between(p[2].x, x, p[3].x)) return;
    Offset dst[10];
    int n = chopCubicAtYExtrema(p, dst);
    for (int i = 0; i <= n; ++i) {
        const Offset* c = dst + i * 3;
        float t;
        if (!chopMonoCubicAtY(c, y, &t) || !nearlyEqual(x, cubicAt(c, t).x)) continue;
        tangents.push_back(cubicTangent(c, t));
    }
}

void tangentConic(const Offset p[3], float w, float x, float y, std::vector<Offset>& tangents) {
    if (!between(p[0].y, y, p[1].y) && !between(p[1].y, y, p[2].y)) return;
    if (!between(p[0].x, x, p[1].x) && !between(p[1].x, x, p[2].x)) return;
    float roots[2];
    float a = p[2].y, b = p[1].y * w - y * w + y, c = p[0].y;
    a += c - 2 * b;
    b -= c;
    c -= y;
    int n = unitQuadRoots(a, 2 * b, c, roots);
    for (int i = 0; i < n; ++i) {
        float t = roots[i];
        float xt = float(conicNumerator(p[0].x, p[1].x, p[2].x, w, t) / conicDenominator(w, t));
        if (!nearlyEqual(x, xt)) continue;
        tangents.push_back(Conic{{p[0], p[1], p[2]}, w}.tangent(t));
    }
}

void tangentQuad(const Offset p[3], float x, float y, std::vector<Offset>& tangents) {
    if (!between(p[0].y, y, p[1].y) && !between(p[1].y, y, p[2].y)) return;
    if (!between(p[0].x, x, p[1].x) && !between(p[1].x, x, p[2].x)) return;
    float roots[2];
    int n = unitQuadRoots(p[0].y - 2 * p[1].y + p[2].y, 2 * (p[1].y - p[0].y), p[0].y - y, roots);
    for (int i = 0; i < n; ++i) {
        float t = roots[i], c = p[0].x, a = p[2].x - 2 * p[1].x + c, b = 2 * (p[1].x - c);
        if (!nearlyEqual(x, (a * t + b) * t + c)) continue;
        tangents.push_back(quadTangent(p, t));
    }
}

void tangentLine(const Offset p[2], float x, float y, std::vector<Offset>& tangents) {
    if (!between(p[0].y, y, p[1].y) || !between(p[0].x, x, p[1].x)) return;
    float dx = p[1].x - p[0].x, dy = p[1].y - p[0].y;
    if (!nearlyEqual((x - p[0].x) * dy, dx * (y - p[0].y))) return;
    tangents.push_back({dx, dy});
}

// Each edge of the path, open contours closed by a line unless `closeOpen` is false (Skia's
// SkPathEdgeIter).
template <class Edge>
void forEachEdge(const std::vector<PathCommand>& commands, Edge&& edge, bool closeOpen = true) {
    Offset start, current;
    bool open = false;
    auto closeContour = [&] {
        if (closeOpen && open && !same(current, start)) {
            Offset line[2] = {current, start};
            edge(PathCommand::Verb::Line, line, 1.f);
        }
        open = false;
        current = start;
    };
    for (const PathCommand& c : commands) {
        Offset end{c.x, c.y};
        switch (c.verb) {
        case PathCommand::Verb::Move:
            closeContour();
            start = current = end;
            break;
        case PathCommand::Verb::Line: {
            Offset p[2] = {current, end};
            edge(c.verb, p, 1.f);
            current = end, open = true;
            break;
        }
        case PathCommand::Verb::Quad:
        case PathCommand::Verb::Conic: {
            Offset p[3] = {current, {c.x1, c.y1}, end};
            edge(c.verb, p, c.w);
            current = end, open = true;
            break;
        }
        case PathCommand::Verb::Cubic: {
            Offset p[4] = {current, {c.x1, c.y1}, {c.x2, c.y2}, end};
            edge(c.verb, p, 1.f);
            current = end, open = true;
            break;
        }
        case PathCommand::Verb::Close: closeContour(); break;
        }
    }
    closeContour();
}

} // namespace

int winding(const std::vector<PathCommand>& commands, Offset q, bool closeOpen) {
    int w = 0, onCount = 0;
    forEachEdge(
        commands,
        [&](PathCommand::Verb verb, const Offset* p, float weight) {
            switch (verb) {
            case PathCommand::Verb::Line: w += windingLine(p, q.x, q.y, &onCount); break;
            case PathCommand::Verb::Quad: w += windingQuad(p, q.x, q.y, &onCount); break;
            case PathCommand::Verb::Conic: w += windingConic(p, weight, q.x, q.y, &onCount); break;
            case PathCommand::Verb::Cubic: w += windingCubic(p, q.x, q.y, &onCount); break;
            default: break;
            }
        },
        closeOpen);
    return w;
}

bool contains(const std::vector<PathCommand>& commands, Path::FillType fill, Offset q) {
    if (commands.empty()) return false;
    // Inside its bounds (control points included), edges inclusive.
    float l = 1e30f, t = 1e30f, r = -1e30f, b = -1e30f;
    for (const PathCommand& c : commands) {
        if (c.verb == PathCommand::Verb::Close) continue;
        auto add = [&](float x, float y) { l = std::min(l, x), t = std::min(t, y), r = std::max(r, x), b = std::max(b, y); };
        add(c.x, c.y);
        if (c.verb == PathCommand::Verb::Quad || c.verb == PathCommand::Verb::Conic || c.verb == PathCommand::Verb::Cubic) add(c.x1, c.y1);
        if (c.verb == PathCommand::Verb::Cubic) add(c.x2, c.y2);
    }
    if (!(l <= q.x && q.x <= r && t <= q.y && q.y <= b)) return false;
    int w = 0, onCount = 0;
    forEachEdge(commands, [&](PathCommand::Verb verb, const Offset* p, float weight) {
        switch (verb) {
        case PathCommand::Verb::Line: w += windingLine(p, q.x, q.y, &onCount); break;
        case PathCommand::Verb::Quad: w += windingQuad(p, q.x, q.y, &onCount); break;
        case PathCommand::Verb::Conic: w += windingConic(p, weight, q.x, q.y, &onCount); break;
        case PathCommand::Verb::Cubic: w += windingCubic(p, q.x, q.y, &onCount); break;
        default: break;
        }
    });
    bool evenOdd = fill == Path::FillType::EvenOdd;
    if (evenOdd) w &= 1;
    if (w) return true;
    if (onCount <= 1) return onCount != 0;
    if ((onCount & 1) || evenOdd) return (onCount & 1) != 0;
    // On an even number of edges, nonzero: inside unless they coincide (the same tangent, opposite ways).
    std::vector<Offset> tangents;
    forEachEdge(commands, [&](PathCommand::Verb verb, const Offset* p, float weight) {
        size_t before = tangents.size();
        switch (verb) {
        case PathCommand::Verb::Line: tangentLine(p, q.x, q.y, tangents); break;
        case PathCommand::Verb::Quad: tangentQuad(p, q.x, q.y, tangents); break;
        case PathCommand::Verb::Conic: tangentConic(p, weight, q.x, q.y, tangents); break;
        case PathCommand::Verb::Cubic: tangentCubic(p, q.x, q.y, tangents); break;
        default: break;
        }
        if (tangents.size() <= before) return;
        size_t last = tangents.size() - 1;
        Offset tan = tangents[last];
        if (std::abs(dot(tan, tan)) <= kNearlyZero) {
            tangents.pop_back();
            return;
        }
        for (size_t i = 0; i < last; ++i) {
            Offset test = tangents[i];
            if (std::abs(cross(test, tan)) <= kNearlyZero && tan.x * test.x <= 0 && tan.y * test.y <= 0) {
                tangents.pop_back();
                tangents[i] = tangents.back();
                tangents.pop_back();
                break;
            }
        }
    });
    return !tangents.empty();
}

// --- Convexity (Skia's Convexicator) ------------------------------------------------------

namespace {

struct Convexicator {
    enum Change { Unknown, Left, Right, Straight, Backwards, Invalid };
    Offset firstPt, firstVec, lastPt, lastVec;
    Change expected = Invalid;
    int reversals = 0;
    bool isFinite = true;

    void setMove(Offset p) {
        firstPt = lastPt = p;
        expected = Invalid;
    }
    bool addPt(Offset p) {
        if (same(lastPt, p)) return true;
        if (same(firstPt, lastPt) && expected == Invalid && lastVec.x == 0 && lastVec.y == 0) {
            lastVec = firstVec = p - lastPt;
        } else if (!addVec(p - lastPt)) {
            return false;
        }
        lastPt = p;
        return true;
    }
    bool close() { return addPt(firstPt) && addVec(firstVec); }
    Change change(Offset v) const {
        float c = cross(lastVec, v);
        if (!std::isfinite(c)) return Unknown;
        if (c == 0) return dot(lastVec, v) < 0 ? Backwards : Straight;
        return c > 0 ? Right : Left;
    }
    bool addVec(Offset v) {
        switch (Change d = change(v)) {
        case Left:
        case Right:
            if (expected == Invalid) expected = d;
            else if (d != expected) return false;
            lastVec = v;
            break;
        case Straight: break;
        case Backwards: lastVec = v; return ++reversals < 3;
        case Unknown: return isFinite = false;
        case Invalid: break;
        }
        return true;
    }
    static bool concaveBySign(const std::vector<Offset>& pts) {
        if (pts.size() <= 3) return false;
        int dxes = 0, dyes = 0, lastSx = 2, lastSy = 2;
        Offset curr = pts[0];
        auto visit = [&](Offset p) {
            Offset v = p - curr;
            if (v.x != 0 || v.y != 0) {
                if (!finite(v)) return true;
                int sx = v.x < 0, sy = v.y < 0;
                dxes += sx != lastSx, dyes += sy != lastSy;
                if (dxes > 3 || dyes > 3) return true;
                lastSx = sx, lastSy = sy;
            }
            curr = p;
            return false;
        };
        for (size_t i = 1; i < pts.size(); ++i)
            if (visit(pts[i])) return true;
        return visit(pts[0]);  // round to the first again
    }
};

} // namespace

bool convex(const std::vector<PathCommand>& commands) {
    size_t n = commands.size();
    while (n > 0 && commands[n - 1].verb == PathCommand::Verb::Move) --n;
    if (n == 0) return true;
    std::vector<Offset> points;  // all of them, control points too
    for (size_t i = 0; i < n; ++i) {
        const PathCommand& c = commands[i];
        if (c.verb == PathCommand::Verb::Close) continue;
        if (c.verb == PathCommand::Verb::Quad || c.verb == PathCommand::Verb::Conic || c.verb == PathCommand::Verb::Cubic) points.push_back({c.x1, c.y1});
        if (c.verb == PathCommand::Verb::Cubic) points.push_back({c.x2, c.y2});
        points.push_back({c.x, c.y});
    }
    if (Convexicator::concaveBySign(points)) return false;
    int contours = 0;
    bool needsClose = false;
    Convexicator state;
    for (size_t i = 0; i < n; ++i) {
        const PathCommand& c = commands[i];
        if (contours == 0) {
            if (c.verb == PathCommand::Verb::Move) state.setMove({c.x, c.y});
            else ++contours, needsClose = true;
        }
        if (contours == 1) {
            if (c.verb == PathCommand::Verb::Close || c.verb == PathCommand::Verb::Move) {
                if (!state.close()) return false;
                needsClose = false;
                ++contours;
            } else {
                if (c.verb == PathCommand::Verb::Quad || c.verb == PathCommand::Verb::Conic || c.verb == PathCommand::Verb::Cubic)
                    if (!state.addPt({c.x1, c.y1})) return false;
                if (c.verb == PathCommand::Verb::Cubic && !state.addPt({c.x2, c.y2})) return false;
                if (!state.addPt({c.x, c.y})) return false;
            }
        } else if (contours > 1 && c.verb != PathCommand::Verb::Move) {
            return false;  // a second contour
        }
    }
    if (needsClose && !state.close()) return false;
    return !(state.expected == Convexicator::Invalid && state.reversals >= 3);  // (no way round, but back and forth)
}

} // namespace kompoz::detail::curves
