#include <kompoz/animation.hpp>
#include <kompoz/host.hpp>
#include <kompoz/material.hpp>
#include <kompoz/widgets.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <vector>

namespace kompoz {

namespace {

constexpr float kPi = std::numbers::pi_v<float>;

// The wave an indicator follows: `amplitude` dp out and back, `wavelength` dp long, moved
// `shift` dp towards the end.
struct Wave {
    float amplitude = 0, wavelength = 1, shift = 0;
    float at(float distance) const { return amplitude == 0 ? 0 : amplitude * std::sin(2 * kPi * (distance - shift) / wavelength); }
    // How far apart the points of a drawn stretch are.
    float step() const { return amplitude == 0 ? kInfinity : std::min(1.f, wavelength / 16); }
};

// The line from `from` to `to` dp along a linear indicator `width` wide, wavy around `y`;
// a right-to-left one runs from the right.
Path wavyLine(float from, float to, float y, const Wave& wave, float width, bool rtl) {
    Path path;
    int steps = std::max(1, int(std::ceil((to - from) / wave.step())));
    for (int i = 0; i <= steps; ++i) {
        float d = from + (to - from) * float(i) / float(steps);
        float x = rtl ? width - d : d, py = y + wave.at(d);
        i == 0 ? path.moveTo(x, py) : path.lineTo(x, py);
    }
    return path;
}

// The arc of the circle around `center` from `start` for `sweep` radians clockwise (0 = 3
// o'clock), wavy outwards and inwards; a whole turn is closed.
Path wavyArc(Offset center, float radius, float start, float sweep, const Wave& wave) {
    Path path;
    float steps = wave.amplitude == 0 ? sweep / (kPi / 60) : radius * sweep / wave.step();
    int n = std::max(2, int(std::ceil(steps)));
    for (int i = 0; i <= n; ++i) {
        float a = start + sweep * float(i) / float(n);
        float r = radius + wave.at(radius * (a + kPi / 2));  // the wave counts from 12 o'clock
        float x = center.x + r * std::cos(a), y = center.y + r * std::sin(a);
        i == 0 ? path.moveTo(x, y) : path.lineTo(x, y);
    }
    if (sweep >= 2 * kPi - 1e-4f) path.close();
    return path;
}

void stroke(GraphicsContext& canvas, const Path& path, const Rect& b, Color color, float width) {
    canvas.drawPath(path, {b.x, b.y}, Brush::solid(color), b, PathStroke{width, StrokeCap::Round, StrokeJoin::Round});
}

void progressNode(const CallSite& site, const Modifier& modifier, Size size, DrawContent draw, std::string semantics) {
    detail::Composer::require().emit(detail::keyOf(site), [&](LayoutNode& node) {
        node.modifier = modifier;
        node.measurePolicy = [size](LayoutNode&, const Constraints& c) { return c.constrain(size); };
        node.drawContent = std::move(draw);
        node.semanticsText = std::move(semantics);
    }, {});
}

struct Resolved {
    Color color, track;
    float wavelength, speed;
};

Resolved resolve(const WavyProgressIndicatorOptions& o, float wavelength) {
    const ColorScheme& cs = colorScheme();
    float w = std::max(1.f, o.wavelength.value_or(wavelength));
    return {o.color.value_or(cs.primary), o.trackColor.value_or(cs.secondaryContainer), w, std::max(0.f, o.waveSpeed.value_or(w))};
}

// How far the wave has moved (dp) as it is drawn, going while `moving`.
using WaveShift = std::function<float(const LayoutNode&)>;
WaveShift waveShift(bool moving, const Resolved& r) {
    if (!moving || r.speed == 0) return [](const LayoutNode&) { return 0.f; };
    auto clock = detail::rememberDrawClock();
    AnimationSpec spec = infiniteRepeatable(tween(1000 * r.wavelength / r.speed, 0, LinearEasing));
    return [clock, spec, wavelength = r.wavelength](const LayoutNode& node) { return wavelength * clock.progress(node, spec); };
}

// The determinate wave's height (0..1), animating to what `amplitude` asks for this progress: in
// 500 ms, rising with the standard easing, falling with the emphasized accelerating one (Compose's
// Increasing/DecreasingAmplitudeAnimationSpec).
AnimatedState<float> determinateAmplitude(float progress, const WavyProgressIndicatorOptions& o) {  // (read as it is drawn)
    float target = std::clamp(o.amplitude ? o.amplitude(progress) : WavyProgressIndicatorDefaults::indicatorAmplitude(progress), 0.f, 1.f);
    auto& previous = remember([target] { return std::pair{target, true}; });  // the last target, and whether it rose to it
    if (target != previous.first) previous = {target, target > previous.first};
    constexpr Easing standard{0.2f, 0, 0, 1}, emphasizedAccelerate{0.3f, 0, 0.8f, 0.15f};
    return animateFloatAsState(target, tween(500, 0, previous.second ? standard : emphasizedAccelerate));
}

std::string percent(float p) { return std::to_string(int(std::lround(p * 100))) + "%"; }

// Draws the stretches [from, to] (dp) of a linear indicator, and its track in the gaps
// between them.
struct LinearLook {
    std::vector<std::pair<float, float>> lines;  // in order, apart
    float amplitude;                              // dp
    float shift;
    float stopSize;
};

void drawLinear(GraphicsContext& canvas, const Rect& b, bool rtl, const LinearLook& look, const Resolved& r,
                const WavyProgressIndicatorOptions& o) {
    float width = b.width, y = b.height / 2;
    float s = o.strokeWidth, ts = o.trackStrokeWidth;
    Wave wave{look.amplitude, r.wavelength, look.shift};
    // A line shorter than its round ends is a dot as wide as they are, inside the bounds.
    auto lines = look.lines;
    for (auto& [from, to] : lines)
        if (to - from < s) {
            float c = std::clamp((from + to) / 2, s / 2, std::max(s / 2, width - s / 2));
            from = c - s / 2, to = c + s / 2;
        }
    // The track: what the lines leave, a gap away from them.
    float at = 0;
    auto track = [&](float from, float to, bool afterLine, bool beforeLine) {
        from += afterLine ? o.gapSize : 0;
        to -= beforeLine ? o.gapSize : 0;
        if (to - from < ts) return false;
        stroke(canvas, wavyLine(from + ts / 2, to - ts / 2, y, Wave{}, width, rtl), b, r.track, ts);
        return true;
    };
    for (size_t i = 0; i < lines.size(); ++i) {
        track(at, lines[i].first, i > 0, true);
        at = lines[i].second;
    }
    bool drewEnd = track(at, width, !lines.empty(), false);
    if (drewEnd && look.stopSize > 0) {
        float d = look.stopSize, c = width - std::max(d, ts) / 2;
        float x = rtl ? width - c : c;
        canvas.fillRoundRect({b.x + x - d / 2, b.y + y - d / 2, d, d}, d / 2, r.color);
    }
    for (auto [from, to] : lines)  // the round ends stay inside the stretch
        stroke(canvas, wavyLine(from + s / 2, to - s / 2, y, wave, width, rtl), b, r.color, s);
}

// Compose's indeterminate linear motion: two lines chase each other across in 1750 ms.
std::vector<std::pair<float, float>> indeterminateLines(float ms) {
    auto part = [ms](float delay, float duration, Easing easing) { return easing(std::clamp((ms - delay) / duration, 0.f, 1.f)); };
    std::pair<float, float> first{part(333, 850, {0.4f, 0, 1, 1}), part(0, 750, {0.2f, 0, 0.8f, 1})};
    std::pair<float, float> second{part(1267, 533, {0.1f, 0, 0.45f, 1}), part(1000, 567, {0, 0, 0.65f, 1})};
    std::vector<std::pair<float, float>> lines;
    for (auto line : {second, first})  // the second starts behind the first
        if (line.second > line.first) lines.push_back(line);
    return lines;
}

// Draws a circular indicator's arc from `start` for `sweep` radians and the track round the
// rest of the circle.
void drawCircular(GraphicsContext& canvas, const Rect& b, float start, float sweep, float amplitude, float shift,
                  const Resolved& r, const WavyProgressIndicatorOptions& o) {
    float s = o.strokeWidth, ts = o.trackStrokeWidth;
    float side = std::min(b.width, b.height);
    float radius = std::max(1.f, (side - std::max(s, ts)) / 2 - WavyProgressIndicatorDefaults::CircularAmplitude);
    Offset center{b.width / 2, b.height / 2};
    // A whole number of waves goes round, so they meet where the circle closes.
    float turns = std::max(3.f, std::round(2 * kPi * radius / r.wavelength));
    Wave wave{amplitude, 2 * kPi * radius / turns, shift};
    if (sweep >= 2 * kPi) {
        stroke(canvas, wavyArc(center, radius, start, 2 * kPi, wave), b, r.color, s);
        return;
    }
    float cap = s / 2 / radius, trackCap = ts / 2 / radius, gap = o.gapSize / radius;
    if (sweep <= 0)
        stroke(canvas, wavyArc(center, radius, 0, 2 * kPi, Wave{}), b, r.track, ts);
    else if (float rest = 2 * kPi - sweep - 2 * (gap + trackCap); rest > 0)
        stroke(canvas, wavyArc(center, radius, start + sweep + gap + trackCap, rest, Wave{}), b, r.track, ts);
    if (sweep > 0) {
        float inset = std::min(cap, sweep / 2);
        stroke(canvas, wavyArc(center, radius, start + inset, sweep - 2 * inset, wave), b, r.color, s);
    }
}

} // namespace

void LinearWavyProgressIndicator(float progress, const Modifier& modifier, const WavyProgressIndicatorOptions& options, CallSite site) {
    composable(site, progress, modifier, options, [=] {
        Resolved r = resolve(options, WavyProgressIndicatorDefaults::LinearDeterminateWavelength);
        float p = std::clamp(progress, 0.f, 1.f);
        auto waves = determinateAmplitude(p, options);
        WaveShift shift = waveShift(waves.targetValue() > 0 || waves.isRunning(), r);
        progressNode(
            CallSite::current(), modifier.semantics({.progressBarRangeInfo = ProgressBarRangeInfo{p, 0, 1}}), {WavyProgressIndicatorDefaults::LinearContainerWidth, WavyProgressIndicatorDefaults::LinearContainerHeight},
            [=](GraphicsContext& canvas, const Rect& b, const LayoutNode& node) {
                float amplitude = waves.value();
                LinearLook look{{}, amplitude * std::max(0.f, (b.height - options.strokeWidth) / 2), shift(node), options.stopSize};
                if (p > 0) look.lines.push_back({0.f, b.width * p});
                drawLinear(canvas, b, node.rtl, look, r, options);
            },
            percent(p));
    });
}

void LinearWavyProgressIndicator(const Modifier& modifier, const WavyProgressIndicatorOptions& options, CallSite site) {
    composable(site, modifier, options, [=] {
        Resolved r = resolve(options, WavyProgressIndicatorDefaults::LinearIndeterminateWavelength);
        float amplitude = std::clamp(options.indeterminateAmplitude, 0.f, 1.f);
        WaveShift shift = waveShift(amplitude > 0, r);
        auto clock = detail::rememberDrawClock();  // read as it draws
        progressNode(
            CallSite::current(), modifier.semantics({.progressBarRangeInfo = ProgressBarRangeInfo::Indeterminate}), {WavyProgressIndicatorDefaults::LinearContainerWidth, WavyProgressIndicatorDefaults::LinearContainerHeight},
            [=](GraphicsContext& canvas, const Rect& b, const LayoutNode& node) {
                LinearLook look{{}, amplitude * std::max(0.f, (b.height - options.strokeWidth) / 2), shift(node), 0};
                float ms = 1750 * clock.progress(node, infiniteRepeatable(tween(1750, 0, LinearEasing)));
                for (auto [from, to] : indeterminateLines(ms)) look.lines.push_back({from * b.width, to * b.width});
                drawLinear(canvas, b, node.rtl, look, r, options);
            },
            "loading");
    });
}

void CircularWavyProgressIndicator(float progress, const Modifier& modifier, const WavyProgressIndicatorOptions& options, CallSite site) {
    composable(site, progress, modifier, options, [=] {
        Resolved r = resolve(options, WavyProgressIndicatorDefaults::CircularWavelength);
        float p = std::clamp(progress, 0.f, 1.f);
        auto waves = determinateAmplitude(p, options);
        WaveShift shift = waveShift(waves.targetValue() > 0 || waves.isRunning(), r);
        float size = WavyProgressIndicatorDefaults::CircularContainerSize;
        progressNode(
            CallSite::current(), modifier.semantics({.progressBarRangeInfo = ProgressBarRangeInfo{p, 0, 1}}), {size, size},
            [=](GraphicsContext& canvas, const Rect& b, const LayoutNode& node) {
                drawCircular(canvas, b, -kPi / 2, 2 * kPi * p, waves.value() * WavyProgressIndicatorDefaults::CircularAmplitude, shift(node), r, options);
            },
            percent(p));
    });
}

void CircularWavyProgressIndicator(const Modifier& modifier, const WavyProgressIndicatorOptions& options, CallSite site) {
    composable(site, modifier, options, [=] {
        Resolved r = resolve(options, WavyProgressIndicatorDefaults::CircularWavelength);
        float amplitude = std::clamp(options.indeterminateAmplitude, 0.f, 1.f);
        WaveShift shift = waveShift(amplitude > 0, r);
        auto clock = detail::rememberDrawClock();  // read as it draws
        float size = WavyProgressIndicatorDefaults::CircularContainerSize;
        progressNode(
            CallSite::current(), modifier.semantics({.progressBarRangeInfo = ProgressBarRangeInfo::Indeterminate}), {size, size},
            [=](GraphicsContext& canvas, const Rect& b, const LayoutNode& node) {
                // The arc turns and grows and shrinks, as CircularProgressIndicator's does.
                float rotation = 360 * clock.progress(node, infiniteRepeatable(tween(1600, 0, LinearEasing)));
                float phase = clock.progress(node, infiniteRepeatable(tween(1333, 0, LinearEasing)));
                float sweep = (20 + 250 * (0.5f - 0.5f * std::cos(phase * 2 * kPi))) * kPi / 180;
                float start = (rotation + phase * 360 - 90) * kPi / 180;
                drawCircular(canvas, b, start, sweep, amplitude * WavyProgressIndicatorDefaults::CircularAmplitude, shift(node), r, options);
            },
            "loading");
    });
}

} // namespace kompoz
