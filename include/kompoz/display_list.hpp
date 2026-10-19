#pragma once

// A frame's drawing, recorded, so that the next frame can tell which pixels changed and only
// those are drawn again; a frame that changes nothing on the screen isn't drawn at all (an
// animation scrolled out of view, a recomposition that looks the same).
//
//   list.begin(viewport, onePixel);
//   host.draw(list);                       // records
//   if (auto area = list.end())            // what differs from the last frame (dp)
//       list.replay(canvas, *area);        // into the kept frame, clipped to the area
//
// Each command knows the screen area it can touch: its own bounds through the transforms of
// the layers around it, cut by their clips, padded for anti-aliasing. Frames are lined up by
// the layout nodes that drew them (GraphicsContext::beginNode), so content that appears or
// goes doesn't make everything after it look changed: what a node drew (before its first
// child, after each child) is compared with what it drew last time, and the areas of what
// differs, before and after, are the damage.

#include "graphics.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace kompoz {

class DisplayList final : public GraphicsContext {
public:
    explicit DisplayList(TextMeasurer& measurer) : measurer_(measurer) {}

    // Starts recording a frame of `viewport` (dp); `pixel` is a pixel's size in dp.
    void begin(Size viewport, float pixel);
    // Ends it: the area whose pixels differ from the last frame's (dp), or nothing.
    std::optional<Rect> end();
    // Draws the last recorded frame into `canvas`: what touches `area`, or everything.
    void replay(GraphicsContext& canvas, const std::optional<Rect>& area = std::nullopt) const;
    // The next frame is damaged all over (the kept pixels were lost).
    void invalidate() { invalid_ = true; }
    size_t size() const { return frame_.size(); }  // commands in the last frame

    void beginNode(const void* node) override;
    void endNode() override;
    void fillRoundRect(const Rect& rect, const CornerRadii& radii, Color color) override;
    void strokeRoundRect(const Rect& rect, const CornerRadii& radii, float width, Color color) override;
    void drawLine(Offset from, Offset to, float width, Color color) override;
    void drawText(std::string_view utf8, Offset baseline, const Font& font, Color color, bool rtl = false) override;
    void drawTextBrush(std::string_view utf8, Offset baseline, const Font& font, const Brush& brush, const Rect& brushArea,
                       bool rtl = false) override;
    void drawTextShadow(std::string_view utf8, Offset baseline, const Font& font, Color color, float blurRadius,
                        bool rtl = false) override;
    void drawShadow(const Rect& rect, const CornerRadii& radii, const Shadow& shadow, bool inner) override;
    void drawShadowPath(const Path& outline, Offset origin, const Shadow& shadow, bool inner) override;
    void drawVector(const ImageVector& vector, const Rect& bounds, Color color) override;
    void drawArc(const Rect& oval, float startDegrees, float sweepDegrees, float strokeWidth, Color color, bool roundCap) override;
    void drawImage(const ImageBitmap& image, const Rect& src, const Rect& dst, float alpha) override;
    void pushLayer(const Rect& bounds, const CornerRadii& radii, float alpha, const LayerTransform& transform = {}) override;
    void pushOutlineLayer(const Rect& bounds, const Path& outline, float alpha, const LayerTransform& transform = {}) override;
    void pushBlurLayer(const Rect& bounds, float radiusX, float radiusY) override;
    void popLayer() override;
    void pushClip(const Rect& rect) override;
    void pushBlendMode(BlendMode mode) override;
    void popBlendMode() override;
    void pushColorFilter(const ColorFilter& filter) override;
    void popColorFilter() override;
    void popClip() override;
    void pushTranslation(Offset by) override;
    void popTranslation() override;
    void pushTransform(const Matrix& matrix) override;
    void popTransform() override;
    bool quickReject(const Rect& rect) const override;
    void fillGradient(const Rect& rect, const CornerRadii& radii, const Brush& brush, const Rect& brushArea) override;
    void drawPath(const Path& path, Offset origin, const Brush& brush, const Rect& brushArea, const PathStroke& stroke) override;

    // Maps points of what is drawn inside transforms and layers to the screen: (x, y, 1) to
    // (x', y', w), row by row, the point x' / w, y' / w.
    struct Projective {
        float m[9] = {1, 0, 0, 0, 1, 0, 0, 0, 1};
        // The bounding box of the mapped rect; if part of it is behind the eye, all of the plane.
        Rect map(const Rect& r) const;
        friend bool operator==(const Projective&, const Projective&) = default;
    };

private:
    enum class Kind : uint8_t { FillRoundRect, StrokeRoundRect, Line, Text, TextBrush, TextShadow, Shadow, Vector, Arc, Image, Gradient, Path,
                                PushLayer, PopLayer, PushClip, PopClip, PushTranslation, PopTranslation, PushBlend, PopBlend,
                                ShadowPath, PushFilter, PopFilter, PushTransform, PopTransform };
    struct Command {
        Kind kind{};
        Rect rect, rect2;     // the shape; an image's source, a brush's area
        float f1 = 0, f2 = 0, f3 = 0;  // width, angles, alpha
        CornerRadii radii;             // a rounded rect's, a layer's
        Color color;
        Offset p1, p2;
        bool flag = false;    // rtl, round cap
        std::string text;
        Font font;
        ImageVector vector;
        ImageBitmap image;
        std::optional<Brush> brush;
        std::optional<ColorFilter> filter;  // PushFilter's
        Path path;
        PathStroke stroke;
        LayerTransform transform;
        Matrix matrix;        // PushTransform's
        Projective m;         // the transform it is drawn through
        Rect bounds;          // what it can touch on the screen (a layer: where it is composited)
        uint32_t end = 0;     // a push: the index after its pop
        bool same(const Command& o) const;
    };
    struct Segment {  // what one node drew between its children
        uint64_t key;
        uint32_t first, count;
    };
    struct State {  // a layer's, clip's, translation's or transform's
        Projective m;
        Rect clip;
        uint32_t push;  // its command
    };
    struct OpenNode {
        uint64_t key;
        uint64_t after = 0;  // the child the stretch being drawn follows (its key)
    };

    Command& add(Kind kind);
    // Records a draw command touching `local` (padded by `pad` dp), unless that is clipped away.
    Command* draw(Kind kind, const Rect& local, float pad);
    // Records text (any kind of it) whose glyphs may reach `blur` further.
    Command* text(Kind kind, std::string_view utf8, Offset baseline, const Font& font, bool rtl, float blur = 0);
    void pop(Kind kind);
    static void addDamage(std::optional<Rect>& damage, const std::vector<Command>& commands, uint32_t first, uint32_t count);

    TextMeasurer& measurer_;
    std::vector<Command> recording_, frame_;
    std::vector<Segment> recordingSegments_, frameSegments_;
    std::vector<State> states_;
    std::vector<OpenNode> nodes_;
    std::unordered_map<const void*, uint32_t> occurrences_;
    std::unordered_map<uint64_t, uint32_t> lookup_;  // end(): the last frame's segments by key
    std::vector<char> matched_;
    bool segmentOpen_ = false, invalid_ = true;
    Size viewport_, frameViewport_;
    float pixel_ = 1, framePixel_ = 0;
};

} // namespace kompoz
