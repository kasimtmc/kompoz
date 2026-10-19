// A path's geometry for the GPU renderer (see path_geometry.hpp).

#include "path_geometry.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kompoz::gpu {
namespace {

Offset operator+(Offset a, Offset b) { return {a.x + b.x, a.y + b.y}; }
Offset operator-(Offset a, Offset b) { return {a.x - b.x, a.y - b.y}; }
Offset operator*(Offset a, float k) { return {a.x * k, a.y * k}; }
float dot(Offset a, Offset b) { return a.x * b.x + a.y * b.y; }
float cross(Offset a, Offset b) { return a.x * b.y - a.y * b.x; }
float length(Offset a) { return std::hypot(a.x, a.y); }
Offset normalized(Offset a) {
    float l = length(a);
    return l > 0 ? a * (1 / l) : Offset{1, 0};
}
// To the left of travel on the screen (y down): (1, 0) has (0, -1).
Offset leftOf(Offset d) { return {d.y, -d.x}; }

// How many lines a curve of degree `degree` whose control polygon bends by `bend` (the largest
// |p[i] - 2 p[i+1] + p[i+2]|) needs to stay within `tolerance` (Wang's formula).
int pieces(float bend, int degree, float tolerance) {
    float n = std::sqrt(float(degree * (degree - 1)) / 8 * bend / tolerance);
    return std::clamp(int(std::ceil(n)), 1, 1000);
}

// Points round `center` from `from` (a vector from it) turning by `angle` radians (screen
// clockwise if positive), the first one left out, within `tolerance` of the circle.
void arc(std::vector<Offset>& out, Offset center, Offset from, float angle, float tolerance) {
    float r = length(from);
    if (r <= 0) return;
    float step = 2 * std::acos(std::max(-1.f, 1 - tolerance / r));
    int n = std::max(1, int(std::ceil(std::abs(angle) / std::max(step, 1e-3f))));
    float c = std::cos(angle / float(n)), s = std::sin(angle / float(n));
    Offset v = from;
    for (int i = 0; i < n; ++i) {
        v = {v.x * c - v.y * s, v.x * s + v.y * c};
        out.push_back(center + v);
    }
}

// The points one side of a stroke takes round the vertex p where it turns from `in` to `out`
// (unit directions), that side being to the left of travel `half` px off the line.
void join(std::vector<Offset>& points, Offset p, Offset in, Offset out, float half, StrokeJoin kind, float miterLimit, float tolerance) {
    Offset nIn = leftOf(in) * half, nOut = leftOf(out) * half;
    float turn = cross(in, out), along = dot(in, out);
    if (std::abs(turn) < 1e-6f && along > 0) {  // straight on
        points.push_back(p + nIn);
        return;
    }
    points.push_back(p + nIn);
    if (turn < 0) {  // the inner side: through the vertex, so that it winds the same way
        points.push_back(p);
        points.push_back(p + nOut);
        return;
    }
    switch (kind) {
    case StrokeJoin::Miter: {
        Offset mid = (nIn + nOut) * 0.5f;
        float m = length(mid);
        if (m > 1e-6f && half / m <= miterLimit) points.push_back(p + mid * (half * half / (m * m)));
        break;
    }
    case StrokeJoin::Round: arc(points, p, nIn, std::atan2(cross(nIn, nOut), dot(nIn, nOut)), tolerance); points.pop_back(); break;
    case StrokeJoin::Bevel: break;
    }
    points.push_back(p + nOut);
}

// The end of an open stroke at p going `dir`: from its left side round to its right.
void cap(std::vector<Offset>& points, Offset p, Offset dir, float half, StrokeCap kind, float tolerance) {
    Offset n = leftOf(dir) * half;
    switch (kind) {
    case StrokeCap::Butt: break;
    case StrokeCap::Square:
        points.push_back(p + n + dir * half);
        points.push_back(p - n + dir * half);
        break;
    case StrokeCap::Round: arc(points, p, n, std::numbers::pi_v<float>, tolerance); points.pop_back(); break;
    }
}

} // namespace

std::vector<Contour> flattenPath(const std::vector<PathCommand>& commands, const PathPlacement& place) {
    std::vector<Contour> out;
    Offset current, start;
    auto add = [&](Offset p) {
        if (out.empty() || out.back().closed) out.push_back({{current}, false});  // drawing on after close(): from its start
        out.back().points.push_back(p);
        current = p;
    };
    for (const PathCommand& c : commands) {
        Offset end = place.at(c.x, c.y);
        switch (c.verb) {
        case PathCommand::Verb::Move:
            out.push_back({{end}, false});
            current = start = end;
            break;
        case PathCommand::Verb::Line: add(end); break;
        case PathCommand::Verb::Quad: {
            Offset p0 = current, p1 = place.at(c.x1, c.y1);
            int n = pieces(length(p0 - p1 * 2 + end), 2, place.tolerance);
            for (int i = 1; i <= n; ++i) {
                float t = float(i) / float(n), u = 1 - t;
                add(p0 * (u * u) + p1 * (2 * u * t) + end * (t * t));
            }
            break;
        }
        case PathCommand::Verb::Cubic: {
            Offset p0 = current, p1 = place.at(c.x1, c.y1), p2 = place.at(c.x2, c.y2);
            int n = pieces(std::max(length(p0 - p1 * 2 + p2), length(p1 - p2 * 2 + end)), 3, place.tolerance);
            for (int i = 1; i <= n; ++i) {
                float t = float(i) / float(n), u = 1 - t;
                add(p0 * (u * u * u) + p1 * (3 * u * u * t) + p2 * (3 * u * t * t) + end * (t * t * t));
            }
            break;
        }
        case PathCommand::Verb::Close:
            if (!out.empty() && !out.back().closed) out.back().closed = true;
            current = start;
            break;
        }
    }
    return out;
}

std::vector<Contour> strokeOutline(const std::vector<Contour>& contours, float width, StrokeCap capKind, StrokeJoin joinKind, float miterLimit,
                                   float tolerance) {
    std::vector<Contour> out;
    float half = width / 2;
    if (half <= 0) return out;
    for (const Contour& c : contours) {
        // Points that go somewhere (a closed contour's last one isn't its first again).
        std::vector<Offset> p;
        for (Offset q : c.points)
            if (p.empty() || length(q - p.back()) > 1e-4f) p.push_back(q);
        if (c.closed && p.size() > 1 && length(p.front() - p.back()) <= 1e-4f) p.pop_back();
        if (p.size() == 1 || (p.size() == 2 && c.closed && length(p[1] - p[0]) <= 1e-4f)) {  // a dot
            if (capKind == StrokeCap::Butt) continue;
            Contour dot{{}, true};
            if (capKind == StrokeCap::Round) {
                arc(dot.points, p[0], {half, 0}, 2 * std::numbers::pi_v<float>, tolerance);
            } else {
                dot.points = {p[0] + Offset{-half, -half}, p[0] + Offset{half, -half}, p[0] + Offset{half, half}, p[0] + Offset{-half, half}};
            }
            out.push_back(std::move(dot));
            continue;
        }
        if (p.empty()) continue;
        size_t n = p.size();
        if (c.closed && n >= 2) {
            // Two loops, one each side, going opposite ways: what lies between them is covered.
            std::vector<Offset> dirs(n);
            for (size_t i = 0; i < n; ++i) dirs[i] = normalized(p[(i + 1) % n] - p[i]);
            Contour left{{}, true}, right{{}, true};
            for (size_t i = 0; i < n; ++i) join(left.points, p[i], dirs[(i + n - 1) % n], dirs[i], half, joinKind, miterLimit, tolerance);
            for (size_t k = 0; k < n; ++k) {
                size_t i = n - 1 - k;  // backwards: its left is the other side
                join(right.points, p[i], dirs[i] * -1, dirs[(i + n - 1) % n] * -1, half, joinKind, miterLimit, tolerance);
            }
            out.push_back(std::move(left));
            out.push_back(std::move(right));
            continue;
        }
        // Open: one side forwards, the end's cap, the other side backwards, the start's cap.
        std::vector<Offset> dirs(n - 1);
        for (size_t i = 0; i + 1 < n; ++i) dirs[i] = normalized(p[i + 1] - p[i]);
        Contour outline{{}, true};
        auto& o = outline.points;
        o.push_back(p[0] + leftOf(dirs[0]) * half);
        for (size_t i = 1; i + 1 < n; ++i) join(o, p[i], dirs[i - 1], dirs[i], half, joinKind, miterLimit, tolerance);
        o.push_back(p[n - 1] + leftOf(dirs[n - 2]) * half);
        cap(o, p[n - 1], dirs[n - 2], half, capKind, tolerance);
        o.push_back(p[n - 1] - leftOf(dirs[n - 2]) * half);
        for (size_t k = 1; k + 1 < n; ++k) {
            size_t i = n - 1 - k;
            join(o, p[i], dirs[i] * -1, dirs[i - 1] * -1, half, joinKind, miterLimit, tolerance);
        }
        o.push_back(p[0] - leftOf(dirs[0]) * half);
        cap(o, p[0], dirs[0] * -1, half, capKind, tolerance);
        out.push_back(std::move(outline));
    }
    return out;
}

void appendEdges(const std::vector<Contour>& contours, std::vector<Edge>& out) {
    for (const Contour& c : contours) {
        size_t n = c.points.size();
        if (n < 2) continue;
        for (size_t i = 0; i < n; ++i) {  // every contour closed, as a fill closes it
            Offset a = c.points[i], b = c.points[(i + 1) % n];
            if (a.y != b.y) out.push_back({a.x, a.y, b.x, b.y});
        }
    }
}

Projective Projective::of(const Matrix& t) {
    const auto& v = t.values;
    Projective p{{v[Matrix::ScaleX], v[Matrix::SkewX], v[Matrix::TranslateX], v[Matrix::SkewY], v[Matrix::ScaleY], v[Matrix::TranslateY],
                  v[Matrix::Perspective0], v[Matrix::Perspective1], v[Matrix::Perspective2]}};
    if (p.affine() && p.m[8] != 0 && p.m[8] != 1)  // w the same everywhere: 1 (in front, whatever its sign was)
        for (float& x : p.m) x /= v[Matrix::Perspective2];
    return p;
}

bool Projective::identity() const {
    return m[0] == 1 && m[1] == 0 && m[2] == 0 && m[3] == 0 && m[4] == 1 && m[5] == 0 && m[6] == 0 && m[7] == 0 && m[8] == 1;
}

Offset Projective::apply(float x, float y, float* w) const {
    float q = m[6] * x + m[7] * y + m[8];
    if (w) *w = q;
    float k = q != 0 ? 1 / q : 0;
    return {(m[0] * x + m[1] * y + m[2]) * k, (m[3] * x + m[4] * y + m[5]) * k};
}

Projective operator*(const Projective& a, const Projective& b) {
    Projective out;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) out.m[r * 3 + c] = a.m[r * 3] * b.m[c] + a.m[r * 3 + 1] * b.m[3 + c] + a.m[r * 3 + 2] * b.m[6 + c];
    return out;
}

Projective Projective::inverse() const {
    const float* a = m;
    float c0 = a[4] * a[8] - a[5] * a[7], c1 = a[5] * a[6] - a[3] * a[8], c2 = a[3] * a[7] - a[4] * a[6];
    float det = a[0] * c0 + a[1] * c1 + a[2] * c2;
    if (det == 0 || !std::isfinite(det)) return {};
    float k = 1 / det;
    return {{c0 * k, (a[2] * a[7] - a[1] * a[8]) * k, (a[1] * a[5] - a[2] * a[4]) * k,
             c1 * k, (a[0] * a[8] - a[2] * a[6]) * k, (a[2] * a[3] - a[0] * a[5]) * k,
             c2 * k, (a[1] * a[6] - a[0] * a[7]) * k, (a[0] * a[4] - a[1] * a[3]) * k}};
}

namespace {
// The singular values of the map's derivative at (x, y): how much it stretches lengths there, the
// most and the least.
std::pair<float, float> stretch(const Projective& p, float x, float y) {
    const float* m = p.m;
    float w;
    Offset q = p.apply(x, y, &w);
    if (w == 0) return {0, 0};
    float j00 = (m[0] - m[6] * q.x) / w, j01 = (m[1] - m[7] * q.x) / w;
    float j10 = (m[3] - m[6] * q.y) / w, j11 = (m[4] - m[7] * q.y) / w;
    float t = j00 * j00 + j01 * j01 + j10 * j10 + j11 * j11, det = std::abs(j00 * j11 - j01 * j10);
    float root = std::sqrt(std::max(0.f, t * t - 4 * det * det));
    return {std::sqrt((t + root) / 2), std::sqrt(std::max(0.f, (t - root) / 2))};
}
} // namespace

float Projective::maxScale(float x, float y) const { return stretch(*this, x, y).first; }
float Projective::minScale(float x, float y) const { return stretch(*this, x, y).second; }

std::vector<Contour> project(const std::vector<Contour>& polygons, const Projective& p) {
    constexpr float kNear = 1e-4f;  // w below this: behind the eye (or too close to it)
    const float* m = p.m;
    std::vector<Contour> out;
    struct H {
        float x, y, w;
    };
    std::vector<H> clipped;
    for (const Contour& c : polygons) {
        size_t n = c.points.size();
        if (n < 2) continue;
        // Each point in homogeneous coordinates, the polygon cut to w >= kNear (Sutherland-Hodgman).
        clipped.clear();
        auto mapped = [&](Offset q) { return H{m[0] * q.x + m[1] * q.y + m[2], m[3] * q.x + m[4] * q.y + m[5], m[6] * q.x + m[7] * q.y + m[8]}; };
        H prev = mapped(c.points[n - 1]);
        for (size_t i = 0; i < n; ++i) {
            H cur = mapped(c.points[i]);
            bool in = cur.w >= kNear, wasIn = prev.w >= kNear;
            if (in != wasIn) {
                float t = (kNear - prev.w) / (cur.w - prev.w);
                clipped.push_back({prev.x + (cur.x - prev.x) * t, prev.y + (cur.y - prev.y) * t, kNear});
            }
            if (in) clipped.push_back(cur);
            prev = cur;
        }
        if (clipped.size() < 2) continue;
        Contour d{{}, true};
        d.points.reserve(clipped.size());
        for (const H& h : clipped) d.points.push_back({h.x / h.w, h.y / h.w});
        out.push_back(std::move(d));
    }
    return out;
}

} // namespace kompoz::gpu
