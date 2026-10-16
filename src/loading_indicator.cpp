#include <kompoz/animation.hpp>
#include <kompoz/host.hpp>
#include <kompoz/material.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>

namespace kompoz {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// The shapes as how far their outline is from the center at each angle (0 = 3 o'clock,
// clockwise), the farthest point at 1: close to Material's shapes, and two of them morph by
// blending the distances angle by angle.
using Outline = float (*)(float angle);

// A shape with `lobes` soft bumps, `depth` of the radius deep between them.
float lobed(float angle, int lobes, float depth) { return 1 - depth * (1 - std::cos(float(lobes) * angle)) / 2; }

// A regular polygon with `sides` (a corner at the top), rounded by blending it with the circle
// halfway between its corners and its sides.
float polygon(float angle, int sides, float roundness) {
    float wedge = 2 * kPi / float(sides);
    float a = std::fmod(angle + kPi / 2 + 4 * kPi, wedge) - wedge / 2;
    float edge = std::cos(wedge / 2) / std::cos(a);  // the flat polygon, corners at 1
    return (1 - roundness) * edge + roundness * (1 + std::cos(wedge / 2)) / 2;
}

// A capsule lying across, `ratio` as tall as wide.
float capsule(float angle, float ratio) {
    float r = ratio, a = 1 - r;  // radius of its ends, half the length of its straight part
    float c = std::abs(std::cos(angle)), s = std::abs(std::sin(angle));
    if (s > 1e-4f && r / s * c <= a) return r / s;  // the long sides
    // The round ends: |d (c, s) - (a, 0)| = r.
    float b = -2 * a * c, k = a * a - r * r;
    return (-b + std::sqrt(std::max(0.f, b * b - 4 * k))) / 2;
}

float circle(float) { return 1; }
float softBurst(float angle) { return lobed(angle, 10, 0.16f); }
float cookie9(float angle) { return lobed(angle, 9, 0.12f); }
float pentagon(float angle) { return polygon(angle, 5, 0.35f); }
float pill(float angle) { return capsule(angle + kPi / 4, 0.62f); }  // aslant
float sunny(float angle) { return lobed(angle, 8, 0.14f); }
float cookie4(float angle) { return lobed(angle, 4, 0.2f); }
float oval(float angle) {
    float a = 1, b = 0.72f, t = angle - kPi / 4;  // aslant too
    return a * b / std::sqrt(b * std::cos(t) * b * std::cos(t) + a * std::sin(t) * a * std::sin(t));
}

constexpr std::array<Outline, 7> kIndeterminate{softBurst, cookie9, pentagon, pill, sunny, cookie4, oval};
constexpr int kPoints = 120;

// Each outline's farthest point (sampled as it is drawn), so all of them fill the same box.
float reach(Outline outline) {
    float most = 0;
    for (int i = 0; i < kPoints; ++i) most = std::max(most, outline(2 * kPi * float(i) / kPoints));
    return most;
}

// `t` of the way from `a` to `b`, turned by `turn` radians, filling a circle of `radius` around
// `center`.
Path morph(Outline a, Outline b, float t, float turn, Offset center, float radius) {
    float ra = reach(a), rb = reach(b);
    Path path;
    for (int i = 0; i < kPoints; ++i) {
        float angle = 2 * kPi * float(i) / kPoints;
        float r = radius * ((1 - t) * a(angle) / ra + t * b(angle) / rb);
        float x = center.x + r * std::cos(angle + turn), y = center.y + r * std::sin(angle + turn);
        i == 0 ? path.moveTo(x, y) : path.lineTo(x, y);
    }
    path.close();
    return path;
}

void indicatorNode(const Modifier& modifier, std::optional<Color> container, Color color,
                   std::function<Path(Offset, float, const LayoutNode&)> shape, std::string semantics, const CallSite& site) {
    detail::Composer::require().emit(detail::keyOf(site), [&](LayoutNode& node) {
        node.modifier = modifier;
        node.measurePolicy = [](LayoutNode&, const Constraints& c) {
            return c.constrain({LoadingIndicatorDefaults::ContainerSize, LoadingIndicatorDefaults::ContainerSize});
        };
        node.drawContent = [container, color, shape](GraphicsContext& canvas, const Rect& b, const LayoutNode& node) {
            float side = std::min(b.width, b.height);
            if (container) canvas.fillRoundRect(b, side / 2, *container);
            float radius = side * LoadingIndicatorDefaults::IndicatorSize / LoadingIndicatorDefaults::ContainerSize / 2;
            canvas.drawPath(shape({b.width / 2, b.height / 2}, radius, node), {b.x, b.y}, Brush::solid(color), b, PathStroke{});
        };
        node.semanticsText = std::move(semantics);
    }, {});
}

// The shape now: the morph from one to the next takes MorphMillis (springing a little past it),
// each turning the shape a quarter more, on top of a steady turn.
void indeterminate(const Modifier& modifier, std::optional<Color> container, Color color, const CallSite& site) {
    // Four rounds of the shapes before it repeats: their quarter turns add up to whole ones.
    constexpr int kSteps = 4 * int(kIndeterminate.size());
    auto clock = detail::rememberDrawClock();  // read as it draws: nothing composed or laid out as it morphs
    indicatorNode(modifier, container, color, [clock](Offset c, float r, const LayoutNode& node) {
        float cycle = LoadingIndicatorDefaults::MorphMillis * float(kSteps);
        float ms = cycle * clock.progress(node, infiniteRepeatable(tween(cycle, 0, LinearEasing)));
        float spin = 2 * kPi * clock.progress(node, infiniteRepeatable(tween(LoadingIndicatorDefaults::TurnMillis, 0, LinearEasing)));
        int step = std::min(int(ms / LoadingIndicatorDefaults::MorphMillis), kSteps - 1);
        float local = ms / LoadingIndicatorDefaults::MorphMillis - float(step);
        float t = Easing{0.3f, 0, 0.1f, 1.25f}(std::clamp(local, 0.f, 1.f));  // a little past, then back
        size_t n = kIndeterminate.size();
        Outline from = kIndeterminate[size_t(step) % n], to = kIndeterminate[(size_t(step) + 1) % n];
        float turn = spin + (float(step) + t) * kPi / 2;
        return morph(from, to, std::clamp(t, 0.f, 1.f), turn, c, r);
    }, "loading", site);
}

void determinate(float progress, const Modifier& modifier, std::optional<Color> container, Color color, const CallSite& site) {
    float p = std::clamp(progress, 0.f, 1.f);
    indicatorNode(modifier, container, color, [p](Offset c, float r, const LayoutNode&) { return morph(circle, softBurst, p, p * kPi, c, r); },
                  std::to_string(int(std::lround(p * 100))) + "%", site);
}

} // namespace

void LoadingIndicator(const Modifier& modifier, std::optional<Color> color, CallSite site) {
    composable(site, modifier, color, [=] { indeterminate(modifier, std::nullopt, color.value_or(colorScheme().primary), CallSite::current()); });
}

void LoadingIndicator(float progress, const Modifier& modifier, std::optional<Color> color, CallSite site) {
    composable(site, progress, modifier, color,
               [=] { determinate(progress, modifier, std::nullopt, color.value_or(colorScheme().primary), CallSite::current()); });
}

void ContainedLoadingIndicator(const Modifier& modifier, std::optional<Color> indicatorColor, std::optional<Color> containerColor,
                               CallSite site) {
    composable(site, modifier, indicatorColor, containerColor, [=] {
        const ColorScheme& cs = colorScheme();
        indeterminate(modifier, containerColor.value_or(cs.primaryContainer), indicatorColor.value_or(cs.onPrimaryContainer),
                      CallSite::current());
    });
}

void ContainedLoadingIndicator(float progress, const Modifier& modifier, std::optional<Color> indicatorColor,
                               std::optional<Color> containerColor, CallSite site) {
    composable(site, progress, modifier, indicatorColor, containerColor, [=] {
        const ColorScheme& cs = colorScheme();
        determinate(progress, modifier, containerColor.value_or(cs.primaryContainer), indicatorColor.value_or(cs.onPrimaryContainer),
                    CallSite::current());
    });
}

} // namespace kompoz
