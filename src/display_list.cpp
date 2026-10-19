#include <kompoz/display_list.hpp>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace kompoz {

namespace {

bool same(const Rect& a, const Rect& b) { return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height; }
bool same(Offset a, Offset b) { return a.x == b.x && a.y == b.y; }
bool same(const LayerTransform& a, const LayerTransform& b) {
    return a.scaleX == b.scaleX && a.scaleY == b.scaleY && a.rotationZ == b.rotationZ && a.translationX == b.translationX &&
           a.translationY == b.translationY && same(a.origin, b.origin);
}
bool same(const PathStroke& a, const PathStroke& b) { return a.width == b.width && a.cap == b.cap && a.join == b.join && a.miter == b.miter; }

bool empty(const Rect& r) { return r.width <= 0 || r.height <= 0; }
bool overlaps(const Rect& a, const Rect& b) { return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom(); }
Rect grown(const Rect& r, float by) { return {r.x - by, r.y - by, r.width + 2 * by, r.height + 2 * by}; }
Rect united(const Rect& a, const Rect& b) {
    float l = std::min(a.x, b.x), t = std::min(a.y, b.y);
    return {l, t, std::max(a.right(), b.right()) - l, std::max(a.bottom(), b.bottom()) - t};
}

uint64_t mix(uint64_t h, uint64_t v) { return h ^ (v + 0x9e3779b97f4a7c15ull + (h << 6) + (h >> 2)); }

using Projective = DisplayList::Projective;

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
           kompoz::same(p1, o.p1) && kompoz::same(p2, o.p2) && flag == o.flag && font == o.font && vector == o.vector &&
           image == o.image && kompoz::same(stroke, o.stroke) && kompoz::same(transform, o.transform) && text == o.text &&
           brush == o.brush && filter == o.filter && path == o.path && matrix == o.matrix;
}

void DisplayList::begin(Size viewport, float pixel) {
    viewport_ = viewport;
    pixel_ = pixel;
    recording_.clear();
    recordingSegments_.clear();
    occurrences_.clear();
    states_.assign(1, {{}, {0, 0, viewport.width, viewport.height}, 0});
    nodes_.assign(1, {0x6b6f6d706f7aull});  // the root: what is drawn outside any node
    segmentOpen_ = false;
}

void DisplayList::beginNode(const void* node) {
    segmentOpen_ = false;
    uint32_t occurrence = occurrences_[node]++;  // a node drawn twice (a shared element's overlay)
    nodes_.push_back({mix(mix(uint64_t(reinterpret_cast<uintptr_t>(node)), occurrence), 0x51ed27)});
}

void DisplayList::endNode() {
    if (nodes_.size() <= 1) return;
    uint64_t child = nodes_.back().key;
    nodes_.pop_back();
    nodes_.back().after = child;  // what the parent draws next follows this child: a child added before it leaves that alone
    segmentOpen_ = false;
}

DisplayList::Command& DisplayList::add(Kind kind) {
    if (!segmentOpen_) {
        const OpenNode& n = nodes_.back();
        recordingSegments_.push_back({mix(n.key, n.after), uint32_t(recording_.size()), 0});
        segmentOpen_ = true;
    }
    ++recordingSegments_.back().count;
    Command& c = recording_.emplace_back();
    c.kind = kind;
    c.m = states_.back().m;
    return c;
}

DisplayList::Command* DisplayList::draw(Kind kind, const Rect& local, float pad) {
    const State& s = states_.back();
    // Two pixels more: anti-aliased edges, and backends snapping to whole pixels.
    Rect area = grown(s.m.map(grown(local, pad)), 2 * pixel_).intersect(s.clip);
    if (empty(area)) return nullptr;  // clipped away: it changes no pixel
    Command& c = add(kind);
    c.bounds = area;
    return &c;
}

void DisplayList::fillRoundRect(const Rect& rect, const CornerRadii& radii, Color color) {
    if (color.a == 0) return;
    if (Command* c = draw(Kind::FillRoundRect, rect, 0)) c->rect = rect, c->radii = radii, c->color = color;
}

void DisplayList::strokeRoundRect(const Rect& rect, const CornerRadii& radii, float width, Color color) {
    if (color.a == 0) return;
    if (Command* c = draw(Kind::StrokeRoundRect, rect, width / 2)) c->rect = rect, c->radii = radii, c->f2 = width, c->color = color;
}

void DisplayList::drawLine(Offset from, Offset to, float width, Color color) {
    if (color.a == 0) return;
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
    if (utf8.empty() || color.a == 0) return;
    if (Command* c = text(Kind::Text, utf8, baseline, font, rtl)) c->color = color;
}

void DisplayList::drawTextBrush(std::string_view utf8, Offset baseline, const Font& font, const Brush& brush, const Rect& brushArea,
                                bool rtl) {
    if (utf8.empty()) return;
    if (Command* c = text(Kind::TextBrush, utf8, baseline, font, rtl)) c->brush = brush, c->rect2 = brushArea;
}

void DisplayList::drawTextShadow(std::string_view utf8, Offset baseline, const Font& font, Color color, float blurRadius, bool rtl) {
    if (utf8.empty() || color.a == 0) return;
    if (Command* c = text(Kind::TextShadow, utf8, baseline, font, rtl, std::max(0.f, blurRadius))) c->color = color, c->f1 = blurRadius;
}

void DisplayList::drawVector(const ImageVector& vector, const Rect& bounds, Color color) {
    if (vector.empty() || color.a == 0) return;
    if (Command* c = draw(Kind::Vector, bounds, 0)) c->vector = vector, c->rect = bounds, c->color = color;
}

void DisplayList::drawArc(const Rect& oval, float startDegrees, float sweepDegrees, float strokeWidth, Color color, bool roundCap) {
    if (color.a == 0) return;
    if (Command* c = draw(Kind::Arc, oval, strokeWidth / 2))
        c->rect = oval, c->f1 = startDegrees, c->f2 = sweepDegrees, c->f3 = strokeWidth, c->color = color, c->flag = roundCap;
}

void DisplayList::drawShadow(const Rect& rect, const CornerRadii& radii, const Shadow& shadow, bool inner) {
    if (shadow.color.a <= 0 || rect.width <= 0 || rect.height <= 0) return;
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
    if (shadow.color.a <= 0 || outline.isEmpty()) return;
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

void DisplayList::drawImage(const ImageBitmap& image, const Rect& src, const Rect& dst, float alpha) {
    if (image.empty() || alpha <= 0) return;
    if (Command* c = draw(Kind::Image, dst, 0)) c->image = image, c->rect = dst, c->rect2 = src, c->f1 = alpha;
}

void DisplayList::fillGradient(const Rect& rect, const CornerRadii& radii, const Brush& brush, const Rect& brushArea) {
    if (Command* c = draw(Kind::Gradient, rect, 0)) c->rect = rect, c->radii = radii, c->brush = brush, c->rect2 = brushArea;
}

void DisplayList::drawPath(const Path& path, Offset origin, const Brush& brush, const Rect& brushArea, const PathStroke& stroke) {
    if (path.isEmpty()) return;
    Rect b = path.bounds();
    b.x += origin.x;
    b.y += origin.y;
    // A stroke reaches half its width out, more at mitered corners and square caps.
    float pad = 0;
    if (stroke.width > 0) {
        float reach = stroke.join == StrokeJoin::Miter ? std::max(1.f, stroke.miter) : 1.f;
        if (stroke.cap == StrokeCap::Square) reach = std::max(reach, std::numbers::sqrt2_v<float>);
        pad = stroke.width / 2 * reach;
    }
    if (Command* c = draw(Kind::Path, b, pad)) {
        c->path = path;
        c->p1 = origin;
        c->brush = brush;
        c->rect2 = brushArea;
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
    states_.push_back({m, area, uint32_t(recording_.size() - 1)});
}

void DisplayList::pushOutlineLayer(const Rect& bounds, const Path& outline, float alpha, const LayerTransform& transform) {
    pushLayer(bounds, CornerRadii(), alpha, transform);
    recording_.back().path = outline;  // replayed as an outline layer
}

void DisplayList::pushBlurLayer(const Rect& bounds, float radiusX, float radiusY) {
    pushLayer(bounds, CornerRadii(), 1);
    recording_.back().p2 = {radiusX, radiusY};  // replayed as a blur layer
}

void DisplayList::pushClip(const Rect& rect) {
    State s = states_.back();
    Rect area = grown(s.m.map(rect), pixel_).intersect(s.clip);  // backends round it to pixels
    Command& c = add(Kind::PushClip);
    c.rect = rect;
    c.bounds = area;
    states_.push_back({s.m, area, uint32_t(recording_.size() - 1)});
}

void DisplayList::pushClipRoundRect(const Rect& rect, const CornerRadii& radii, ClipOp op) {
    State s = states_.back();
    // Inside: where the rect is; outside: anywhere the clips around it leave.
    Rect area = op == ClipOp::Intersect ? grown(s.m.map(rect), pixel_).intersect(s.clip) : s.clip;
    Command& c = add(Kind::PushClip);
    c.rect = rect;
    c.radii = radii;
    c.f1 = 1;
    c.flag = op == ClipOp::Difference;
    c.bounds = area;
    states_.push_back({s.m, area, uint32_t(recording_.size() - 1)});
}

void DisplayList::pushClipPath(const Path& path, Offset origin, ClipOp op) {
    State s = states_.back();
    Rect b = path.bounds();
    Rect area = op == ClipOp::Intersect ? grown(s.m.map({b.x + origin.x, b.y + origin.y, b.width, b.height}), pixel_).intersect(s.clip) : s.clip;
    Command& c = add(Kind::PushClip);
    c.path = path;
    c.p1 = origin;
    c.f1 = 2;
    c.flag = op == ClipOp::Difference;
    c.bounds = area;
    states_.push_back({s.m, area, uint32_t(recording_.size() - 1)});
}

void DisplayList::pushTranslation(Offset by) {
    State s = states_.back();
    Projective moved = compose(s.m, Projective{{1, 0, by.x, 0, 1, by.y, 0, 0, 1}});
    Command& c = add(Kind::PushTranslation);
    c.p1 = by;
    c.bounds = s.clip;  // what anything inside can touch (replay skips it when that's outside the area)
    states_.push_back({moved, s.clip, uint32_t(recording_.size() - 1)});
}

void DisplayList::pop(Kind kind) {
    if (states_.size() <= 1) return;
    uint32_t push = states_.back().push;
    states_.pop_back();
    add(kind);
    recording_[push].end = uint32_t(recording_.size());
}

void DisplayList::pushBlendMode(BlendMode mode) {
    State s = states_.back();
    Command& c = add(Kind::PushBlend);
    c.f1 = float(int(mode));
    c.bounds = s.clip;  // what anything inside can touch (replay skips it when that's outside the area)
    states_.push_back({s.m, s.clip, uint32_t(recording_.size() - 1)});
}

void DisplayList::popBlendMode() { pop(Kind::PopBlend); }

void DisplayList::pushColorFilter(const ColorFilter& filter) {
    State s = states_.back();
    Command& c = add(Kind::PushFilter);
    c.filter = filter;
    c.bounds = s.clip;  // what anything inside can touch (replay skips it when that's outside the area)
    states_.push_back({s.m, s.clip, uint32_t(recording_.size() - 1)});
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
    states_.push_back({compose(s.m, planeMap(matrix)), s.clip, uint32_t(recording_.size() - 1)});
}

void DisplayList::popTransform() { pop(Kind::PopTransform); }

bool DisplayList::quickReject(const Rect& rect) const {
    const State& s = states_.back();
    return empty(grown(s.m.map(rect), pixel_).intersect(s.clip));
}

void DisplayList::addDamage(std::optional<Rect>& damage, const std::vector<Command>& commands, uint32_t first, uint32_t count) {
    for (uint32_t i = first; i < first + count; ++i) {
        const Command& c = commands[i];
        if (c.kind == Kind::PopLayer || c.kind == Kind::PushClip || c.kind == Kind::PopClip || c.kind == Kind::PushTranslation ||
            c.kind == Kind::PopTranslation || c.kind == Kind::PushBlend || c.kind == Kind::PopBlend || c.kind == Kind::PushFilter ||
            c.kind == Kind::PopFilter || c.kind == Kind::PushTransform || c.kind == Kind::PopTransform || empty(c.bounds))
            continue;
        damage = damage ? united(*damage, c.bounds) : c.bounds;
    }
}

std::optional<Rect> DisplayList::end() {
    while (states_.size() > 1) {  // pushes left open: closed
        Kind open = recording_[states_.back().push].kind;
        pop(open == Kind::PushClip          ? Kind::PopClip
            : open == Kind::PushTranslation ? Kind::PopTranslation
            : open == Kind::PushBlend       ? Kind::PopBlend
            : open == Kind::PushFilter      ? Kind::PopFilter
            : open == Kind::PushTransform   ? Kind::PopTransform
                                            : Kind::PopLayer);
    }
    Rect screen{0, 0, viewport_.width, viewport_.height};
    std::optional<Rect> damage;
    if (invalid_ || !(viewport_ == frameViewport_) || pixel_ != framePixel_) {
        damage = screen;
    } else {
        // What each node drew is compared with what it drew last time: what differs is damage
        // where it was and where it is. So is all of what moved in front of or behind something
        // else: the segments kept are those that follow each other as they did.
        lookup_.clear();
        for (uint32_t i = 0; i < frameSegments_.size(); ++i) lookup_.emplace(frameSegments_[i].key, i);
        matched_.assign(frameSegments_.size(), 0);
        int64_t last = -1;  // the last frame's segment those kept so far end with
        for (const Segment& s : recordingSegments_) {
            auto it = lookup_.find(s.key);
            if (it == lookup_.end() || matched_[it->second]) {  // new
                addDamage(damage, recording_, s.first, s.count);
                continue;
            }
            uint32_t o = it->second;
            matched_[o] = 1;
            const Segment& p = frameSegments_[o];
            if (int64_t(o) < last) {  // drawn in another order
                addDamage(damage, frame_, p.first, p.count);
                addDamage(damage, recording_, s.first, s.count);
                continue;
            }
            last = o;
            // What it drew first and last as before stays; the rest changed.
            uint32_t n = s.count, m = p.count, head = 0, tail = 0;
            while (head < n && head < m && recording_[s.first + head].same(frame_[p.first + head])) ++head;
            while (tail < n - head && tail < m - head && recording_[s.first + n - 1 - tail].same(frame_[p.first + m - 1 - tail])) ++tail;
            addDamage(damage, frame_, p.first + head, m - head - tail);
            addDamage(damage, recording_, s.first + head, n - head - tail);
        }
        for (uint32_t i = 0; i < frameSegments_.size(); ++i)
            if (!matched_[i]) addDamage(damage, frame_, frameSegments_[i].first, frameSegments_[i].count);
        if (damage) {
            *damage = damage->intersect(screen);
            if (empty(*damage)) damage.reset();
        }
    }
    std::swap(recording_, frame_);
    std::swap(recordingSegments_, frameSegments_);
    frameViewport_ = viewport_;
    framePixel_ = pixel_;
    invalid_ = false;
    return damage;
}

void DisplayList::replay(GraphicsContext& canvas, const std::optional<Rect>& area) const {
    auto skip = [&](const Command& c) { return area && !overlaps(c.bounds, *area); };
    for (size_t i = 0; i < frame_.size();) {
        const Command& c = frame_[i];
        switch (c.kind) {
        case Kind::PushLayer:
        case Kind::PushClip:
        case Kind::PushTranslation:
        case Kind::PushBlend:
        case Kind::PushFilter:
        case Kind::PushTransform:
            if (skip(c) && c.end > i) {  // nothing inside touches the area
                i = c.end;
                continue;
            }
            if (c.kind == Kind::PushLayer && !c.path.isEmpty()) canvas.pushOutlineLayer(c.rect, c.path, c.f2, c.transform);
            else if (c.kind == Kind::PushLayer && (c.p2.x > 0 || c.p2.y > 0)) canvas.pushBlurLayer(c.rect, c.p2.x, c.p2.y);
            else if (c.kind == Kind::PushLayer) canvas.pushLayer(c.rect, c.radii, c.f2, c.transform);
            else if (c.kind == Kind::PushClip && c.f1 == 1) canvas.pushClipRoundRect(c.rect, c.radii, c.flag ? ClipOp::Difference : ClipOp::Intersect);
            else if (c.kind == Kind::PushClip && c.f1 == 2) canvas.pushClipPath(c.path, c.p1, c.flag ? ClipOp::Difference : ClipOp::Intersect);
            else if (c.kind == Kind::PushClip) canvas.pushClip(c.rect);
            else if (c.kind == Kind::PushBlend) canvas.pushBlendMode(BlendMode(int(c.f1)));
            else if (c.kind == Kind::PushFilter) canvas.pushColorFilter(*c.filter);
            else if (c.kind == Kind::PushTransform) canvas.pushTransform(c.matrix);
            else canvas.pushTranslation(c.p1);
            break;
        case Kind::PopBlend: canvas.popBlendMode(); break;
        case Kind::PopFilter: canvas.popColorFilter(); break;
        case Kind::PopLayer: canvas.popLayer(); break;
        case Kind::PopClip: canvas.popClip(); break;
        case Kind::PopTranslation: canvas.popTranslation(); break;
        case Kind::PopTransform: canvas.popTransform(); break;
        default:
            if (skip(c)) break;
            switch (c.kind) {
            case Kind::FillRoundRect: canvas.fillRoundRect(c.rect, c.radii, c.color); break;
            case Kind::StrokeRoundRect: canvas.strokeRoundRect(c.rect, c.radii, c.f2, c.color); break;
            case Kind::Line: canvas.drawLine(c.p1, c.p2, c.f2, c.color); break;
            case Kind::Text: canvas.drawText(c.text, c.p1, c.font, c.color, c.flag); break;
            case Kind::TextBrush: canvas.drawTextBrush(c.text, c.p1, c.font, *c.brush, c.rect2, c.flag); break;
            case Kind::TextShadow: canvas.drawTextShadow(c.text, c.p1, c.font, c.color, c.f1, c.flag); break;
            case Kind::Shadow: canvas.drawShadow(c.rect, c.radii, {c.color, c.p1, c.f1, c.f2}, c.flag); break;
            case Kind::ShadowPath: canvas.drawShadowPath(c.path, c.p2, {c.color, c.p1, c.f1, c.f2}, c.flag); break;
            case Kind::Vector: canvas.drawVector(c.vector, c.rect, c.color); break;
            case Kind::Arc: canvas.drawArc(c.rect, c.f1, c.f2, c.f3, c.color, c.flag); break;
            case Kind::Image: canvas.drawImage(c.image, c.rect2, c.rect, c.f1); break;
            case Kind::Gradient: canvas.fillGradient(c.rect, c.radii, *c.brush, c.rect2); break;
            case Kind::Path: canvas.drawPath(c.path, c.p1, *c.brush, c.rect2, c.stroke); break;
            default: break;
            }
        }
        ++i;
    }
}

} // namespace kompoz
