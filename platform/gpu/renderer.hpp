#pragma once

// Kompoz's GPU renderer: GraphicsContext drawn with the hardware layer (device.hpp) on any of its
// backends. Every primitive is a quad whose pixels the fragment shader covers exactly from the
// shape's distance (shaders/primitives.slang); text comes from the glyph atlas.
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

namespace kompoz {
class DisplayList;
}

namespace kompoz::gpu {

class Renderer final : public GraphicsContext {
public:
    Renderer(Device& device, TextSource& text);
    ~Renderer() override;

    float scale = 1;     // px per dp
    int layerCount = 0;  // layers pushed (KOMPOZ_PROFILE)
    int keptLayers = 0;  // of them, composited from the pixels they had in a frame before

    // Draws into `target` until endTarget(), cleared first to `clear` if given.
    void beginTarget(Texture& target, const std::optional<ClearColor>& clear = std::nullopt);
    void endTarget();
    // Sets a px rect of the target to `color` as it is (no blending).
    void fillOpaque(const IRect& rect, Color color);
    // Copies `texture` into the target as it is, texel for pixel, turned by `quarterTurns`
    // (clockwise, about the target: Device::swapchainTurns).
    void blit(Texture& texture, int quarterTurns = 0, bool flipped = false);
    // Moves the target's pixels in `from` by (dx, dy) (as it is: the pixels it leaves keep theirs).
    // False if it can't (no texture to copy them through).
    bool move(const IRect& from, int dx, int dy);
    // Draws the frame `list` recorded last (DisplayList::end) into `target`: all of it, or with
    // `changes` only that, into the frame before it (its moves made, its areas drawn again each
    // clipped to itself). What isn't drawn on is white; what an opaque rect covering an area hides
    // (the white too) isn't drawn.
    void drawFrame(Texture& target, const DisplayList& list, bool changes);
    // Submits what the targets ended so far recorded (glyphs and images uploaded first).
    void flush();
    // After each frame: textures of images that are gone, and layer targets unused for a while, go.
    void endFrame();
    // A bitmap drawn into (graphics::Canvas(ImageBitmap)): `draws` over what it holds, a unit a
    // pixel, into a new texture of its own (draws recorded before still read the old one). Kept
    // here where the bitmap keeps 8 or 16 bits a channel with alpha (Argb8888, Gpu, F16: the bitmap
    // told only the renderer has what it holds), else read back into it at once (flushing what is
    // recorded), kept as its config keeps it. flush() submits the drawing.
    void drawIntoBitmap(const ImageBitmap& bitmap, const DisplayList& draws);
    // The pixels of a bitmap only this renderer has, read back into it (flushing what is recorded).
    void readBackBitmap(const ImageBitmap& bitmap);
    // Every bitmap's that only this renderer has, before it goes.
    void releaseBitmaps();

    void fillRoundRect(const Rect& rect, const CornerRadii& radii, Color color) override;
    void strokeRoundRect(const Rect& rect, const CornerRadii& radii, float width, Color color) override;
    void drawLine(Offset from, Offset to, float width, Color color) override;
    void drawText(std::string_view utf8, Offset baseline, const Font& font, Color color, bool rtl) override;
    void drawTextBrush(std::string_view utf8, Offset baseline, const Font& font, const Brush& brush, const Rect& brushArea,
                       bool rtl) override;
    void drawTextShadow(std::string_view utf8, Offset baseline, const Font& font, Color color, float blurRadius, bool rtl) override;
    void drawArc(const Rect& oval, float startDegrees, float sweepDegrees, float strokeWidth, Color color, bool roundCap) override;
    void drawImage(const ImageBitmap& image, const Rect& src, const Rect& dst, float alpha, FilterQuality quality = DefaultFilterQuality) override;
    void drawImageMask(const ImageBitmap& image, const Rect& src, const Rect& dst, const Brush& brush, const Rect& brushArea, float alpha,
                       FilterQuality quality) override;
    void pushLayer(const Rect& bounds, const CornerRadii& radii, float alpha, const LayerTransform& transform = {}) override;
    void popLayer() override;
    void pushClip(const Rect& rect) override;
    void pushClipRoundRect(const Rect& rect, const CornerRadii& radii, ClipOp op) override;
    void pushClipPath(const Path& path, Offset origin, ClipOp op) override;
    void popClip() override;
    void pushTranslation(Offset by) override;
    void popTranslation() override;
    void pushTransform(const Matrix& matrix) override;
    void popTransform() override;
    bool quickReject(const Rect& rect) const override;
    void drawPath(const Path& path, Offset origin, const Brush& brush, const Rect& brushArea, const PathStroke& stroke) override;
    void drawVertices(const Vertices& vertices, Offset origin, BlendMode blendMode, const Brush& brush, const Rect& brushArea,
                      float alpha) override;
    void drawShadow(const Rect& rect, const CornerRadii& radii, const Shadow& shadow, bool inner) override;
    void fillGradient(const Rect& rect, const CornerRadii& radii, const Brush& brush, const Rect& brushArea) override;
    void pushBlendMode(BlendMode mode) override;
    void pushAntiAlias(bool on) override;
    void popAntiAlias() override;
    void popBlendMode() override;
    void pushColorFilter(const ColorFilter& filter) override;
    void popColorFilter() override;
    void pushOutlineLayer(const Rect& bounds, const Path& outline, float alpha, const LayerTransform& transform = {}) override;
    void pushEffectLayer(const Rect& bounds, const RenderEffect& effect, float alpha) override;
    void drawBackdrop(const Rect& bounds, const RenderEffect& effect, float alpha) override;
    void drawElevationShadow(const Outline& outline, Offset origin, const ElevationShadow& shadow) override;
    // A layer whose content is the same as when it was last drawn (whole) is composited from the
    // pixels it had (and what its effect made of them), not drawn again; one that stays the same
    // is drawn whole, and kept.
    void keyLayer(uint64_t key, bool same) override;
    LayerContent layerContent() const override { return pushed_; }

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
    // What a batch's primitives need of the fragment shader (its specialization constants: see
    // shaders/primitives.slang): their kinds and paints (bits), whether they are clipped or
    // filtered, the flags they set. A pipeline is made for each one met (and blend mode).
    struct ShaderVariant {
        uint16_t kinds = 0;
        uint8_t paints = 0, flags = 0;
        bool clipped = false, filtered = false;
        ShaderVariant& operator|=(const ShaderVariant& o) {
            kinds |= o.kinds, paints |= o.paints, flags |= o.flags;
            clipped = clipped || o.clipped, filtered = filtered || o.filtered;
            return *this;
        }
        bool operator==(const ShaderVariant&) const = default;
    };
    struct Batch {
        Texture* texture;
        Texture* texture1;
        Mask mask;
        bool opaque;  // written as it is, not blended
        BlendMode blend;
        IRect scissor;
        uint32_t generation;  // the pass's masks it reads (see Pass)
        uint32_t first, count;
        Sampler sampler;  // texture's (an image's, as its FilterQuality says)
        Sampler sampler1;  // texture1's (an image shader's, as its tile modes say)
        bool mesh = false;  // (see below)
        Texture* texture5 = nullptr;  // a composite paint's source image (white if none)
        Sampler sampler5;  // its primitives a mesh's triangles (three vertices each, by vsMesh)
        ShaderVariant variant;  // its primitives' together
    };
    // One edge of a path in the coverage mask, and the path's part of the mask (shaders/path.slang).
    struct EdgeInstance {
        float edge[4], region[4], mode[4];  // (mode.x: not anti-aliased)
    };
    // The paths of the passes recorded until a flush with none still open are rasterized into one
    // coverage mask, all of them in one go before the passes run (a mask pass between one pass and
    // the next held a tiling GPU up at each). When it is full a new generation starts, in a mask of
    // its own.
    struct MaskShelf {
        int width = 0, height = 0;  // what a generation has
        int x = 0, y = 0, row = 0;
        uint32_t generation = 0;
    };
    // A pass of an effect (shaders/effects.slang): `rect` of the target made from `source`
    // (filtered linearly, or not), with this frame's table if it reads stops, or a mask of its own
    // in the table's place.
    struct EffectDraw {
        Texture* source;
        IRect rect;
        float uniforms[32];
        bool linear = false, table = false;
        Texture* mask = nullptr;
    };
    struct Pass {
        Texture* target;
        std::optional<ClearColor> clear;
        std::vector<Prim> prims;
        std::vector<Batch> batches;
        std::vector<EdgeInstance> edges;
        std::vector<uint32_t> edgeGenerations;  // the generation each edge is in
        std::optional<EffectDraw> effect;  // set: this instead of the rest
        struct Copy {
            Texture* from;
            IRect region;
            int x = 0, y = 0;  // where in the target
        };
        std::optional<Copy> copy;  // set: that part of that texture copied into the target, instead
    };
    // A clip in effect: the pixels it leaves (whole ones), and the clips that cut them finer (a
    // rect turned, say), if any: their chain.
    struct Clip {
        IRect scissor;
        int chain = -1;
    };
    // What a primitive's coverage is cut to (or to what is outside it): a rounded rect in its own
    // space, which `toLocal` maps the target's pixels to; or a path, its polygons in the target's
    // pixels, filled by the non-zero or even-odd rule, rasterized into the pass's coverage mask
    // within `region` (the pixels they reach, within the clips around).
    struct ClipElement {
        float rect[4]{}, radii[4]{};
        Projective toLocal;
        bool outside = false;
        std::shared_ptr<const std::vector<Contour>> polygons;  // a path's (shared by the chains inside it)
        bool evenOdd = false;
        IRect region;
    };
    // The clips in effect, outermost first, and their row in this frame's table once a primitive
    // in them is drawn (a chain with paths gets a row for each mask generation it is drawn in).
    struct ClipChain {
        std::vector<ClipElement> elements;
        bool paths = false;
        int row = -1;
        uint32_t generation = 0;  // the row's
        size_t pass = SIZE_MAX;   // and its pass's (a pass split by a backdrop starts its mask anew)
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
        Projective windowFromTarget;
        std::vector<Clip> clips;
        std::vector<Placement> placements;
        std::vector<BlendMode> blends;
        std::vector<ColorFilter> filters;
        std::vector<bool> antiAlias;
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
        std::optional<RenderEffect> effect;  // an effect layer's (not cut, its own way of compositing)
        uint64_t key = 0;   // keyLayer's: kept (the texture is the kept one's) or to keep
        bool kept = false;
    };
    // What an effect layer's steps make of its texture: `texels` of `texture`, whose texel (0, 0)
    // is at `origin` in the layer's pixels, each texel `step` of them across and down; it shows
    // `shown` (whole pixels), moved by `offset`.
    struct EffectImage {
        Texture* texture;
        int originX, originY, stepX = 1, stepY = 1, texelsX, texelsY;
        IRect shown;
        float offsetX = 0, offsetY = 0;
    };
    struct ImageTexture {
        std::unique_ptr<Texture> texture;
        std::weak_ptr<const void> token;
        bool mipmapped = false;
        uint64_t version = 0;  // what of the bitmap it holds (ImageBitmap::version)
        std::weak_ptr<ImageBitmap::Data> kept;  // a bitmap drawn into here: whose pixels only this texture may have
    };
    struct PooledTexture {
        std::unique_ptr<Texture> texture;  // (its format: a target's copy has the target's)
        bool inUse = false;
        int idleFrames = 0;
    };
    // A layer's pixels kept from frame to frame (by its key): its texture as it was made, where
    // its exact bounds were in it (a fraction of a pixel), at what resolution and px per dp; what
    // its effect made of it, if it has one (its texture, unless that is the layer's own, and where
    // it goes from the layer's rect's corner).
    struct KeptLayer {
        std::unique_ptr<Texture> texture, effected;
        int width = 0, height = 0;
        float fractionX = 0, fractionY = 0, resolution = 1, parentScale = 1;
        std::optional<RenderEffect> effect;
        EffectImage image{};
        int idleFrames = 0;
    };

    // Records a primitive: its own transform (the prim's) goes through the current one, unless it
    // is in the target's pixels already (a path's coverage, an opaque fill).
    void add(int kind, const Prim& prim, Texture* texture = nullptr, bool opaque = false, Mask mask = Mask::None, Texture* texture1 = nullptr,
             bool inTargetPixels = false, Sampler sampler = {});
    // Rasterizes polygons into the current pass's coverage mask, within `region` (px of the
    // target, or of a layer before its transform): where the mask's texel (0, 0) is in those
    // pixels, or nothing if the region is empty.
    std::optional<Offset> rasterize(const std::vector<Contour>& contours, IRect region, bool aliased = false);
    void fillContours(const std::vector<Contour>& contours, bool evenOdd, const Prim& paint);
    // A path's polygons in the target's pixels (its stroke's outline if `stroke` has a width),
    // through the current transform.
    std::vector<Contour> polygonsOf(const Path& path, Offset origin, const PathStroke& stroke);
    // rasterize() for a primitive about to be added: in the same mask generation as the paths of
    // the clips it is drawn in.
    std::optional<Offset> rasterizeInClips(const std::vector<Contour>& contours, IRect region, bool aliased = false);
    // A clip in the chain inside the current one: a new chain, the clips it leaves (whole pixels).
    void pushChain(ClipElement element, const IRect& scissor);
    // pushLayer(), a layer too big for a texture cut to `within` (the parent's pixels), if any.
    void pushLayer(const Rect& bounds, const CornerRadii& radii, float alpha, const LayerTransform& transform,
                   const std::optional<Rect>& within);
    // An effect layer's steps on its texture (in passes that run after its own), and the image
    // they make.
    EffectImage applyEffect(const Layer& layer);
    // A progressive blur (the first of an effect's steps, as there is nothing it blurs but the
    // layer) on the layer's texture: cut to its bounds, then across and down.
    EffectImage progressiveBlur(const Layer& layer, const RenderEffect::Step& step);
    // A pass of an effect into `target`, from `source`, as `uniforms` say (all but the first four,
    // and the table texel's size).
    void effectPass(Texture& target, Texture& source, const IRect& rect, const float (&uniforms)[32], bool linear = false,
                    bool table = false, Texture* mask = nullptr);
    // A row of the table: each stop's position and value at a texel of its own.
    int tableRow(const std::vector<std::pair<float, float>>& stops);
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
    Texture* imageTexture(const ImageBitmap& image, bool mipmapped = false);  // its pixels premultiplied
    Texture* acquire(int width, int height, Format format = Format::RGBA8);
    IRect scissor() const;
    // The pipeline for a batch (made once); none if this device can't make one.
    Pipeline* pipeline(Format target, const BlendState& blend, bool mesh, const ShaderVariant& variant, int how);
    void execute(Pass& pass);
    void drawMasks();  // the coverage of the paths of the passes about to run

    Device& device_;
    TextSource& text_;
    std::unique_ptr<Texture> white_;  // what quads that sample nothing bind
    bool whiteUploaded_ = false;
    std::vector<std::unique_ptr<Texture>> pages_;  // the glyph atlas's pages
    std::unordered_map<uint64_t, ImageTexture> images_;
    std::vector<std::unique_ptr<Texture>> retired_;  // bitmaps' textures drawn again: gone after the frame (its draws read them)
    std::vector<PooledTexture> pool_;  // layer targets
    std::unordered_map<uint64_t, KeptLayer> kept_;
    std::optional<std::pair<uint64_t, bool>> nextLayer_;  // keyLayer's, for the next pushLayer
    LayerContent pushed_ = LayerContent::Draw;
    // A pooled texture taken out of the pool (to keep), and one given back (still in use this frame).
    std::unique_ptr<Texture> takeFromPool(Texture* texture);
    void giveToPool(std::unique_ptr<Texture> texture);
    struct PipelineKey {
        Format format;
        BlendState blend;
        bool mesh;  // vsMesh's triangles, not vs's quads
        ShaderVariant variant;
        int how;  // how the coverage goes in (the blend plan's)
        bool operator==(const PipelineKey&) const = default;
    };
    std::vector<std::pair<PipelineKey, std::unique_ptr<Pipeline>>> pipelines_;
    std::unique_ptr<Texture> dstCopy_;  // a target's pixels under a draw whose blend mode reads them
    std::unique_ptr<Pipeline> coveragePipeline_;
    std::unique_ptr<Pipeline> effectPipeline_;
    MaskShelf shelf_;  // (started over once no pass is open: a clip's row lives no longer than its target)
    std::vector<std::unique_ptr<Texture>> masks_;  // a generation's coverage mask each, as big as it needed so far
    // Gradients' stops this frame, a row each (see shaders/primitives.slang), and their texture.
    static constexpr int kStopsWidth = 256;  // texels: 128 stops
    std::vector<float> stops_;
    std::vector<Brush> stopBrushes_;
    std::vector<int> stopRows_;  // each one's row
    // Image paints' textures this frame (an image shader's, a shader baked), as paint.z indexes them.
    struct PaintTexture {
        Texture* texture;
        Sampler sampler;
    };
    std::vector<PaintTexture> paintTextures_;
    // (`before`: what the point the shader is asked for goes through to be this target's px
    // first: a mesh's texture coordinates, in dp.)
    Prim paintedShader(const Shader& shader, const Rect& area, float alpha, float enlarged, const Projective* before = nullptr);
    Prim bakedShader(const Shader& shader, const Rect& area, float alpha, float enlarged, const Projective* before = nullptr);
    int shaderRow(const std::vector<ColorStop>* stops, const Projective& map);
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
    Projective windowFromTarget_;  // where the current target's pixels are in the window's (the light's)
    std::vector<Clip> clips_;
    std::vector<Placement> placements_;  // pushTranslation's and pushTransform's
    std::vector<ClipChain> clipChains_;  // this frame's
    std::vector<BlendMode> blends_;       // pushBlendMode's in this target, the innermost last
    std::vector<bool> antiAlias_;         // pushAntiAlias's in this target
    bool aliased() const { return !antiAlias_.empty() && !antiAlias_.back(); }
    std::vector<ColorFilter> filters_;    // pushColorFilter's in this target, the innermost last
    std::vector<std::vector<ColorFilter>> filterChains_;  // this frame's, a table row each (after the stops')
    std::vector<int> filterRows_;
};

} // namespace kompoz::gpu
