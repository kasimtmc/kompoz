#pragma once

// Custom drawing, like Compose's DrawScope: Canvas(), Modifier::drawBehind and
// Modifier::drawWithContent. Coordinates are local to the drawn area, in dp.
//
//   Canvas(Modifier().size(120), [](DrawScope& draw) {
//       draw.drawCircle(Color::hex(0x6750A4));
//       draw.drawArc(colors::White, -90, 270, {}, {}, Stroke(8, StrokeCap::Round));
//   });

#include "layout.hpp"

#include <optional>
#include <string_view>

namespace kompoz {

// drawPoints(): each point a dot (round with StrokeCap::Round, else square), every two a line,
// or all of them one line.
enum class PointMode { Points, Lines, Polygon };

class TextLayoutResult;
class TextLayoutMeasurer;
class AnnotatedString;
struct TextStyle;
enum class TextOverflow;
enum class TextDecoration;

class GraphicsLayer;
class DrawScope;

// What withTransform does before the drawing, in order, each inside the one before (Compose's
// DrawTransform): withTransform([](DrawTransform& t) { t.translate(10, 0); t.rotate(45); }, ...).
class DrawTransform {
public:
    void translate(float left, float top);
    void rotate(float degrees, std::optional<Offset> pivot = {});
    void rotateRad(float radians, std::optional<Offset> pivot = {});
    void scale(float scaleX, float scaleY, std::optional<Offset> pivot = {});
    void scale(float scale, std::optional<Offset> pivot = {}) { this->scale(scale, scale, pivot); }
    // Through `matrix` (from the drawing's coordinates to the scope's, its top left the origin).
    void transform(const Matrix& matrix);
    void inset(float left, float top, float right, float bottom);
    void inset(float horizontal, float vertical) { inset(horizontal, vertical, horizontal, vertical); }
    void inset(float all) { inset(all, all, all, all); }
    void clipRect(Offset topLeft, Size size, ClipOp op = ClipOp::Intersect);
    void clipPath(const Path& path, ClipOp op = ClipOp::Intersect);

private:
    friend class DrawScope;
    using Step = std::function<void(DrawScope&, const std::function<void(DrawScope&)>&)>;
    std::vector<Step> steps_;
};

class DrawScope {
public:
    DrawScope(GraphicsContext& context, Rect bounds, TextMeasurer* measurer = nullptr, Density density = {},
              LayoutDirection layoutDirection = LayoutDirection::Ltr)
        : context_(&context), bounds_(bounds), measurer_(measurer), density_(density), layoutDirection_(layoutDirection) {}
    // Over a node: its text measurer, the screen's density, its layout direction.
    DrawScope(GraphicsContext& context, Rect bounds, const LayoutNode& node);

    Size size() const { return {bounds_.width, bounds_.height}; }
    Offset center() const { return {bounds_.width / 2, bounds_.height / 2}; }
    GraphicsContext& context() const { return *context_; }
    Rect bounds() const { return bounds_; }  // absolute, for GraphicsContext calls
    // The px in a dp (and the font scale), and the direction it is laid out in (Compose's
    // DrawScope is a Density and has a layoutDirection; its lengths here are dp already).
    Density density() const { return density_; }
    LayoutDirection layoutDirection() const { return layoutDirection_; }

    // As Compose's: each in a color or a brush (a gradient spread over the whole area, as in
    // Compose), faded by `alpha`, filled or outlined (`style`: Fill, Stroke(...)), its colors
    // through `colorFilter`, onto what is drawn already by `blendMode` (inside a graphicsLayer
    // with CompositingStrategy::Offscreen: the layer's content; SDL draws SrcOver, DstIn, DstOut,
    // Clear, Src, Dst and Plus, the others as SrcOver). An unset size reaches from topLeft to the
    // area's bottom right; an unset center is its middle. Stroke widths of 0 are hairlines.
    void drawLine(const Brush& brush, Offset start, Offset end, float strokeWidth = 0, StrokeCap cap = StrokeCap::Butt,
                  std::optional<PathEffect> pathEffect = {}, float alpha = 1, std::optional<ColorFilter> colorFilter = {},
                  BlendMode blendMode = BlendMode::SrcOver);
    void drawLine(Color color, Offset start, Offset end, float strokeWidth = 0, StrokeCap cap = StrokeCap::Butt,
                  std::optional<PathEffect> pathEffect = {}, float alpha = 1, std::optional<ColorFilter> colorFilter = {},
                  BlendMode blendMode = BlendMode::SrcOver);
    void drawRect(const Brush& brush, Offset topLeft = {}, std::optional<Size> size = {}, float alpha = 1, const DrawStyle& style = Fill,
                  std::optional<ColorFilter> colorFilter = {}, BlendMode blendMode = BlendMode::SrcOver);
    void drawRect(Color color, Offset topLeft = {}, std::optional<Size> size = {}, float alpha = 1, const DrawStyle& style = Fill,
                  std::optional<ColorFilter> colorFilter = {}, BlendMode blendMode = BlendMode::SrcOver);
    // At its size in pixels (as dp), or the part of it from srcOffset, srcSize pixels (unset: to
    // its far corner) into the rect from dstOffset, dstSize dp (unset: srcSize's), sampled as
    // filterQuality says. A stroke style draws it as a fill does (as Android's).
    void drawImage(const ImageBitmap& image, Offset topLeft = {}, float alpha = 1, const DrawStyle& style = Fill,
                   std::optional<ColorFilter> colorFilter = {}, BlendMode blendMode = BlendMode::SrcOver);
    void drawImage(const ImageBitmap& image, IntOffset srcOffset, std::optional<IntSize> srcSize = {}, Offset dstOffset = {},
                   std::optional<Size> dstSize = {}, float alpha = 1, const DrawStyle& style = Fill, std::optional<ColorFilter> colorFilter = {},
                   BlendMode blendMode = BlendMode::SrcOver, FilterQuality filterQuality = DefaultFilterQuality);
    // Corners quarter ellipses of cornerRadius (quarter circles when its x and y are the same).
    // (In a color, as Compose's: its style before its alpha.)
    void drawRoundRect(const Brush& brush, Offset topLeft = {}, std::optional<Size> size = {}, CornerRadius cornerRadius = {},
                       float alpha = 1, const DrawStyle& style = Fill, std::optional<ColorFilter> colorFilter = {},
                       BlendMode blendMode = BlendMode::SrcOver);
    void drawRoundRect(Color color, Offset topLeft = {}, std::optional<Size> size = {}, CornerRadius cornerRadius = {},
                       const DrawStyle& style = Fill, float alpha = 1, std::optional<ColorFilter> colorFilter = {},
                       BlendMode blendMode = BlendMode::SrcOver);
    // Unset radius: the largest circle that fits.
    void drawCircle(const Brush& brush, std::optional<float> radius = {}, std::optional<Offset> center = {}, float alpha = 1,
                    const DrawStyle& style = Fill, std::optional<ColorFilter> colorFilter = {}, BlendMode blendMode = BlendMode::SrcOver);
    void drawCircle(Color color, std::optional<float> radius = {}, std::optional<Offset> center = {}, float alpha = 1,
                    const DrawStyle& style = Fill, std::optional<ColorFilter> colorFilter = {}, BlendMode blendMode = BlendMode::SrcOver);
    // The oval in a rect.
    void drawOval(const Brush& brush, Offset topLeft = {}, std::optional<Size> size = {}, float alpha = 1, const DrawStyle& style = Fill,
                  std::optional<ColorFilter> colorFilter = {}, BlendMode blendMode = BlendMode::SrcOver);
    void drawOval(Color color, Offset topLeft = {}, std::optional<Size> size = {}, float alpha = 1, const DrawStyle& style = Fill,
                  std::optional<ColorFilter> colorFilter = {}, BlendMode blendMode = BlendMode::SrcOver);
    // Part of the oval in a rect: from startAngle through sweepAngle (degrees, 0 at 3 o'clock,
    // clockwise); useCenter closes it through the center (a pie slice), else straight across
    // its ends (filled) or not at all (outlined).
    void drawArc(const Brush& brush, float startAngle, float sweepAngle, bool useCenter, Offset topLeft = {},
                 std::optional<Size> size = {}, float alpha = 1, const DrawStyle& style = Fill, std::optional<ColorFilter> colorFilter = {},
                 BlendMode blendMode = BlendMode::SrcOver);
    void drawArc(Color color, float startAngle, float sweepAngle, bool useCenter, Offset topLeft = {}, std::optional<Size> size = {},
                 float alpha = 1, const DrawStyle& style = Fill, std::optional<ColorFilter> colorFilter = {},
                 BlendMode blendMode = BlendMode::SrcOver);
    void drawPath(const Path& path, Color color, float alpha = 1, const DrawStyle& style = Fill, std::optional<ColorFilter> colorFilter = {},
                  BlendMode blendMode = BlendMode::SrcOver);
    void drawPath(const Path& path, const Brush& brush, float alpha = 1, const DrawStyle& style = Fill,
                  std::optional<ColorFilter> colorFilter = {}, BlendMode blendMode = BlendMode::SrcOver);
    // Each point a dot (round with StrokeCap::Round, else square), every two a line, or all of
    // them one line.
    void drawPoints(const std::vector<Offset>& points, PointMode pointMode, Color color, float strokeWidth = 0,
                    StrokeCap cap = StrokeCap::Butt, std::optional<PathEffect> pathEffect = {}, float alpha = 1,
                    std::optional<ColorFilter> colorFilter = {}, BlendMode blendMode = BlendMode::SrcOver);
    void drawPoints(const std::vector<Offset>& points, PointMode pointMode, const Brush& brush, float strokeWidth = 0,
                    StrokeCap cap = StrokeCap::Butt, std::optional<PathEffect> pathEffect = {}, float alpha = 1,
                    std::optional<ColorFilter> colorFilter = {}, BlendMode blendMode = BlendMode::SrcOver);
    // Text laid out by `measurer` (rememberTextMeasurer) with `style` at topLeft, in `size` (its
    // width the lines are aligned in; unset: as far as the area's bottom right), cut off as
    // overflow, softWrap and maxLines say where they are given (else as the style does), cut
    // to its box where it overflows it (unless TextOverflow::Visible) (Compose's drawText).
    void drawText(const TextLayoutMeasurer& measurer, const std::string& text, Offset topLeft = {});  // (in the default style)
    void drawText(const TextLayoutMeasurer& measurer, const std::string& text, Offset topLeft, const TextStyle& style,
                  std::optional<TextOverflow> overflow = {}, std::optional<bool> softWrap = {}, std::optional<int> maxLines = {},
                  std::optional<Size> size = {}, BlendMode blendMode = BlendMode::SrcOver);
    void drawText(const TextLayoutMeasurer& measurer, const AnnotatedString& text, Offset topLeft = {});  // (in the default style)
    void drawText(const TextLayoutMeasurer& measurer, const AnnotatedString& text, Offset topLeft, const TextStyle& style,
                  std::optional<TextOverflow> overflow = {}, std::optional<bool> softWrap = {}, std::optional<int> maxLines = {},
                  std::optional<Size> size = {}, BlendMode blendMode = BlendMode::SrcOver);
    // Text laid out already, at topLeft: `color` (or `brush`) paints what the text's own style
    // painted (spans and links keep theirs), faded by alpha (unset: as it is); shadow,
    // textDecoration and drawStyle, where given, go where the text's style set them.
    void drawText(const TextLayoutResult& layout, std::optional<Color> color = {}, Offset topLeft = {}, std::optional<float> alpha = {},
                  std::optional<Shadow> shadow = {}, std::optional<TextDecoration> textDecoration = {},
                  std::optional<DrawStyle> drawStyle = {}, BlendMode blendMode = BlendMode::SrcOver);
    void drawText(const TextLayoutResult& layout, const Brush& brush, Offset topLeft = {}, std::optional<float> alpha = {},
                  std::optional<Shadow> shadow = {}, std::optional<TextDecoration> textDecoration = {},
                  std::optional<DrawStyle> drawStyle = {}, BlendMode blendMode = BlendMode::SrcOver);
    // One line in one font at topLeft (its top), and how big it is (Kompoz's own: quick labels).
    void drawText(std::string_view text, Offset topLeft, Font font = {}, Color color = colors::Black);
    Size measureText(std::string_view text, Font font = {}) const;
    // A vector filled in `color` (one in paints of its own in them), in a rect.
    void drawVector(const ImageVector& vector, Color color, Offset topLeft = {}, std::optional<Size> size = {});
    // Records `block` into `layer` instead of drawing it here (Compose's GraphicsLayer.record):
    // what it draws — a ContentDrawScope's drawContent() too — from this scope's top left, in a
    // size of its own or this scope's. drawLayer() draws the recording, as often as wanted.
    void record(const GraphicsLayer& layer, const std::function<void(DrawScope&)>& block, std::optional<Size> size = {});
    // Draws what `layer` recorded at its topLeft, with its alpha, scale, rotation and translation.
    void drawLayer(const GraphicsLayer& layer);

    // Draws `block` moved, inset, clipped, rotated or scaled (about the center unless a pivot is
    // given), as Compose does: what it draws — drawContent() too — goes through the transform, and
    // isn't clipped to this area.
    void translate(float left, float top, const std::function<void(DrawScope&)>& block);
    void inset(float left, float top, float right, float bottom, const std::function<void(DrawScope&)>& block);
    void inset(float all, const std::function<void(DrawScope&)>& block) { inset(all, all, all, all, block); }
    void inset(float horizontal, float vertical, const std::function<void(DrawScope&)>& block) {
        inset(horizontal, vertical, horizontal, vertical, block);
    }
    void clipRect(Offset topLeft, Size size, const std::function<void(DrawScope&)>& block, ClipOp op = ClipOp::Intersect);
    void rotate(float degrees, const std::function<void(DrawScope&)>& block, std::optional<Offset> pivot = {});
    void rotateRad(float radians, const std::function<void(DrawScope&)>& block, std::optional<Offset> pivot = {});
    void scale(float scaleX, float scaleY, const std::function<void(DrawScope&)>& block,
               std::optional<Offset> pivot = {});
    void scale(float scale, const std::function<void(DrawScope&)>& block, std::optional<Offset> pivot = {}) {
        this->scale(scale, scale, block, pivot);
    }
    // What `block` draws, only inside `path` (or outside it: ClipOp::Difference), any path by its
    // fill type, its edge anti-aliased.
    void clipPath(const Path& path, const std::function<void(DrawScope&)>& block, ClipOp op = ClipOp::Intersect);
    // Several of the above at once (Compose's withTransform).
    void withTransform(const std::function<void(DrawTransform&)>& transform, const std::function<void(DrawScope&)>& block);

private:
    Rect local(Offset topLeft, std::optional<Size> size) const {
        Size s = size.value_or(Size{bounds_.width - topLeft.x, bounds_.height - topLeft.y});
        return {bounds_.x + topLeft.x, bounds_.y + topLeft.y, s.width, s.height};
    }
    Offset abs(Offset p) const { return {bounds_.x + p.x, bounds_.y + p.y}; }
    // Draws `block` through `matrix`, which is in this scope's coordinates (its top left the origin).
    void transformed(const Matrix& matrix, const std::function<void(DrawScope&)>& block);
    void paint(const Path& path, const Brush& brush, float alpha, const DrawStyle& style, const std::optional<ColorFilter>& colorFilter,
               BlendMode blendMode);
    void paintRoundRect(const Rect& rect, CornerRadius corner, const Brush& brush, float alpha, const DrawStyle& style,
                        const std::optional<ColorFilter>& colorFilter, BlendMode blendMode, bool oval = false);
    friend class DrawTransform;

    GraphicsContext* context_;
    Rect bounds_;
    TextMeasurer* measurer_;
    Density density_;
    LayoutDirection layoutDirection_ = LayoutDirection::Ltr;
};

namespace detail {
// How a layer is placed (graphicsLayer's, a GraphicsLayer's): scaled, turned in 2D or 3D, moved,
// about `pivot` (absolute dp); `density` px per dp (the camera's distance is in px, as Android's).
struct LayerGeometry {
    float scaleX = 1, scaleY = 1, translationX = 0, translationY = 0, rotationX = 0, rotationY = 0, rotationZ = 0, cameraDistance = 8;
    Offset pivot;
    float density = 1;
};
// The matrix it is drawn through, as Android's RenderNode makes it.
Matrix layerMatrix(const LayerGeometry& geometry);
// Draws `content` (over `bounds`) through such a layer: its matrix, its shadow, its clip to
// `outline` (from the bounds' top left), its alpha, blend mode, color filter and render effect,
// offscreen only where it has to be.
struct LayerDraw {
    Rect bounds;
    LayerGeometry geometry;
    float alpha = 1;
    float shadowElevation = 0;  // dp
    Color ambientShadowColor = DefaultShadowColor, spotShadowColor = DefaultShadowColor;
    bool clip = false;
    Outline outline;  // (its shadow's too)
    BlendMode blendMode = BlendMode::SrcOver;
    std::optional<ColorFilter> colorFilter;
    std::optional<RenderEffect> renderEffect;
    CompositingStrategy strategy = CompositingStrategy::Auto;
    LayerOutsets outsets;
};
void drawThroughLayer(GraphicsContext& g, const LayerDraw& layer, const std::function<void()>& content);
} // namespace detail

// drawWithContent: decide where the content goes among your own drawing.
class ContentDrawScope : public DrawScope {
public:
    ContentDrawScope(GraphicsContext& context, Rect bounds, TextMeasurer* measurer, const std::function<void()>& content)
        : DrawScope(context, bounds, measurer), content_(&content) {}
    ContentDrawScope(GraphicsContext& context, Rect bounds, const LayoutNode& node, const std::function<void()>& content)
        : DrawScope(context, bounds, node), content_(&content) {}
    void drawContent() const { (*content_)(); }

private:
    const std::function<void()>* content_;
};

// A drawing kept to draw again (Compose's GraphicsLayer): record into it in a draw scope, then
// drawLayer it — faded, scaled, turned, moved — anywhere, or turn it into pixels.
//   auto layer = rememberGraphicsLayer();
//   Box(Modifier().drawWithContent([layer](ContentDrawScope& d) {
//       d.record(layer, [&](DrawScope&) { d.drawContent(); });
//       d.drawLayer(layer);
//   }), ...);
//   ImageBitmap shot = layer.toImageBitmap();  // later, a "share" button
class GraphicsLayer {
public:
    // How drawLayer draws it (Compose's GraphicsLayer's properties), as graphicsLayer does its
    // content (see GraphicsLayerScope): offscreen only where it has to be.
    struct Properties {
        Offset topLeft;  // where drawLayer puts it in its scope
        float alpha = 1, scaleX = 1, scaleY = 1, translationX = 0, translationY = 0;
        float rotationX = 0, rotationY = 0, rotationZ = 0, cameraDistance = 8;
        std::optional<Offset> pivotOffset;  // what it scales and turns about, dp from its top left (unset: its middle)
        float shadowElevation = 0;          // dp: its outline's shadow (see GraphicsLayerScope)
        Color ambientShadowColor = DefaultShadowColor, spotShadowColor = DefaultShadowColor;
        // Clipped to its outline (unset: its bounds; setRectOutline, setRoundRectOutline, setPathOutline).
        bool clip = false;
        std::optional<Outline> outline;
        std::optional<RenderEffect> renderEffect;
        BlendMode blendMode = BlendMode::SrcOver;
        std::optional<ColorFilter> colorFilter;
        CompositingStrategy compositingStrategy = CompositingStrategy::Auto;
    };
    GraphicsLayer();
    Properties& properties() const;  // change it any time: the next drawLayer uses it
    // Its outline, from its top left (Compose's): a rect, a rounded rect, any path.
    void setRectOutline(Offset topLeft, Size size) const;
    void setRoundRectOutline(Offset topLeft, Size size, float cornerRadius) const;
    void setPathOutline(const Path& path) const;
    Size size() const;               // the last recording's
    bool empty() const;              // nothing recorded yet
    // The recording as pixels at the screen's density (straight alpha), drawn offscreen by the
    // platform; empty where it can't be (no renderer: tests). Compose's toImageBitmap, here
    // right away. (Image shows pixels as dp: give it a size.)
    ImageBitmap toImageBitmap() const;
    friend bool operator==(const GraphicsLayer& a, const GraphicsLayer& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;
};
GraphicsLayer rememberGraphicsLayer(CallSite site = CallSite::current());

// drawWithCache: build paths, brushes and other objects once per size, then draw them every frame.
//   Modifier().drawWithCache([](CacheDrawScope& cache) {
//       Path path = star(cache.size());
//       return cache.onDrawBehind([path](DrawScope& d) { d.drawPath(path, color); });
//   })
struct DrawResult {
    std::function<void(DrawScope&)> behind;
    std::function<void(ContentDrawScope&)> withContent;
};

class CacheDrawScope {
public:
    explicit CacheDrawScope(Size size) : size_(size) {}
    Size size() const { return size_; }
    DrawResult onDrawBehind(std::function<void(DrawScope&)> onDraw) const { return {std::move(onDraw), {}}; }
    DrawResult onDrawWithContent(std::function<void(ContentDrawScope&)> onDraw) const { return {{}, std::move(onDraw)}; }

private:
    Size size_;
};

// A space to draw in: Canvas(Modifier().size(100), [](DrawScope& draw) { ... });
void Canvas(const Modifier& modifier, std::function<void(DrawScope&)> onDraw, CallSite site = CallSite::current());

// Something drawn into whatever size it is given (Compose's Painter): a bitmap (BitmapPainter),
// a vector (VectorPainter, black unless a color filter tints it), a color, a brush or drawing
// of your own. Its intrinsic size (a bitmap's pixels and a vector's viewport, as dp; none for a
// color or a brush) sizes Image and Modifier.paint. The built-in ones are equal when what they
// draw is; custom ones only to themselves.
class Painter {
public:
    Painter() = default;  // draws nothing, has no size
    // Drawing of your own into `scope` (its size the painter's); an alpha below 1 goes through
    // an offscreen layer. Unspecified: no intrinsic size.
    static Painter custom(std::function<void(DrawScope&)> onDraw, std::optional<Size> intrinsicSize = {});

    bool hasIntrinsicSize() const;
    Size intrinsicSize() const;  // {0, 0} without one
    // Draws into `bounds` (absolute), faded by `alpha` and through `colorFilter`; `rtl`: a
    // vector that mirrors does.
    void draw(GraphicsContext& canvas, const Rect& bounds, float alpha = 1, const std::optional<ColorFilter>& colorFilter = {},
              bool rtl = false) const;
    void draw(DrawScope& scope, Size size, float alpha = 1, const std::optional<ColorFilter>& colorFilter = {}) const {
        draw(scope.context(), {scope.bounds().x, scope.bounds().y, size.width, size.height}, alpha, colorFilter);
    }
    friend bool operator==(const Painter& a, const Painter& b);

    struct Data;
    std::shared_ptr<const Data> data_;
};
Painter BitmapPainter(ImageBitmap image);
Painter VectorPainter(ImageVector image);
Painter ColorPainter(Color color);
Painter BrushPainter(Brush brush);  // (a color filter isn't applied to it)

} // namespace kompoz
