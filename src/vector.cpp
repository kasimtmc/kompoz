#include <kompoz/animation.hpp>
#include <kompoz/display_list.hpp>
#include <kompoz/draw.hpp>
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
#include <numbers>
#include <string>
#include <unordered_map>

namespace kompoz {

// --- Path data ---------------------------------------------------------------------------------

bool PathNode::isCurve() const {
    return kind == Kind::RelativeCurveTo || kind == Kind::CurveTo || kind == Kind::RelativeReflectiveCurveTo || kind == Kind::ReflectiveCurveTo;
}
bool PathNode::isQuad() const {
    return kind == Kind::RelativeQuadTo || kind == Kind::QuadTo || kind == Kind::RelativeReflectiveQuadTo || kind == Kind::ReflectiveQuadTo;
}

namespace {

bool digit(char c) { return c >= '0' && c <= '9'; }

// The number at `start` (Compose's nextFloat: an optional sign, digits with a point among or
// before them, an exponent) and where it stops; NaN, and where it gave up, if there is none.
std::pair<size_t, float> nextFloat(std::string_view s, size_t start, size_t end) {
    size_t i = start;
    if (i != end && (s[i] == '-' || s[i] == '+')) ++i;
    int digits = 0;
    while (i != end && digit(s[i])) ++i, ++digits;
    if (i != end && s[i] == '.') {
        ++i;
        while (i != end && digit(s[i])) ++i, ++digits;
    }
    if (digits == 0) return {i, std::numeric_limits<float>::quiet_NaN()};
    if (i != end && (s[i] | 0x20) == 'e') {
        size_t e = i + 1;
        if (e != end && (s[e] == '-' || s[e] == '+')) ++e;
        if (e != end && digit(s[e])) {
            while (e != end && digit(s[e])) ++e;
            i = e;
        }
    }
    return {i, std::strtof(std::string(s.substr(start, i - start)).c_str(), nullptr)};
}

// The nodes of one command letter and the numbers after it (Compose's Char.addPathNodes): as
// many as there are whole sets of numbers for (a move's after the first are lines).
bool addNodes(char command, const std::vector<float>& a, std::vector<PathNode>& nodes) {
    using K = PathNode::Kind;
    auto sets = [&](size_t n, auto make) {
        for (size_t i = 0; i + n <= a.size(); i += n) nodes.push_back(make(&a[i]));
    };
    switch (command) {
    case 'z':
    case 'Z': nodes.push_back(PathNode::Close()); break;
    case 'm':
    case 'M':
        if (a.size() >= 2) {
            bool relative = command == 'm';
            nodes.push_back(relative ? PathNode::RelativeMoveTo(a[0], a[1]) : PathNode::MoveTo(a[0], a[1]));
            for (size_t i = 2; i + 2 <= a.size(); i += 2) nodes.push_back(relative ? PathNode::RelativeLineTo(a[i], a[i + 1]) : PathNode::LineTo(a[i], a[i + 1]));
        }
        break;
    case 'l': sets(2, [](const float* v) { return PathNode::RelativeLineTo(v[0], v[1]); }); break;
    case 'L': sets(2, [](const float* v) { return PathNode::LineTo(v[0], v[1]); }); break;
    case 'h': sets(1, [](const float* v) { return PathNode::RelativeHorizontalTo(v[0]); }); break;
    case 'H': sets(1, [](const float* v) { return PathNode::HorizontalTo(v[0]); }); break;
    case 'v': sets(1, [](const float* v) { return PathNode::RelativeVerticalTo(v[0]); }); break;
    case 'V': sets(1, [](const float* v) { return PathNode::VerticalTo(v[0]); }); break;
    case 'c': sets(6, [](const float* v) { return PathNode::RelativeCurveTo(v[0], v[1], v[2], v[3], v[4], v[5]); }); break;
    case 'C': sets(6, [](const float* v) { return PathNode::CurveTo(v[0], v[1], v[2], v[3], v[4], v[5]); }); break;
    case 's': sets(4, [](const float* v) { return PathNode::RelativeReflectiveCurveTo(v[0], v[1], v[2], v[3]); }); break;
    case 'S': sets(4, [](const float* v) { return PathNode::ReflectiveCurveTo(v[0], v[1], v[2], v[3]); }); break;
    case 'q': sets(4, [](const float* v) { return PathNode::RelativeQuadTo(v[0], v[1], v[2], v[3]); }); break;
    case 'Q': sets(4, [](const float* v) { return PathNode::QuadTo(v[0], v[1], v[2], v[3]); }); break;
    case 't': sets(2, [](const float* v) { return PathNode::RelativeReflectiveQuadTo(v[0], v[1]); }); break;
    case 'T': sets(2, [](const float* v) { return PathNode::ReflectiveQuadTo(v[0], v[1]); }); break;
    case 'a':
    case 'A':
        sets(7, [&](const float* v) {
            return PathNode{command == 'a' ? K::RelativeArcTo : K::ArcTo, {v[0], v[1], v[2], v[5], v[6]}, v[3] != 0, v[4] != 0};
        });
        break;
    default: return false;
    }
    return true;
}

} // namespace

std::vector<PathNode>& PathParser::pathStringToNodes(std::string_view s, std::vector<PathNode>& nodes) {
    auto blank = [](char c) { return static_cast<unsigned char>(c) <= ' '; };
    size_t start = 0, end = s.size();
    while (start < end && blank(s[start])) ++start;
    while (end > start && blank(s[end - 1])) --end;
    std::vector<float> args;
    size_t i = start;
    while (i < end) {
        char command = 0;
        do {  // the next command letter ('e' is an exponent's)
            char c = s[i++];
            int lower = static_cast<unsigned char>(c) | 0x20;
            if (lower >= 'a' && lower <= 'z' && lower != 'e') {
                command = c;
                break;
            }
        } while (i < end);
        if (!command) break;
        args.clear();
        if ((command | 0x20) != 'z') {
            while (i < end && blank(s[i])) ++i;
            bool arc = (command | 0x20) == 'a';
            float value = 0;
            do {
                size_t k = args.size() % 7;  // an arc's flags: one character each ("a1 1 0 01.5.5")
                auto [next, v] = nextFloat(s, i, arc && (k == 3 || k == 4) ? std::min(i + 1, end) : end);
                i = next;
                value = v;
                if (!std::isnan(v)) args.push_back(v);
                while (i < end && (blank(s[i]) || s[i] == ',')) ++i;
            } while (i < end && !std::isnan(value));
        }
        if (!addNodes(command, args, nodes)) {
            std::fprintf(stderr, "kompoz: unsupported path command '%c'\n", command);
            break;
        }
    }
    return nodes;
}

PathParser& PathParser::parsePathString(std::string_view pathData) {
    nodes_.clear();
    pathStringToNodes(pathData, nodes_);
    return *this;
}
PathParser& PathParser::addPathNodes(const std::vector<PathNode>& nodes) {
    nodes_.insert(nodes_.end(), nodes.begin(), nodes.end());
    return *this;
}
Path PathParser::toPath() const { return kompoz::toPath(nodes_); }

std::vector<PathNode> addPathNodes(std::string_view pathData) {
    std::vector<PathNode> nodes;
    PathParser().pathStringToNodes(pathData, nodes);
    return nodes;
}

namespace {

// Compose's arcToBezier: the ellipse's arc from angle `start` through `sweep` (radians, about
// its center, the ellipse turned by `theta`) as cubics of at most an eighth of a turn.
void arcToBezier(Path& p, double cx, double cy, double a, double b, double e1x, double e1y, double theta, double start, double sweep) {
    int segments = int(std::ceil(std::abs(sweep * 4 / std::numbers::pi)));
    double eta1 = start, cosTheta = std::cos(theta), sinTheta = std::sin(theta), cosEta1 = std::cos(eta1), sinEta1 = std::sin(eta1);
    double ep1x = (-a * cosTheta * sinEta1) - (b * sinTheta * cosEta1), ep1y = (-a * sinTheta * sinEta1) + (b * cosTheta * cosEta1);
    double perSegment = sweep / segments;
    for (int i = 0; i < segments; ++i) {
        double eta2 = eta1 + perSegment, sinEta2 = std::sin(eta2), cosEta2 = std::cos(eta2);
        double e2x = cx + (a * cosTheta * cosEta2) - (b * sinTheta * sinEta2), e2y = cy + (a * sinTheta * cosEta2) + (b * cosTheta * sinEta2);
        double ep2x = (-a * cosTheta * sinEta2) - (b * sinTheta * cosEta2), ep2y = (-a * sinTheta * sinEta2) + (b * cosTheta * cosEta2);
        double tanDiff2 = std::tan((eta2 - eta1) / 2);
        double alpha = std::sin(eta2 - eta1) * (std::sqrt(4 + 3.0 * tanDiff2 * tanDiff2) - 1) / 3;
        p.cubicTo(float(e1x + alpha * ep1x), float(e1y + alpha * ep1y), float(e2x - alpha * ep2x), float(e2y - alpha * ep2y), float(e2x), float(e2y));
        eta1 = eta2, e1x = e2x, e1y = e2y, ep1x = ep2x, ep1y = ep2y;
    }
}

// Compose's drawArc: the SVG arc from (x0, y0) to (x1, y1), its radii a and b, turned by
// `theta` degrees, by its center worked out in the unit circle's space.
void drawArc(Path& p, double x0, double y0, double x1, double y1, double a, double b, double theta, bool moreThanHalf, bool positiveArc) {
    if (a == 0 || b == 0) {  // (SVG: a line; Compose's arithmetic would make the path not finite)
        if (x0 != x1 || y0 != y1) p.lineTo(float(x1), float(y1));
        return;
    }
    double thetaD = theta / 180 * std::numbers::pi, cosTheta = std::cos(thetaD), sinTheta = std::sin(thetaD);
    double x0p = (x0 * cosTheta + y0 * sinTheta) / a, y0p = (-x0 * sinTheta + y0 * cosTheta) / b;
    double x1p = (x1 * cosTheta + y1 * sinTheta) / a, y1p = (-x1 * sinTheta + y1 * cosTheta) / b;
    double dx = x0p - x1p, dy = y0p - y1p, xm = (x0p + x1p) / 2, ym = (y0p + y1p) / 2;
    double dsq = dx * dx + dy * dy;
    if (dsq == 0.0) return;  // the same point
    double disc = 1.0 / dsq - 1.0 / 4.0;
    if (disc < 0.0) {  // too far apart for these radii: as small as reaches
        double adjust = float(std::sqrt(dsq) / 1.99999);
        return drawArc(p, x0, y0, x1, y1, a * adjust, b * adjust, theta, moreThanHalf, positiveArc);
    }
    double s = std::sqrt(disc), sdx = s * dx, sdy = s * dy;
    double cx = moreThanHalf == positiveArc ? xm - sdy : xm + sdy, cy = moreThanHalf == positiveArc ? ym + sdx : ym - sdx;
    double eta0 = std::atan2(y0p - cy, x0p - cx), eta1 = std::atan2(y1p - cy, x1p - cx), sweep = eta1 - eta0;
    if (positiveArc != (sweep >= 0)) sweep += sweep > 0 ? -2 * std::numbers::pi : 2 * std::numbers::pi;
    cx *= a;
    cy *= b;
    double tcx = cx;
    cx = cx * cosTheta - cy * sinTheta;
    cy = tcx * sinTheta + cy * cosTheta;
    arcToBezier(p, cx, cy, a, b, x0, y0, thetaD, eta0, sweep);
}

} // namespace

void toPath(const std::vector<PathNode>& nodes, Path& target) {
    using K = PathNode::Kind;
    Path::FillType fillType = target.fillType;
    target.rewind();
    target.fillType = fillType;
    float currentX = 0, currentY = 0, ctrlX = 0, ctrlY = 0, segmentX = 0, segmentY = 0;
    PathNode previous = nodes.empty() ? PathNode::Close() : nodes[0];
    for (const PathNode& n : nodes) {
        const auto& v = n.values;
        switch (n.kind) {
        case K::Close:
            currentX = ctrlX = segmentX;
            currentY = ctrlY = segmentY;
            target.close();
            break;
        case K::RelativeMoveTo:
            currentX += v[0], currentY += v[1];
            target.relativeMoveTo(v[0], v[1]);
            segmentX = currentX, segmentY = currentY;
            break;
        case K::MoveTo:
            currentX = v[0], currentY = v[1];
            target.moveTo(v[0], v[1]);
            segmentX = currentX, segmentY = currentY;
            break;
        case K::RelativeLineTo:
            target.relativeLineTo(v[0], v[1]);
            currentX += v[0], currentY += v[1];
            break;
        case K::LineTo:
            target.lineTo(v[0], v[1]);
            currentX = v[0], currentY = v[1];
            break;
        case K::RelativeHorizontalTo:
            target.relativeLineTo(v[0], 0);
            currentX += v[0];
            break;
        case K::HorizontalTo:
            target.lineTo(v[0], currentY);
            currentX = v[0];
            break;
        case K::RelativeVerticalTo:
            target.relativeLineTo(0, v[0]);
            currentY += v[0];
            break;
        case K::VerticalTo:
            target.lineTo(currentX, v[0]);
            currentY = v[0];
            break;
        case K::RelativeCurveTo:
            target.relativeCubicTo(v[0], v[1], v[2], v[3], v[4], v[5]);
            ctrlX = currentX + v[2], ctrlY = currentY + v[3];
            currentX += v[4], currentY += v[5];
            break;
        case K::CurveTo:
            target.cubicTo(v[0], v[1], v[2], v[3], v[4], v[5]);
            ctrlX = v[2], ctrlY = v[3];
            currentX = v[4], currentY = v[5];
            break;
        case K::RelativeReflectiveCurveTo: {
            bool after = previous.isCurve();
            float rx = after ? currentX - ctrlX : 0, ry = after ? currentY - ctrlY : 0;
            target.relativeCubicTo(rx, ry, v[0], v[1], v[2], v[3]);
            ctrlX = currentX + v[0], ctrlY = currentY + v[1];
            currentX += v[2], currentY += v[3];
            break;
        }
        case K::ReflectiveCurveTo: {
            bool after = previous.isCurve();
            float rx = after ? 2 * currentX - ctrlX : currentX, ry = after ? 2 * currentY - ctrlY : currentY;
            target.cubicTo(rx, ry, v[0], v[1], v[2], v[3]);
            ctrlX = v[0], ctrlY = v[1];
            currentX = v[2], currentY = v[3];
            break;
        }
        case K::RelativeQuadTo:
            target.relativeQuadraticTo(v[0], v[1], v[2], v[3]);
            ctrlX = currentX + v[0], ctrlY = currentY + v[1];
            currentX += v[2], currentY += v[3];
            break;
        case K::QuadTo:
            target.quadraticTo(v[0], v[1], v[2], v[3]);
            ctrlX = v[0], ctrlY = v[1];
            currentX = v[2], currentY = v[3];
            break;
        case K::RelativeReflectiveQuadTo: {
            bool after = previous.isQuad();
            float rx = after ? currentX - ctrlX : 0, ry = after ? currentY - ctrlY : 0;
            target.relativeQuadraticTo(rx, ry, v[0], v[1]);
            ctrlX = currentX + rx, ctrlY = currentY + ry;
            currentX += v[0], currentY += v[1];
            break;
        }
        case K::ReflectiveQuadTo: {
            bool after = previous.isQuad();
            float rx = after ? 2 * currentX - ctrlX : currentX, ry = after ? 2 * currentY - ctrlY : currentY;
            target.quadraticTo(rx, ry, v[0], v[1]);
            ctrlX = rx, ctrlY = ry;
            currentX = v[0], currentY = v[1];
            break;
        }
        case K::RelativeArcTo:
        case K::ArcTo: {
            bool relative = n.kind == K::RelativeArcTo;
            float endX = relative ? v[3] + currentX : v[3], endY = relative ? v[4] + currentY : v[4];
            drawArc(target, currentX, currentY, endX, endY, v[0], v[1], v[2], n.isMoreThanHalf, n.isPositiveArc);
            currentX = ctrlX = endX;
            currentY = ctrlY = endY;
            break;
        }
        }
        previous = n;
    }
}

Path toPath(const std::vector<PathNode>& nodes) {
    Path path;
    toPath(nodes, path);
    return path;
}

// --- Android vector drawables ----------------------------------------------------------------

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
    std::map<std::string, std::vector<PathNode>> paths;  // pathData
};

// A <vector> element as an ImageVector, read as Compose's XmlVectorParser reads it (and its
// alpha): its groups and clip-paths through the Builder, its paths in their paints (gradients
// from their aapt:attr children), `values` over named elements'.
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
    std::string mode = vector.attribute("tintMode");
    BlendMode tintMode = mode == "src_over" ? BlendMode::SrcOver : mode == "src_atop" ? BlendMode::SrcAtop : mode == "multiply" ? BlendMode::Modulate
                       : mode == "screen" ? BlendMode::Screen : mode == "add" ? BlendMode::Plus : BlendMode::SrcIn;
    ImageVector::Builder builder(number(vector, "width", vw), number(vector, "height", vh), vw, vh, xmlColorOf(vector.attributes, "tint"), tintMode,
                                 vector.attribute("autoMirrored") == "true");
    float alpha = number(vector, "alpha", 1);
    auto pathOf = [&](const XmlNode& n) {
        VectorPath p;
        p.name = n.attribute("name");
        auto animated = p.name.empty() ? values.paths.end() : values.paths.find(p.name);
        p.pathData = animated != values.paths.end() ? animated->second : addPathNodes(n.attribute("pathData"));
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
        p.strokeLineWidth = number(n, "strokeWidth", 1);
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
                builder.addGroup({.name = n.attribute("name"), .rotation = number(n, "rotation", 0), .pivotX = number(n, "pivotX", 0),
                                  .pivotY = number(n, "pivotY", 0), .scaleX = number(n, "scaleX", 1), .scaleY = number(n, "scaleY", 1),
                                  .translationX = number(n, "translateX", 0), .translationY = number(n, "translateY", 0)});
                contents(n);
                builder.clearGroup();
            } else if (n.name == "clip-path") {
                std::string name = n.attribute("name");
                auto animated = name.empty() ? values.paths.end() : values.paths.find(name);
                builder.addGroup({.name = name, .clipPathData = animated != values.paths.end() ? animated->second : addPathNodes(n.attribute("pathData"))});
                ++clips;
            } else if (n.name == "path") {
                builder.addPath(pathOf(n));
            }
        }
        for (; clips > 0; --clips) builder.clearGroup();
    };
    contents(vector);
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
    std::vector<PathNode> fromNodes, toNodes;
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
            a.fromNodes = addPathNodes(from);
            a.toNodes = addPathNodes(to);
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
            bool morphs = a.fromNodes.size() == a.toNodes.size() &&
                          std::equal(a.fromNodes.begin(), a.fromNodes.end(), a.toNodes.begin(), [](const PathNode& x, const PathNode& y) { return x.kind == y.kind; });
            if (!morphs) {
                values.paths[a.target] = *f < 0.5f ? a.fromNodes : a.toNodes;
                break;
            }
            // Each number in between; an arc's flag set while it is anywhere but 0 (as Android
            // interpolates its flags as numbers and draws them as set unless 0).
            std::vector<PathNode> between = a.fromNodes;
            for (size_t i = 0; i < between.size(); ++i) {
                const PathNode &x = a.fromNodes[i], &y = a.toNodes[i];
                auto mix = [&](float p, float q) { return p + (q - p) * *f; };
                for (size_t k = 0; k < x.values.size(); ++k) between[i].values[k] = mix(x.values[k], y.values[k]);
                between[i].isMoreThanHalf = mix(x.isMoreThanHalf, y.isMoreThanHalf) != 0;
                between[i].isPositiveArc = mix(x.isPositiveArc, y.isPositiveArc) != 0;
            }
            values.paths[a.target] = std::move(between);
            break;
        }
        }
    }
    return vectorOf(data_->vector, values);
}

// --- ImageVector ------------------------------------------------------------------------------

bool operator==(const VectorGroup& a, const VectorGroup& b) {
    return a.name == b.name && a.rotation == b.rotation && a.pivotX == b.pivotX && a.pivotY == b.pivotY && a.scaleX == b.scaleX &&
           a.scaleY == b.scaleY && a.translationX == b.translationX && a.translationY == b.translationY && a.clipPathData == b.clipPathData &&
           a.children == b.children;
}

namespace {
std::atomic<uint64_t> gNextVectorId{1};

// Which bitmaps vectors were drawn into (detail::vectorDrawnInto), by the bitmap's id.
struct DrawnInto {
    std::mutex mutex;
    std::unordered_map<uint64_t, uint64_t> vectors;
};
DrawnInto& drawnInto() {
    static DrawnInto* d = new DrawnInto;  // (never destroyed: vectors that outlive main's statics take theirs out)
    return *d;
}
} // namespace

struct ImageVector::Data {
    std::string name;
    float defaultWidth = 0, defaultHeight = 0, viewportWidth = 0, viewportHeight = 0;
    VectorGroup root;
    std::optional<Color> tintColor;
    BlendMode tintBlendMode = BlendMode::SrcIn;
    bool autoMirror = false;
    uint64_t id = 0;
    // Compose's GroupComponent.isTintable and tintColor of its root: every color it paints with
    // a color of one rgb (that color), no gradients.
    bool tintable = true;
    std::optional<Color> oneColor;
    // The bitmaps it was drawn into, for the size they were drawn at (px) and their config; the
    // last drawn last.
    struct Drawn {
        float width, height;
        ImageBitmapConfig config;
        ImageBitmap bitmap;
    };
    mutable std::mutex mutex;
    mutable std::vector<Drawn> drawn;
    ~Data() {
        std::lock_guard lock(drawnInto().mutex);
        for (const Drawn& d : drawn) drawnInto().vectors.erase(d.bitmap.id());
    }
};

namespace {
// Compose's GroupComponent.markTintFor…: what `group`'s paths and groups paint with, one rgb or not.
void markTint(const VectorGroup& group, bool& tintable, std::optional<Color>& color) {
    auto markColor = [&](Color c) {
        if (!tintable) return;
        if (!color) color = c;
        else if (color->r != c.r || color->g != c.g || color->b != c.b) tintable = false, color.reset();
    };
    auto markBrush = [&](const std::optional<Brush>& brush) {
        if (!tintable || !brush) return;
        if (brush->kind() == Brush::Kind::Solid && !brush->stops().empty()) markColor(brush->stops().front().color);
        else tintable = false, color.reset();
    };
    for (const VectorNode& node : group) {
        if (const VectorPath* p = node.path()) {
            markBrush(p->fill);
            markBrush(p->stroke);
        } else {
            bool childTintable = true;
            std::optional<Color> childColor;
            markTint(*node.group(), childTintable, childColor);
            if (childTintable && tintable) {
                if (childColor) markColor(*childColor);
            } else {
                tintable = false, color.reset();
            }
        }
    }
}
} // namespace

ImageVector::Builder::Builder(std::string name, float defaultWidth, float defaultHeight, float viewportWidth, float viewportHeight,
                              std::optional<Color> tintColor, BlendMode tintBlendMode, bool autoMirror)
    : name_(std::move(name)), defaultWidth_(defaultWidth), defaultHeight_(defaultHeight), viewportWidth_(viewportWidth),
      viewportHeight_(viewportHeight), tintColor_(tintColor), tintBlendMode_(tintBlendMode), autoMirror_(autoMirror) {}

ImageVector::Builder& ImageVector::Builder::addGroup(VectorGroup group) {
    groups_.push_back(std::move(group));
    return *this;
}
ImageVector::Builder& ImageVector::Builder::clearGroup() {
    if (groups_.size() > 1) {  // (the root stays)
        VectorGroup closed = std::move(groups_.back());
        groups_.pop_back();
        groups_.back().children.emplace_back(std::move(closed));
    }
    return *this;
}
ImageVector::Builder& ImageVector::Builder::addPath(VectorPath path) {
    groups_.back().children.emplace_back(std::move(path));
    return *this;
}
ImageVector ImageVector::Builder::build() {
    while (groups_.size() > 1) clearGroup();
    auto data = std::make_shared<Data>();
    data->name = name_;
    data->defaultWidth = defaultWidth_, data->defaultHeight = defaultHeight_;
    data->viewportWidth = viewportWidth_, data->viewportHeight = viewportHeight_;
    data->root = groups_.front();
    data->tintColor = tintColor_;
    data->tintBlendMode = tintBlendMode_;
    data->autoMirror = autoMirror_;
    data->id = gNextVectorId++;
    markTint(data->root, data->tintable, data->oneColor);
    ImageVector vector;
    vector.data_ = std::move(data);
    return vector;
}

ImageVector ImageVector::fromSvgPath(std::string_view pathData, float viewportWidth, float viewportHeight) {
    return Builder(viewportWidth, viewportHeight, viewportWidth, viewportHeight)
        .addPath({.pathData = addPathNodes(pathData), .fill = Brush::solid(colors::Black)})
        .build();
}

const std::string& ImageVector::name() const {
    static const std::string none;
    return data_ ? data_->name : none;
}
float ImageVector::defaultWidth() const { return data_ ? data_->defaultWidth : 0; }
float ImageVector::defaultHeight() const { return data_ ? data_->defaultHeight : 0; }
float ImageVector::viewportWidth() const { return data_ ? data_->viewportWidth : 0; }
float ImageVector::viewportHeight() const { return data_ ? data_->viewportHeight : 0; }
const VectorGroup& ImageVector::root() const {
    static const VectorGroup none;
    return data_ ? data_->root : none;
}
std::optional<Color> ImageVector::tintColor() const { return data_ ? data_->tintColor : std::nullopt; }
BlendMode ImageVector::tintBlendMode() const { return data_ ? data_->tintBlendMode : BlendMode::SrcIn; }
bool ImageVector::autoMirror() const { return data_ && data_->autoMirror; }
uint64_t ImageVector::id() const { return data_ ? data_->id : 0; }

bool operator==(const ImageVector& a, const ImageVector& b) {
    if (a.data_ == b.data_) return true;
    if (!a.data_ || !b.data_) return false;
    const ImageVector::Data &x = *a.data_, &y = *b.data_;
    return x.name == y.name && x.defaultWidth == y.defaultWidth && x.defaultHeight == y.defaultHeight && x.viewportWidth == y.viewportWidth &&
           x.viewportHeight == y.viewportHeight && x.root == y.root && x.tintColor == y.tintColor && x.tintBlendMode == y.tintBlendMode &&
           x.autoMirror == y.autoMirror;
}

// --- Drawing (Compose's VectorComponent, GroupComponent, PathComponent, DrawCache) ------------

namespace {

// A tint the Alpha8 bitmap of a one-color vector can be drawn through (Compose's
// tintableWithAlphaMask): none, or a tint by SrcIn or SrcOver.
bool maskable(const std::optional<ColorFilter>& filter) {
    return !filter || (filter->kind() == ColorFilter::Kind::Tint && (filter->blendMode() == BlendMode::SrcIn || filter->blendMode() == BlendMode::SrcOver));
}

void drawPathNode(DrawScope& s, const VectorPath& p) {
    Path path = toPath(p.pathData);
    path.fillType = p.pathFillType;
    if (p.trimPathStart != 0 || p.trimPathEnd != 1) {  // the piece of its first contour, as Android's PathMeasure measures it
        PathMeasure measure(path, false);
        float length = measure.length();
        float start = std::fmod(p.trimPathStart + p.trimPathOffset, 1.f) * length, end = std::fmod(p.trimPathEnd + p.trimPathOffset, 1.f) * length;
        Path trimmed;  // (filled non-zero, as Compose's render path is)
        if (start > end) {
            Path piece;
            measure.getSegment(start, length, piece, true);
            trimmed.addPath(piece);
            piece.reset();
            measure.getSegment(0, end, piece, true);
            trimmed.addPath(piece);
        } else {
            measure.getSegment(start, end, trimmed, true);
        }
        path = std::move(trimmed);
    }
    if (p.fill) s.drawPath(path, *p.fill, p.fillAlpha);
    if (p.stroke) s.drawPath(path, *p.stroke, p.strokeAlpha, Stroke(p.strokeLineWidth, p.strokeLineMiter, p.strokeLineCap, p.strokeLineJoin));
}

void drawGroupNode(DrawScope& s, const VectorGroup& g) {
    auto children = [&g](DrawScope& in) {
        for (const VectorNode& node : g) {
            if (const VectorPath* p = node.path()) drawPathNode(in, *p);
            else drawGroupNode(in, *node.group());
        }
    };
    Matrix m;
    m.translate(g.translationX + g.pivotX, g.translationY + g.pivotY);
    m.rotateZ(g.rotation);
    m.scale(g.scaleX, g.scaleY, 1);
    m.translate(-g.pivotX, -g.pivotY);
    if (m.isIdentity() && g.clipPathData.empty()) return children(s);  // (nothing to transform or clip by)
    s.withTransform(
        [&](DrawTransform& t) {
            if (!m.isIdentity()) t.transform(m);
            if (!g.clipPathData.empty()) t.clipPath(toPath(g.clipPathData));
        },
        children);
}

constexpr size_t kDrawnKept = 8;  // bitmaps a vector keeps (for as many sizes and configs)

} // namespace

void detail::drawVector(DrawScope& scope, const ImageVector& vector, float alpha, const std::optional<ColorFilter>& colorFilter) {
    if (vector.empty() || alpha <= 0) return;
    const ImageVector::Data& d = *vector.data_;
    float density = scope.density().density;
    Size size = scope.size();
    float width = size.width * density, height = size.height * density;  // px
    if (!(width > 0 && height > 0) || d.viewportWidth <= 0 || d.viewportHeight <= 0) return;
    std::optional<ColorFilter> intrinsic;
    if (d.tintColor) intrinsic = ColorFilter::tint(*d.tintColor, d.tintBlendMode);
    bool oneColor = d.tintable && d.oneColor;
    ImageBitmapConfig config = oneColor && maskable(intrinsic) && maskable(colorFilter) ? ImageBitmapConfig::Alpha8 : ImageBitmapConfig::Argb8888;
    // Its bitmap: whole pixels around it (just its size when that is within a hundredth of whole
    // pixels), the vector scaled to fill the size.
    auto pixels = [](float v) { return std::abs(v - std::round(v)) < 0.01f && std::round(v) >= 1 ? int(std::round(v)) : int(std::ceil(v)); };
    int w = pixels(width), h = pixels(height);
    if (w > maxImageDimension || h > maxImageDimension) return;
    ImageBitmap bitmap;
    {
        std::lock_guard lock(d.mutex);
        for (auto it = d.drawn.begin(); it != d.drawn.end(); ++it)
            if (it->width == width && it->height == height && it->config == config) {
                std::rotate(it, it + 1, d.drawn.end());  // (the last drawn last)
                bitmap = d.drawn.back().bitmap;
                break;
            }
    }
    if (bitmap.empty()) {
        bitmap = ImageBitmap(w, h, config);
        {
            graphics::Canvas canvas(bitmap);
            DrawScope cache(canvas, {float(w), float(h)}, nullptr, scope.density(), LayoutDirection::Ltr);
            cache.scale(width / d.viewportWidth, height / d.viewportHeight, [&](DrawScope& s) { drawGroupNode(s, d.root); }, Offset{0, 0});
        }
        std::lock_guard lock(d.mutex);
        d.drawn.push_back({width, height, config, bitmap});
        std::lock_guard registry(drawnInto().mutex);
        drawnInto().vectors[bitmap.id()] = d.id;
        if (d.drawn.size() > kDrawnKept) {
            drawnInto().vectors.erase(d.drawn.front().bitmap.id());
            d.drawn.erase(d.drawn.begin());
        }
    }
    // Through the painter's filter, else the vector's tint, else (one color, as a mask) its color.
    std::optional<ColorFilter> filter = colorFilter ? colorFilter : intrinsic;
    if (!filter && config == ImageBitmapConfig::Alpha8) filter = ColorFilter::tint(d.oneColor->copy(1));
    auto draw = [&](DrawScope& s) {
        s.drawImage(bitmap, IntOffset{0, 0}, IntSize{w, h}, Offset{0, 0}, Size{float(w) / density, float(h) / density}, alpha, Fill, filter);
    };
    if (d.autoMirror && scope.layoutDirection() == LayoutDirection::Rtl) scope.scale(-1, 1, draw);
    else draw(scope);
}

uint64_t detail::vectorDrawnInto(const ImageBitmap& bitmap) {
    std::lock_guard lock(drawnInto().mutex);
    auto it = drawnInto().vectors.find(bitmap.id());
    return it == drawnInto().vectors.end() ? 0 : it->second;
}

} // namespace kompoz

namespace kompoz {

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
