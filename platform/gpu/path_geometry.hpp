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

// Where a path point (dp, from `origin`) is in the target: (p + origin) * scale - offset.
struct PathPlacement {
    Offset origin;
    float scale = 1;
    Offset offset;
    Offset at(float x, float y) const { return {(x + origin.x) * scale - offset.x, (y + origin.y) * scale - offset.y}; }
};

// The path's contours as polylines in the target's pixels.
std::vector<Contour> flattenPath(const std::vector<PathCommand>& commands, const PathPlacement& place);

// The outline a stroke `width` px wide covers along `contours` (closed polygons, filled by the
// non-zero rule): miter joins beyond `miterLimit` (the miter's length over half the width, as Skia
// and FreeType have it) become bevels; a contour that goes nowhere is a dot for round and square
// caps and nothing for butt ones, as in Skia.
std::vector<Contour> strokeOutline(const std::vector<Contour>& contours, float width, StrokeCap cap, StrokeJoin join, float miterLimit);

// The edges of closed polygons: x0, y0, x1, y1 each, none horizontal.
struct Edge {
    float x0, y0, x1, y1;
};
void appendEdges(const std::vector<Contour>& contours, std::vector<Edge>& out);

} // namespace kompoz::gpu
