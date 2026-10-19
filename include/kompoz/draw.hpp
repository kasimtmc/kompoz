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
struct TextStyle;

class GraphicsLayer;
class DrawScope;

// How clipPath clips (Compose's ClipOp): to what is inside the path, or to what is outside it.
enum class ClipOp { Intersect, Difference };

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
    void clipRect(Offset topLeft, Size size);
    void clipPath(const Path& path, ClipOp op = ClipOp::Intersect);

private:
    friend class DrawScope;
    using Step = std::function<void(DrawScope&, const std::function<void(DrawScope&)>&)>;
    std::vector<Step> steps_;
};

class DrawScope {
public:
    DrawScope(GraphicsContext& context, Rect bounds, TextMeasurer* measurer = nullptr)
        : context_(&context), bounds_(bounds), measurer_(measurer) {}

    Size size() const { return {bounds_.width, bounds_.height}; }
    Offset center() const { return {bounds_.width / 2, bounds_.height / 2}; }
    GraphicsContext& context() const { return *context_; }
    Rect bounds() const { return bounds_; }  // absolute, for GraphicsContext calls

    // Unset sizes and positions cover the whole area. `blendMode`: how it goes onto what is drawn
    // already (inside a graphicsLayer with CompositingStrategy::Offscreen: the layer's content).
    // SDL draws SrcOver, DstIn, DstOut, Clear, Src, Dst and Plus; the others as SrcOver.
    void drawRect(Color color, Offset topLeft = {}, std::optional<Size> size = {}, DrawStyle style = Fill,
                  BlendMode blendMode = BlendMode::SrcOver);
    void drawRoundRect(Color color, float cornerRadius, Offset topLeft = {}, std::optional<Size> size = {},
                       DrawStyle style = Fill, BlendMode blendMode = BlendMode::SrcOver);
    // Defaults: the largest circle that fits, centered.
    void drawCircle(Color color, std::optional<float> radius = {}, std::optional<Offset> center = {},
                    DrawStyle style = Fill, BlendMode blendMode = BlendMode::SrcOver);
    // Gradients spread over the whole drawing area, as in Compose.
    void drawRect(const Brush& brush, Offset topLeft = {}, std::optional<Size> size = {}, BlendMode blendMode = BlendMode::SrcOver);
    void drawRoundRect(const Brush& brush, float cornerRadius, Offset topLeft = {}, std::optional<Size> size = {},
                       BlendMode blendMode = BlendMode::SrcOver);
    void drawCircle(const Brush& brush, std::optional<float> radius = {}, std::optional<Offset> center = {},
                    BlendMode blendMode = BlendMode::SrcOver);
    void drawPath(const Path& path, Color color, DrawStyle style = Fill, BlendMode blendMode = BlendMode::SrcOver);
    void drawPath(const Path& path, const Brush& brush, DrawStyle style = Fill, BlendMode blendMode = BlendMode::SrcOver);
    void drawLine(Color color, Offset start, Offset end, float strokeWidth = 1);
    void drawLine(Color color, Offset start, Offset end, float strokeWidth, StrokeCap cap, std::optional<PathEffect> pathEffect = {});
    void drawPoints(const std::vector<Offset>& points, PointMode mode, Color color, float strokeWidth = 1,
                    StrokeCap cap = StrokeCap::Butt, std::optional<PathEffect> pathEffect = {});
    // Angles in degrees: 0 = 3 o'clock, clockwise. With Fill (useCenter) it draws a pie slice.
    // The oval in a rect (Compose's drawOval; unset: the whole area).
    void drawOval(Color color, Offset topLeft = {}, std::optional<Size> size = {}, DrawStyle style = Fill,
                  BlendMode blendMode = BlendMode::SrcOver);
    void drawOval(const Brush& brush, Offset topLeft = {}, std::optional<Size> size = {}, DrawStyle style = Fill,
                  BlendMode blendMode = BlendMode::SrcOver);
    void drawArc(Color color, float startAngle, float sweepAngle, Offset topLeft = {}, std::optional<Size> size = {},
                 DrawStyle style = Fill);
    void drawText(std::string_view text, Offset topLeft, Font font = {}, Color color = colors::Black);
    // Text laid out by a TextLayoutMeasurer (text_measure.hpp): all its lines, styles and
    // alignment, at topLeft; `color` paints all of it instead of its own colors.
    void drawText(const TextLayoutResult& layout, Offset topLeft = {}, std::optional<Color> color = {});
    void drawText(const TextLayoutMeasurer& measurer, const std::string& text, Offset topLeft, const TextStyle& style);
    Size measureText(std::string_view text, Font font = {}) const;
    void drawImage(const ImageBitmap& image, Offset topLeft = {}, std::optional<Size> size = {}, float alpha = 1,
                   std::optional<ColorFilter> colorFilter = {});
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
    void clipRect(Offset topLeft, Size size, const std::function<void(DrawScope&)>& block);
    void rotate(float degrees, const std::function<void(DrawScope&)>& block, std::optional<Offset> pivot = {});
    void rotateRad(float radians, const std::function<void(DrawScope&)>& block, std::optional<Offset> pivot = {});
    void scale(float scaleX, float scaleY, const std::function<void(DrawScope&)>& block,
               std::optional<Offset> pivot = {});
    void scale(float scale, const std::function<void(DrawScope&)>& block, std::optional<Offset> pivot = {}) {
        this->scale(scale, scale, block, pivot);
    }
    // What `block` draws, only inside `path` (or outside it: ClipOp::Difference), any path, its
    // edge antialiased: drawn in a layer, then what is outside (inside) the path erased (DstOut).
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
    friend class DrawTransform;

    GraphicsContext* context_;
    Rect bounds_;
    TextMeasurer* measurer_;
};

// drawWithContent: decide where the content goes among your own drawing.
class ContentDrawScope : public DrawScope {
public:
    ContentDrawScope(GraphicsContext& context, Rect bounds, TextMeasurer* measurer, const std::function<void()>& content)
        : DrawScope(context, bounds, measurer), content_(&content) {}
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
    struct Properties {
        Offset topLeft;  // where drawLayer puts it in its scope
        float alpha = 1, scaleX = 1, scaleY = 1, rotationZ = 0, translationX = 0, translationY = 0;
        Offset pivot{0.5f, 0.5f};  // what it scales and turns about, a fraction of its size
    };
    GraphicsLayer();
    Properties& properties() const;  // change it any time: the next drawLayer uses it
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
