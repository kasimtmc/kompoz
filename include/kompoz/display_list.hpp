#pragma once

// A frame's drawing, recorded, so that the next frame can tell which pixels changed and only
// those are drawn again; a frame that changes nothing on the screen isn't drawn at all (an
// animation scrolled out of view, a recomposition that looks the same).
//
//   list.begin(viewport, onePixel);
//   host.draw(list);                       // records
//   if (list.end())                        // something differs from the last frame
//       for (Rect area : list.areas())     // where (dp)
//           list.replay(canvas, area);     // into the kept frame, clipped to the area
//
// Each command knows the screen area it can touch: its own bounds through the transforms of
// the layers around it, cut by their clips, padded for anti-aliasing. Frames are lined up by
// the layout nodes that drew them (GraphicsContext::beginNode), so content that appears or
// goes doesn't make everything after it look changed: what a node drew (before its first
// child, after each child) is compared with what it drew last time, and the areas of what
// differs, before and after, are the damage.
//
// Where the kept frame's pixels can be moved (allowMoves), what moved by whole pixels within a
// rect clip, as a scrolled list does, moves them (moves(), before the areas) instead of being
// drawn again: only what that leaves wrong is, the strip that came into view and what didn't move
// with it.
//
// Where nodes are kept (keepNodes), a node whose drawing didn't change since the last frame
// (recordNode's "unchanged") isn't drawn again: what it drew last frame is recorded again, moved
// as far as the node moved (a list scrolled), exactly as drawing it would record it. What its
// drawing left out or asked (what was clipped away, quickReject's answers) is recorded with it and
// asked again there: where any of that comes out otherwise (something scrolled into view), or
// it can't be moved so (a transform, the light of a shadow), the node is drawn.

#include "graphics.hpp"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace kompoz {

class DisplayList final : public GraphicsContext {
public:
    explicit DisplayList(TextMeasurer& measurer) : measurer_(measurer) {}

    // Starts recording a frame of `viewport` (dp); `pixel` is a pixel's size in dp.
    void begin(Size viewport, float pixel);
    // Ends it: the area whose pixels differ from the last frame's (dp), or nothing. That is
    // areas()'s bounds; with moves allowed, what moves() leaves to draw.
    std::optional<Rect> end();
    // Pixels of the kept frame to move before areas are drawn again: content that moved by whole
    // pixels within a rect clip, as a scrolled list's does. From `from` (dp, whole pixels) by `by`.
    struct Move {
        Rect from;
        Offset by;
    };
    // Lets end() find moves (off at first): the canvas the frame is kept on can move its pixels.
    void allowMoves(bool on) { allowMoves_ = on; }
    // Keeps what nodes drew from one frame to the next, for recordNode (off at first).
    void keepNodes(bool on) { keepNodes_ = on; }
    // In the last recording, the nodes recordNode kept (with all in them) and those it had drawn.
    int keptNodes() const { return keptNodes_; }
    int drawnNodes() const { return drawnNodes_; }
    // The last end()'s damage as separate rects (dp), each to be drawn again clipped to itself
    // (after the moves), and its moves.
    const std::vector<Rect>& areas() const { return areas_; }
    const std::vector<Move>& moves() const { return moves_; }
    // Draws the last recorded frame into `canvas`: what touches `area`, or everything. What comes
    // before command `hidden` (opaqueCover's) is left out but its state.
    void replay(GraphicsContext& canvas, const std::optional<Rect>& area = std::nullopt, size_t hidden = 0) const;
    // The last recorded frame's last command that paints all of `area` (dp) opaquely: a solid
    // opaque rect, square to the screen, under nothing but translations, scales and rect clips.
    // Nothing before it shows in `area` (replay leaves it out); none if no command covers it.
    std::optional<size_t> opaqueCover(const Rect& area) const;
    // The next frame is damaged all over (the kept pixels were lost).
    void invalidate() { invalid_ = true; }
    size_t size() const { return frame_.size(); }  // commands in the last frame
    // The last frame recorded the same as `other`'s (where things are, within a thousandth of a
    // dp): what kept nodes recorded is what drawing them would have (KOMPOZ_KEEP_CHECK). If not,
    // `why` says where.
    bool sameFrame(const DisplayList& other, std::string* why = nullptr) const;

    NodeRecording recordNode(const void* node, Offset origin, bool unchanged) override;
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
    void pushAntiAlias(bool on) override;
    void popAntiAlias() override;
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
    enum class Kind : uint8_t { FillRoundRect, StrokeRoundRect, Line, Text, TextBrush, TextShadow, Shadow, Arc, Image, Gradient, Path,
                                PushLayer, PopLayer, PushClip, PopClip, PushTranslation, PopTranslation, PushBlend, PopBlend,
                                ShadowPath, PushFilter, PopFilter, PushTransform, PopTransform, Backdrop, ElevationShadow, ShapeShadow,
                                Vertices, ImageMask, PushAlias, PopAlias };
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
        uint32_t parent = kNone;  // the push it is drawn in (the innermost)
        uint64_t layerKey = 0;    // a layer's: its node's, the same from frame to frame
        bool layerSame = false;   // a layer's: what is drawn in it is what was last frame
        bool same(const Command& o) const;
        // The same as `o` moved by `by` (in its own space), but for its bounds (a clip may cut them);
        // `withTransform`: drawn through the same transform too.
        bool sameMoved(const Command& o, Offset by, bool withTransform = true) const;
        // Where it is (in its own space), to tell how far it moved; none if it has no place.
        std::optional<Offset> place() const;
    };
    static constexpr uint32_t kNone = UINT32_MAX;
    struct Segment {  // what one node drew between its children
        uint64_t key;
        uint32_t first, count;
    };
    struct State {  // a layer's, clip's, translation's or transform's
        Projective m;
        Rect clip;
        uint32_t push;  // its command
        // The same but for the layers' own transforms (as they are at rest): what is drawn in a
        // layer is kept if it shows either way, so that what a layer holds is what its content
        // is, not how far its transform takes it (a stretch's, growing and shrinking).
        Projective restM;
        Rect restClip;
    };
    struct OpenNode {
        uint64_t key;
        uint64_t after = 0;  // the child the stretch being drawn follows (its key)
    };

    Command& add(Kind kind);
    // Records a draw command touching `local` (padded by `pad` dp), unless that is clipped away.
    Command* draw(Kind kind, const Rect& local, float pad);
    // What reaches `reach` (dp where drawn) shows on the screen, now or at rest (draw()'s test).
    bool shows(const Rect& reach) const;
    // Records text (any kind of it) whose glyphs may reach `blur` further.
    Command* text(Kind kind, std::string_view utf8, Offset baseline, const Font& font, bool rtl, float blur = 0);
    void pop(Kind kind);
    // `c` drawn into `canvas` moved by `by` (its place, as sameMoved says it; replay()'s, keep()'s).
    static void replayCommand(GraphicsContext& canvas, const Command& c, Offset by);
    // What happened in a recording besides its commands, before command `at`: a node begun (its
    // range) or ended, something it drew clipped away (its reach, dp where drawn), quickReject
    // asked (the rect, its answer), the light's center asked (what is drawn from it doesn't move
    // with the node).
    struct Event {
        enum Type : uint8_t { Begin, End, Culled, Rejected, Accepted, Light };
        Type type;
        uint32_t at;
        uint32_t range = 0;  // Begin's
        const void* node = nullptr;
        Rect rect;
    };
    // A node's part of a recording: its events and commands, Begin to End; where it was (its top
    // left, if recordNode said: else it isn't kept) and the state it was drawn in.
    struct Range {
        uint64_t key;
        uint32_t firstEvent, endEvent = 0, firstCommand, endCommand = 0;
        std::optional<Offset> origin;
        State entry;
        uint32_t firstSegment = 0, endSegment = 0, endRange = 0;  // (its nodes' ranges: it and the next up to endRange)
    };
    // Last frame's range `index` recorded again, moved by `by`, if what it left out and asked comes
    // out as it did; if not, nothing is recorded.
    bool keep(uint32_t index, Offset by);
    // The same not moved, drawn in the same state: its commands, segments, events and ranges as
    // they were (but where they go in this recording, and what images hold now).
    void copyKept(uint32_t index);
    // A node drawn in a range copied whole this frame (it isn't in occurrences_ for it): 1, else 0.
    uint32_t keptBefore(const void* node) const;
    Offset lightCenterHere() const;  // lightCenter(), not told
    // What `count` commands from `first` touch on the screen, as one rect (none if nothing).
    static void addDamage(std::vector<Rect>& damage, const std::vector<Command>& commands, uint32_t first, uint32_t count);
    // How far all of segment `s` moved (on the screen) from what last frame's `p` drew: the same
    // commands but for their place, all moved alike.
    std::optional<Offset> movedBy(const Segment& s, const Segment& p) const;
    // Of the segments that moved, those that moved together within a rect clip become moves
    // (their pixels moved, not drawn again: damage only where that leaves the frame wrong); the
    // rest are damage where they were and are.
    void settleMoves(std::vector<Rect>& damage);
    // Whether what this frame's layer `push` draws is what last frame's layer `last` drew, in the
    // layer's own space (moved with it, through its transform).
    bool sameInLayer(uint32_t push, uint32_t last) const;
    // The last command before `limit` in `commands` that paints all of `area` opaquely (opaqueCover).
    static std::optional<size_t> coverBefore(const std::vector<Command>& commands, size_t limit, const Rect& area, float pixel, Size screen);

    TextMeasurer& measurer_;
    std::vector<Command> recording_, frame_;
    std::vector<Segment> recordingSegments_, frameSegments_;
    std::vector<State> states_;
    std::vector<OpenNode> nodes_;
    // Numbers by key (a node, a segment's or layer's key), filled again every frame: open addressing,
    // at most half full, emptied without being freed (no allocation an entry, as a map's).
    class IndexTable {
    public:
        void clear();
        uint32_t& operator[](uint64_t key);  // (0 if it wasn't there)
        void emplace(uint64_t key, uint32_t value);  // unless it is there
        const uint32_t* find(uint64_t key) const;

    private:
        struct Slot {
            uint64_t key = 0;
            uint32_t value = 0;
            bool used = false;
        };
        size_t index(uint64_t key) const;
        std::vector<Slot> slots_;
        size_t used_ = 0;
    };
    IndexTable occurrences_;  // how many times each node was drawn so far in the recording
    IndexTable lookup_;  // end(): the last frame's segments by key
    IndexTable layerLookup_;  // and its layers
    std::vector<char> matched_;
    // end(), finding moves: the segments that moved (this frame's, last frame's, by how far);
    // which commands moved (1, then 2 + the move they are in), this frame's and last frame's.
    struct MovedSegment {
        uint32_t segment, last;
        Offset by;
    };
    std::vector<MovedSegment> moved_;
    std::vector<uint32_t> group_, lastGroup_;
    std::vector<Rect> areas_;
    std::vector<Move> moves_;
    bool allowMoves_ = false;
    bool keepNodes_ = false;
    int keptNodes_ = 0, drawnNodes_ = 0;
    mutable std::vector<Event> recordingEvents_;  // (quickReject and lightCenter tell them too)
    std::vector<Event> frameEvents_;
    std::vector<Range> recordingRanges_, frameRanges_;
    std::vector<uint32_t> openRanges_;  // the ranges being recorded, innermost last
    IndexTable rangeLookup_;  // the last frame's ranges by key
    std::optional<Offset> nextOrigin_;  // recordNode's, for the range beginNode starts
    std::vector<const void*> keptBegun_;  // (keep()'s: the nodes it began)
    std::vector<std::pair<uint32_t, uint32_t>> copiedRanges_;  // this frame's copyKept: last frame's ranges, from, to
    uint32_t layersInSegment_ = 0;  // (the layers pushed in the segment being recorded so far)
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
// What was drawn into it with no renderer there, waiting for one (for tests).
std::vector<std::shared_ptr<const DisplayList>> pending(const ImageBitmap& bitmap);
} // namespace bitmaps

} // namespace detail

} // namespace kompoz
