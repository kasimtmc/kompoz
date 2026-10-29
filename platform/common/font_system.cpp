// Kompoz's text on every platform: fonts, shaping and glyph rasterization (see font_system.hpp).

#include "font_system.hpp"

#include "system.hpp"

#include FT_TRUETYPE_TABLES_H
#include FT_SYNTHESIS_H
#include FT_OUTLINE_H
#include FT_STROKER_H
#include FT_MULTIPLE_MASTERS_H

#if KOMPOZ_HARFBUZZ
#include <hb-ft.h>
#include <hb.h>
#endif

#include <kompoz/bidi.hpp>

#include "raster.hpp"

#ifdef __ANDROID__
#include "../android/android_ime.hpp"
#endif

#include <algorithm>
#include <cmath>
#include <functional>
#include <optional>
#include <unordered_map>
#include <utility>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
// The web has no font files to fall back on: what the bundled font lacks (emoji, CJK, Arabic,
// symbols) is drawn by the browser on a 2D canvas, with its own fonts and shaping.
EM_JS(void, kompoz_web_text_size, (const char* text, int px, int flags, float* out), {
    const s = UTF8ToString(text);
    const c = Module.kompozTextCanvas || (Module.kompozTextCanvas = new OffscreenCanvas(1, 1));
    const font = ((flags & 2) ? 'italic ' : "") + ((flags >> 4) || 400) + ' ' + px + 'px sans-serif';
    let ctx = c.getContext('2d');
    ctx.font = font;
    ctx.direction = (flags & 4) ? 'rtl' : 'ltr';
    ctx.textAlign = 'left';
    const m = ctx.measureText(s);
    const left = Math.ceil(m.actualBoundingBoxLeft) + 1, right = Math.ceil(m.actualBoundingBoxRight) + 1;
    const ascent = Math.ceil(m.actualBoundingBoxAscent) + 1, descent = Math.ceil(m.actualBoundingBoxDescent) + 1;
    const values = [m.width, Math.max(1, left + right), Math.max(1, ascent + descent), left, ascent];
    for (let i = 0; i < 5; ++i) HEAPF32[(out >> 2) + i] = values[i];
    if (!(flags & 8)) return;  // the advance only
    c.width = values[1];
    c.height = values[2];
    ctx = c.getContext('2d');
    ctx.font = font;
    ctx.direction = (flags & 4) ? 'rtl' : 'ltr';
    ctx.textAlign = 'left';
    ctx.textBaseline = 'alphabetic';
    ctx.fillStyle = '#fff';  // white: tinted like any glyph, unless it came out in color (emoji)
    ctx.fillText(s, left, ascent);
    Module.kompozTextPixels = ctx.getImageData(0, 0, values[1], values[2]).data;
});
EM_JS(void, kompoz_web_text_pixels, (unsigned char* out), { HEAPU8.set(Module.kompozTextPixels, out); });
EM_JS_DEPS(kompoz_web_text, "$UTF8ToString");
#endif

namespace kompoz::platform {
namespace {

std::vector<std::string> fontCandidates(FontWeight weight) {
    if (weight == FontWeight::Medium)
        return {"fonts/Medium.ttf",
                "/usr/share/fonts/noto/NotoSans-Medium.ttf",
                "/usr/share/fonts/truetype/noto/NotoSans-Medium.ttf",
                "/system/fonts/Roboto-Medium.ttf"};
    if (weight == FontWeight::Bold)
        return {"fonts/Bold.ttf",  // bundled with the app (APK assets, app bundle, next to the binary)
                "/usr/share/fonts/noto/NotoSans-Bold.ttf",
                "/usr/share/fonts/truetype/noto/NotoSans-Bold.ttf",
                "/usr/share/fonts/TTF/DejaVuSans-Bold.ttf",
                "/usr/share/fonts/truetype/dejavu/DejaVuSans-Bold.ttf",
                "/System/Library/Fonts/Supplemental/Arial Bold.ttf",
                "C:/Windows/Fonts/segoeuib.ttf",
                "/system/fonts/Roboto-Bold.ttf"};
    return {"fonts/Regular.ttf",
            "/usr/share/fonts/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/truetype/noto/NotoSans-Regular.ttf",
            "/usr/share/fonts/TTF/DejaVuSans.ttf",
            "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
            "/System/Library/Fonts/Supplemental/Arial.ttf",
            "C:/Windows/Fonts/segoeui.ttf",
            "/system/fonts/Roboto-Regular.ttf"};
}

// System fonts for the generic families, by variant (regular, bold, italic, bold italic).
std::vector<std::string> systemFamilyCandidates(uint16_t family, bool bold, bool italic) {
    if (family == FontFamily::Serif.id) {
        if (bold && italic)
            return {"/system/fonts/NotoSerif-BoldItalic.ttf", "/usr/share/fonts/noto/NotoSerif-BoldItalic.ttf",
                    "/usr/share/fonts/TTF/DejaVuSerif-BoldItalic.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSerif-BoldItalic.ttf",
                    "C:/Windows/Fonts/timesbi.ttf"};
        if (bold)
            return {"/system/fonts/NotoSerif-Bold.ttf", "/usr/share/fonts/noto/NotoSerif-Bold.ttf",
                    "/usr/share/fonts/TTF/DejaVuSerif-Bold.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Bold.ttf",
                    "C:/Windows/Fonts/timesbd.ttf"};
        if (italic)
            return {"/system/fonts/NotoSerif-Italic.ttf", "/usr/share/fonts/noto/NotoSerif-Italic.ttf",
                    "/usr/share/fonts/TTF/DejaVuSerif-Italic.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSerif-Italic.ttf",
                    "C:/Windows/Fonts/timesi.ttf"};
        return {"/system/fonts/NotoSerif-Regular.ttf", "/usr/share/fonts/noto/NotoSerif-Regular.ttf",
                "/usr/share/fonts/TTF/DejaVuSerif.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSerif.ttf",
                "/System/Library/Fonts/Supplemental/Times New Roman.ttf", "C:/Windows/Fonts/times.ttf"};
    }
    if (family == FontFamily::Monospace.id) {
        if (bold)
            return {"/usr/share/fonts/noto/NotoSansMono-Bold.ttf", "/usr/share/fonts/TTF/DejaVuSansMono-Bold.ttf",
                    "/usr/share/fonts/truetype/dejavu/DejaVuSansMono-Bold.ttf", "C:/Windows/Fonts/consolab.ttf"};
        if (italic) return {};
        return {"/system/fonts/DroidSansMono.ttf", "/usr/share/fonts/noto/NotoSansMono-Regular.ttf",
                "/usr/share/fonts/TTF/DejaVuSansMono.ttf", "/usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf",
                "/System/Library/Fonts/Menlo.ttc", "C:/Windows/Fonts/consola.ttf"};
    }
    return {};
}

// Where characters the chosen font lacks come from, tried in order (and loaded only when needed).
const std::vector<std::string>& fallbackFonts() {
    static const std::vector<std::string> paths = {
        // Scripts the bundled font lacks: Arabic, Hebrew, Devanagari, Bengali, Tamil, Thai, Georgian, Armenian.
        "/system/fonts/NotoNaskhArabicUI-Regular.ttf", "/system/fonts/NotoNaskhArabic-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansArabic-Regular.ttf", "/usr/share/fonts/truetype/noto/NotoSansArabic-Regular.ttf",
        "/system/fonts/NotoSansHebrew-Regular.ttf", "/usr/share/fonts/noto/NotoSansHebrew-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansHebrew-Regular.ttf",
        "/system/fonts/NotoSansDevanagariUI-VF.ttf", "/system/fonts/NotoSansDevanagari-VF.ttf",
        "/system/fonts/NotoSansDevanagariUI-Regular.ttf", "/usr/share/fonts/noto/NotoSansDevanagari-Regular.ttf",
        "/usr/share/fonts/truetype/noto/NotoSansDevanagari-Regular.ttf",
        "/system/fonts/NotoSansBengaliUI-VF.ttf", "/system/fonts/NotoSansBengaliUI-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansBengali-Regular.ttf",
        "/system/fonts/NotoSansTamilUI-VF.ttf", "/system/fonts/NotoSansTamilUI-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansTamil-Regular.ttf",
        "/system/fonts/NotoSansThaiUI-Regular.ttf", "/usr/share/fonts/noto/NotoSansThai-Regular.ttf",
        "/system/fonts/NotoSansGeorgian-VF.ttf", "/usr/share/fonts/noto/NotoSansGeorgian-Regular.ttf",
        "/system/fonts/NotoSansArmenian-VF.ttf", "/usr/share/fonts/noto/NotoSansArmenian-Regular.ttf",
        "C:/Windows/Fonts/arial.ttf", "C:/Windows/Fonts/Nirmala.ttc", "C:/Windows/Fonts/tahoma.ttf",
        "/system/fonts/NotoSansSymbols-Regular-Subsetted.ttf", "/system/fonts/NotoSansSymbols-Regular-Subsetted2.ttf",
        "/usr/share/fonts/noto/NotoSansSymbols-Regular.ttf", "/usr/share/fonts/noto/NotoSansSymbols2-Regular.ttf",
        "/usr/share/fonts/noto/NotoSansMath-Regular.ttf", "/usr/share/fonts/TTF/DejaVuSans.ttf",
        "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf", "C:/Windows/Fonts/seguisym.ttf",
        "/system/fonts/NotoColorEmoji.ttf", "/system/fonts/NotoColorEmojiFlags.ttf", "/usr/share/fonts/noto/NotoColorEmoji.ttf",
        "/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf", "C:/Windows/Fonts/seguiemj.ttf",
        "/system/fonts/NotoSansCJK-Regular.ttc", "/usr/share/fonts/noto-cjk/NotoSansCJK-Regular.ttc",
        "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc", "C:/Windows/Fonts/msyh.ttc"};
    return paths;
}

} // namespace

struct FontSystem::Impl {
    Impl() { FT_Init_FreeType(&library_); }

    ~Impl() {
#if KOMPOZ_HARFBUZZ
        for (auto& [face, font] : hbFonts_) hb_font_destroy(font);
        if (buffer_) hb_buffer_destroy(buffer_);
#endif
        for (auto& f : faces_)
            if (f->face) FT_Done_Face(f->face);
        FT_Done_FreeType(library_);
    }

    // The bundled sans-serif in one weight (Normal, Medium or Bold).
    bool load(FontWeight weight, const std::string& preferredPath) {
        const char* name = weight == FontWeight::Bold ? "bold" : weight == FontWeight::Medium ? "medium" : "regular";
        std::vector<std::string> candidates;
        if (!preferredPath.empty()) candidates.push_back(preferredPath);
        const char* variable = weight == FontWeight::Bold ? "KOMPOZ_FONT_BOLD" : weight == FontWeight::Medium ? "KOMPOZ_FONT_MEDIUM" : "KOMPOZ_FONT";
        if (const char* env = std::getenv(variable)) candidates.push_back(env);
        const std::string& base = platform::programDirectory();
        for (const auto& path : fontCandidates(weight)) {
            if (!base.empty() && path.front() != '/' && path.find(':') == std::string::npos) candidates.push_back(base + path);
            candidates.push_back(path);
        }
        for (const auto& path : candidates) {
            if (Face* f = loadFace(path, false)) {  // (platform::readFile: Android's APK assets too)
                setWeight(*f, weight);
                defaults_.push_back(f);
                platform::log(std::string("kompoz: ") + name + " font: " + path);
                return true;
            }
        }
        return false;
    }

    int rasterized = 0;  // glyphs rasterized so far (KOMPOZ_PROFILE)
    gpu::GlyphAtlas atlas;
    void setScale(float scale) { scale_ = scale; }
    void setFontScale(float fontScale) {
        if (fontScale > 0) fontScale_ = fontScale;
    }
    FT_Library library() const { return library_; }

    float measureWidth(std::string_view text, const Font& font) { return shaped(text, font, false)->advance / scale_; }
    std::vector<float> clusterAdvances(std::string_view text, const Font& font) {
        std::vector<float> out(text.size(), 0.f);
        for (const ShapedGlyph& g : shaped(text, font, false)->glyphs)
            if (g.cluster >= 0 && size_t(g.cluster) < out.size()) out[size_t(g.cluster)] += g.advance / scale_;
        return out;
    }
    float fontScale() const { return fontScale_; }

    FontMetrics metrics(const Font& font) {
        int px = pixelSize(font);
        uint64_t key = uint64_t(px) | (uint64_t(std::clamp(font.weight.weight, 1, 1000)) << 32) |
                       (uint64_t(font.style == FontStyle::Italic) << 42) | (uint64_t(font.family) << 43);
        auto it = metrics_.find(key);
        if (it == metrics_.end()) {
            FT_Face face = resolve(font).face->face;
            FT_Set_Pixel_Sizes(face, 0, px);
            const FT_Size_Metrics& m = face->size->metrics;
            it = metrics_.emplace(key, FontMetrics{m.ascender / 64.f, -m.descender / 64.f, m.height / 64.f}).first;
        }
        const FontMetrics& m = it->second;  // cached in pixels
        return {m.ascent / scale_, m.descent / scale_, m.lineHeight / scale_};
    }

    // The glyphs of text from the pen at (xPx, baselinePx). `shadow`: only the glyphs that take a
    // color (no emoji), blurred by `blurPx`. `scale`: px per dp for this text, if not scale_.
    void layout(std::string_view text, float xPx, float baselinePx, const Font& font, bool rtl, std::vector<gpu::GlyphQuad>& out,
                bool shadow, int blurPx, float scale) {
        struct Restore {
            float& value;
            float kept;
            ~Restore() { value = kept; }
        } restore{scale_, scale_};
        if (scale > 0) scale_ = scale;
        int px = pixelSize(font);
        GlyphStyle style{font.stroked ? std::max(1, int(std::lround(font.strokeWidth * scale_))) : 0, font.strokeJoin, font.scaleX, font.skewX,
                         font.strokeMiter};
        std::shared_ptr<const Shaped> shape = shaped(text, font, rtl);
        for (const ShapedGlyph& sg : shape->glyphs) {
            if (shadow && !sg.face) continue;
            Glyph g = sg.face ? (blurPx > 0     ? blurredGlyph(*sg.face, sg.index, px, sg.synthBold, sg.synthItalic, blurPx, style)
                                 : !style.plain() ? styledGlyph(*sg.face, sg.index, px, sg.synthBold, sg.synthItalic, style)
                                                : glyphAt(*sg.face, sg.index, px, sg.synthBold, sg.synthItalic))
                              : platformGlyph(std::string_view(shape->text).substr(size_t(sg.start), size_t(sg.end - sg.start)), px,
                                              font, sg.rtl);
            if (g.width <= 0 || (shadow && g.color)) continue;
            float x0 = std::round(xPx + sg.x) + float(g.left), y0 = std::round(baselinePx - sg.y) - float(g.top);
            atlas.use(g.page);
            out.push_back({x0, y0, x0 + float(g.width), y0 + float(g.height), g.page, g.x, g.y, g.width, g.height, g.color});
        }
    }

    struct Face {
        int id = 0;  // its index in faces_
        FT_Face face = nullptr;
        std::vector<uint8_t> data;  // in-memory fonts (assets)
        FontWeight weight;
        bool bold = false, italic = false;  // bold: 600 and up
    };
    static void setWeight(Face& f, FontWeight weight) {
        f.weight = weight;
        f.bold = weight.weight >= 600;
    }
    struct Glyph {
        float advance = 0;
        int left = 0, top = 0, width = 0, height = 0, x = 0, y = 0;  // x, y: on the atlas page
        bool color = false;  // an emoji bitmap: drawn untinted
        int page = 0;
        uint64_t generation = 0;  // the page's when it was put there
    };
    // A cached glyph that is still on its atlas page (or has nothing to draw).
    bool fresh(const Glyph& g) const { return g.width <= 0 || atlas.valid(g.page, g.generation); }
    struct Resolved {
        Face* face = nullptr;
        bool synthBold = false, synthItalic = false;
        bool operator==(const Resolved&) const = default;
    };

    // Loads a font once by path (and a variable one's axis settings: a face for each instance):
    // assets are read whole (platform::readFile: Android's APK assets too), system fonts are
    // opened by FreeType directly. Failures are remembered too.
    Face* loadFace(const std::string& path, bool system, const std::vector<FontVariation::Setting>& variations = {}) {
        std::string key = path;
        for (const FontVariation::Setting& v : variations) key += "\x1f" + v.axis + "=" + std::to_string(v.value);
        if (auto it = byPath_.find(key); it != byPath_.end()) return it->second;
        auto f = std::make_unique<Face>();
        bool absolute = !path.empty() && (path.front() == '/' || (path.size() > 1 && path[1] == ':'));
        bool ok = false;
        if (absolute && system) {
            ok = FT_New_Face(library_, path.c_str(), 0, &f->face) == 0;
        } else {
            f->data = platform::readFile(path);
            if (!f->data.empty()) ok = FT_New_Memory_Face(library_, f->data.data(), FT_Long(f->data.size()), 0, &f->face) == 0;
        }
        Face* result = nullptr;
        if (ok && !variations.empty() && FT_HAS_MULTIPLE_MASTERS(f->face)) setVariations(f->face, variations);
        if (ok) {
            auto* os2 = static_cast<TT_OS2*>(FT_Get_Sfnt_Table(f->face, FT_SFNT_OS2));
            bool bold = (f->face->style_flags & FT_STYLE_FLAG_BOLD) != 0;
            setWeight(*f, os2 && os2->usWeightClass > 0 ? FontWeight{os2->usWeightClass} : bold ? FontWeight::Bold : FontWeight::Normal);
            f->italic = (f->face->style_flags & FT_STYLE_FLAG_ITALIC) != 0;
            result = f.get();
            f->id = int(faces_.size());
            faces_.push_back(std::move(f));
        }
        byPath_[key] = result;
        return result;
    }

    // A variable face's instance: the axes named by their tags set (clamped to their ranges),
    // the others at their defaults.
    void setVariations(FT_Face face, const std::vector<FontVariation::Setting>& variations) {
        FT_MM_Var* mm = nullptr;
        if (FT_Get_MM_Var(face, &mm) != 0) return;
        std::vector<FT_Fixed> coords(mm->num_axis);
        for (FT_UInt a = 0; a < mm->num_axis; ++a) {
            const FT_Var_Axis& axis = mm->axis[a];
            coords[a] = axis.def;
            for (const FontVariation::Setting& v : variations)
                if (v.axis.size() == 4 && FT_MAKE_TAG(v.axis[0], v.axis[1], v.axis[2], v.axis[3]) == axis.tag)
                    coords[a] = std::clamp(FT_Fixed(std::lround(v.value * 65536)), axis.minimum, axis.maximum);
        }
        FT_Set_Var_Design_Coordinates(face, mm->num_axis, coords.data());
        FT_Done_MM_Var(library_, mm);
    }

    // The face for a family, weight and style: the style's file of the nearest weight (CSS's
    // order), emboldened if it is too light, slanted if there is no italic.
    Resolved resolve(const Font& font) {
        FontWeight weight{std::clamp(font.weight.weight, 1, 1000)};
        bool italic = font.style == FontStyle::Italic;
        uint64_t key = uint64_t(font.family) << 11 | uint64_t(weight.weight) << 1 | uint64_t(italic);
        if (auto it = resolved_.find(key); it != resolved_.end()) return it->second;
        auto nearest = [&](const std::vector<Face*>& faces) -> Face* {
            std::vector<FontWeight> weights;
            for (Face* f : faces) weights.push_back(f->weight);
            int i = FontWeight::closest(weights, weight);
            return i < 0 ? nullptr : faces[size_t(i)];
        };
        auto pick = [&](bool i) -> Face* {
            std::vector<Face*> faces;
            if (font.family >= 16) {
                for (const FontFile& file : FontFamily::files(font.family))
                    if ((file.style == FontStyle::Italic) == i)
                        if (Face* f = loadFace(file.path, false, file.variationSettings)) {
                            setWeight(*f, file.weight);
                            f->italic = i;
                            faces.push_back(f);
                        }
            } else {  // the generic families' system fonts come regular and bold
                for (bool b : {false, true})
                    for (const std::string& path : systemFamilyCandidates(font.family, b, i))
                        if (Face* f = loadFace(path, true)) {
                            faces.push_back(f);
                            break;
                        }
            }
            return nearest(faces);
        };
        auto with = [&](Face* f, bool slant) { return Resolved{f, FontWeight::synthesizesBold(weight, f->weight), slant}; };
        Resolved r;
        if (Face* exact = pick(italic)) r = with(exact, false);
        else if (Face* upright = italic ? pick(false) : nullptr) r = with(upright, true);
        else r = with(nearest(defaults_), italic);  // the bundled sans-serif
        resolved_[key] = r;
        return r;
    }

    // Text sizes are sp: the system font size setting scales them.
    int pixelSize(const Font& font) const { return std::max(1, int(std::lround(font.size * fontScale_ * scale_))); }

    // A multi-character emoji (a ZWJ sequence, a flag, a skin tone, a keycap, a tag sequence),
    // which the emoji font draws as one glyph.
    static bool isEmojiSequence(std::string_view cluster) {
        int count = 0;
        bool joined = false;
        for (size_t i = 0; i < cluster.size(); ++count) {
            char32_t c = utf8::next(cluster, i);
            joined = joined || c == 0x200D || utf8::isRegionalIndicator(c) || (c >= 0x1F3FB && c <= 0x1F3FF) || c == 0x20E3 ||
                     (c >= 0xE0020 && c <= 0xE007F);
        }
        return count > 1 && joined;
    }

    // Laid out text: glyphs of the faces the characters came from, placed from the pen's start.
    struct ShapedGlyph {
        Face* face = nullptr;  // null: a cluster the platform draws, text[start, end) (Android's COLRv1 emoji)
        uint32_t index = 0;    // glyph id in the face
        float x = 0, y = 0;    // px from the start, y up
        bool synthBold = false, synthItalic = false;
        int start = 0, end = 0;
        bool rtl = false;  // a platform-drawn stretch going right to left
        int cluster = 0;     // the byte its cluster starts at in the text
        float advance = 0;   // px it moves the pen on by (its letter spacing too)
    };
    struct Shaped {
        std::string text;
        std::vector<ShapedGlyph> glyphs;
        float advance = 0;  // px
    };

    // Shapes text in a font, for a paragraph of the given direction: each cluster gets the face
    // that has it (the font's, else a fallback; emoji presentation asks for a color font), the
    // line is split into runs of one direction (bidi.hpp), and each run's stretches of one face
    // are shaped by HarfBuzz (ligatures, kerning, Arabic joining, Indic reordering) and placed
    // in visual order. Cached by text and font.
    std::shared_ptr<const Shaped> shaped(std::string_view text, const Font& font, bool rtl) {
        int px = pixelSize(font);
        std::string& key = lookupKey_;  // reused: no allocation per lookup
        key.clear();
        uint64_t head = uint64_t(px & 0xfffff) | (uint64_t(font.style == FontStyle::Italic) << 20) | (uint64_t(rtl) << 21) |
                        (uint64_t(std::clamp(font.weight.weight, 1, 1000)) << 22) | (uint64_t(font.family) << 32) |
                        (uint64_t(font.features) << 48);
        key.append(reinterpret_cast<const char*>(&head), sizeof head);
        float spacing = font.letterSpacing * fontScale_ * scale_;  // px after each cluster
        key.append(reinterpret_cast<const char*>(&spacing), sizeof spacing);
        key.append(reinterpret_cast<const char*>(&font.scaleX), sizeof font.scaleX);
        key.append(text);
        if (auto it = shaped_.find(key); it != shaped_.end()) return it->second;
        if (shaped_.size() > 8192) shaped_.clear();
        std::string storedKey = key;  // the lookup buffer is reused while shaping

        auto out = std::make_shared<Shaped>();
        out->text = std::string(text);
        Resolved base = resolve(font);
        struct Item {
            int start, end;
            Resolved face;
            bool platform;
        };
        std::vector<Item> items;  // clusters by face, in logical order
        Face* previous = nullptr;
        for (size_t i = 0; i < text.size();) {
            size_t end = utf8::nextCluster(text, i);
            std::optional<Resolved> found = faceFor(base, text.substr(i, end - i), previous);
            bool platform = found ? drawnByPlatform(*found->face) : kPlatformFallback;
            Resolved r = found.value_or(base);
            // Stretches of the browser's fallback text stay together, for it to shape (Arabic words).
            bool joins = platform ? kPlatformFallback && !found : true;
            if (!items.empty() && items.back().platform == platform && joins && (platform || items.back().face == r))
                items.back().end = int(end);
            else
                items.push_back({int(i), int(end), r, platform});
            previous = platform || FT_HAS_COLOR(r.face->face) ? nullptr : r.face;
            i = end;
        }
        float pen = 0;
        for (const bidi::Run& run : bidi::visualRuns(text, rtl)) {
            std::vector<Item> parts;  // the run's stretches, in the order they are drawn
            for (const Item& item : items) {
                int a = std::max(item.start, run.start), b = std::min(item.end, run.end);
                if (a < b) parts.push_back({a, b, item.face, item.platform});
            }
            if (run.rtl) std::reverse(parts.begin(), parts.end());
            for (const Item& part : parts) {
                if (part.platform) {
                    float advance = platformAdvance(text.substr(size_t(part.start), size_t(part.end - part.start)), px, font, run.rtl) * font.scaleX;
                    for (size_t i = size_t(part.start); i < size_t(part.end); i = utf8::nextCluster(text, i)) advance += spacing;
                    out->glyphs.push_back({nullptr, 0, pen, 0, false, false, part.start, part.end, run.rtl, part.start, advance});
                    pen += advance;
                } else {
                    pen = shapePart(*out, text, part.start, part.end, part.face, run.rtl, px, pen, spacing, font.features, font.scaleX);
                }
            }
        }
        out->advance = pen;
        shaped_.emplace(std::move(storedKey), out);
        return out;
    }

    // The face for a cluster, or nothing if no font has it (or none has it in color, for emoji
    // presentation). Spaces, punctuation and digits stay with the face before them if it has
    // them (Arabic words keep their font's spaces), unless that was an emoji font.
    std::optional<Resolved> faceFor(const Resolved& base, std::string_view cluster, Face* previous) {
        size_t i = 0;
        char32_t cp = utf8::next(cluster, i);
        // Emoji presentation: a sequence, U+FE0F, or a character that is an emoji by default (👍),
        // unless U+FE0E asks for text.
        bool emoji = (isEmojiSequence(cluster) || cluster.find("\xEF\xB8\x8F") != std::string_view::npos || utf8::isEmojiPresentation(cp)) &&
                     cluster.find("\xEF\xB8\x8E") == std::string_view::npos;
        bool bold = base.face->bold || base.synthBold, italic = base.face->italic || base.synthItalic;
        bidi::Class cls = bidi::classOf(cp);
        if (previous && !emoji && previous != base.face && (cls == bidi::Class::Neutral || cls == bidi::Class::EN) &&
            FT_Get_Char_Index(previous->face, cp) != 0)
            return Resolved{previous, bold && !previous->bold, italic && !previous->italic};
        bool missing = FT_Get_Char_Index(base.face->face, cp) == 0;
        if (cp < 0x20 || (!missing && !emoji)) return base;
        Face* plain = nullptr;
        for (const std::string& path : fallbackFonts()) {
            Face* f = loadFace(path, true);
            if (!f || FT_Get_Char_Index(f->face, cp) == 0) continue;
            if (emoji && !FT_HAS_COLOR(f->face)) {  // with emoji presentation, a color font first
                if (!plain) plain = f;
                continue;
            }
            return Resolved{f, bold && !f->bold, italic && !f->italic};
        }
        if (plain && missing) return Resolved{plain, bold && !plain->bold, italic && !plain->italic};
        if (missing || (emoji && kPlatformFallback)) return std::nullopt;
        return base;
    }

#ifdef __EMSCRIPTEN__
    static constexpr bool kPlatformFallback = true;  // the browser draws what no font has
#else
    static constexpr bool kPlatformFallback = false;  // the font's missing-glyph box
#endif

    // Android's emoji fonts: COLRv1, which only its own text stack draws, and PNG bitmaps
    // (the flags), which our FreeType there isn't built to decode.
    static bool drawnByPlatform(const Face& face) {
#ifdef __ANDROID__
        return FT_HAS_COLOR(face.face);
#else
        (void)face;
        return false;
#endif
    }

#if KOMPOZ_HARFBUZZ
    // Scripts whose letters join or hang from a line, which letter spacing would tear apart
    // (Android's text stack leaves them alone too).
    static bool spacesLetters(hb_script_t script) {
        switch (script) {
        case HB_SCRIPT_ARABIC: case HB_SCRIPT_SYRIAC: case HB_SCRIPT_NKO: case HB_SCRIPT_MANDAIC: case HB_SCRIPT_MONGOLIAN:
        case HB_SCRIPT_PHAGS_PA: case HB_SCRIPT_PSALTER_PAHLAVI: case HB_SCRIPT_DEVANAGARI: case HB_SCRIPT_BENGALI:
        case HB_SCRIPT_GURMUKHI: case HB_SCRIPT_MODI: case HB_SCRIPT_SHARADA: case HB_SCRIPT_SYLOTI_NAGRI: case HB_SCRIPT_TIRHUTA:
        case HB_SCRIPT_OGHAM: return false;
        default: return true;
        }
    }
#endif

#if KOMPOZ_HARFBUZZ
    // A FontFeatures id's settings as HarfBuzz features (CSS's syntax: comma separated), once.
    const std::vector<hb_feature_t>& hbFeatures(uint16_t id) {
        auto [it, added] = hbFeatures_.try_emplace(id);
        std::string settings = added ? FontFeatures::settings(id) : std::string();
        if (added)
            for (std::string_view rest = settings; !rest.empty();) {
                size_t comma = rest.find(',');
                std::string_view one = rest.substr(0, comma);
                rest = comma == std::string_view::npos ? std::string_view() : rest.substr(comma + 1);
                while (!one.empty() && one.front() == ' ') one.remove_prefix(1);
                while (!one.empty() && one.back() == ' ') one.remove_suffix(1);
                hb_feature_t f;
                if (!one.empty() && hb_feature_from_string(one.data(), int(one.size()), &f)) it->second.push_back(f);
            }
        return it->second;
    }
#endif

    // Shapes text[start, end), all in one face and one direction, appending its glyphs from `pen`
    // (and `spacing` px after each cluster), with the font's OpenType features.
    float shapePart(Shaped& out, std::string_view text, int start, int end, const Resolved& r, bool rtl, int px, float pen,
                    float spacing, uint16_t features, float scaleX) {
        FT_Face ft = r.face->face;
        float boldExtra = r.synthBold ? float(px) / 24.f * scaleX : 0;  // what FT_GlyphSlot_Embolden adds to an advance
#if KOMPOZ_HARFBUZZ
        float k = sizeColorFace(ft, px);  // the size set on the face (a bitmap font's strike, scaled by k)
        float kx = k * scaleX;            // across: stretched too
        hb_font_t*& font = hbFonts_[r.face];
        if (!font) font = hb_ft_font_create_referenced(ft);
        hb_ft_font_changed(font);
        if (!buffer_) buffer_ = hb_buffer_create();
        hb_buffer_clear_contents(buffer_);
        hb_buffer_add_utf8(buffer_, text.data(), int(text.size()), unsigned(start), end - start);  // the rest is context
        hb_buffer_set_direction(buffer_, rtl ? HB_DIRECTION_RTL : HB_DIRECTION_LTR);
        hb_buffer_guess_segment_properties(buffer_);
        if (!spacesLetters(hb_buffer_get_script(buffer_))) spacing = 0;
        const std::vector<hb_feature_t>& feats = hbFeatures(features);
        hb_shape(font, buffer_, feats.empty() ? nullptr : feats.data(), unsigned(feats.size()));
        unsigned count = 0;
        hb_glyph_info_t* info = hb_buffer_get_glyph_infos(buffer_, &count);
        hb_glyph_position_t* pos = hb_buffer_get_glyph_positions(buffer_, &count);
        for (unsigned j = 0; j < count; ++j) {  // in visual order
            float advance = float(pos[j].x_advance) / 64.f * kx;
            advance = advance != 0 ? advance + boldExtra : 0;
            if (j + 1 == count || info[j + 1].cluster != info[j].cluster) advance += spacing;  // a cluster's last glyph
            out.glyphs.push_back({r.face, info[j].codepoint, pen + float(pos[j].x_offset) / 64.f * kx, float(pos[j].y_offset) / 64.f * k,
                                  r.synthBold, r.synthItalic, 0, 0, false, int(info[j].cluster), advance});
            pen += advance;
        }
#else
        // Without HarfBuzz: one glyph per character, reversed for right-to-left (no joining, no features).
        (void)boldExtra;
        (void)features;
        std::vector<std::pair<char32_t, int>> cps;  // and the byte each starts at
        for (size_t i = size_t(start); i < size_t(end);) {
            int at = int(i);
            char32_t cp = utf8::next(text, i);
            if (cp != 0xFE0F && cp != 0xFE0E && cp != 0x200D) cps.push_back({cp, at});  // selectors and joiners take no room
        }
        if (rtl) std::reverse(cps.begin(), cps.end());
        for (auto [cp, at] : cps) {
            uint32_t index = FT_Get_Char_Index(ft, cp);
            float advance = glyphAt(*r.face, index, px, r.synthBold, r.synthItalic).advance * scaleX + spacing;
            out.glyphs.push_back({r.face, index, pen, 0, r.synthBold, r.synthItalic, 0, 0, false, at, advance});
            pen += advance;
        }
#endif
        return pen;
    }

    // A glyph by id, rasterized once per size: outlines with FreeType (bold and slant
    // synthesized if asked), color bitmaps scaled from their strike, COLRv0 layers composited.
    Glyph glyphAt(Face& face, uint32_t index, int px, bool synthBold, bool synthItalic) {
        uint64_t key = (uint64_t(face.id) << 48) | (uint64_t(px & 0xFFF) << 36) | (uint64_t(synthBold) << 35) |
                       (uint64_t(synthItalic) << 34) | uint64_t(index);
        if (auto it = glyphs_.find(key); it != glyphs_.end() && fresh(it->second)) return it->second;
        Glyph g;
        FT_Face ft = face.face;
        if (FT_HAS_COLOR(ft) && !FT_IS_SCALABLE(ft) && ft->num_fixed_sizes > 0) {
            ColorBitmap b = colorBitmap(ft, index, sizeColorFace(ft, px));
            g.advance = b.advance;
            if (b.width > 0) {
                g.left = b.left, g.top = b.top, g.width = b.width, g.height = b.height;
                g.color = true;
                uploadRgba(b.rgba.data(), g);
            }
        } else if (FT_HAS_COLOR(ft)) {
            scalableColorGlyph(ft, index, px, g);
        } else {
            rasterize(face, index, px, synthBold, synthItalic, {}, g.advance, [&](const FT_Bitmap& b, int left, int top) { uploadAt(g, b, left, top); });
        }
        glyphs_.insert_or_assign(key, g);
        ++rasterized;
        return g;
    }

    // How a text style draws its glyphs besides their face and size: outlined (stroke px and
    // join; 0: filled), stretched across and slanted.
    struct GlyphStyle {
        int strokePx = 0;
        StrokeJoin join = StrokeJoin::Miter;
        float scaleX = 1, skewX = 0;
        float miter = 4;  // a miter join's limit, times the stroke's width
        bool plain() const { return strokePx == 0 && scaleX == 1 && skewX == 0; }
    };
    struct StyledGlyphKey {
        uint64_t glyph;  // glyphAt's key
        int strokePx, join, scaleX, skewX;  // the last two in thousandths
        int blur = 0;                       // px: a shadow's (blurredGlyph)
        int miter = 4000;                   // thousandths
        bool operator==(const StyledGlyphKey&) const = default;
    };
    struct StyledGlyphHash {
        size_t operator()(const StyledGlyphKey& k) const {
            uint64_t h = k.glyph * 0x9E3779B97F4A7C15ull;
            for (int v : {k.strokePx, k.join, k.scaleX, k.skewX, k.blur, k.miter}) h = (h ^ uint64_t(uint32_t(v))) * 0x100000001B3ull;
            return size_t(h);
        }
    };

    // Loads a glyph's outline in a style (stretched and slanted, then stroked if asked; bold and
    // slant synthesized if asked) and hands its coverage to `use` with where it goes from the pen
    // (left, top); `advance` is set once it loaded. False if there was nothing to rasterize.
    bool rasterize(Face& face, uint32_t index, int px, bool synthBold, bool synthItalic, const GlyphStyle& style, float& advance,
                   const std::function<void(const FT_Bitmap&, int left, int top)>& use) {
        FT_Face ft = face.face;
        FT_Set_Pixel_Sizes(ft, 0, px);
        bool transformed = style.scaleX != 1 || style.skewX != 0;
        FT_Matrix transform{FT_Fixed(style.scaleX * 65536), FT_Fixed(style.skewX * 65536), 0, 0x10000};  // y goes up here
        if (transformed) FT_Set_Transform(ft, &transform, nullptr);
        bool loaded = FT_Load_Glyph(ft, index, FT_LOAD_TARGET_LIGHT | (style.plain() ? 0 : FT_LOAD_NO_BITMAP)) == 0;
        if (transformed) FT_Set_Transform(ft, nullptr, nullptr);  // the face is shared: plain again for the others
        if (!loaded) return false;
        FT_GlyphSlot slot = ft->glyph;
        if (synthBold) FT_GlyphSlot_Embolden(slot);   // no bold file: synthesize
        if (synthItalic) FT_GlyphSlot_Oblique(slot);  // no italic file: slant
        advance = float(slot->advance.x) / 64.f;
        if (style.strokePx == 0) {
            if (FT_Render_Glyph(slot, FT_RENDER_MODE_LIGHT) != 0) return false;
            use(slot->bitmap, slot->bitmap_left, slot->bitmap_top);
            return true;
        }
        FT_Glyph glyph = nullptr;
        FT_Stroker stroker = nullptr;
        bool done = false;
        if (slot->format == FT_GLYPH_FORMAT_OUTLINE && FT_Get_Glyph(slot, &glyph) == 0 && FT_Stroker_New(library_, &stroker) == 0) {
            FT_Stroker_Set(stroker, FT_Fixed(style.strokePx * 32),  // half of it on each side
                           FT_STROKER_LINECAP_BUTT,
                           style.join == StrokeJoin::Round   ? FT_STROKER_LINEJOIN_ROUND
                           : style.join == StrokeJoin::Bevel ? FT_STROKER_LINEJOIN_BEVEL
                                                             : FT_STROKER_LINEJOIN_MITER,
                           FT_Fixed(style.miter * 65536));
            if (FT_Glyph_Stroke(&glyph, stroker, 1) == 0 && FT_Glyph_To_Bitmap(&glyph, FT_RENDER_MODE_LIGHT, nullptr, 1) == 0) {
                const FT_BitmapGlyph bitmap = reinterpret_cast<FT_BitmapGlyph>(glyph);
                use(bitmap->bitmap, bitmap->left, bitmap->top);
                done = true;
            }
        }
        if (stroker) FT_Stroker_Done(stroker);
        if (glyph) FT_Done_Glyph(glyph);
        return done;
    }
    void uploadAt(Glyph& g, const FT_Bitmap& bitmap, int left, int top) {
        g.left = left;
        g.top = top;
        g.width = int(bitmap.width);
        g.height = int(bitmap.rows);
        if (g.width > 0 && g.height > 0) upload(bitmap, g);
    }
    StyledGlyphKey styledKey(const Face& face, uint32_t index, int px, bool synthBold, bool synthItalic, const GlyphStyle& style, int blur) const {
        return {(uint64_t(face.id) << 48) | (uint64_t(px & 0xFFF) << 36) | (uint64_t(synthBold) << 35) | (uint64_t(synthItalic) << 34) | uint64_t(index),
                style.strokePx, int(style.join), int(std::lround(style.scaleX * 1000)), int(std::lround(style.skewX * 1000)), blur,
                int(std::lround(style.miter * 1000))};
    }

    // A glyph drawn in a style (see GlyphStyle), rasterized once. Color glyphs are drawn as they are.
    Glyph styledGlyph(Face& face, uint32_t index, int px, bool synthBold, bool synthItalic, const GlyphStyle& style) {
        if (FT_HAS_COLOR(face.face)) return glyphAt(face, index, px, synthBold, synthItalic);
        StyledGlyphKey key = styledKey(face, index, px, synthBold, synthItalic, style, 0);
        if (auto it = styledGlyphs_.find(key); it != styledGlyphs_.end() && fresh(it->second)) return it->second;
        Glyph g;
        rasterize(face, index, px, synthBold, synthItalic, style, g.advance, [&](const FT_Bitmap& b, int left, int top) { uploadAt(g, b, left, top); });
        styledGlyphs_.insert_or_assign(key, g);
        ++rasterized;
        return g;
    }

    // A glyph's outline (in its text's style: outlined, stretched, slanted) blurred by `blur` px
    // (a text shadow), rasterized once: a Gaussian of Skia's sigma for that radius (0.57735 r +
    // 0.5), made of three box blurs. Color glyphs have none.
    Glyph blurredGlyph(Face& face, uint32_t index, int px, bool synthBold, bool synthItalic, int blur, const GlyphStyle& style) {
        uint64_t key = (uint64_t(face.id) << 48) | (uint64_t(px & 0xFFF) << 36) | (uint64_t(synthBold) << 35) |
                       (uint64_t(synthItalic) << 34) | (uint64_t(std::min(blur, 0x3FFFF)) << 16) | (index & 0xFFFF);
        StyledGlyphKey styled = styledKey(face, index, px, synthBold, synthItalic, style, blur);  // a styled text's shadow
        if (style.plain()) {
            if (auto it = glyphs_.find(key); it != glyphs_.end() && fresh(it->second)) return it->second;
        } else if (auto it = styledGlyphs_.find(styled); it != styledGlyphs_.end() && fresh(it->second)) {
            return it->second;
        }
        Glyph g;
        if (!FT_HAS_COLOR(face.face) && index <= 0xFFFF)
            rasterize(face, index, px, synthBold, synthItalic, style, g.advance, [&](const FT_Bitmap& bitmap, int left, int top) {
                if (bitmap.width == 0 || bitmap.rows == 0) return;
                float sigma = 0.57735f * float(blur) + 0.5f;
                int pad = int(std::ceil(sigma * 3));
                int w = int(bitmap.width), h = int(bitmap.rows), W = w + 2 * pad, H = h + 2 * pad;
                std::vector<float> a(size_t(W) * size_t(H), 0);
                for (int row = 0; row < h; ++row)
                    for (int col = 0; col < w; ++col)
                        a[size_t(row + pad) * size_t(W) + size_t(col + pad)] = float(bitmap.buffer[row * bitmap.pitch + col]) / 255.f;
                blurCoverage(a, W, H, sigma);
                std::vector<uint8_t> rgba(a.size() * 4);
                for (size_t i = 0; i < a.size(); ++i) {
                    rgba[i * 4] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
                    rgba[i * 4 + 3] = uint8_t(std::lround(std::clamp(a[i], 0.f, 1.f) * 255));
                }
                g.left = left - pad;
                g.top = top + pad;
                g.width = W;
                g.height = H;
                uploadRgba(rgba.data(), g);
            });
        if (style.plain())
            glyphs_.insert_or_assign(key, g);
        else
            styledGlyphs_.insert_or_assign(styled, g);
        ++rasterized;
        return g;
    }

    // Text drawn by the platform's text stack (Android's COLRv1 emoji; on the web, whatever the
    // bundled font lacks), by size, style and text.
    static int platformFlags(const Font& font, bool rtl) {
        int weight = std::clamp(font.weight.weight, 1, 1000);  // in the bits above 4 (the web's CSS font weight)
        return (weight >= 600 ? 1 : 0) | (font.style == FontStyle::Italic ? 2 : 0) | (rtl ? 4 : 0) | weight << 4;
    }
    float platformAdvance(std::string_view text, int px, const Font& font, bool rtl) {
#ifdef __EMSCRIPTEN__
        float m[5] = {};
        kompoz_web_text_size(std::string(text).c_str(), px, platformFlags(font, rtl), m);
        return m[0];
#else
        return platformGlyph(text, px, font, rtl).advance;
#endif
    }
    Glyph platformGlyph(std::string_view cluster, int px, const Font& font, bool rtl) {
        std::string key = std::to_string(px) + ':' + std::to_string(platformFlags(font, rtl)) + ':' + std::string(cluster);
        if (auto it = platformGlyphs_.find(key); it != platformGlyphs_.end() && fresh(it->second)) return it->second;
        Glyph g;
#ifdef __EMSCRIPTEN__
        float m[5] = {};
        kompoz_web_text_size(std::string(cluster).c_str(), px, platformFlags(font, rtl) | 8, m);
        g.advance = m[0];
        g.width = int(m[1]), g.height = int(m[2]), g.left = -int(m[3]), g.top = int(m[4]);
        std::vector<uint8_t> rgba(size_t(g.width) * size_t(g.height) * 4);
        kompoz_web_text_pixels(rgba.data());
        for (size_t i = 0; i < rgba.size() && !g.color; i += 4)  // drawn in white: anything else is an emoji's color
            g.color = rgba[i + 3] > 32 && (rgba[i] < 200 || rgba[i + 1] < 200 || rgba[i + 2] < 200);
        uploadRgba(rgba.data(), g);
#elif defined(__ANDROID__)
        (void)font, (void)rtl;
        android_ime::PlatformGlyph p = android_ime::renderGlyph(std::string(cluster), px);
        g.advance = p.advance;
        if (p.width > 0) {
            g.left = p.left, g.top = p.top, g.width = p.width, g.height = p.height;
            g.color = true;
            uploadRgba(p.rgba.data(), g);
        }
#else
        (void)font, (void)rtl;
#endif
        platformGlyphs_.insert_or_assign(std::move(key), g);
        ++rasterized;
        return g;
    }

    // A COLRv0 color glyph, composited by FreeType.
    void scalableColorGlyph(FT_Face face, uint32_t index, int px, Glyph& g) {
        FT_Set_Pixel_Sizes(face, 0, px);
        if (FT_Load_Glyph(face, index, FT_LOAD_COLOR) != 0) return;
        FT_GlyphSlot slot = face->glyph;
        g.advance = float(slot->advance.x) / 64.f;
        if (FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0 || slot->bitmap.width == 0) return;
        g.left = slot->bitmap_left, g.top = slot->bitmap_top;
        g.width = int(slot->bitmap.width), g.height = int(slot->bitmap.rows);
        if (slot->bitmap.pixel_mode != FT_PIXEL_MODE_BGRA) return upload(slot->bitmap, g);
        std::vector<uint8_t> rgba(size_t(g.width) * size_t(g.height) * 4);
        for (int y = 0; y < g.height; ++y)
            for (int x = 0; x < g.width; ++x) {
                const uint8_t* s = slot->bitmap.buffer + y * slot->bitmap.pitch + x * 4;  // premultiplied BGRA
                uint8_t* d = &rgba[(size_t(y) * size_t(g.width) + size_t(x)) * 4];
                float a = s[3] / 255.f;
                d[0] = uint8_t(a > 0 ? std::min(255.f, s[2] / a) : 0);
                d[1] = uint8_t(a > 0 ? std::min(255.f, s[1] / a) : 0);
                d[2] = uint8_t(a > 0 ? std::min(255.f, s[0] / a) : 0);
                d[3] = s[3];
            }
        g.color = true;
        uploadRgba(rgba.data(), g);
    }

    // A color glyph, straight-alpha RGBA, `k` times the size the face is set to (a bitmap strike
    // scaled down to the text's size; 1 for scalable color fonts).
    struct ColorBitmap {
        std::vector<uint8_t> rgba;
        int left = 0, top = 0, width = 0, height = 0;
        float advance = 0;
    };
    static float sizeColorFace(FT_Face face, int px) {
        if (FT_IS_SCALABLE(face) || face->num_fixed_sizes == 0) {
            FT_Set_Pixel_Sizes(face, 0, px);
            return 1;
        }
        FT_Select_Size(face, 0);
        float strike = float(face->available_sizes[0].y_ppem) / 64.f;
        if (strike <= 0) strike = float(face->available_sizes[0].height);
        return strike > 0 ? float(px) / strike : 1;
    }
    static ColorBitmap colorBitmap(FT_Face face, FT_UInt index, float k) {
        ColorBitmap out;
        if (FT_Load_Glyph(face, index, FT_LOAD_COLOR) != 0) return out;
        FT_GlyphSlot slot = face->glyph;
        out.advance = float(slot->advance.x) / 64.f * k;
        if (slot->format != FT_GLYPH_FORMAT_BITMAP && FT_Render_Glyph(slot, FT_RENDER_MODE_NORMAL) != 0) return out;
        const FT_Bitmap& b = slot->bitmap;
        if (b.width == 0 || b.rows == 0) return out;
        int sw = int(b.width), sh = int(b.rows);
        bool bgra = b.pixel_mode == FT_PIXEL_MODE_BGRA;
        int w = std::max(1, int(std::lround(float(sw) * k))), h = std::max(1, int(std::lround(float(sh) * k)));
        std::vector<uint8_t>& rgba = out.rgba;
        rgba.resize(size_t(w) * size_t(h) * 4);
        for (int y = 0; y < h; ++y)  // box filter over the source pixels each target pixel covers
            for (int x = 0; x < w; ++x) {
                int x0 = x * sw / w, x1 = std::max(x0 + 1, (x + 1) * sw / w);
                int y0 = y * sh / h, y1 = std::max(y0 + 1, (y + 1) * sh / h);
                float sum[4] = {};
                for (int sy = y0; sy < y1; ++sy)
                    for (int sx = x0; sx < x1; ++sx) {
                        if (bgra) {
                            const uint8_t* p = b.buffer + sy * b.pitch + sx * 4;  // premultiplied BGRA
                            sum[0] += p[2], sum[1] += p[1], sum[2] += p[0], sum[3] += p[3];
                        } else {  // a plain glyph in a color font: white coverage
                            uint8_t a = b.buffer[sy * b.pitch + sx];
                            sum[0] += a, sum[1] += a, sum[2] += a, sum[3] += a;
                        }
                    }
                float n = float((x1 - x0) * (y1 - y0)), a = sum[3] / n;
                uint8_t* d = &rgba[(size_t(y) * size_t(w) + size_t(x)) * 4];
                for (int c = 0; c < 3; ++c) d[c] = uint8_t(a > 0 ? std::min(255.f, sum[c] / n * 255.f / a) : 0);  // straight alpha
                d[3] = uint8_t(a);
            }
        out.left = int(std::lround(float(slot->bitmap_left) * k));
        out.top = int(std::lround(float(slot->bitmap_top) * k));
        out.width = w;
        out.height = h;
        return out;
    }

    void upload(const FT_Bitmap& bitmap, Glyph& g) {
        std::vector<uint8_t> rgba(size_t(g.width) * size_t(g.height) * 4);
        for (int row = 0; row < g.height; ++row) {
            const unsigned char* src = bitmap.buffer + row * bitmap.pitch;
            uint8_t* dst = rgba.data() + size_t(row) * size_t(g.width) * 4;
            for (int col = 0; col < g.width; ++col) {
                dst[col * 4 + 0] = dst[col * 4 + 1] = dst[col * 4 + 2] = 255;  // white, tinted by the text's color
                dst[col * 4 + 3] = src[col];
            }
        }
        uploadRgba(rgba.data(), g);
    }

    // Puts a glyph's pixels on the atlas (nothing to draw if it is too big for a page).
    void uploadRgba(const uint8_t* pixels, Glyph& g) {
        if (!atlas.add(pixels, g.width, g.height, g.page, g.x, g.y)) {
            g.width = g.height = 0;
            return;
        }
        g.generation = atlas.generation(g.page);
    }

    FT_Library library_ = nullptr;
    std::vector<std::unique_ptr<Face>> faces_;  // every font loaded
    std::unordered_map<std::string, Face*> byPath_;
    std::unordered_map<uint64_t, Resolved> resolved_;
    std::vector<Face*> defaults_;  // the bundled sans-serif's weights (regular first)
    float scale_ = 1, fontScale_ = 1;
    std::unordered_map<uint64_t, Glyph> glyphs_;            // by face, glyph id and size
    std::unordered_map<StyledGlyphKey, Glyph, StyledGlyphHash> styledGlyphs_;  // the same in a style (outlined, stretched, slanted)
    std::unordered_map<std::string, Glyph> platformGlyphs_;  // by size and text
    std::unordered_map<std::string, std::shared_ptr<const Shaped>> shaped_;
#if KOMPOZ_HARFBUZZ
    std::unordered_map<uint16_t, std::vector<hb_feature_t>> hbFeatures_;
#endif
    std::string lookupKey_;
#if KOMPOZ_HARFBUZZ
    std::unordered_map<Face*, hb_font_t*> hbFonts_;
    hb_buffer_t* buffer_ = nullptr;
#endif
    std::unordered_map<uint64_t, FontMetrics> metrics_;
};

FontSystem::FontSystem() : impl_(std::make_unique<Impl>()) {}
FontSystem::~FontSystem() = default;
bool FontSystem::load(FontWeight weight, const std::string& preferredPath) { return impl_->load(weight, preferredPath); }
void FontSystem::setScale(float scale) { impl_->setScale(scale); }
void FontSystem::setFontScale(float fontScale) { impl_->setFontScale(fontScale); }
FT_Library FontSystem::library() const { return impl_->library(); }
int FontSystem::rasterized() const { return impl_->rasterized; }
float FontSystem::measureWidth(std::string_view utf8, const Font& font) { return impl_->measureWidth(utf8, font); }
std::vector<float> FontSystem::clusterAdvances(std::string_view utf8, const Font& font) { return impl_->clusterAdvances(utf8, font); }
FontMetrics FontSystem::metrics(const Font& font) { return impl_->metrics(font); }
float FontSystem::fontScale() const { return impl_->fontScale(); }
void FontSystem::layout(std::string_view text, float xPx, float baselinePx, const Font& font, bool rtl, std::vector<gpu::GlyphQuad>& out,
                        bool shadow, int blurPx, float scale) {
    impl_->layout(text, xPx, baselinePx, font, rtl, out, shadow, blurPx, scale);
}
gpu::GlyphAtlas& FontSystem::atlas() { return impl_->atlas; }
void FontSystem::endFrame() { impl_->atlas.endFrame(); }

} // namespace kompoz::platform
