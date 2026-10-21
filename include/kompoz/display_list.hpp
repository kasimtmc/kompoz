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
#include <memory>
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
    void drawShapeShadow(const Rect& rect, const Shape& shape, bool rtl, const Shadow& shadow, bool inner) override;
    void drawVector(const ImageVector& vector, const Rect& bounds, Color color) override;
    void drawArc(const Rect& oval, float startDegrees, float sweepDegrees, float strokeWidth, Color color, bool roundCap) override;
    void drawImage(const ImageBitmap& image, const Rect& src, const Rect& dst, float alpha, FilterQuality quality = DefaultFilterQuality) override;
    void drawImageMask(const ImageBitmap& image, const Rect& src, const Rect& dst, const Brush& brush, const Rect& brushArea, float alpha,
                       FilterQuality quality = DefaultFilterQuality) override;
    void pushLayer(const Rect& bounds, const CornerRadii& radii, float alpha, const LayerTransform& transform = {}) override;
    void pushOutlineLayer(const Rect& bounds, const Path& outline, float alpha, const LayerTransform& transform = {}) override;
    void pushEffectLayer(const Rect& bounds, const RenderEffect& effect, float alpha) override;
    void drawBackdrop(const Rect& bounds, const RenderEffect& effect, float alpha) override;
    void drawElevationShadow(const Outline& outline, Offset origin, const ElevationShadow& shadow) override;
    Offset lightCenter() const override;
    void popLayer() override;
    void pushClip(const Rect& rect) override;
    void pushClipRoundRect(const Rect& rect, const CornerRadii& radii, ClipOp op) override;
    void pushClipPath(const Path& path, Offset origin, ClipOp op) override;
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
    void drawVertices(const Vertices& vertices, Offset origin, BlendMode blendMode, const Brush& brush, const Rect& brushArea,
                      float alpha) override;

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
                                ShadowPath, PushFilter, PopFilter, PushTransform, PopTransform, Backdrop, ElevationShadow, ShapeShadow,
                                Vertices, ImageMask };
    struct Command {
        Kind kind{};
        Rect rect, rect2;     // the shape; an image's source, a brush's area
        float f1 = 0, f2 = 0, f3 = 0;  // width, angles, alpha; a clip's kind (0 a rect, 1 a rounded rect, 2 a path)
        CornerRadii radii;             // a rounded rect's, a layer's
        Color color;
        Offset p1, p2;
        bool flag = false;    // rtl, round cap; a clip's ClipOp::Difference
        std::string text;
        Font font;
        ImageVector vector;
        ImageBitmap image;
        uint64_t imageVersion = 0;  // what it held (drawn into since: a change); a brush's images', summed up
        std::optional<Brush> brush;
        std::optional<ColorFilter> filter;  // PushFilter's
        std::optional<RenderEffect> effect;  // an effect layer's, a backdrop's
        std::optional<std::pair<Outline, kompoz::ElevationShadow>> elevated;  // an elevation shadow's (at p1)
        std::optional<std::pair<Shape, Shadow>> shaped;  // a brushed shape shadow's (over rect; rtl in flag, inner in f1)
        Path path;
        std::optional<kompoz::Vertices> vertices;  // (at p1; its blend mode in f2, its alpha f1)
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

namespace detail {

// What draws into bitmaps (graphics::Canvas(ImageBitmap)): the platform's renderer, set on the UI
// thread while there is one.
class BitmapRenderer {
public:
    virtual ~BitmapRenderer() = default;
    // `bitmap` drawn into by `draws` (over what it holds, a unit a pixel), now: its new pixels
    // stored (bitmaps::store) or kept by the renderer (bitmaps::drawnOnRenderer).
    virtual void draw(const ImageBitmap& bitmap, const DisplayList& draws) = 0;
    // Its pixels, which only the renderer has, read back (bitmaps::store).
    virtual void readBack(const ImageBitmap& bitmap) = 0;
    virtual TextMeasurer* measurer() { return nullptr; }  // what recordings into bitmaps measure text with
};
// Sets it (null: none); what was drawn into bitmaps while there was none is drawn now.
void setBitmapRenderer(BitmapRenderer* renderer);
// `draws` drawn into `bitmap`: by the renderer, on the UI thread (waited for from another), or
// kept for one to come.
void drawIntoBitmap(const ImageBitmap& bitmap, std::shared_ptr<const DisplayList> draws);
// What recordings into bitmaps measure text with: the renderer's, or one that knows no fonts.
TextMeasurer& bitmapMeasurer();

// For renderers: what a bitmap holds, and where.
namespace bitmaps {
bool pixelsCurrent(const ImageBitmap& bitmap);  // its pixels hold what it does (else only the renderer has them)
const std::vector<float>& floats(const ImageBitmap& bitmap);  // F16's, straight RGBA (empty for the others)
// It holds these (premultiplied RGBA, 8 bits a channel or floats, as drawn), kept as its config
// keeps them; `drawn`: just drawn into it (what it holds is new).
void store(const ImageBitmap& bitmap, const std::vector<uint8_t>& premultiplied, bool drawn);
void storeFloats(const ImageBitmap& bitmap, const std::vector<float>& premultiplied, bool drawn);
// It was drawn into and only the renderer has what it holds now (read back when it is asked for).
void drawnOnRenderer(const ImageBitmap& bitmap);
} // namespace bitmaps

} // namespace detail

} // namespace kompoz
