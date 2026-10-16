// Material 3 Expressive's shapes (Compose's MaterialShapes) and morphs between them.
//
//   Box(Modifier().size(96).clip(toShape(MaterialShapes::Cookie9Sided())), ...);
//   Box(Modifier().size(48).background(cs.primary, toShape(MaterialShapes::Sunny())));
//
//   auto morph = remember([] { return std::make_shared<Morph>(MaterialShapes::Cookie9Sided(), MaterialShapes::Clover4Leaf()); });
//   float t = animateFloatAsState(on ? 1.f : 0.f);
//   Box(Modifier().size(96).background(cs.primaryContainer, morph->toShape(t)));
#pragma once

#include "graphics.hpp"

#include <array>
#include <memory>
#include <vector>

namespace kompoz {

// The outline as a Shape (Compose's RoundedPolygon.toShape()).
inline Shape toShape(const ShapeOutline& outline) { return OutlineShape(outline); }

// A polygon with rounded corners (Compose's RoundedPolygon): its corners (in a unit square,
// clockwise) and how round each is (a radius in the same units; a corner gives at most half of
// either edge to its rounding). Fitted to the unit square, keeping its proportions.
struct RoundedCorner {
    float x = 0, y = 0;
    float rounding = 0;
};
ShapeOutline roundedPolygon(const std::vector<RoundedCorner>& corners);
// A star of `points` tips around the middle, its notches `innerRadius` as far out as its tips
// (1: a polygon), each rounded by its own radius; the first tip at the top. Cookies, clovers,
// suns and bursts are stars.
ShapeOutline roundedStar(int points, float innerRadius, float tipRounding, float notchRounding);

// Material's 35 shapes (close to Compose's MaterialShapes), each made once.
namespace MaterialShapes {
const ShapeOutline& Circle();
const ShapeOutline& Square();
const ShapeOutline& Slanted();
const ShapeOutline& Arch();
const ShapeOutline& Fan();
const ShapeOutline& Arrow();
const ShapeOutline& SemiCircle();
const ShapeOutline& Oval();
const ShapeOutline& Pill();
const ShapeOutline& Triangle();
const ShapeOutline& Diamond();
const ShapeOutline& ClamShell();
const ShapeOutline& Pentagon();
const ShapeOutline& Gem();
const ShapeOutline& Sunny();
const ShapeOutline& VerySunny();
const ShapeOutline& Cookie4Sided();
const ShapeOutline& Cookie6Sided();
const ShapeOutline& Cookie7Sided();
const ShapeOutline& Cookie9Sided();
const ShapeOutline& Cookie12Sided();
const ShapeOutline& Ghostish();
const ShapeOutline& Clover4Leaf();
const ShapeOutline& Clover8Leaf();
const ShapeOutline& Burst();
const ShapeOutline& SoftBurst();
const ShapeOutline& Boom();
const ShapeOutline& SoftBoom();
const ShapeOutline& Flower();
const ShapeOutline& Puffy();
const ShapeOutline& PuffyDiamond();
const ShapeOutline& PixelCircle();
const ShapeOutline& PixelTriangle();
const ShapeOutline& Bun();
const ShapeOutline& Heart();

struct Named {
    const char* name;
    const ShapeOutline& (*outline)();
};
// All of them, in Material's order, with their names.
const std::array<Named, 35>& all();
} // namespace MaterialShapes

// A morph from one outline to another (Compose's Morph): how far each is from the middle,
// angle by angle, blended. Both should be star-shaped about the middle (Material's are). The
// shapes along the way come in 64 steps, each its own outline (made when first asked for), so
// a shape animating through them changes as a Shape does. Keep the morph alive while its shapes
// are in use (remember it).
class Morph {
public:
    Morph(const ShapeOutline& start, const ShapeOutline& end);
    const ShapeOutline& outlineAt(float progress);  // 0: the start, 1: the end
    Shape toShape(float progress) { return OutlineShape(outlineAt(progress)); }

    static constexpr int kSteps = 64;
    static constexpr int kSamples = 180;  // directions around the middle

private:
    std::array<float, kSamples> from_{}, to_{};
    std::array<std::unique_ptr<ShapeOutline>, kSteps + 1> steps_;
};

// How far `outline` reaches from the middle of the unit square in each of `n` directions (from 3
// o'clock, clockwise): where a ray from the middle leaves it.
std::vector<float> polarRadii(const ShapeOutline& outline, int n);

} // namespace kompoz
