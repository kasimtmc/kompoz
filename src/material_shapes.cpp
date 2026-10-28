// Material 3 Expressive's shapes, as rounded polygons and stars, and morphs between them.

#include <kompoz/material_shapes.hpp>

#include <algorithm>
#include <cmath>
#include <functional>
#include <numbers>

namespace kompoz {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// Fits the points into the unit square, keeping their proportions, in the middle.
ShapeOutline fitted(std::vector<Offset> points) {
    float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
    for (Offset p : points) x0 = std::min(x0, p.x), y0 = std::min(y0, p.y), x1 = std::max(x1, p.x), y1 = std::max(y1, p.y);
    float k = 1 / std::max({x1 - x0, y1 - y0, 1e-6f});
    float dx = (1 - (x1 - x0) * k) / 2, dy = (1 - (y1 - y0) * k) / 2;
    for (Offset& p : points) p = {(p.x - x0) * k + dx, (p.y - y0) * k + dy};
    return ShapeOutline{std::move(points)};
}

// A corner rounded by an arc of `radius` (smaller if the edges are too short), as points.
void roundCorner(Offset prev, Offset at, Offset next, float radius, std::vector<Offset>& out) {
    auto unit = [](Offset v) {
        float len = std::hypot(v.x, v.y);
        return len > 0 ? Offset{v.x / len, v.y / len} : Offset{};
    };
    Offset d1 = unit({prev.x - at.x, prev.y - at.y}), d2 = unit({next.x - at.x, next.y - at.y});
    float cosine = std::clamp(d1.x * d2.x + d1.y * d2.y, -1.f, 1.f);
    float angle = std::acos(cosine);  // between the edges
    if (radius <= 0 || angle < 1e-3f || angle > kPi - 1e-3f) {
        out.push_back(at);
        return;
    }
    float half = angle / 2;
    float room = std::min(std::hypot(prev.x - at.x, prev.y - at.y), std::hypot(next.x - at.x, next.y - at.y)) / 2;
    float along = std::min(radius / std::tan(half), room);  // from the corner to where the arc starts
    float r = along * std::tan(half);
    Offset bisector = unit({d1.x + d2.x, d1.y + d2.y});
    float out_ = r / std::sin(half);
    Offset center{at.x + bisector.x * out_, at.y + bisector.y * out_};
    Offset start{at.x + d1.x * along, at.y + d1.y * along}, end{at.x + d2.x * along, at.y + d2.y * along};
    float a0 = std::atan2(start.y - center.y, start.x - center.x), a1 = std::atan2(end.y - center.y, end.x - center.x);
    float sweep = a1 - a0;
    while (sweep > kPi) sweep -= 2 * kPi;
    while (sweep < -kPi) sweep += 2 * kPi;
    int steps = std::clamp(int(std::ceil(std::abs(sweep) / (kPi / 24))), 2, 24);
    for (int i = 0; i <= steps; ++i) {
        float a = a0 + sweep * float(i) / float(steps);
        out.push_back({center.x + r * std::cos(a), center.y + r * std::sin(a)});
    }
}

// Points around an ellipse (or part of one) through `steps`, from angle `a0` to `a1`.
void arc(std::vector<RoundedCorner>& out, float cx, float cy, float rx, float ry, float a0, float a1, int steps, float rounding = 0) {
    for (int i = 0; i <= steps; ++i) {
        float a = a0 + (a1 - a0) * float(i) / float(steps);
        out.push_back({cx + rx * std::cos(a), cy + ry * std::sin(a), rounding});
    }
}

// A staircase around the cells of a grid that are in (the pixel shapes): its outline, clockwise.
ShapeOutline pixels(int n, const std::function<bool(int x, int y)>& in) {
    // The right edge going down row by row, then the left edge going up: each row a horizontal
    // stretch from its first to its last cell in.
    std::vector<std::pair<int, int>> rows;  // first and one past the last cell of each row in
    for (int y = 0; y < n; ++y) {
        int a = n, b = -1;
        for (int x = 0; x < n; ++x)
            if (in(x, y)) a = std::min(a, x), b = std::max(b, x);
        if (b >= 0) rows.push_back({a, b + 1});
    }
    std::vector<Offset> pts;
    float k = 1.f / float(n);
    int top = 0;  // the first row with a cell in
    while (top < n) {
        bool any = false;
        for (int x = 0; x < n; ++x) any = any || in(x, top);
        if (any) break;
        ++top;
    }
    int r = int(rows.size());
    for (int i = 0; i < r; ++i) {  // down the right side
        float y = float(top + i) * k;
        pts.push_back({float(rows[size_t(i)].second) * k, y});
        pts.push_back({float(rows[size_t(i)].second) * k, y + k});
    }
    for (int i = r - 1; i >= 0; --i) {  // up the left side
        float y = float(top + i) * k;
        pts.push_back({float(rows[size_t(i)].first) * k, y + k});
        pts.push_back({float(rows[size_t(i)].first) * k, y});
    }
    // Drop points in the middle of straight runs.
    std::vector<Offset> out;
    size_t m = pts.size();
    for (size_t i = 0; i < m; ++i) {
        Offset a = pts[(i + m - 1) % m], b = pts[i], c = pts[(i + 1) % m];
        bool straight = (a.x == b.x && b.x == c.x) || (a.y == b.y && b.y == c.y);
        if (!straight) out.push_back(b);
    }
    return fitted(std::move(out));
}

// Leaves around the middle (clovers, a flower): how far out at each angle, `lobes` of them, the
// notches `depth` of the way in; `roundness` < 1 makes the leaves rounder and the notches sharper.
ShapeOutline lobed(int lobes, float depth, float roundness, float turn = 0) {
    std::vector<Offset> pts;
    constexpr int kPoints = 240;
    for (int i = 0; i < kPoints; ++i) {
        float a = 2 * kPi * float(i) / kPoints - kPi / 2;
        float r = 1 - depth + depth * std::pow(std::abs(std::cos(float(lobes) * (a + kPi / 2 + turn) / 2)), roundness);
        pts.push_back({0.5f + 0.5f * r * std::cos(a), 0.5f + 0.5f * r * std::sin(a)});
    }
    return fitted(std::move(pts));
}

ShapeOutline turned(const ShapeOutline& o, float degrees) {
    float a = degrees * kPi / 180, c = std::cos(a), s = std::sin(a);
    std::vector<Offset> pts;
    for (Offset p : o.points) {
        float x = p.x - 0.5f, y = p.y - 0.5f;
        pts.push_back({x * c - y * s, x * s + y * c});
    }
    return fitted(std::move(pts));
}

ShapeOutline squashed(const ShapeOutline& o, float sx, float sy) {
    std::vector<Offset> pts;
    for (Offset p : o.points) pts.push_back({(p.x - 0.5f) * sx, (p.y - 0.5f) * sy});
    return fitted(std::move(pts));
}

} // namespace

ShapeOutline roundedPolygon(const std::vector<RoundedCorner>& corners) {
    std::vector<Offset> pts;
    size_t n = corners.size();
    for (size_t i = 0; i < n; ++i) {
        const RoundedCorner &p = corners[(i + n - 1) % n], &c = corners[i], &q = corners[(i + 1) % n];
        roundCorner({p.x, p.y}, {c.x, c.y}, {q.x, q.y}, c.rounding, pts);
    }
    return fitted(std::move(pts));
}

ShapeOutline roundedStar(int points, float innerRadius, float tipRounding, float notchRounding) {
    std::vector<RoundedCorner> corners;
    for (int i = 0; i < 2 * points; ++i) {
        float a = -kPi / 2 + kPi * float(i) / float(points);
        bool tip = i % 2 == 0;
        float r = tip ? 0.5f : 0.5f * innerRadius;
        corners.push_back({0.5f + r * std::cos(a), 0.5f + r * std::sin(a), tip ? tipRounding : notchRounding});
    }
    return roundedPolygon(corners);
}

namespace MaterialShapes {

#define KOMPOZ_SHAPE(name, ...)                     \
    const ShapeOutline& name() {                    \
        static const ShapeOutline outline = __VA_ARGS__; \
        return outline;                             \
    }

KOMPOZ_SHAPE(Circle, [] {
    std::vector<RoundedCorner> c;
    arc(c, 0.5f, 0.5f, 0.5f, 0.5f, -kPi / 2, 3 * kPi / 2 - 2 * kPi / 96, 95);
    return roundedPolygon(c);
}())
KOMPOZ_SHAPE(Square, roundedPolygon({{0, 0, 0.3f}, {1, 0, 0.3f}, {1, 1, 0.3f}, {0, 1, 0.3f}}))
KOMPOZ_SHAPE(Slanted, roundedPolygon({{0.12f, 0, 0.3f}, {1, 0.04f, 0.3f}, {0.88f, 1, 0.3f}, {0, 0.96f, 0.3f}}))
KOMPOZ_SHAPE(Arch, roundedPolygon({{0, 0.06f, 0.5f}, {1, 0.06f, 0.5f}, {1, 1, 0.12f}, {0, 1, 0.12f}}))
KOMPOZ_SHAPE(Fan, [] {
    std::vector<RoundedCorner> c{{0, 0, 0.18f}};
    arc(c, 0, 0, 1, 1, 0, kPi / 2, 24);
    c[1].rounding = c.back().rounding = 0.18f;
    return roundedPolygon(c);
}())
KOMPOZ_SHAPE(Arrow, roundedPolygon({{0.5f, 0, 0.14f}, {1, 0.92f, 0.16f}, {0.5f, 0.72f, 0.12f}, {0, 0.92f, 0.16f}}))
KOMPOZ_SHAPE(SemiCircle, [] {
    std::vector<RoundedCorner> c;
    arc(c, 0.5f, 0.5f, 0.5f, 0.5f, kPi, 2 * kPi, 48);
    c.front().rounding = c.back().rounding = 0.1f;
    return roundedPolygon(c);
}())
KOMPOZ_SHAPE(Oval, [] {
    std::vector<RoundedCorner> c;
    arc(c, 0.5f, 0.5f, 0.5f, 0.32f, 0, 2 * kPi - 2 * kPi / 96, 95);
    return turned(roundedPolygon(c), -45);
}())
KOMPOZ_SHAPE(Pill, [] {
    std::vector<RoundedCorner> c;
    arc(c, 0.3f, 0.5f, 0.3f, 0.3f, kPi / 2, 3 * kPi / 2, 32);
    arc(c, 0.7f, 0.5f, 0.3f, 0.3f, -kPi / 2, kPi / 2, 32);
    return roundedPolygon(c);
}())
KOMPOZ_SHAPE(Triangle, roundedPolygon({{0.5f, 0, 0.18f}, {1, 0.866f, 0.18f}, {0, 0.866f, 0.18f}}))
KOMPOZ_SHAPE(Diamond, roundedPolygon({{0.5f, 0, 0.18f}, {0.88f, 0.5f, 0.18f}, {0.5f, 1, 0.18f}, {0.12f, 0.5f, 0.18f}}))
KOMPOZ_SHAPE(ClamShell, roundedPolygon({{0.22f, 0.04f, 0.14f}, {0.78f, 0.04f, 0.14f}, {1, 0.42f, 0.14f}, {0.78f, 0.96f, 0.14f},
                                        {0.22f, 0.96f, 0.14f}, {0, 0.42f, 0.14f}}))
KOMPOZ_SHAPE(Pentagon, roundedStar(5, 1, 0.16f, 0))
KOMPOZ_SHAPE(Gem, roundedPolygon({{0.5f, 0, 0.18f}, {0.94f, 0.3f, 0.2f}, {0.8f, 0.86f, 0.2f}, {0.5f, 1, 0.18f}, {0.2f, 0.86f, 0.2f},
                                  {0.06f, 0.3f, 0.2f}}))
KOMPOZ_SHAPE(Sunny, roundedStar(8, 0.8f, 0.08f, 0.08f))
KOMPOZ_SHAPE(VerySunny, roundedStar(8, 0.66f, 0.06f, 0.06f))
KOMPOZ_SHAPE(Cookie4Sided, roundedStar(4, 0.72f, 0.3f, 0.3f))
KOMPOZ_SHAPE(Cookie6Sided, roundedStar(6, 0.8f, 0.2f, 0.2f))
KOMPOZ_SHAPE(Cookie7Sided, roundedStar(7, 0.82f, 0.18f, 0.18f))
KOMPOZ_SHAPE(Cookie9Sided, roundedStar(9, 0.85f, 0.14f, 0.14f))
KOMPOZ_SHAPE(Cookie12Sided, roundedStar(12, 0.88f, 0.1f, 0.1f))
KOMPOZ_SHAPE(Ghostish, [] {
    std::vector<RoundedCorner> c;
    arc(c, 0.5f, 0.5f, 0.5f, 0.5f, kPi, 2 * kPi, 32);
    c.push_back({1, 1, 0.1f});
    c.push_back({0.75f, 0.86f, 0.12f});
    c.push_back({0.5f, 1, 0.1f});
    c.push_back({0.25f, 0.86f, 0.12f});
    c.push_back({0, 1, 0.1f});
    return roundedPolygon(c);
}())
KOMPOZ_SHAPE(Clover4Leaf, lobed(4, 0.42f, 0.45f))
KOMPOZ_SHAPE(Clover8Leaf, lobed(8, 0.34f, 0.45f))
KOMPOZ_SHAPE(Burst, roundedStar(12, 0.72f, 0.02f, 0.02f))
KOMPOZ_SHAPE(SoftBurst, roundedStar(10, 0.74f, 0.08f, 0.08f))
KOMPOZ_SHAPE(Boom, roundedStar(15, 0.56f, 0.01f, 0.02f))
KOMPOZ_SHAPE(SoftBoom, roundedStar(16, 0.62f, 0.05f, 0.05f))
KOMPOZ_SHAPE(Flower, lobed(8, 0.28f, 0.75f))
KOMPOZ_SHAPE(Puffy, squashed(roundedStar(10, 0.86f, 0.12f, 0.04f), 1, 0.86f))
KOMPOZ_SHAPE(PuffyDiamond, turned(roundedStar(4, 0.62f, 0.26f, 0.08f), 45))
KOMPOZ_SHAPE(PixelCircle, pixels(10, [](int x, int y) {
    float cx = float(x) + 0.5f - 5, cy = float(y) + 0.5f - 5;
    return cx * cx + cy * cy <= 25.5f;
}))
KOMPOZ_SHAPE(PixelTriangle, pixels(10, [](int x, int y) {
    float half = (float(y) + 1) / 2;  // a row half as wide again as it is far down
    return std::abs(float(x) + 0.5f - 5) <= half;
}))
KOMPOZ_SHAPE(Bun, [] {
    std::vector<RoundedCorner> c;
    arc(c, 0.5f, 0.42f, 0.5f, 0.42f, kPi, 2 * kPi, 32);  // the top
    c.push_back({0.94f, 0.58f, 0.04f});                   // the pinch
    c.push_back({1, 0.72f, 0.2f});
    c.push_back({1, 1, 0.22f});
    c.push_back({0, 1, 0.22f});
    c.push_back({0, 0.72f, 0.2f});
    c.push_back({0.06f, 0.58f, 0.04f});
    return roundedPolygon(c);
}())
KOMPOZ_SHAPE(Heart, [] {
    std::vector<RoundedCorner> c;
    for (int i = 0; i < 96; ++i) {
        float t = 2 * kPi * float(i) / 96;
        float x = 16 * std::pow(std::sin(t), 3.f);
        float y = 13 * std::cos(t) - 5 * std::cos(2 * t) - 2 * std::cos(3 * t) - std::cos(4 * t);
        c.push_back({x, -y, 0});
    }
    return roundedPolygon(c);
}())

#undef KOMPOZ_SHAPE

const std::array<Named, 35>& all() {
    static const std::array<Named, 35> shapes{{
        {"Circle", Circle},       {"Square", Square},       {"Slanted", Slanted},     {"Arch", Arch},
        {"Fan", Fan},             {"Arrow", Arrow},         {"SemiCircle", SemiCircle}, {"Oval", Oval},
        {"Pill", Pill},           {"Triangle", Triangle},   {"Diamond", Diamond},     {"ClamShell", ClamShell},
        {"Pentagon", Pentagon},   {"Gem", Gem},             {"Sunny", Sunny},         {"VerySunny", VerySunny},
        {"Cookie4Sided", Cookie4Sided}, {"Cookie6Sided", Cookie6Sided}, {"Cookie7Sided", Cookie7Sided},
        {"Cookie9Sided", Cookie9Sided}, {"Cookie12Sided", Cookie12Sided}, {"Ghostish", Ghostish},
        {"Clover4Leaf", Clover4Leaf}, {"Clover8Leaf", Clover8Leaf}, {"Burst", Burst}, {"SoftBurst", SoftBurst},
        {"Boom", Boom},           {"SoftBoom", SoftBoom},   {"Flower", Flower},       {"Puffy", Puffy},
        {"PuffyDiamond", PuffyDiamond}, {"PixelCircle", PixelCircle}, {"PixelTriangle", PixelTriangle},
        {"Bun", Bun},             {"Heart", Heart},
    }};
    return shapes;
}

} // namespace MaterialShapes

std::vector<float> polarRadii(const ShapeOutline& outline, int n) {
    std::vector<float> radii(size_t(n), 0.f), dxs(size_t(n), 0.f), dys(size_t(n), 0.f);
    for (int i = 0; i < n; ++i) {
        float a = 2 * kPi * float(i) / float(n);
        dxs[size_t(i)] = std::cos(a), dys[size_t(i)] = std::sin(a);
    }
    const auto& p = outline.points;
    size_t m = p.size();
    float step = 2 * kPi / float(n);
    for (size_t k = 0; k < m; ++k) {  // where each edge crosses the rays from the middle it can
        Offset u{p[k].x - 0.5f, p[k].y - 0.5f}, v{p[(k + 1) % m].x - 0.5f, p[(k + 1) % m].y - 0.5f};
        Offset e{v.x - u.x, v.y - u.y};
        // The directions it spans seen from the middle (the shorter way round), a ray more each
        // way; all of them for an edge at or across the middle.
        float a0 = std::atan2(u.y, u.x), span = std::remainder(std::atan2(v.y, v.x) - a0, 2 * kPi);
        bool across = std::hypot(u.x, u.y) < 1e-3f || std::hypot(v.x, v.y) < 1e-3f || std::abs(span) > kPi - 1e-2f;
        int first = across ? 0 : int(std::floor(std::min(a0, a0 + span) / step)) - 1;
        int last = across ? n - 1 : int(std::ceil(std::max(a0, a0 + span) / step)) + 1;
        for (int j = first; j <= last && j - first < n; ++j) {
            size_t i = size_t((j % n + n) % n);
            float dx = dxs[i], dy = dys[i];
            float denom = dx * e.y - dy * e.x;
            if (std::abs(denom) < 1e-9f) continue;
            float t = (u.x * e.y - u.y * e.x) / denom;  // along the ray
            float s = (u.x * dy - u.y * dx) / denom;    // along the edge
            if (t >= 0 && s >= -1e-5f && s <= 1 + 1e-5f) radii[i] = std::max(radii[i], t);
        }
    }
    return radii;
}

Morph::Morph(const ShapeOutline& start, const ShapeOutline& end) {
    auto a = polarRadii(start, kSamples), b = polarRadii(end, kSamples);
    std::copy(a.begin(), a.end(), from_.begin());
    std::copy(b.begin(), b.end(), to_.begin());
}

const ShapeOutline& Morph::outlineAt(float progress) {
    int step = std::clamp(int(std::lround(std::clamp(progress, 0.f, 1.f) * kSteps)), 0, kSteps);
    auto& slot = steps_[size_t(step)];
    if (!slot) {
        float t = float(step) / kSteps;
        auto outline = std::make_unique<ShapeOutline>();
        for (int i = 0; i < kSamples; ++i) {
            float a = 2 * kPi * float(i) / kSamples, r = from_[size_t(i)] + (to_[size_t(i)] - from_[size_t(i)]) * t;
            outline->points.push_back({0.5f + r * std::cos(a), 0.5f + r * std::sin(a)});
        }
        slot = std::move(outline);
    }
    return *slot;
}

} // namespace kompoz
