#pragma once

// A path's geometry for the GPU renderer, in the target's pixels: its curves cut into lines fine
// enough that no point is more than kTolerance px off the curve (Wang's formula), and a stroke
// turned into the outline it covers (joins, caps), to be filled by the non-zero rule like any
// fill. The renderer then accumulates the lines' coverage on the GPU (shaders/path.slang).

#include <kompoz/graphics.hpp>

#include <vector>

namespace kompoz::gpu {

constexpr float kTolerance = 0.2f;  // px

struct Contour {
    std::vector<Offset> points;
    bool closed = false;
};

// Where a path point (dp, from `origin`) is in the target: (p + origin) * scale - offset. Curves are
// cut finely enough to stay within `tolerance` px there (finer when a transform enlarges them after).
struct PathPlacement {
    Offset origin;
    float scale = 1;
    Offset offset;
    float tolerance = kTolerance;
    Offset at(float x, float y) const { return {(x + origin.x) * scale - offset.x, (y + origin.y) * scale - offset.y}; }
};

// The path's contours as polylines in the target's pixels.
std::vector<Contour> flattenPath(const std::vector<PathCommand>& commands, const PathPlacement& place);

// The outline a stroke `width` px wide covers along `contours` (closed polygons, filled by the
// non-zero rule): miter joins beyond `miterLimit` (the miter's length over half the width, as Skia
// and FreeType have it) become bevels; a contour that goes nowhere is a dot for round and square
// caps and nothing for butt ones, as in Skia. Round joins and caps stay within `tolerance`.
std::vector<Contour> strokeOutline(const std::vector<Contour>& contours, float width, StrokeCap cap, StrokeJoin join, float miterLimit,
                                   float tolerance = kTolerance);

// A projective transform of the plane, (x, y, 1) to (x', y', w) with x' / w and y' / w the point,
// row by row: what a GraphicsContext's transforms make of its pixels.
struct Projective {
    float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};

    static Projective translation(float x, float y) { return {{1, 0, x, 0, 1, y, 0, 0, 1}}; }
    static Projective scaling(float x, float y) { return {{x, 0, 0, 0, y, 0, 0, 0, 1}}; }
    // Matrix's plane part (its z row and column left out, as Android leaves them out).
    static Projective of(const Matrix& matrix);
    bool identity() const;
    // Only scaled and moved: axis-aligned rects stay so.
    bool axisAligned() const { return m[1] == 0 && m[3] == 0 && m[6] == 0 && m[7] == 0; }
    bool affine() const { return m[6] == 0 && m[7] == 0; }
    // Where (x, y) goes, and its w (the point is behind the eye where that isn't positive).
    Offset apply(float x, float y, float* w = nullptr) const;
    Projective inverse() const;  // the identity if there is none
    // How much it enlarges lengths at (x, y): the most and the least, in any direction.
    float maxScale(float x, float y) const;
    float minScale(float x, float y) const;
};
Projective operator*(const Projective& a, const Projective& b);  // b, then a

// Polygons (each closed) mapped by `m`, cut where they pass behind the eye.
std::vector<Contour> project(const std::vector<Contour>& polygons, const Projective& m);

// The edges of closed polygons: x0, y0, x1, y1 each, none horizontal.
struct Edge {
    float x0, y0, x1, y1;
};
void appendEdges(const std::vector<Contour>& contours, std::vector<Edge>& out);

} // namespace kompoz::gpu
