// Lengths along a path and path effects, as Skia's (SkContourMeasure and SkPathMeasure;
// SkDashPath, SkCornerPathEffect, SkPath1DPathEffect and SkComposePathEffect), on Kompoz's Path.

#include <kompoz/graphics.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// Skia's float arithmetic as written: no fused multiply-adds.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC optimize("fp-contract=off")
#endif

namespace kompoz {

namespace {

using Verb = PathCommand::Verb;

Offset operator+(Offset a, Offset b) { return {a.x + b.x, a.y + b.y}; }
Offset operator-(Offset a, Offset b) { return {a.x - b.x, a.y - b.y}; }
Offset operator*(Offset a, float s) { return {a.x * s, a.y * s}; }
bool finite(Offset p) { return std::isfinite(p.x) && std::isfinite(p.y); }

float interp(float a, float b, float t) { return a + (b - a) * t; }  // (SkScalarInterp)

// SkPoint::Length.
float length(float dx, float dy) {
    float mag2 = dx * dx + dy * dy;
    if (std::isfinite(mag2)) return std::sqrt(mag2);
    double xx = dx, yy = dy;
    return float(std::sqrt(xx * xx + yy * yy));
}
float distance(Offset a, Offset b) { return length(b.x - a.x, b.y - a.y); }

// SkPoint::setLength(x, y, 1): the unit vector, (0, 0) if there is none.
Offset normalized(float x, float y) {
    double xx = x, yy = y;
    double scale = 1 / std::sqrt(xx * xx + yy * yy);
    x = float(x * scale), y = float(y * scale);
    if (!std::isfinite(x) || !std::isfinite(y) || (x == 0 && y == 0)) return {};
    return {x, y};
}

// ---- Skia's SkGeometry: curves evaluated and chopped -----------------------------------------

Offset evalQuad(const Offset p[3], float t) {  // (SkQuadCoeff)
    Offset c = p[0], b = (p[1] - c) * 2, a = p[2] - p[1] * 2 + c;
    return {(a.x * t + b.x) * t + c.x, (a.y * t + b.y) * t + c.y};
}

Offset quadTangent(const Offset p[3], float t) {  // (SkEvalQuadTangentAt)
    if ((t == 0 && p[0] == p[1]) || (t == 1 && p[1] == p[2])) return p[2] - p[0];
    Offset b = p[1] - p[0], a = p[2] - p[1] - b, tt{a.x * t + b.x, a.y * t + b.y};
    return tt + tt;
}

void chopQuad(const Offset src[3], Offset dst[5], float t) {  // (SkChopQuadAt)
    auto mix = [t](Offset a, Offset b) { return Offset{a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t}; };
    Offset p01 = mix(src[0], src[1]), p12 = mix(src[1], src[2]);
    dst[0] = src[0], dst[1] = p01, dst[2] = mix(p01, p12), dst[3] = p12, dst[4] = src[2];
}

Offset evalCubic(const Offset p[4], float t) {  // (SkCubicCoeff)
    Offset a = p[3] + (p[1] - p[2]) * 3 - p[0], b = (p[2] - p[1] * 2 + p[0]) * 3, c = (p[1] - p[0]) * 3, d = p[0];
    return {((a.x * t + b.x) * t + c.x) * t + d.x, ((a.y * t + b.y) * t + c.y) * t + d.y};
}

Offset cubicTangent(const Offset p[4], float t) {  // (SkEvalCubicAt's tangent)
    if ((t == 0 && p[0] == p[1]) || (t == 1 && p[2] == p[3])) {
        Offset v = t == 0 ? p[2] - p[0] : p[3] - p[1];
        if (v.x == 0 && v.y == 0) v = p[3] - p[0];
        return v;
    }
    Offset a = p[3] + (p[1] - p[2]) * 3 - p[0], b = (p[2] - p[1] * 2 + p[0]) * 2, c = p[1] - p[0];
    return {(a.x * t + b.x) * t + c.x, (a.y * t + b.y) * t + c.y};
}

void chopCubic(const Offset src[4], Offset dst[7], float t) {  // (SkChopCubicAt)
    if (t == 1) {
        std::copy(src, src + 4, dst);
        dst[4] = dst[5] = dst[6] = src[3];
        return;
    }
    auto mix = [t](Offset a, Offset b) { return Offset{(b.x - a.x) * t + a.x, (b.y - a.y) * t + a.y}; };
    Offset ab = mix(src[0], src[1]), bc = mix(src[1], src[2]), cd = mix(src[2], src[3]);
    Offset abc = mix(ab, bc), bcd = mix(bc, cd), abcd = mix(abc, bcd);
    dst[0] = src[0], dst[1] = ab, dst[2] = abc, dst[3] = abcd, dst[4] = bcd, dst[5] = cd, dst[6] = src[3];
}

struct Conic {  // (SkConic)
    Offset pts[3];
    float w = 1;

    // (SkConicCoeff) Its numerator's and denominator's quadratics at t.
    void coeffs(float t, Offset& numer, float& denom) const {
        Offset p1w = pts[1] * w, c = pts[0], a = pts[2] - p1w * 2 + c, b = (p1w - c) * 2;
        numer = {(a.x * t + b.x) * t + c.x, (a.y * t + b.y) * t + c.y};
        float dc = 1, db = 2 * (w - dc), da = 0 - db;
        denom = (da * t + db) * t + dc;
    }
    Offset evalAt(float t) const {
        Offset n;
        float d;
        coeffs(t, n, d);
        return {n.x / d, n.y / d};
    }
    Offset tangentAt(float t) const {
        if ((t == 0 && pts[0] == pts[1]) || (t == 1 && pts[1] == pts[2])) return pts[2] - pts[0];
        Offset p20 = pts[2] - pts[0], p10 = pts[1] - pts[0];
        Offset c = p10 * w, a = p20 * w - p20, b = p20 - c - c;
        return {(a.x * t + b.x) * t + c.x, (a.y * t + b.y) * t + c.y};
    }
    // Cut at t in two (false if that is not finite).
    bool chopAt(float t, Conic dst[2]) const {
        float tmp[3][3] = {{pts[0].x, pts[0].y, 1}, {pts[1].x * w, pts[1].y * w, w}, {pts[2].x, pts[2].y, 1}}, out[3][3];
        for (int k = 0; k < 3; ++k) {  // (p3d_interp)
            float ab = interp(tmp[0][k], tmp[1][k], t), bc = interp(tmp[1][k], tmp[2][k], t);
            out[0][k] = ab, out[1][k] = interp(ab, bc, t), out[2][k] = bc;
        }
        auto down = [](const float v[3]) { return Offset{v[0] / v[2], v[1] / v[2]}; };
        dst[0].pts[0] = pts[0], dst[0].pts[1] = down(out[0]), dst[0].pts[2] = down(out[1]);
        dst[1].pts[0] = dst[0].pts[2], dst[1].pts[1] = down(out[2]), dst[1].pts[2] = pts[2];
        float root = std::sqrt(out[1][2]);
        dst[0].w = out[0][2] / root, dst[1].w = out[2][2] / root;
        for (const Conic& c : {dst[0], dst[1]})
            if (!finite(c.pts[0]) || !finite(c.pts[1]) || !finite(c.pts[2]) || !std::isfinite(c.w)) return false;
        return true;
    }
    // The piece from t1 to t2.
    void chopAt(float t1, float t2, Conic& dst) const {
        if (t1 == 0 || t2 == 1) {
            if (t1 == 0 && t2 == 1) {
                dst = *this;
                return;
            }
            Conic pair[2];
            if (chopAt(t1 != 0 ? t1 : t2, pair)) {
                dst = pair[t1 != 0 ? 1 : 0];
                return;
            }
        }
        Offset aXY, dXY, cXY;
        float aZ, dZ, cZ;
        coeffs(t1, aXY, aZ);
        coeffs((t1 + t2) / 2, dXY, dZ);
        coeffs(t2, cXY, cZ);
        Offset bXY = dXY * 2 - (aXY + cXY) * 0.5f;
        float bZ = dZ * 2 - (aZ + cZ) * 0.5f;
        dst.pts[0] = {aXY.x / aZ, aXY.y / aZ};
        dst.pts[1] = {bXY.x / bZ, bXY.y / bZ};
        dst.pts[2] = {cXY.x / cZ, cXY.y / cZ};
        dst.w = bZ / std::sqrt(aZ * cZ);
    }
};

// The last point of a path (SkPath::getLastPt), if it has one.
bool lastPoint(const Path& path, Offset& at) {
    for (auto c = path.commands().rbegin(); c != path.commands().rend(); ++c)
        if (c->verb != Verb::Close) return at = {c->x, c->y}, true;
    return false;
}

// Its two points, if a path is one line (SkPath::isLine).
bool isLine(const Path& path, Offset pts[2]) {
    const auto& c = path.commands();
    if (c.size() != 2 || c[0].verb != Verb::Move || c[1].verb != Verb::Line) return false;
    pts[0] = {c[0].x, c[0].y}, pts[1] = {c[1].x, c[1].y};
    return true;
}

// A path's verbs as SkPath::Iter gives them (not forced closed): each with its points (the one it
// starts from first), a closing line before a close that doesn't end where its contour began, a
// trailing move left out.
struct Iter {
    struct Rec {
        Verb verb;
        Offset pts[4];
        float w = 1;
    };
    explicit Iter(const Path& path) : c_(path.commands()) {}
    bool next(Rec& r) {
        if (i_ >= c_.size()) return false;
        const PathCommand& c = c_[i_];
        r.verb = c.verb;
        switch (c.verb) {
        case Verb::Move:
            if (i_ + 1 >= c_.size()) return false;  // a trailing move
            moveTo_ = last_ = r.pts[0] = {c.x, c.y};
            break;
        case Verb::Line:
            r.pts[0] = last_, r.pts[1] = last_ = {c.x, c.y};
            break;
        case Verb::Quad:
        case Verb::Conic:
            r.pts[0] = last_, r.pts[1] = {c.x1, c.y1}, r.pts[2] = last_ = {c.x, c.y}, r.w = c.w;
            break;
        case Verb::Cubic:
            r.pts[0] = last_, r.pts[1] = {c.x1, c.y1}, r.pts[2] = {c.x2, c.y2}, r.pts[3] = last_ = {c.x, c.y};
            break;
        case Verb::Close:
            if (!(last_ == moveTo_) && !std::isnan(last_.x) && !std::isnan(last_.y)) {  // the line back first
                r.verb = Verb::Line, r.pts[0] = last_, r.pts[1] = moveTo_;
                last_ = moveTo_;
                return true;
            }
            r.pts[0] = moveTo_;
            last_ = moveTo_;
            break;
        }
        ++i_;
        return true;
    }
    // Whether the contour being given ends with a close (SkPath::Iter::isClosedContour).
    bool isClosedContour() const {
        for (size_t k = i_; k < c_.size(); ++k) {
            if (c_[k].verb == Verb::Close) return true;
            if (c_[k].verb == Verb::Move && k > i_) return false;
        }
        return false;
    }

private:
    const std::vector<PathCommand>& c_;
    size_t i_ = 0;
    Offset moveTo_, last_;
};

void addConic(Path& dst, Offset c, Offset to, float w) { dst.conicTo(c.x, c.y, to.x, to.y, w); }

} // namespace

// ---- Skia's SkContourMeasure -------------------------------------------------------------------

namespace detail {

class ContourMeasure {
public:
    enum SegType : unsigned { Line, Quad, Cubic, ConicSeg };
    static constexpr int kMaxTValue = 0x3FFFFFFF;
    static constexpr float tValue(int t) { return float(t) * (1.0f / float(kMaxTValue)); }

    struct Segment {
        float distance;  // total distance up to this point
        unsigned ptIndex;
        unsigned tValue : 30;
        unsigned type : 2;
        float scalarT() const { return ContourMeasure::tValue(int(tValue)); }
    };

    std::vector<Segment> segments;
    std::vector<Offset> pts;  // a conic's weight in its first point's x, then its two points
    float length = 0;
    bool closed = false;

    // The segment `distance` (within 0..length) falls in, and the t there.
    const Segment* distanceToSegment(float distance, float& t) const {
        const Segment* base = segments.data();
        int count = int(segments.size());
        // (SkTKSearch)
        unsigned lo = 0, hi = unsigned(count - 1);
        while (lo < hi) {
            unsigned mid = (hi + lo) >> 1;
            if (base[mid].distance < distance) lo = mid + 1;
            else hi = mid;
        }
        int index = int(hi);
        if (base[hi].distance < distance) index = int(hi + 1);
        if (index >= count) index = count - 1;  // (Skia's ~index xor gives the insertion point)
        const Segment* seg = &base[index];
        float startT = 0, startD = 0;
        if (index > 0) {
            startD = seg[-1].distance;
            if (seg[-1].ptIndex == seg->ptIndex) startT = seg[-1].scalarT();
        }
        t = startT + (seg->scalarT() - startT) * (distance - startD) / (seg->distance - startD);
        return seg;
    }

    void posTan(const Segment& seg, float t, Offset* pos, Offset* tangent) const {
        const Offset* p = &pts[seg.ptIndex];
        switch (seg.type) {
        case Line:
            if (pos) *pos = {interp(p[0].x, p[1].x, t), interp(p[0].y, p[1].y, t)};
            if (tangent) *tangent = normalized(p[1].x - p[0].x, p[1].y - p[0].y);
            break;
        case Quad:
            if (pos) *pos = evalQuad(p, t);
            if (tangent) {
                Offset v = quadTangent(p, t);
                *tangent = normalized(v.x, v.y);
            }
            break;
        case ConicSeg: {
            Conic conic{{p[0], p[2], p[3]}, p[1].x};
            if (pos) *pos = conic.evalAt(t);
            if (tangent) {
                Offset v = conic.tangentAt(t);
                *tangent = normalized(v.x, v.y);
            }
        } break;
        case Cubic:
            if (pos) *pos = evalCubic(p, t);
            if (tangent) {
                Offset v = cubicTangent(p, t);
                *tangent = normalized(v.x, v.y);
            }
            break;
        }
    }

    bool getPosTan(float distance, Offset* pos, Offset* tangent) const {
        if (std::isnan(distance)) return false;
        distance = distance < 0 ? 0 : distance > length ? length : distance;
        float t;
        const Segment* seg = distanceToSegment(distance, t);
        if (std::isnan(t)) return false;
        posTan(*seg, t, pos, tangent);
        return true;
    }

    // (SkContourMeasure_segTo) The piece of a segment's curve from startT to stopT.
    void segTo(const Segment& seg, float startT, float stopT, Path& dst) const {
        const Offset* p = &pts[seg.ptIndex];
        if (startT == stopT) {
            Offset last;
            if (lastPoint(dst, last)) dst.lineTo(last.x, last.y);  // a zero-length dash: caps for it
            return;
        }
        Offset tmp0[7], tmp1[7];
        switch (seg.type) {
        case Line:
            if (stopT == 1) dst.lineTo(p[1].x, p[1].y);
            else dst.lineTo(interp(p[0].x, p[1].x, stopT), interp(p[0].y, p[1].y, stopT));
            break;
        case Quad:
            if (startT == 0) {
                if (stopT == 1) {
                    dst.quadraticTo(p[1].x, p[1].y, p[2].x, p[2].y);
                } else {
                    chopQuad(p, tmp0, stopT);
                    dst.quadraticTo(tmp0[1].x, tmp0[1].y, tmp0[2].x, tmp0[2].y);
                }
            } else {
                chopQuad(p, tmp0, startT);
                if (stopT == 1) {
                    dst.quadraticTo(tmp0[3].x, tmp0[3].y, tmp0[4].x, tmp0[4].y);
                } else {
                    chopQuad(&tmp0[2], tmp1, (stopT - startT) / (1 - startT));
                    dst.quadraticTo(tmp1[1].x, tmp1[1].y, tmp1[2].x, tmp1[2].y);
                }
            }
            break;
        case ConicSeg: {
            Conic conic{{p[0], p[2], p[3]}, p[1].x};
            if (startT == 0) {
                if (stopT == 1) {
                    addConic(dst, conic.pts[1], conic.pts[2], conic.w);
                } else {
                    Conic tmp[2];
                    if (conic.chopAt(stopT, tmp)) addConic(dst, tmp[0].pts[1], tmp[0].pts[2], tmp[0].w);
                }
            } else if (stopT == 1) {
                Conic tmp[2];
                if (conic.chopAt(startT, tmp)) addConic(dst, tmp[1].pts[1], tmp[1].pts[2], tmp[1].w);
            } else {
                Conic tmp;
                conic.chopAt(startT, stopT, tmp);
                addConic(dst, tmp.pts[1], tmp.pts[2], tmp.w);
            }
        } break;
        case Cubic:
            if (startT == 0) {
                if (stopT == 1) {
                    dst.cubicTo(p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y);
                } else {
                    chopCubic(p, tmp0, stopT);
                    dst.cubicTo(tmp0[1].x, tmp0[1].y, tmp0[2].x, tmp0[2].y, tmp0[3].x, tmp0[3].y);
                }
            } else {
                chopCubic(p, tmp0, startT);
                if (stopT == 1) {
                    dst.cubicTo(tmp0[4].x, tmp0[4].y, tmp0[5].x, tmp0[5].y, tmp0[6].x, tmp0[6].y);
                } else {
                    chopCubic(&tmp0[3], tmp1, (stopT - startT) / (1 - startT));
                    dst.cubicTo(tmp1[1].x, tmp1[1].y, tmp1[2].x, tmp1[2].y, tmp1[3].x, tmp1[3].y);
                }
            }
            break;
        }
    }

    bool getSegment(float startD, float stopD, Path& dst, bool startWithMoveTo) const {
        if (startD < 0) startD = 0;
        if (stopD > length) stopD = length;
        if (!(startD <= stopD)) return false;  // (NaNs too)
        if (segments.empty()) return false;
        float startT, stopT;
        const Segment* seg = distanceToSegment(startD, startT);
        if (!std::isfinite(startT)) return false;
        const Segment* stopSeg = distanceToSegment(stopD, stopT);
        if (!std::isfinite(stopT)) return false;
        if (startWithMoveTo) {
            Offset p;
            posTan(*seg, startT, &p, nullptr);
            dst.moveTo(p.x, p.y);
        }
        if (seg->ptIndex == stopSeg->ptIndex) {
            segTo(*seg, startT, stopT, dst);
        } else {
            do {
                segTo(*seg, startT, 1, dst);
                // (Segment::Next: the first segment of the next piece of the contour)
                unsigned at = seg->ptIndex;
                do ++seg;
                while (seg->ptIndex == at);
                startT = 0;
            } while (seg->ptIndex < stopSeg->ptIndex);
            segTo(*seg, 0, stopT, dst);
        }
        return true;
    }
};

namespace {

using CM = ContourMeasure;
constexpr float kCheapDistLimit = 0.5f;  // (CHEAP_DIST_LIMIT)
constexpr int kMaxRecursionDepth = 8;

bool tspanBigEnough(int tspan) { return (tspan >> 10) != 0; }

bool quadTooCurvy(const Offset p[3], float tolerance) {
    float dx = (p[1].x / 2.f) - (((p[0].x + p[2].x) / 2.f) / 2.f);
    float dy = (p[1].y / 2.f) - (((p[0].y + p[2].y) / 2.f) / 2.f);
    return std::max(std::abs(dx), std::abs(dy)) > tolerance;
}

bool conicTooCurvy(Offset first, Offset mid, Offset last, float tolerance) {
    Offset ends = (first + last) * 0.5f, d = mid - ends;
    return std::max(std::abs(d.x), std::abs(d.y)) > tolerance;
}

bool cheapDistExceedsLimit(Offset p, float x, float y, float tolerance) {
    return std::max(std::abs(x - p.x), std::abs(y - p.y)) > tolerance;
}

bool cubicTooCurvy(const Offset p[4], float tolerance) {
    return cheapDistExceedsLimit(p[1], interp(p[0].x, p[3].x, 1.f / 3), interp(p[0].y, p[3].y, 1.f / 3), tolerance) ||
           cheapDistExceedsLimit(p[2], interp(p[0].x, p[3].x, 2.f / 3), interp(p[0].y, p[3].y, 2.f / 3), tolerance);
}

// (SkContourMeasureIter::Impl) Measures a path's contours one after another.
struct ContourBuilder {
    const std::vector<PathCommand>& cmds;
    size_t at = 0;
    float tolerance;
    bool forceClosed;
    CM* cm = nullptr;

    void addSeg(float distance, unsigned ptIndex, unsigned type, int t) {
        cm->segments.push_back({distance, ptIndex, unsigned(t), type});
    }

    float lineSeg(Offset p0, Offset p1, float distance, unsigned ptIndex) {
        float d = kompoz::distance(p0, p1), prev = distance;
        distance += d;
        if (distance > prev) addSeg(distance, ptIndex, CM::Line, CM::kMaxTValue);
        return distance;
    }

    float quadSegs(const Offset p[3], float distance, int mint, int maxt, unsigned ptIndex, int depth = 0) {
        if (depth < kMaxRecursionDepth && tspanBigEnough(maxt - mint) && quadTooCurvy(p, tolerance)) {
            Offset tmp[5];
            int half = (mint + maxt) >> 1;
            chopQuad(p, tmp, 0.5f);
            distance = quadSegs(tmp, distance, mint, half, ptIndex, depth + 1);
            distance = quadSegs(&tmp[2], distance, half, maxt, ptIndex, depth + 1);
        } else {
            float d = kompoz::distance(p[0], p[2]), prev = distance;
            distance += d;
            if (distance > prev) addSeg(distance, ptIndex, CM::Quad, maxt);
        }
        return distance;
    }

    float conicSegs(const Conic& conic, float distance, int mint, Offset minPt, int maxt, Offset maxPt, unsigned ptIndex, int depth = 0) {
        int half = (mint + maxt) >> 1;
        Offset halfPt = conic.evalAt(CM::tValue(half));
        if (!finite(halfPt)) return distance;
        if (depth < kMaxRecursionDepth && tspanBigEnough(maxt - mint) && conicTooCurvy(minPt, halfPt, maxPt, tolerance)) {
            distance = conicSegs(conic, distance, mint, minPt, half, halfPt, ptIndex, depth + 1);
            distance = conicSegs(conic, distance, half, halfPt, maxt, maxPt, ptIndex, depth + 1);
        } else {
            float d = kompoz::distance(minPt, maxPt), prev = distance;
            distance += d;
            if (distance > prev) addSeg(distance, ptIndex, CM::ConicSeg, maxt);
        }
        return distance;
    }

    float cubicSegs(const Offset p[4], float distance, int mint, int maxt, unsigned ptIndex, int depth = 0) {
        if (depth < kMaxRecursionDepth && tspanBigEnough(maxt - mint) && cubicTooCurvy(p, tolerance)) {
            Offset tmp[7];
            int half = (mint + maxt) >> 1;
            chopCubic(p, tmp, 0.5f);
            distance = cubicSegs(tmp, distance, mint, half, ptIndex, depth + 1);
            distance = cubicSegs(&tmp[3], distance, half, maxt, ptIndex, depth + 1);
        } else {
            float d = kompoz::distance(p[0], p[3]), prev = distance;
            distance += d;
            if (distance > prev) addSeg(distance, ptIndex, CM::Cubic, maxt);
        }
        return distance;
    }

    // The next contour's measure: null if it has no length (or isn't finite).
    std::shared_ptr<CM> build() {
        auto measure = std::make_shared<CM>();
        cm = measure.get();
        int ptIndex = -1;
        float distance = 0;
        bool seenClose = forceClosed, seenMove = false;
        Offset last;
        for (; at < cmds.size(); ++at) {
            const PathCommand& c = cmds[at];
            if (seenMove && c.verb == Verb::Move) break;
            Offset to{c.x, c.y};
            switch (c.verb) {
            case Verb::Move:
                ptIndex += 1;
                cm->pts.push_back(to);
                seenMove = true;
                break;
            case Verb::Line: {
                float prev = distance;
                distance = lineSeg(last, to, distance, unsigned(ptIndex));
                if (distance > prev) cm->pts.push_back(to), ptIndex += 1;
            } break;
            case Verb::Quad: {
                Offset p[3] = {last, {c.x1, c.y1}, to};
                float prev = distance;
                distance = quadSegs(p, distance, 0, CM::kMaxTValue, unsigned(ptIndex));
                if (distance > prev) cm->pts.push_back(p[1]), cm->pts.push_back(to), ptIndex += 2;
            } break;
            case Verb::Conic: {
                Conic conic{{last, {c.x1, c.y1}, to}, c.w};
                float prev = distance;
                distance = conicSegs(conic, distance, 0, conic.pts[0], CM::kMaxTValue, conic.pts[2], unsigned(ptIndex));
                if (distance > prev) {
                    cm->pts.push_back({conic.w, 0});
                    cm->pts.push_back(conic.pts[1]), cm->pts.push_back(to);
                    ptIndex += 3;
                }
            } break;
            case Verb::Cubic: {
                Offset p[4] = {last, {c.x1, c.y1}, {c.x2, c.y2}, to};
                float prev = distance;
                distance = cubicSegs(p, distance, 0, CM::kMaxTValue, unsigned(ptIndex));
                if (distance > prev) cm->pts.push_back(p[1]), cm->pts.push_back(p[2]), cm->pts.push_back(to), ptIndex += 3;
            } break;
            case Verb::Close:
                seenClose = true;
                break;
            }
            if (c.verb != Verb::Close) last = to;
        }
        if (!std::isfinite(distance) || cm->segments.empty()) return nullptr;
        if (seenClose) {
            float prev = distance;
            Offset first = cm->pts[0];
            distance = lineSeg(cm->pts[size_t(ptIndex)], first, distance, unsigned(ptIndex));
            if (distance > prev) cm->pts.push_back(first);
        }
        cm->length = distance;
        cm->closed = seenClose;
        return measure;
    }
};

bool finitePath(const Path& path) {
    for (const PathCommand& c : path.commands())
        if (!std::isfinite(c.x1) || !std::isfinite(c.y1) || !std::isfinite(c.x2) || !std::isfinite(c.y2) || !std::isfinite(c.x) ||
            !std::isfinite(c.y) || !std::isfinite(c.w))
            return false;
    return true;
}

} // namespace

// Every contour of a path with a length, measured (SkContourMeasureIter).
std::vector<std::shared_ptr<const ContourMeasure>> measureContours(const Path& path, bool forceClosed, float resScale) {
    std::vector<std::shared_ptr<const ContourMeasure>> out;
    if (!finitePath(path)) return out;
    ContourBuilder b{path.commands(), 0, kCheapDistLimit * (1.0f / resScale), forceClosed};
    while (b.at < b.cmds.size())
        if (auto cm = b.build()) out.push_back(std::move(cm));
    return out;
}

} // namespace detail

// ---- PathMeasure (SkPathMeasure) ---------------------------------------------------------------

void PathMeasure::setPath(const Path& path, bool forceClosed, float resScale) {
    contours_ = detail::measureContours(path, forceClosed, resScale);
    current_ = 0;
}

float PathMeasure::length() const { return current_ < contours_.size() ? contours_[current_]->length : 0; }

bool PathMeasure::isClosed() const { return current_ < contours_.size() && contours_[current_]->closed; }

bool PathMeasure::nextContour() {
    if (current_ >= contours_.size()) return false;
    return ++current_ < contours_.size();
}

Offset PathMeasure::getPosition(float distance) const {
    Offset p;
    if (current_ >= contours_.size() || !contours_[current_]->getPosTan(distance, &p, nullptr)) return {NAN, NAN};
    return p;
}

Offset PathMeasure::getTangent(float distance) const {
    Offset t;
    if (current_ >= contours_.size() || !contours_[current_]->getPosTan(distance, nullptr, &t)) return {NAN, NAN};
    return t;
}

bool PathMeasure::getSegment(float startDistance, float stopDistance, Path& destination, bool startWithMoveTo) const {
    // (Android's PathMeasure: the distances kept within the contour, nothing unless start is under stop)
    float length = this->length();
    if (startDistance < 0) startDistance = 0;
    if (stopDistance > length) stopDistance = length;
    if (!(startDistance < stopDistance)) return false;
    return contours_[current_]->getSegment(startDistance, stopDistance, destination, startWithMoveTo);
}

// ---- Path effects -------------------------------------------------------------------------------

struct PathEffect::Node {
    enum class Kind { Dash, Corner, Stamp, Chain } kind;
    // Dash (SkDashImpl): its intervals, its phase within them, where the first dash starts.
    std::vector<float> intervals;
    float phase = 0, initialDashLength = -1, intervalLength = 0;
    size_t initialDashIndex = 0;
    // Corner.
    float radius = 0;
    // Stamp (SkPath1DPathEffectImpl): its shape, advance, offset along each contour, style.
    Path shape;
    float advance = 0, initialOffset = 0;
    StampedPathEffectStyle style = StampedPathEffectStyle::Translate;
    // Chain: inner's effect, then outer's.
    std::shared_ptr<const Node> outer, inner;

    bool filter(Path& dst, const Path& src, PathStroke& stroke, float resScale) const;

    friend bool operator==(const Node& a, const Node& b) {
        auto same = [](const std::shared_ptr<const Node>& x, const std::shared_ptr<const Node>& y) { return x == y || (x && y && *x == *y); };
        return a.kind == b.kind && a.intervals == b.intervals && a.phase == b.phase && a.radius == b.radius && a.shape == b.shape &&
               a.advance == b.advance && a.initialOffset == b.initialOffset && a.style == b.style && same(a.outer, b.outer) &&
               same(a.inner, b.inner);
    }
};

bool operator==(const PathEffect& a, const PathEffect& b) { return a.node_ == b.node_ || (a.node_ && b.node_ && *a.node_ == *b.node_); }

namespace {

constexpr float kMaxDashCount = 1000000;
constexpr float kNearlyZero = 1.f / (1 << 12);  // (SK_ScalarNearlyZero)

bool isEven(size_t x) { return (x & 1) == 0; }

// (SkDashPath's SpecialLineRec) A line dashed with butt caps: each dash its rectangle, filled.
struct SpecialLine {
    Offset pts[2], tangent, normal;
    float pathLength = 0;

    bool init(const Path& src, PathStroke& stroke, size_t intervalCount, float intervalLength) {
        if (stroke.hairline || !isLine(src, pts)) return false;
        if (stroke.cap != StrokeCap::Butt) return false;
        pathLength = distance(pts[0], pts[1]);
        tangent = pts[1] - pts[0];
        if (tangent.x == 0 && tangent.y == 0) return false;
        tangent = tangent * (1.0f / pathLength);
        if (!finite(tangent)) return false;
        normal = Offset{tangent.y, -tangent.x} * (stroke.width / 2.f);  // (RotateCCW)
        float ptCount = std::min(pathLength * float(intervalCount) / intervalLength, kMaxDashCount);
        if (std::isnan(ptCount)) return false;
        stroke = PathStroke{};  // filled: the dashes are their own outlines
        return true;
    }
    void addSegment(float d0, float d1, Path& dst) const {
        if (d1 > pathLength) d1 = pathLength;
        float x0 = pts[0].x + tangent.x * d0, x1 = pts[0].x + tangent.x * d1;
        float y0 = pts[0].y + tangent.y * d0, y1 = pts[0].y + tangent.y * d1;
        dst.moveTo(x0 + normal.x, y0 + normal.y);
        dst.lineTo(x1 + normal.x, y1 + normal.y);
        dst.lineTo(x1 - normal.x, y1 - normal.y);
        dst.lineTo(x0 - normal.x, y0 - normal.y);
    }
};

// (SkDashPath::InternalFilter, without a cull rect)
bool dashFilter(Path& dst, const Path& source, PathStroke& stroke, float resScale, const PathEffect::Node& e) {
    if (source.isEmpty()) return true;
    if (!stroke.strokes()) return false;  // a fill is not dashed
    const std::vector<float>& intervals = e.intervals;
    size_t count = intervals.size();
    Path zeroLine;
    const Path* src = &source;
    Offset line[2];
    if (isLine(source, line) && line[0] == line[1]) {  // (cull_path: a zero-length line made a hair long)
        line[1].x += std::max(1.001f, line[1].x) * kNearlyZero;
        zeroLine.moveTo(line[0].x, line[0].y);
        zeroLine.lineTo(line[1].x, line[1].y);
        src = &zeroLine;
    }
    SpecialLine special;
    bool specialLine = special.init(*src, stroke, count >> 1, e.intervalLength);
    float dashCount = 0;
    for (const auto& cm : detail::measureContours(*src, false, resScale)) {
        bool skipFirstSegment = cm->closed, addedSegment = false;
        float length = cm->length;
        size_t index = e.initialDashIndex;
        dashCount += length * float(count >> 1) / e.intervalLength;
        if (dashCount > kMaxDashCount) {
            dst.reset();
            return false;
        }
        double distance = 0, dlen = e.initialDashLength;
        while (distance < length) {
            addedSegment = false;
            if (isEven(index) && !skipFirstSegment) {
                addedSegment = true;
                if (specialLine) special.addSegment(float(distance), float(distance + dlen), dst);
                else cm->getSegment(float(distance), float(distance + dlen), dst, true);
            }
            distance += dlen;
            skipFirstSegment = false;
            if (++index == count) index = 0;
            dlen = intervals[index];
        }
        // A closed contour ending in a dash joins the first one (skipped at the start).
        if (cm->closed && isEven(e.initialDashIndex) && e.initialDashLength >= 0) cm->getSegment(0, e.initialDashLength, dst, !addedSegment);
    }
    return true;
}

// (SkCornerPathEffectImpl::onFilterPath)
bool cornerFilter(Path& dst, const Path& src, float radius) {
    if (radius <= 0) return false;
    auto step = [radius](Offset a, Offset b, Offset& s) {  // (ComputeStep)
        float d = distance(a, b);
        s = b - a;
        if (d <= radius * 2) {
            s = s * 0.5f;
            return false;
        }
        s = s * (radius / d);
        return true;
    };
    bool prevValid = true, havePrev = false;
    Verb prevVerb = Verb::Close;
    Offset moveTo, lastCorner, firstStep, s;
    Iter iter(src);
    Iter::Rec r;
    while (iter.next(r)) {
        const Offset* p = r.pts;
        switch (r.verb) {
        case Verb::Move:
            if (havePrev && prevVerb == Verb::Line) dst.lineTo(lastCorner.x, lastCorner.y);  // the open contour before ends
            if (iter.isClosedContour()) {
                moveTo = p[0];
                prevValid = false;
            } else {
                dst.moveTo(p[0].x, p[0].y);
                prevValid = true;
            }
            break;
        case Verb::Line: {
            bool drawSegment = step(p[0], p[1], s);
            if (!prevValid) {
                dst.moveTo(moveTo.x + s.x, moveTo.y + s.y);
                prevValid = true;
            } else {
                dst.quadraticTo(p[0].x, p[0].y, p[0].x + s.x, p[0].y + s.y);
            }
            if (drawSegment) dst.lineTo(p[1].x - s.x, p[1].y - s.y);
            lastCorner = p[1];
            prevValid = true;
        } break;
        case Verb::Quad:
        case Verb::Conic:
        case Verb::Cubic:  // curves as they are
            if (!prevValid) {
                dst.moveTo(p[0].x, p[0].y);
                prevValid = true;
            }
            if (r.verb == Verb::Quad) dst.quadraticTo(p[1].x, p[1].y, p[2].x, p[2].y), lastCorner = p[2];
            else if (r.verb == Verb::Conic) addConic(dst, p[1], p[2], r.w), lastCorner = p[2];
            else dst.cubicTo(p[1].x, p[1].y, p[2].x, p[2].y, p[3].x, p[3].y), lastCorner = p[3];
            firstStep = {};
            break;
        case Verb::Close:
            if (firstStep.x != 0 || firstStep.y != 0)
                dst.quadraticTo(lastCorner.x, lastCorner.y, lastCorner.x + firstStep.x, lastCorner.y + firstStep.y);
            dst.close();
            prevValid = false;
            break;
        }
        if (havePrev && prevVerb == Verb::Move) firstStep = s;
        prevVerb = r.verb;
        havePrev = true;
    }
    if (prevValid) dst.lineTo(lastCorner.x, lastCorner.y);
    return true;
}

// A point of the stamp at `dist` along the contour, bent along it (morphpoints).
bool morphPoint(const detail::ContourMeasure& cm, float dist, Offset s, Offset& out) {
    Offset pos, tan;
    if (!cm.getPosTan(dist + s.x, &pos, &tan)) return false;
    // setSinCos(tan.y, tan.x), preTranslate(-s.x, 0), postTranslate(pos): (s.x, s.y) mapped.
    float tx = tan.x * -s.x + pos.x, ty = tan.y * -s.x + pos.y;
    out = {tan.x * s.x + -tan.y * s.y + tx, tan.y * s.x + tan.x * s.y + ty};
    return true;
}

// (SkPath1DPathEffectImpl) The stamp at each advance along each contour, filled.
bool stampFilter(Path& dst, const Path& src, PathStroke& stroke, const PathEffect::Node& e) {
    stroke = PathStroke{};
    constexpr int kMaxIterations = 100000;  // (MAX_REASONABLE_ITERATIONS)
    for (const auto& cm : detail::measureContours(src, false, 1)) {
        int governor = kMaxIterations;
        for (float d = e.initialOffset; d < cm->length && --governor >= 0; d += e.advance) {
            switch (e.style) {
            case StampedPathEffectStyle::Translate: {
                Offset pos;
                if (cm->getPosTan(d, &pos, nullptr)) dst.addPath(e.shape, pos);
            } break;
            case StampedPathEffectStyle::Rotate: {
                Offset pos, tan;
                if (!cm->getPosTan(d, &pos, &tan)) break;
                auto map = [&](float x, float y) { return Offset{tan.x * x - tan.y * y + pos.x, tan.y * x + tan.x * y + pos.y}; };
                for (const PathCommand& c : e.shape.commands()) {
                    Offset p1 = map(c.x1, c.y1), p2 = map(c.x2, c.y2), to = map(c.x, c.y);
                    switch (c.verb) {
                    case Verb::Move: dst.moveTo(to.x, to.y); break;
                    case Verb::Line: dst.lineTo(to.x, to.y); break;
                    case Verb::Quad: dst.quadraticTo(p1.x, p1.y, to.x, to.y); break;
                    case Verb::Conic: addConic(dst, p1, to, c.w); break;
                    case Verb::Cubic: dst.cubicTo(p1.x, p1.y, p2.x, p2.y, to.x, to.y); break;
                    case Verb::Close: dst.close(); break;
                    }
                }
            } break;
            case StampedPathEffectStyle::Morph: {  // (morphpath: a line bent as a quadratic)
                Iter iter(e.shape);
                Iter::Rec r;
                Offset q[3];
                while (iter.next(r)) {
                    const Offset* p = r.pts;
                    auto morph = [&](int from, int n) {
                        for (int k = 0; k < n; ++k)
                            if (!morphPoint(*cm, d, p[from + k], q[k])) return false;
                        return true;
                    };
                    switch (r.verb) {
                    case Verb::Move:
                        if (morph(0, 1)) dst.moveTo(q[0].x, q[0].y);
                        break;
                    case Verb::Line: {
                        Offset mid{(p[0].x + p[1].x) * 0.5f, (p[0].y + p[1].y) * 0.5f};  // (sk_float_midpoint)
                        if (morphPoint(*cm, d, mid, q[0]) && morphPoint(*cm, d, p[1], q[1])) dst.quadraticTo(q[0].x, q[0].y, q[1].x, q[1].y);
                    } break;
                    case Verb::Quad:
                        if (morph(1, 2)) dst.quadraticTo(q[0].x, q[0].y, q[1].x, q[1].y);
                        break;
                    case Verb::Conic:
                        if (morph(1, 2)) addConic(dst, q[0], q[1], r.w);
                        break;
                    case Verb::Cubic:
                        if (morph(1, 3)) dst.cubicTo(q[0].x, q[0].y, q[1].x, q[1].y, q[2].x, q[2].y);
                        break;
                    case Verb::Close: dst.close(); break;
                    }
                }
            } break;
            }
        }
        if (governor < 0) return false;
    }
    return true;
}

} // namespace

bool PathEffect::Node::filter(Path& dst, const Path& src, PathStroke& stroke, float resScale) const {
    switch (kind) {
    case Kind::Dash: return dashFilter(dst, src, stroke, resScale, *this);
    case Kind::Corner: return cornerFilter(dst, src, radius);
    case Kind::Stamp: return stampFilter(dst, src, stroke, *this);
    case Kind::Chain: {  // (SkComposePathEffect)
        Path tmp;
        const Path* ptr = &src;
        if (inner->filter(dst, src, stroke, resScale)) {
            tmp = std::move(dst);
            dst = Path();
            ptr = &tmp;
        }
        return outer->filter(dst, *ptr, stroke, resScale);
    }
    }
    return false;
}

float PathEffect::reach() const {
    auto of = [](auto& self, const Node* n) -> float {
        if (!n) return 0;
        switch (n->kind) {
        case Node::Kind::Stamp: {
            Rect b = n->shape.bounds();
            float far = 0;
            for (float x : {b.x, b.right()})
                for (float y : {b.y, b.bottom()}) far = std::max(far, std::hypot(x, y));
            return far;
        }
        case Node::Kind::Chain: return self(self, n->outer.get()) + self(self, n->inner.get());
        default: return 0;
        }
    };
    return of(of, node_.get());
}

bool PathEffect::filterPath(Path& dst, const Path& src, PathStroke& stroke, float resScale) const {
    // (What it makes winds non-zero, as a Skia path builder's does.)
    return node_ && node_->filter(dst, src, stroke, resScale);
}

PathEffect PathEffect::dashPathEffect(std::span<const float> intervals, float phase) {
    intervals = intervals.first(intervals.size() & ~size_t(1));  // (Android's: an even count, the odd one left out)
    // (SkDashPath::ValidDashPath)
    if (intervals.size() < 2) return {};
    float length = 0;
    for (float v : intervals) {
        if (v < 0) return {};
        length += v;
    }
    if (!(length > 0) || !std::isfinite(phase) || !std::isfinite(length)) return {};
    auto n = std::make_shared<Node>();
    n->kind = Node::Kind::Dash;
    n->intervals.assign(intervals.begin(), intervals.end());
    n->intervalLength = length;
    // (SkDashPath::CalcDashParameters) The phase within the pattern, a negative one from its end.
    if (phase < 0) {
        phase = -phase;
        if (phase > length) phase = std::fmod(phase, length);
        phase = length - phase;
        if (phase == length) phase = 0;
    } else if (phase >= length) {
        phase = std::fmod(phase, length);
    }
    n->phase = phase;
    // (find_first_interval)
    n->initialDashIndex = 0, n->initialDashLength = intervals[0];
    for (size_t i = 0; i < intervals.size(); ++i) {
        float gap = intervals[i];
        if (phase > gap || (phase == gap && gap != 0)) {
            phase -= gap;
        } else {
            n->initialDashIndex = i, n->initialDashLength = gap - phase;
            break;
        }
    }
    PathEffect e;
    e.node_ = std::move(n);
    return e;
}

PathEffect PathEffect::cornerPathEffect(float radius) {
    if (!std::isfinite(radius) || radius <= 0) return {};
    auto n = std::make_shared<Node>();
    n->kind = Node::Kind::Corner;
    n->radius = radius;
    PathEffect e;
    e.node_ = std::move(n);
    return e;
}

PathEffect PathEffect::stampedPathEffect(const Path& shape, float advance, float phase, StampedPathEffectStyle style) {
    if (advance <= 0 || !std::isfinite(advance) || !std::isfinite(phase) || shape.isEmpty()) return {};
    // The phase turned into an offset along the path (as PostScript's).
    if (phase < 0) {
        phase = -phase;
        if (phase > advance) phase = std::fmod(phase, advance);
    } else {
        if (phase > advance) phase = std::fmod(phase, advance);
        phase = advance - phase;
    }
    if (phase >= advance) phase = 0;
    auto n = std::make_shared<Node>();
    n->kind = Node::Kind::Stamp;
    n->shape = shape;
    n->advance = advance;
    n->initialOffset = phase;
    n->style = style;
    PathEffect e;
    e.node_ = std::move(n);
    return e;
}

PathEffect PathEffect::chainPathEffect(const PathEffect& outer, const PathEffect& inner) {
    if (!outer.node_) return inner;
    if (!inner.node_) return outer;
    auto n = std::make_shared<Node>();
    n->kind = Node::Kind::Chain;
    n->outer = outer.node_;
    n->inner = inner.node_;
    PathEffect e;
    e.node_ = std::move(n);
    return e;
}

float detail::resScaleOf(float scaleX, float skewY, float skewX, float scaleY) {
    float sx = length(scaleX, skewY), sy = length(skewX, scaleY);
    if (std::isfinite(sx) && std::isfinite(sy)) {
        float scale = std::max(sx, sy);
        if (scale > 0) return std::min(scale, 1e5f);
    }
    return 1;
}

const Path& detail::pathEffected(const Path& path, PathStroke& stroke, float resScale, Path& effected) {
    std::optional<PathEffect> effect = std::move(stroke.pathEffect);
    stroke.pathEffect.reset();
    if (effect && effect->filterPath(effected, path, stroke, resScale)) return effected;
    return path;
}

} // namespace kompoz
