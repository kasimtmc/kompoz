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
    bool quickReject(const Rect& rect) const override;
    void drawPath(const Path& path, Offset origin, const Brush& brush, const Rect& brushArea, const PathStroke& stroke) override;
    void drawShadow(const Rect& rect, const CornerRadii& radii, const Shadow& shadow, bool inner) override;

private:
    // One quad: what shaders/primitives.slang's Instance says.
    struct Prim {
        float bounds[4]{}, shape[4]{}, radii[4]{}, color[4]{}, params[4]{}, uv[4]{};
        float xform[4]{1, 0, 0, 1}, extra[4]{};
    };
    struct Batch {
        Texture* texture;
        bool opaque;  // written as it is, not blended
        IRect scissor;
        uint32_t first, count;
    };
    struct Pass {
        Texture* target;
        std::optional<ClearColor> clear;
        std::vector<Prim> prims;
        std::vector<Batch> batches;
    };
    // A target being drawn into: its pass, and the drawing state outside it.
    struct Target {
        size_t pass;
        float ox, oy;  // the pixel origin
        std::vector<IRect> clips;
        std::vector<Offset> translations;
    };
    struct Layer {
        Texture* texture;  // null: too big, drawn straight into the target
        IRect rect;        // in the parent's pixels, whole ones (where the texture goes)
        Rect shape;        // the exact bounds there (px), which rounded corners follow
        CornerRadii radii;  // px
        float alpha;
        LayerTransform transform;
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

    void add(int kind, const Prim& prim, Texture* texture = nullptr, bool opaque = false);
    void glyphs(Color color);
    Texture* pageTexture(int page);
    Texture* imageTexture(const ImageBitmap& image);
    Texture* acquire(int width, int height);
    IRect scissor() const;
    Pipeline& pipeline(Format target, bool opaque);
    void execute(Pass& pass);

    Device& device_;
    TextSource& text_;
    std::unique_ptr<Texture> white_;  // what quads that sample nothing bind
    bool whiteUploaded_ = false;
    std::vector<std::unique_ptr<Texture>> pages_;  // the glyph atlas's pages
    std::unordered_map<uint64_t, ImageTexture> images_;
    std::vector<PooledTexture> pool_;  // layer targets
    std::vector<std::pair<std::pair<Format, bool>, std::unique_ptr<Pipeline>>> pipelines_;
    std::vector<Pass> passes_;
    std::vector<size_t> order_;  // ended passes, in the order they run
    std::vector<Target> targets_;  // the targets being drawn into, innermost last
    std::vector<Layer> layers_;
    std::vector<GlyphQuad> glyphs_;
    float ox_ = 0, oy_ = 0;  // the current target's pixel origin
    std::vector<IRect> clips_;
    std::vector<Offset> translations_;
};

} // namespace kompoz::gpu
