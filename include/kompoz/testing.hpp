#pragma once

// Headless UI testing, in the spirit of Compose's createComposeRule():
//
//   ComposeTestRule rule([] { Counter(); });
//   rule.onNodeWithText("+").performClick();
//   rule.onNodeWithText("1").assertIsDisplayed();

#include "host.hpp"

#include <thread>
#include <functional>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace kompoz::testing {

// Deterministic text metrics: every code point is half an em wide (stretched by its scaleX, and
// its letter spacing more).
class FakeTextMeasurer final : public TextMeasurer {
public:
    float measureWidth(std::string_view text, const Font& font) override {
        size_t count = 0;
        for (size_t i = 0; i < text.size(); ++count) utf8::next(text, i);
        return float(count) * (font.size * 0.5f * font.scaleX + font.letterSpacing);
    }
    FontMetrics metrics(const Font& font) override { return {font.size * 0.8f, font.size * 0.2f, font.size * 1.25f}; }
    float scale = 1;  // the system font size: only line heights (Font::lineHeight) follow it here
    float fontScale() const override { return scale; }
};

// Records drawn text, so tests can check what actually reached the screen (where it lands:
// translations are applied; layer transforms and pushTransform's matrices aren't).
class RecordingCanvas final : public GraphicsContext {
public:
    std::vector<std::string> texts;
    std::vector<Font> fonts;   // of each drawn text
    std::vector<Color> colors;
    std::vector<Offset> baselines;  // where each text starts
    std::vector<bool> rtl;          // whether its paragraph goes right to left
    std::vector<uint64_t> vectors;  // ids of drawn ImageVectors
    std::vector<uint64_t> images;
    std::vector<ImageBitmap> drawnImages;  // each drawn image itself
    std::vector<Rect> imageSources, imageDestinations;
    int layers = 0, depth = 0, maxDepth = 0, transformedLayers = 0, gradients = 0;
    std::vector<Rect> layerBounds;  // each pushLayer's bounds, alpha and transform
    std::vector<float> layerAlphas;
    std::vector<LayerTransform> layerTransforms;
    std::vector<Rect> paths;  // bounds of drawn paths (absolute)
    std::vector<Rect> rects;  // filled (rounded) rects
    std::vector<Color> rectColors;
    std::vector<float> rectRadii;          // each filled rect's corner radius (the top left one)
    std::vector<CornerRadii> rectCorners;  // and all four
    std::vector<CornerRadii> strokeCorners;
    std::vector<CornerRadii> layerCorners;
    std::vector<Rect> clips;  // pushClip calls
    std::vector<Rect> strokes;  // stroked (rounded) rects: borders, focus rings
    std::vector<Color> strokeColors;
    std::string order;  // the fills as they came: 'r' a rect, 'p' a path (the nth of each in rects, paths)
    // What was pushed and popped, in order: a layer (L, l), a clip (C, c), a transform (T, t), a
    // translation (M, m), a color filter (F, f), a blend mode (B, b), anti-aliasing (A, a).
    std::string stack;
    std::vector<Offset> translations;  // each pushTranslation's
    Offset translation;                // in effect now (their sum)
    std::vector<Matrix> transforms;    // each pushTransform's
    int transformDepth = 0;            // pushTransform's in effect now
    std::vector<ColorFilter> filterStack;  // pushColorFilter's in effect, the innermost last
    std::vector<std::vector<ColorFilter>> imageFilters;  // each drawn image's (as filterStack was)
    // A color as the filters in effect make it (the innermost first): what a backend would show.
    Color filtered(Color c) const {
        for (auto it = filterStack.rbegin(); it != filterStack.rend(); ++it) c = it->apply(c);
        return c;
    }
    void pushColorFilter(const ColorFilter& filter) override {
        stack += 'F';
        filterStack.push_back(filter);
    }
    void popColorFilter() override {
        stack += 'f';
        if (!filterStack.empty()) filterStack.pop_back();
    }
    std::vector<BlendMode> blendStack;  // pushBlendMode's in effect
    std::vector<BlendMode> rectBlends;  // each filled rect's (and gradient's) blend mode
    std::vector<int> rectDepths;        // and how many layers it is drawn in
    std::vector<BlendMode> pathBlends;  // each path's
    BlendMode blend() const { return blendStack.empty() ? BlendMode::SrcOver : blendStack.back(); }
    std::vector<bool> antiAliasStack;  // pushAntiAlias's in effect
    std::vector<bool> pathAntiAliased;  // each path's (drawn anti-aliased)
    bool antiAliased() const { return antiAliasStack.empty() || antiAliasStack.back(); }
    void pushAntiAlias(bool on) override {
        stack += 'A';
        antiAliasStack.push_back(on);
    }
    void popAntiAlias() override {
        stack += 'a';
        if (!antiAliasStack.empty()) antiAliasStack.pop_back();
    }
    void pushBlendMode(BlendMode mode) override {
        stack += 'B';
        blendStack.push_back(mode);
    }
    void popBlendMode() override {
        stack += 'b';
        if (!blendStack.empty()) blendStack.pop_back();
    }
    void fillRoundRect(const Rect& r, const CornerRadii& radii, Color c) override {
        order += 'r';
        rectDepths.push_back(depth);
        rectBlends.push_back(blend());
        rects.push_back(moved(r));
        rectColors.push_back(filtered(c));
        rectRadii.push_back(radii.topLeft);
        rectCorners.push_back(radii);
    }
    void strokeRoundRect(const Rect& r, const CornerRadii& radii, float, Color c) override {
        strokes.push_back(moved(r));
        strokeColors.push_back(filtered(c));
        strokeCorners.push_back(radii);
    }
    std::vector<std::pair<Offset, Offset>> lines;  // drawn lines (underlines, strike-throughs, carets), where they land
    std::vector<Color> lineColors;
    void drawLine(Offset from, Offset to, float, Color c) override {
        lines.push_back({{from.x + translation.x, from.y + translation.y}, {to.x + translation.x, to.y + translation.y}});
        lineColors.push_back(c);
    }
    void drawText(std::string_view text, Offset baseline, const Font& font, Color color, bool rightToLeft = false) override {
        textOrder += 't';
        texts.emplace_back(text);
        fonts.push_back(font);
        colors.push_back(filtered(color));
        baselines.push_back({baseline.x + translation.x, baseline.y + translation.y});
        rtl.push_back(rightToLeft);
    }
    std::string textOrder;  // texts and their shadows as they came: 't' a text, 's' a shadow
    std::vector<std::string> shadowTexts;
    std::vector<Offset> shadowBaselines;
    std::vector<Color> shadowColors;
    std::vector<float> shadowBlurs;
    void drawTextShadow(std::string_view text, Offset baseline, const Font&, Color color, float blur, bool = false) override {
        textOrder += 's';
        shadowTexts.emplace_back(text);
        shadowBaselines.push_back({baseline.x + translation.x, baseline.y + translation.y});
        shadowColors.push_back(color);
        shadowBlurs.push_back(blur);
    }
    struct ShapeShadow {
        Rect rect;
        CornerRadii radii;
        Shadow shadow;
        bool inner = false;
        BlendMode blend = BlendMode::SrcOver;  // what it was drawn with
    };
    std::vector<ShapeShadow> shadows;  // drawShadow calls (dropShadow, innerShadow)
    struct PathShadow {
        Rect bounds;  // the outline's, where it is
        Shadow shadow;
        bool inner = false;
    };
    std::vector<PathShadow> pathShadows;  // drawShadowPath calls (an outline shape's)
    std::vector<Rect> brushedShadows;     // drawShapeShadow calls with a brush (not one color): where the shape is
    void drawShapeShadow(const Rect& rect, const Shape& shape, bool rtl, const Shadow& shadow, bool inner) override {
        if (shadow.brush && !shadow.brush->isSolid()) brushedShadows.push_back(moved(rect));
        GraphicsContext::drawShapeShadow(rect, shape, rtl, shadow, inner);
    }
    void drawShadowPath(const Path& outline, Offset origin, const Shadow& shadow, bool inner) override {
        Rect b = outline.bounds();
        pathShadows.push_back({moved({origin.x + b.x, origin.y + b.y, b.width, b.height}), shadow, inner});
    }
    void drawShadow(const Rect& rect, const CornerRadii& radii, const Shadow& shadow, bool inner) override {
        order += 's';
        shadows.push_back({moved(rect), radii, shadow, inner, blend()});
    }
    std::vector<Rect> brushAreas;  // each text painted with a brush (in texts too, in the color at the area's center)
    void drawTextBrush(std::string_view text, Offset baseline, const Font& font, const Brush& brush, const Rect& area,
                       bool rightToLeft = false) override {
        brushAreas.push_back(moved(area));
        drawText(text, baseline, font, brush.colorAt(area.center(), area), rightToLeft);
    }
    // Each vector drawn (the bitmap a painter drew it into, drawn: see detail::vectorDrawnInto):
    // its color (a one-color one's, its mask's paint as the filters in effect make it; one in its
    // own colors, white as they make it), faded by its alpha, and where it went.
    std::vector<Color> vectorColors;
    std::vector<Rect> vectorBounds;
    void vectorDrawn(const ImageBitmap& image, Color c, float alpha, const Rect& dst) {
        if (uint64_t v = detail::vectorDrawnInto(image)) {
            vectors.push_back(v);
            vectorColors.push_back(c.copy(c.a * alpha));
            vectorBounds.push_back(moved(dst));
        }
    }
    struct Arc {
        Rect oval;
        float start, sweep, strokeWidth;
        Color color;
    };
    std::vector<Arc> arcs;  // drawArc calls
    void drawArc(const Rect& oval, float start, float sweep, float strokeWidth, Color c, bool) override {
        arcs.push_back({moved(oval), start, sweep, strokeWidth, c});
    }
    std::vector<FilterQuality> imageQualities;  // how each image is sampled
    std::vector<float> imageAlphas;             // and how opaque
    std::vector<BlendMode> imageBlends;         // and its blend mode
    void drawImage(const ImageBitmap& image, const Rect& src, const Rect& dst, float alpha, FilterQuality quality) override {
        vectorDrawn(image, filtered(colors::White), alpha, dst);
        images.push_back(image.id());
        drawnImages.push_back(image);
        imageAlphas.push_back(alpha);
        imageBlends.push_back(blend());
        imageQualities.push_back(quality);
        imageFilters.push_back(filterStack);
        imageSources.push_back(src);
        imageDestinations.push_back(moved(dst));
    }
    // Each mask drawn (an Alpha8 image, drawImageMask: in images too): its paint (a brush's color
    // at its area's middle, as the filters in effect make it) and alpha.
    std::vector<Color> maskColors;
    std::vector<float> maskAlphas;
    void drawImageMask(const ImageBitmap& image, const Rect& src, const Rect& dst, const Brush& brush, const Rect& brushArea, float alpha,
                       FilterQuality quality) override {
        Color c = filtered(brush.colorAt(brushArea.center(), brushArea));
        vectorDrawn(image, c, alpha, dst);
        maskColors.push_back(c);
        maskAlphas.push_back(alpha);
        images.push_back(image.id());
        drawnImages.push_back(image);
        imageAlphas.push_back(alpha);
        imageBlends.push_back(blend());
        imageQualities.push_back(quality);
        imageFilters.push_back(filterStack);
        imageSources.push_back(src);
        imageDestinations.push_back(moved(dst));
    }
    void pushLayer(const Rect& bounds, const CornerRadii& radii, float alpha, const LayerTransform& t) override {
        layerEffects.push_back({});
        layerBounds.push_back(moved(bounds));
        layerCorners.push_back(radii);
        layerAlphas.push_back(alpha);
        layerTransforms.push_back(t);
        if (!t.identity()) ++transformedLayers;
        stack += 'L';
        ++layers;
        maxDepth = std::max(maxDepth, ++depth);
    }
    void popLayer() override {
        stack += 'l';
        --depth;
    }
    std::vector<Path> layerOutlines;  // each outline layer's outline (pushOutlineLayer), from its top left
    struct Elevated {
        Rect bounds;  // the outline's, where it is
        Outline outline;
        ElevationShadow shadow;
        float elevation() const { return shadow.plane[2]; }  // (flat)
    };
    std::vector<Elevated> elevationShadows;  // drawElevationShadow calls
    void drawElevationShadow(const Outline& outline, Offset origin, const ElevationShadow& shadow) override {
        order += 'e';
        Rect b = outline.bounds();
        elevationShadows.push_back({moved({b.x + origin.x, b.y + origin.y, b.width, b.height}), outline, shadow});
    }
    struct Backdrop {
        Rect bounds;
        RenderEffect effect;
        float alpha;
    };
    std::vector<Backdrop> backdrops;  // drawBackdrop calls
    void drawBackdrop(const Rect& bounds, const RenderEffect& effect, float alpha) override {
        order += 'b';
        backdrops.push_back({moved(bounds), effect, alpha});
    }
    std::vector<std::optional<RenderEffect>> layerEffects;  // each layer's effect (pushEffectLayer)
    void pushEffectLayer(const Rect& bounds, const RenderEffect& effect, float alpha) override {
        pushLayer(bounds, CornerRadii(), alpha, {});
        layerEffects.back() = effect;
    }
    void pushOutlineLayer(const Rect& bounds, const Path& outline, float alpha, const LayerTransform& t) override {
        layerOutlines.push_back(outline);
        pushLayer(bounds, CornerRadii(), alpha, t);
    }
    void pushClip(const Rect& r) override {
        stack += 'C';
        clips.push_back(moved(r));
    }
    // Each pushClipRoundRect's and pushClipPath's: its bounds (where it is), corners, path and op.
    struct ClipShape {
        Rect bounds;
        CornerRadii radii;
        std::optional<Path> path;  // (around the origin it was clipped at)
        ClipOp op;
    };
    std::vector<ClipShape> clipShapes;
    void pushClipRoundRect(const Rect& r, const CornerRadii& radii, ClipOp op) override {
        stack += 'C';
        clipShapes.push_back({moved(r), radii, {}, op});
    }
    void pushClipPath(const Path& path, Offset origin, ClipOp op) override {
        stack += 'C';
        Rect b = path.bounds();
        clipShapes.push_back({moved({b.x + origin.x, b.y + origin.y, b.width, b.height}), {}, path, op});
    }
    void popClip() override { stack += 'c'; }
    void pushTranslation(Offset by) override {
        stack += 'M';
        translations.push_back(by);
        stack_.push_back(translation);
        translation = {translation.x + by.x, translation.y + by.y};
    }
    void popTranslation() override {
        stack += 'm';
        if (stack_.empty()) return;
        translation = stack_.back();
        stack_.pop_back();
    }
    void pushTransform(const Matrix& matrix) override {
        stack += 'T';
        transforms.push_back(matrix);
        ++transformDepth;
    }
    void popTransform() override {
        stack += 't';
        --transformDepth;
    }
    std::optional<Rect> visible;  // set: what lies outside isn't drawn (as a real screen culls it)
    bool quickReject(const Rect& rect) const override {
        Rect r = moved(rect);
        return visible && (r.right() <= visible->x || r.bottom() <= visible->y || r.x >= visible->right() || r.y >= visible->bottom());
    }
    void fillGradient(const Rect& r, const CornerRadii& radii, const Brush& brush, const Rect& area) override {
        order += 'r';
        rectDepths.push_back(depth);
        rectBlends.push_back(blend());
        ++gradients;
        rects.push_back(moved(r));
        rectColors.push_back(filtered(brush.colorAt(r.center(), area)));  // (a gradient's at the rect's center)
        rectRadii.push_back(radii.topLeft);
        rectCorners.push_back(radii);
    }
    std::vector<Color> pathColors;   // each drawn path's color (a gradient's at the path's center)
    std::vector<float> pathStrokes;  // and stroke width (0: filled, unless a hairline)
    std::vector<PathStroke> pathStrokeStyles;  // and all of its stroke
    std::vector<Path> pathShapes;    // and the path itself (around the origin it was drawn at)
    struct Mesh {  // a drawVertices call
        Vertices vertices;
        Offset origin;
        BlendMode blendMode;
        Brush brush;
        Rect brushArea;
        float alpha;
        BlendMode paintBlend;              // the blend mode in effect
        std::vector<ColorFilter> filters;  // and the filters
    };
    std::vector<Mesh> meshes;
    void drawVertices(const Vertices& v, Offset origin, BlendMode mode, const Brush& brush, const Rect& area, float alpha) override {
        order += 'v';
        meshes.push_back({v, {origin.x + translation.x, origin.y + translation.y}, mode, brush, area, alpha, blend(), filterStack});
    }
    void drawPath(const Path& path, Offset origin, const Brush& brush, const Rect& area, const PathStroke& stroke) override {
        order += 'p';
        pathBlends.push_back(blend());
        pathAntiAliased.push_back(antiAliased());
        Rect b = path.bounds();
        pathColors.push_back(filtered(brush.colorAt(Rect{b.x + origin.x, b.y + origin.y, b.width, b.height}.center(), area)));
        paths.push_back(moved({b.x + origin.x, b.y + origin.y, b.width, b.height}));
        pathStrokes.push_back(stroke.width);
        pathStrokeStyles.push_back(stroke);
        pathShapes.push_back(path);
    }

private:
    Rect moved(const Rect& r) const { return {r.x + translation.x, r.y + translation.y, r.width, r.height}; }
    std::vector<Offset> stack_;
};

class ComposeTestRule;

// A failed assertion (Compose's AssertionError): what was looked for, and what was found.
class AssertionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// A node found by a matcher (or none). The assertions throw AssertionError and return the
// node, so they chain: rule.onNodeWithText("Tamam").assertIsDisplayed().assertIsEnabled().
// Like Compose's merged semantics, a text inside a clickable answers for that clickable
// (enabled, click action, selected).
class NodeInteraction {
public:
    bool exists() const { return node_ != nullptr; }
    LayoutNode* node() const { return node_; }
    Rect bounds() const { return node_ ? node_->bounds() : Rect{}; }
    std::string text() const { return node_ ? node_->semanticsText : std::string{}; }

    const NodeInteraction& assertExists() const;
    const NodeInteraction& assertDoesNotExist() const;
    const NodeInteraction& assertIsDisplayed() const;  // in the window and not clipped away
    const NodeInteraction& assertIsNotDisplayed() const;
    const NodeInteraction& assertTextEquals(std::string_view text) const;
    const NodeInteraction& assertTextContains(std::string_view text) const;
    const NodeInteraction& assertContentDescriptionEquals(std::string_view description) const;
    const NodeInteraction& assertHasClickAction() const;
    const NodeInteraction& assertHasNoClickAction() const;
    const NodeInteraction& assertIsEnabled() const;
    const NodeInteraction& assertIsNotEnabled() const;
    const NodeInteraction& assertIsSelected() const;  // checked: a selected tab, a ticked box, a switch on
    const NodeInteraction& assertIsNotSelected() const;
    const NodeInteraction& assertIsOn() const { return assertIsSelected(); }
    const NodeInteraction& assertIsOff() const { return assertIsNotSelected(); }

    const NodeInteraction& performClick() const;
    const NodeInteraction& performTextInput(std::string_view text) const;  // focuses the node first
    const NodeInteraction& performKey(Key key) const;
    const NodeInteraction& performScrollTo() const;  // scrolls the scrollables around it to show it

private:
    friend class ComposeTestRule;
    friend class NodeCollection;
    NodeInteraction(ComposeTestRule& rule, LayoutNode* node, std::string matcher)
        : rule_(&rule), node_(node), matcher_(std::move(matcher)) {}
    [[noreturn]] void fail(const std::string& what) const { throw AssertionError(what + " (node: " + matcher_ + ")"); }
    const LayoutNode& require(const char* assertion) const {
        if (!node_) fail(std::string(assertion) + " failed: no such node");
        return *node_;
    }
    // The clickable the node belongs to: itself or the nearest one around it.
    const ClickableElement* clickable() const {
        for (const LayoutNode* n = node_; n; n = n->overlay ? nullptr : n->parent)
            if (auto* c = n->modifier.find<ClickableElement>()) return c;
        return nullptr;
    }
    // Its selected / checked state, up to that clickable.
    std::optional<bool> checked() const {
        for (const LayoutNode* n = node_; n; n = n->overlay ? nullptr : n->parent) {
            if (auto s = detail::semanticsOf(n->modifier); s && s->checked) return s->checked;
            if (n->modifier.find<ClickableElement>()) break;
        }
        return std::nullopt;
    }
    ComposeTestRule* rule_;
    LayoutNode* node_;
    std::string matcher_;
};

// Every node a matcher finds (onAllNodesWithText and co.).
class NodeCollection {
public:
    size_t size() const { return nodes_.size(); }
    NodeInteraction operator[](size_t i) const;
    const NodeCollection& assertCountEquals(size_t count) const {
        if (nodes_.size() != count)
            throw AssertionError("assertCountEquals(" + std::to_string(count) + ") failed: found " + std::to_string(nodes_.size()) +
                                 " (nodes: " + matcher_ + ")");
        return *this;
    }

private:
    friend class ComposeTestRule;
    NodeCollection(ComposeTestRule& rule, std::vector<LayoutNode*> nodes, std::string matcher)
        : rule_(&rule), nodes_(std::move(nodes)), matcher_(std::move(matcher)) {}
    ComposeTestRule* rule_;
    std::vector<LayoutNode*> nodes_;
    std::string matcher_;
};

class ComposeTestRule {
public:
    explicit ComposeTestRule(Content content, Size viewport = {400, 800}) : host_(measurer_, std::move(content)) {
        host_.setViewportSize(viewport);
        waitForIdle(0);
    }

    // Runs frames (16 ms of virtual time each) until nothing is pending, including coroutine
    // delays due within `timerHorizonMillis` (so an endless ticking loop doesn't run forever;
    // use advanceTimeBy for those).
    void waitForIdle(int timerHorizonMillis = 2000) {
        int64_t horizon = clock_ + int64_t(timerHorizonMillis) * 1'000'000;
        for (int i = 0; i < 2'000; ++i) {
            // Work on worker threads (withBackground) takes real time: give it some.
            if (detail::backgroundWorkInFlight() > 0) std::this_thread::sleep_for(std::chrono::milliseconds(1));
            clock_ += 16'000'000;
            host_.frame(clock_);
            int64_t next = host_.nextTimerDeadline();
            if (!host_.hasPendingWork() && (next < 0 || next > horizon)) return;
        }
    }

    // Runs frames for `millis` of virtual time (coroutine delays, animations).
    void advanceTimeBy(int millis) {
        for (int t = 0; t < millis; t += 16) advanceFrame();
    }

    // Advances virtual time by exactly one frame.
    void advanceFrame() {
        clock_ += 16'000'000;
        host_.frame(clock_);
    }

    // A finger drag from `from` to `to`, one move per frame. Doesn't wait for a fling to end.
    void swipe(Offset from, Offset to, int frames = 10) {
        host_.pointerDown(from, clock_);
        for (int i = 1; i <= frames; ++i) {
            advanceFrame();
            float t = float(i) / float(frames);
            host_.pointerMove({from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t}, clock_);
        }
        host_.pointerUp(to, clock_);
        advanceFrame();
    }

    NodeInteraction onNodeWithText(std::string_view text, bool substring = false) {
        return find(textMatcher(text, substring), describe(substring ? "text containing" : "text", text));
    }

    // Nodes described with Modifier::semantics() (radio buttons, sliders, ...).
    NodeInteraction onNodeWithContentDescription(std::string_view description) {
        return find(
            [&](LayoutNode& n) {
                auto s = detail::semanticsOf(n.modifier);
                return s && s->contentDescription == description;
            },
            describe("content description", description));
    }

    NodeInteraction onNodeWithTag(std::string_view tag) { return find(tagMatcher(tag), describe("tag", tag)); }

    NodeCollection onAllNodesWithText(std::string_view text, bool substring = false) {
        return findAll(textMatcher(text, substring), describe(substring ? "text containing" : "text", text));
    }
    NodeCollection onAllNodesWithTag(std::string_view tag) { return findAll(tagMatcher(tag), describe("tag", tag)); }

    std::vector<std::string> drawnTexts() {
        RecordingCanvas canvas;
        host_.draw(canvas);
        return canvas.texts;
    }

    ComposeHost& host() { return host_; }

private:
    static std::string describe(const char* what, std::string_view value) { return std::string(what) + " \"" + std::string(value) + "\""; }
    static std::function<bool(LayoutNode&)> textMatcher(std::string_view text, bool substring) {
        return [text = std::string(text), substring](LayoutNode& n) {
            return substring ? n.semanticsText.find(text) != std::string::npos : n.semanticsText == text;
        };
    }
    static std::function<bool(LayoutNode&)> tagMatcher(std::string_view tag) {
        return [tag = std::string(tag)](LayoutNode& n) {
            for (const auto& e : n.modifier.elements())
                if (auto* t = dynamic_cast<const TestTagElement*>(e.get()); t && t->tag == tag) return true;
            return false;
        };
    }
    template <class Pred>
    std::vector<LayoutNode*> collect(Pred&& pred, bool firstOnly) {
        std::vector<LayoutNode*> found;
        auto visit = [&](auto&& self, LayoutNode& n) -> void {
            if (firstOnly && !found.empty()) return;
            if (pred(n)) {
                found.push_back(&n);
                if (firstOnly) return;
            }
            for (auto* c : n.children) self(self, *c);
            for (auto* o : n.overlays) self(self, *o);
        };
        visit(visit, host_.root());
        return found;
    }
    template <class Pred>
    NodeInteraction find(Pred&& pred, std::string matcher) {
        auto found = collect(pred, true);
        return {*this, found.empty() ? nullptr : found.front(), std::move(matcher)};
    }
    template <class Pred>
    NodeCollection findAll(Pred&& pred, std::string matcher) {
        return {*this, collect(pred, false), std::move(matcher)};
    }
    friend class NodeCollection;

    FakeTextMeasurer measurer_;
    ComposeHost host_;
    int64_t clock_ = 0;
};

inline const NodeInteraction& NodeInteraction::performClick() const {
    if (!node_) return *this;
    Offset c = node_->bounds().center();
    rule_->host().pointerMove(c);
    rule_->host().pointerDown(c);
    rule_->host().pointerUp(c);
    rule_->waitForIdle();
    return *this;
}

inline const NodeInteraction& NodeInteraction::performTextInput(std::string_view text) const {
    if (!node_) return *this;
    if (rule_->host().focusedNode() != node_) performClick();
    rule_->host().textInput(text);
    rule_->waitForIdle();
    return *this;
}

inline const NodeInteraction& NodeInteraction::performKey(Key key) const {
    rule_->host().keyDown(key);
    rule_->waitForIdle();
    return *this;
}

inline const NodeInteraction& NodeInteraction::performScrollTo() const {
    if (!node_) return *this;
    rule_->host().bringIntoView(*node_);
    rule_->waitForIdle();
    return *this;
}

inline const NodeInteraction& NodeInteraction::assertExists() const {
    require("assertExists");
    return *this;
}
inline const NodeInteraction& NodeInteraction::assertDoesNotExist() const {
    if (node_) fail("assertDoesNotExist failed: the node exists");
    return *this;
}
inline const NodeInteraction& NodeInteraction::assertIsDisplayed() const {
    const LayoutNode& n = require("assertIsDisplayed");
    // What shows of it: within the window and every clipping node around it.
    Size window = rule_->host().viewportSize();
    Rect r = n.bounds();
    auto clipTo = [&](Rect c) {
        float x0 = std::max(r.x, c.x), y0 = std::max(r.y, c.y);
        float x1 = std::min(r.right(), c.right()), y1 = std::min(r.bottom(), c.bottom());
        r = {x0, y0, std::max(0.f, x1 - x0), std::max(0.f, y1 - y0)};
    };
    clipTo({0, 0, window.width, window.height});
    for (const LayoutNode* p = n.overlay ? nullptr : n.parent; p; p = p->overlay ? nullptr : p->parent)
        for (const auto& e : p->modifier.elements())
            if (e->clipsContent()) clipTo(p->bounds());
    if (r.width <= 0 || r.height <= 0) fail("assertIsDisplayed failed: the node is out of view");
    return *this;
}
inline const NodeInteraction& NodeInteraction::assertIsNotDisplayed() const {
    if (!node_) return *this;
    try {
        assertIsDisplayed();
    } catch (const AssertionError&) {
        return *this;
    }
    fail("assertIsNotDisplayed failed: the node shows");
}
inline const NodeInteraction& NodeInteraction::assertTextEquals(std::string_view text) const {
    const LayoutNode& n = require("assertTextEquals");
    if (n.semanticsText != text) fail("assertTextEquals(\"" + std::string(text) + "\") failed: the text is \"" + n.semanticsText + "\"");
    return *this;
}
inline const NodeInteraction& NodeInteraction::assertTextContains(std::string_view text) const {
    const LayoutNode& n = require("assertTextContains");
    if (n.semanticsText.find(text) == std::string::npos)
        fail("assertTextContains(\"" + std::string(text) + "\") failed: the text is \"" + n.semanticsText + "\"");
    return *this;
}
inline const NodeInteraction& NodeInteraction::assertContentDescriptionEquals(std::string_view description) const {
    const LayoutNode& n = require("assertContentDescriptionEquals");
    auto s = detail::semanticsOf(n.modifier);
    std::string actual = s ? s->contentDescription : std::string();
    if (actual != description)
        fail("assertContentDescriptionEquals(\"" + std::string(description) + "\") failed: it is \"" + actual + "\"");
    return *this;
}
inline const NodeInteraction& NodeInteraction::assertHasClickAction() const {
    require("assertHasClickAction");
    if (!clickable()) fail("assertHasClickAction failed: nothing to click");
    return *this;
}
inline const NodeInteraction& NodeInteraction::assertHasNoClickAction() const {
    require("assertHasNoClickAction");
    if (clickable()) fail("assertHasNoClickAction failed: it is clickable");
    return *this;
}
inline const NodeInteraction& NodeInteraction::assertIsEnabled() const {
    require("assertIsEnabled");
    auto* c = clickable();
    if (c && !c->enabled) fail("assertIsEnabled failed: it is disabled");
    return *this;
}
inline const NodeInteraction& NodeInteraction::assertIsNotEnabled() const {
    require("assertIsNotEnabled");
    auto* c = clickable();
    if (!c || c->enabled) fail("assertIsNotEnabled failed: it is enabled");
    return *this;
}
inline const NodeInteraction& NodeInteraction::assertIsSelected() const {
    require("assertIsSelected");
    if (checked() != true) fail("assertIsSelected failed: it is not selected");
    return *this;
}
inline const NodeInteraction& NodeInteraction::assertIsNotSelected() const {
    require("assertIsNotSelected");
    if (checked() != false) fail("assertIsNotSelected failed: it is selected (or has no selected state)");
    return *this;
}

inline NodeInteraction NodeCollection::operator[](size_t i) const {
    return {*rule_, i < nodes_.size() ? nodes_[i] : nullptr, matcher_ + " #" + std::to_string(i)};
}

} // namespace kompoz::testing
