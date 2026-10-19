// Kompoz platform backend on SDL3: window, input, IME/soft keyboard and 2D rendering.
//
// SDL3 runs on Windows, macOS, Linux, Android, iOS and the web, and the main-callback model
// used here (SDL_AppInit / SDL_AppIterate / SDL_AppEvent) is the one that works on all of
// them, so this single file is what makes a Kompoz app multiplatform.
// Text is rasterized with FreeType into a glyph atlas (font_system.hpp).

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_OUTLINE_H
#include FT_STROKER_H

#include <kompoz/app.hpp>
#include <kompoz/display_list.hpp>
#include <kompoz/host.hpp>
#include <kompoz/preview.hpp>

#include "../gpu/renderer.hpp"
#include "font_system.hpp"
#include "image_loader.hpp"
#include "raster.hpp"
#ifdef KOMPOZ_GPU_VULKAN
#include <SDL3/SDL_vulkan.h>
#endif
#ifdef KOMPOZ_DESKTOP_ACCENT
#include "desktop_accent.hpp"
#endif
#ifdef KOMPOZ_WALLPAPER_COLORS
#include "wallpaper_colors.hpp"
#endif
#ifdef KOMPOZ_ATSPI
#include "atspi_bridge.hpp"
#endif
#ifdef KOMPOZ_UIA
#include "uia_bridge.hpp"
#endif
#if defined(KOMPOZ_ATSPI) || defined(KOMPOZ_UIA)
#define KOMPOZ_A11Y_BRIDGE 1  // the desktop's screen readers read the semantics tree
#endif

#ifdef SDL_PLATFORM_ANDROID
#include "android_ime.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <format>
#include <memory>
#include <numbers>
#include <functional>
#include <optional>
#include <sstream>
#include <string>
#include <unordered_map>
#include <vector>

#ifdef SDL_PLATFORM_EMSCRIPTEN
#include <emscripten.h>
// A touchpad pinch reaches the page as wheel events with ctrlKey set (no Ctrl key is down, so
// SDL's modifier state doesn't show it): remember it from the last wheel event.
EM_JS(void, kompoz_web_watch_wheel, (), {
    window.addEventListener('wheel', function(e) { Module.kompozWheelCtrl = e.ctrlKey; }, {capture: true, passive: true});
});
EM_JS(int, kompoz_web_wheel_ctrl, (), { return Module.kompozWheelCtrl ? 1 : 0; });
// The address's #fragment is a route's path: the screen shown is put there (a reload or a
// shared address opens it again), and one typed or followed there is opened (a deep link).
EM_JS(char*, kompoz_web_take_hash, (), {
    if (!Module.kompozHashWatched) {
        Module.kompozHashWatched = true;
        Module.kompozHash = location.hash;
        window.addEventListener('hashchange', function() { Module.kompozHash = location.hash; });
    }
    const h = Module.kompozHash;
    Module.kompozHash = null;
    return h && h.length > 1 ? stringToNewUTF8(h.substring(1)) : 0;
});
EM_JS(void, kompoz_web_set_hash, (const char* path), {
    const h = '#' + UTF8ToString(path);
    if (location.hash !== h) history.replaceState(null, "", h);
});
EM_JS_DEPS(kompoz_web_hash, "$stringToNewUTF8,$UTF8ToString");
// HapticFeedback: where the browser can vibrate (Chrome on Android; not iOS Safari).
EM_JS(void, kompoz_web_vibrate, (int millis), {
    if (navigator.vibrate) navigator.vibrate(millis);
});
#endif

namespace kompoz::sdl {
namespace {

SDL_FColor toF(Color c) { return {c.r, c.g, c.b, c.a}; }




// GraphicsContext on top of SDL_Renderer. Shapes are triangulated with a 1px alpha fringe, which
// gives anti-aliased edges on every SDL render backend (GPU or software).
class SdlCanvas final : public GraphicsContext {
public:
    SdlCanvas(SDL_Renderer* renderer, FontSystem& fonts) : renderer_(renderer), fonts_(fonts) {
        const char* name = SDL_GetRendererName(renderer);
        software_ = name && std::string_view(name) == "software";
    }

    float scale = 1;  // px per dp

    // The radii in pixels, each at most half the shorter side.
    CornerRadii pixelRadii(const CornerRadii& radii, float w, float h) const {
        float half = std::min(w, h) / 2;
        auto px = [&](float r) { return std::clamp(r * scale, 0.f, half); };
        return {px(radii.topLeft), px(radii.topRight), px(radii.bottomRight), px(radii.bottomLeft)};
    }

    void fillRoundRect(const Rect& rect, const CornerRadii& radii, Color color) override {
        color = filtered(color);
        if (color.a <= 0 || rect.width <= 0 || rect.height <= 0) return;
        float x = rect.x * scale - ox_, y = rect.y * scale - oy_, w = rect.width * scale, h = rect.height * scale;
        CornerRadii r = pixelRadii(radii, w, h);
        if (r.max() < 0.5f) {
            fillPixelRect(x, y, w, h, color);
            return;
        }
        SDL_FColor c = toF(color), clear{c.r, c.g, c.b, 0};
        contour(x, y, w, h, r);
        begin();
        int center = vertex({x + w / 2, y + h / 2}, c);
        int inner = ring(-0.5f, c), outer = ring(0.5f, clear);
        int n = int(points_.size());
        for (int i = 0; i < n; ++i) triangle(center, inner + i, inner + (i + 1) % n);
        strip(inner, outer);
        flush();
    }

    void strokeRoundRect(const Rect& rect, const CornerRadii& radii, float width, Color color) override {
        color = filtered(color);
        if (color.a <= 0 || width <= 0 || rect.width <= 0 || rect.height <= 0) return;
        float x = rect.x * scale - ox_, y = rect.y * scale - oy_, w = rect.width * scale, h = rect.height * scale;
        float sw = std::min(width * scale, std::min(w, h) / 2);
        CornerRadii r = pixelRadii(radii, w, h);
        if (r.max() < 0.5f) {
            fillPixelRect(x, y, w, sw, color);
            fillPixelRect(x, y + h - sw, w, sw, color);
            fillPixelRect(x, y + sw, sw, h - 2 * sw, color);
            fillPixelRect(x + w - sw, y + sw, sw, h - 2 * sw, color);
            return;
        }
        SDL_FColor c = toF(color), clear{c.r, c.g, c.b, 0};
        contour(x, y, w, h, {std::max(r.topLeft, sw), std::max(r.topRight, sw), std::max(r.bottomRight, sw), std::max(r.bottomLeft, sw)});
        begin();
        if (sw >= 1) {
            int a = ring(0.5f, clear), b = ring(-0.5f, c), d = ring(-(sw - 0.5f), c), e = ring(-(sw + 0.5f), clear);
            strip(a, b);
            strip(b, d);
            strip(d, e);
        } else {  // hairline: fade instead of covering a full pixel
            SDL_FColor mid{c.r, c.g, c.b, c.a * sw};
            int a = ring(0.5f, clear), b = ring(-sw / 2, mid), e = ring(-sw - 0.5f, clear);
            strip(a, b);
            strip(b, e);
        }
        flush();
    }

    void drawLine(Offset from, Offset to, float width, Color color) override {
        color = filtered(color);
        float x0 = from.x * scale - ox_, y0 = from.y * scale - oy_, x1 = to.x * scale - ox_, y1 = to.y * scale - oy_;
        float dx = x1 - x0, dy = y1 - y0, length = std::hypot(dx, dy);
        if (length < 1e-3f || color.a <= 0) return;
        dx /= length;
        dy /= length;
        float hw = width * scale / 2;
        x0 -= dx * hw, y0 -= dy * hw, x1 += dx * hw, y1 += dy * hw;  // square caps
        float nx = -dy, ny = dx;
        SDL_FColor c = toF(color), clear{c.r, c.g, c.b, 0};
        if (hw < 0.5f) c.a *= hw * 2;
        float inner = std::max(hw - 0.5f, 0.f), outer = hw + 0.5f;
        begin();
        auto rail = [&](float offset, SDL_FColor col) {
            int first = vertex({x0 + nx * offset, y0 + ny * offset}, col);
            vertex({x1 + nx * offset, y1 + ny * offset}, col);
            return first;
        };
        int r0 = rail(-outer, clear), r1 = rail(-inner, c), r2 = rail(inner, c), r3 = rail(outer, clear);
        for (auto [a, b] : {std::pair{r0, r1}, std::pair{r1, r2}, std::pair{r2, r3}}) {
            triangle(a, a + 1, b + 1);
            triangle(a, b + 1, b);
        }
        flush();
    }

    void drawText(std::string_view text, Offset baseline, const Font& font, Color color, bool rtl) override {
        color = filtered(color);
        if (color.a <= 0) return;
        glyphs_.clear();
        fonts_.layout(text, baseline.x * scale - ox_, baseline.y * scale - oy_, font, rtl, glyphs_);
        drawGlyphs(toF(color));
    }

    // A gradient across text: each glyph's corners take the brush's color where they are.
    void drawTextBrush(std::string_view text, Offset baseline, const Font& font, const Brush& unfiltered, const Rect& area, bool rtl) override {
        if (unfiltered.isSolid()) return drawText(text, baseline, font, unfiltered.stops().front().color, rtl);
        Brush brush = filtered(unfiltered);
        float ox = ox_, oy = oy_, s = scale;
        std::function<SDL_FColor(float, float)> shade = [&](float x, float y) { return toF(brush.colorAt({(x + ox) / s, (y + oy) / s}, area)); };
        glyphs_.clear();
        fonts_.layout(text, baseline.x * scale - ox_, baseline.y * scale - oy_, font, rtl, glyphs_);
        drawGlyphs(SDL_FColor{1, 1, 1, 1}, &shade);
    }

    void drawTextShadow(std::string_view text, Offset baseline, const Font& font, Color color, float blurRadius, bool rtl) override {
        color = filtered(color);
        if (color.a <= 0) return;
        int blur = std::max(0, int(std::lround(blurRadius * scale)));
        glyphs_.clear();
        fonts_.layout(text, baseline.x * scale - ox_, baseline.y * scale - oy_, font, rtl, glyphs_, true, blur);
        drawGlyphs(toF(color));
    }

    void drawArc(const Rect& oval, float start, float sweep, float width, Color color, bool roundCap) override {
        Color unfiltered = color;  // (the caps are filled by fillRoundRect, which filters)
        color = filtered(color);
        if (color.a <= 0 || width <= 0 || sweep == 0) return;
        float cx = (oval.x + oval.width / 2) * scale - ox_, cy = (oval.y + oval.height / 2) * scale - oy_;
        float r = std::min(oval.width, oval.height) / 2 * scale, hw = width * scale / 2;
        int segments = std::clamp(int(std::abs(sweep) / 360.f * r), 6, 256);
        SDL_FColor c = toF(color), clear{c.r, c.g, c.b, 0};
        if (hw < 0.5f) c.a *= hw * 2;
        float rails[4] = {r + hw + 0.5f, r + std::max(hw - 0.5f, 0.f), r - std::max(hw - 0.5f, 0.f), r - hw - 0.5f};
        begin();
        for (int i = 0; i <= segments; ++i) {
            float a = (start + sweep * float(i) / float(segments)) * std::numbers::pi_v<float> / 180.f;
            float dx = std::cos(a), dy = std::sin(a);
            for (int k = 0; k < 4; ++k) vertex({cx + dx * rails[k], cy + dy * rails[k]}, k == 0 || k == 3 ? clear : c);
        }
        for (int i = 0; i < segments; ++i)
            for (int k = 0; k < 3; ++k) {
                int a = i * 4 + k, b = (i + 1) * 4 + k;
                triangle(a, b, b + 1);
                triangle(a, b + 1, a + 1);
            }
        flush();
        if (roundCap) {
            for (float deg : {start, start + sweep}) {
                float a = deg * std::numbers::pi_v<float> / 180.f;
                float px = oval.x + oval.width / 2 + std::cos(a) * r / scale, py = oval.y + oval.height / 2 + std::sin(a) * r / scale;
                fillRoundRect({px - width / 2, py - width / 2, width, width}, width / 2, unfiltered);
            }
        }
    }

    void drawVector(const ImageVector& vector, const Rect& b, Color color) override {
        color = filtered(color);
        if (color.a <= 0 || b.width <= 0 || b.height <= 0) return;
        glyphs_.resize(1);
        if (fonts_.vectorQuad(vector, b.x * scale - ox_, b.y * scale - oy_, b.width * scale, b.height * scale, glyphs_[0])) drawGlyphs(toF(color));
    }

    void pushClip(const Rect& r) override {
        // Edges go to the nearest pixel: rounding outwards let a clipped list show a pixel row of
        // what scrolled past its edge (at fractional densities).
        int x0 = int(std::lround(r.x * scale - ox_)), y0 = int(std::lround(r.y * scale - oy_));
        int x1 = int(std::lround(r.right() * scale - ox_)), y1 = int(std::lround(r.bottom() * scale - oy_));
        SDL_Rect rect{x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
        if (!clips_.empty() && !SDL_GetRectIntersection(&rect, &clips_.back(), &rect)) rect = {0, 0, 0, 0};
        clips_.push_back(rect);
        SDL_SetRenderClipRect(renderer_, &rect);
    }

    void popClip() override {
        clips_.pop_back();
        SDL_SetRenderClipRect(renderer_, clips_.empty() ? nullptr : &clips_.back());
    }

    // Moves the pixel origin; the clips already set are in pixels and stay where they are.
    void pushTranslation(Offset by) override {
        translations_.push_back({ox_, oy_});
        ox_ -= by.x * scale;
        oy_ -= by.y * scale;
    }

    void popTranslation() override {
        if (translations_.empty()) return;
        ox_ = translations_.back().x;
        oy_ = translations_.back().y;
        translations_.pop_back();
    }

    // A transform that only moves is a translation. Any other draws into a layer over what can
    // show through it (the visible pixels mapped back), composited through the matrix.
    void pushTransform(const Matrix& matrix) override {
        const auto& v = matrix.values;
        bool moved = v[Matrix::ScaleX] == 1 && v[Matrix::SkewY] == 0 && v[Matrix::SkewX] == 0 && v[Matrix::ScaleY] == 1 &&
                     v[Matrix::Perspective0] == 0 && v[Matrix::Perspective1] == 0 && v[Matrix::Perspective2] == 1;
        transformLayers_.push_back(!moved);
        if (moved) return pushTranslation({v[Matrix::TranslateX], v[Matrix::TranslateY]});
        SDL_Rect visible = visibleRect();
        Rect seen{(float(visible.x) + ox_) / scale, (float(visible.y) + oy_) / scale, float(visible.w) / scale, float(visible.h) / scale};
        Matrix back = matrix;
        back.invert();
        // Where the visible corners come from (the visible area three times over if one of them
        // comes from behind the eye), no more than a layer can be.
        Rect bounds{seen.x - seen.width, seen.y - seen.height, seen.width * 3, seen.height * 3};
        bool front = true;
        for (int c = 0; c < 4; ++c) {
            float x = c & 1 ? seen.right() : seen.x, y = c & 2 ? seen.bottom() : seen.y;
            front = front && back(0, 3) * x + back(1, 3) * y + back(3, 3) > 0;
        }
        if (front) bounds = back.map(seen);
        float most = 4000 / scale;
        Offset middle = bounds.center();
        bounds = bounds.intersect({middle.x - most / 2, middle.y - most / 2, most, most});
        float pox = ox_, poy = oy_;
        pushLayer(bounds, CornerRadii(), 1, {});
        // The matrix in the parent's pixels: back to dp, through it, to pixels again.
        auto& m = layers_.back().matrix;
        float sx = scale, a = v[Matrix::ScaleX], b = v[Matrix::SkewX], c = v[Matrix::TranslateX], d = v[Matrix::SkewY], e = v[Matrix::ScaleY],
              f = v[Matrix::TranslateY], g = v[Matrix::Perspective0], h = v[Matrix::Perspective1], i = v[Matrix::Perspective2];
        // [s 0 -o; 0 s -o; 0 0 1] M [1/s 0 o/s; 0 1/s o/s; 0 0 1]
        float px = pox / sx, py = poy / sx;
        float r0[3] = {sx * a - pox * g, sx * b - pox * h, sx * c - pox * i}, r1[3] = {sx * d - poy * g, sx * e - poy * h, sx * f - poy * i},
              r2[3] = {g, h, i};
        for (int k = 0; k < 3; ++k) {
            const float* r = k == 0 ? r0 : k == 1 ? r1 : r2;
            m[size_t(k * 3)] = r[0] / sx, m[size_t(k * 3 + 1)] = r[1] / sx, m[size_t(k * 3 + 2)] = r[0] * px + r[1] * py + r[2];
        }
        layers_.back().matrixSet = true;
    }

    void popTransform() override {
        if (transformLayers_.empty()) return;
        bool layer = transformLayers_.back();
        transformLayers_.pop_back();
        if (layer) popLayer();
        else popTranslation();
    }

    // The clip, or else the whole target (the layer's texture or the window).
    SDL_Rect visibleRect() const {
        if (!clips_.empty()) return clips_.back();
        for (auto it = layers_.rbegin(); it != layers_.rend(); ++it)
            if (it->texture) return {0, 0, it->rect.w, it->rect.h};
        int w = 0, h = 0;
        SDL_GetCurrentRenderOutputSize(renderer_, &w, &h);
        return {0, 0, w, h};
    }

    bool quickReject(const Rect& r) const override {
        SDL_Rect visible = visibleRect();
        float x0 = r.x * scale - ox_, y0 = r.y * scale - oy_, x1 = r.right() * scale - ox_, y1 = r.bottom() * scale - oy_;
        return x1 <= float(visible.x) || y1 <= float(visible.y) || x0 >= float(visible.x + visible.w) ||
               y0 >= float(visible.y + visible.h);
    }

    // Shadows of rounded rects: an alpha mask, blurred on the CPU (three box blurs making Skia's
    // sigma for the radius) once for each size, corners, blur (and an inner one's hole), then
    // drawn tinted. Too big a mask falls back to the base class's rings.
    void drawShadow(const Rect& rect, const CornerRadii& radii, const Shadow& shadow, bool inner) override {
        drawFilteredShadow(rect, radii, shadow, inner);
    }
    void drawFilteredShadow(const Rect& rect, const CornerRadii& radii, const Shadow& original, bool inner) {
        Shadow shadow = original;
        shadow.color = filtered(shadow.color);
        if (shadow.color.a <= 0 || rect.width <= 0 || rect.height <= 0) return;
        float blur = std::max(0.f, shadow.blurRadius) * scale;
        float sigma = 0.57735f * blur + 0.5f;
        int pad = blur > 0 ? int(std::ceil(sigma * 3)) : 1;
        // Whole pixels: a soft edge doesn't show it, and a mask then serves wherever the shape is.
        float grow = inner ? -shadow.spread : shadow.spread;
        Rect shape{rect.x - grow, rect.y - grow, rect.width + 2 * grow, rect.height + 2 * grow};
        if (!inner) shape.x += shadow.offset.x, shape.y += shadow.offset.y;
        int x0 = int(std::lround((inner ? rect.x : shape.x) * scale - ox_)), y0 = int(std::lround((inner ? rect.y : shape.y) * scale - oy_));
        int w = int(std::lround((inner ? rect.width : shape.width) * scale)), h = int(std::lround((inner ? rect.height : shape.height) * scale));
        if (w <= 0 || h <= 0) return;
        int W = inner ? w : w + 2 * pad, H = inner ? h : h + 2 * pad;
        if (W > 4096 || H > 4096 || double(W) * H > 6e6) return GraphicsContext::drawShadow(rect, radii, original, inner);  // (filtered as it fills)
        CornerRadii r = radii.scaled(scale);
        // An inner shadow's hole, in the mask's pixels (from the rect's corner).
        float hx = (shape.x - rect.x + shadow.offset.x) * scale, hy = (shape.y - rect.y + shadow.offset.y) * scale;
        float hw = shape.width * scale, hh = shape.height * scale;
        std::string key;
        auto add = [&](float v) { key.append(reinterpret_cast<const char*>(&v), sizeof v); };
        for (float v : {float(w), float(h), r.topLeft, r.topRight, r.bottomRight, r.bottomLeft, blur, float(inner), grow * scale}) add(v);
        if (inner) for (float v : {std::round(hx), std::round(hy), std::round(hw), std::round(hh)}) add(v);
        auto it = shadowMasks_.find(key);
        if (it == shadowMasks_.end()) {
            SDL_Texture* t = shadowMask(w, h, W, H, pad, r, r.grown(grow * scale), blur > 0 ? sigma : 0, inner, std::round(hx), std::round(hy),
                                        std::round(hw), std::round(hh));
            it = shadowMasks_.emplace(std::move(key), ShadowMask{t}).first;
        }
        it->second.used = true;
        if (!it->second.texture) return;
        SDL_SetTextureColorModFloat(it->second.texture, shadow.color.r, shadow.color.g, shadow.color.b);
        SDL_SetTextureAlphaModFloat(it->second.texture, shadow.color.a);
        SDL_FRect d = inner ? SDL_FRect{float(x0), float(y0), float(W), float(H)}
                            : SDL_FRect{float(x0 - pad), float(y0 - pad), float(W), float(H)};
        SDL_RenderTexture(renderer_, it->second.texture, nullptr, &d);
    }

    // How much of a pixel at (x, y) a rounded rect from (0, 0), w x h, covers (its signed distance).
    static float roundRectCoverage(float x, float y, float w, float h, const CornerRadii& r) {
        float cx = w / 2, cy = h / 2, px = x - cx, py = y - cy;
        float rad = std::clamp(px < 0 ? (py < 0 ? r.topLeft : r.bottomLeft) : (py < 0 ? r.topRight : r.bottomRight), 0.f, std::min(cx, cy));
        float qx = std::abs(px) - (cx - rad), qy = std::abs(py) - (cy - rad);
        float d = std::min(std::max(qx, qy), 0.f) + std::hypot(std::max(qx, 0.f), std::max(qy, 0.f)) - rad;
        return std::clamp(0.5f - d, 0.f, 1.f);
    }

    // `r`: the rect's corners (an inner shadow is cut to them); `shaped`: the shadow's shape's
    // (spread out for a drop shadow, in for an inner one's hole at hx, hy, hw x hh).
    SDL_Texture* shadowMask(int w, int h, int W, int H, int pad, const CornerRadii& r, const CornerRadii& shaped, float sigma, bool inner,
                            float hx, float hy, float hw, float hh) {
        // Drawn on a canvas `pad` bigger all round, blurred, then (inner) cut to the rect.
        int cw = (inner ? w : W) + (inner ? 2 * pad : 0), ch = (inner ? h : H) + (inner ? 2 * pad : 0);
        std::vector<float> a(size_t(cw) * size_t(ch));
        for (int y = 0; y < ch; ++y)
            for (int x = 0; x < cw; ++x) {
                float px = float(x) + 0.5f - float(pad), py = float(y) + 0.5f - float(pad);
                a[size_t(y) * size_t(cw) + size_t(x)] = inner ? 1 - roundRectCoverage(px - hx, py - hy, hw, hh, shaped)
                                                              : roundRectCoverage(px, py, float(w), float(h), shaped);
            }
        if (sigma > 0) blurCoverage(a, cw, ch, sigma);
        std::vector<float> out(size_t(W) * size_t(H));
        for (int y = 0; y < H; ++y)
            for (int x = 0; x < W; ++x)
                out[size_t(y) * size_t(W) + size_t(x)] =
                    inner ? a[size_t(y + pad) * size_t(cw) + size_t(x + pad)] * roundRectCoverage(float(x) + 0.5f, float(y) + 0.5f, float(w), float(h), r)
                          : a[size_t(y) * size_t(cw) + size_t(x)];
        return coverageTexture(out, W, H);
    }


    // Coverage grown (or shrunk) by `r` px each way: the largest (smallest) value around.
    static void spreadCoverage(std::vector<float>& a, int w, int h, int r, bool grow) {
        if (r <= 0) return;
        std::vector<float> t(a.size());
        auto pick = [grow](float x, float y) { return grow ? std::max(x, y) : std::min(x, y); };
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float v = a[size_t(y) * size_t(w) + size_t(x)];
                for (int d = -r; d <= r; ++d) v = pick(v, a[size_t(y) * size_t(w) + size_t(std::clamp(x + d, 0, w - 1))]);
                t[size_t(y) * size_t(w) + size_t(x)] = v;
            }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                float v = t[size_t(y) * size_t(w) + size_t(x)];
                for (int d = -r; d <= r; ++d) v = pick(v, t[size_t(std::clamp(y + d, 0, h - 1)) * size_t(w) + size_t(x)]);
                a[size_t(y) * size_t(w) + size_t(x)] = v;
            }
    }

    // Coverage as a white texture with that alpha, to tint with a color mod.
    SDL_Texture* coverageTexture(const std::vector<float>& a, int W, int H) {
        std::vector<uint8_t> rgba(size_t(W) * size_t(H) * 4);
        for (size_t i = 0; i < a.size(); ++i) {
            rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = uint8_t(std::lround(std::clamp(a[i], 0.f, 1.f) * 255));
        }
        SDL_Texture* tex = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, W, H);
        if (!tex) return nullptr;
        SDL_UpdateTexture(tex, nullptr, rgba.data(), W * 4);
        SDL_SetTextureBlendMode(tex, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_LINEAR);
        return tex;
    }

    // An outline shape's shadow: its fill rasterized (moved by the offset for a drop shadow, for
    // an inner one's hole), spread by growing or shrinking the coverage, blurred, cached.
    void drawShadowPath(const Path& outline, Offset origin, const Shadow& shadow, bool inner) override {
        drawFilteredShadowPath(outline, origin, shadow, inner);
    }
    void drawFilteredShadowPath(const Path& outline, Offset origin, const Shadow& original, bool inner) {
        Shadow shadow = original;
        shadow.color = filtered(shadow.color);
        if (shadow.color.a <= 0 || outline.isEmpty()) return;
        Rect b = outline.bounds();
        float blur = std::max(0.f, shadow.blurRadius) * scale, sigma = 0.57735f * blur + 0.5f;
        int pad = blur > 0 ? int(std::ceil(sigma * 3)) : 1, spread = int(std::lround(std::abs(shadow.spread) * scale));
        int margin = pad + (inner ? 0 : spread) + 1;
        float sx = (origin.x + b.x + (inner ? 0 : shadow.offset.x)) * scale - ox_, sy = (origin.y + b.y + (inner ? 0 : shadow.offset.y)) * scale - oy_;
        float fx = std::round((sx - std::floor(sx)) * 4) / 4, fy = std::round((sy - std::floor(sy)) * 4) / 4;  // quarter pixels
        int x0 = int(std::floor(sx)) - margin, y0 = int(std::floor(sy)) - margin;
        int W = int(std::ceil(b.width * scale + fx)) + 2 * margin, H = int(std::ceil(b.height * scale + fy)) + 2 * margin;
        if (W > 4096 || H > 4096 || double(W) * H > 6e6) return GraphicsContext::drawShadowPath(outline, origin, original, inner);
        std::string key = "path";
        auto add = [&](float v) { key.append(reinterpret_cast<const char*>(&v), sizeof v); };
        uint64_t pk = pathKey(outline, PathStroke{});
        key.append(reinterpret_cast<const char*>(&pk), sizeof pk);
        for (float v : {fx, fy, blur, shadow.spread * scale, float(inner), inner ? shadow.offset.x * scale : 0.f, inner ? shadow.offset.y * scale : 0.f, scale})
            add(v);
        auto it = shadowMasks_.find(key);
        if (it == shadowMasks_.end()) {
            auto at = [&](float dx, float dy) {
                return [&, dx, dy](float x, float y) { return SDL_FPoint{(x - b.x) * scale + fx + float(margin) + dx, (y - b.y) * scale + fy + float(margin) + dy}; };
            };
            SDL_Texture* t = nullptr;
            std::vector<float> a = fillCoverage(outline, at(0, 0), W, H);
            if (!a.empty() && !inner) {
                spreadCoverage(a, W, H, spread, shadow.spread > 0);
                if (blur > 0) blurCoverage(a, W, H, sigma);
                t = coverageTexture(a, W, H);
            } else if (!a.empty()) {
                // Where a hole (the shape moved by the offset, shrunk by the spread) isn't, blurred, inside the shape.
                std::vector<float> hole = fillCoverage(outline, at(shadow.offset.x * scale, shadow.offset.y * scale), W, H);
                if (hole.empty()) hole.assign(a.size(), 0.f);
                spreadCoverage(hole, W, H, spread, shadow.spread < 0);
                for (float& v : hole) v = 1 - v;
                if (blur > 0) blurCoverage(hole, W, H, sigma);
                for (size_t i = 0; i < a.size(); ++i) a[i] *= hole[i];
                t = coverageTexture(a, W, H);
            }
            it = shadowMasks_.emplace(std::move(key), ShadowMask{t}).first;
        }
        it->second.used = true;
        if (!it->second.texture) return;
        SDL_SetTextureColorModFloat(it->second.texture, shadow.color.r, shadow.color.g, shadow.color.b);
        SDL_SetTextureAlphaModFloat(it->second.texture, shadow.color.a);
        SDL_FRect d{float(x0), float(y0), float(W), float(H)};
        SDL_RenderTexture(renderer_, it->second.texture, nullptr, &d);
    }

    void drawImage(const ImageBitmap& unfiltered, const Rect& src, const Rect& dst, float alpha) override {
        ImageBitmap image = unfiltered;
        for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) image = it->apply(image);
        SDL_Texture* texture = textureFor(image);
        if (!texture || alpha <= 0) return;
        SDL_FRect s{src.x, src.y, src.width, src.height};
        SDL_FRect d{dst.x * scale - ox_, dst.y * scale - oy_, dst.width * scale, dst.height * scale};
        SDL_SetTextureAlphaModFloat(texture, alpha);
        SDL_RenderTexture(renderer_, texture, &s, &d);
    }

    // Layers render into a target texture. Drawing with normal alpha blending onto a cleared
    // target leaves premultiplied colors, so the layer is composited premultiplied.
    int layerCount = 0;  // pushed this frame (KOMPOZ_PROFILE)
    void pushLayer(const Rect& bounds, const CornerRadii& radii, float alpha, const LayerTransform& transform) override {
        ++layerCount;
        int x0 = int(std::floor(bounds.x * scale - ox_)), y0 = int(std::floor(bounds.y * scale - oy_));
        int x1 = int(std::ceil(bounds.right() * scale - ox_)), y1 = int(std::ceil(bounds.bottom() * scale - oy_));
        Layer layer{nullptr, SDL_GetRenderTarget(renderer_), ox_, oy_, {},
                    SDL_Rect{x0, y0, std::max(1, x1 - x0), std::max(1, y1 - y0)},
                    SDL_FRect{bounds.x * scale - ox_, bounds.y * scale - oy_, bounds.width * scale, bounds.height * scale},
                    radii.scaled(scale), alpha, transform};
        if (layer.rect.w <= 4096 && layer.rect.h <= 4096) layer.texture = acquire(layer.rect.w, layer.rect.h, &layer.texW, &layer.texH);
        // Too big or no render target support: draw directly (no rounding, alpha or transform),
        // keeping the clips that are in effect.
        if (layer.texture) {
            layer.clips = std::move(clips_);  // the layer starts unclipped; these come back on pop
            clips_.clear();
            SDL_SetRenderTarget(renderer_, layer.texture);
            SDL_SetRenderClipRect(renderer_, nullptr);
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
            SDL_RenderClear(renderer_);
            ox_ += float(x0);
            oy_ += float(y0);
        }
        layers_.push_back(std::move(layer));
    }

    // Clipped to an outline: its polygon (curves flattened) in the parent's pixels, composited
    // through a fan from the middle as rounded layers are.
    void pushOutlineLayer(const Rect& bounds, const Path& outline, float alpha, const LayerTransform& transform) override {
        pushLayer(bounds, CornerRadii(), alpha, transform);
        Layer& layer = layers_.back();
        if (!layer.texture) return;  // drawn directly: no clip
        float x0 = layer.shape.x, y0 = layer.shape.y;
        auto at = [&](float x, float y) { return SDL_FPoint{x0 + x * scale, y0 + y * scale}; };
        SDL_FPoint last{};
        for (const PathCommand& c : outline.commands()) {
            switch (c.verb) {
            case PathCommand::Verb::Move:
            case PathCommand::Verb::Line: layer.outline.push_back(at(c.x, c.y)); break;
            case PathCommand::Verb::Quad:
            case PathCommand::Verb::Cubic:
                for (int i = 1; i <= 8; ++i) {
                    float t = float(i) / 8, u = 1 - t;
                    float x = c.verb == PathCommand::Verb::Quad ? u * u * last.x + 2 * u * t * c.x1 + t * t * c.x
                                                                : u * u * u * last.x + 3 * u * u * t * c.x1 + 3 * u * t * t * c.x2 + t * t * t * c.x;
                    float y = c.verb == PathCommand::Verb::Quad ? u * u * last.y + 2 * u * t * c.y1 + t * t * c.y
                                                                : u * u * u * last.y + 3 * u * u * t * c.y1 + 3 * u * t * t * c.y2 + t * t * t * c.y;
                    layer.outline.push_back(at(x, y));
                }
                break;
            case PathCommand::Verb::Close: break;
            }
            if (c.verb != PathCommand::Verb::Close) last = {c.x, c.y};
        }
        if (layer.outline.size() < 3) layer.outline.clear();
    }

    // The contour of an outline layer: its points with normals pointing out, whichever way round.
    void outlineContour(const std::vector<SDL_FPoint>& outline) {
        points_ = outline;
        normals_.clear();
        size_t n = points_.size();
        float area = 0;
        for (size_t i = 0; i < n; ++i) {
            const SDL_FPoint &a = points_[i], &b = points_[(i + 1) % n];
            area += a.x * b.y - b.x * a.y;
        }
        float way = area >= 0 ? 1.f : -1.f;  // clockwise on screen: out is to the left of travel
        auto edgeNormal = [&](size_t i) {
            const SDL_FPoint &a = points_[i], &b = points_[(i + 1) % n];
            float dx = b.x - a.x, dy = b.y - a.y, len = std::hypot(dx, dy);
            return len > 0 ? SDL_FPoint{way * dy / len, -way * dx / len} : SDL_FPoint{0, 0};
        };
        for (size_t i = 0; i < n; ++i) {
            SDL_FPoint p = edgeNormal((i + n - 1) % n), q = edgeNormal(i);
            float x = p.x + q.x, y = p.y + q.y, len = std::hypot(x, y);
            normals_.push_back(len > 1e-4f ? SDL_FPoint{x / len, y / len} : q);
        }
    }

    // Color filters: applied here, to each color, brush and image drawn (the innermost first).
    void pushColorFilter(const ColorFilter& filter) override { filters_.push_back(filter); }
    void popColorFilter() override {
        if (!filters_.empty()) filters_.pop_back();
    }
    Color filtered(Color c) const {
        for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) c = it->apply(c);
        return c;
    }
    Brush filtered(const Brush& brush) const {
        Brush b = brush;
        for (auto it = filters_.rbegin(); it != filters_.rend(); ++it) b = it->apply(b);
        return b;
    }

    // Blend modes, on what the renderer draws next (the colors drawn aren't premultiplied, layers
    // are). Where the renderer can't, or for a mode left out, it stays SrcOver.
    static SDL_BlendMode sdlBlend(BlendMode mode) {
        auto custom = [](SDL_BlendFactor src, SDL_BlendFactor dst, SDL_BlendFactor srcA, SDL_BlendFactor dstA) {
            return SDL_ComposeCustomBlendMode(src, dst, SDL_BLENDOPERATION_ADD, srcA, dstA, SDL_BLENDOPERATION_ADD);
        };
        switch (mode) {
        case BlendMode::DstIn: return custom(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_SRC_ALPHA);
        case BlendMode::DstOut:
            return custom(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA);
        case BlendMode::Clear: return custom(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ZERO);
        case BlendMode::Src: return custom(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ZERO);
        case BlendMode::Dst: return custom(SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ZERO, SDL_BLENDFACTOR_ONE);
        case BlendMode::Plus: return custom(SDL_BLENDFACTOR_SRC_ALPHA, SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE);
        default: return SDL_BLENDMODE_BLEND;
        }
    }
    void applyBlend() {
        blend_ = blends_.empty() ? SDL_BLENDMODE_BLEND : sdlBlend(blends_.back());
        if (!SDL_SetRenderDrawBlendMode(renderer_, blend_)) {
            blend_ = SDL_BLENDMODE_BLEND;
            SDL_SetRenderDrawBlendMode(renderer_, blend_);
        }
    }
    void pushBlendMode(BlendMode mode) override {
        blends_.push_back(mode);
        applyBlend();
    }
    void popBlendMode() override {
        if (!blends_.empty()) blends_.pop_back();
        applyBlend();
    }

    void pushBlurLayer(const Rect& bounds, float radiusX, float radiusY) override {
        pushLayer(bounds, CornerRadii(), 1, {});
        layers_.back().blurX = radiusX * scale;
        layers_.back().blurY = radiusY * scale;
    }

    // Blurs a layer's texture in place: scaled down by halves (each a 2x2 average, by linear
    // filtering) about as many times as the radius is pixels wide in powers of two, then back
    // up a level at a time (bilinear) — smooth and cheap on the GPU, close to a Gaussian.
    void blurLayer(SDL_Texture* texture, int width, int height, float blurX, float blurY) {
        auto levels = [](float r) { return r < 1 ? 0 : std::clamp(int(std::lround(std::log2(r))), 1, 7); };
        int lx = levels(blurX), ly = levels(blurY), n = std::max(lx, ly);
        if (n == 0 || software_) return;
        struct Level {
            SDL_Texture* texture;
            int w, h;
        };
        std::vector<Level> chain{{texture, width, height}};
        SDL_SetRenderClipRect(renderer_, nullptr);
        auto copy = [&](const Level& from, const Level& to) {
            SDL_SetRenderTarget(renderer_, to.texture);
            SDL_SetRenderDrawColor(renderer_, 0, 0, 0, 0);
            SDL_RenderClear(renderer_);
            SDL_SetTextureBlendMode(from.texture, SDL_BLENDMODE_NONE);  // premultiplied pixels as they are
            SDL_FRect src{0, 0, float(from.w), float(from.h)}, dst{0, 0, float(to.w), float(to.h)};
            SDL_RenderTexture(renderer_, from.texture, &src, &dst);
        };
        for (int k = 1; k <= n; ++k) {
            int w = std::max(1, width >> std::min(k, lx)), h = std::max(1, height >> std::min(k, ly));
            int tw = 0, th = 0;
            SDL_Texture* t = acquire(w, h, &tw, &th);
            if (!t) break;
            Level next{t, w, h};
            copy(chain.back(), next);
            chain.push_back(next);
        }
        for (size_t k = chain.size() - 1; k > 0; --k) copy(chain[k], chain[k - 1]);  // back up, into the layer's own at last
        for (size_t k = 1; k < chain.size(); ++k) release(chain[k].texture);
    }

    void popLayer() override {
        Layer layer = std::move(layers_.back());
        layers_.pop_back();
        if (!layer.texture) return;  // drawn directly: the clips never changed
        if (layer.blurX > 0 || layer.blurY > 0) blurLayer(layer.texture, layer.rect.w, layer.rect.h, layer.blurX, layer.blurY);
        SDL_SetRenderTarget(renderer_, layer.previous);
        ox_ = layer.ox;
        oy_ = layer.oy;
        clips_ = std::move(layer.clips);
        SDL_SetRenderClipRect(renderer_, clips_.empty() ? nullptr : &clips_.back());

        SDL_Texture* t = layer.texture;
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND_PREMULTIPLIED);
        float x = float(layer.rect.x), y = float(layer.rect.y), w = float(layer.rect.w), h = float(layer.rect.h);
        float a = layer.alpha;
        // The outline follows the exact bounds: a texture grown to whole pixels would put a
        // circle at half a pixel off round (flat on one side).
        const SDL_FRect& b = layer.shape;
        float half = std::min(b.w, b.h) / 2;
        auto clamped = [half](float r) { return std::clamp(r, 0.f, half); };
        CornerRadii r{clamped(layer.radii.topLeft), clamped(layer.radii.topRight), clamped(layer.radii.bottomRight),
                      clamped(layer.radii.bottomLeft)};
        const LayerTransform& tf = layer.transform;
        bool outlined = !layer.outline.empty();
        const auto& mx = layer.matrix;
        if (layer.matrixSet) {
            // Through pushTransform's matrix: a grid of cells small enough for SDL's software
            // triangles (which leave out big ones) and for the texture to follow a perspective
            // (SDL interpolates texture coordinates straight across each triangle). Its edges lie
            // past what can show: no anti-aliasing for them.
            int cells = std::clamp(int(std::ceil(std::max(b.w, b.h) / 128)), 1, 64);
            if (software_) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            begin();
            float tw = float(layer.texW), th = float(layer.texH);
            SDL_FColor c{a, a, a, a};
            for (int j = 0; j <= cells; ++j)
                for (int i = 0; i <= cells; ++i) {
                    float px = b.x + b.w * float(i) / float(cells), py = b.y + b.h * float(j) / float(cells);
                    float depth = mx[6] * px + mx[7] * py + mx[8];
                    float k = depth > 1e-4f ? 1 / depth : 1e4f;
                    vertices_.push_back({{(mx[0] * px + mx[1] * py + mx[2]) * k, (mx[3] * px + mx[4] * py + mx[5]) * k}, c,
                                         {std::clamp((px - x) / tw, 0.f, w / tw), std::clamp((py - y) / th, 0.f, h / th)}});
                }
            for (int j = 0; j < cells; ++j)
                for (int i = 0; i < cells; ++i) {
                    int q = j * (cells + 1) + i;
                    triangle(q, q + 1, q + cells + 2);
                    triangle(q, q + cells + 2, q + cells + 1);
                }
            SDL_RenderGeometry(renderer_, t, vertices_.data(), int(vertices_.size()), indices_.data(), int(indices_.size()));
        } else if (!outlined && r.max() < 0.5f && tf.rotationZ == 0) {
            // Square and not turned: one quad, scaled about the origin and moved (the fan below
            // showed its seams through anything see-through in a scaled layer — a ripple in a
            // pulsing button).
            float px = b.x + b.w * tf.origin.x, py = b.y + b.h * tf.origin.y;
            SDL_FRect src{0, 0, w, h},
                d{px + (x - px) * tf.scaleX + tf.translationX * scale, py + (y - py) * tf.scaleY + tf.translationY * scale, w * tf.scaleX,
                  h * tf.scaleY};
            SDL_SetTextureColorModFloat(t, a, a, a);
            SDL_SetTextureAlphaModFloat(t, a);
            SDL_RenderTexture(renderer_, t, &src, &d);
            SDL_SetTextureColorModFloat(t, 1, 1, 1);
            SDL_SetTextureAlphaModFloat(t, 1);
        } else {  // rounded and/or transformed: textured fan with an anti-aliased fringe
            // SDL's software triangle blitter ignores premultiplied blending (it would copy
            // transparent pixels); plain blending only darkens the anti-aliased edges a little.
            if (software_) SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            SDL_FColor c{a, a, a, a}, clear{0, 0, 0, 0};
            if (outlined)
                outlineContour(layer.outline);
            else
                contour(b.x, b.y, b.w, b.h, r);
            begin();
            // Clamped: the fringe lies half a pixel outside, and UVs beyond [0, 1] make SDL wrap.
            float tw = float(layer.texW), th = float(layer.texH);
            auto uv = [&](SDL_FPoint p) {
                return SDL_FPoint{std::clamp((p.x - x) / tw, 0.f, w / tw), std::clamp((p.y - y) / th, 0.f, h / th)};
            };
            float px0 = b.x + b.w * tf.origin.x, py0 = b.y + b.h * tf.origin.y;
            float rad = tf.rotationZ * std::numbers::pi_v<float> / 180.f, cs = std::cos(rad), sn = std::sin(rad);
            auto place = [&](SDL_FPoint p) {  // scale and rotate around the origin, then translate
                float dx = (p.x - px0) * tf.scaleX, dy = (p.y - py0) * tf.scaleY;
                return SDL_FPoint{px0 + dx * cs - dy * sn + tf.translationX * scale,
                                  py0 + dx * sn + dy * cs + tf.translationY * scale};
            };
            auto add = [&](SDL_FPoint p, SDL_FColor col) {
                vertices_.push_back({place(p), col, uv(p)});
                return int(vertices_.size()) - 1;
            };
            int center = add({b.x + b.w / 2, b.y + b.h / 2}, c);
            int n = int(points_.size());
            int inner = int(vertices_.size());
            for (int i = 0; i < n; ++i) add({points_[i].x - normals_[i].x * 0.5f, points_[i].y - normals_[i].y * 0.5f}, c);
            int outer = int(vertices_.size());
            for (int i = 0; i < n; ++i) add({points_[i].x + normals_[i].x * 0.5f, points_[i].y + normals_[i].y * 0.5f}, clear);
            for (int i = 0; i < n; ++i) triangle(center, inner + i, inner + (i + 1) % n);
            strip(inner, outer);
            SDL_RenderGeometry(renderer_, t, vertices_.data(), int(vertices_.size()), indices_.data(), int(indices_.size()));
        }
        release(t);
    }

    void fillGradient(const Rect& rect, const CornerRadii& radii, const Brush& unfiltered, const Rect& area) override {
        if (unfiltered.isSolid()) return fillRoundRect(rect, radii, unfiltered.stops().front().color);
        Brush brush = filtered(unfiltered);
        if (rect.width <= 0 || rect.height <= 0) return;
        float x = rect.x * scale - ox_, y = rect.y * scale - oy_, w = rect.width * scale, h = rect.height * scale;
        CornerRadii r = pixelRadii(radii, w, h);
        // The mesh only depends on the size and where the brush's area is relative to the rect:
        // built once, then moved along (a scrolling page doesn't color thousands of vertices
        // every frame).
        Rect areaRel{area.x - rect.x, area.y - rect.y, area.width, area.height};
        // A gradient that tiles (Repeated, Mirror, Decal) is drawn from a texture its UVs run
        // across: the mesh's interpolated colors would blur where it starts over.
        bool tiled = brush.tileMode() != TileMode::Clamp && brush.kind() != Brush::Kind::Sweep;
        SDL_Texture* texture = tiled ? gradientTexture(brush) : nullptr;
        auto render = [&](const std::vector<int>& indices) {
            if (texture) {
                SDL_TextureAddressMode mode = brush.tileMode() == TileMode::Decal ? SDL_TEXTURE_ADDRESS_CLAMP : SDL_TEXTURE_ADDRESS_WRAP;
                SDL_SetRenderTextureAddressMode(renderer_, mode, SDL_TEXTURE_ADDRESS_CLAMP);
            }
            SDL_RenderGeometry(renderer_, texture, vertices_.data(), int(vertices_.size()), indices.data(), int(indices.size()));
            if (texture) SDL_SetRenderTextureAddressMode(renderer_, SDL_TEXTURE_ADDRESS_AUTO, SDL_TEXTURE_ADDRESS_AUTO);
        };
        for (GradientMesh& g : gradients_)
            if (g.w == w && g.h == h && g.r == r && g.scale == scale && g.area.x == areaRel.x && g.area.y == areaRel.y &&
                g.area.width == areaRel.width && g.area.height == areaRel.height && g.brush == brush) {
                g.used = true;
                vertices_ = g.vertices;
                for (SDL_Vertex& v : vertices_) v.position.x += x, v.position.y += y;
                render(g.indices);
                return;
            }
        auto colorAt = [&](SDL_FPoint p) { return toF(brush.colorAt({(p.x + ox_) / scale, (p.y + oy_) / scale}, area)); };
        // The gradient is interpolated between vertices: a contour split every 12 px and rings
        // towards the center keep it close to the exact one.
        contour(x, y, w, h, r);
        densify(12);
        begin();
        SDL_FPoint c{x + w / 2, y + h / 2};
        int center = vertex(c, colorAt(c));
        int n = int(points_.size()), rings = std::clamp(int(std::max(w, h) / 24), 2, 24);
        std::vector<int> first;
        for (int k = 1; k <= rings; ++k) {
            float t = float(k) / float(rings);
            first.push_back(int(vertices_.size()));
            for (int i = 0; i < n; ++i) {
                SDL_FPoint edge{points_[i].x - normals_[i].x * 0.5f, points_[i].y - normals_[i].y * 0.5f};
                SDL_FPoint p{c.x + (edge.x - c.x) * t, c.y + (edge.y - c.y) * t};
                vertex(p, colorAt(p));
            }
        }
        int outer = int(vertices_.size());
        for (int i = 0; i < n; ++i) {
            SDL_FPoint p{points_[i].x + normals_[i].x * 0.5f, points_[i].y + normals_[i].y * 0.5f};
            SDL_FColor col = colorAt(p);
            col.a = 0;
            vertex(p, col);
        }
        for (int i = 0; i < n; ++i) {
            int a = first[0] + i, b = first[0] + (i + 1) % n, apex = center;
            if (brush.kind() == Brush::Kind::Sweep) {  // the center has no single color: one per wedge
                SDL_FPoint mid{(vertices_[size_t(a)].position.x + vertices_[size_t(b)].position.x) / 2,
                               (vertices_[size_t(a)].position.y + vertices_[size_t(b)].position.y) / 2};
                apex = vertex(c, colorAt({c.x + (mid.x - c.x) * 0.01f, c.y + (mid.y - c.y) * 0.01f}));
            }
            triangle(apex, a, b);
        }
        for (int k = 0; k + 1 < rings; ++k) strip(first[size_t(k)], first[size_t(k) + 1]);
        strip(first.back(), outer);
        if (texture)  // white (the edge's fade kept), the texture across: where each vertex is along the gradient
            for (SDL_Vertex& v : vertices_) {
                float t = brush.parameterAt({(v.position.x + ox_) / scale, (v.position.y + oy_) / scale}, area);
                v.tex_coord = {gradientU(brush, t), 0.5f};
                v.color = {1, 1, 1, v.color.a > 0 ? 1.f : 0.f};
            }
        GradientMesh mesh{brush, w, h, r, scale, areaRel, vertices_, indices_};
        for (SDL_Vertex& v : mesh.vertices) v.position.x -= x, v.position.y -= y;
        gradients_.push_back(std::move(mesh));
        render(indices_);
    }

    // A tiling gradient's colors in 256 texels: one period (Repeated), two with the second
    // turned back (Mirror), or one between two transparent texels (Decal).
    SDL_Texture* gradientTexture(const Brush& brush) {
        for (GradientTexture& g : gradientTextures_)
            if (g.brush == brush) {
                g.used = true;
                return g.texture;
            }
        constexpr int kTexels = 256;
        std::array<uint8_t, kTexels * 4> pixels{};
        for (int i = 0; i < kTexels; ++i) {
            Color c = colors::Transparent;
            switch (brush.tileMode()) {
            case TileMode::Repeated: c = brush.colorAtParameter((float(i) + 0.5f) / kTexels); break;
            case TileMode::Mirror: c = brush.colorAtParameter((float(i) + 0.5f) / (kTexels / 2)); break;
            case TileMode::Decal:
                if (i > 0 && i < kTexels - 1) c = brush.colorAtParameter((float(i - 1) + 0.5f) / (kTexels - 2));
                break;
            case TileMode::Clamp: c = brush.colorAtParameter((float(i) + 0.5f) / kTexels); break;
            }
            auto byte = [](float v) { return uint8_t(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
            pixels[size_t(i) * 4] = byte(c.r), pixels[size_t(i) * 4 + 1] = byte(c.g), pixels[size_t(i) * 4 + 2] = byte(c.b),
                               pixels[size_t(i) * 4 + 3] = byte(c.a);
        }
        SDL_Texture* texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kTexels, 1);
        if (!texture) return nullptr;
        SDL_UpdateTexture(texture, nullptr, pixels.data(), kTexels * 4);
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_LINEAR);
        gradientTextures_.push_back({brush, texture});
        return texture;
    }
    // Where `t` along a tiling gradient is across its texture.
    static float gradientU(const Brush& brush, float t) {
        switch (brush.tileMode()) {
        case TileMode::Mirror: return t / 2;
        case TileMode::Decal: return (1 + t * 254) / 256;
        default: return t;
        }
    }

    void drawPath(const Path& path, Offset origin, const Brush& unfiltered, const Rect& area, const PathStroke& stroke) override {
        if (path.isEmpty()) return;
        Brush brush = filtered(unfiltered);
        // Rasterized relative to a whole pixel, with the fraction (in quarter pixels) part of the
        // cache key: moving a path by whole pixels doesn't rasterize it again. One that moves by
        // fractions (scrolling at 2.625 px per dp) is drawn snapped to whole pixels until it
        // stays put, instead of being rasterized again every frame.
        float ax = origin.x * scale - ox_, ay = origin.y * scale - oy_;
        float bx = std::floor(ax), by = std::floor(ay);
        float fx = std::round((ax - bx) * 4) / 4, fy = std::round((ay - by) * 4) / 4;
        PathMotion& motion = pathMotion_[pathKey(path, stroke)];
        motion.idleFrames = 0;
        motion.stableFrames = motion.fx == fx && motion.fy == fy ? motion.stableFrames + 1 : 0;
        motion.fx = fx, motion.fy = fy;
        if (motion.stableFrames < 8) {
            bx = std::round(ax), by = std::round(ay);
            fx = fy = 0;
        }
        const PathMask* m = pathMask(path, fx, fy, stroke);
        if (!m || !m->texture) return;
        float x0 = bx + float(m->left), y0 = by + float(m->top), x1 = x0 + float(m->width), y1 = y0 + float(m->height);
        begin();
        if (brush.isSolid()) {
            SDL_FColor c = toF(brush.stops().front().color);
            vertices_ = {{{x0, y0}, c, {0, 0}}, {{x1, y0}, c, {1, 0}}, {{x1, y1}, c, {1, 1}}, {{x0, y1}, c, {0, 1}}};
            indices_ = {0, 1, 2, 0, 2, 3};
        } else {  // a grid colored by the gradient, textured with the coverage
            int cols = std::clamp(int(std::ceil(float(m->width) / 16)), 1, 64);
            int rows = std::clamp(int(std::ceil(float(m->height) / 16)), 1, 64);
            for (int r = 0; r <= rows; ++r)
                for (int c = 0; c <= cols; ++c) {
                    float u = float(c) / float(cols), v = float(r) / float(rows);
                    SDL_FPoint p{x0 + (x1 - x0) * u, y0 + (y1 - y0) * v};
                    vertices_.push_back({p, toF(brush.colorAt({(p.x + ox_) / scale, (p.y + oy_) / scale}, area)), {u, v}});
                }
            for (int r = 0; r < rows; ++r)
                for (int c = 0; c < cols; ++c) {
                    int a = r * (cols + 1) + c, b = a + cols + 1;
                    indices_.insert(indices_.end(), {a, a + 1, b + 1, a, b + 1, b});
                }
        }
        if (blend_ != SDL_BLENDMODE_BLEND) SDL_SetTextureBlendMode(m->texture, blend_);  // the mask's coverage is its alpha
        SDL_RenderGeometry(renderer_, m->texture, vertices_.data(), int(vertices_.size()), indices_.data(),
                           int(indices_.size()));
        if (blend_ != SDL_BLENDMODE_BLEND) SDL_SetTextureBlendMode(m->texture, SDL_BLENDMODE_BLEND);
    }

    // After each frame: free textures of images that are gone and layer targets not used.
    void endFrame() {
        for (auto it = images_.begin(); it != images_.end();) {
            if (it->second.token.expired()) {
                SDL_DestroyTexture(it->second.texture);
                it = images_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = pathMasks_.begin(); it != pathMasks_.end();) {  // paths not drawn for a second
            it->second.idleFrames = it->second.used ? 0 : it->second.idleFrames + 1;
            it->second.used = false;
            if (it->second.idleFrames > 60) {
                if (it->second.texture) SDL_DestroyTexture(it->second.texture);
                it = pathMasks_.erase(it);
            } else {
                ++it;
            }
        }
        for (auto it = shadowMasks_.begin(); it != shadowMasks_.end();) {  // shadows not drawn for a second
            it->second.idleFrames = it->second.used ? 0 : it->second.idleFrames + 1;
            it->second.used = false;
            if (it->second.idleFrames > 60) {
                if (it->second.texture) SDL_DestroyTexture(it->second.texture);
                it = shadowMasks_.erase(it);
            } else {
                ++it;
            }
        }
        std::erase_if(pathMotion_, [](auto& entry) { return ++entry.second.idleFrames > 60; });
        std::erase_if(gradients_, [](GradientMesh& g) {
            g.idleFrames = g.used ? 0 : g.idleFrames + 1;
            g.used = false;
            return g.idleFrames > 60;
        });
        std::erase_if(gradientTextures_, [](GradientTexture& g) {
            g.idleFrames = g.used ? 0 : g.idleFrames + 1;
            g.used = false;
            if (g.idleFrames <= 60) return false;
            SDL_DestroyTexture(g.texture);
            return true;
        });
        std::erase_if(pool_, [](PooledTexture& p) {  // kept for a second unused: layers scroll back in
            p.idleFrames = p.usedThisFrame ? 0 : p.idleFrames + 1;
            p.usedThisFrame = false;
            bool stale = p.idleFrames > 60;
            if (stale) SDL_DestroyTexture(p.texture);
            return stale;
        });
    }

    // GPU context lost: every texture is invalid.
    void reset() {
        for (SDL_Texture*& t : pages_)
            if (t) SDL_DestroyTexture(std::exchange(t, nullptr));
        for (auto& [id, image] : images_) SDL_DestroyTexture(image.texture);
        images_.clear();
        for (auto& p : pool_) SDL_DestroyTexture(p.texture);
        pool_.clear();
        for (auto& [key, mask] : pathMasks_)
            if (mask.texture) SDL_DestroyTexture(mask.texture);
        pathMasks_.clear();
        for (auto& [key, mask] : shadowMasks_)
            if (mask.texture) SDL_DestroyTexture(mask.texture);
        shadowMasks_.clear();
        for (auto& g : gradientTextures_) SDL_DestroyTexture(g.texture);
        gradientTextures_.clear();
        gradients_.clear();  // their UVs went with them
    }

    ~SdlCanvas() override { reset(); }

private:
    struct Layer {
        SDL_Texture* texture;
        SDL_Texture* previous;
        float ox, oy;
        std::vector<SDL_Rect> clips;
        SDL_Rect rect;    // in the parent's pixels, whole ones (the part of the texture it uses)
        SDL_FRect shape;  // the exact bounds there, which the rounding follows
        CornerRadii radii;  // px
        float alpha;
        LayerTransform transform;
        int texW = 0, texH = 0;  // the pooled texture can be a little bigger than rect
        std::vector<SDL_FPoint> outline;  // an outline layer's, in the parent's pixels (else rounded)
        float blurX = 0, blurY = 0;       // px: blurred as it is composited
        std::array<float, 9> matrix{};    // pushTransform's, in the parent's pixels (row by row), if matrixSet
        bool matrixSet = false;
    };
    std::vector<bool> transformLayers_;  // each pushTransform: drawn through a layer (else a translation)
    struct ImageTexture {
        SDL_Texture* texture;
        std::weak_ptr<const void> token;
    };
    // A rasterized path: white with coverage in alpha, tinted (or gradient-colored) per vertex.
    struct PathMask {
        SDL_Texture* texture = nullptr;
        int left = 0, top = 0, width = 0, height = 0;  // px, relative to the path's pixel origin
        bool used = true;
        int idleFrames = 0;
    };
    std::unordered_map<uint64_t, PathMask> pathMasks_;
    struct ShadowMask {
        SDL_Texture* texture = nullptr;
        bool used = true;
        int idleFrames = 0;
    };
    std::unordered_map<std::string, ShadowMask> shadowMasks_;
    // Where each path was last drawn within a pixel (quarter pixels), and for how many frames.
    struct PathMotion {
        float fx = -1, fy = -1;
        int stableFrames = 0, idleFrames = 0;
    };
    std::unordered_map<uint64_t, PathMotion> pathMotion_;

    struct PooledTexture {
        SDL_Texture* texture;
        int w, h;
        bool inUse = false, usedThisFrame = false;
        int idleFrames = 0;
    };

    SDL_Texture* textureFor(const ImageBitmap& image) {
        if (image.empty()) return nullptr;
        if (auto it = images_.find(image.id()); it != images_.end()) return it->second.texture;
        SDL_Texture* t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, image.width(),
                                           image.height());
        if (!t) return nullptr;
        SDL_UpdateTexture(t, nullptr, image.pixels(), image.width() * 4);
        SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
        SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
        images_.emplace(image.id(), ImageTexture{t, image.token()});
        return t;
    }

    // A render target at least w x h. Layers that move by fractions of a pixel vary by one in
    // size: sizes are rounded up to 32 px and a slightly bigger free texture is reused, so a
    // scrolling page doesn't create (and drop) a texture every frame.
    SDL_Texture* acquire(int w, int h, int* texW, int* texH) {
        PooledTexture* best = nullptr;
        for (auto& p : pool_)
            if (!p.inUse && p.w >= w && p.h >= h && p.w <= w + 64 && p.h <= h + 64 && (!best || p.w * p.h < best->w * best->h))
                best = &p;
        if (!best) {
            int tw = (w + 31) / 32 * 32, th = (h + 31) / 32 * 32;
            SDL_Texture* t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, tw, th);
            if (!t) return nullptr;
            SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
            pool_.push_back({t, tw, th});
            best = &pool_.back();
        }
        best->inUse = best->usedThisFrame = true;
        *texW = best->w;
        *texH = best->h;
        return best->texture;
    }

    void release(SDL_Texture* t) {
        for (auto& p : pool_)
            if (p.texture == t) p.inUse = false;
    }

    void fillPixelRect(float x, float y, float w, float h, Color color) {
        if (w <= 0 || h <= 0) return;
        SDL_SetRenderDrawColorFloat(renderer_, color.r, color.g, color.b, color.a);
        SDL_FRect rect{x, y, w, h};
        SDL_RenderFillRect(renderer_, &rect);
    }

    // Outline of a rounded rect (clockwise) with outward normals.
    void contour(float x, float y, float w, float h, const CornerRadii& radii) {
        points_.clear();
        normals_.clear();
        // A quarter circle in some steps, computed once per step count; each corner is it, as
        // big as the corner's radius, turned by a multiple of 90 degrees.
        static const auto quarters = [] {
            std::array<std::array<SDL_FPoint, 17>, 17> q{};
            for (int s = 1; s <= 16; ++s)
                for (int i = 0; i <= s; ++i) {
                    float a = float(i) / float(s) * std::numbers::pi_v<float> / 2;
                    q[size_t(s)][size_t(i)] = {std::cos(a), std::sin(a)};
                }
            return q;
        }();
        const float rs[4] = {radii.topRight, radii.bottomRight, radii.bottomLeft, radii.topLeft};  // clockwise
        const float corners[4][2] = {{x + w - rs[0], y + rs[0]}, {x + w - rs[1], y + h - rs[1]}, {x + rs[2], y + h - rs[2]}, {x + rs[3], y + rs[3]}};
        for (int c = 0; c < 4; ++c) {
            float r = rs[c];
            int segments = std::clamp(int(std::ceil(r / 2)), 1, 16);
            const auto& q = quarters[size_t(segments)];
            for (int i = 0; i <= segments; ++i) {
                SDL_FPoint u = q[size_t(i)];  // from -90, 0, 90 and 180 degrees on
                SDL_FPoint n = c == 0 ? SDL_FPoint{u.y, -u.x} : c == 1 ? u : c == 2 ? SDL_FPoint{-u.y, u.x} : SDL_FPoint{-u.x, -u.y};
                points_.push_back({corners[c][0] + n.x * r, corners[c][1] + n.y * r});
                normals_.push_back(n);
            }
        }
    }

    // Splits contour edges longer than `maxLength` px (normals interpolated).
    void densify(float maxLength) {
        std::vector<SDL_FPoint> points, normals;
        size_t n = points_.size();
        for (size_t i = 0; i < n; ++i) {
            size_t j = (i + 1) % n;
            points.push_back(points_[i]);
            normals.push_back(normals_[i]);
            float dx = points_[j].x - points_[i].x, dy = points_[j].y - points_[i].y;
            int pieces = int(std::hypot(dx, dy) / maxLength);
            for (int k = 1; k < pieces; ++k) {
                float t = float(k) / float(pieces);
                points.push_back({points_[i].x + dx * t, points_[i].y + dy * t});
                float nx = normals_[i].x + (normals_[j].x - normals_[i].x) * t;
                float ny = normals_[i].y + (normals_[j].y - normals_[i].y) * t;
                float len = std::max(1e-6f, std::hypot(nx, ny));
                normals.push_back({nx / len, ny / len});
            }
        }
        points_.swap(points);
        normals_.swap(normals);
    }

    // Rasterizes `path` (filled, or stroked by FreeType's stroker) into a coverage texture.
    // FNV-1a over everything but the position that changes the pixels.
    uint64_t pathKey(const Path& path, const PathStroke& stroke) const {
        uint64_t key = 1469598103934665603ull;
        auto mix = [&](const void* data, size_t size) {
            for (size_t i = 0; i < size; ++i) key = (key ^ static_cast<const uint8_t*>(data)[i]) * 1099511628211ull;
        };
        for (const PathCommand& c : path.commands()) mix(&c, sizeof c);
        float params[3] = {scale, stroke.width, stroke.miter};
        int kinds[3] = {int(path.fillType), int(stroke.cap), int(stroke.join)};
        mix(params, sizeof params);
        mix(kinds, sizeof kinds);
        return key;
    }


    // A path's fill as coverage (0..1) on a width x height canvas, each point at at(x, y) px (y
    // down); empty if it can't be rasterized.
    template <class At>
    std::vector<float> fillCoverage(const Path& path, const At& at, int width, int height) {
        using Tag = std::remove_pointer_t<decltype(FT_Outline::tags)>;
        using ContourEnd = std::remove_pointer_t<decltype(FT_Outline::contours)>;
        auto pt = [&](float x, float y) {  // 26.6 px, y up
            SDL_FPoint p = at(x, y);
            return FT_Vector{FT_Pos(std::lround(p.x * 64)), FT_Pos(std::lround((float(height) - p.y) * 64))};
        };
        std::vector<FT_Vector> points;
        std::vector<Tag> tags;
        std::vector<ContourEnd> contours;
        FT_Outline outline{};
        if (!fillOutline(path.commands(), path.fillType == Path::FillType::EvenOdd, pt, points, tags, contours, outline)) return {};
        std::vector<unsigned char> gray;
        FT_Bitmap bitmap = grayCanvas(gray, width, height);
        if (FT_Outline_Get_Bitmap(fonts_.library(), &outline, &bitmap) != 0) return {};
        std::vector<float> out(gray.size());
        for (size_t i = 0; i < gray.size(); ++i) out[i] = float(gray[i]) / 255.f;
        return out;
    }

    const PathMask* pathMask(const Path& path, float fx, float fy, const PathStroke& stroke) {
        uint64_t key = pathKey(path, stroke);
        for (float f : {fx, fy}) key = (key ^ uint64_t(std::lround(f * 4) + 1)) * 1099511628211ull;
        if (auto it = pathMasks_.find(key); it != pathMasks_.end()) {
            it->second.used = true;
            return &it->second;
        }
        PathMask& m = pathMasks_[key];
        Rect b = path.bounds();
        float pad = 2 + (stroke.width > 0 ? stroke.width * scale / 2 * std::max(1.f, stroke.join == StrokeJoin::Miter ? stroke.miter : 1.f) : 0);
        float minX = b.x * scale + fx - pad, minY = b.y * scale + fy - pad;
        float maxX = b.right() * scale + fx + pad, maxY = b.bottom() * scale + fy + pad;
        m.left = int(std::floor(minX));
        m.top = int(std::floor(minY));
        m.width = int(std::ceil(maxX)) - m.left;
        m.height = int(std::ceil(maxY)) - m.top;
        if (m.width <= 0 || m.height <= 0 || m.width > 4096 || m.height > 4096) return &m;

        FT_Library lib = fonts_.library();
        auto pt = [&](float x, float y) {  // path dp -> 26.6 px in the bitmap, y up
            return FT_Vector{FT_Pos(std::lround((x * scale + fx - float(m.left)) * 64)),
                             FT_Pos(std::lround((float(m.height) - (y * scale + fy - float(m.top))) * 64))};
        };
        using Tag = std::remove_pointer_t<decltype(FT_Outline::tags)>;
        using ContourEnd = std::remove_pointer_t<decltype(FT_Outline::contours)>;
        std::vector<FT_Vector> points;
        std::vector<Tag> tags;
        std::vector<ContourEnd> contours;
        FT_Outline outline{};
        bool stroked = stroke.width > 0;
        FT_Stroker stroker = nullptr;
        if (stroked) {
            FT_Stroker_New(lib, &stroker);
            FT_Stroker_Set(stroker, FT_Fixed(stroke.width * scale / 2 * 64),
                           stroke.cap == StrokeCap::Round    ? FT_STROKER_LINECAP_ROUND
                           : stroke.cap == StrokeCap::Square ? FT_STROKER_LINECAP_SQUARE
                                                             : FT_STROKER_LINECAP_BUTT,
                           stroke.join == StrokeJoin::Round   ? FT_STROKER_LINEJOIN_ROUND
                           : stroke.join == StrokeJoin::Bevel ? FT_STROKER_LINEJOIN_BEVEL
                                                              : FT_STROKER_LINEJOIN_MITER,
                           FT_Fixed(stroke.miter * 65536));
            // Each subpath separately: open ones get caps, closed ones are joined all around.
            const auto& cmds = path.commands();
            for (size_t i = 0; i < cmds.size();) {
                if (cmds[i].verb != PathCommand::Verb::Move) {
                    ++i;
                    continue;
                }
                size_t end = i + 1;
                while (end < cmds.size() && cmds[end].verb != PathCommand::Verb::Move && cmds[end].verb != PathCommand::Verb::Close) ++end;
                bool closed = end < cmds.size() && cmds[end].verb == PathCommand::Verb::Close;
                FT_Vector start = pt(cmds[i].x, cmds[i].y);
                auto differs = [&](float x, float y) {
                    FT_Vector p = pt(x, y);
                    return p.x != start.x || p.y != start.y;
                };
                bool moves = false;
                for (size_t k = i + 1; k < end && !moves; ++k) {
                    const PathCommand& c = cmds[k];
                    moves = differs(c.x, c.y) || (c.verb != PathCommand::Verb::Line && differs(c.x1, c.y1)) ||
                            (c.verb == PathCommand::Verb::Cubic && differs(c.x2, c.y2));
                }
                if (end > i + 1 && !moves) {
                    // FreeType's stroker crashes on a subpath that goes nowhere. It's a dot: its
                    // caps drawn round a line 1/64 px long (nothing for butt caps, as Skia does).
                    if (stroke.cap != StrokeCap::Butt) {
                        FT_Vector to{start.x + 1, start.y};
                        FT_Stroker_BeginSubPath(stroker, &start, true);
                        FT_Stroker_LineTo(stroker, &to);
                        FT_Stroker_EndSubPath(stroker);
                    }
                } else if (end > i + 1) {
                    FT_Stroker_BeginSubPath(stroker, &start, !closed);
                    for (size_t k = i + 1; k < end; ++k) {
                        const PathCommand& c = cmds[k];
                        FT_Vector to = pt(c.x, c.y), c1 = pt(c.x1, c.y1), c2 = pt(c.x2, c.y2);
                        if (c.verb == PathCommand::Verb::Line) FT_Stroker_LineTo(stroker, &to);
                        if (c.verb == PathCommand::Verb::Quad) FT_Stroker_ConicTo(stroker, &c1, &to);
                        if (c.verb == PathCommand::Verb::Cubic) FT_Stroker_CubicTo(stroker, &c1, &c2, &to);
                    }
                    FT_Stroker_EndSubPath(stroker);
                }
                i = closed ? end + 1 : end;
            }
            FT_UInt np = 0, nc = 0;
            FT_Stroker_GetCounts(stroker, &np, &nc);
            if (np == 0 || FT_Outline_New(lib, np, FT_Int(nc), &outline) != 0) {
                FT_Stroker_Done(stroker);
                return &m;
            }
            outline.n_points = 0;
            outline.n_contours = 0;
            FT_Stroker_Export(stroker, &outline);
        } else if (!fillOutline(path.commands(), path.fillType == Path::FillType::EvenOdd, pt, points, tags, contours, outline)) {
            return &m;
        }
        std::vector<unsigned char> gray;
        FT_Bitmap bitmap = grayCanvas(gray, m.width, m.height);
        bool ok = FT_Outline_Get_Bitmap(lib, &outline, &bitmap) == 0;
        if (stroked) {
            FT_Outline_Done(lib, &outline);
            FT_Stroker_Done(stroker);
        }
        if (!ok) return &m;
        std::vector<uint8_t> rgba(gray.size() * 4);
        for (size_t i = 0; i < gray.size(); ++i) {
            rgba[i * 4 + 0] = rgba[i * 4 + 1] = rgba[i * 4 + 2] = 255;
            rgba[i * 4 + 3] = gray[i];
        }
        m.texture = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, m.width, m.height);
        if (m.texture) {
            SDL_SetTextureBlendMode(m.texture, SDL_BLENDMODE_BLEND);
            SDL_UpdateTexture(m.texture, nullptr, rgba.data(), m.width * 4);
        }
        return &m;
    }

    // The glyphs in glyphs_, from their atlas pages (uploaded first where they changed), in
    // runs of one page. `shade`: each corner's color from where it is (a gradient) instead of
    // `color`. Emoji keep their colors, at the color's alpha.
    void drawGlyphs(SDL_FColor color, const std::function<SDL_FColor(float, float)>* shade = nullptr) {
        constexpr float kPage = float(gpu::AtlasPage::kSize);
        for (size_t start = 0; start < glyphs_.size();) {
            int page = glyphs_[start].page;
            size_t end = start;
            begin();
            for (; end < glyphs_.size() && glyphs_[end].page == page; ++end) {
                const gpu::GlyphQuad& g = glyphs_[end];
                float u0 = float(g.u) / kPage, v0 = float(g.v) / kPage;
                float u1 = float(g.u + g.width) / kPage, v1 = float(g.v + g.height) / kPage;
                SDL_FColor c = g.color ? SDL_FColor{1, 1, 1, color.a} : color;
                auto at = [&](float x, float y) { return shade && !g.color ? (*shade)(x, y) : c; };
                int base = int(vertices_.size());
                vertices_.push_back({{g.x0, g.y0}, at(g.x0, g.y0), {u0, v0}});
                vertices_.push_back({{g.x1, g.y0}, at(g.x1, g.y0), {u1, v0}});
                vertices_.push_back({{g.x1, g.y1}, at(g.x1, g.y1), {u1, v1}});
                vertices_.push_back({{g.x0, g.y1}, at(g.x0, g.y1), {u0, v1}});
                indices_.insert(indices_.end(), {base, base + 1, base + 2, base, base + 2, base + 3});
            }
            if (SDL_Texture* t = pageTexture(page))
                SDL_RenderGeometry(renderer_, t, vertices_.data(), int(vertices_.size()), indices_.data(), int(indices_.size()));
            start = end;
        }
    }

    // An atlas page's texture, with what changed on the page since the last time uploaded.
    SDL_Texture* pageTexture(int index) {
        if (size_t(index) >= pages_.size()) pages_.resize(size_t(index) + 1, nullptr);
        gpu::AtlasPage& page = fonts_.atlas().page(index);
        gpu::DirtyRect dirty = page.takeDirty();
        SDL_Texture*& t = pages_[size_t(index)];
        if (!t) {  // new, or lost with the GPU context: all of it
            constexpr int kSize = gpu::AtlasPage::kSize;
            t = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_STATIC, kSize, kSize);
            if (!t) return nullptr;
            SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
            dirty = {0, 0, kSize, kSize};
        }
        if (!dirty.empty()) {
            SDL_Rect rect{dirty.x0, dirty.y0, dirty.x1 - dirty.x0, dirty.y1 - dirty.y0};
            SDL_UpdateTexture(t, &rect, page.pixels() + (size_t(dirty.y0) * gpu::AtlasPage::kSize + size_t(dirty.x0)) * 4,
                              gpu::AtlasPage::kSize * 4);
        }
        return t;
    }

    void begin() {
        vertices_.clear();
        indices_.clear();
    }
    int vertex(SDL_FPoint p, SDL_FColor c) {
        vertices_.push_back({p, c, {0, 0}});
        return int(vertices_.size()) - 1;
    }
    void triangle(int a, int b, int c) { indices_.insert(indices_.end(), {a, b, c}); }
    // The contour moved `distance` px along its normals.
    int ring(float distance, SDL_FColor c) {
        int first = int(vertices_.size());
        for (size_t i = 0; i < points_.size(); ++i)
            vertex({points_[i].x + normals_[i].x * distance, points_[i].y + normals_[i].y * distance}, c);
        return first;
    }
    void strip(int a, int b) {
        int n = int(points_.size());
        for (int i = 0; i < n; ++i) {
            int j = (i + 1) % n;
            triangle(a + i, b + i, b + j);
            triangle(a + i, b + j, a + j);
        }
    }
    void flush() {
        SDL_RenderGeometry(renderer_, nullptr, vertices_.data(), int(vertices_.size()), indices_.data(),
                           int(indices_.size()));
    }

    bool software_ = false;
    SDL_Renderer* renderer_;
    FontSystem& fonts_;
    std::vector<SDL_Texture*> pages_;  // the glyph atlas's pages
    std::vector<gpu::GlyphQuad> glyphs_;
    std::vector<SDL_Rect> clips_;
    std::vector<Layer> layers_;
    std::vector<BlendMode> blends_;  // pushBlendMode's, innermost last
    std::vector<ColorFilter> filters_;  // pushColorFilter's, innermost last
    SDL_BlendMode blend_ = SDL_BLENDMODE_BLEND;
    std::unordered_map<uint64_t, ImageTexture> images_;
    std::vector<PooledTexture> pool_;
    struct GradientMesh {
        Brush brush;
        float w, h;
        CornerRadii r;
        float scale;
        Rect area;  // the brush's, relative to the rect (dp)
        std::vector<SDL_Vertex> vertices;  // relative to the rect's corner (px)
        std::vector<int> indices;
        bool used = true;
        int idleFrames = 0;
    };
    std::vector<GradientMesh> gradients_;
    struct GradientTexture {
        Brush brush;
        SDL_Texture* texture = nullptr;
        bool used = true;
        int idleFrames = 0;
    };
    std::vector<GradientTexture> gradientTextures_;
    float ox_ = 0, oy_ = 0;  // pixel origin of the current layer
    std::vector<SDL_FPoint> translations_;  // the origins before each pushTranslation
    std::vector<SDL_FPoint> points_, normals_;
    std::vector<SDL_Vertex> vertices_;
    std::vector<int> indices_;
};

// KOMPOZ_SCRIPT="click 100 200; type hello; key enter; wait 30; shot out.png; quit"
// drives the app without a user, for UI tests and screenshots. Time is virtual while a
// script runs (16.7 ms per frame), so animations are deterministic; then it goes on in real
// time, for whoever takes over where the script left the app.
struct Script {
    std::vector<std::string> commands;
    size_t next = 0;
    int waitFrames = 0;
    uint64_t sleepUntilNs = 0;  // `sleep`: real time going by (SDL ticks), for work on other threads
    int64_t clock = 0;
    // A swipe in progress: one pointer move per frame, so it has a real velocity.
    Offset swipeFrom, swipeTo;
    int swipeFrames = 0, swipeStep = 0;
    std::deque<std::string> dropped;  // what dropfile/droptext pushed to SDL, alive until handled

    bool enabled() const { return !commands.empty(); }
    bool running() const { return next < commands.size() || waitFrames > 0 || sleepUntilNs > 0 || swipeStep < swipeFrames; }
};

struct App {
    Application config;
#ifdef KOMPOZ_DESKTOP_ACCENT
    std::unique_ptr<kompoz::sdl::DesktopAccent> accent;  // the desktop's accent color: dynamic color (Linux)
#endif
#ifdef KOMPOZ_WALLPAPER_COLORS
    std::unique_ptr<kompoz::sdl::WallpaperColors> wallpaper;  // the wallpaper's own colors (desktops)
#endif
#ifdef KOMPOZ_ATSPI
    std::unique_ptr<kompoz::sdl::AtspiBridge> a11y;  // screen readers (Linux: AT-SPI)
#elif defined(KOMPOZ_UIA)
    std::unique_ptr<kompoz::sdl::UiaBridge> a11y;  // screen readers (Windows: UI Automation)
#endif
    int64_t lastSemantics = 0;  // Android accessibility, AT-SPI
    bool semanticsDirty = true;  // the screen changed since the tree was last published
    bool wantsBack = false;  // as Android was last told (the app's back callback registered)
    bool keepScreenOn = false;  // as SDL was last told
    bool sensitiveContent = false;  // as Android was last told
    float frameRate = 0;             // the same
    kompoz::FrameRateCategory frameRateCategory = kompoz::FrameRateCategory::Default;
    std::vector<kompoz::Rect> gestureExclusion;  // as Android was last told (systemGestureExclusion)
    bool mediaProbed = false;  // a microphone and a camera looked for (after the first frame)
    bool hasMicrophone = false, hasCamera = false;
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    std::unique_ptr<FontSystem> fonts;
    std::unique_ptr<SdlCanvas> canvas;
    // KOMPOZ_GPU=vulkan: Kompoz's GPU renderer instead of SDL_Renderer (gpu/renderer.hpp), with
    // the kept frame in a texture of its own.
    std::unique_ptr<gpu::Device> gpu;
    std::unique_ptr<gpu::Renderer> gpuCanvas;
    std::unique_ptr<gpu::Texture> gpuFrame;
    bool gpuInFrame = false;
    GraphicsContext& graphics() { return gpuCanvas ? static_cast<GraphicsContext&>(*gpuCanvas) : *canvas; }
    float pixelScale() const { return gpuCanvas ? gpuCanvas->scale : canvas->scale; }
    std::unique_ptr<ComposeHost> host;
    float scale = 1, pointToDp = 1;
    Uint32 wakeEvent = 0;
    std::atomic<bool> wakePending{false};
    bool waiting = false, quit = false;
    SDL_Cursor* cursors[9] = {};  // by kompoz::Cursor
    Script script;
    int64_t clockOffset = 0;  // added to SDL's clock after a script whose virtual time got ahead of it
    bool trace = SDL_getenv("KOMPOZ_TRACE") != nullptr;  // log what each recomposition ran
    // KOMPOZ_PROFILE: where frame time goes. Each slow frame is logged (input handling, compose +
    // layout, drawing, present, offscreen layers, glyphs rasterized), and a summary every second.
    bool profile = SDL_getenv("KOMPOZ_PROFILE") != nullptr;
    float pinchScale = 1;  // the touchpad pinch's scale so far (Wayland reports it since the start)
    bool pinchSinceStart = false;
    struct Profile {
        uint64_t lastStart = 0, eventsNs = 0, summaryStart = 0;
        int frames = 0, slow = 0, unchanged = 0;  // unchanged: recorded, nothing on the screen differed
        double workTotal = 0, workMax = 0, intervalMax = 0, drawnArea = 0;  // drawnArea: fractions of the screen, summed
    } prof;
    // The frame is recorded, and only what changed on the screen is drawn again, into pixels
    // kept in a texture (`frame`) that is then shown; a frame that changes nothing isn't shown.
    std::unique_ptr<kompoz::DisplayList> display;
    SDL_Texture* frame = nullptr;
    int frameW = 0, frameH = 0;
    bool showFrame = true;  // the window needs the kept pixels again (exposed, first frame)
    // Frames still to draw whole and show after the window was resized. Android resizes its
    // surface a frame or two after telling us (the keyboard coming or going): what is shown
    // meanwhile goes into buffers of the old size, which it would keep stretched over the new
    // one until something changed.
    int resizeFrames = 0;
    bool focused = true, minimized = false, hidden = false;  // desktops' and browsers' lifecycle
    uint64_t framePeriodNs = 16'666'667;  // the display's refresh interval: frames not shown are paced to it
    bool damageCheck = SDL_getenv("KOMPOZ_DAMAGE_CHECK") != nullptr;  // each frame compared with all of it drawn
    bool damageShow = SDL_getenv("KOMPOZ_DAMAGE_SHOW") != nullptr;    // outlines what was drawn again
    bool fullRedraw = SDL_getenv("KOMPOZ_FULL_REDRAW") != nullptr;    // all of every frame drawn, as before
    // A drag from another app coming in: what it drops, gathered until it is complete.
    bool dropping = false;
    std::vector<std::string> dropFiles;
    std::string dropText;
    kompoz::Offset dropAt;
    int damageFrames = 0, damageWrong = 0;
    SDL_TimerID wakeTimer = 0;  // wakes the idle loop when a coroutine delay is due
    int64_t wakeDeadline = -1;
    kompoz::detail::RecompositionStats traced;

    ~App() {
        kompoz::sdl::stopImageLoader();  // its threads end before the rest goes
        if (wakeTimer) SDL_RemoveTimer(wakeTimer);
        if (frame) SDL_DestroyTexture(frame);
        if (damageCheck) SDL_Log("kompoz: damage check: %d of %d frames differed", damageWrong, damageFrames);
#ifdef SDL_PLATFORM_ANDROID
        android_ime::setHandlers(nullptr, nullptr, nullptr, nullptr);
        android_ime::setMediaHandler(nullptr);
        android_ime::setDeepLinkHandler(nullptr);
        android_ime::setBackHandler(nullptr);
#endif
#ifdef KOMPOZ_A11Y_BRIDGE
        a11y.reset();  // (UI Automation's hooked into the window)
#endif
        host.reset();  // runs effect cleanups while the window still exists
        canvas.reset();
        gpuFrame.reset();
        gpuCanvas.reset();
        gpu.reset();
        fonts.reset();
        for (auto* cursor : cursors)
            if (cursor) SDL_DestroyCursor(cursor);
        if (renderer) SDL_DestroyRenderer(renderer);
        if (window) SDL_DestroyWindow(window);
    }

    Offset toDp(float x, float y) const { return {x * pointToDp, y * pointToDp}; }
    // Frame and input times are on one clock: a script's virtual one while it runs, SDL's after.
    int64_t clock() const { return int64_t(SDL_GetTicksNS()) + clockOffset; }
    int64_t eventTime(Uint64 timestamp) const { return script.running() ? script.clock : int64_t(timestamp) + clockOffset; }
};

// The device for mediaQuery: Android's own account of it; elsewhere a mouse is a fine pointer, a
// touch screen a coarse one, a keyboard SDL knows of a physical one, the microphone and camera
// as probeMedia found them.
void refreshMedia(App& app) {
    kompoz::MediaInfo info;
#ifdef SDL_PLATFORM_ANDROID
    if (auto told = kompoz::android_ime::mediaInfo()) info = *told;
#else
    int mice = 0, keyboards = 0, touch = 0;
    SDL_free(SDL_GetMice(&mice));
    SDL_free(SDL_GetKeyboards(&keyboards));
    SDL_free(SDL_GetTouchDevices(&touch));
    info.pointerPrecision = mice > 0 ? kompoz::PointerPrecision::Fine : touch > 0 ? kompoz::PointerPrecision::Coarse : kompoz::PointerPrecision::None;
    info.physicalKeyboard = keyboards > 0;
    info.hasMicrophone = app.hasMicrophone;
    info.hasCamera = app.hasCamera;
#endif
    app.host->setMediaInfo(info);
}

// Looks for a recording device and a camera, once, after the first frame (starting SDL's audio
// and camera subsystems takes a few tens of milliseconds); not on the web, where it would ask.
void probeMedia(App& app) {
    app.mediaProbed = true;
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_EMSCRIPTEN)
    int count = 0;
    if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
        SDL_free(SDL_GetAudioRecordingDevices(&count));
        app.hasMicrophone = count > 0;
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
    count = 0;
    if (SDL_InitSubSystem(SDL_INIT_CAMERA)) {
        SDL_free(SDL_GetCameras(&count));
        app.hasCamera = count > 0;
        SDL_QuitSubSystem(SDL_INIT_CAMERA);
    }
    refreshMedia(app);
#endif
}

void updateMetrics(App& app) {
    int pw = 0, ph = 0;
    if (app.renderer)
        SDL_GetRenderOutputSize(app.renderer, &pw, &ph);
    else
        SDL_GetWindowSizeInPixels(app.window, &pw, &ph);
    float scale = SDL_GetWindowDisplayScale(app.window);
    float density = SDL_GetWindowPixelDensity(app.window);
#ifndef SDL_PLATFORM_ANDROID
    if (const char* env = SDL_getenv("KOMPOZ_SCALE")) scale = density = float(std::atof(env));  // a phone's density, say 2.625
#endif
    if (scale <= 0) scale = 1;
    if (density <= 0) density = 1;
    app.scale = scale;
    app.pointToDp = density / scale;
    app.fonts->setScale(scale);
    if (app.canvas) app.canvas->scale = scale;
    if (app.gpuCanvas) app.gpuCanvas->scale = scale;
    float fontScale = 1;
#ifdef SDL_PLATFORM_ANDROID
    fontScale = kompoz::android_ime::fontScale();  // Settings > Display > Font size
#else
    if (const char* env = SDL_getenv("KOMPOZ_FONT_SCALE")) fontScale = std::max(0.5f, float(std::atof(env)));
#endif
    app.fonts->setFontScale(fontScale);
    app.host->setDensity({scale, fontScale});
    app.host->setViewportSize({float(pw) / scale, float(ph) / scale});
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(app.window));
    app.framePeriodNs = uint64_t(1e9 / (mode && mode->refresh_rate > 0 ? mode->refresh_rate : 60.f));

    // System bars and display cutouts (iOS notch, desktop). Android reports its own insets
    // through KompozInput, without SDL's gesture-navigation edges.
#ifndef SDL_PLATFORM_ANDROID
    SDL_Rect safe;
    int w = 0, h = 0;
    if (SDL_GetWindowSafeArea(app.window, &safe) && SDL_GetWindowSize(app.window, &w, &h)) {
        float k = app.pointToDp;
        app.host->setWindowInsets({float(safe.x) * k, float(safe.y) * k, float(w - safe.x - safe.w) * k,
                                   float(h - safe.y - safe.h) * k});
    }
#endif
    app.host->setSystemDarkTheme(SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);
#ifdef KOMPOZ_WALLPAPER_COLORS
    if (app.wallpaper) app.wallpaper->setDarkTheme(SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);  // a dark variant
#endif
    // The preferred language: right to left when it is written so (KOMPOZ_LAYOUT_DIRECTION=rtl|ltr
    // decides instead), and the library's texts in it (KOMPOZ_LANGUAGE=tr|en decides instead).
    std::string language;
    int count = 0;
    if (SDL_Locale** locales = SDL_GetPreferredLocales(&count)) {
        if (count > 0 && locales[0] && locales[0]->language) language = locales[0]->language;
        SDL_free(locales);
    }
    bool rtl = kompoz::isRtlLanguage(language);
    if (const char* forced = SDL_getenv("KOMPOZ_LAYOUT_DIRECTION")) rtl = std::string_view(forced) == "rtl";
    app.host->setLayoutDirection(rtl ? kompoz::LayoutDirection::Rtl : kompoz::LayoutDirection::Ltr);
    if (const char* forced = SDL_getenv("KOMPOZ_LANGUAGE")) language = forced;
    app.host->setStrings(kompoz::Strings::forLanguage(language));
#ifdef SDL_PLATFORM_ANDROID
    app.host->setDynamicTonalPalette(kompoz::android_ime::dynamicPalette());
    app.host->setWallpaperTonalPalette(kompoz::android_ime::wallpaperPalette());
    if (auto config = kompoz::android_ime::viewConfiguration(app.scale)) app.host->setViewConfiguration(*config);
#endif
    refreshMedia(app);
    app.host->requestRedraw();
}

Key keyOf(SDL_Keycode k) {
    if (k >= SDLK_A && k <= SDLK_Z) return Key(int(Key::A) + int(k - SDLK_A));
    if (k >= SDLK_0 && k <= SDLK_9) return Key(int(Key::Zero) + int(k - SDLK_0));
    if (k >= SDLK_F1 && k <= SDLK_F12) return Key(int(Key::F1) + int(k - SDLK_F1));
    switch (k) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER: return Key::Enter;
    case SDLK_ESCAPE: return Key::Escape;
    case SDLK_TAB: return Key::Tab;
    case SDLK_BACKSPACE: return Key::Backspace;
    case SDLK_DELETE: return Key::Delete;
    case SDLK_SPACE: return Key::Space;
    case SDLK_LEFT: return Key::Left;
    case SDLK_RIGHT: return Key::Right;
    case SDLK_UP: return Key::Up;
    case SDLK_DOWN: return Key::Down;
    case SDLK_HOME: return Key::Home;
    case SDLK_END: return Key::End;
    case SDLK_PAGEUP: return Key::PageUp;
    case SDLK_PAGEDOWN: return Key::PageDown;
    default: return Key::Unknown;
    }
}

// An image asset (APK on Android) or a file next to the binary: JPEG, PNG, WebP, BMP (on the
// web, where the browser decodes asynchronously, PNG and BMP; AsyncImage takes the others).
ImageBitmap loadImage(std::string_view path) {
    std::string p(path);
#ifndef SDL_PLATFORM_EMSCRIPTEN
    if (std::vector<uint8_t> bytes = readFile(p); !bytes.empty())
        if (ImageBitmap image = decodeImage(bytes); !image.empty()) return image;
#endif
    SDL_Surface* surface = SDL_LoadSurface(p.c_str());
    if (!surface && SDL_GetBasePath()) surface = SDL_LoadSurface((SDL_GetBasePath() + p).c_str());
    if (!surface) {
        SDL_Log("kompoz: can't load image %s: %s", p.c_str(), SDL_GetError());
        return {};
    }
    SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (!rgba) return {};
    std::vector<uint8_t> pixels(size_t(rgba->w) * size_t(rgba->h) * 4);
    for (int y = 0; y < rgba->h; ++y)
        std::memcpy(pixels.data() + size_t(y) * size_t(rgba->w) * 4,
                    static_cast<const uint8_t*>(rgba->pixels) + size_t(y) * size_t(rgba->pitch), size_t(rgba->w) * 4);
    ImageBitmap image = ImageBitmap::fromPixels(rgba->w, rgba->h, std::move(pixels));
    SDL_DestroySurface(rgba);
    return image;
}

// From `frame` when there is one: the whole of what was drawn (an offscreen OpenGL window keeps the
// size it was created at, so reading the window would cut a larger one off).
void saveScreenshot(SDL_Renderer* renderer, const std::string& path, SDL_Texture* frame = nullptr) {
    SDL_Texture* before = SDL_GetRenderTarget(renderer);
    if (frame) SDL_SetRenderTarget(renderer, frame);
    SDL_Surface* surface = SDL_RenderReadPixels(renderer, nullptr);
    if (frame) SDL_SetRenderTarget(renderer, before);
    if (!surface) return;
    bool ok;
#if SDL_VERSION_ATLEAST(3, 4, 0)
    ok = path.ends_with(".png") ? SDL_SavePNG(surface, path.c_str()) : SDL_SaveBMP(surface, path.c_str());
#else
    ok = SDL_SaveBMP(surface, path.c_str());
#endif
    SDL_Log("kompoz: screenshot %s %s", path.c_str(), ok ? "saved" : SDL_GetError());
    SDL_DestroySurface(surface);
}

// Runs script commands until one asks to wait. Returns a screenshot path for this frame, if any.
// KOMPOZ_PROFILE: logs a frame that took long (or came long after the one before) and a
// summary every second. "work" is input + compose/layout + drawing; present waits for vsync.
using DrawInto = std::function<void(SDL_Texture*, const std::optional<Rect>&)>;

// GraphicsLayer::toImageBitmap: `draw` into a texture of `size` dp at the window's density, read
// back. A target starts transparent and blending leaves premultiplied colors: they are divided
// back out.
// Pixels read back from a target (RGBA, premultiplied) as an image: the colors divided back out.
ImageBitmap unpremultipliedImage(int w, int h, std::vector<uint8_t> pixels) {
    if (pixels.size() != size_t(w) * size_t(h) * 4) return {};
    for (size_t i = 0; i < pixels.size(); i += 4)
        if (uint8_t a = pixels[i + 3]; a > 0 && a < 255)
            for (int k = 0; k < 3; ++k) pixels[i + size_t(k)] = uint8_t(std::min(255, int(pixels[i + size_t(k)]) * 255 / a));
    return ImageBitmap::fromPixels(w, h, std::move(pixels));
}

ImageBitmap renderImageGpu(App& app, Size size, const std::function<void(GraphicsContext&)>& draw) {
    gpu::Renderer& r = *app.gpuCanvas;
    int w = std::max(1, int(std::ceil(size.width * r.scale))), h = std::max(1, int(std::ceil(size.height * r.scale)));
    if (w > maxImageDimension || h > maxImageDimension) return {};
    bool ownFrame = !app.gpuInFrame;  // (asked for between frames: a frame of its own)
    if (ownFrame) app.gpu->beginFrame();
    std::unique_ptr<gpu::Texture> target = app.gpu->createTexture(w, h, gpu::Format::RGBA8, gpu::TextureUsage::Sampled | gpu::TextureUsage::RenderTarget);
    std::vector<uint8_t> pixels;
    if (target) {
        r.beginTarget(*target, gpu::ClearColor{});
        draw(r);
        r.endTarget();
        r.flush();
        pixels = app.gpu->readPixels(*target, {0, 0, w, h});
    }
    if (ownFrame) app.gpu->endFrame(false);
    return unpremultipliedImage(w, h, std::move(pixels));
}

ImageBitmap renderImage(App& app, Size size, const std::function<void(GraphicsContext&)>& draw) {
    if (app.gpuCanvas) return renderImageGpu(app, size, draw);
    float scale = app.canvas->scale;
    int w = std::max(1, int(std::ceil(size.width * scale))), h = std::max(1, int(std::ceil(size.height * scale)));
    if (w > maxImageDimension || h > maxImageDimension) return {};
    SDL_Texture* target = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, w, h);
    if (!target) return {};
    SDL_Texture* before = SDL_GetRenderTarget(app.renderer);
    SDL_SetRenderTarget(app.renderer, target);
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
    SDL_SetRenderDrawColor(app.renderer, 0, 0, 0, 0);
    SDL_RenderClear(app.renderer);
    SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
    draw(*app.canvas);
    SDL_Surface* surface = SDL_RenderReadPixels(app.renderer, nullptr);
    SDL_SetRenderTarget(app.renderer, before);
    SDL_DestroyTexture(target);
    if (!surface) return {};
    SDL_Surface* rgba = SDL_ConvertSurface(surface, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(surface);
    if (!rgba) return {};
    std::vector<uint8_t> pixels(size_t(rgba->w) * size_t(rgba->h) * 4);
    for (int y = 0; y < rgba->h; ++y)
        std::memcpy(pixels.data() + size_t(y) * size_t(rgba->w) * 4,
                    static_cast<const uint8_t*>(rgba->pixels) + size_t(y) * size_t(rgba->pitch), size_t(rgba->w) * 4);
    ImageBitmap image = unpremultipliedImage(rgba->w, rgba->h, std::move(pixels));
    SDL_DestroySurface(rgba);
    return image;
}

// KOMPOZ_DAMAGE_CHECK: the kept pixels (a), with only what changed drawn again, against all of
// the frame drawn afresh (b), w x h, rows `pitch` bytes apart. Differences are logged: they would
// be stale pixels on the screen.
void compareDamage(App& app, const Rect& damage, const uint8_t* a, int pitchA, const uint8_t* b, int pitchB, int w, int h) {
    ++app.damageFrames;
    int count = 0, x0 = w, y0 = h, x1 = -1, y1 = -1, worst = 0;
    for (int y = 0; y < h; ++y) {
        const uint8_t* ra = a + size_t(y) * size_t(pitchA);
        const uint8_t* rb = b + size_t(y) * size_t(pitchB);
        for (int x = 0; x < w; ++x) {
            int d = 0;
            for (int k = 0; k < 4; ++k) d = std::max(d, std::abs(int(ra[x * 4 + k]) - int(rb[x * 4 + k])));
            if (d == 0) continue;
            ++count, worst = std::max(worst, d);
            x0 = std::min(x0, x), y0 = std::min(y0, y), x1 = std::max(x1, x), y1 = std::max(y1, y);
        }
    }
    if (count) {
        ++app.damageWrong;
        float s = app.pixelScale();
        SDL_Log("kompoz: damage check: %d pixels differ (by up to %d) in %d,%d %dx%d px; drawn again: %.0f,%.0f %.0fx%.0f px",
                count, worst, x0, y0, x1 - x0 + 1, y1 - y0 + 1, damage.x * s, damage.y * s, damage.width * s, damage.height * s);
    }
}

void checkDamage(App& app, const Rect& damage, const DrawInto& drawInto) {
    SDL_Texture* full = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, app.frameW, app.frameH);
    if (!full) return;
    drawInto(full, std::nullopt);
    auto read = [&](SDL_Texture* t) {
        SDL_SetRenderTarget(app.renderer, t);
        SDL_Surface* pixels = SDL_RenderReadPixels(app.renderer, nullptr);
        SDL_SetRenderTarget(app.renderer, nullptr);
        return pixels;
    };
    SDL_Surface *kept = read(app.frame), *fresh = read(full);
    if (kept && fresh && kept->w == fresh->w && kept->h == fresh->h)
        compareDamage(app, damage, static_cast<const uint8_t*>(kept->pixels), kept->pitch, static_cast<const uint8_t*>(fresh->pixels),
                      fresh->pitch, kept->w, kept->h);
    SDL_DestroySurface(kept);
    SDL_DestroySurface(fresh);
    SDL_DestroyTexture(full);
}

// A dropped file's path as a file: URI ("text/uri-list"): Windows' backslashes and drive letters
// turned around, anything but letters, digits and -._~/ percent-encoded.
std::string fileUri(std::string path) {
    std::replace(path.begin(), path.end(), '\\', '/');
    if (path.size() > 1 && path[1] == ':') path.insert(path.begin(), '/');
    std::string out = "file://";
    for (unsigned char c : path) {
        if (std::isalnum(c) || c == '-' || c == '.' || c == '_' || c == '~' || c == '/' || c == ':') out += char(c);
        else out += std::format("%{:02X}", c);
    }
    return out;
}

// Draws the frame: recorded, then what changed drawn again into the kept pixels, which are put
// in the window if anything changed (or for a screenshot, or when the window lost them).
struct Drawn {
    bool shown = false;  // to present
    float area = 0;      // drawn again, of the screen (-1: all of it, straight; render targets are missing)
};
// A screenshot from pixels read back (RGBA, top row first).
void savePixels(const std::string& path, int w, int h, std::vector<uint8_t>& pixels) {
    SDL_Surface* surface = SDL_CreateSurfaceFrom(w, h, SDL_PIXELFORMAT_RGBA32, pixels.data(), w * 4);
    if (!surface) return;
    bool ok;
#if SDL_VERSION_ATLEAST(3, 4, 0)
    ok = path.ends_with(".png") ? SDL_SavePNG(surface, path.c_str()) : SDL_SaveBMP(surface, path.c_str());
#else
    ok = SDL_SaveBMP(surface, path.c_str());
#endif
    SDL_Log("kompoz: screenshot %s %s", path.c_str(), ok ? "saved" : SDL_GetError());
    SDL_DestroySurface(surface);
}

// drawFrame on the GPU renderer: the same, its kept frame a texture drawn into in a frame of the
// device's (begun by the caller), copied onto the swapchain image.
Drawn drawFrameGpu(App& app, const std::string& shot) {
    gpu::Device& device = *app.gpu;
    gpu::Renderer& r = *app.gpuCanvas;
    int pw = 0, ph = 0;
    SDL_GetWindowSizeInPixels(app.window, &pw, &ph);
    pw = std::max(pw, 1), ph = std::max(ph, 1);
    if (!app.gpuFrame || app.gpuFrame->width() != pw || app.gpuFrame->height() != ph) {
        app.gpuFrame = device.createTexture(pw, ph, gpu::Format::RGBA8, gpu::TextureUsage::Sampled | gpu::TextureUsage::RenderTarget);
        app.display->invalidate();
        app.showFrame = true;
    }
    r.layerCount = 0;
    float scale = r.scale;
    Size screen{float(pw) / scale, float(ph) / scale};
    app.display->begin(screen, 1 / scale);
    app.host->draw(*app.display);
    if (app.fullRedraw) app.display->invalidate();
    std::optional<Rect> damage = app.display->end();
    // Drawn again: the area made white, then what touches it, clipped to it.
    auto drawInto = [&](gpu::Texture& target, const std::optional<Rect>& area) {
        r.beginTarget(target);
        if (area) r.pushClip(*area);
        r.fillOpaque({0, 0, target.width(), target.height()}, colors::White);
        app.display->replay(r, area);
        if (area) r.popClip();
        r.endTarget();
    };
    if (damage) drawInto(*app.gpuFrame, damage);
    if (damage && app.damageCheck) {
        auto full = device.createTexture(pw, ph, gpu::Format::RGBA8, gpu::TextureUsage::Sampled | gpu::TextureUsage::RenderTarget);
        drawInto(*full, std::nullopt);
        r.flush();
        std::vector<uint8_t> kept = device.readPixels(*app.gpuFrame, {0, 0, pw, ph}), fresh = device.readPixels(*full, {0, 0, pw, ph});
        if (kept.size() == fresh.size() && !kept.empty()) compareDamage(app, *damage, kept.data(), pw * 4, fresh.data(), pw * 4, pw, ph);
    }
    float area = damage ? std::max(1e-4f, damage->width * damage->height / (screen.width * screen.height)) : 0;
    if (!damage && shot.empty() && !app.showFrame) {
        r.flush();
        return {false, 0};
    }
    gpu::Texture* swapchain = device.swapchainTexture();
    if (swapchain) {
        app.showFrame = false;
        r.beginTarget(*swapchain);
        r.blit(*app.gpuFrame);
        if (damage && app.damageShow)  // what was drawn again, outlined (only in the window, not the kept pixels)
            r.strokeRoundRect(*damage, CornerRadii(), 3 / scale, Color{1, 0, 80 / 255.f, 1});
        r.endTarget();
    }
    r.flush();
    if (!shot.empty()) {
        std::vector<uint8_t> pixels = device.readPixels(*app.gpuFrame, {0, 0, pw, ph});
        savePixels(shot, pw, ph, pixels);
    }
    return {swapchain != nullptr, area};
}

Drawn drawFrame(App& app, const std::string& shot) {
    if (app.gpuCanvas) return drawFrameGpu(app, shot);
    int pw = 0, ph = 0;
    SDL_GetRenderOutputSize(app.renderer, &pw, &ph);
    if (!app.frame || app.frameW != pw || app.frameH != ph) {
        if (app.frame) SDL_DestroyTexture(app.frame);
        app.frame = SDL_CreateTexture(app.renderer, SDL_PIXELFORMAT_RGBA32, SDL_TEXTUREACCESS_TARGET, std::max(pw, 1), std::max(ph, 1));
        app.frameW = pw, app.frameH = ph;
        if (app.frame) SDL_SetTextureBlendMode(app.frame, SDL_BLENDMODE_NONE);
        if (app.display) app.display->invalidate();
        app.showFrame = true;
    }
    app.canvas->layerCount = 0;
    if (!app.frame) {  // no render targets: all of it, straight to the window
        SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
        SDL_RenderClear(app.renderer);
        app.host->draw(*app.canvas);
        if (!shot.empty()) saveScreenshot(app.renderer, shot);
        return {true, -1};
    }
    float scale = app.canvas->scale;
    Size screen{float(pw) / scale, float(ph) / scale};
    app.display->begin(screen, 1 / scale);
    app.host->draw(*app.display);
    if (app.fullRedraw) app.display->invalidate();
    std::optional<Rect> damage = app.display->end();
    // Drawn again: the area cleared, then what touches it, clipped to it.
    DrawInto drawInto = [&](SDL_Texture* target, const std::optional<Rect>& area) {
        SDL_SetRenderTarget(app.renderer, target);
        if (area) app.canvas->pushClip(*area);
        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_NONE);
        SDL_SetRenderDrawColor(app.renderer, 255, 255, 255, 255);
        SDL_RenderFillRect(app.renderer, nullptr);
        SDL_SetRenderDrawBlendMode(app.renderer, SDL_BLENDMODE_BLEND);
        app.display->replay(*app.canvas, area);
        if (area) app.canvas->popClip();
        SDL_SetRenderTarget(app.renderer, nullptr);
    };
    if (damage) drawInto(app.frame, damage);
    if (damage && app.damageCheck) checkDamage(app, *damage, drawInto);
    float area = damage ? std::max(1e-4f, damage->width * damage->height / (screen.width * screen.height)) : 0;
    if (!damage && shot.empty() && !app.showFrame) return {false, 0};
    app.showFrame = false;
    SDL_RenderTexture(app.renderer, app.frame, nullptr, nullptr);
    if (damage && app.damageShow) {  // what was drawn again, outlined (only in the window, not the kept pixels)
        SDL_SetRenderDrawColor(app.renderer, 255, 0, 80, 255);
        SDL_FRect r{damage->x * scale, damage->y * scale, damage->width * scale, damage->height * scale};
        for (int i = 0; i < 3; ++i, r = {r.x + 1, r.y + 1, r.w - 2, r.h - 2}) SDL_RenderRect(app.renderer, &r);
    }
    if (!shot.empty()) saveScreenshot(app.renderer, shot, app.frame);
    return {true, area};
}

void profileFrame(App& app, uint64_t start, uint64_t composed, uint64_t drawn, uint64_t presented, int glyphs, float area) {
    auto ms = [](uint64_t ns) { return double(ns) / 1e6; };
    App::Profile& p = app.prof;
    double interval = p.lastStart ? ms(start - p.lastStart) : 0;
    double events = ms(p.eventsNs), compose = ms(composed - start), draw = ms(drawn - composed), present = ms(presented - drawn);
    double work = events + compose + draw;
    p.lastStart = start;
    p.eventsNs = 0;
    if (!p.summaryStart) p.summaryStart = start;
    ++p.frames;
    p.workTotal += work;
    p.workMax = std::max(p.workMax, work);
    p.intervalMax = std::max(p.intervalMax, interval);
    if (area == 0) ++p.unchanged;
    p.drawnArea += area < 0 ? 1 : area;
    if (work > 8 || interval > 34) {
        ++p.slow;
        SDL_Log("kompoz: frame %.1f ms after the last: input %.1f, compose+layout %.1f, draw %.1f, present %.1f ms; %d layers, %d glyphs, "
                "%.0f%% drawn again",
                interval, events, compose, draw, present, app.gpuCanvas ? app.gpuCanvas->layerCount : app.canvas->layerCount, glyphs,
                (area < 0 ? 1 : area) * 100);
    }
    if (start - p.summaryStart >= 1'000'000'000) {
        SDL_Log("kompoz: %d frames in %.0f ms (%d changed nothing on the screen; %.0f%% of it drawn again on average), work avg %.1f "
                "max %.1f ms, longest gap %.1f ms, %d slow",
                p.frames, ms(start - p.summaryStart), p.unchanged, p.drawnArea / p.frames * 100, p.workTotal / p.frames, p.workMax,
                p.intervalMax, p.slow);
        p = {};
        p.lastStart = p.summaryStart = start;
    }
}

std::string stepScript(App& app) {
    Script& s = app.script;
    ComposeHost& host = *app.host;
    if (s.swipeStep < s.swipeFrames) {
        float t = float(++s.swipeStep) / float(s.swipeFrames);
        Offset p{lerp(s.swipeFrom.x, s.swipeTo.x, t), lerp(s.swipeFrom.y, s.swipeTo.y, t)};
        host.pointerMove(p, s.clock);
        if (s.swipeStep == s.swipeFrames) host.pointerUp(p, s.clock);
        return {};
    }
    if (s.waitFrames > 0) {
        --s.waitFrames;
        return {};
    }
    if (s.sleepUntilNs > 0) {
        if (SDL_GetTicksNS() < s.sleepUntilNs) {
            SDL_DelayNS(2'000'000);
            return {};
        }
        s.sleepUntilNs = 0;
    }
    while (s.next < s.commands.size()) {
        std::istringstream in(s.commands[s.next++]);
        std::string command;
        in >> command;
        float x = 0, y = 0, x2 = 0, y2 = 0;
        if (command == "wait") {
            in >> s.waitFrames;
            return {};
        } else if (command == "sleep") {  // real milliseconds: what other threads do (an image read, the wallpaper's colors)
            int ms = 0;
            in >> ms;
            s.sleepUntilNs = SDL_GetTicksNS() + uint64_t(std::max(ms, 0)) * 1'000'000;
            return {};
        } else if (command == "click") {
            in >> x >> y;
            host.pointerMove({x, y});
            host.pointerDown({x, y});
            host.pointerUp({x, y});
        } else if (command == "rclick") {  // a right click: context menus
            in >> x >> y;
            host.mouseButton({x, y}, PointerButton::Secondary, true);
            host.secondaryClick({x, y});
            host.mouseButton({x, y}, PointerButton::Secondary, false);
        } else if (command == "mclick") {  // a middle click
            in >> x >> y;
            host.mouseButton({x, y}, PointerButton::Tertiary, true);
            host.mouseButton({x, y}, PointerButton::Tertiary, false);
        } else if (command == "down") {  // down/up: a press held across waits (long press)
            in >> x >> y;
            host.pointerDown({x, y}, s.clock);
        } else if (command == "up") {
            in >> x >> y;
            host.pointerUp({x, y}, s.clock);
        } else if (command == "move") {
            in >> x >> y;
            host.pointerMove({x, y});
        } else if (command == "fdown" || command == "fmove" || command == "fup") {  // fdown id x y: multi-touch
            int64_t id = 0;
            in >> id >> x >> y;
            if (command == "fdown") host.pointerDown({x, y}, s.clock, id);
            if (command == "fmove") host.pointerMove({x, y}, s.clock, id);
            if (command == "fup") host.pointerUp({x, y}, s.clock, id);
        } else if (command == "drag" || command == "mdrag") {  // mdrag: with the mouse
            in >> x >> y >> x2 >> y2;
            host.pointerDown({x, y}, -1, 0, command == "mdrag" ? ComposeHost::PointerType::Mouse : ComposeHost::PointerType::Touch);
            for (int i = 1; i <= 10; ++i) host.pointerMove({lerp(x, x2, i / 10.f), lerp(y, y2, i / 10.f)});
            host.pointerUp({x2, y2});
        } else if (command == "swipe") {  // swipe x1 y1 x2 y2 frames
            int frames = 10;
            in >> x >> y >> x2 >> y2 >> frames;
            s.swipeFrom = {x, y};
            s.swipeTo = {x2, y2};
            s.swipeFrames = std::max(1, frames);
            s.swipeStep = 0;
            host.pointerDown(s.swipeFrom, s.clock);
            return {};
        } else if (command == "wheel") {
            float delta = 0;
            in >> x >> y >> delta;
            host.scroll({x, y}, delta);
        } else if (command == "dropfile" || command == "droptext") {  // dropfile x y path: as a file manager's drag would
            std::string what;
            in >> x >> y;
            std::getline(in >> std::ws, what);
            s.dropped.push_back(what);
            float k = 1 / app.pointToDp;  // dp -> window points
            auto push = [&](SDL_EventType type, const char* data) {
                SDL_Event e{};
                e.drop.type = type;
                e.drop.x = x * k;
                e.drop.y = y * k;
                e.drop.data = data;
                SDL_PushEvent(&e);
            };
            push(SDL_EVENT_DROP_BEGIN, nullptr);
            push(SDL_EVENT_DROP_POSITION, nullptr);
            push(command == "dropfile" ? SDL_EVENT_DROP_FILE : SDL_EVENT_DROP_TEXT, s.dropped.back().c_str());
            push(SDL_EVENT_DROP_COMPLETE, nullptr);
        } else if (command == "window") {  // window focuslost|focusgained|minimized|restored: as the window manager would
            std::string what;
            in >> what;
            static const std::pair<std::string_view, SDL_EventType> names[] = {
                {"focuslost", SDL_EVENT_WINDOW_FOCUS_LOST}, {"focusgained", SDL_EVENT_WINDOW_FOCUS_GAINED},
                {"minimized", SDL_EVENT_WINDOW_MINIMIZED}, {"restored", SDL_EVENT_WINDOW_RESTORED}};
            for (const auto& [n, type] : names)
                if (what == n) {
                    SDL_Event e{};
                    e.window.type = type;
                    e.window.windowID = SDL_GetWindowID(app.window);
                    SDL_PushEvent(&e);
                }
        } else if (command == "zoom") {  // zoom x y factor: Ctrl+wheel or a touchpad pinch
            float factor = 1;
            in >> x >> y >> factor;
            host.zoom({x, y}, factor);
        } else if (command == "type") {
            std::string text;
            std::getline(in >> std::ws, text);
            host.textInput(text);
        } else if (command == "key") {
            std::string name;
            in >> name;
            static const std::pair<std::string_view, Key> names[] = {
                {"enter", Key::Enter}, {"backspace", Key::Backspace}, {"escape", Key::Escape}, {"tab", Key::Tab},
                {"space", Key::Space}, {"up", Key::Up},       {"down", Key::Down},     {"left", Key::Left},
                {"right", Key::Right}, {"home", Key::Home},   {"end", Key::End},       {"delete", Key::Delete},
                {"pageup", Key::PageUp}, {"pagedown", Key::PageDown}};
            // ctrl+, shift+ and alt+ in front, any order; then a name, a letter, a digit or f1..f12.
            KeyModifiers mods;
            std::string_view bare = name;
            for (bool more = true; more;) {
                more = false;
                for (auto [prefix, flag] : {std::pair<std::string_view, bool*>{"ctrl+", &mods.ctrl}, {"shift+", &mods.shift}, {"alt+", &mods.alt}})
                    if (bare.starts_with(prefix)) {
                        *flag = true;
                        bare.remove_prefix(prefix.size());
                        more = true;
                    }
            }
            std::optional<Key> key;
            for (const auto& [n, k] : names)
                if (bare == n) key = k;
            if (bare.size() == 1 && bare[0] >= 'a' && bare[0] <= 'z') key = Key(int(Key::A) + (bare[0] - 'a'));
            if (bare.size() == 1 && bare[0] >= '0' && bare[0] <= '9') key = Key(int(Key::Zero) + (bare[0] - '0'));
            if (bare.size() >= 2 && bare[0] == 'f') {
                int n = std::atoi(std::string(bare.substr(1)).c_str());
                if (n >= 1 && n <= 12) key = Key(int(Key::F1) + n - 1);
            }
            if (key) {
                host.keyDown(*key, mods);
                host.keyUp(*key, mods);
            }
        } else if (command == "resize") {  // resize w h (window points)
            int w = 0, h = 0;
            in >> w >> h;
            if (w > 0 && h > 0) SDL_SetWindowSize(app.window, w, h);
            updateMetrics(app);
        } else if (command == "shot") {
            std::string path;
            in >> path;
            return path;
        } else if (command == "quit") {
            app.quit = true;
            return {};
        } else if (!command.empty()) {
            SDL_Log("kompoz: unknown script command '%s'", command.c_str());
        }
    }
    return {};
}

// The app's lifecycle on desktops and in browsers: resumed while the window has the focus,
// started while it is on the screen without it, created while it is minimized or hidden.
// (Phones follow the app going to the background and back instead.)
void updateLifecycle(App& app) {
#if !defined(SDL_PLATFORM_ANDROID) && !defined(SDL_PLATFORM_IOS)
    app.host->setLifecycleState(app.minimized || app.hidden ? LifecycleState::Created
                                : app.focused                ? LifecycleState::Resumed
                                                             : LifecycleState::Started);
#else
    (void)app;
#endif
}

// The mouse's buttons other than the left one, as onPointerEvent knows them.
std::optional<PointerButton> otherButton(Uint8 button) {
    switch (button) {
    case SDL_BUTTON_RIGHT: return PointerButton::Secondary;
    case SDL_BUTTON_MIDDLE: return PointerButton::Tertiary;
    case SDL_BUTTON_X1: return PointerButton::Back;
    case SDL_BUTTON_X2: return PointerButton::Forward;
    default: return std::nullopt;
    }
}

void handleEvent(App& app, const SDL_Event& e) {
    ComposeHost& host = *app.host;
#ifdef KOMPOZ_DESKTOP_ACCENT
    if (app.accent && app.accent->eventType() != 0 && e.type == app.accent->eventType()) {  // learned or changed
        std::optional<Color> accent = app.accent->accent();
        host.setDynamicTonalPalette(accent ? std::optional(TonalPalette::fromSeed(*accent)) : std::nullopt);
        return;
    }
#endif
#ifdef KOMPOZ_WALLPAPER_COLORS
    if (app.wallpaper && app.wallpaper->eventType() != 0 && e.type == app.wallpaper->eventType()) {  // read or changed
        host.setWallpaperTonalPalette(app.wallpaper->palette());
        return;
    }
#endif
#ifdef KOMPOZ_A11Y_BRIDGE
    if (app.a11y && app.a11y->eventType() != 0 && e.type == app.a11y->eventType()) {  // asked for, or connected
        for (const auto& r : app.a11y->takeRequests()) {
            if (r.action == kompoz::SemanticsAction::SetText) host.performSemanticsAction(r.id, r.action, r.text);
            else host.performSemanticsAction(r.id, r.action, r.argument);
        }
        return;
    }
#endif
    switch (e.type) {  // pointer events carry the keys held down
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
    case SDL_EVENT_MOUSE_MOTION:
    case SDL_EVENT_MOUSE_WHEEL:
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        SDL_Keymod m = SDL_GetModState();
        host.setKeyboardModifiers({(m & SDL_KMOD_SHIFT) != 0, (m & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, (m & SDL_KMOD_ALT) != 0});
        break;
    }
    default: break;
    }
    switch (e.type) {
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        app.focused = e.type == SDL_EVENT_WINDOW_FOCUS_GAINED;
        app.semanticsDirty = true;  // (AT-SPI tells which window is active)
        host.setWindowFocused(app.focused);
        updateLifecycle(app);
        break;
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_MAXIMIZED:
        app.minimized = e.type == SDL_EVENT_WINDOW_MINIMIZED;
        updateLifecycle(app);
        break;
    case SDL_EVENT_WINDOW_HIDDEN:
        app.hidden = true;
        updateLifecycle(app);
        break;
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
    case SDL_EVENT_DID_ENTER_BACKGROUND:
        host.setLifecycleState(LifecycleState::Created);
        break;
    case SDL_EVENT_WILL_ENTER_FOREGROUND:
        host.setLifecycleState(LifecycleState::Started);
        break;
#endif
    case SDL_EVENT_MOUSE_ADDED:  // what mediaQuery tells of the pointer and the keyboard
    case SDL_EVENT_MOUSE_REMOVED:
    case SDL_EVENT_KEYBOARD_ADDED:
    case SDL_EVENT_KEYBOARD_REMOVED:
        refreshMedia(app);
        break;
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED:
        app.resizeFrames = 4;
        [[fallthrough]];
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
    case SDL_EVENT_SYSTEM_THEME_CHANGED:
    case SDL_EVENT_LOCALE_CHANGED:
        updateMetrics(app);
        break;
    case SDL_EVENT_RENDER_DEVICE_RESET:
        if (app.canvas) app.canvas->reset();  // (the atlas's pages are kept on the CPU: uploaded again)
        if (app.frame) SDL_DestroyTexture(app.frame);
        app.frame = nullptr;
        host.requestRedraw();
        break;
    case SDL_EVENT_DID_ENTER_FOREGROUND:
        updateMetrics(app);  // the wallpaper (and so the dynamic colors) may have changed meanwhile
#if defined(SDL_PLATFORM_ANDROID) || defined(SDL_PLATFORM_IOS)
        host.setLifecycleState(LifecycleState::Resumed);
#endif
        [[fallthrough]];
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_RESTORED:
        if (e.type == SDL_EVENT_WINDOW_SHOWN) app.hidden = false;
        if (e.type == SDL_EVENT_WINDOW_RESTORED) app.minimized = false;
        updateLifecycle(app);
        // Back on the screen: Android gives the window a new, empty surface, which shows nothing
        // until a frame is put in it, and with nothing changed none would be (black until a tap).
        // All of it drawn again, in case the kept pixels went with the old one.
        if (app.display) app.display->invalidate();
        app.showFrame = true;
        host.requestRedraw();
        break;
#ifdef SDL_PLATFORM_ANDROID
    case SDL_EVENT_WILL_ENTER_BACKGROUND:  // on the SDL thread, between frames: Android may end the app from now on
        host.setLifecycleState(LifecycleState::Started);  // paused: what that saves goes in the saved state
        android_ime::setSavedState(host.saveState());
        break;
#endif
    case SDL_EVENT_WINDOW_EXPOSED:
        app.showFrame = true;
        host.requestRedraw();
        break;
    case SDL_EVENT_RENDER_TARGETS_RESET:  // the kept pixels are gone
        if (app.display) app.display->invalidate();
        app.showFrame = true;
        host.requestRedraw();
        break;
    case SDL_EVENT_WINDOW_MOUSE_LEAVE:
        host.pointerExit();
        break;
    // Touch screens send fingers (each with its own id, for pinch and twist) and SDL also
    // makes up mouse events from them: those are left out.
    case SDL_EVENT_MOUSE_MOTION:
        if (e.motion.which == SDL_TOUCH_MOUSEID) break;
        host.pointerMove(app.toDp(e.motion.x, e.motion.y), app.eventTime(e.motion.timestamp));
        break;
    // Files from a file manager, text from a browser: the dragAndDropTargets take them.
    case SDL_EVENT_DROP_BEGIN:
        app.dropping = true;
        app.dropFiles.clear();
        app.dropText.clear();
        host.externalDragEntered({"text/uri-list", "text/plain"});  // which one isn't told until the drop
        break;
    case SDL_EVENT_DROP_POSITION:
        if (!app.dropping) {
            app.dropping = true;
            host.externalDragEntered({"text/uri-list", "text/plain"});
        }
        host.externalDragMoved(app.toDp(e.drop.x, e.drop.y));
        break;
    case SDL_EVENT_DROP_FILE:
        if (e.drop.data) app.dropFiles.emplace_back(e.drop.data);
        app.dropAt = app.toDp(e.drop.x, e.drop.y);
        break;
    case SDL_EVENT_DROP_TEXT:
        if (e.drop.data) app.dropText += e.drop.data;
        app.dropAt = app.toDp(e.drop.x, e.drop.y);
        break;
    case SDL_EVENT_DROP_COMPLETE: {
        app.dropping = false;
        kompoz::DragAndDropTransferData data;
        if (!app.dropFiles.empty()) {
            data.mimeTypes = {"text/uri-list"};
            for (const std::string& path : app.dropFiles) data.text += fileUri(path) + "\r\n";
            data.files = std::move(app.dropFiles);
        } else if (!app.dropText.empty()) {
            data.mimeTypes = {"text/plain"};
            data.text = std::move(app.dropText);
        } else {
            host.externalDragLeft();  // it left the window, or brought nothing
            break;
        }
        host.externalDrop(std::move(data), app.dropAt);
        app.dropFiles.clear();
        app.dropText.clear();
        break;
    }
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
        if (e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID)
            host.pointerDown(app.toDp(e.button.x, e.button.y), app.eventTime(e.button.timestamp), 0, kompoz::ComposeHost::PointerType::Mouse);
        else if (auto button = otherButton(e.button.button); button && e.button.which != SDL_TOUCH_MOUSEID) {
            host.mouseButton(app.toDp(e.button.x, e.button.y), *button, true);
            if (*button == PointerButton::Secondary) host.secondaryClick(app.toDp(e.button.x, e.button.y));  // context menus
        }
        break;
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.button == SDL_BUTTON_LEFT && e.button.which != SDL_TOUCH_MOUSEID)
            host.pointerUp(app.toDp(e.button.x, e.button.y), app.eventTime(e.button.timestamp));
        else if (auto button = otherButton(e.button.button); button && e.button.which != SDL_TOUCH_MOUSEID)
            host.mouseButton(app.toDp(e.button.x, e.button.y), *button, false);
        break;
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        if (SDL_GetTouchDeviceType(e.tfinger.touchID) != SDL_TOUCH_DEVICE_DIRECT) break;  // touchpads move the mouse
        int w = 0, h = 0;  // finger positions are fractions of the window
        SDL_GetWindowSize(app.window, &w, &h);
        Offset p = app.toDp(e.tfinger.x * float(w), e.tfinger.y * float(h));
        auto id = int64_t(e.tfinger.fingerID);
        auto time = app.eventTime(e.tfinger.timestamp);
        if (e.type == SDL_EVENT_FINGER_DOWN) {
            host.pointerDown(p, time, id);
        } else if (e.type == SDL_EVENT_FINGER_MOTION) {
            host.pointerMove(p, time, id);
        } else {
            host.pointerUp(p, time, id);
            host.pointerExit();  // a lifted finger doesn't hover
        }
        break;
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        float flip = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f;
        Offset p = app.toDp(e.wheel.mouse_x, e.wheel.mouse_y);
        // Ctrl+wheel zooms (a transformable), as touchpad pinches do on Windows and the web.
        bool ctrl = (SDL_GetModState() & SDL_KMOD_CTRL) != 0;
#ifdef SDL_PLATFORM_EMSCRIPTEN
        ctrl = ctrl || kompoz_web_wheel_ctrl();
#endif
        bool shift = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
        if (ctrl && shift && host.zoom(p, 1, -15 * e.wheel.y * flip)) break;  // Ctrl+Shift turns it, 15 degrees a notch
        if (ctrl && host.zoom(p, std::pow(1.1f, e.wheel.y * flip))) break;
        host.scroll(p, e.wheel.y * flip, e.wheel.x * flip);
        break;
    }
    case SDL_EVENT_PINCH_BEGIN:
        app.pinchScale = 1;
        break;
    case SDL_EVENT_PINCH_UPDATE: {  // a touchpad pinch (Wayland, macOS): around the mouse pointer
        float factor = e.pinch.scale;
        // Wayland gives the scale since the pinch began, which SDL passes on as the change since
        // the last update: multiplied up, the smallest finger movement zoomed a lot.
        if (app.pinchSinceStart) {
            factor = e.pinch.scale / std::max(app.pinchScale, 1e-3f);
            app.pinchScale = e.pinch.scale;
        }
        float x = 0, y = 0;
        SDL_GetMouseState(&x, &y);
        host.zoom(app.toDp(x, y), factor);
        break;
    }
    case SDL_EVENT_TEXT_INPUT:
        host.textInput(e.text.text);
        break;
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP: {
        KeyModifiers mods{(e.key.mod & SDL_KMOD_SHIFT) != 0, (e.key.mod & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0,
                          (e.key.mod & SDL_KMOD_ALT) != 0};
        if (e.key.key == SDLK_AC_BACK) {  // Android's back button
            if (e.type == SDL_EVENT_KEY_UP) break;
#ifdef SDL_PLATFORM_ANDROID
            // From Android 13 back comes as the app's OnBackInvokedCallback while it takes back
            // (KompozInput.setBackEnabled), and the system goes back itself otherwise. Taken here
            // too, the key would do it twice: a drawer closed by it, its callback gone by the
            // time the key is let go, and the system then leaving the app.
            if (SDL_GetAndroidSDKVersion() >= 33) break;
            if (!host.keyDown(Key::Escape)) SDL_SendAndroidBackButton();  // leave the app as usual
#else
            host.keyDown(Key::Escape);
#endif
            break;
        }
        Key key = keyOf(e.key.key);
        if (key == Key::Unknown) break;
        if (e.type == SDL_EVENT_KEY_DOWN)
            host.keyDown(key, mods);
        else
            host.keyUp(key, mods);
        break;
    }
    default:
        if (e.type == app.wakeEvent) app.wakePending = false;
        break;
    }
}

} // namespace
} // namespace kompoz::sdl

using kompoz::sdl::App;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    auto app = std::make_unique<App>();
    app->config = kompoz::createApplication(argc, argv);
    if (const char* preview = SDL_getenv("KOMPOZ_PREVIEW")) kompoz::detail::showPreview(app->config, preview);  // a preview instead of the app
    SDL_SetAppMetadata(app->config.title.c_str(), "0.1", "dev.kompoz.app");
    kompoz::sdl::startImageDiskCache(app->config.title, app->config.imageDiskCacheBytes);
    // Back comes to the app first (popups, BackHandler); unused presses go back to Android.
    SDL_SetHint(SDL_HINT_ANDROID_TRAP_BACK_BUTTON, "1");
    // The screen may go dark as usual (SDL keeps it on by default); Modifier::keepScreenOn
    // keeps it on while it is laid out.
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("kompoz: SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
#ifdef SDL_PLATFORM_ANDROID
    // No environment there: a file in the app's own storage turns the frame profile on
    //   adb shell run-as <package> touch files/kompoz-profile
    // or, where run-as is refused (SELinux), in its external storage, which the app creates:
    //   adb shell touch /sdcard/Android/data/<package>/files/kompoz-profile
    for (const char* dir : {SDL_GetAndroidInternalStoragePath(), SDL_GetAndroidExternalStoragePath()})
        if (dir) app->profile = app->profile || SDL_GetPathInfo((std::string(dir) + "/kompoz-profile").c_str(), nullptr);
#endif
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    const char* gpuChoice = SDL_getenv("KOMPOZ_GPU");  // vulkan: Kompoz's GPU renderer
    bool useGpu = gpuChoice && std::string_view(gpuChoice) == "vulkan";
#ifdef KOMPOZ_GPU_VULKAN
    if (useGpu) flags |= SDL_WINDOW_VULKAN;
#else
    if (useGpu) {
        SDL_Log("kompoz: KOMPOZ_GPU=%s: this build has no Vulkan backend", gpuChoice);
        useGpu = false;
    }
#endif
#ifdef SDL_PLATFORM_EMSCRIPTEN
    flags |= SDL_WINDOW_FILL_DOCUMENT;  // the whole page, resized with the browser window
    kompoz_web_watch_wheel();
#endif
    SDL_PropertiesID windowProps = SDL_CreateProperties();
    SDL_SetStringProperty(windowProps, SDL_PROP_WINDOW_CREATE_TITLE_STRING, app->config.title.c_str());
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, app->config.width);
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, app->config.height);
    SDL_SetNumberProperty(windowProps, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, static_cast<Sint64>(flags));
    // On the offscreen driver the window starts without a graphics API, as it does elsewhere: on
    // macOS SDL would make it an OpenGL window, which offscreen means EGL, which macOS hasn't, so
    // even the software renderer couldn't start. A GL renderer still makes it an OpenGL window.
    if (const char* driver = SDL_GetCurrentVideoDriver(); driver && std::string_view(driver) == "offscreen" && !useGpu)
        SDL_SetBooleanProperty(windowProps, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);
    app->window = SDL_CreateWindowWithProperties(windowProps);
    SDL_DestroyProperties(windowProps);
    if (!app->window) {
        SDL_Log("kompoz: SDL_CreateWindow failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    // Window sizes are in points; make the initial size match the requested dp.
    float pointsPerDp = SDL_GetWindowDisplayScale(app->window) / SDL_GetWindowPixelDensity(app->window);
    if (std::isfinite(pointsPerDp) && std::abs(pointsPerDp - 1) > 0.01f)
        SDL_SetWindowSize(app->window, int(std::lround(app->config.width * pointsPerDp)),
                          int(std::lround(app->config.height * pointsPerDp)));

    if (const char* driver = SDL_GetCurrentVideoDriver()) app->pinchSinceStart = std::string_view(driver) == "wayland";
    if (useGpu) {
#ifdef KOMPOZ_GPU_VULKAN
        kompoz::gpu::setLogger([](const char* message) { SDL_Log("%s", message); });
        kompoz::gpu::Surface surface;
        Uint32 count = 0;
        if (const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count)) surface.vulkanInstanceExtensions.assign(names, names + count);
        surface.vulkanGetInstanceProcAddr = reinterpret_cast<void* (*)(void*, const char*)>(SDL_Vulkan_GetVkGetInstanceProcAddr());
        SDL_Window* window = app->window;
        surface.createVulkanSurface = [window](void* instance, uint64_t* out) {
            VkSurfaceKHR made{};
            if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(instance), nullptr, &made)) return false;
            std::memcpy(out, &made, sizeof made);
            return true;
        };
        surface.drawableSize = [window](int* w, int* h) { SDL_GetWindowSizeInPixels(window, w, h); };
        std::string error;
        app->gpu = kompoz::gpu::Device::create(kompoz::gpu::Backend::Vulkan, surface, &error);
        if (!app->gpu) {
            SDL_Log("kompoz: no Vulkan device: %s", error.c_str());
            return SDL_APP_FAILURE;
        }
        SDL_Log("kompoz: renderer: Kompoz on %s (%s)", kompoz::gpu::backendName(app->gpu->backend()), app->gpu->deviceName().c_str());
#endif
    } else {
        app->renderer = SDL_CreateRenderer(app->window, nullptr);
        if (!app->renderer) {
            SDL_Log("kompoz: SDL_CreateRenderer failed: %s", SDL_GetError());
            return SDL_APP_FAILURE;
        }
        SDL_Log("kompoz: renderer: %s", SDL_GetRendererName(app->renderer));
        SDL_SetRenderVSync(app->renderer, 1);
        SDL_SetRenderDrawBlendMode(app->renderer, SDL_BLENDMODE_BLEND);
    }

    app->fonts = std::make_unique<kompoz::sdl::FontSystem>();
    if (!app->fonts->load(kompoz::FontWeight::Normal, app->config.fontPath)) {
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kompoz", "No usable font found. Set KOMPOZ_FONT.", app->window);
        return SDL_APP_FAILURE;
    }
    app->fonts->load(kompoz::FontWeight::Bold, app->config.boldFontPath);  // optional
    app->fonts->load(kompoz::FontWeight::Medium, app->config.mediumFontPath);  // optional
    if (app->gpu)
        app->gpuCanvas = std::make_unique<kompoz::gpu::Renderer>(*app->gpu, *app->fonts);
    else
        app->canvas = std::make_unique<kompoz::sdl::SdlCanvas>(app->renderer, *app->fonts);
    app->display = std::make_unique<kompoz::DisplayList>(*app->fonts);
    app->host = std::make_unique<kompoz::ComposeHost>(*app->fonts, app->config.content);
#ifdef KOMPOZ_DESKTOP_ACCENT
    app->accent = std::make_unique<kompoz::sdl::DesktopAccent>();  // tells its color as an event, soon
#endif
#ifdef KOMPOZ_WALLPAPER_COLORS
    app->wallpaper = std::make_unique<kompoz::sdl::WallpaperColors>(SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);
#endif
#ifdef KOMPOZ_ATSPI
    app->a11y = std::make_unique<kompoz::sdl::AtspiBridge>(app->config.title);  // connects on its thread, if wanted
#elif defined(KOMPOZ_UIA)
    app->a11y = std::make_unique<kompoz::sdl::UiaBridge>(
        SDL_GetPointerProperty(SDL_GetWindowProperties(app->window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr));
#endif
    app->wakeEvent = SDL_RegisterEvents(1);

    App* raw = app.get();
    const SDL_SystemCursor systemCursors[] = {SDL_SYSTEM_CURSOR_DEFAULT,   SDL_SYSTEM_CURSOR_POINTER,   SDL_SYSTEM_CURSOR_TEXT,
                                              SDL_SYSTEM_CURSOR_CROSSHAIR, SDL_SYSTEM_CURSOR_MOVE,      SDL_SYSTEM_CURSOR_EW_RESIZE,
                                              SDL_SYSTEM_CURSOR_NS_RESIZE, SDL_SYSTEM_CURSOR_NOT_ALLOWED, SDL_SYSTEM_CURSOR_WAIT};
    for (size_t i = 0; i < std::size(systemCursors); ++i) app->cursors[i] = SDL_CreateSystemCursor(systemCursors[i]);
    app->host->setCallbacks({
        .wake =
            [raw] {
                if (raw->wakePending.exchange(true)) return;
                SDL_Event e{};
                e.type = raw->wakeEvent;
                SDL_PushEvent(&e);
            },
#ifdef SDL_PLATFORM_ANDROID
        // Android uses our own IME bridge; SDL's loses sync with autocomplete keyboards.
        .textInput =
            [raw](bool active, kompoz::Rect area, const kompoz::TextEditState& state, const kompoz::ImeOptions& options) {
                if (!active) {
                    kompoz::android_ime::hide();
                    return;
                }
                float k = raw->scale;  // dp -> window pixels
                kompoz::android_ime::show(state, {area.x * k, area.y * k, area.width * k, area.height * k}, options);
            },
        .textInputChanged = [](const kompoz::TextEditState& state) { kompoz::android_ime::update(state); },
#else
        .textInput =
            [raw](bool active, kompoz::Rect area, const kompoz::TextEditState&, const kompoz::ImeOptions& options) {
                if (!active) {
                    SDL_StopTextInput(raw->window);
                    return;
                }
                float k = 1 / raw->pointToDp;  // dp -> window points
                SDL_Rect r{int(area.x * k), int(area.y * k), int(area.width * k), int(area.height * k)};
                SDL_SetTextInputArea(raw->window, &r, 0);
                SDL_PropertiesID props = SDL_CreateProperties();
                SDL_SetBooleanProperty(props, SDL_PROP_TEXTINPUT_MULTILINE_BOOLEAN, !options.singleLine);
                SDL_StartTextInputWithProperties(raw->window, props);
                SDL_DestroyProperties(props);
            },
#endif
        .cursor = [raw](kompoz::Cursor cursor) {
            SDL_Cursor* c = raw->cursors[int(cursor)];
            SDL_SetCursor(c ? c : raw->cursors[0]);  // a look the system doesn't have: the arrow
        },
        .loadImage = [](std::string_view path) { return kompoz::sdl::loadImage(path); },
        .loadImageAsync = [](const std::string& model, kompoz::ImageRequestSize size, std::function<void(kompoz::ImageBitmap)> done) {
            kompoz::sdl::loadImageAsync(model, size, std::move(done));
        },
        .getClipboard =
            [] {
                char* text = SDL_GetClipboardText();
                std::string result = text ? text : "";
                SDL_free(text);
                return result;
            },
        .setClipboard = [](const std::string& text) { SDL_SetClipboardText(text.c_str()); },
        .systemBarsAppearance =
            [](bool lightStatusBars, bool lightNavigationBars) {
#ifdef SDL_PLATFORM_ANDROID
                kompoz::android_ime::setSystemBarsAppearance(lightStatusBars, lightNavigationBars);
#else
                (void)lightStatusBars, (void)lightNavigationBars;  // desktop windows have no system bars
#endif
            },
#ifdef SDL_PLATFORM_EMSCRIPTEN
        .locationChanged = [](const std::string& path) { kompoz_web_set_hash(path.c_str()); },
#endif
        // The browser, the app for the scheme (Android intents, xdg-open, ShellExecute, window.open).
        .openUri =
            [](const std::string& uri) {
                if (!SDL_OpenURL(uri.c_str())) SDL_Log("kompoz: can't open %s: %s", uri.c_str(), SDL_GetError());
            },
        .hapticFeedback =
            [](kompoz::HapticFeedbackType type) {
#if defined(SDL_PLATFORM_ANDROID)
                kompoz::android_ime::performHapticFeedback(int(type));
#elif defined(SDL_PLATFORM_EMSCRIPTEN)
                using T = kompoz::HapticFeedbackType;  // a press is felt longer than a tick
                bool strong = type == T::LongPress || type == T::Confirm || type == T::Reject || type == T::GestureThresholdActivate;
                kompoz_web_vibrate(strong ? 25 : 8);
#else
                (void)type;  // nothing to vibrate
#endif
            },
#ifdef SDL_PLATFORM_ANDROID
        .hideSoftwareKeyboard = [] { kompoz::android_ime::hide(); },
        .imeControlBy = [raw](float dp) { kompoz::android_ime::imeControlBy(dp * raw->scale); },  // dp -> window pixels
        .imeControlEnd = [raw](float velocity) { kompoz::android_ime::imeControlEnd(velocity * raw->scale); },
#endif
        .renderImage = [raw](kompoz::Size size, const std::function<void(kompoz::GraphicsContext&)>& draw) {
            return kompoz::sdl::renderImage(*raw, size, draw);
        },
#ifdef SDL_PLATFORM_ANDROID
        .autofillFocus = [raw](int id, kompoz::Rect bounds, bool entered) { kompoz::android_ime::autofillFocus(id, bounds, entered, raw->scale); },
        .autofillValue = [](int id, const std::string& text) { kompoz::android_ime::autofillValue(id, text); },
        .autofillVisibility = [](int id, bool visible) { kompoz::android_ime::autofillVisibility(id, visible); },
        .autofillDone = [](bool commit) { kompoz::android_ime::autofillDone(commit); },
#endif
        .readAsset = [](std::string_view path) { return kompoz::sdl::readFile(std::string(path)); },
    });
    // Deep links: an address given on the command line, the web address's #fragment, an
    // Android VIEW intent.
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]).find("://") != std::string_view::npos) app->host->openDeepLink(argv[i]);
#ifdef SDL_PLATFORM_EMSCRIPTEN
    if (char* hash = kompoz_web_take_hash()) {
        app->host->openDeepLink(hash);
        free(hash);
    }
#endif
    if (const char* path = SDL_getenv("KOMPOZ_STATE_FILE"))
        if (std::vector<uint8_t> state = kompoz::sdl::readFile(path); !state.empty()) app->host->restoreState(state);
#ifdef SDL_PLATFORM_ANDROID
    // Created again after Android ended the process: rememberSaveable values come back.
    if (std::vector<uint8_t> state = kompoz::android_ime::takeRestoredState(); !state.empty()) app->host->restoreState(state);
    kompoz::android_ime::setDeepLinkHandler([raw](std::string link) { raw->host->openDeepLink(std::move(link)); });
    kompoz::android_ime::setHandlers(
        [raw](const kompoz::TextEditState& s) { raw->host->textEdit(s); },
        [raw](kompoz::ImeAction action) { raw->host->imeAction(action); },
        [raw](const std::array<float, 46>& v) {
            float k = 1 / raw->scale;  // window pixels -> dp
            auto kind = [&](size_t i) { return kompoz::PaddingValues{v[i] * k, v[i + 1] * k, v[i + 2] * k, v[i + 3] * k}; };
            raw->host->setSystemInsets({.statusBars = kind(0), .navigationBars = kind(4), .ime = kind(8),
                                        .displayCutout = kind(12), .systemGestures = kind(16), .captionBar = kind(30),
                                        .mandatorySystemGestures = kind(34), .tappableElement = kind(38), .waterfall = kind(42),
                                        .imeAnimating = v[29] != 0,
                                        .imeVisible = v[28] != 0, .imeAnimationSource = kind(20), .imeAnimationTarget = kind(24)});
        },
        [raw](float fontScale) {  // Settings > Display > Font size changed while running
            raw->fonts->setFontScale(fontScale);
            raw->host->setDensity({raw->scale, fontScale});
        });
    kompoz::android_ime::setMediaHandler([raw] { kompoz::sdl::refreshMedia(*raw); });  // a mouse or keyboard attached, docking
    kompoz::android_ime::setSemanticsActionHandler([raw](int id, kompoz::SemanticsAction action, float argument) {
        raw->host->performSemanticsAction(id, action, argument);
    });
    kompoz::android_ime::setTextActionHandler([raw](int id, std::string text) {
        raw->host->performSemanticsAction(id, kompoz::SemanticsAction::SetText, text);
    });
    kompoz::android_ime::setBackHandler([raw](kompoz::android_ime::BackPhase phase, kompoz::BackEventCompat event) {
        using Phase = kompoz::android_ime::BackPhase;
        event.touchX /= raw->scale, event.touchY /= raw->scale;  // window pixels -> dp
        switch (phase) {
        case Phase::Started: raw->host->backStarted(event); break;
        case Phase::Progressed: raw->host->backProgressed(event); break;
        case Phase::Cancelled: raw->host->backCancelled(); break;
        case Phase::Invoked:
            if (!raw->host->backInvoked()) SDL_SendAndroidBackButton();  // nothing took it after all: leave the app
            break;
        }
    });
    kompoz::android_ime::attach();
#endif

    if (const char* script = SDL_getenv("KOMPOZ_SCRIPT")) {
        std::stringstream in(script);
        for (std::string command; std::getline(in, command, ';');) app->script.commands.push_back(command);
    }
    kompoz::sdl::updateMetrics(*app);
    *appstate = app.release();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    App& app = *static_cast<App*>(appstate);
    uint64_t iterationStart = SDL_GetTicksNS();
#ifdef SDL_PLATFORM_EMSCRIPTEN
    if (char* hash = kompoz_web_take_hash()) {  // typed in the address, or a link to #... followed
        app.host->openDeepLink(hash);
        free(hash);
    }
#endif
    bool scripted = app.script.running();
    std::string shot = app.script.enabled() ? kompoz::sdl::stepScript(app) : std::string{};
    int64_t now = scripted ? (app.script.clock += 16'666'667) : app.clock();
    if (scripted && !app.script.running())  // its last frame: real time from here, never back
        app.clockOffset = std::max<int64_t>(0, now - int64_t(SDL_GetTicksNS()));

    uint64_t t0 = app.profile ? SDL_GetTicksNS() : 0;
    int glyphsBefore = app.profile ? app.fonts->rasterized() : 0;
    bool redraw = app.host->frame(now);
    uint64_t t1 = app.profile ? SDL_GetTicksNS() : 0, t2 = t1, t3 = t1;
    if (bool on = app.host->keepScreenOn(); on != app.keepScreenOn) {  // keepScreenOn: the screen saver (Android's KEEP_SCREEN_ON) off while it is laid out
        app.keepScreenOn = on;
        if (on) SDL_DisableScreenSaver();
        else SDL_EnableScreenSaver();
    }
#ifdef KOMPOZ_A11Y_BRIDGE
    // The same for AT-SPI and UI Automation, which answer from the tree last published (on threads
    // of their own), with where the window is: once asked for (they want the first), after frames
    // that changed the screen.
    bool treeRequested = app.a11y && app.a11y->takeTreeRequest();
    app.semanticsDirty = app.semanticsDirty || redraw || treeRequested;
    if (app.a11y && app.a11y->active() && app.semanticsDirty &&
        (treeRequested || now - app.lastSemantics > (app.host->hasPendingWork() ? 500'000'000 : 50'000'000))) {
        app.lastSemantics = now;
        app.semanticsDirty = false;
#ifdef KOMPOZ_ATSPI
        int x = 0, y = 0, w = 0, h = 0;
        SDL_GetWindowPosition(app.window, &x, &y);
        SDL_GetWindowSize(app.window, &w, &h);
        const char* title = SDL_GetWindowTitle(app.window);
        app.a11y->publish(app.host->semanticsTree(), {.x = float(x), .y = float(y), .width = float(w), .height = float(h),
                                                      .dpToPoints = 1 / app.pointToDp, .focused = app.focused,
                                                      .title = title ? title : ""});
#else
        app.a11y->publish(app.host->semanticsTree(), SDL_GetWindowPixelDensity(app.window) / app.pointToDp);
#endif
    }
#endif
#ifdef SDL_PLATFORM_ANDROID
    // Accessibility services read the semantics tree from the UI thread: publish it after
    // frames that changed the screen, at most every 50 ms, and only every half second while it
    // keeps changing (a scroll, an animation): building the tree takes a frame's time.
    app.semanticsDirty = app.semanticsDirty || redraw || kompoz::android_ime::takePublishRequest();
    if (kompoz::android_ime::accessibilityEnabled() && app.semanticsDirty &&
        now - app.lastSemantics > (app.host->hasPendingWork() ? 500'000'000 : 50'000'000)) {
        app.lastSemantics = now;
        app.semanticsDirty = false;
        kompoz::android_ime::publishSemantics(app.host->semanticsTree(), app.scale);
    }
    // Android 13+ goes back itself (leaving the app, with its animation) unless the app's
    // callback is there: it is while something here takes back.
    if (bool wants = app.host->wantsBack(); wants != app.wantsBack) {
        app.wantsBack = wants;
        kompoz::android_ime::setBackEnabled(wants);
    }
    // Something sensitive laid out: the window kept out of screen shares (Android 15+).
    if (bool sensitive = app.host->sensitiveContent(); sensitive != app.sensitiveContent) {
        app.sensitiveContent = sensitive;
        kompoz::android_ime::setContentSensitive(sensitive);
    }
    // The refresh rate it would like (preferredFrameRate), when that changes.
    if (float rate = app.host->preferredFrameRate(); rate != app.frameRate || app.host->preferredFrameRateCategory() != app.frameRateCategory) {
        app.frameRate = rate;
        app.frameRateCategory = app.host->preferredFrameRateCategory();
        kompoz::android_ime::setFrameRate(rate, int(app.frameRateCategory));
    }
    // Where the system's edge swipes are kept off, when that changes.
    const std::vector<kompoz::Rect>& exclusion = app.host->systemGestureExclusion();
    auto same = [](const kompoz::Rect& a, const kompoz::Rect& b) { return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height; };
    if (!std::ranges::equal(exclusion, app.gestureExclusion, same)) {
        app.gestureExclusion = exclusion;
        kompoz::android_ime::setSystemGestureExclusion(exclusion, app.scale);
    }
#endif
    if (const auto& stats = app.host->stats(); app.trace && stats.recompositions != app.traced.recompositions) {
        SDL_Log("kompoz: recomposition %llu: %llu scopes ran, %llu skipped", (unsigned long long)stats.recompositions,
                (unsigned long long)(stats.scopesRun - app.traced.scopesRun),
                (unsigned long long)(stats.scopesSkipped - app.traced.scopesSkipped));
        app.traced = stats;
    }
    if (app.resizeFrames > 0) {
        --app.resizeFrames;
        if (app.display) app.display->invalidate();
        app.showFrame = true;
    }
    kompoz::sdl::Drawn drawn;
    if (redraw || !shot.empty() || app.showFrame) {
        if (app.gpu) {
            app.gpu->beginFrame();
            app.gpuInFrame = true;
        }
        drawn = kompoz::sdl::drawFrame(app, shot);
        t2 = app.profile ? SDL_GetTicksNS() : 0;
        if (app.gpu) {
            app.gpu->endFrame(drawn.shown);
            app.gpuInFrame = false;
            app.gpuCanvas->endFrame();
        } else if (drawn.shown) {
            SDL_RenderPresent(app.renderer);
            app.canvas->endFrame();
        }
        if (drawn.shown && !app.mediaProbed) kompoz::sdl::probeMedia(app);
        app.fonts->endFrame();
        t3 = app.profile ? SDL_GetTicksNS() : 0;
    }
    if (app.profile && redraw) kompoz::sdl::profileFrame(app, t0, t1, t2, t3, app.fonts->rasterized() - glyphsBefore, drawn.area);
    // Sleep until the next event when there is nothing to animate or recompose, or until the
    // next coroutine delay is due.
    bool busy = app.host->hasPendingWork() || app.script.running() || app.resizeFrames > 0;
#ifdef SDL_PLATFORM_ANDROID
    busy = busy || (kompoz::android_ime::accessibilityEnabled() && app.semanticsDirty);  // one more frame publishes it
#endif
#ifdef KOMPOZ_A11Y_BRIDGE
    busy = busy || (app.a11y && app.a11y->active() && app.semanticsDirty);  // the same
#endif
#ifdef SDL_PLATFORM_EMSCRIPTEN
    busy = true;  // the browser's frames don't wait for events there: input would sit until the next timer
#else
    // A frame not shown didn't wait for the display: an animation that changes nothing on the
    // screen (scrolled out of view) would spin as fast as it can. Paced to the display instead.
    if (busy && !drawn.shown && !app.script.running())
        if (uint64_t spent = SDL_GetTicksNS() - iterationStart; spent < app.framePeriodNs) SDL_DelayNS(app.framePeriodNs - spent);
#endif
    if (int64_t deadline = app.host->nextTimerDeadline(); !busy && deadline >= 0 && deadline != app.wakeDeadline) {
        if (app.wakeTimer) SDL_RemoveTimer(app.wakeTimer);
        app.wakeDeadline = deadline;
        int64_t delayNs = std::max<int64_t>(deadline - now, 0) + 1'000'000;
        app.wakeTimer = SDL_AddTimerNS(
            Uint64(delayNs),
            [](void* event, SDL_TimerID, Uint64) -> Uint64 {
                SDL_Event e{};
                e.type = Uint32(reinterpret_cast<uintptr_t>(event));
                SDL_PushEvent(&e);
                return 0;
            },
            reinterpret_cast<void*>(uintptr_t(app.wakeEvent)));
    }
    if (busy == app.waiting) {
        app.waiting = !busy;
        SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, busy ? "0" : "waitevent");
    }
    return app.quit ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    App& app = *static_cast<App*>(appstate);
    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    uint64_t start = app.profile ? SDL_GetTicksNS() : 0;
    kompoz::sdl::handleEvent(app, *event);
    if (app.profile) app.prof.eventsNs += SDL_GetTicksNS() - start;
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    App* app = static_cast<App*>(appstate);
    // KOMPOZ_STATE_FILE: the saved state written out at exit and read at start, as Android does
    // when it ends and brings back an app (to try rememberSaveable on the desktop).
    if (const char* path = SDL_getenv("KOMPOZ_STATE_FILE"); path && app) {
        std::vector<uint8_t> state = app->host->saveState();
        SDL_SaveFile(path, state.data(), state.size());
    }
    delete app;
}
