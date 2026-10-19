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
// |p[i] - 2 p[i+1] + p[i+2]|) needs to stay within kTolerance (Wang's formula).
int pieces(float bend, int degree) {
    float n = std::sqrt(float(degree * (degree - 1)) / 8 * bend / kTolerance);
    return std::clamp(int(std::ceil(n)), 1, 1000);
}

// Points round `center` from `from` (a vector from it) turning by `angle` radians (screen
// clockwise if positive), the first one left out.
void arc(std::vector<Offset>& out, Offset center, Offset from, float angle) {
    float r = length(from);
    if (r <= 0) return;
    float step = 2 * std::acos(std::max(-1.f, 1 - kTolerance / r));
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
void join(std::vector<Offset>& points, Offset p, Offset in, Offset out, float half, StrokeJoin kind, float miterLimit) {
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
    case StrokeJoin::Round: arc(points, p, nIn, std::atan2(cross(nIn, nOut), dot(nIn, nOut))); points.pop_back(); break;
    case StrokeJoin::Bevel: break;
    }
    points.push_back(p + nOut);
}

// The end of an open stroke at p going `dir`: from its left side round to its right.
void cap(std::vector<Offset>& points, Offset p, Offset dir, float half, StrokeCap kind) {
    Offset n = leftOf(dir) * half;
    switch (kind) {
    case StrokeCap::Butt: break;
    case StrokeCap::Square:
        points.push_back(p + n + dir * half);
        points.push_back(p - n + dir * half);
        break;
    case StrokeCap::Round: arc(points, p, n, std::numbers::pi_v<float>); points.pop_back(); break;
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
            int n = pieces(length(p0 - p1 * 2 + end), 2);
            for (int i = 1; i <= n; ++i) {
                float t = float(i) / float(n), u = 1 - t;
                add(p0 * (u * u) + p1 * (2 * u * t) + end * (t * t));
            }
            break;
        }
        case PathCommand::Verb::Cubic: {
            Offset p0 = current, p1 = place.at(c.x1, c.y1), p2 = place.at(c.x2, c.y2);
            int n = pieces(std::max(length(p0 - p1 * 2 + p2), length(p1 - p2 * 2 + end)), 3);
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

std::vector<Contour> strokeOutline(const std::vector<Contour>& contours, float width, StrokeCap capKind, StrokeJoin joinKind, float miterLimit) {
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
                arc(dot.points, p[0], {half, 0}, 2 * std::numbers::pi_v<float>);
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
            for (size_t i = 0; i < n; ++i) join(left.points, p[i], dirs[(i + n - 1) % n], dirs[i], half, joinKind, miterLimit);
            for (size_t k = 0; k < n; ++k) {
                size_t i = n - 1 - k;  // backwards: its left is the other side
                join(right.points, p[i], dirs[i] * -1, dirs[(i + n - 1) % n] * -1, half, joinKind, miterLimit);
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
        for (size_t i = 1; i + 1 < n; ++i) join(o, p[i], dirs[i - 1], dirs[i], half, joinKind, miterLimit);
        o.push_back(p[n - 1] + leftOf(dirs[n - 2]) * half);
        cap(o, p[n - 1], dirs[n - 2], half, capKind);
        o.push_back(p[n - 1] - leftOf(dirs[n - 2]) * half);
        for (size_t k = 1; k + 1 < n; ++k) {
            size_t i = n - 1 - k;
            join(o, p[i], dirs[i] * -1, dirs[i - 1] * -1, half, joinKind, miterLimit);
        }
        o.push_back(p[0] - leftOf(dirs[0]) * half);
        cap(o, p[0], dirs[0] * -1, half, capKind);
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

} // namespace kompoz::gpu
