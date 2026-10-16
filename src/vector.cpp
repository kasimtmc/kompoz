#include <kompoz/animation.hpp>
#include <kompoz/graphics.hpp>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <optional>
#include <map>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <string>

namespace kompoz {

struct ImageVector::Data {
    std::vector<PathCommand> commands;  // a Builder's: its filled paths' together (drawn tinted)
    std::vector<VectorPath> paths;      // a Builder's
    float viewportWidth = 24, viewportHeight = 24, defaultWidth = 24, defaultHeight = 24;
    uint64_t id = 0;
    bool autoMirror = false;
    std::shared_ptr<const Data> base;  // an autoMirrored() one: the vector it was made from (its mirror is shared)
    mutable std::once_flag mirrorOnce;
    mutable std::shared_ptr<const Data> mirror;  // mirrored(), made the first time it is asked for
};

namespace {
std::atomic<uint64_t> gNextVectorId{1};
}

namespace {

// Splits SVG path data into command letters and numbers ("1.41-1.41" is two numbers,
// so is "0.5.5").
class PathTokenizer {
public:
    explicit PathTokenizer(std::string_view s) : s_(s) {}

    void skipSeparators() {
        while (i_ < s_.size() && (std::isspace(static_cast<unsigned char>(s_[i_])) || s_[i_] == ',')) ++i_;
    }
    bool atEnd() {
        skipSeparators();
        return i_ >= s_.size();
    }
    bool atCommand() {
        skipSeparators();
        return i_ < s_.size() && std::isalpha(static_cast<unsigned char>(s_[i_])) && s_[i_] != 'e' && s_[i_] != 'E';
    }
    char command() { return s_[i_++]; }
    // An arc's flag: a single 0 or 1 (flags may run together, "a1 1 0 01.5.5").
    bool flag(bool& out) {
        skipSeparators();
        if (i_ >= s_.size() || (s_[i_] != '0' && s_[i_] != '1')) return false;
        out = s_[i_++] == '1';
        return true;
    }

    bool number(float& out) {
        skipSeparators();
        size_t start = i_;
        if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
        bool digits = false, dot = false;
        while (i_ < s_.size()) {
            char c = s_[i_];
            if (std::isdigit(static_cast<unsigned char>(c))) {
                digits = true;
            } else if (c == '.' && !dot) {
                dot = true;
            } else if ((c == 'e' || c == 'E') && digits) {
                ++i_;
                if (i_ < s_.size() && (s_[i_] == '-' || s_[i_] == '+')) ++i_;
                continue;
            } else {
                break;
            }
            ++i_;
        }
        if (!digits) {
            i_ = start;
            return false;
        }
        out = std::strtof(std::string(s_.substr(start, i_ - start)).c_str(), nullptr);
        return true;
    }

private:
    std::string_view s_;
    size_t i_ = 0;
};

} // namespace

namespace {
// An SVG elliptical arc from (x0, y0) to (x, y) as cubics of at most a quarter turn each (the
// spec's endpoint to center conversion, then 4/3 tan(delta/4) handles).
void arcToCubics(std::vector<PathCommand>& out, float x0, float y0, float rx, float ry, float degrees, bool largeArc, bool sweep, float x,
                 float y) {
    if (x0 == x && y0 == y) return;
    if (rx == 0 || ry == 0) {  // no radius: a line
        out.push_back({PathCommand::Verb::Line, 0, 0, 0, 0, x, y});
        return;
    }
    constexpr double kPi = 3.14159265358979323846;
    double prx = std::abs(rx), pry = std::abs(ry);
    double phi = degrees * kPi / 180, c = std::cos(phi), s = std::sin(phi);
    double dx = (x0 - x) / 2.0, dy = (y0 - y) / 2.0;
    double x1 = c * dx + s * dy, y1 = -s * dx + c * dy;
    double lambda = x1 * x1 / (prx * prx) + y1 * y1 / (pry * pry);
    if (lambda > 1) prx *= std::sqrt(lambda), pry *= std::sqrt(lambda);  // too small to reach: as small as reaches
    double num = prx * prx * pry * pry - prx * prx * y1 * y1 - pry * pry * x1 * x1, den = prx * prx * y1 * y1 + pry * pry * x1 * x1;
    double k = (den > 0 ? std::sqrt(std::max(0.0, num / den)) : 0) * (largeArc == sweep ? -1 : 1);
    double cxp = k * prx * y1 / pry, cyp = -k * pry * x1 / prx;
    double cx = c * cxp - s * cyp + (x0 + x) / 2.0, cy = s * cxp + c * cyp + (y0 + y) / 2.0;
    auto angle = [](double ux, double uy, double vx, double vy) { return std::atan2(ux * vy - uy * vx, ux * vx + uy * vy); };
    double ux = (x1 - cxp) / prx, uy = (y1 - cyp) / pry, vx = (-x1 - cxp) / prx, vy = (-y1 - cyp) / pry;
    double theta = angle(1, 0, ux, uy), delta = angle(ux, uy, vx, vy);
    if (!sweep && delta > 0) delta -= 2 * kPi;
    if (sweep && delta < 0) delta += 2 * kPi;
    int segments = std::max(1, int(std::ceil(std::abs(delta) / (kPi / 2) - 1e-9)));
    double step = delta / segments, t = 4.0 / 3.0 * std::tan(step / 4);
    auto point = [&](double a) { return std::pair{cx + prx * std::cos(a) * c - pry * std::sin(a) * s, cy + prx * std::cos(a) * s + pry * std::sin(a) * c}; };
    auto tangent = [&](double a) { return std::pair{-prx * std::sin(a) * c - pry * std::cos(a) * s, -prx * std::sin(a) * s + pry * std::cos(a) * c}; };
    for (int i = 0; i < segments; ++i) {
        double a1 = theta + i * step, a2 = a1 + step;
        auto [p1x, p1y] = point(a1);
        auto [p2x, p2y] = i == segments - 1 ? std::pair<double, double>{x, y} : point(a2);  // ends exactly where asked
        auto [d1x, d1y] = tangent(a1);
        auto [d2x, d2y] = tangent(a2);
        out.push_back({PathCommand::Verb::Cubic, float(p1x + t * d1x), float(p1y + t * d1y), float(p2x - t * d2x), float(p2y - t * d2y), float(p2x),
                       float(p2y)});
    }
}
} // namespace

std::vector<PathCommand> VectorPath::parse(std::string_view pathData) {
    std::vector<PathCommand> out;

    PathTokenizer t(pathData);
    float cx = 0, cy = 0, sx = 0, sy = 0;  // current point, subpath start
    float lcx = 0, lcy = 0;                // last control point, for S and T
    char last = 0, cmd = 0;
    bool needMove = false;  // drawing right after Z starts a new subpath at the old start
    while (!t.atEnd()) {
        if (t.atCommand()) cmd = t.command();
        else if (!cmd) break;
        bool rel = std::islower(static_cast<unsigned char>(cmd));
        float ox = rel ? cx : 0, oy = rel ? cy : 0;
        float v[6];
        auto read = [&](int n) {
            for (int k = 0; k < n; ++k)
                if (!t.number(v[k])) return false;
            return true;
        };
        char upper = char(std::toupper(static_cast<unsigned char>(cmd)));
        if (needMove && upper != 'M' && upper != 'Z') out.push_back({PathCommand::Verb::Move, 0, 0, 0, 0, sx, sy});
        needMove = false;
        switch (upper) {
        case 'M':
            if (!read(2)) goto done;
            cx = sx = ox + v[0];
            cy = sy = oy + v[1];
            out.push_back({PathCommand::Verb::Move, 0, 0, 0, 0, cx, cy});
            cmd = rel ? 'l' : 'L';  // further pairs are line-tos
            break;
        case 'L':
            if (!read(2)) goto done;
            cx = ox + v[0];
            cy = oy + v[1];
            out.push_back({PathCommand::Verb::Line, 0, 0, 0, 0, cx, cy});
            break;
        case 'H':
            if (!read(1)) goto done;
            cx = ox + v[0];
            out.push_back({PathCommand::Verb::Line, 0, 0, 0, 0, cx, cy});
            break;
        case 'V':
            if (!read(1)) goto done;
            cy = oy + v[0];
            out.push_back({PathCommand::Verb::Line, 0, 0, 0, 0, cx, cy});
            break;
        case 'C':
            if (!read(6)) goto done;
            out.push_back({PathCommand::Verb::Cubic, ox + v[0], oy + v[1], ox + v[2], oy + v[3], ox + v[4], oy + v[5]});
            lcx = ox + v[2];
            lcy = oy + v[3];
            cx = ox + v[4];
            cy = oy + v[5];
            break;
        case 'S': {
            if (!read(4)) goto done;
            bool smooth = last == 'C' || last == 'S';
            float x1 = smooth ? 2 * cx - lcx : cx, y1 = smooth ? 2 * cy - lcy : cy;
            out.push_back({PathCommand::Verb::Cubic, x1, y1, ox + v[0], oy + v[1], ox + v[2], oy + v[3]});
            lcx = ox + v[0];
            lcy = oy + v[1];
            cx = ox + v[2];
            cy = oy + v[3];
            break;
        }
        case 'Q':
            if (!read(4)) goto done;
            out.push_back({PathCommand::Verb::Quad, ox + v[0], oy + v[1], 0, 0, ox + v[2], oy + v[3]});
            lcx = ox + v[0];
            lcy = oy + v[1];
            cx = ox + v[2];
            cy = oy + v[3];
            break;
        case 'T': {
            if (!read(2)) goto done;
            bool smooth = last == 'Q' || last == 'T';
            float x1 = smooth ? 2 * cx - lcx : cx, y1 = smooth ? 2 * cy - lcy : cy;
            out.push_back({PathCommand::Verb::Quad, x1, y1, 0, 0, ox + v[0], oy + v[1]});
            lcx = x1;
            lcy = y1;
            cx = ox + v[0];
            cy = oy + v[1];
            break;
        }
        case 'Z':
            out.push_back({PathCommand::Verb::Close});
            cx = sx;
            cy = sy;
            needMove = true;
            cmd = 0;
            break;
        case 'A': {
            bool largeArc = false, sweep = false;
            if (!read(3) || !t.flag(largeArc) || !t.flag(sweep) || !t.number(v[3]) || !t.number(v[4])) goto done;
            float x = ox + v[3], y = oy + v[4];
            arcToCubics(out, cx, cy, v[0], v[1], v[2], largeArc, sweep, x, y);
            cx = x;
            cy = y;
            break;
        }
        default:
            std::fprintf(stderr, "kompoz: unsupported path command '%c'\n", cmd);
            goto done;
        }
        last = upper;
    }
done:
    return out;
}

ImageVector ImageVector::fromSvgPath(std::string_view pathData, float viewportWidth, float viewportHeight) {
    auto data = std::make_shared<Data>();
    data->viewportWidth = data->defaultWidth = viewportWidth;
    data->viewportHeight = data->defaultHeight = viewportHeight;
    data->id = gNextVectorId++;
    data->commands = VectorPath::parse(pathData);
    ImageVector vector;
    vector.data_ = std::move(data);
    return vector;
}

namespace {
// Just enough XML for vector drawables: elements, their attributes (a prefix like "android:"
// dropped), nesting; comments, declarations and text skipped.
struct XmlTag {
    std::string name;  // without a prefix
    std::map<std::string, std::string> attributes;
    bool closing = false, selfClosing = false;
};
class XmlReader {
public:
    explicit XmlReader(std::string_view s) : s_(s) {}
    std::optional<XmlTag> next() {
        while (true) {
            size_t lt = s_.find('<', i_);
            if (lt == std::string_view::npos) return std::nullopt;
            i_ = lt;
            if (s_.substr(i_, 4) == "<!--") {
                size_t end = s_.find("-->", i_);
                if (end == std::string_view::npos) return std::nullopt;
                i_ = end + 3;
                continue;
            }
            if (s_.substr(i_, 2) == "<?" || s_.substr(i_, 2) == "<!") {
                size_t end = s_.find('>', i_);
                if (end == std::string_view::npos) return std::nullopt;
                i_ = end + 1;
                continue;
            }
            break;
        }
        XmlTag tag;
        ++i_;
        if (i_ < s_.size() && s_[i_] == '/') tag.closing = true, ++i_;
        tag.name = stripPrefix(name());
        while (true) {
            skipSpace();
            if (i_ >= s_.size()) return std::nullopt;
            if (s_[i_] == '>') {
                ++i_;
                return tag;
            }
            if (s_.substr(i_, 2) == "/>") {
                i_ += 2;
                tag.selfClosing = true;
                return tag;
            }
            std::string key = stripPrefix(name());
            if (key.empty()) return std::nullopt;
            skipSpace();
            if (i_ >= s_.size() || s_[i_] != '=') return std::nullopt;
            ++i_;
            skipSpace();
            if (i_ >= s_.size() || (s_[i_] != '"' && s_[i_] != '\'')) return std::nullopt;
            char quote = s_[i_++];
            size_t end = s_.find(quote, i_);
            if (end == std::string_view::npos) return std::nullopt;
            tag.attributes[key] = std::string(s_.substr(i_, end - i_));  // entities: none in what is read
            i_ = end + 1;
        }
    }

private:
    void skipSpace() {
        while (i_ < s_.size() && std::isspace(static_cast<unsigned char>(s_[i_]))) ++i_;
    }
    std::string_view name() {
        size_t start = i_;
        while (i_ < s_.size() && !std::isspace(static_cast<unsigned char>(s_[i_])) && s_[i_] != '=' && s_[i_] != '>' && s_[i_] != '/') ++i_;
        return s_.substr(start, i_ - start);
    }
    static std::string stripPrefix(std::string_view n) {
        size_t colon = n.find(':');
        return std::string(colon == std::string_view::npos ? n : n.substr(colon + 1));
    }
    std::string_view s_;
    size_t i_ = 0;
};

float xmlNumber(const std::map<std::string, std::string>& a, const char* key, float fallback) {
    auto it = a.find(key);
    if (it == a.end()) return fallback;
    char* end = nullptr;
    float v = std::strtof(it->second.c_str(), &end);
    return end == it->second.c_str() ? fallback : v;  // "24dp": 24
}

std::optional<Color> xmlColor(const std::string& v) {
    if (v.empty()) return std::nullopt;
    if (v[0] != '#') return colors::Black;  // a reference (@color/…, ?attr/…): not resolved here
    std::string hex = v.substr(1);
    auto digit = [](char c) -> int { return std::isdigit(static_cast<unsigned char>(c)) ? c - '0' : std::isxdigit(static_cast<unsigned char>(c)) ? (std::tolower(c) - 'a' + 10) : -1; };
    for (char c : hex)
        if (digit(c) < 0) return std::nullopt;
    if (hex.size() == 3 || hex.size() == 4) {  // #RGB, #ARGB: each digit twice
        std::string wide;
        for (char c : hex) wide += std::string(2, c);
        hex = wide;
    }
    if (hex.size() == 6) hex = "ff" + hex;
    if (hex.size() != 8) return std::nullopt;
    uint32_t argb = uint32_t(std::stoul(hex, nullptr, 16));
    return Color::hex(argb & 0xFFFFFF, float((argb >> 24) & 0xFF) / 255.f);
}

// A group's transform, as Android applies it: scaled and turned about its pivot, then moved.
struct Affine {
    float a = 1, b = 0, c = 0, d = 1, e = 0, f = 0;  // x' = a x + c y + e, y' = b x + d y + f
    Affine then(const Affine& inner) const {  // this after inner: what inner's points become here
        return {a * inner.a + c * inner.b, b * inner.a + d * inner.b, a * inner.c + c * inner.d, b * inner.c + d * inner.d,
                a * inner.e + c * inner.f + e, b * inner.e + d * inner.f + f};
    }
    void apply(float& x, float& y) const {
        float nx = a * x + c * y + e, ny = b * x + d * y + f;
        x = nx, y = ny;
    }
};
Affine groupTransform(const VectorGroup& g) {
    float rad = g.rotate * 3.14159265f / 180, cs = std::cos(rad), sn = std::sin(rad);
    Affine toPivot{1, 0, 0, 1, -g.pivotX, -g.pivotY}, scale{g.scaleX, 0, 0, g.scaleY, 0, 0}, turn{cs, sn, -sn, cs, 0, 0};
    Affine back{1, 0, 0, 1, g.pivotX + g.translationX, g.pivotY + g.translationY};
    return back.then(turn.then(scale.then(toPivot)));
}
Affine affineOf(const std::array<float, 6>& m) { return {m[0], m[1], m[2], m[3], m[4], m[5]}; }
void transformCommands(std::vector<PathCommand>& commands, const Affine& m) {
    for (PathCommand& c : commands)
        for (auto [x, y] : {std::pair{&c.x1, &c.y1}, std::pair{&c.x2, &c.y2}, std::pair{&c.x, &c.y}}) m.apply(*x, *y);
}

std::optional<Color> xmlColorOf(const std::map<std::string, std::string>& a, const char* key) {
    auto it = a.find(key);
    return it == a.end() ? std::nullopt : xmlColor(it->second);
}

// An aapt:attr <gradient> (its <item>s, else its start, center and end colors), in its path's
// units (the Builder moves it with its groups).
std::optional<Brush> gradientBrush(const std::map<std::string, std::string>& at, std::vector<ColorStop> stops) {
    if (stops.empty()) {
        if (auto c = xmlColorOf(at, "startColor")) stops.push_back({0, *c});
        if (auto c = xmlColorOf(at, "centerColor")) stops.push_back({0.5f, *c});
        if (auto c = xmlColorOf(at, "endColor")) stops.push_back({1, *c});
    }
    if (stops.empty()) return std::nullopt;
    auto value = [&](const char* key) { return at.contains(key) ? at.at(key) : std::string(); };
    std::string type = value("type"), tile = value("tileMode");
    TileMode mode = tile == "repeat" ? TileMode::Repeated : tile == "mirror" ? TileMode::Mirror : TileMode::Clamp;
    auto point = [&](const char* x, const char* y) { return Offset{xmlNumber(at, x, 0), xmlNumber(at, y, 0)}; };
    if (type == "radial") {
        float radius = xmlNumber(at, "gradientRadius", 0);
        if (radius <= 0) return Brush::solid(stops.front().color);  // Android refuses it
        return Brush::radialGradient(std::move(stops), point("centerX", "centerY"), radius, mode);
    }
    if (type == "sweep") return Brush::sweepGradient(std::move(stops), point("centerX", "centerY"));
    return Brush::linearGradient(std::move(stops), point("startX", "startY"), point("endX", "endY"), mode);
}
} // namespace

namespace {
// The piece of `path` from `start` to `end` (fractions of its length, every contour's together),
// both moved on by `offset`, wrapping round its end (Android's trimPathStart/End/Offset).
Path trimmedPath(const Path& path, float start, float end, float offset) {
    std::vector<float> lengths;
    float total = 0;
    PathMeasure measure(path);
    do {
        lengths.push_back(measure.length());
        total += lengths.back();
    } while (measure.nextContour());
    Path out;
    out.fillType = path.fillType;
    if (total <= 0) return out;
    auto piece = [&](float from, float to) {  // distances along the whole of it
        PathMeasure m(path);
        float base = 0;
        size_t i = 0;
        do {
            float length = lengths[i++], a = std::max(from, base), b = std::min(to, base + length);
            if (b > a) m.getSegment(a - base, b - base, out);
            base += length;
        } while (m.nextContour() && i < lengths.size());
    };
    float s = start + offset, e = end + offset;
    s -= std::floor(s), e -= std::floor(e);  // an end of 1 comes round to 0: the piece wraps
    if (s <= e) {
        piece(s * total, e * total);
    } else {
        piece(s * total, total);
        piece(0, e * total);
    }
    return out;
}
} // namespace

Brush ImageVector::mapBrush(const Brush& brush, const std::array<float, 6>& m, Size viewport) {
    if (brush.isSolid()) return brush;
    Affine t = affineOf(m);
    auto map = [&](float x, float y) {
        t.apply(x, y);
        return Offset{x, y};
    };
    auto resolve = [](float v, float size) { return std::isinf(v) ? size : v; };
    Brush b = brush;
    if (b.kind_ == Brush::Kind::Linear) {
        b.start_ = map(resolve(brush.start_.x, viewport.width), resolve(brush.start_.y, viewport.height));
        b.end_ = map(resolve(brush.end_.x, viewport.width), resolve(brush.end_.y, viewport.height));
        return b;
    }
    b.start_ = brush.hasCenter_ ? map(brush.start_.x, brush.start_.y) : map(viewport.width / 2, viewport.height / 2);
    b.hasCenter_ = true;
    if (b.kind_ == Brush::Kind::Radial)
        b.radius_ = (std::isinf(brush.radius_) ? std::min(viewport.width, viewport.height) / 2 : brush.radius_) * std::sqrt(std::abs(t.a * t.d - t.b * t.c));
    return b;
}

namespace {
// An XML element with its attributes and children (a vector drawable, an animated one).
struct XmlNode {
    std::string name;
    std::map<std::string, std::string> attributes;
    std::vector<XmlNode> children;
    std::string attribute(const char* key) const {
        auto it = attributes.find(key);
        return it == attributes.end() ? std::string() : it->second;
    }
    const XmlNode* find(std::string_view element) const {  // the first such element in it (it too)
        if (name == element) return this;
        for (const XmlNode& c : children)
            if (const XmlNode* found = c.find(element)) return found;
        return nullptr;
    }
};

std::optional<XmlNode> readXmlTree(std::string_view xml) {
    XmlReader reader(xml);
    std::vector<XmlNode> open;
    auto done = [&](XmlNode node) -> std::optional<XmlNode> {
        if (open.empty()) return node;
        open.back().children.push_back(std::move(node));
        return std::nullopt;
    };
    while (std::optional<XmlTag> tag = reader.next()) {
        std::optional<XmlNode> root;
        if (tag->closing) {
            if (open.empty()) continue;
            XmlNode node = std::move(open.back());
            open.pop_back();
            root = done(std::move(node));
        } else if (tag->selfClosing) {
            root = done({tag->name, std::move(tag->attributes), {}});
        } else {
            open.push_back({tag->name, std::move(tag->attributes), {}});
        }
        if (root) return root;
    }
    return std::nullopt;
}

// Animated values of a vector drawable's named elements (by name and attribute), over theirs.
struct VectorValues {
    std::map<std::pair<std::string, std::string>, float> numbers;
    std::map<std::pair<std::string, std::string>, Color> colors;
    std::map<std::string, std::vector<PathCommand>> paths;  // pathData
};

// A <vector> element as an ImageVector: its groups and clip-paths through the Builder, its paths
// in their paints (gradients from their aapt:attr children), `values` over named elements'.
ImageVector vectorOf(const XmlNode& vector, const VectorValues& values) {
    auto number = [&](const XmlNode& n, const char* key, float fallback) {
        if (std::string name = n.attribute("name"); !name.empty())
            if (auto it = values.numbers.find({name, key}); it != values.numbers.end()) return it->second;
        return xmlNumber(n.attributes, key, fallback);
    };
    auto color = [&](const XmlNode& n, const char* key) -> std::optional<Color> {
        if (std::string name = n.attribute("name"); !name.empty())
            if (auto it = values.colors.find({name, key}); it != values.colors.end()) return it->second;
        return xmlColorOf(n.attributes, key);
    };
    float vw = number(vector, "viewportWidth", 24), vh = number(vector, "viewportHeight", 24);
    ImageVector::Builder builder(number(vector, "width", vw), number(vector, "height", vh), vw, vh);
    float alpha = number(vector, "alpha", 1);
    auto pathOf = [&](const XmlNode& n) {
        VectorPath p;
        auto animated = values.paths.find(n.attribute("name"));
        p.commands = animated != values.paths.end() && !n.attribute("name").empty() ? animated->second : VectorPath::parse(n.attribute("pathData"));
        p.fill.reset();
        if (auto c = color(n, "fillColor")) p.fill = Brush::solid(*c);
        if (auto c = color(n, "strokeColor")) p.stroke = Brush::solid(*c);
        for (const XmlNode& attr : n.children) {  // <aapt:attr name="android:fillColor"><gradient>…
            const XmlNode* gradient = attr.name == "attr" ? attr.find("gradient") : nullptr;
            if (!gradient) continue;
            std::vector<ColorStop> stops;
            for (const XmlNode& item : gradient->children)
                if (auto c = item.name == "item" ? xmlColorOf(item.attributes, "color") : std::nullopt) stops.push_back({xmlNumber(item.attributes, "offset", 0), *c});
            if (std::optional<Brush> brush = gradientBrush(gradient->attributes, std::move(stops)))
                (attr.attribute("name").find("strokeColor") != std::string::npos ? p.stroke : p.fill) = std::move(*brush);
        }
        p.fillAlpha = number(n, "fillAlpha", 1) * alpha;
        p.strokeAlpha = number(n, "strokeAlpha", 1) * alpha;
        p.strokeLineWidth = number(n, "strokeWidth", 0);
        p.strokeLineMiter = number(n, "strokeMiterLimit", 4);
        p.trimPathStart = number(n, "trimPathStart", 0);
        p.trimPathEnd = number(n, "trimPathEnd", 1);
        p.trimPathOffset = number(n, "trimPathOffset", 0);
        std::string cap = n.attribute("strokeLineCap"), join = n.attribute("strokeLineJoin");
        p.strokeLineCap = cap == "round" ? StrokeCap::Round : cap == "square" ? StrokeCap::Square : StrokeCap::Butt;
        p.strokeLineJoin = join == "round" ? StrokeJoin::Round : join == "bevel" ? StrokeJoin::Bevel : StrokeJoin::Miter;
        if (n.attribute("fillType") == "evenOdd") p.pathFillType = Path::FillType::EvenOdd;
        return p;
    };
    std::function<void(const XmlNode&)> contents = [&](const XmlNode& parent) {
        int clips = 0;  // its <clip-path>s: groups of their own, clipping what comes after them, closed with it
        for (const XmlNode& n : parent.children) {
            if (n.name == "group") {
                builder.addGroup({.rotate = number(n, "rotation", 0), .pivotX = number(n, "pivotX", 0), .pivotY = number(n, "pivotY", 0),
                                  .scaleX = number(n, "scaleX", 1), .scaleY = number(n, "scaleY", 1),
                                  .translationX = number(n, "translateX", 0), .translationY = number(n, "translateY", 0)});
                contents(n);
                builder.clearGroup();
            } else if (n.name == "clip-path") {
                builder.addGroup({.clipPathData = n.attribute("pathData")});
                ++clips;
            } else if (n.name == "path") {
                builder.path(pathOf(n));
            }
        }
        for (; clips > 0; --clips) builder.clearGroup();
    };
    contents(vector);
    builder.autoMirror(vector.attribute("autoMirrored") == "true");
    return builder.build();
}
} // namespace

ImageVector ImageVector::fromVectorXml(std::string_view xml) {
    std::optional<XmlNode> root = readXmlTree(xml);
    const XmlNode* vector = root ? root->find("vector") : nullptr;
    return vector ? vectorOf(*vector, {}) : ImageVector();
}

namespace {
// One property of a named element going from one value to another (an <objectAnimator> or one
// of its <propertyValuesHolder>s), at its time in the whole.
struct Animator {
    std::string target, property;
    enum class Kind { Number, Color, Path } kind = Kind::Number;
    float from = 0, to = 0;
    Color fromColor, toColor;
    std::vector<PathCommand> fromPath, toPath;
    float start = 0, duration = 300;
    Easing easing{0.37f, 0, 0.63f, 1};  // accelerate_decelerate (Android's default), near enough
    int repeats = 0;                    // after the first time
    bool reverses = false;              // repeatMode="reverse"
    float end() const { return start + duration * float(repeats + 1); }
    // How far along it is at `t` (0..1, eased): nothing before its start, its last after its end.
    std::optional<float> fraction(float t) const {
        if (t < start) return std::nullopt;
        float local = t - start;
        int turn = duration > 0 ? int(std::floor(local / duration)) : repeats + 1;
        float f = 1;
        if (turn <= repeats) {
            f = (local - float(turn) * duration) / duration;
        } else {
            turn = repeats;
        }
        if (reverses && turn % 2 == 1) f = 1 - f;
        return easing(f);
    }
};

Easing interpolatorEasing(const std::string& name) {
    auto is = [&](const char* s) { return name.find(s) != std::string::npos; };
    if (is("fast_out_slow_in")) return FastOutSlowInEasing;
    if (is("fast_out_linear_in")) return FastOutLinearInEasing;
    if (is("linear_out_slow_in")) return LinearOutSlowInEasing;
    if (is("linear")) return LinearEasing;
    if (is("accelerate_decelerate")) return Easing{0.37f, 0, 0.63f, 1};
    if (is("accelerate")) return Easing{1 / 3.f, 0, 2 / 3.f, 1 / 3.f};  // t², exactly
    if (is("decelerate")) return Easing{1 / 3.f, 2 / 3.f, 2 / 3.f, 1};  // 1 - (1 - t)², exactly
    return Easing{0.37f, 0, 0.63f, 1};
}

const XmlNode* named(const XmlNode& e, const std::string& name) {  // the element of that android:name in it
    if (e.attribute("name") == name) return &e;
    for (const XmlNode& c : e.children)
        if (const XmlNode* found = named(c, name)) return found;
    return nullptr;
}

// Reads an animator element (an <objectAnimator>, a <set> of them) for `target` from `start`;
// returns where it ends.
float readAnimators(const XmlNode& n, const std::string& target, float start, const XmlNode& vector, std::vector<Animator>& out) {
    if (n.name == "set") {
        bool sequential = n.attribute("ordering") == "sequentially";
        float end = start;
        for (const XmlNode& c : n.children) {
            float childEnd = readAnimators(c, target, sequential ? end : start, vector, out);
            end = std::max(end, childEnd);
        }
        return end;
    }
    if (n.name != "objectAnimator") return start;
    Animator base;
    base.target = target;
    base.start = start + xmlNumber(n.attributes, "startOffset", 0);
    base.duration = xmlNumber(n.attributes, "duration", 300);
    if (std::string i = n.attribute("interpolator"); !i.empty()) base.easing = interpolatorEasing(i);
    base.repeats = int(xmlNumber(n.attributes, "repeatCount", 0));
    if (base.repeats < 0) base.repeats = 0;  // infinite: played once
    base.reverses = n.attribute("repeatMode") == "reverse";
    const XmlNode* element = named(vector, target);  // its own values: where an animator without valueFrom starts
    auto add = [&](const std::map<std::string, std::string>& at) {
        Animator a = base;
        auto value = [&](const char* key) {
            auto it = at.find(key);
            return it == at.end() ? std::string() : it->second;
        };
        a.property = value("propertyName");
        if (a.property.empty()) return;
        std::string type = value("valueType"), from = value("valueFrom"), to = value("valueTo");
        if (from.empty() && element) from = element->attribute(a.property.c_str());
        if (to.empty()) return;
        if (type == "pathType" || a.property == "pathData") {
            a.kind = Animator::Kind::Path;
            a.fromPath = VectorPath::parse(from);
            a.toPath = VectorPath::parse(to);
        } else if (type == "colorType" || (!to.empty() && to[0] == '#')) {
            a.kind = Animator::Kind::Color;
            a.fromColor = xmlColor(from).value_or(colors::Transparent);
            a.toColor = xmlColor(to).value_or(colors::Transparent);
        } else {
            a.from = std::strtof(from.c_str(), nullptr);
            a.to = std::strtof(to.c_str(), nullptr);
        }
        out.push_back(std::move(a));
    };
    add(n.attributes);
    for (const XmlNode& holder : n.children)
        if (holder.name == "propertyValuesHolder") add(holder.attributes);
    return base.end();
}
} // namespace

struct AnimatedImageVector::Data {
    XmlNode vector;
    std::vector<Animator> animators;  // in the order they start
    float duration = 0;
    ImageVector start;
};

AnimatedImageVector AnimatedImageVector::fromAnimatedVectorXml(std::string_view xml) {
    std::optional<XmlNode> root = readXmlTree(xml);
    if (!root || root->name != "animated-vector") return {};
    const XmlNode* vector = root->find("vector");
    if (!vector) return {};
    auto data = std::make_shared<Data>();
    data->vector = *vector;
    for (const XmlNode& target : root->children) {
        if (target.name != "target") continue;
        for (const XmlNode& attr : target.children)
            if (attr.name == "attr")  // <aapt:attr name="android:animation">
                for (const XmlNode& animator : attr.children) data->duration = std::max(data->duration, readAnimators(animator, target.attribute("name"), 0, *vector, data->animators));
    }
    std::stable_sort(data->animators.begin(), data->animators.end(), [](const Animator& a, const Animator& b) { return a.start < b.start; });
    data->start = vectorOf(data->vector, {});
    AnimatedImageVector v;
    v.data_ = std::move(data);
    return v;
}

int AnimatedImageVector::totalDurationMillis() const { return data_ ? int(std::lround(data_->duration)) : 0; }
ImageVector AnimatedImageVector::imageVector() const { return data_ ? data_->start : ImageVector(); }

ImageVector AnimatedImageVector::vectorAt(float millis) const {
    if (!data_) return {};
    VectorValues values;
    for (const Animator& a : data_->animators) {  // the later started over the earlier
        std::optional<float> f = a.fraction(millis);
        if (!f) continue;
        std::pair<std::string, std::string> key{a.target, a.property};
        switch (a.kind) {
        case Animator::Kind::Number: values.numbers[key] = a.from + (a.to - a.from) * *f; break;
        case Animator::Kind::Color: values.colors[key] = lerp(a.fromColor, a.toColor, *f); break;
        case Animator::Kind::Path: {
            bool morphs = a.fromPath.size() == a.toPath.size() &&
                          std::equal(a.fromPath.begin(), a.fromPath.end(), a.toPath.begin(), [](const PathCommand& x, const PathCommand& y) { return x.verb == y.verb; });
            if (!morphs) {
                values.paths[a.target] = *f < 0.5f ? a.fromPath : a.toPath;
                break;
            }
            std::vector<PathCommand> between = a.fromPath;
            for (size_t i = 0; i < between.size(); ++i) {
                const PathCommand &x = a.fromPath[i], &y = a.toPath[i];
                auto mix = [&](float p, float q) { return p + (q - p) * *f; };
                between[i] = {x.verb, mix(x.x1, y.x1), mix(x.y1, y.y1), mix(x.x2, y.x2), mix(x.y2, y.y2), mix(x.x, y.x), mix(x.y, y.y)};
            }
            values.paths[a.target] = std::move(between);
            break;
        }
        }
    }
    return vectorOf(data_->vector, values);
}

ImageVector::Builder::Builder(float defaultWidth, float defaultHeight, float viewportWidth, float viewportHeight)
    : defaultWidth_(defaultWidth), defaultHeight_(defaultHeight), viewportWidth_(viewportWidth), viewportHeight_(viewportHeight) {}
ImageVector::Builder& ImageVector::Builder::path(VectorPath path) {
    if (!groups_.empty()) {  // in its groups: moved, scaled, turned (its gradients, its stroke too), clipped
        const Group& g = groups_.back();
        Affine m = affineOf(g.transform);
        transformCommands(path.commands, m);
        for (std::vector<PathCommand>& clip : path.clipPaths) transformCommands(clip, m);
        path.clipPaths.insert(path.clipPaths.begin(), g.clips.begin(), g.clips.end());
        Size viewport{viewportWidth_, viewportHeight_};
        if (path.fill) path.fill = mapBrush(*path.fill, g.transform, viewport);
        if (path.stroke) path.stroke = mapBrush(*path.stroke, g.transform, viewport);
        path.strokeLineWidth *= std::sqrt(std::abs(m.a * m.d - m.b * m.c));
    }
    paths_.push_back(std::move(path));
    return *this;
}
ImageVector::Builder& ImageVector::Builder::addGroup(const VectorGroup& group) {
    Group g = groups_.empty() ? Group{} : groups_.back();
    Affine m = affineOf(g.transform).then(groupTransform(group));
    g.transform = {m.a, m.b, m.c, m.d, m.e, m.f};
    if (!group.clipPathData.empty()) {
        std::vector<PathCommand> clip = VectorPath::parse(group.clipPathData);
        transformCommands(clip, m);
        g.clips.push_back(std::move(clip));
    }
    groups_.push_back(std::move(g));
    return *this;
}
ImageVector::Builder& ImageVector::Builder::clearGroup() {
    if (!groups_.empty()) groups_.pop_back();
    return *this;
}
ImageVector::Builder& ImageVector::Builder::path(std::string_view pathData, std::optional<Brush> fill, std::optional<Brush> stroke,
                                                 float strokeLineWidth) {
    VectorPath p;
    p.commands = VectorPath::parse(pathData);
    p.fill = std::move(fill);
    p.stroke = std::move(stroke);
    p.strokeLineWidth = strokeLineWidth;
    return path(std::move(p));
}
ImageVector::Builder& ImageVector::Builder::autoMirror(bool on) {
    autoMirror_ = on;
    return *this;
}
ImageVector ImageVector::Builder::build() const {
    auto data = std::make_shared<Data>();
    data->viewportWidth = viewportWidth_;
    data->viewportHeight = viewportHeight_;
    data->defaultWidth = defaultWidth_;
    data->defaultHeight = defaultHeight_;
    data->id = gNextVectorId++;
    data->paths = paths_;
    for (const VectorPath& p : paths_)
        if (p.fill) data->commands.insert(data->commands.end(), p.commands.begin(), p.commands.end());
    ImageVector vector;
    vector.data_ = std::move(data);
    return autoMirror_ ? vector.autoMirrored() : vector;
}

const std::vector<VectorPath>& ImageVector::paths() const {
    static const std::vector<VectorPath> none;
    return data_ ? data_->paths : none;
}
float ImageVector::defaultWidth() const { return data_ ? data_->defaultWidth : 0; }
float ImageVector::defaultHeight() const { return data_ ? data_->defaultHeight : 0; }

void detail::drawImageVector(GraphicsContext& canvas, const ImageVector& vector, const Rect& b, std::optional<Color> tint, float alpha) {
    if (vector.empty() || b.width <= 0 || b.height <= 0 || alpha <= 0) return;
    const auto& paths = vector.paths();
    bool strokes = std::any_of(paths.begin(), paths.end(), [](const VectorPath& p) { return p.stroke && p.strokeLineWidth > 0; });
    bool clipped = std::any_of(paths.begin(), paths.end(), [](const VectorPath& p) { return !p.clipPaths.empty(); });
    if (paths.empty() || (tint && !strokes && !clipped)) {  // one color: the backend's cached raster
        Color c = tint.value_or(colors::Black);
        canvas.drawVector(vector, b, c.copy(c.a * alpha));
        return;
    }
    float sx = b.width / std::max(1e-3f, vector.viewportWidth()), sy = b.height / std::max(1e-3f, vector.viewportHeight());
    auto scaledPath = [&](const std::vector<PathCommand>& commands) {
        Path path;
        for (const PathCommand& c : commands) {
            switch (c.verb) {
            case PathCommand::Verb::Move: path.moveTo(c.x * sx, c.y * sy); break;
            case PathCommand::Verb::Line: path.lineTo(c.x * sx, c.y * sy); break;
            case PathCommand::Verb::Quad: path.quadraticTo(c.x1 * sx, c.y1 * sy, c.x * sx, c.y * sy); break;
            case PathCommand::Verb::Cubic: path.cubicTo(c.x1 * sx, c.y1 * sy, c.x2 * sx, c.y2 * sy, c.x * sx, c.y * sy); break;
            case PathCommand::Verb::Close: path.close(); break;
            }
        }
        return path;
    };
    for (const VectorPath& p : paths) {
        // Inside its groups' clip paths: a layer clipped to each, over the clip's bounds.
        size_t layers = 0;
        for (const std::vector<PathCommand>& clip : p.clipPaths) {
            Path outline = scaledPath(clip);
            Rect r = outline.bounds();
            if (r.width <= 0 || r.height <= 0) break;
            outline.translate({-r.x, -r.y});
            canvas.pushOutlineLayer({b.x + r.x, b.y + r.y, r.width, r.height}, outline, 1);
            ++layers;
        }
        if (layers < p.clipPaths.size()) {  // clipped to nothing
            for (; layers > 0; --layers) canvas.popLayer();
            continue;
        }
        Path path = scaledPath(p.commands);
        path.fillType = p.pathFillType;
        if (p.trimPathStart != 0 || p.trimPathEnd != 1) path = trimmedPath(path, p.trimPathStart, p.trimPathEnd, p.trimPathOffset);
        auto paint = [&](const Brush& brush, float a, const PathStroke& stroke) {
            float total = a * alpha;
            if (total <= 0 || path.isEmpty()) return;  // (trimmed to nothing)
            if (tint || brush.kind() == Brush::Kind::Solid) {  // a color: its alpha
                Color c = tint.value_or(brush.colorAt(b.center(), b));
                canvas.drawPath(path, {b.x, b.y}, Brush::solid(c.copy(c.a * total)), b, stroke);
            } else if (total < 1) {  // a gradient (in viewport units, as its path): through a layer
                canvas.pushLayer(b, 0, total, {});
                canvas.drawPath(path, {b.x, b.y}, brush.scaled(sx, sy), b, stroke);
                canvas.popLayer();
            } else {
                canvas.drawPath(path, {b.x, b.y}, brush.scaled(sx, sy), b, stroke);
            }
        };
        if (p.fill) paint(*p.fill, p.fillAlpha, {});
        if (p.stroke && p.strokeLineWidth > 0)
            paint(*p.stroke, p.strokeAlpha, {p.strokeLineWidth * std::min(sx, sy), p.strokeLineCap, p.strokeLineJoin, p.strokeLineMiter});
        for (; layers > 0; --layers) canvas.popLayer();
    }
}

const std::vector<PathCommand>& ImageVector::commands() const {
    static const std::vector<PathCommand> none;
    return data_ ? data_->commands : none;
}
float ImageVector::viewportWidth() const { return data_ ? data_->viewportWidth : 0; }
float ImageVector::viewportHeight() const { return data_ ? data_->viewportHeight : 0; }
uint64_t ImageVector::id() const { return data_ ? data_->id : 0; }

ImageVector ImageVector::mirrored() const {
    if (!data_) return {};
    const Data* source = data_->base ? data_->base.get() : data_.get();  // one mirror per drawing
    std::call_once(source->mirrorOnce, [d = source] {
        auto m = std::make_shared<Data>();
        m->viewportWidth = d->viewportWidth;
        m->viewportHeight = d->viewportHeight;
        m->id = gNextVectorId++;
        m->defaultWidth = d->defaultWidth;
        m->defaultHeight = d->defaultHeight;
        m->commands = d->commands;
        m->paths = d->paths;
        auto mirror = [&](std::vector<PathCommand>& commands) {
            for (PathCommand& c : commands)
                for (float* x : {&c.x1, &c.x2, &c.x}) *x = d->viewportWidth - *x;
        };
        mirror(m->commands);
        std::array<float, 6> flip{-1, 0, 0, 1, d->viewportWidth, 0};
        for (VectorPath& p : m->paths) {
            mirror(p.commands);
            for (std::vector<PathCommand>& clip : p.clipPaths) mirror(clip);
            if (p.fill) p.fill = mapBrush(*p.fill, flip, {d->viewportWidth, d->viewportHeight});
            if (p.stroke) p.stroke = mapBrush(*p.stroke, flip, {d->viewportWidth, d->viewportHeight});
        }
        d->mirror = std::move(m);
    });
    ImageVector v;
    v.data_ = source->mirror;
    return v;
}

ImageVector ImageVector::autoMirrored() const {
    if (!data_) return {};
    auto d = std::make_shared<Data>();
    d->commands = data_->commands;
    d->paths = data_->paths;
    d->viewportWidth = data_->viewportWidth;
    d->viewportHeight = data_->viewportHeight;
    d->defaultWidth = data_->defaultWidth;
    d->defaultHeight = data_->defaultHeight;
    d->id = data_->id;  // drawn the same, left to right
    d->autoMirror = true;
    d->base = data_->base ? data_->base : data_;
    ImageVector v;
    v.data_ = std::move(d);
    return v;
}

bool ImageVector::autoMirror() const { return data_ && data_->autoMirror; }

} // namespace kompoz

namespace kompoz {

struct ImageBitmap::Data {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    uint64_t id = 0;
};

ImageBitmap ImageBitmap::fromPixels(int width, int height, std::vector<uint8_t> rgba) {
    static std::atomic<uint64_t> nextId{1};
    ImageBitmap image;
    if (width <= 0 || height <= 0 || rgba.size() < size_t(width) * size_t(height) * 4) return image;
    auto data = std::make_shared<Data>();
    data->width = width;
    data->height = height;
    data->rgba = std::move(rgba);
    data->id = nextId++;
    image.data_ = std::move(data);
    return image;
}

int ImageBitmap::width() const { return data_ ? data_->width : 0; }
int ImageBitmap::height() const { return data_ ? data_->height : 0; }
const uint8_t* ImageBitmap::pixels() const { return data_ ? data_->rgba.data() : nullptr; }
uint64_t ImageBitmap::id() const { return data_ ? data_->id : 0; }

namespace {
// How much the image would be scaled to meet the size (below 1: it is bigger than needed).
double scaleFor(int width, int height, const ImageRequestSize& size) {
    double sx = double(size.width) / width, sy = double(size.height) / height;
    if (size.any()) return 1;
    if (size.width <= 0) return sy;
    if (size.height <= 0) return sx;
    return size.fill ? std::max(sx, sy) : std::min(sx, sy);
}
} // namespace

int imageSampleSize(int width, int height, const ImageRequestSize& size) {
    if (width <= 0 || height <= 0) return 1;
    double scale = scaleFor(width, height, size);
    int sample = 1;
    while (sample < (1 << 16) && scale * sample * 2 <= 1) sample *= 2;  // 1/(2 sample) of it is still enough
    while (sample < (1 << 16) && ((width + sample - 1) / sample > maxImageDimension || (height + sample - 1) / sample > maxImageDimension))
        sample *= 2;
    return sample;
}

bool imageCovers(int width, int height, const ImageRequestSize& size) {
    return width > 0 && height > 0 && !size.any() && scaleFor(width, height, size) <= 1 + 1e-9;
}

ImageBitmap scaleDown(const ImageBitmap& image, int sample) {
    if (image.empty() || sample <= 1) return image;
    int w = image.width(), h = image.height();
    int ow = (w + sample - 1) / sample, oh = (h + sample - 1) / sample;
    std::vector<uint8_t> out(size_t(ow) * size_t(oh) * 4);
    const uint8_t* in = image.pixels();
    for (int oy = 0; oy < oh; ++oy)
        for (int ox = 0; ox < ow; ++ox) {
            uint64_t r = 0, g = 0, b = 0, a = 0, n = 0;
            for (int y = oy * sample; y < std::min(h, (oy + 1) * sample); ++y)
                for (int x = ox * sample; x < std::min(w, (ox + 1) * sample); ++x) {
                    const uint8_t* p = in + (size_t(y) * size_t(w) + size_t(x)) * 4;
                    r += uint64_t(p[0]) * p[3];
                    g += uint64_t(p[1]) * p[3];
                    b += uint64_t(p[2]) * p[3];
                    a += p[3];
                    ++n;
                }
            uint8_t* q = out.data() + (size_t(oy) * size_t(ow) + size_t(ox)) * 4;
            if (a > 0) {
                q[0] = uint8_t((r + a / 2) / a);
                q[1] = uint8_t((g + a / 2) / a);
                q[2] = uint8_t((b + a / 2) / a);
            }
            q[3] = uint8_t((a + n / 2) / n);
        }
    return ImageBitmap::fromPixels(ow, oh, std::move(out));
}

} // namespace kompoz
