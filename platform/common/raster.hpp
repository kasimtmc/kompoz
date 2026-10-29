#pragma once

// FreeType helpers the font system rasterizes with: a path's fill as a FreeType outline, a
// Gaussian blur of coverage, and a coverage canvas to render outlines into.

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <kompoz/graphics.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace kompoz::platform {

// A path's (or a vector's) fill as a FreeType outline over `points`, `tags` and `contours` (which
// must outlive it), each point at pt(x, y); false if it has none (or too many).
// `commands` with each conic as quadratics within a tenth of a pixel of it where `pt` (to 26.6
// px) puts them: FreeType has no conics.
template <class Pt>
std::vector<PathCommand> withoutConics(const std::vector<PathCommand>& commands, const Pt& pt) {
    if (std::none_of(commands.begin(), commands.end(), [](const PathCommand& c) { return c.verb == PathCommand::Verb::Conic; })) return commands;
    FT_Vector o = pt(0, 0), x = pt(1, 0), y = pt(0, 1);
    float scale = std::max(std::hypot(float(x.x - o.x), float(x.y - o.y)), std::hypot(float(y.x - o.x), float(y.y - o.y))) / 64;
    std::vector<PathCommand> out;
    for (PathIterator it(Path::fromCommands(commands), ConicEvaluation::AsQuadratics, 0.1f / std::max(scale, 1e-3f)); it.hasNext();) {
        PathSegment s = it.next();
        const auto& p = s.points;
        switch (s.type) {
        case PathSegment::Type::Move: out.push_back({PathCommand::Verb::Move, 0, 0, 0, 0, p[0].x, p[0].y}); break;
        case PathSegment::Type::Line: out.push_back({PathCommand::Verb::Line, 0, 0, 0, 0, p[1].x, p[1].y}); break;
        case PathSegment::Type::Quadratic: out.push_back({PathCommand::Verb::Quad, p[1].x, p[1].y, 0, 0, p[2].x, p[2].y}); break;
        case PathSegment::Type::Cubic: out.push_back({PathCommand::Verb::Cubic, p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y}); break;
        case PathSegment::Type::Close: out.push_back({PathCommand::Verb::Close}); break;
        default: break;
        }
    }
    return out;
}

template <class Pt, class Tag, class ContourEnd>
bool fillOutline(const std::vector<PathCommand>& given, bool evenOdd, const Pt& pt, std::vector<FT_Vector>& points,
                 std::vector<Tag>& tags, std::vector<ContourEnd>& contours, FT_Outline& outline) {
    std::vector<PathCommand> commands = withoutConics(given, pt);
    auto add = [&](float x, float y, int tag) {
        points.push_back(pt(x, y));
        tags.push_back(Tag(tag));
    };
    auto endContour = [&] {
        if (!points.empty() && (contours.empty() || contours.back() != ContourEnd(points.size() - 1)))
            contours.push_back(ContourEnd(points.size() - 1));
    };
    for (const PathCommand& c : commands) {
        switch (c.verb) {
        case PathCommand::Verb::Move: endContour(); add(c.x, c.y, FT_CURVE_TAG_ON); break;
        case PathCommand::Verb::Line: add(c.x, c.y, FT_CURVE_TAG_ON); break;
        case PathCommand::Verb::Quad:
            add(c.x1, c.y1, FT_CURVE_TAG_CONIC);
            add(c.x, c.y, FT_CURVE_TAG_ON);
            break;
        case PathCommand::Verb::Cubic:
            add(c.x1, c.y1, FT_CURVE_TAG_CUBIC);
            add(c.x2, c.y2, FT_CURVE_TAG_CUBIC);
            add(c.x, c.y, FT_CURVE_TAG_ON);
            break;
        case PathCommand::Verb::Close: endContour(); break;
        default: break;  // (no conics left)
        }
    }
    endContour();
    if (points.empty() || points.size() >= 32768) return false;
    outline.n_points = decltype(outline.n_points)(points.size());
    outline.n_contours = decltype(outline.n_contours)(contours.size());
    outline.points = points.data();
    outline.tags = tags.data();
    outline.contours = contours.data();
    outline.flags = evenOdd ? FT_OUTLINE_EVEN_ODD_FILL : FT_OUTLINE_NONE;  // otherwise non-zero winding, like SVG and Compose
    return true;
}

// A Gaussian blur of coverage (three box blurs each way; beyond the canvas as at its edge).
inline void blurCoverage(std::vector<float>& a, int cw, int ch, float sigma) {
    std::vector<float> t(a.size());
    float ideal = std::sqrt(12 * sigma * sigma / 3 + 1);
    int wl = int(std::floor(ideal));
    if (wl % 2 == 0) --wl;
    int m = int(std::lround((12 * sigma * sigma - 3.f * float(wl * wl) - 12.f * float(wl) - 9) / (-4.f * float(wl) - 4)));
    for (int pass = 0; pass < 3; ++pass) {
        int k = ((pass < m ? wl : wl + 2) - 1) / 2;
        float n = float(2 * k + 1);
        auto at = [&](const std::vector<float>& v, int x, int y) {
            return v[size_t(std::clamp(y, 0, ch - 1)) * size_t(cw) + size_t(std::clamp(x, 0, cw - 1))];
        };
        for (int y = 0; y < ch; ++y) {
            float sum = 0;
            for (int x = -k; x <= k; ++x) sum += at(a, x, y);
            for (int x = 0; x < cw; ++x) {
                t[size_t(y) * size_t(cw) + size_t(x)] = sum / n;
                sum += at(a, x + k + 1, y) - at(a, x - k, y);
            }
        }
        for (int x = 0; x < cw; ++x) {
            float sum = 0;
            for (int y = -k; y <= k; ++y) sum += at(t, x, y);
            for (int y = 0; y < ch; ++y) {
                a[size_t(y) * size_t(cw) + size_t(x)] = sum / n;
                sum += at(t, x, y + k + 1) - at(t, x, y - k);
            }
        }
    }
}

// An 8-bit coverage canvas of width x height over `gray` (made that big and cleared), for
// FT_Outline_Get_Bitmap.
inline FT_Bitmap grayCanvas(std::vector<unsigned char>& gray, int width, int height) {
    gray.assign(size_t(width) * size_t(height), 0);
    FT_Bitmap bitmap{};
    bitmap.rows = unsigned(height);
    bitmap.width = unsigned(width);
    bitmap.pitch = width;
    bitmap.buffer = gray.data();
    bitmap.num_grays = 256;
    bitmap.pixel_mode = FT_PIXEL_MODE_GRAY;
    return bitmap;
}

} // namespace kompoz::platform
