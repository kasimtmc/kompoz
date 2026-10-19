#pragma once

// Kompoz's GPU renderer: GraphicsContext drawn with the hardware layer (device.hpp) on any of its
// backends. Every primitive is a quad whose pixels the fragment shader covers exactly from the
// shape's distance (shaders/primitives.slang); text and ImageVectors come from the glyph atlas.
//
// Drawing is recorded, then submitted by flush(): each target (the kept frame, the swapchain
// image, a layer's texture) gets a pass of batched quads, and a layer's pass runs before the one
// that composites it.
//
//   renderer.beginTarget(frame);           // draws into `frame` until endTarget()
//   host.draw(renderer);
//   renderer.endTarget();
//   renderer.flush();

#include "device.hpp"
#include "glyph_atlas.hpp"
#include "path_geometry.hpp"

#include <kompoz/graphics.hpp>

#include <memory>
#include <optional>
#include <unordered_map>
#include <vector>

namespace kompoz::gpu {

class Renderer final : public GraphicsContext {
public:
    Renderer(Device& device, TextSource& text);
    ~Renderer() override;

    float scale = 1;     // px per dp
    int layerCount = 0;  // layers pushed (KOMPOZ_PROFILE)

    // Draws into `target` until endTarget(), cleared first to `clear` if given.
    void beginTarget(Texture& target, const std::optional<ClearColor>& clear = std::nullopt);
    void endTarget();
    // Sets a px rect of the target to `color` as it is (no blending).
    void fillOpaque(const IRect& rect, Color color);
    // Copies `texture` over the whole target as it is.
    void blit(Texture& texture);
    // Submits what the targets ended so far recorded (glyphs and images uploaded first).
    void flush();
    // After each frame: textures of images that are gone, and layer targets unused for a while, go.
    void endFrame();

    void fillRoundRect(const Rect& rect, const CornerRadii& radii, Color color) override;
    void strokeRoundRect(const Rect& rect, const CornerRadii& radii, float width, Color color) override;
    void drawLine(Offset from, Offset to, float width, Color color) override;
    void drawText(std::string_view utf8, Offset baseline, const Font& font, Color color, bool rtl) override;
    void drawTextBrush(std::string_view utf8, Offset baseline, const Font& font, const Brush& brush, const Rect& brushArea,
                       bool rtl) override;
    void drawTextShadow(std::string_view utf8, Offset baseline, const Font& font, Color color, float blurRadius, bool rtl) override;
    void drawVector(const ImageVector& vector, const Rect& bounds, Color color) override;
    void drawArc(const Rect& oval, float startDegrees, float sweepDegrees, float strokeWidth, Color color, bool roundCap) override;
    void drawImage(const ImageBitmap& image, const Rect& src, const Rect& dst, float alpha) override;
    void pushLayer(const Rect& bounds, const CornerRadii& radii, float alpha, const LayerTransform& transform = {}) override;
    void popLayer() override;
    void pushClip(const Rect& rect) override;
    void popClip() override;
    void pushTranslation(Offset by) override;
    void popTranslation() override;
    void pushTransform(const Matrix& matrix) override;
    void popTransform() override;
    bool quickReject(const Rect& rect) const override;
    void drawPath(const Path& path, Offset origin, const Brush& brush, const Rect& brushArea, const PathStroke& stroke) override;
    void drawShadow(const Rect& rect, const CornerRadii& radii, const Shadow& shadow, bool inner) override;
    void fillGradient(const Rect& rect, const CornerRadii& radii, const Brush& brush, const Rect& brushArea) override;
    void pushBlendMode(BlendMode mode) override;
    void popBlendMode() override;
    void pushColorFilter(const ColorFilter& filter) override;
    void popColorFilter() override;
    void pushOutlineLayer(const Rect& bounds, const Path& outline, float alpha, const LayerTransform& transform = {}) override;

private:
    // One quad: what shaders/primitives.slang's Instance says.
    struct Prim {
        float bounds[4]{}, shape[4]{}, radii[4]{}, color[4]{}, params[4]{}, uv[4]{};
        float xform[4]{1, 0, 0, 1}, extra[4]{};
        float paint[4]{}, gradient[4]{};
        float filter[4]{};  // how many color filters, their row in the table; how many clips, their row
        float projection[4]{0, 0, 1, 0};
    };
    // Where a batch's textures come from: its own, or the coverage mask (made when the pass runs).
    enum class Mask : uint8_t { None, Texture0, Texture1 };
    struct Batch {
        Texture* texture;
        Texture* texture1;
        Mask mask;
        bool opaque;  // written as it is, not blended
        BlendMode blend;
        IRect scissor;
        uint32_t generation;  // the pass's masks it reads (see Pass)
        uint32_t first, count;
    };
    // One edge of a path in the coverage mask, and the path's part of the mask (shaders/path.slang).
    struct EdgeInstance {
        float edge[4], region[4];
    };
    // A pass's paths are rasterized into the coverage mask before it runs. When the mask is full a
    // new generation starts: the mask is made again for the batches recorded after that.
    struct MaskShelf {
        int width = 0, height = 0;  // what a generation has
        int x = 0, y = 0, row = 0;
        uint32_t generation = 0;
    };
    struct Pass {
        Texture* target;
        std::optional<ClearColor> clear;
        std::vector<Prim> prims;
        std::vector<Batch> batches;
        std::vector<EdgeInstance> edges;
        std::vector<uint32_t> edgeGenerations;  // the generation each edge is in
        MaskShelf shelf;
    };
    // A clip in effect: the pixels it leaves (whole ones), and the clips that cut them finer (a
    // rect turned, say), if any: their chain.
    struct Clip {
        IRect scissor;
        int chain = -1;
    };
    // A rounded rect a primitive's coverage is cut to (or to what is outside it): in its own space,
    // which `toLocal` maps the target's pixels to.
    struct ClipElement {
        float rect[4], radii[4];
        Projective toLocal;
        bool outside = false;
    };
    // The clips in effect, outermost first, and their row in this frame's table once a primitive
    // in them is drawn.
    struct ClipChain {
        std::vector<ClipElement> elements;
        int row = -1;
    };
    // What pushTranslation and pushTransform change: the pixel origin, and the transform after it.
    struct Placement {
        float ox, oy;
        Projective transform;
    };
    // A target being drawn into: its pass, and the drawing state outside it.
    struct Target {
        size_t pass;
        float ox, oy;  // the pixel origin
        Projective transform;
        std::vector<Clip> clips;
        std::vector<Placement> placements;
        std::vector<BlendMode> blends;
        std::vector<ColorFilter> filters;
    };
    // A layer's texture is in the parent's pixels before the parent's transform, times
    // `resolution` (the transform's scale, so that what it enlarges stays sharp).
    struct Layer {
        Texture* texture;  // null: too big, drawn straight into the target
        IRect rect;        // in the parent's pixels times the resolution, whole ones (where the texture goes)
        Rect shape;        // the exact bounds there, which rounded corners follow
        CornerRadii radii;  // there
        float alpha;
        LayerTransform transform;
        float resolution = 1;
        float parentScale = 1;  // the px per dp outside it
        std::optional<Path> outline;  // cut to this (dp from the bounds' top left) instead of the corners
    };
    struct ImageTexture {
        std::unique_ptr<Texture> texture;
        std::weak_ptr<const void> token;
    };
    struct PooledTexture {
        std::unique_ptr<Texture> texture;
        bool inUse = false;
        int idleFrames = 0;
    };

    // Records a primitive: its own transform (the prim's) goes through the current one, unless it
    // is in the target's pixels already (a path's coverage, an opaque fill).
    void add(int kind, const Prim& prim, Texture* texture = nullptr, bool opaque = false, Mask mask = Mask::None, Texture* texture1 = nullptr);
    // Rasterizes polygons into the current pass's coverage mask, within `region` (px of the
    // target, or of a layer before its transform): where the mask's texel (0, 0) is in those
    // pixels, or nothing if the region is empty.
    std::optional<Offset> rasterize(const std::vector<Contour>& contours, IRect region);
    void fillContours(const std::vector<Contour>& contours, bool evenOdd, const Prim& paint);
    // A prim painted with `brush` over `area` (dp): its color, or its gradient (at `alpha`), in
    // pixels `enlarged` times.
    Prim painted(const Brush& brush, const Rect& area, float alpha = 1, float enlarged = 1);
    // The glyphs laid out in pixels `enlarged` times.
    void glyphs(const Prim& paint, float enlarged);
    // How many times its pixels something around the px point (x, y) is to be rasterized so
    // that the current transform shows it sharp: 1 when that doesn't scale it, else its largest
    // stretch there, rounded up to an eighth of an octave (text scaled by an animation is laid out
    // at a few sizes, not anew each frame).
    float rasterScale(float x, float y) const;
    // The bounding box in the target's pixels of the px rect through the current transform, or
    // nothing if part of it is behind the eye.
    std::optional<Rect> mapped(float x0, float y0, float x1, float y1) const;
    // The current clip's chain, as a table row.
    int clipRow(int chain);
    Texture* pageTexture(int page);
    Texture* imageTexture(const ImageBitmap& image);
    Texture* acquire(int width, int height);
    IRect scissor() const;
    Pipeline& pipeline(Format target, const BlendState& blend);
    void execute(Pass& pass);

    Device& device_;
    TextSource& text_;
    std::unique_ptr<Texture> white_;  // what quads that sample nothing bind
    bool whiteUploaded_ = false;
    std::vector<std::unique_ptr<Texture>> pages_;  // the glyph atlas's pages
    std::unordered_map<uint64_t, ImageTexture> images_;
    std::vector<PooledTexture> pool_;  // layer targets
    struct PipelineKey {
        Format format;
        BlendState blend;
        bool operator==(const PipelineKey&) const = default;
    };
    std::vector<std::pair<PipelineKey, std::unique_ptr<Pipeline>>> pipelines_;
    std::unique_ptr<Texture> dstCopy_;  // a target's pixels under a draw whose blend mode reads them
    std::unique_ptr<Pipeline> coveragePipeline_;
    std::unique_ptr<Texture> mask_;  // the coverage mask, as big as a generation needed so far
    // Gradients' stops this frame, a row each (see shaders/primitives.slang), and their texture.
    static constexpr int kStopsWidth = 256;  // texels: 128 stops
    std::vector<float> stops_;
    std::vector<Brush> stopBrushes_;
    std::vector<int> stopRows_;  // each one's row
    int stopsUploaded_ = 0;  // rows
    std::unique_ptr<Texture> stopsTexture_;
    std::vector<Edge> edgeScratch_;
    std::vector<Pass> passes_;
    std::vector<size_t> order_;  // ended passes, in the order they run
    std::vector<Target> targets_;  // the targets being drawn into, innermost last
    std::vector<Layer> layers_;
    std::vector<GlyphQuad> glyphs_;
    float ox_ = 0, oy_ = 0;  // the current target's pixel origin
    Projective transform_;   // the current target's pixels after the origin: what pushTransform makes of them
    std::vector<Clip> clips_;
    std::vector<Placement> placements_;  // pushTranslation's and pushTransform's
    std::vector<ClipChain> clipChains_;  // this frame's
    std::vector<BlendMode> blends_;       // pushBlendMode's in this target, the innermost last
    std::vector<ColorFilter> filters_;    // pushColorFilter's in this target, the innermost last
    std::vector<std::vector<ColorFilter>> filterChains_;  // this frame's, a table row each (after the stops')
    std::vector<int> filterRows_;
};

} // namespace kompoz::gpu
