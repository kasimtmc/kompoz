#include <kompoz/display_list.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <numbers>

namespace kompoz {

namespace {

// What the images in a brush's shader hold (their ids and versions, summed up): one drawn into
// since is a change.
uint64_t imagesIn(const Brush& brush, const Rect& area) {
    if (brush.kind() != Brush::Kind::Shader) return 0;
    std::function<uint64_t(const Shader&)> walk = [&](const Shader& s) -> uint64_t {
        if (s.kind() == Shader::Kind::Image) return s.image().id() * 1000003u + s.image().version();
        if (s.kind() == Shader::Kind::Composite) return walk(s.dst()) * 31u + walk(s.src());
        return 0;
    };
    return walk(brush.createShader({area.width, area.height}));
}

bool same(const Rect& a, const Rect& b) { return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height; }
bool same(Offset a, Offset b) { return a.x == b.x && a.y == b.y; }
bool same(const LayerTransform& a, const LayerTransform& b) {
    return a.scaleX == b.scaleX && a.scaleY == b.scaleY && a.rotationZ == b.rotationZ && a.translationX == b.translationX &&
           a.translationY == b.translationY && same(a.origin, b.origin);
}
bool same(const PathStroke& a, const PathStroke& b) {
    return a.width == b.width && a.cap == b.cap && a.join == b.join && a.miter == b.miter && a.hairline == b.hairline &&
           a.pathEffect == b.pathEffect;
}

bool empty(const Rect& r) { return r.width <= 0 || r.height <= 0; }
bool overlaps(const Rect& a, const Rect& b) { return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom(); }
Rect grown(const Rect& r, float by) { return {r.x - by, r.y - by, r.width + 2 * by, r.height + 2 * by}; }
Rect united(const Rect& a, const Rect& b) {
    float l = std::min(a.x, b.x), t = std::min(a.y, b.y);
    return {l, t, std::max(a.right(), b.right()) - l, std::max(a.bottom(), b.bottom()) - t};
}

uint64_t mix(uint64_t h, uint64_t v) { return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)); }
// A node's key in a recording: it, and the how many-th time it is drawn in it.
uint64_t nodeKey(const void* node, uint32_t occurrence) { return mix(mix(uint64_t(reinterpret_cast<uintptr_t>(node)), occurrence), 0x51ed27); }

float area(const Rect& r) { return empty(r) ? 0 : r.width * r.height; }
Rect moved(const Rect& r, Offset by) { return {r.x + by.x, r.y + by.y, r.width, r.height}; }
// Where things moved to are a thousandth of a dp off at most (whole px summed in floats).
bool near(float a, float b) { return std::abs(a - b) <= 1e-3f; }
bool near(Offset a, Offset b) { return near(a.x, b.x) && near(a.y, b.y); }
bool movedTo(Offset a, Offset o, Offset by) { return near(a.x, o.x + by.x) && near(a.y, o.y + by.y); }
bool movedTo(const Rect& a, const Rect& o, Offset by) { return movedTo(Offset{a.x, a.y}, Offset{o.x, o.y}, by) && a.width == o.width && a.height == o.height; }
// `r` added to `rects` (that don't overlap): made one with those it overlaps.
void join(std::vector<Rect>& rects, Rect r) {
    for (size_t i = 0; i < rects.size();)
        if (overlaps(rects[i], r)) {
            r = united(r, rects[i]);
            rects.erase(rects.begin() + ptrdiff_t(i));
            i = 0;  // (grown: it may reach those already passed)
        } else {
            ++i;
        }
    rects.push_back(r);
}

// What is left of `r` outside `hole` (a rect within it, or not), in up to four rects that don't
// overlap: the bands above and below it all across, those beside it between them.
void subtract(const Rect& r, const Rect& hole, std::vector<Rect>& out) {
    Rect h = hole.intersect(r);
    if (empty(h)) {
        if (!empty(r)) out.push_back(r);
        return;
    }
    auto add = [&](Rect piece) {
        if (!empty(piece)) out.push_back(piece);
    };
    add({r.x, r.y, r.width, h.y - r.y});
    add({r.x, h.bottom(), r.width, r.bottom() - h.bottom()});
    add({r.x, h.y, h.x - r.x, h.height});
    add({h.right(), h.y, r.right() - h.right(), h.height});
}

using Projective = DisplayList::Projective;

bool squareToScreen(const Projective& p) { return p.m[1] == 0 && p.m[3] == 0 && p.m[6] == 0 && p.m[7] == 0 && p.m[8] == 1; }

// A layer's compositing (as backends do it): scaled and rotated around its origin, then moved.
Projective layerMap(const Rect& bounds, const LayerTransform& t) {
    if (t.identity()) return {};
    float px = bounds.x + bounds.width * t.origin.x, py = bounds.y + bounds.height * t.origin.y;
    float rad = t.rotationZ * std::numbers::pi_v<float> / 180.f, cs = std::cos(rad), sn = std::sin(rad);
    float a = t.scaleX * cs, b = t.scaleX * sn, c = -t.scaleY * sn, d = t.scaleY * cs;
    return {{a, c, px + t.translationX - (a * px + c * py), b, d, py + t.translationY - (b * px + d * py), 0, 0, 1}};
}

// Matrix's plane part (as backends draw through it).
Projective planeMap(const Matrix& t) {
    const auto& v = t.values;
    return {{v[Matrix::ScaleX], v[Matrix::SkewX], v[Matrix::TranslateX], v[Matrix::SkewY], v[Matrix::ScaleY], v[Matrix::TranslateY],
             v[Matrix::Perspective0], v[Matrix::Perspective1], v[Matrix::Perspective2]}};
}

Projective compose(const Projective& a, const Projective& b) {  // a after b
    Projective out;
    for (int r = 0; r < 3; ++r)
        for (int c = 0; c < 3; ++c) out.m[r * 3 + c] = a.m[r * 3] * b.m[c] + a.m[r * 3 + 1] * b.m[3 + c] + a.m[r * 3 + 2] * b.m[6 + c];
    return out;
}

} // namespace

Rect DisplayList::Projective::map(const Rect& r) const {
    if (m[6] == 0 && m[7] == 0 && m[1] == 0 && m[3] == 0 && m[8] != 0) {  // only scaled and moved: no need for the corners
        float x0 = (m[0] * r.x + m[2]) / m[8], x1 = (m[0] * r.right() + m[2]) / m[8];
        float y0 = (m[4] * r.y + m[5]) / m[8], y1 = (m[4] * r.bottom() + m[5]) / m[8];
        return {std::min(x0, x1), std::min(y0, y1), std::abs(x1 - x0), std::abs(y1 - y0)};
    }
    bool affine = m[6] == 0 && m[7] == 0;  // (w the same everywhere: in front whatever its sign)
    float l = kInfinity, t = kInfinity, rr = -kInfinity, bb = -kInfinity;
    for (int c = 0; c < 4; ++c) {
        float x = c & 1 ? r.right() : r.x, y = c & 2 ? r.bottom() : r.y;
        float w = m[6] * x + m[7] * y + m[8];
        if (affine ? w == 0 : !(w > 0)) return {-1e30f, -1e30f, 2e30f, 2e30f};  // behind the eye
        float px = (m[0] * x + m[1] * y + m[2]) / w, py = (m[3] * x + m[4] * y + m[5]) / w;
        l = std::min(l, px), t = std::min(t, py), rr = std::max(rr, px), bb = std::max(bb, py);
    }
    return {l, t, rr - l, bb - t};
}

bool DisplayList::Command::same(const Command& o) const {
    return kind == o.kind && kompoz::same(bounds, o.bounds) && m == o.m && kompoz::same(rect, o.rect) &&
           kompoz::same(rect2, o.rect2) && radii == o.radii && f1 == o.f1 && f2 == o.f2 && f3 == o.f3 && color == o.color &&
           kompoz::same(p1, o.p1) && kompoz::same(p2, o.p2) && flag == o.flag && font == o.font &&
           image == o.image && imageVersion == o.imageVersion && kompoz::same(stroke, o.stroke) && kompoz::same(transform, o.transform) && text == o.text &&
           brush == o.brush && vertices == o.vertices && filter == o.filter && effect == o.effect && elevated == o.elevated && shaped == o.shaped && path == o.path &&
           matrix == o.matrix;
}

std::optional<Offset> DisplayList::Command::place() const {
    switch (kind) {
    case Kind::FillRoundRect: case Kind::StrokeRoundRect: case Kind::Arc: case Kind::Gradient: case Kind::Shadow: case Kind::ShapeShadow:
    case Kind::Image: case Kind::ImageMask: case Kind::PushLayer:
        return Offset{rect.x, rect.y};
    case Kind::PushClip: return f1 == 2 ? p1 : Offset{rect.x, rect.y};
    case Kind::Line: case Kind::Text: case Kind::TextBrush: case Kind::TextShadow: case Kind::Path: case Kind::ElevationShadow:
    case Kind::Vertices:
        return p1;
    case Kind::ShadowPath: return p2;
    default: return std::nullopt;
    }
}

// Which of its fields say where it is, by kind: its rect (a shape's, an image's destination, a
// layer's or clip's bounds), its brush's area (rect2, or p1 and the size in p2 for an image
// mask's), its first point (a baseline, an origin, a line's start) and its second (a line's end, a
// path shadow's origin). The rest (an image's source, a shadow's offset) stays as it was.
bool DisplayList::Command::sameMoved(const Command& o, Offset by, bool withTransform) const {
    if (kind != o.kind || kind == Kind::Backdrop || (withTransform && !(m == o.m))) return false;  // (what a backdrop reads moved too: not the same)
    if (kind == Kind::PushLayer && !(path.isEmpty() && o.path.isEmpty())) return false;  // (an outline's place isn't told)
    bool rectMoves = false, brushMoves = false, p1Moves = false, p2Moves = false;
    switch (kind) {
    case Kind::FillRoundRect: case Kind::StrokeRoundRect: case Kind::Arc: case Kind::Shadow: case Kind::ShapeShadow: case Kind::Image:
    case Kind::PushLayer:
        rectMoves = true;
        break;
    case Kind::Gradient: rectMoves = brushMoves = true; break;
    case Kind::ImageMask: rectMoves = p1Moves = true; break;
    case Kind::PushClip: (f1 == 2 ? p1Moves : rectMoves) = true; break;
    case Kind::Line: p1Moves = p2Moves = true; break;
    case Kind::Text: case Kind::TextShadow: case Kind::ElevationShadow: p1Moves = true; break;
    case Kind::TextBrush: case Kind::Path: case Kind::Vertices: p1Moves = brushMoves = true; break;
    case Kind::ShadowPath: p2Moves = true; break;
    default: break;
    }
    // A brush of one color is that wherever its area is (rect2; an image mask's, p1 and p2).
    bool solid = brush && brush->isSolid() && o.brush && o.brush->isSolid();
    bool anyArea = solid && brushMoves, anyMaskArea = solid && kind == Kind::ImageMask;
    return (rectMoves ? movedTo(rect, o.rect, by) : kompoz::same(rect, o.rect)) &&
           (anyArea || (brushMoves ? movedTo(rect2, o.rect2, by) : kompoz::same(rect2, o.rect2))) &&
           (anyMaskArea || (p1Moves ? movedTo(p1, o.p1, by) : kompoz::same(p1, o.p1))) &&
           (anyMaskArea || (p2Moves ? movedTo(p2, o.p2, by) : kompoz::same(p2, o.p2))) &&
           radii == o.radii && f1 == o.f1 && f2 == o.f2 && f3 == o.f3 && color == o.color && flag == o.flag && font == o.font &&
           image == o.image && imageVersion == o.imageVersion && kompoz::same(stroke, o.stroke) && kompoz::same(transform, o.transform) &&
           text == o.text && brush == o.brush && vertices == o.vertices && filter == o.filter && effect == o.effect && elevated == o.elevated &&
           shaped == o.shaped && path == o.path && matrix == o.matrix;
}

void DisplayList::IndexTable::clear() {
    std::fill(slots_.begin(), slots_.end(), Slot{});
    used_ = 0;
}

size_t DisplayList::IndexTable::index(uint64_t key) const {
    size_t mask = slots_.size() - 1;  // (a power of two)
    size_t i = size_t((key * 0x9E3779B97F4A7C15ull) >> 32) & mask;
    while (slots_[i].used && slots_[i].key != key) i = (i + 1) & mask;
    return i;
}

uint32_t& DisplayList::IndexTable::operator[](uint64_t key) {
    if (2 * (used_ + 1) > slots_.size()) {  // twice as many slots, the entries moved over
        std::vector<Slot> old(std::max<size_t>(256, 2 * slots_.size()));
        old.swap(slots_);
        for (const Slot& e : old)
            if (e.used) slots_[index(e.key)] = e;
    }
    Slot& slot = slots_[index(key)];
    if (!slot.used) slot = {key, 0, true}, ++used_;
    return slot.value;
}

void DisplayList::IndexTable::emplace(uint64_t key, uint32_t value) {
    if (!find(key)) (*this)[key] = value;
}

const uint32_t* DisplayList::IndexTable::find(uint64_t key) const {
    if (slots_.empty()) return nullptr;
    const Slot& slot = slots_[index(key)];
    return slot.used ? &slot.value : nullptr;
}

void DisplayList::begin(Size viewport, float pixel) {
    viewport_ = viewport;
    pixel_ = pixel;
    recording_.clear();
    recordingSegments_.clear();
    occurrences_.clear();
    recordingEvents_.clear();
    recordingRanges_.clear();
    openRanges_.clear();
    nextOrigin_.reset();
    keptNodes_ = drawnNodes_ = 0;
    states_.assign(1, {{}, {0, 0, viewport.width, viewport.height}, 0, {}, {0, 0, viewport.width, viewport.height}});
    nodes_.assign(1, {0x6b6f6d706f7aull});  // the root: what is drawn outside any node
    segmentOpen_ = false;
}

void DisplayList::beginNode(const void* node) {
    segmentOpen_ = false;
    uint32_t occurrence = occurrences_[reinterpret_cast<uintptr_t>(node)]++;  // a node drawn twice (a shared element's overlay)
    nodes_.push_back({nodeKey(node, occurrence)});
    if (!keepNodes_) return;
    openRanges_.push_back(uint32_t(recordingRanges_.size()));
    recordingRanges_.push_back({nodes_.back().key, uint32_t(recordingEvents_.size()), 0, uint32_t(recording_.size()), 0,
                                std::exchange(nextOrigin_, std::nullopt), states_.back()});
    recordingEvents_.push_back({Event::Begin, uint32_t(recording_.size()), openRanges_.back(), node, {}});
}

void DisplayList::endNode() {
    if (nodes_.size() <= 1) return;
    uint64_t child = nodes_.back().key;
    nodes_.pop_back();
    nodes_.back().after = child;  // what the parent draws next follows this child: a child added before it leaves that alone
    segmentOpen_ = false;
    if (!keepNodes_ || openRanges_.empty()) return;
    Range& r = recordingRanges_[openRanges_.back()];
    openRanges_.pop_back();
    recordingEvents_.push_back({Event::End, uint32_t(recording_.size())});
    r.endEvent = uint32_t(recordingEvents_.size());
    r.endCommand = uint32_t(recording_.size());
}

DisplayList::Command& DisplayList::add(Kind kind) {
    if (!segmentOpen_) {
        const OpenNode& n = nodes_.back();
        recordingSegments_.push_back({mix(n.key, n.after), uint32_t(recording_.size()), 0});
        segmentOpen_ = true;
        layersInSegment_ = 0;
    }
    ++recordingSegments_.back().count;
    Command& c = recording_.emplace_back();
    c.kind = kind;
    c.m = states_.back().m;
    if (states_.size() > 1) c.parent = states_.back().push;
    // (its node's n-th, in this recording: another's layers are others)
    if (kind == Kind::PushLayer) c.layerKey = mix(mix(recordingSegments_.back().key, ++layersInSegment_), reinterpret_cast<uintptr_t>(this));
    return c;
}

DisplayList::Command* DisplayList::draw(Kind kind, const Rect& local, float pad) {
    const State& s = states_.back();
    Rect reach = grown(local, pad);
    // Two pixels more: anti-aliased edges, and backends snapping to whole pixels.
    Rect area = grown(s.m.map(reach), 2 * pixel_).intersect(s.clip);
    if (empty(area) && !shows(reach)) {  // clipped away: it changes no pixel (kept in a layer if it shows there at rest)
        // (A shadow's reach is from the light, not moved with its node.)
        if (keepNodes_) recordingEvents_.push_back({kind == Kind::ElevationShadow ? Event::Light : Event::Culled, uint32_t(recording_.size()), 0, nullptr, reach});
        return nullptr;
    }
    Command& c = add(kind);
    c.bounds = area;
    return &c;
}

bool DisplayList::shows(const Rect& reach) const {
    const State& s = states_.back();
    return !empty(grown(s.m.map(reach), 2 * pixel_).intersect(s.clip)) || !empty(grown(s.restM.map(reach), 2 * pixel_).intersect(s.restClip));
}

void DisplayList::fillRoundRect(const Rect& rect, const CornerRadii& radii, Color color) {
    if (color.alpha() == 0) return;
    if (Command* c = draw(Kind::FillRoundRect, rect, 0)) c->rect = rect, c->radii = radii, c->color = color;
}

void DisplayList::strokeRoundRect(const Rect& rect, const CornerRadii& radii, float width, Color color) {
    if (color.alpha() == 0) return;
    if (Command* c = draw(Kind::StrokeRoundRect, rect, width / 2)) c->rect = rect, c->radii = radii, c->f2 = width, c->color = color;
}

void DisplayList::drawLine(Offset from, Offset to, float width, Color color) {
    if (color.alpha() == 0) return;
    Rect r{std::min(from.x, to.x), std::min(from.y, to.y), std::abs(to.x - from.x), std::abs(to.y - from.y)};
    if (Command* c = draw(Kind::Line, r, width)) c->p1 = from, c->p2 = to, c->f2 = width, c->color = color;
}

DisplayList::Command* DisplayList::text(Kind kind, std::string_view utf8, Offset baseline, const Font& font, bool rtl, float blur) {
    FontMetrics fm = measurer_.metrics(font);
    float width = measurer_.measureWidth(utf8, font);
    // Glyphs reach past their advances and the line's metrics (italics, accents, emoji).
    float pad = 0.3f * std::max(fm.lineHeight, fm.ascent + fm.descent) + 3 * blur;
    Rect r{baseline.x, baseline.y - fm.ascent, width, fm.ascent + fm.descent};
    Command* c = draw(kind, r, pad);
    if (c) c->text = utf8, c->p1 = baseline, c->font = font, c->flag = rtl;
    return c;
}

void DisplayList::drawText(std::string_view utf8, Offset baseline, const Font& font, Color color, bool rtl) {
    if (utf8.empty() || color.alpha() == 0) return;
    if (Command* c = text(Kind::Text, utf8, baseline, font, rtl)) c->color = color;
}

void DisplayList::drawTextBrush(std::string_view utf8, Offset baseline, const Font& font, const Brush& brush, const Rect& brushArea,
                                bool rtl) {
    if (utf8.empty()) return;
    if (Command* c = text(Kind::TextBrush, utf8, baseline, font, rtl)) c->brush = brush, c->rect2 = brushArea, c->imageVersion = imagesIn(brush, brushArea);
}

void DisplayList::drawTextShadow(std::string_view utf8, Offset baseline, const Font& font, Color color, float blurRadius, bool rtl) {
    if (utf8.empty() || color.alpha() == 0) return;
    if (Command* c = text(Kind::TextShadow, utf8, baseline, font, rtl, std::max(0.f, blurRadius))) c->color = color, c->f1 = blurRadius;
}

void DisplayList::drawArc(const Rect& oval, float startDegrees, float sweepDegrees, float strokeWidth, Color color, bool roundCap) {
    if (color.alpha() == 0) return;
    if (Command* c = draw(Kind::Arc, oval, strokeWidth / 2))
        c->rect = oval, c->f1 = startDegrees, c->f2 = sweepDegrees, c->f3 = strokeWidth, c->color = color, c->flag = roundCap;
}

void DisplayList::drawShadow(const Rect& rect, const CornerRadii& radii, const Shadow& shadow, bool inner) {
    if (shadow.color.alpha() <= 0 || rect.width <= 0 || rect.height <= 0) return;
    // A drop shadow reaches its spread, offset and three blur radii out; an inner one stays inside.
    Rect area = rect;
    float pad = 0;
    if (!inner) {
        float grow = std::max(0.f, shadow.spread);
        area = {rect.x - grow + std::min(0.f, shadow.offset.x), rect.y - grow + std::min(0.f, shadow.offset.y),
                rect.width + 2 * grow + std::abs(shadow.offset.x), rect.height + 2 * grow + std::abs(shadow.offset.y)};
        pad = 3 * std::max(0.f, shadow.blurRadius);
    }
    if (Command* c = draw(Kind::Shadow, area, pad)) {
        c->rect = rect;
        c->radii = radii;
        c->color = shadow.color;
        c->p1 = shadow.offset;
        c->f1 = shadow.blurRadius;
        c->f2 = shadow.spread;
        c->flag = inner;
    }
}

void DisplayList::drawShadowPath(const Path& outline, Offset origin, const Shadow& shadow, bool inner) {
    if (shadow.color.alpha() <= 0 || outline.isEmpty()) return;
    Rect rect = outline.bounds();
    rect.x += origin.x;
    rect.y += origin.y;
    Rect area = rect;  // as a rounded rect's (drawShadow)
    float pad = 0;
    if (!inner) {
        float grow = std::max(0.f, shadow.spread);
        area = {rect.x - grow + std::min(0.f, shadow.offset.x), rect.y - grow + std::min(0.f, shadow.offset.y),
                rect.width + 2 * grow + std::abs(shadow.offset.x), rect.height + 2 * grow + std::abs(shadow.offset.y)};
        pad = 3 * std::max(0.f, shadow.blurRadius);
    }
    if (Command* c = draw(Kind::ShadowPath, area, pad)) {
        c->path = outline;
        c->p2 = origin;
        c->color = shadow.color;
        c->p1 = shadow.offset;
        c->f1 = shadow.blurRadius;
        c->f2 = shadow.spread;
        c->flag = inner;
    }
}

// A brushed one (not one color) as it is, so that the canvas draws it its own way if it has one;
// any other as the base class breaks it down. It reaches as far as its bitmap (where it is cut).
void DisplayList::drawShapeShadow(const Rect& rect, const Shape& shape, bool rtl, const Shadow& shadow, bool inner) {
    if (!shadow.brush || shadow.brush->isSolid()) return GraphicsContext::drawShapeShadow(rect, shape, rtl, shadow, inner);
    if (shadow.alpha <= 0 || rect.width <= 0 || rect.height <= 0) return;
    float out = std::max(0.f, shadow.blurRadius) + std::max(0.f, shadow.spread);
    Rect area = inner ? rect : Rect{rect.x - out + shadow.offset.x, rect.y - out + shadow.offset.y, rect.width + 2 * out, rect.height + 2 * out};
    if (Command* c = draw(Kind::ShapeShadow, area, 0)) {
        c->rect = rect;
        c->flag = rtl;
        c->f1 = inner ? 1 : 0;
        c->shaped = std::pair{shape, shadow};
    }
}

void DisplayList::drawImage(const ImageBitmap& image, const Rect& src, const Rect& dst, float alpha, FilterQuality quality) {
    if (image.empty() || alpha <= 0) return;
    if (Command* c = draw(Kind::Image, dst, 0))
        c->image = image, c->imageVersion = image.version(), c->rect = dst, c->rect2 = src, c->f1 = alpha, c->f2 = float(int(quality));
}

void DisplayList::drawImageMask(const ImageBitmap& image, const Rect& src, const Rect& dst, const Brush& brush, const Rect& brushArea,
                                float alpha, FilterQuality quality) {
    if (image.empty() || alpha <= 0) return;
    if (Command* c = draw(Kind::ImageMask, dst, 0)) {
        c->image = image, c->rect = dst, c->rect2 = src, c->f1 = alpha, c->f2 = float(int(quality)), c->brush = brush, c->p1 = {brushArea.x, brushArea.y};
        c->p2 = {brushArea.width, brushArea.height};
        c->imageVersion = image.version() * 1000003u + imagesIn(brush, brushArea);
    }
}

void DisplayList::fillGradient(const Rect& rect, const CornerRadii& radii, const Brush& brush, const Rect& brushArea) {
    if (Command* c = draw(Kind::Gradient, rect, 0))
        c->rect = rect, c->radii = radii, c->brush = brush, c->rect2 = brushArea, c->imageVersion = imagesIn(brush, brushArea);
}

void DisplayList::drawVertices(const Vertices& vertices, Offset origin, BlendMode blendMode, const Brush& brush, const Rect& brushArea,
                               float alpha) {
    if (vertices.positions().empty() || alpha <= 0) return;
    Rect b = vertices.bounds();
    b.x += origin.x;
    b.y += origin.y;
    if (Command* c = draw(Kind::Vertices, b, 1)) {  // (a pixel's reach either way: its edges aren't anti-aliased)
        c->vertices = vertices;
        c->p1 = origin;
        c->f1 = alpha;
        c->f2 = float(int(blendMode));
        c->brush = brush;
        c->rect2 = brushArea;
        c->imageVersion = imagesIn(brush, brushArea);
    }
}

void DisplayList::drawPath(const Path& path, Offset origin, const Brush& brush, const Rect& brushArea, const PathStroke& stroke) {
    if (path.isEmpty()) return;
    Rect b = path.bounds();
    b.x += origin.x;
    b.y += origin.y;
    // A stroke reaches half its width out, more at mitered corners and square caps.
    float pad = 0;
    if (stroke.hairline) {
        pad = 1;  // (a pixel, which is at most a dp)
    } else if (stroke.width > 0) {
        float reach = stroke.join == StrokeJoin::Miter ? std::max(1.f, stroke.miter) : 1.f;
        if (stroke.cap == StrokeCap::Square) reach = std::max(reach, std::numbers::sqrt2_v<float>);
        pad = stroke.width / 2 * reach;
    }
    if (Command* c = draw(Kind::Path, b, pad)) {
        c->path = path;
        c->p1 = origin;
        c->brush = brush;
        c->rect2 = brushArea;
        c->imageVersion = imagesIn(brush, brushArea);
        c->stroke = stroke;
    }
}

void DisplayList::pushLayer(const Rect& bounds, const CornerRadii& radii, float alpha, const LayerTransform& transform) {
    State s = states_.back();
    Projective m = compose(s.m, layerMap(bounds, transform));
    // Its texture is whole pixels around the bounds, composited with an anti-aliased edge.
    Rect area = grown(m.map(grown(bounds, pixel_)), 2 * pixel_).intersect(s.clip);
    Command& c = add(Kind::PushLayer);
    c.rect = bounds;
    c.radii = radii;
    c.f2 = alpha;
    c.transform = transform;
    c.bounds = area;
    states_.push_back({m, area, uint32_t(recording_.size() - 1), s.restM, grown(s.restM.map(grown(bounds, pixel_)), 2 * pixel_).intersect(s.restClip)});
}

void DisplayList::pushOutlineLayer(const Rect& bounds, const Path& outline, float alpha, const LayerTransform& transform) {
    pushLayer(bounds, CornerRadii(), alpha, transform);
    recording_.back().path = outline;  // replayed as an outline layer
}

void DisplayList::pushEffectLayer(const Rect& bounds, const RenderEffect& effect, float alpha) {
    State s = states_.back();
    Command& c = add(Kind::PushLayer);
    c.rect = bounds;
    c.f2 = alpha;
    c.effect = effect;
    // It shows where the effect takes what it draws. That is all of what it draws within its
    // bounds, off the screen too (a blur brings it in): popLayer() makes each of those commands'
    // area its own, as a change to any of them changes all of it.
    c.bounds = grown(s.m.map(effect.outputBounds(bounds)), 2 * pixel_).intersect(s.clip);
    states_.push_back({s.m, grown(s.m.map(grown(bounds, pixel_)), 2 * pixel_), uint32_t(recording_.size() - 1), s.restM,
                       grown(s.restM.map(grown(bounds, pixel_)), 2 * pixel_)});
}

// It shows where the effect takes what is under it, and reads that from as far around it as the
// effect reaches (rect2: there on the screen): end() widens damage that reaches either to both, as
// what is under it has to be drawn anew before it is read.
void DisplayList::drawBackdrop(const Rect& bounds, const RenderEffect& effect, float alpha) {
    Rect reads = effect.outputBounds(bounds);
    if (Command* c = draw(Kind::Backdrop, effect.outputBounds(reads), 0)) {
        c->rect = bounds;
        c->effect = effect;
        c->f2 = alpha;
        c->rect2 = grown(states_.back().m.map(reads), 2 * pixel_);
    }
}

Offset DisplayList::lightCenter() const {
    if (keepNodes_) recordingEvents_.push_back({Event::Light, uint32_t(recording_.size())});
    return lightCenterHere();
}

// The light's center through the inverse of the transform drawn through (where it isn't one,
// as given).
Offset DisplayList::lightCenterHere() const {
    const float* m = states_.back().m.m;
    float a = m[4] * m[8] - m[5] * m[7], b = m[5] * m[6] - m[3] * m[8], c = m[3] * m[7] - m[4] * m[6];
    float det = m[0] * a + m[1] * b + m[2] * c;
    if (std::abs(det) < 1e-12f) return light.center;
    float i[9] = {a, m[2] * m[7] - m[1] * m[8], m[1] * m[5] - m[2] * m[4], b, m[0] * m[8] - m[2] * m[6], m[2] * m[3] - m[0] * m[5],
                  c, m[1] * m[6] - m[0] * m[7], m[0] * m[4] - m[1] * m[3]};
    float x = light.center.x, y = light.center.y;  // (the screen's dp, as the transform's)
    float w = i[6] * x + i[7] * y + i[8];
    if (!(std::abs(w) > 1e-12f)) return light.center;
    return {(i[0] * x + i[1] * y + i[2]) / w, (i[3] * x + i[4] * y + i[5]) / w};
}

// It reaches as far as Skia's bounds for it say (ElevationShadow::reach).
void DisplayList::drawElevationShadow(const Outline& outline, Offset origin, const kompoz::ElevationShadow& shadow) {
    Rect b = outline.bounds();
    b.x += origin.x, b.y += origin.y;
    if (Command* c = draw(Kind::ElevationShadow, shadow.reach(b, light, lightCenterHere()), 0)) {
        c->p1 = origin;
        c->elevated = std::pair{outline, shadow};
    }
}

void DisplayList::pushClip(const Rect& rect) {
    State s = states_.back();
    Rect area = grown(s.m.map(rect), pixel_).intersect(s.clip);  // backends round it to pixels
    Command& c = add(Kind::PushClip);
    c.rect = rect;
    c.bounds = area;
    states_.push_back({s.m, area, uint32_t(recording_.size() - 1), s.restM, grown(s.restM.map(rect), pixel_).intersect(s.restClip)});
}

void DisplayList::pushClipRoundRect(const Rect& rect, const CornerRadii& radii, ClipOp op) {
    State s = states_.back();
    // Inside: where the rect is; outside: anywhere the clips around it leave.
    Rect area = op == ClipOp::Intersect ? grown(s.m.map(rect), pixel_).intersect(s.clip) : s.clip;
    Rect rest = op == ClipOp::Intersect ? grown(s.restM.map(rect), pixel_).intersect(s.restClip) : s.restClip;
    Command& c = add(Kind::PushClip);
    c.rect = rect;
    c.radii = radii;
    c.f1 = 1;
    c.flag = op == ClipOp::Difference;
    c.bounds = area;
    states_.push_back({s.m, area, uint32_t(recording_.size() - 1), s.restM, rest});
}

void DisplayList::pushClipPath(const Path& path, Offset origin, ClipOp op) {
    State s = states_.back();
    Rect b = path.bounds();
    b.x += origin.x, b.y += origin.y;
    Rect area = op == ClipOp::Intersect ? grown(s.m.map(b), pixel_).intersect(s.clip) : s.clip;
    Rect rest = op == ClipOp::Intersect ? grown(s.restM.map(b), pixel_).intersect(s.restClip) : s.restClip;
    Command& c = add(Kind::PushClip);
    c.path = path;
    c.p1 = origin;
    c.f1 = 2;
    c.flag = op == ClipOp::Difference;
    c.bounds = area;
    states_.push_back({s.m, area, uint32_t(recording_.size() - 1), s.restM, rest});
}

void DisplayList::pushTranslation(Offset by) {
    State s = states_.back();
    Projective shift{{1, 0, by.x, 0, 1, by.y, 0, 0, 1}};
    Command& c = add(Kind::PushTranslation);
    c.p1 = by;
    c.bounds = s.clip;  // what anything inside can touch (replay skips it when that's outside the area)
    states_.push_back({compose(s.m, shift), s.clip, uint32_t(recording_.size() - 1), compose(s.restM, shift), s.restClip});
}

void DisplayList::pop(Kind kind) {
    if (states_.size() <= 1) return;
    uint32_t push = states_.back().push;
    states_.pop_back();
    add(kind);
    recording_[push].end = uint32_t(recording_.size());
    if (recording_[push].effect)
        for (uint32_t i = push + 1; i < recording_.size(); ++i)
            if (!empty(recording_[i].bounds)) recording_[i].bounds = recording_[push].bounds;
}

void DisplayList::pushBlendMode(BlendMode mode) {
    State s = states_.back();
    Command& c = add(Kind::PushBlend);
    c.f1 = float(int(mode));
    c.bounds = s.clip;  // what anything inside can touch (replay skips it when that's outside the area)
    states_.push_back({s.m, s.clip, uint32_t(recording_.size() - 1), s.restM, s.restClip});
}

void DisplayList::popBlendMode() { pop(Kind::PopBlend); }

void DisplayList::pushAntiAlias(bool on) {
    State s = states_.back();
    Command& c = add(Kind::PushAlias);
    c.flag = on;
    c.bounds = s.clip;
    states_.push_back({s.m, s.clip, uint32_t(recording_.size() - 1), s.restM, s.restClip});
}

void DisplayList::popAntiAlias() { pop(Kind::PopAlias); }

void DisplayList::pushColorFilter(const ColorFilter& filter) {
    State s = states_.back();
    Command& c = add(Kind::PushFilter);
    c.filter = filter;
    c.bounds = s.clip;  // what anything inside can touch (replay skips it when that's outside the area)
    states_.push_back({s.m, s.clip, uint32_t(recording_.size() - 1), s.restM, s.restClip});
}

void DisplayList::popColorFilter() { pop(Kind::PopFilter); }
void DisplayList::popLayer() { pop(Kind::PopLayer); }
void DisplayList::popClip() { pop(Kind::PopClip); }
void DisplayList::popTranslation() { pop(Kind::PopTranslation); }

void DisplayList::pushTransform(const Matrix& matrix) {
    State s = states_.back();
    Command& c = add(Kind::PushTransform);
    c.matrix = matrix;
    c.bounds = s.clip;  // what anything inside can touch (replay skips it when that's outside the area)
    states_.push_back({compose(s.m, planeMap(matrix)), s.clip, uint32_t(recording_.size() - 1), compose(s.restM, planeMap(matrix)), s.restClip});
}

void DisplayList::popTransform() { pop(Kind::PopTransform); }

bool DisplayList::quickReject(const Rect& rect) const {
    const State& s = states_.back();
    bool rejected = empty(grown(s.m.map(rect), pixel_).intersect(s.clip)) && empty(grown(s.restM.map(rect), pixel_).intersect(s.restClip));
    if (keepNodes_) recordingEvents_.push_back({rejected ? Event::Rejected : Event::Accepted, uint32_t(recording_.size()), 0, nullptr, rect});
    return rejected;
}

void DisplayList::addDamage(std::vector<Rect>& damage, const std::vector<Command>& commands, uint32_t first, uint32_t count) {
    std::optional<Rect> all;
    for (uint32_t i = first; i < first + count; ++i) {
        const Command& c = commands[i];
        if (c.kind == Kind::PopLayer || c.kind == Kind::PushClip || c.kind == Kind::PopClip || c.kind == Kind::PushTranslation ||
            c.kind == Kind::PopTranslation || c.kind == Kind::PushBlend || c.kind == Kind::PopBlend || c.kind == Kind::PushAlias ||
            c.kind == Kind::PopAlias || c.kind == Kind::PushFilter ||
            c.kind == Kind::PopFilter || c.kind == Kind::PushTransform || c.kind == Kind::PopTransform || empty(c.bounds))
            continue;
        all = all ? united(*all, c.bounds) : c.bounds;
    }
    if (all) damage.push_back(*all);
}

std::optional<Rect> DisplayList::end() {
    while (states_.size() > 1) {  // pushes left open: closed
        Kind open = recording_[states_.back().push].kind;
        pop(open == Kind::PushClip          ? Kind::PopClip
            : open == Kind::PushTranslation ? Kind::PopTranslation
            : open == Kind::PushBlend       ? Kind::PopBlend
            : open == Kind::PushAlias       ? Kind::PopAlias
            : open == Kind::PushFilter      ? Kind::PopFilter
            : open == Kind::PushTransform   ? Kind::PopTransform
                                            : Kind::PopLayer);
    }
    Rect screen{0, 0, viewport_.width, viewport_.height};
    areas_.clear();
    moves_.clear();
    bool afresh = invalid_ || !(viewport_ == frameViewport_) || pixel_ != framePixel_;
    // Each layer: what is drawn in it the same as last frame?
    layerLookup_.clear();
    for (uint32_t i = 0; i < frame_.size(); ++i)  // (drawn afresh, the pixels a canvas kept of a layer still are its)
        if (frame_[i].kind == Kind::PushLayer) layerLookup_.emplace(frame_[i].layerKey, i);
    for (uint32_t i = 0; i < recording_.size(); ++i)
        if (recording_[i].kind == Kind::PushLayer) {
            const uint32_t* last = layerLookup_.find(recording_[i].layerKey);
            recording_[i].layerSame = last && sameInLayer(i, *last);
        }
    if (afresh) {
        areas_.push_back(screen);
    } else {
        // What each node drew is compared with what it drew last time: what differs is damage
        // where it was and where it is. So is all of what moved in front of or behind something
        // else: the segments kept are those that follow each other as they did.
        std::vector<Rect> damage;
        lookup_.clear();
        for (uint32_t i = 0; i < frameSegments_.size(); ++i) lookup_.emplace(frameSegments_[i].key, i);
        matched_.assign(frameSegments_.size(), 0);
        moved_.clear();
        int64_t last = -1;  // the last frame's segment those kept so far end with
        for (uint32_t si = 0; si < recordingSegments_.size(); ++si) {
            const Segment& s = recordingSegments_[si];
            const uint32_t* found = lookup_.find(s.key);
            if (!found || matched_[*found]) {  // new
                addDamage(damage, recording_, s.first, s.count);
                continue;
            }
            uint32_t o = *found;
            matched_[o] = 1;
            const Segment& p = frameSegments_[o];
            if (int64_t(o) < last) {  // drawn in another order
                addDamage(damage, frame_, p.first, p.count);
                addDamage(damage, recording_, s.first, s.count);
                continue;
            }
            last = o;
            // What it drew first and last as before stays; the rest changed (or all of it moved).
            uint32_t n = s.count, m = p.count, head = 0, tail = 0;
            while (head < n && head < m && recording_[s.first + head].same(frame_[p.first + head])) ++head;
            while (tail < n - head && tail < m - head && recording_[s.first + n - 1 - tail].same(frame_[p.first + m - 1 - tail])) ++tail;
            if (head + tail < std::max(n, m) && allowMoves_ && n == m)
                if (std::optional<Offset> by = movedBy(s, p)) {
                    moved_.push_back({si, o, *by});
                    continue;
                }
            addDamage(damage, frame_, p.first + head, m - head - tail);
            addDamage(damage, recording_, s.first + head, n - head - tail);
        }
        for (uint32_t i = 0; i < frameSegments_.size(); ++i)
            if (!matched_[i]) addDamage(damage, frame_, frameSegments_[i].first, frameSegments_[i].count);
        if (!moved_.empty()) settleMoves(damage);
        // A backdrop that damage reaches (what it reads, or where it shows) is drawn anew with all
        // that is under what it reads.
        for (bool widened = !damage.empty(); widened;) {
            widened = false;
            for (const Command& c : recording_) {
                if (c.kind != Kind::Backdrop) continue;
                Rect all = united(c.bounds, c.rect2);
                bool reached = false, inside = false;
                for (const Rect& d : damage) {
                    reached = reached || overlaps(d, all);
                    inside = inside || (d.x <= all.x && d.y <= all.y && d.right() >= all.right() && d.bottom() >= all.bottom());
                }
                if (!reached || inside) continue;
                damage.push_back(all);
                widened = true;
            }
        }
        // As a few rects: those that overlap made one, then the two that grow least by it while
        // there are too many.
        constexpr size_t kMaxAreas = 8;
        for (Rect d : damage) {
            // In whole pixels: a clip's edge then cuts none (what is drawn again there is all of
            // it, not mixed with what was there by how much of the pixel it covers).
            d = d.intersect(screen);
            if (empty(d)) continue;
            float x0 = std::floor(d.x / pixel_ + 1e-3f), y0 = std::floor(d.y / pixel_ + 1e-3f);
            float x1 = std::ceil(d.right() / pixel_ - 1e-3f), y1 = std::ceil(d.bottom() / pixel_ - 1e-3f);
            join(areas_, Rect{x0 * pixel_, y0 * pixel_, (x1 - x0) * pixel_, (y1 - y0) * pixel_}.intersect(screen));
        }
        while (areas_.size() > kMaxAreas) {
            size_t bi = 0, bj = 1;
            float best = kInfinity;
            for (size_t i = 0; i < areas_.size(); ++i)
                for (size_t j = i + 1; j < areas_.size(); ++j) {
                    float grows = area(united(areas_[i], areas_[j])) - area(areas_[i]) - area(areas_[j]);
                    if (grows < best) best = grows, bi = i, bj = j;
                }
            Rect both = united(areas_[bi], areas_[bj]);
            areas_.erase(areas_.begin() + ptrdiff_t(bj));
            areas_[bi] = both;
            for (size_t i = 0; i < areas_.size();)  // (what it now overlaps joins it)
                if (i != bi && overlaps(areas_[i], areas_[bi])) {
                    areas_[bi] = united(areas_[bi], areas_[i]);
                    areas_.erase(areas_.begin() + ptrdiff_t(i));
                    if (i < bi) --bi;
                    i = 0;
                } else {
                    ++i;
                }
        }
    }
    std::swap(recording_, frame_);
    std::swap(recordingSegments_, frameSegments_);
    std::swap(recordingEvents_, frameEvents_);
    std::swap(recordingRanges_, frameRanges_);
    rangeLookup_.clear();
    for (uint32_t i = 0; i < frameRanges_.size(); ++i)  // (those recordNode placed and that ended)
        if (frameRanges_[i].origin && frameRanges_[i].endEvent) rangeLookup_.emplace(frameRanges_[i].key, i);
    frameViewport_ = viewport_;
    framePixel_ = pixel_;
    invalid_ = false;
    if (areas_.empty()) return std::nullopt;
    Rect bounds = areas_[0];
    for (const Rect& a : areas_) bounds = united(bounds, a);
    return bounds;
}

bool DisplayList::sameInLayer(uint32_t push, uint32_t last) const {
    // In the layer's space: what it draws moved as it did (through whatever transform: one in it
    // is a push of it, the same, and what is around it is the layer's too).
    const Command &a = recording_[push], &b = frame_[last];
    if (a.end - push != b.end - last) return false;
    Offset by{a.rect.x - b.rect.x, a.rect.y - b.rect.y};
    for (uint32_t k = 1; k + push + 1 < a.end; ++k)  // (what is drawn in it: not its pop, drawn outside)
        if (!recording_[push + k].sameMoved(frame_[last + k], by, false)) return false;
    return true;
}

std::optional<Offset> DisplayList::movedBy(const Segment& s, const Segment& p) const {
    // How far its first command with a place moved, on the screen; then each command the same
    // but for its place, which moved as far on the screen.
    std::optional<Offset> local, screen;
    for (uint32_t i = 0; i < s.count && !local; ++i) {
        std::optional<Offset> now = recording_[s.first + i].place(), then = frame_[p.first + i].place();
        if (now && then) local = Offset{now->x - then->x, now->y - then->y};
    }
    if (!local) return std::nullopt;
    for (uint32_t i = 0; i < s.count; ++i) {
        const Command& c = recording_[s.first + i];
        if (!c.sameMoved(frame_[p.first + i], *local)) return std::nullopt;
        if (!c.place()) continue;
        const float* m = c.m.m;
        if (m[6] != 0 || m[7] != 0 || m[8] == 0) return std::nullopt;  // (in perspective it moves unevenly)
        Offset on{(m[0] * local->x + m[1] * local->y) / m[8], (m[3] * local->x + m[4] * local->y) / m[8]};
        if (!screen) screen = on;
        else if (!near(*screen, on)) return std::nullopt;
    }
    return screen;
}

void DisplayList::settleMoves(std::vector<Rect>& damage) {
    auto isPush = [](Kind k) {
        return k == Kind::PushLayer || k == Kind::PushClip || k == Kind::PushTranslation || k == Kind::PushBlend || k == Kind::PushAlias ||
               k == Kind::PushFilter || k == Kind::PushTransform;
    };
    auto paints = [&](const Command& c) {  // a command that draws (not a push or a pop)
        return !isPush(c.kind) && c.kind != Kind::PopLayer && c.kind != Kind::PopClip && c.kind != Kind::PopTranslation &&
               c.kind != Kind::PopBlend && c.kind != Kind::PopAlias && c.kind != Kind::PopFilter && c.kind != Kind::PopTransform && !empty(c.bounds);
    };
    // Pushes that change pixels as they are, wherever they are (no clip, no texture of their own).
    auto inPlace = [](const Command& c) {
        return c.kind == Kind::PushTranslation || c.kind == Kind::PushBlend || c.kind == Kind::PushAlias || c.kind == Kind::PushFilter ||
               c.kind == Kind::PushTransform;
    };
    auto squareClip = [](const Command& c) { return c.kind == Kind::PushClip && c.f1 == 0 && !c.flag && squareToScreen(c.m); };
    group_.assign(recording_.size(), 0);
    lastGroup_.assign(frame_.size(), 0);
    for (const MovedSegment& ms : moved_) {
        const Segment& s = recordingSegments_[ms.segment];
        const Segment& p = frameSegments_[ms.last];
        std::fill_n(group_.begin() + s.first, s.count, 1u);
        std::fill_n(lastGroup_.begin() + p.first, p.count, 1u);
    }
    // Segments that moved alike within the same rect clip (the innermost that stayed, with only
    // pushes that change pixels in place between) move together.
    struct Group {
        uint32_t clip;
        Offset by;
        std::vector<uint32_t> moved;  // in moved_
        size_t commands = 0;
        Rect exact, area;  // the clip's (and those around it), and the whole pixels in it
        bool ok = true;
    };
    std::vector<Group> groups;
    std::vector<uint32_t> rejected;  // moved_ that move with none
    // (in this frame's commands, or the last frame's)
    auto clipOf = [&](const std::vector<Command>& commands, const std::vector<uint32_t>& moving, uint32_t i) {
        uint32_t p = commands[i].parent;
        while (p != kNone && moving[p]) p = commands[p].parent;
        while (p != kNone && inPlace(commands[p])) p = commands[p].parent;
        return p != kNone && squareClip(commands[p]) ? p : kNone;
    };
    for (uint32_t k = 0; k < moved_.size(); ++k) {
        const Segment& s = recordingSegments_[moved_[k].segment];
        const Segment& last = frameSegments_[moved_[k].last];
        uint32_t p = clipOf(recording_, group_, s.first);
        for (uint32_t i = s.first + 1; i < s.first + s.count && p != kNone; ++i)
            if (clipOf(recording_, group_, i) != p) p = kNone;  // (all of it in the same one)
        // It was in the same clip as it is.
        uint32_t then = clipOf(frame_, lastGroup_, last.first);
        if (p != kNone && (then == kNone || !recording_[p].same(frame_[then]))) p = kNone;
        if (p == kNone) {
            rejected.push_back(k);
            continue;
        }
        auto g = std::find_if(groups.begin(), groups.end(), [&](const Group& g) { return g.clip == p && near(g.by, moved_[k].by); });
        if (g == groups.end()) g = groups.insert(groups.end(), Group{p, moved_[k].by});
        g->moved.push_back(k);
        g->commands += s.count;
    }
    Rect screen{0, 0, viewport_.width, viewport_.height};
    for (Group& g : groups) {
        for (const Group& o : groups)  // of a clip's, the one that moved most
            if (&o != &g && o.clip == g.clip && (o.commands > g.commands || (o.commands == g.commands && &o < &g))) g.ok = false;
        // By whole pixels, within the clip in whole pixels (and the rect clips around it).
        float px = g.by.x / pixel_, py = g.by.y / pixel_;
        if (std::abs(px - std::round(px)) > 0.01f || std::abs(py - std::round(py)) > 0.01f) g.ok = false;
        g.by = {std::round(px) * pixel_, std::round(py) * pixel_};
        Rect c = recording_[g.clip].m.map(recording_[g.clip].rect).intersect(screen);
        for (uint32_t q = recording_[g.clip].parent; q != kNone && g.ok; q = recording_[q].parent)
            if (squareClip(recording_[q])) c = c.intersect(recording_[q].m.map(recording_[q].rect));
            else if (!inPlace(recording_[q])) g.ok = false;  // (a layer's texture, a clip of another shape)
        g.exact = c;
        float x0 = std::ceil(c.x / pixel_ - 1e-3f), y0 = std::ceil(c.y / pixel_ - 1e-3f);
        float x1 = std::floor(c.right() / pixel_ + 1e-3f), y1 = std::floor(c.bottom() / pixel_ + 1e-3f);
        g.area = {x0 * pixel_, y0 * pixel_, (x1 - x0) * pixel_, (y1 - y0) * pixel_};
        if (empty(g.area) || std::abs(g.by.x) >= g.area.width || std::abs(g.by.y) >= g.area.height || (g.by.x == 0 && g.by.y == 0))
            g.ok = false;
    }
    // The largest first; none overlapping one taken.
    std::vector<Group*> order;
    for (Group& g : groups) order.push_back(&g);
    std::stable_sort(order.begin(), order.end(), [](const Group* a, const Group* b) { return area(a->area) > area(b->area); });
    std::vector<Rect> taken;
    for (uint32_t gi = 0; gi < order.size(); ++gi) {
        Group& g = *order[gi];
        if (g.ok)
            for (const Rect& t : taken) g.ok = g.ok && !overlaps(t, g.area);
        if (!g.ok) continue;
        uint32_t id = gi + 2, first = kNone, lastFirst = kNone;
        for (uint32_t k : g.moved) {
            const Segment& s = recordingSegments_[moved_[k].segment];
            const Segment& p = frameSegments_[moved_[k].last];
            std::fill_n(group_.begin() + s.first, s.count, id);
            std::fill_n(lastGroup_.begin() + p.first, p.count, id);
            first = std::min(first, s.first), lastFirst = std::min(lastFirst, p.first);
        }
        // Under it all there, a color: the opaque rect covering the clip (or the white under
        // everything; one that changed is damage over all of it). What else touches the clip,
        // before or after what moved, is damage where it is and where moving would have taken it.
        std::optional<size_t> cover = coverBefore(recording_, first, g.area, pixel_, viewport_),
                              lastCover = coverBefore(frame_, lastFirst, g.area, pixel_, viewport_);
        size_t from = cover ? *cover + 1 : 0, lastFrom = lastCover ? *lastCover + 1 : 0;
        std::vector<Rect> wrong;
        for (size_t i = from; i < recording_.size(); ++i)
            if (group_[i] != id && paints(recording_[i]) && overlaps(recording_[i].bounds, g.area))
                wrong.push_back(recording_[i].bounds.intersect(g.area));
        for (size_t i = lastFrom; i < frame_.size(); ++i)
            if (lastGroup_[i] != id && paints(frame_[i]) && overlaps(moved(frame_[i].bounds, g.by), g.area))
                wrong.push_back(moved(frame_[i].bounds, g.by).intersect(g.area));
        // The pixels moved: what was where the clip is less how far, to there; the rest of the clip
        // is new.
        Rect from_ = g.area.intersect(moved(g.area, {-g.by.x, -g.by.y}));
        Rect to = moved(from_, g.by);
        subtract(g.area, to, wrong);
        // Around it, the pixels the clip's edge cuts (none when that is on whole pixels).
        float x0 = std::floor(g.exact.x / pixel_ + 1e-3f), y0 = std::floor(g.exact.y / pixel_ + 1e-3f);
        float x1 = std::ceil(g.exact.right() / pixel_ - 1e-3f), y1 = std::ceil(g.exact.bottom() / pixel_ - 1e-3f);
        subtract({x0 * pixel_, y0 * pixel_, (x1 - x0) * pixel_, (y1 - y0) * pixel_}, g.area, wrong);
        std::vector<Rect> drawn;  // (as end() makes them areas)
        for (const Rect& w : wrong) join(drawn, w);
        float worth = 0;
        for (const Rect& d : drawn) worth += area(d);
        if (worth >= area(g.area)) {  // as much drawn as without moving
            for (uint32_t k : g.moved) {
                const Segment& s = recordingSegments_[moved_[k].segment];
                const Segment& p = frameSegments_[moved_[k].last];
                std::fill_n(group_.begin() + s.first, s.count, 1u);
                std::fill_n(lastGroup_.begin() + p.first, p.count, 1u);
            }
            g.ok = false;
            continue;
        }
        taken.push_back(g.area);
        moves_.push_back({from_, g.by});
        damage.insert(damage.end(), wrong.begin(), wrong.end());
    }
    for (Group& g : groups)
        if (!g.ok) rejected.insert(rejected.end(), g.moved.begin(), g.moved.end());
    for (uint32_t k : rejected) {  // damage where it was and is
        const Segment& s = recordingSegments_[moved_[k].segment];
        const Segment& p = frameSegments_[moved_[k].last];
        addDamage(damage, frame_, p.first, p.count);
        addDamage(damage, recording_, s.first, s.count);
    }
}

std::optional<size_t> DisplayList::opaqueCover(const Rect& area) const {
    return coverBefore(frame_, frame_.size(), area, framePixel_, frameViewport_);
}

std::optional<size_t> DisplayList::coverBefore(const std::vector<Command>& commands, size_t limit, const Rect& area, float pixel, Size screen) {
    // All of the pixels `area` touches (an edge of the cover in one would leave it part of what is
    // under; none off the screen, where there are no pixels).
    float x0 = std::floor(area.x / pixel + 1e-3f), y0 = std::floor(area.y / pixel + 1e-3f);
    float x1 = std::ceil(area.right() / pixel - 1e-3f), y1 = std::ceil(area.bottom() / pixel - 1e-3f);
    Rect need = Rect{x0 * pixel, y0 * pixel, (x1 - x0) * pixel, (y1 - y0) * pixel}.intersect({0, 0, screen.width, screen.height});
    struct Open {
        uint32_t end;
        Rect clip;   // what the clips so far let through
        bool hides;  // something the cover would show through: a layer, a blend mode, a filter, a clip not a rect
    };
    std::vector<Open> open;
    std::optional<size_t> cover;
    constexpr float kFar = 1e9f;
    for (size_t i = 0; i < limit && i < commands.size(); ++i) {
        while (!open.empty() && open.back().end <= i) open.pop_back();
        const Command& c = commands[i];
        Rect clip = open.empty() ? Rect{-kFar, -kFar, 2 * kFar, 2 * kFar} : open.back().clip;
        bool hides = !open.empty() && open.back().hides;
        switch (c.kind) {
        case Kind::PushClip:
            if (c.f1 == 0 && !c.flag && squareToScreen(c.m)) clip = clip.intersect(c.m.map(c.rect));
            else hides = true;
            open.push_back({c.end, clip, hides});
            break;
        case Kind::PushTranslation:
        case Kind::PushTransform:  // (what is drawn inside comes with its transform)
        case Kind::PushAlias:
            open.push_back({c.end, clip, hides});
            break;
        case Kind::PushLayer:
        case Kind::PushBlend:
        case Kind::PushFilter:
            open.push_back({c.end, clip, true});
            break;
        case Kind::FillRoundRect: {
            const CornerRadii& r = c.radii;
            bool square = r.topLeft <= 0 && r.topRight <= 0 && r.bottomRight <= 0 && r.bottomLeft <= 0;
            if (hides || !square || c.color.alpha() < 1 || !squareToScreen(c.m)) break;
            Rect painted = c.m.map(c.rect).intersect(clip);
            if (painted.x <= need.x && painted.y <= need.y && painted.right() >= need.right() && painted.bottom() >= need.bottom()) cover = i;
            break;
        }
        default: break;
        }
    }
    return cover;
}

void DisplayList::replay(GraphicsContext& canvas, const std::optional<Rect>& area, size_t hidden) const {
    size_t whole = 0;  // what comes before it is drawn wherever it is (a layer the canvas keeps)
    auto skip = [&](size_t i) { return area && i >= whole && !overlaps(frame_[i].bounds, *area); };
    for (size_t i = 0; i < frame_.size();) {
        const Command& c = frame_[i];
        switch (c.kind) {
        case Kind::PushLayer:
        case Kind::PushClip:
        case Kind::PushTranslation:
        case Kind::PushBlend:
        case Kind::PushAlias:
        case Kind::PushFilter:
        case Kind::PushTransform:
            if ((skip(i) || c.end <= hidden) && c.end > i) {  // nothing inside touches the area, or shows
                i = c.end;
                continue;
            }
            if (c.kind == Kind::PushLayer) canvas.keyLayer(c.layerKey, c.layerSame);
            replayCommand(canvas, c, {});
            if (c.kind == Kind::PushLayer && c.end > i) {
                GraphicsContext::LayerContent content = canvas.layerContent();
                if (content == GraphicsContext::LayerContent::Kept) {  // (on to its pop)
                    i = c.end - 1;
                    continue;
                }
                if (content == GraphicsContext::LayerContent::DrawAll) whole = std::max(whole, size_t(c.end));
            }
            break;
        case Kind::PopBlend: case Kind::PopAlias: case Kind::PopFilter: case Kind::PopLayer: case Kind::PopClip: case Kind::PopTranslation:
        case Kind::PopTransform:
            replayCommand(canvas, c, {});
            break;
        default:
            if (!skip(i) && i >= hidden) replayCommand(canvas, c, {});
        }
        ++i;
    }
}

void DisplayList::replayCommand(GraphicsContext& canvas, const Command& c, Offset by) {
    auto r = [&](const Rect& rect) { return moved(rect, by); };
    auto p = [&](Offset o) { return Offset{o.x + by.x, o.y + by.y}; };
    ClipOp op = c.flag ? ClipOp::Difference : ClipOp::Intersect;
    switch (c.kind) {
    case Kind::PushLayer:
        if (!c.path.isEmpty()) canvas.pushOutlineLayer(r(c.rect), c.path, c.f2, c.transform);  // (the outline from its top left)
        else if (c.effect) canvas.pushEffectLayer(r(c.rect), *c.effect, c.f2);
        else canvas.pushLayer(r(c.rect), c.radii, c.f2, c.transform);
        break;
    case Kind::PushClip:
        if (c.f1 == 1) canvas.pushClipRoundRect(r(c.rect), c.radii, op);
        else if (c.f1 == 2) canvas.pushClipPath(c.path, p(c.p1), op);
        else canvas.pushClip(r(c.rect));
        break;
    case Kind::PushBlend: canvas.pushBlendMode(BlendMode(int(c.f1))); break;
    case Kind::PushAlias: canvas.pushAntiAlias(c.flag); break;
    case Kind::PushFilter: canvas.pushColorFilter(*c.filter); break;
    case Kind::PushTransform: canvas.pushTransform(c.matrix); break;  // (not moved: keep() doesn't move one)
    case Kind::PushTranslation: canvas.pushTranslation(c.p1); break;
    case Kind::PopBlend: canvas.popBlendMode(); break;
    case Kind::PopAlias: canvas.popAntiAlias(); break;
    case Kind::PopFilter: canvas.popColorFilter(); break;
    case Kind::PopLayer: canvas.popLayer(); break;
    case Kind::PopClip: canvas.popClip(); break;
    case Kind::PopTranslation: canvas.popTranslation(); break;
    case Kind::PopTransform: canvas.popTransform(); break;
    case Kind::FillRoundRect: canvas.fillRoundRect(r(c.rect), c.radii, c.color); break;
    case Kind::StrokeRoundRect: canvas.strokeRoundRect(r(c.rect), c.radii, c.f2, c.color); break;
    case Kind::Line: canvas.drawLine(p(c.p1), p(c.p2), c.f2, c.color); break;
    case Kind::Text: canvas.drawText(c.text, p(c.p1), c.font, c.color, c.flag); break;
    case Kind::TextBrush: canvas.drawTextBrush(c.text, p(c.p1), c.font, *c.brush, r(c.rect2), c.flag); break;
    case Kind::TextShadow: canvas.drawTextShadow(c.text, p(c.p1), c.font, c.color, c.f1, c.flag); break;
    case Kind::Shadow: canvas.drawShadow(r(c.rect), c.radii, {c.color, c.p1, c.f1, c.f2}, c.flag); break;
    case Kind::ShadowPath: canvas.drawShadowPath(c.path, p(c.p2), {c.color, c.p1, c.f1, c.f2}, c.flag); break;
    case Kind::Backdrop: canvas.drawBackdrop(r(c.rect), *c.effect, c.f2); break;
    case Kind::ElevationShadow: canvas.drawElevationShadow(c.elevated->first, p(c.p1), c.elevated->second); break;
    case Kind::ShapeShadow: canvas.drawShapeShadow(r(c.rect), c.shaped->first, c.flag, c.shaped->second, c.f1 != 0); break;
    case Kind::Arc: canvas.drawArc(r(c.rect), c.f1, c.f2, c.f3, c.color, c.flag); break;
    case Kind::Image: canvas.drawImage(c.image, c.rect2, r(c.rect), c.f1, FilterQuality(int(c.f2))); break;
    case Kind::Gradient: canvas.fillGradient(r(c.rect), c.radii, *c.brush, r(c.rect2)); break;
    case Kind::Path: canvas.drawPath(c.path, p(c.p1), *c.brush, r(c.rect2), c.stroke); break;
    case Kind::Vertices: canvas.drawVertices(*c.vertices, p(c.p1), BlendMode(int(c.f2)), *c.brush, r(c.rect2), c.f1); break;
    case Kind::ImageMask:
        canvas.drawImageMask(c.image, c.rect2, r(c.rect), *c.brush, {c.p1.x + by.x, c.p1.y + by.y, c.p2.x, c.p2.y}, c.f1, FilterQuality(int(c.f2)));
        break;
    }
}

GraphicsContext::NodeRecording DisplayList::recordNode(const void* node, Offset origin, bool unchanged) {
    if (!keepNodes_) return NodeRecording::Other;
    if (const uint32_t* drawn = occurrences_.find(reinterpret_cast<uintptr_t>(node)); drawn && *drawn) return NodeRecording::Other;
    if (unchanged && pixel_ == framePixel_)
        if (const uint32_t* last = rangeLookup_.find(nodeKey(node, 0))) {
            const Range& r = frameRanges_[*last];
            const State& s = states_.back();
            if (r.entry.m == s.m && same(r.entry.clip, s.clip) && r.entry.restM == s.restM && same(r.entry.restClip, s.restClip) &&
                keep(r, {origin.x - r.origin->x, origin.y - r.origin->y})) {
                ++keptNodes_;
                return NodeRecording::Kept;
            }
        }
    nextOrigin_ = origin;
    ++drawnNodes_;
    return NodeRecording::Fresh;
}

bool DisplayList::keep(const Range& r, Offset by) {
    bool moving = by.x != 0 || by.y != 0;
    // What is recorded from here on goes again if anything comes out otherwise.
    size_t commands = recording_.size(), segments = recordingSegments_.size(), events = recordingEvents_.size();
    size_t ranges = recordingRanges_.size(), states = states_.size(), nodes = nodes_.size(), open = openRanges_.size();
    bool segmentOpen = segmentOpen_;
    uint32_t layers = layersInSegment_;
    keptBegun_.clear();
    bool same = true;
    uint32_t next = r.firstCommand;
    for (uint32_t i = r.firstEvent; same && i < r.endEvent; ++i) {
        const Event& e = frameEvents_[i];
        for (; same && next < e.at; ++next) {
            const Command& c = frame_[next];
            if (moving && c.kind == Kind::PushTransform) same = false;  // (its matrix is from where the node was)
            else replayCommand(*this, c, by);
        }
        if (!same) break;
        switch (e.type) {
        case Event::Begin:
            if (const std::optional<Offset>& at = frameRanges_[e.range].origin) nextOrigin_ = Offset{at->x + by.x, at->y + by.y};
            keptBegun_.push_back(e.node);
            beginNode(e.node);
            break;
        case Event::End: endNode(); break;
        case Event::Culled: {
            Rect reach = moved(e.rect, by);
            if (shows(reach)) same = false;  // (it was left out: it would be drawn now)
            else recordingEvents_.push_back({Event::Culled, uint32_t(recording_.size()), 0, nullptr, reach});
            break;
        }
        case Event::Rejected:
        case Event::Accepted: same = quickReject(moved(e.rect, by)) == (e.type == Event::Rejected); break;
        case Event::Light:
            if (moving) same = false;
            else recordingEvents_.push_back({Event::Light, uint32_t(recording_.size())});
            break;
        }
    }
    if (same) return true;
    recording_.erase(recording_.begin() + ptrdiff_t(commands), recording_.end());
    recordingSegments_.resize(segments);
    recordingEvents_.resize(events);
    recordingRanges_.resize(ranges);
    states_.resize(states);
    nodes_.resize(nodes);
    openRanges_.resize(open);
    segmentOpen_ = segmentOpen;
    layersInSegment_ = layers;
    for (const void* node : keptBegun_) --occurrences_[reinterpret_cast<uintptr_t>(node)];
    nextOrigin_.reset();
    return false;
}

namespace {
std::string describe(const Rect& r) {
    char out[96];
    std::snprintf(out, sizeof out, "(%g, %g, %g x %g)", r.x, r.y, r.width, r.height);
    return out;
}
std::string describe(Offset p) {
    char out[64];
    std::snprintf(out, sizeof out, "(%g, %g)", p.x, p.y);
    return out;
}
} // namespace

bool DisplayList::sameFrame(const DisplayList& other, std::string* why) const {
    auto fail = [&](const std::string& what) {
        if (why) *why = what;
        return false;
    };
    if (frame_.size() != other.frame_.size()) return fail(std::to_string(frame_.size()) + " commands, not " + std::to_string(other.frame_.size()));
    if (frameSegments_.size() != other.frameSegments_.size()) return fail("segments differ in number");
    for (size_t i = 0; i < frameSegments_.size(); ++i) {
        const Segment &a = frameSegments_[i], &b = other.frameSegments_[i];
        if (a.key != b.key || a.first != b.first || a.count != b.count) return fail("segment " + std::to_string(i) + " differs");
    }
    auto nearRect = [](const Rect& a, const Rect& b) { return near(a.x, b.x) && near(a.y, b.y) && near(a.width, b.width) && near(a.height, b.height); };
    for (size_t i = 0; i < frame_.size(); ++i) {
        const Command &a = frame_[i], &b = other.frame_[i];
        bool m = true;
        for (int k = 0; k < 9; ++k) m = m && near(a.m.m[k], b.m.m[k]);
        // The fields that differ, by name.
        std::string fields;
        auto check = [&](bool same, const char* name) {
            if (!same) fields += std::string(fields.empty() ? "" : ", ") + name;
        };
        check(a.kind == b.kind, "kind");
        check(m, "transform");
        check(nearRect(a.bounds, b.bounds), "bounds");
        check(nearRect(a.rect, b.rect), "rect");
        check(nearRect(a.rect2, b.rect2), "rect2");
        check(near(a.p1, b.p1) && near(a.p2, b.p2), "points");
        check(a.radii == b.radii, "radii");
        check(a.f1 == b.f1 && a.f2 == b.f2 && a.f3 == b.f3, "numbers");
        check(a.color == b.color, "color");
        check(a.flag == b.flag, "flag");
        check(a.font == b.font && a.text == b.text, "text");
        check(a.image == b.image && a.imageVersion == b.imageVersion, "image");
        check(kompoz::same(a.stroke, b.stroke), "stroke");
        check(kompoz::same(a.transform, b.transform), "layer transform");
        check(a.brush == b.brush, "brush");
        check(a.vertices == b.vertices, "vertices");
        check(a.filter == b.filter, "filter");
        check(a.effect == b.effect, "effect");
        check(a.elevated == b.elevated, "elevation shadow");
        check(a.shaped == b.shaped, "shape shadow");
        check(a.path == b.path, "path");
        check(a.matrix == b.matrix, "matrix");
        // (A layer's key is its list's too: two lists' differ. It is made from the segments'.)
        check(a.parent == b.parent && a.end == b.end, "nesting");
        if (!fields.empty())
            return fail("command " + std::to_string(i) + " (kind " + std::to_string(int(a.kind)) + ", at " + describe(a.rect) + " " + describe(a.p1) +
                        ") differs in " + fields);
    }
    return true;
}

} // namespace kompoz
