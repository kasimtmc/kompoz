#pragma once

// FreeType helpers the SDL3 backend's rasterizers share: a path's fill as a FreeType outline, a
// Gaussian blur of coverage, and a coverage canvas to render outlines into.

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H

#include <kompoz/graphics.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

namespace kompoz::sdl {

// A path's (or a vector's) fill as a FreeType outline over `points`, `tags` and `contours` (which
// must outlive it), each point at pt(x, y); false if it has none (or too many).
template <class Pt, class Tag, class ContourEnd>
bool fillOutline(const std::vector<PathCommand>& commands, bool evenOdd, const Pt& pt, std::vector<FT_Vector>& points,
                 std::vector<Tag>& tags, std::vector<ContourEnd>& contours, FT_Outline& outline) {
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

} // namespace kompoz::sdl
