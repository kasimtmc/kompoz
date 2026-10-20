// Tests of the GPU renderer (platform/gpu) on a real device: scenes drawn through GraphicsContext,
// each compared with its picture in tests/golden/, and what is known exactly checked as such (a
// rect's inside is its color, an edge half a pixel in covers half the pixel, a layer at half alpha
// is half its content). Text uses only the bundled Noto Sans, so the pictures are the same on any
// machine.
//
//   KOMPOZ_GOLDEN_UPDATE=1   writes the pictures anew (look at them before committing them)
//   KOMPOZ_GPU_REQUIRED=1    no device is a failure (CI), not a skip
//   KOMPOZ_GPU_VALIDATION=1  the API's validation on: any error it reports is a failure
//
// The device is Vulkan's, on SDL's offscreen video driver (a headless surface): lavapipe in CI.

#include "../platform/gpu/device.hpp"
#include "../platform/gpu/path_geometry.hpp"
#include "../platform/gpu/renderer.hpp"
#include "../platform/sdl3/font_system.hpp"

#include <kompoz/draw.hpp>

#include <SDL3/SDL.h>
#include <SDL3/SDL_vulkan.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <memory>
#include <string>
#include <vector>

using namespace kompoz;

namespace {

int checks = 0, failures = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        ++checks;                                                      \
        if (!(c)) {                                                    \
            ++failures;                                                \
            std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
        }                                                              \
    } while (0)

struct Pixels {
    int width = 0, height = 0;
    std::vector<uint8_t> rgba;
    const uint8_t* at(int x, int y) const { return rgba.data() + (size_t(y) * size_t(width) + size_t(x)) * 4; }
};

// A device, fonts and a renderer for the tests.
struct Gpu {
    SDL_Window* window = nullptr;
    std::unique_ptr<gpu::Device> device;
    std::unique_ptr<sdl::FontSystem> fonts;
    std::unique_ptr<gpu::Renderer> renderer;

    bool start(std::string* why) {
        window = SDL_CreateWindow("kompoz_gpu_tests", 64, 64, SDL_WINDOW_VULKAN | SDL_WINDOW_HIDDEN);
        if (!window) {
            *why = std::string("no Vulkan window: ") + SDL_GetError();
            return false;
        }
        gpu::Surface surface;
        Uint32 count = 0;
        if (const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count)) surface.vulkanInstanceExtensions.assign(names, names + count);
        surface.vulkanGetInstanceProcAddr = reinterpret_cast<void* (*)(void*, const char*)>(SDL_Vulkan_GetVkGetInstanceProcAddr());
        SDL_Window* w = window;
        surface.createVulkanSurface = [w](void* instance, uint64_t* out) {
            VkSurfaceKHR made{};
            if (!SDL_Vulkan_CreateSurface(w, static_cast<VkInstance>(instance), nullptr, &made)) return false;
            std::memcpy(out, &made, sizeof made);
            return true;
        };
        surface.drawableSize = [w](int* width, int* height) { SDL_GetWindowSizeInPixels(w, width, height); };
        device = gpu::Device::create(gpu::Backend::Vulkan, surface, why);
        if (!device) return false;
        fonts = std::make_unique<sdl::FontSystem>();
        if (!fonts->load(FontWeight::Normal, "fonts/Regular.ttf")) {
            *why = "no fonts/Regular.ttf beside the test";
            return false;
        }
        fonts->load(FontWeight::Bold, "fonts/Bold.ttf");
        fonts->load(FontWeight::Medium, "fonts/Medium.ttf");
        renderer = std::make_unique<gpu::Renderer>(*device, *fonts);
        return true;
    }

    ~Gpu() {
        renderer.reset();
        device.reset();
        if (window) SDL_DestroyWindow(window);
    }

    // `draw` on a w x h px target (white, or `clear`) at `scale` px per dp, read back (premultiplied).
    Pixels render(int w, int h, float scale, const std::function<void(GraphicsContext&)>& draw, gpu::ClearColor clear = {1, 1, 1, 1}) {
        fonts->setScale(scale);
        renderer->scale = scale;
        device->beginFrame();
        auto target = device->createTexture(w, h, gpu::Format::RGBA8, gpu::TextureUsage::Sampled | gpu::TextureUsage::RenderTarget);
        renderer->beginTarget(*target, clear);
        draw(*renderer);
        renderer->endTarget();
        renderer->flush();
        Pixels out{w, h, device->readPixels(*target, {0, 0, w, h})};
        device->endFrame(false);
        renderer->endFrame();
        fonts->endFrame();
        return out;
    }
};

int maxDifference(const uint8_t* a, const uint8_t* b) {
    int d = 0;
    for (int k = 0; k < 4; ++k) d = std::max(d, std::abs(int(a[k]) - int(b[k])));
    return d;
}

// Within `tolerance` of the color (0..255 each).
bool near(const uint8_t* p, int r, int g, int b, int tolerance = 2) {
    const uint8_t want[4] = {uint8_t(r), uint8_t(g), uint8_t(b), 255};
    return maxDifference(p, want) <= tolerance;
}

std::string goldenPath(const std::string& name) { return std::string(KOMPOZ_GOLDEN_DIR) + "/" + name + ".png"; }

bool savePng(const std::string& path, Pixels& p) {
    SDL_Surface* s = SDL_CreateSurfaceFrom(p.width, p.height, SDL_PIXELFORMAT_RGBA32, p.rgba.data(), p.width * 4);
    bool ok = s && SDL_SavePNG(s, path.c_str());
    SDL_DestroySurface(s);
    return ok;
}

Pixels loadPng(const std::string& path) {
    Pixels out;
    SDL_Surface* loaded = SDL_LoadPNG(path.c_str());
    if (!loaded) return out;
    SDL_Surface* rgba = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (!rgba) return out;
    out.width = rgba->w, out.height = rgba->h;
    out.rgba.resize(size_t(out.width) * size_t(out.height) * 4);
    for (int y = 0; y < out.height; ++y)
        std::memcpy(out.rgba.data() + size_t(y) * size_t(out.width) * 4, static_cast<const uint8_t*>(rgba->pixels) + size_t(y) * size_t(rgba->pitch),
                    size_t(out.width) * 4);
    SDL_DestroySurface(rgba);
    return out;
}

// The scene against its picture: every pixel within 3 of it (GPUs round a little differently).
// What differs is written next to the picture (<name>.actual.png) to look at.
void matchesGolden(const std::string& name, Pixels& actual) {
    std::string path = goldenPath(name);
    if (const char* update = std::getenv("KOMPOZ_GOLDEN_UPDATE"); update && std::string(update) == "1") {
        CHECK(savePng(path, actual));
        std::printf("  wrote %s\n", path.c_str());
        return;
    }
    Pixels golden = loadPng(path);
    CHECK(golden.width == actual.width && golden.height == actual.height);
    if (golden.width != actual.width || golden.height != actual.height) {
        std::printf("  %s: no picture of that size (KOMPOZ_GOLDEN_UPDATE=1 writes it)\n", name.c_str());
        return;
    }
    int worst = 0, count = 0;
    for (int y = 0; y < actual.height; ++y)
        for (int x = 0; x < actual.width; ++x) {
            int d = maxDifference(actual.at(x, y), golden.at(x, y));
            worst = std::max(worst, d);
            if (d > 3) ++count;
        }
    CHECK(count == 0);
    if (count) {
        std::printf("  %s: %d pixels differ by more than 3 (by up to %d)\n", name.c_str(), count, worst);
        savePng(goldenPath(name + ".actual"), actual);
    }
}

// --- Scenes --------------------------------------------------------------------------------------

// A transform about (cx, cy): `change` made there.
Matrix about(float cx, float cy, const std::function<void(Matrix&)>& change) {
    Matrix m;  // (each step before the one above it)
    m.translate(cx, cy);
    change(m);
    m.translate(-cx, -cy);
    return m;
}

void rects(GraphicsContext& c) {
    Color red{0.86f, 0.15f, 0.15f, 1}, blue{0.15f, 0.3f, 0.86f, 1};
    c.fillRoundRect({8, 8, 40, 30}, 0, red);                   // square, whole pixels
    c.fillRoundRect({56.5f, 8.25f, 40, 30}, 0, blue);         // square, fractions
    c.fillRoundRect({104, 8, 40, 30}, 8, red);                 // rounded
    c.fillRoundRect({152.4f, 8.6f, 32, 30}, {16, 2, 10, 0}, blue);  // corners of their own
    c.fillRoundRect({8, 48, 60, 60}, 30, red.copy(0.5f));      // a circle, half alpha
    c.fillRoundRect({40, 70, 60, 60}, 30, blue.copy(0.5f));    // overlapping
    c.strokeRoundRect({110, 48, 70, 40}, 10, 1, red);          // hairline-ish
    c.strokeRoundRect({110, 96, 70, 40}, 14, 6, blue);         // thick
    c.strokeRoundRect({8, 140, 60, 40}, 0, 0.5f, red);         // thinner than a pixel
    c.strokeRoundRect({80.3f, 140.3f, 60, 40}, 4, 2.5f, blue);  // fractions
}

void lines(GraphicsContext& c) {
    Color ink{0.1f, 0.1f, 0.1f, 1};
    for (int i = 0; i < 6; ++i) {
        float angle = float(i) * 18 * 3.14159265f / 180, w = 0.5f + float(i) * 1.1f;
        c.drawLine({20, 20 + float(i) * 8}, {20 + 150 * std::cos(angle), 20 + float(i) * 8 + 150 * std::sin(angle)}, w, ink);
    }
    Color teal{0, 0.5f, 0.5f, 1};
    c.drawArc({20, 110, 70, 70}, -90, 270, 6, teal, false);
    c.drawArc({110, 110, 70, 70}, 30, 200, 8, teal.copy(0.7f), true);
    c.drawArc({125, 125, 40, 40}, 0, 360, 2, ink, false);
}

void shadows(GraphicsContext& c) {
    Color shade{0, 0, 0, 0.5f};
    c.fillRoundRect({0, 0, 192, 192}, 0, Color{0.95f, 0.95f, 0.97f, 1});
    c.drawShadow({20, 20, 60, 40}, 8, {shade, {0, 4}, 8, 0}, false);
    c.fillRoundRect({20, 20, 60, 40}, 8, colors::White);
    c.drawShadow({110, 20, 60, 40}, 20, {shade, {4, 4}, 0, 2}, false);  // no blur, spread
    c.fillRoundRect({110, 20, 60, 40}, 20, colors::White);
    c.drawShadow({20, 100, 60, 60}, 30, {Color{0.8f, 0.2f, 0.2f, 0.8f}, {0, 0}, 16, 4}, false);
    c.fillRoundRect({20, 100, 60, 60}, 30, colors::White);
    c.fillRoundRect({110, 100, 60, 60}, 12, colors::White);
    c.drawShadow({110, 100, 60, 60}, 12, {shade, {3, 3}, 6, 2}, true);  // inner
}

void text(GraphicsContext& c) {
    Color ink{0.1f, 0.1f, 0.1f, 1};
    c.drawText("Kompoz", {8, 28}, Font{24}, ink, false);
    c.drawText("İçerik ğüşöç ÇĞİÖŞÜ", {8, 56}, Font{16}, ink, false);
    Font bold{18};
    bold.weight = FontWeight::Bold;
    c.drawText("Bold 0123", {8, 84}, bold, Color{0.6f, 0.1f, 0.1f, 1}, false);
    c.drawTextShadow("Shadow", {10, 122}, Font{28}, Color{0, 0, 0, 0.6f}, 4, false);
    c.drawText("Shadow", {8, 120}, Font{28}, Color{0.2f, 0.3f, 0.8f, 1}, false);
    c.drawText("small 11 sp", {8, 150}, Font{11}, ink.copy(0.7f), false);
    Font wide{14};
    wide.letterSpacing = 3;
    c.drawText("spaced", {8, 176}, wide, ink, false);
}

ImageBitmap checker() {
    std::vector<uint8_t> px(16 * 16 * 4);
    for (int y = 0; y < 16; ++y)
        for (int x = 0; x < 16; ++x) {
            uint8_t* p = &px[size_t(y * 16 + x) * 4];
            bool on = ((x / 4) + (y / 4)) % 2 == 0;
            p[0] = on ? 230 : 30, p[1] = on ? 120 : 30, p[2] = on ? 20 : 160, p[3] = x < 8 ? 255 : 128;
        }
    return ImageBitmap::fromPixels(16, 16, std::move(px));
}

void images(GraphicsContext& c) {
    static ImageBitmap image = checker();
    c.drawImage(image, {0, 0, 16, 16}, {8, 8, 64, 64}, 1);       // scaled up
    c.drawImage(image, {0, 0, 16, 16}, {80.5f, 8.5f, 16, 16}, 1);  // fractions
    c.drawImage(image, {4, 4, 8, 8}, {110, 8, 70, 70}, 0.5f);    // part of it, half alpha
    c.drawImage(image, {0, 0, 16, 16}, {8, 90, 8, 8}, 1);         // scaled down
}

void layers(GraphicsContext& c) {
    Color red{0.86f, 0.15f, 0.15f, 1}, blue{0.15f, 0.3f, 0.86f, 1};
    c.pushLayer({8, 8, 80, 60}, 0, 0.5f);  // half alpha over what overlaps inside it
    c.fillRoundRect({8, 8, 50, 40}, 0, red);
    c.fillRoundRect({30, 20, 50, 40}, 0, blue);
    c.popLayer();
    c.pushLayer({100, 8, 80, 60}, 20, 1);  // rounded: the content cut to it
    c.fillRoundRect({90, 0, 100, 80}, 0, blue);
    c.popLayer();
    LayerTransform turned;
    turned.rotationZ = 30;
    c.pushLayer({20, 90, 60, 40}, 6, 0.9f, turned);
    c.fillRoundRect({20, 90, 60, 40}, 0, red);
    c.drawText("turn", {28, 116}, Font{16}, colors::White, false);
    c.popLayer();
    LayerTransform scaled;
    scaled.scaleX = scaled.scaleY = 1.4f;
    scaled.translationX = 4;
    c.pushLayer({110, 100, 50, 40}, 0, 1, scaled);
    c.fillRoundRect({110, 100, 50, 40}, 8, blue);
    c.pushLayer({120, 110, 30, 20}, 4, 0.5f);  // nested
    c.fillRoundRect({120, 110, 30, 20}, 0, colors::White);
    c.popLayer();
    c.popLayer();
    c.pushClip({8, 150, 60, 30});  // clipped and moved
    c.pushTranslation({10, 5});
    c.fillRoundRect({0, 140, 80, 40}, 10, red);
    c.popTranslation();
    c.popClip();
}

Path star(float cx, float cy, float r) {  // five points, drawn in one go: it crosses itself
    Path p;
    for (int i = 0; i < 5; ++i) {
        float a = -3.14159265f / 2 + float(i) * 4 * 3.14159265f / 5;
        if (i == 0)
            p.moveTo(cx + r * std::cos(a), cy + r * std::sin(a));
        else
            p.lineTo(cx + r * std::cos(a), cy + r * std::sin(a));
    }
    p.close();
    return p;
}

void paths(GraphicsContext& c) {
    Color ink{0.15f, 0.15f, 0.2f, 1}, green{0.1f, 0.55f, 0.3f, 1}, red{0.8f, 0.2f, 0.2f, 1};
    Brush inkBrush = Brush::solid(ink);
    Path s1 = star(30, 32, 26);
    c.drawPath(s1, {}, Brush::solid(green), {}, {});  // non-zero: the middle filled
    Path s2 = star(90, 32, 26);
    s2.fillType = Path::FillType::EvenOdd;
    c.drawPath(s2, {}, Brush::solid(green), {}, {});  // even-odd: the middle left out
    Path circle;
    circle.addOval({126, 8, 52, 48});
    c.drawPath(circle, {}, Brush::solid(red.copy(0.6f)), {}, {});
    // Strokes: caps (butt, round, square), joins (miter, round, bevel), a miter past its limit.
    StrokeCap caps[3] = {StrokeCap::Butt, StrokeCap::Round, StrokeCap::Square};
    StrokeJoin joins[3] = {StrokeJoin::Miter, StrokeJoin::Round, StrokeJoin::Bevel};
    for (int i = 0; i < 3; ++i) {
        Path z;
        z.moveTo(14 + float(i) * 60, 112);
        z.lineTo(34 + float(i) * 60, 76);
        z.lineTo(54 + float(i) * 60, 112);
        c.drawPath(z, {}, inkBrush, {}, {8, caps[i], joins[i], 4});
    }
    Path sharp;  // too sharp for a miter limit of 2: beveled
    sharp.moveTo(10, 150);
    sharp.lineTo(60, 140);
    sharp.lineTo(10, 130);
    c.drawPath(sharp, {}, Brush::solid(red), {}, {6, StrokeCap::Butt, StrokeJoin::Miter, 2});
    Path curve;
    curve.moveTo(72, 180);
    curve.cubicTo(90, 120, 120, 200, 140, 130);
    c.drawPath(curve, {}, inkBrush, {}, {3, StrokeCap::Round, StrokeJoin::Round, 4});
    Path thin;
    thin.moveTo(150, 130);
    thin.quadraticTo(185, 140, 160, 186);
    c.drawPath(thin, {}, inkBrush, {}, {0.5f, StrokeCap::Butt, StrokeJoin::Miter, 4});
    Path dot;  // a contour that goes nowhere: a dot for round caps
    dot.moveTo(176, 120);
    dot.lineTo(176, 120);
    c.drawPath(dot, {}, Brush::solid(red), {}, {10, StrokeCap::Round, StrokeJoin::Round, 4});
    Path ring;
    ring.addRoundRect({100, 140, 40, 40}, 10);
    c.drawPath(ring, {}, Brush::solid(green), {}, {4, StrokeCap::Butt, StrokeJoin::Miter, 4});
}

void gradients(GraphicsContext& c) {
    Color a{0.95f, 0.45f, 0.1f, 1}, b{0.3f, 0.1f, 0.7f, 1}, d{0.1f, 0.7f, 0.6f, 1};
    c.fillGradient({8, 8, 80, 50}, 12, Brush::linearGradient({a, b}), {8, 8, 80, 50});  // corner to corner
    c.fillGradient({100, 8, 84, 50}, 25, Brush::horizontalGradient({a, d, b}, 0, 30, TileMode::Mirror), {100, 8, 84, 50});
    c.fillGradient({8, 66, 60, 60}, 30, Brush::radialGradient({colors::White, a, b}), {8, 66, 60, 60});
    c.fillGradient({76, 66, 60, 60}, 8, Brush::sweepGradient({a, d, b, a}), {76, 66, 60, 60});
    Path ring;  // a gradient stroke, as a BorderStroke with a brush draws one
    ring.addRoundRect({146, 70, 40, 52}, 12);
    c.drawPath(ring, {}, Brush::verticalGradient({a, b}), {146, 70, 40, 52}, {4, StrokeCap::Butt, StrokeJoin::Miter, 4});
    Font big{30};
    big.weight = FontWeight::Bold;
    c.drawTextBrush("Gradient", {8, 168}, big, Brush::horizontalGradient({a, d, b}), {8, 140, 150, 34}, false);
    c.fillGradient({160, 136, 24, 48}, 4, Brush::verticalGradient({a.copy(0), a}), {160, 136, 24, 48});  // to see-through
}

void blendModes(GraphicsContext& c) {  // every mode, a swatch each: an orange square over a blue to green ring
    for (int m = 0; m <= int(BlendMode::Luminosity); ++m) {
        float x = float(m % 6) * 32 + 2, y = float(m / 6) * 38 + 2;
        c.fillGradient({x, y, 28, 28}, 14, Brush::linearGradient({Color{0.1f, 0.3f, 0.9f, 1}, Color{0.2f, 0.8f, 0.4f, 0.6f}}), {x, y, 28, 28});
        c.pushBlendMode(BlendMode(m));
        c.fillRoundRect({x + 8, y + 8, 18, 18}, 3, Color{0.95f, 0.55f, 0.1f, 0.85f});
        c.popBlendMode();
    }
}

void outlineLayers(GraphicsContext& c) {
    Color blue{0.15f, 0.3f, 0.86f, 1}, orange{0.9f, 0.6f, 0.1f, 1};
    c.pushOutlineLayer({2, 2, 80, 80}, star(40, 40, 38), 1);  // (the outline from the bounds' top left)
    c.fillRoundRect({2, 2, 80, 40}, 0, blue);
    c.fillRoundRect({2, 42, 80, 40}, 0, orange);
    c.popLayer();
    LayerTransform turned;
    turned.rotationZ = 20;
    Path heart;
    heart.moveTo(40, 72);
    heart.cubicTo(-10, 40, 10, 0, 40, 22);
    heart.cubicTo(70, 0, 90, 40, 40, 72);
    heart.close();
    c.pushOutlineLayer({100, 100, 80, 80}, heart, 0.8f, turned);  // turned, half see-through
    c.fillRoundRect({100, 100, 80, 80}, 0, Color{0.8f, 0.2f, 0.3f, 1});
    c.drawText("love", {118, 140}, Font{16}, colors::White, false);
    c.popLayer();
}

void transforms(GraphicsContext& c) {  // through pushTransform: turned, skewed, stretched, in perspective
    static ImageBitmap image = checker();
    Color a{0.95f, 0.45f, 0.1f, 1}, b{0.3f, 0.1f, 0.7f, 1}, ink{0.1f, 0.1f, 0.12f, 1};
    c.pushTransform(about(50, 34, [](Matrix& m) { m.rotateZ(-20); }));  // turned text, a gradient across it
    Font big{22};
    big.weight = FontWeight::Bold;
    c.drawTextBrush("Kompoz", {6, 42}, big, Brush::horizontalGradient({a, b}), {6, 20, 88, 28}, false);
    c.popTransform();
    Matrix skew;  // x leans with y
    skew(1, 0) = 0.5f;
    skew.translate(-14, 0);
    c.pushTransform(skew);
    c.fillRoundRect({110, 8, 40, 40}, CornerRadii(8), Color{0.15f, 0.5f, 0.85f, 1});
    c.strokeRoundRect({110, 8, 40, 40}, CornerRadii(8), 2, ink);
    c.popTransform();
    c.pushTransform(about(150, 80, [](Matrix& m) { m.scale(2.5f, 0.6f); }));  // a circle stretched, a line through it
    c.fillRoundRect({138, 68, 24, 24}, CornerRadii(12), Color{0.1f, 0.6f, 0.35f, 1});
    c.drawLine({136, 80}, {164, 80}, 1, ink);
    c.drawArc({140, 70, 20, 20}, 0, 270, 2, colors::White, true);
    c.popTransform();
    // A card in perspective: turned about y, seen from in front.
    Matrix card = about(50, 130, [](Matrix& m) {
        Matrix far;  // the right side further away: w grows to the right
        far(0, 3) = 0.006f;
        far *= m;
        m = far;
        m.rotateZ(4);
    });
    c.pushTransform(card);
    c.drawShadow({14, 100, 72, 60}, CornerRadii(10), {Color{0, 0, 0, 0.4f}, {0, 4}, 6, 0}, false);
    c.fillRoundRect({14, 100, 72, 60}, CornerRadii(10), colors::White);
    c.drawImage(image, {0, 0, 16, 16}, {20, 106, 24, 24}, 1);
    c.drawText("card", {50, 124}, Font{14}, ink, false);
    Path ring;
    ring.addOval({22, 134, 60, 20});
    c.drawPath(ring, {}, Brush::linearGradient({a, b}), {22, 134, 60, 20}, {3, StrokeCap::Butt, StrokeJoin::Miter, 4});
    c.popTransform();
    // Clipped to a turned rect, and a layer inside a turn.
    c.pushTransform(about(150, 150, [](Matrix& m) { m.rotateZ(30); }));
    c.pushClip({128, 128, 44, 44});
    c.fillGradient({100, 100, 100, 100}, 0, Brush::radialGradient({colors::White, a, b}), {120, 120, 60, 60});
    c.popClip();
    c.pushLayer({134, 134, 32, 32}, CornerRadii(6), 0.7f);
    c.fillRoundRect({120, 140, 60, 20}, 0, ink);
    c.popLayer();
    c.popTransform();
}

void graphicsLayers(GraphicsContext& c) {  // graphicsLayer's: in 3D, faded, filtered, blended
    static ImageBitmap image = checker();
    Color a{0.95f, 0.45f, 0.1f, 1}, b{0.3f, 0.1f, 0.7f, 1}, ink{0.1f, 0.1f, 0.12f, 1};
    auto card = [&](Rect r) {
        c.fillGradient(r, CornerRadii(10), Brush::linearGradient({a, b}), r);
        c.drawImage(image, {0, 0, 16, 16}, {r.x + 8, r.y + 8, 24, 24}, 1);
        c.drawText("3D", {r.x + 40, r.y + 28}, Font{16}, colors::White, false);
    };
    auto layer = [&](Rect bounds, const std::function<void(detail::LayerDraw&)>& set, const std::function<void()>& content) {
        detail::LayerDraw l;
        l.bounds = bounds;
        l.geometry.pivot = bounds.center();
        set(l);
        detail::drawThroughLayer(c, l, content);
    };
    layer({12, 10, 76, 56}, [](detail::LayerDraw& l) { l.geometry.rotationX = 40; }, [&] { card({12, 10, 76, 56}); });
    layer({104, 10, 76, 56}, [](detail::LayerDraw& l) {
        l.geometry.rotationY = -45;
        l.clip = true;
        l.outline = Outline::Rounded({{0, 0, 76, 56}, 18, 18, 18, 18});
    }, [&] { card({104, 10, 76, 56}); c.fillRoundRect({104, 10, 30, 56}, 0, ink.copy(0.4f)); });
    layer({12, 90, 76, 56}, [](detail::LayerDraw& l) {
        l.geometry.rotationZ = -12;
        l.geometry.scaleX = 0.9f;
        l.alpha = 0.6f;
    }, [&] { card({12, 90, 76, 56}); c.fillRoundRect({50, 100, 40, 40}, 20, Color{0.1f, 0.6f, 0.4f, 1}); });
    ColorMatrix gray;
    gray.setToSaturation(0);
    layer({104, 90, 76, 40}, [&](detail::LayerDraw& l) { l.colorFilter = ColorFilter::colorMatrix(gray); }, [&] { card({104, 90, 76, 40}); });
    c.fillGradient({12, 156, 168, 30}, CornerRadii(6), Brush::horizontalGradient({Color{0.9f, 0.9f, 0.3f, 1}, Color{0.3f, 0.8f, 0.9f, 1}}), {12, 156, 168, 30});
    layer({20, 150, 152, 42}, [](detail::LayerDraw& l) {
        l.blendMode = BlendMode::Multiply;
        l.geometry.rotationX = -30;
    }, [&] {
        c.fillRoundRect({20, 150, 152, 42}, 21, Color{0.9f, 0.3f, 0.5f, 1});
        c.drawText("Multiply", {64, 177}, Font{16}, colors::White, false);
    });
}

void effects(GraphicsContext& c) {  // render effects: a blur's tile modes, wide, moved, cut, turned
    static ImageBitmap image = checker();
    Color a{0.95f, 0.45f, 0.1f, 1}, b{0.3f, 0.1f, 0.7f, 1};
    auto card = [&](Rect r) {
        c.fillGradient(r, CornerRadii(8), Brush::linearGradient({a, b}), r);
        c.drawImage(image, {0, 0, 16, 16}, {r.x + 6, r.y + 6, 18, 18}, 1);
        c.fillRoundRect({r.x + r.width - 16, r.y + 4, 8, r.height - 8}, 4, colors::White);
    };
    auto layer = [&](Rect bounds, const std::function<void(detail::LayerDraw&)>& set) {
        detail::LayerDraw l;
        l.bounds = bounds;
        l.geometry.pivot = bounds.center();
        set(l);
        detail::drawThroughLayer(c, l, [&] { card(bounds); });
    };
    const TileMode tiles[] = {TileMode::Clamp, TileMode::Decal, TileMode::Repeated, TileMode::Mirror};
    for (int i = 0; i < 4; ++i)  // each cut to what is around it, to show how its edges go
        layer({float(12 + i * 44), 12, 36, 30}, [&](detail::LayerDraw& l) {
            l.renderEffect = BlurEffect(3, 3, tiles[i]);
            l.clip = true;
            l.outline = Outline::Rectangle({-4, -4, 44, 38});
        });
    layer({16, 62, 70, 40}, [](detail::LayerDraw& l) { l.renderEffect = BlurEffect(18, 6, TileMode::Decal); });
    layer({112, 62, 56, 40}, [](detail::LayerDraw& l) {
        l.renderEffect = OffsetEffect(BlurEffect(2, 2, TileMode::Decal), {6, 6});
        l.alpha = 0.7f;
    });
    layer({16, 122, 70, 50}, [](detail::LayerDraw& l) {
        l.renderEffect = BlurEffect(5, 5);
        l.clip = true;
        l.outline = Outline::Rounded({{0, 0, 70, 50}, 25, 25, 25, 25});
    });
    layer({108, 122, 64, 50}, [](detail::LayerDraw& l) {
        l.renderEffect = BlurEffect(2, 2, TileMode::Decal);
        l.geometry.rotationZ = 20;
        l.geometry.rotationX = 30;
    });
}

void fractionalDensity(GraphicsContext& c) {  // at 2.625 px per dp, as a phone's
    c.fillRoundRect({4, 4, 30, 20}, 6, Color{0.2f, 0.5f, 0.3f, 1});
    c.strokeRoundRect({38, 4, 30, 20}, 6, 1, Color{0.2f, 0.2f, 0.2f, 1});
    c.drawText("2.625x", {4, 44}, Font{14}, Color{0.1f, 0.1f, 0.1f, 1}, false);
    c.drawShadow({4, 52, 30, 14}, 4, {Color{0, 0, 0, 0.4f}, {0, 2}, 4, 0}, false);
    c.fillRoundRect({4, 52, 30, 14}, 4, colors::White);
    c.drawLine({40, 52}, {68, 66}, 1, Color{0.6f, 0.1f, 0.1f, 1});
}

// --- Exact checks --------------------------------------------------------------------------------

// How much of each pixel of a w x h picture polygons (each closed) cover by the non-zero rule:
// along 64 lines across each row of pixels, exactly where each line is inside.
std::vector<float> coverageOf(const std::vector<gpu::Contour>& polygons, int w, int h) {
    std::vector<float> out(size_t(w) * size_t(h), 0.f);
    std::vector<std::pair<float, int>> crossings;
    for (int y = 0; y < h; ++y)
        for (int j = 0; j < 64; ++j) {
            float line = float(y) + (float(j) + 0.5f) / 64;
            crossings.clear();
            for (const gpu::Contour& c : polygons)
                for (size_t i = 0, n = c.points.size(); i < n; ++i) {
                    Offset a = c.points[i], b = c.points[(i + 1) % n];
                    if ((a.y <= line) != (b.y <= line)) crossings.push_back({a.x + (b.x - a.x) * (line - a.y) / (b.y - a.y), b.y > a.y ? 1 : -1});
                }
            std::sort(crossings.begin(), crossings.end());
            int winding = 0;
            for (size_t k = 0; k + 1 < crossings.size(); ++k) {
                winding += crossings[k].second;
                if (winding == 0) continue;
                float from = std::max(crossings[k].first, 0.f), to = std::min(crossings[k + 1].first, float(w));
                for (int x = int(std::floor(from)); x < w && float(x) < to; ++x)
                    out[size_t(y) * size_t(w) + size_t(x)] += (std::min(to, float(x + 1)) - std::max(from, float(x))) / 64;
            }
        }
    return out;
}

// The largest difference between two pictures, the second's pixels found where `at` says (and
// those `skip` says left out).
Offset worstAt;  // where worstAgainst found its largest difference
int worstAgainst(const Pixels& a, const std::function<const uint8_t*(int x, int y)>& at,
                 const std::function<bool(int x, int y)>& skip = nullptr) {
    int worst = 0;
    for (int y = 0; y < a.height; ++y)
        for (int x = 0; x < a.width; ++x) {
            int d = skip && skip(x, y) ? 0 : maxDifference(a.at(x, y), at(x, y));
            if (d > worst) worst = d, worstAt = {float(x), float(y)};
        }
    return worst;
}

// Pixels within 1.5 px of any of `corners`.
std::function<bool(int, int)> around(std::vector<Offset> corners) {
    return [corners](int x, int y) {
        for (Offset c : corners)
            if (std::abs(float(x) + 0.5f - c.x) < 1.5f && std::abs(float(y) + 0.5f - c.y) < 1.5f) return true;
        return false;
    };
}


void edgesAreCoveredExactly(Gpu& g) {
    Pixels p = g.render(64, 32, 1, [](GraphicsContext& c) {
        c.fillRoundRect({4, 4, 10, 10}, 0, Color{1, 0, 0, 1});      // whole pixels
        c.fillRoundRect({20.5f, 4, 10, 10}, 0, Color{1, 0, 0, 1});  // half a pixel in
        c.fillRoundRect({40, 4.25f, 10, 10}, 0, Color{0, 0, 1, 1});
    });
    CHECK(near(p.at(8, 8), 255, 0, 0));    // inside: the color
    CHECK(near(p.at(3, 8), 255, 255, 255));  // outside: the background
    CHECK(near(p.at(4, 8), 255, 0, 0));    // the first pixel, whole
    CHECK(near(p.at(13, 8), 255, 0, 0));
    CHECK(near(p.at(14, 8), 255, 255, 255));
    CHECK(near(p.at(20, 8), 255, 128, 128));  // half covered: half way between
    CHECK(near(p.at(30, 8), 255, 128, 128));
    CHECK(near(p.at(25, 8), 255, 0, 0));
    CHECK(near(p.at(45, 4), 64, 64, 255));    // three quarters covered
}

// A straight edge at any angle cuts each pixel's box exactly (a slanted line's sides, a turned
// rect's, as a path's polygon covers it), a corner square to the screen as its two edges'
// product, and a ring thinner than a pixel covers as much as it is wide.
void edgesCutPixelsExactly(Gpu& g) {
    Color red{1, 0, 0, 1};
    Pixels p = g.render(64, 64, 1, [&](GraphicsContext& c) {
        c.fillRoundRect({4.5f, 4.25f, 8, 8}, 0, red);                     // a corner half and three quarters in
        c.strokeRoundRect({20, 4, 12, 12}, 0, 0.5f, red);                // half a pixel wide: its corner three quarters
        c.drawLine({6, 40}, {30, 26}, 3, red);                           // slanted
        c.fillRoundRect({40.5f, 4.25f, 8, 8}, 0.4f, red);                // rounded by less than a pixel
    });
    CHECK(near(p.at(4, 4), 255, 159, 159));    // 0.5 x 0.75
    // Rounded by 0.4 px, the corner covers a little less than the square one (0.375 - 0.034):
    // the circle's edge alone would make it more.
    CHECK(p.at(40, 4)[1] >= 159 && p.at(40, 4)[1] <= 175);
    CHECK(near(p.at(20, 4), 255, 64, 64));     // 0.5 + 0.5 - 0.25
    CHECK(near(p.at(26, 4), 255, 128, 128));   // the side: half
    // The line's sides as its polygon covers them (its square caps' corners aside).
    float len = std::hypot(24.f, -14.f), ux = 24 / len * 1.5f, uy = -14 / len * 1.5f;
    std::vector<Offset> box{{6 - ux + uy, 40 - uy - ux}, {30 + ux + uy, 26 + uy - ux}, {30 + ux - uy, 26 + uy + ux}, {6 - ux - uy, 40 - uy + ux}};
    std::vector<float> want = coverageOf({{box, true}}, 64, 64);
    auto corners = around(box);
    int worst = 0;
    for (int y = 20; y < 48; ++y)
        for (int x = 0; x < 40; ++x)
            if (!corners(x, y)) worst = std::max(worst, std::abs(int(p.at(x, y)[1]) - int(std::lround(255 * (1 - want[size_t(y * 64 + x)])))));
    CHECK(worst <= 3);
    if (worst > 3) std::printf("  slanted line: off by %d\n", worst);
    // A rect turned 30 degrees, against its polygon (the corners aside).
    Matrix turn;
    turn.translate(40, 40);
    turn.rotateZ(30);
    turn.translate(-40, -40);
    Pixels t = g.render(64, 64, 1, [&](GraphicsContext& c) {
        c.pushTransform(turn);
        c.fillRoundRect({30, 34, 20, 12}, 0, red);
        c.popTransform();
    });
    std::vector<Offset> quad{turn.map(Offset{30, 34}), turn.map(Offset{50, 34}), turn.map(Offset{50, 46}), turn.map(Offset{30, 46})};
    want = coverageOf({{quad, true}}, 64, 64);
    corners = around(quad);
    worst = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x)
            if (!corners(x, y)) worst = std::max(worst, std::abs(int(t.at(x, y)[1]) - int(std::lround(255 * (1 - want[size_t(y * 64 + x)])))));
    CHECK(worst <= 3);
    if (worst > 3) std::printf("  turned rect: off by %d\n", worst);
    // A ring half a pixel wide covers 2 pi r 0.5.
    Pixels ring = g.render(64, 64, 1, [&](GraphicsContext& c) { c.drawArc({12, 12, 40, 40}, 0, 360, 0.5f, red, false); });
    float area = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) area += float(255 - ring.at(x, y)[1]) / 255;
    CHECK(std::abs(area - 2 * 3.14159265f * 20 * 0.5f) < 0.6f);
}

void layerAlphaIsExact(Gpu& g) {
    Pixels p = g.render(32, 32, 1, [](GraphicsContext& c) {
        c.pushLayer({0, 0, 32, 32}, 0, 0.5f);
        c.fillRoundRect({4, 4, 24, 24}, 0, Color{1, 0, 0, 1});
        c.fillRoundRect({8, 8, 16, 16}, 0, Color{0, 0, 1, 1});  // over the red, inside the layer
        c.popLayer();
    });
    CHECK(near(p.at(6, 6), 255, 128, 128));  // half red over white
    CHECK(near(p.at(16, 16), 128, 128, 255));  // the blue alone, at half: the red under it doesn't show
    CHECK(near(p.at(1, 1), 255, 255, 255));
}

void clipsAndTranslationsAreExact(Gpu& g) {
    Pixels p = g.render(32, 32, 2, [](GraphicsContext& c) {  // 2 px per dp
        c.pushClip({2, 2, 4, 4});                                 // px 4..12
        c.fillRoundRect({0, 0, 16, 16}, 0, Color{0, 0.5f, 0, 1});
        c.popClip();
        c.pushTranslation({8, 8});
        c.fillRoundRect({0, 0, 2, 2}, 0, Color{0, 0, 0, 1});      // px 16..20
        c.popTranslation();
        c.pushClip({12, 2, 3, 3});                                // px 24..30
        c.pushClip({10, 0, 6, 16});                               // bigger: still only within the one outside it
        c.fillRoundRect({10, 0, 6, 16}, 0, Color{0.5f, 0, 0.5f, 1});
        c.popClip();
        c.popClip();
    });
    CHECK(near(p.at(3, 6), 255, 255, 255));
    CHECK(near(p.at(4, 6), 0, 128, 0));
    CHECK(near(p.at(11, 11), 0, 128, 0));
    CHECK(near(p.at(12, 11), 255, 255, 255));
    CHECK(near(p.at(16, 16), 0, 0, 0));
    CHECK(near(p.at(19, 19), 0, 0, 0));
    CHECK(near(p.at(20, 20), 255, 255, 255));
    CHECK(near(p.at(26, 6), 128, 0, 128));
    CHECK(near(p.at(26, 2), 255, 255, 255));  // inside the inner clip, outside the outer one
    CHECK(near(p.at(22, 6), 255, 255, 255));
    CHECK(near(p.at(26, 12), 255, 255, 255));
}

void pathsAreCoveredExactly(Gpu& g) {
    Pixels p = g.render(64, 40, 1, [](GraphicsContext& c) {
        Path square;  // as a path: the same coverage as the rect
        square.addRect({4.5f, 4, 10, 10});
        c.drawPath(square, {}, Brush::solid(Color{1, 0, 0, 1}), {}, {});
        Path nested;  // two squares the same way round: non-zero fills the inner one, even-odd doesn't
        nested.addRect({20, 4, 16, 16});
        nested.addRect({24, 8, 8, 8});
        c.drawPath(nested, {}, Brush::solid(Color{0, 0, 1, 1}), {}, {});
        Path holed = nested;
        holed.translate({20, 0});
        holed.fillType = Path::FillType::EvenOdd;
        c.drawPath(holed, {}, Brush::solid(Color{0, 0, 1, 1}), {}, {});
        Path line;  // a 2 px stroke along whole pixels, butt caps: exactly a 2 px wide rect
        line.moveTo(4, 30);
        line.lineTo(20, 30);
        c.drawPath(line, {}, Brush::solid(Color{0, 0, 0, 1}), {}, {2, StrokeCap::Butt, StrokeJoin::Miter, 4});
    });
    CHECK(near(p.at(4, 8), 255, 128, 128));  // half covered
    CHECK(near(p.at(8, 8), 255, 0, 0));
    CHECK(near(p.at(14, 8), 255, 128, 128));
    CHECK(near(p.at(15, 8), 255, 255, 255));
    CHECK(near(p.at(27, 11), 0, 0, 255));      // non-zero: inside the inner square too
    CHECK(near(p.at(47, 11), 255, 255, 255));  // even-odd: a hole
    CHECK(near(p.at(41, 5), 0, 0, 255));
    CHECK(near(p.at(10, 29), 0, 0, 0));
    CHECK(near(p.at(10, 30), 0, 0, 0));
    CHECK(near(p.at(10, 28), 255, 255, 255));
    CHECK(near(p.at(10, 31), 255, 255, 255));
    CHECK(near(p.at(3, 29), 255, 255, 255));   // butt: nothing past the ends
    CHECK(near(p.at(20, 29), 255, 255, 255));
}

// A stroke covers each pixel as much as what its outline encloses does by the non-zero rule:
// round a curve, along a gently bending line, its outline doesn't cross itself
// on the inside of the turns, which would count twice there and darken the inner edge.
void strokesCoverOnce(Gpu& g) {
    Path ring, wave;
    ring.addOval({6, 6, 40, 40});
    wave.moveTo(52, 40);
    wave.cubicTo(62, 4, 76, 60, 90, 12);
    struct Stroke {
        const Path* path;
        float width;
    } strokes[] = {{&ring, 5}, {&wave, 7}};
    Pixels p = g.render(96, 52, 1, [&](GraphicsContext& c) {
        for (const Stroke& k : strokes) c.drawPath(*k.path, {}, Brush::solid(Color{1, 0, 0, 1}), {}, {k.width, StrokeCap::Butt, StrokeJoin::Round, 4});
    });
    std::vector<gpu::Contour> outline;
    for (const Stroke& k : strokes) {
        std::vector<gpu::Contour> o = gpu::strokeOutline(gpu::flattenPath(k.path->commands(), {{0, 0}, 1, {0, 0}}), k.width, StrokeCap::Butt,
                                                         StrokeJoin::Round, 4);
        outline.insert(outline.end(), o.begin(), o.end());
    }
    std::vector<float> want = coverageOf(outline, p.width, p.height);
    int worst = 0, wx = 0, wy = 0;
    for (int y = 0; y < p.height; ++y)
        for (int x = 0; x < p.width; ++x) {
            int d = std::abs(int(p.at(x, y)[1]) - int(std::lround(255 * (1 - std::min(want[size_t(y * p.width + x)], 1.f)))));
            if (d > worst) worst = d, wx = x, wy = y;
        }
    CHECK(worst <= 3);
    if (worst > 3) std::printf("  strokes: off by %d at (%d, %d)\n", worst, wx, wy);
    // Sharp turns, thick, beveled: a zigzag covers what its segments' rects and its bevels'
    // triangles do (its inner offsets meet within the segments, each folding back past the other).
    auto sharp = [&](const std::vector<Offset>& zig, int tolerance) {
        Path zigPath;
        zigPath.moveTo(zig[0].x, zig[0].y);
        for (size_t i = 1; i < zig.size(); ++i) zigPath.lineTo(zig[i].x, zig[i].y);
        const float half = 5;
        Pixels z = g.render(48, 52, 1, [&](GraphicsContext& c) {
            c.drawPath(zigPath, {}, Brush::solid(Color{1, 0, 0, 1}), {}, {2 * half, StrokeCap::Butt, StrokeJoin::Bevel, 4});
        });
        std::vector<gpu::Contour> pieces;
        auto unit = [](Offset a, Offset b) {
            float l = std::hypot(b.x - a.x, b.y - a.y);
            return Offset{(b.x - a.x) / l, (b.y - a.y) / l};
        };
        for (size_t i = 0; i + 1 < zig.size(); ++i) {
            Offset d = unit(zig[i], zig[i + 1]), n{d.y * half, -d.x * half};
            pieces.push_back({{{zig[i].x + n.x, zig[i].y + n.y}, {zig[i + 1].x + n.x, zig[i + 1].y + n.y},
                               {zig[i + 1].x - n.x, zig[i + 1].y - n.y}, {zig[i].x - n.x, zig[i].y - n.y}},
                              true});
            if (i == 0) continue;
            Offset in = unit(zig[i - 1], zig[i]);
            float side = in.x * d.y - in.y * d.x > 0 ? 1.f : -1.f;  // the outside of the turn: to the left of travel when turning right
            Offset a{zig[i].x + side * in.y * half, zig[i].y - side * in.x * half}, b{zig[i].x + side * d.y * half, zig[i].y - side * d.x * half};
            pieces.push_back({{zig[i], a, b}, true});
        }
        std::vector<float> want = coverageOf(pieces, z.width, z.height);
        int worstZig = 0, zx = 0, zy = 0;
        for (int y = 0; y < z.height; ++y)
            for (int x = 0; x < z.width; ++x) {
                int d = std::abs(int(z.at(x, y)[1]) - int(std::lround(255 * (1 - std::min(want[size_t(y * z.width + x)], 1.f)))));
                if (d > worstZig) worstZig = d, zx = x, zy = y;
            }
        CHECK(worstZig <= tolerance);
        if (worstZig > tolerance) std::printf("  sharp turns: off by %d at (%d, %d)\n", worstZig, zx, zy);
    };
    sharp({{8, 46}, {16, 8}, {22, 46}, {32, 8}, {40, 46}}, 3);
    // Narrow Vs of short legs, either way round, and a thin triangle stroked either way round:
    // there the inner offsets would meet past the segments, so the outline goes through the
    // vertices; every point of it lies in what the stroke covers (within half its width of a
    // segment). (Its coverage is heavier where both legs' ends overlap: what a path covers twice
    // counts twice in a pixel its edge crosses, as in FreeType's rasterizer.)
    auto within = [](std::vector<Offset> pts, bool closed) {
        std::vector<gpu::Contour> outline = gpu::strokeOutline({{pts, closed}}, 10, StrokeCap::Butt, StrokeJoin::Bevel, 4);
        if (closed) pts.push_back(pts.front());
        float farthest = 0;
        for (const gpu::Contour& c : outline)
            for (Offset q : c.points) {
                float nearest = 1e30f;
                for (size_t i = 0; i + 1 < pts.size(); ++i) {
                    Offset a = pts[i], b = pts[i + 1];
                    float t = std::clamp(((q.x - a.x) * (b.x - a.x) + (q.y - a.y) * (b.y - a.y)) / ((b.x - a.x) * (b.x - a.x) + (b.y - a.y) * (b.y - a.y)), 0.f, 1.f);
                    nearest = std::min(nearest, std::hypot(q.x - (a.x + (b.x - a.x) * t), q.y - (a.y + (b.y - a.y) * t)));
                }
                farthest = std::max(farthest, nearest);
            }
        return !outline.empty() && farthest <= 5.001f;
    };
    CHECK(within({{17, 30}, {20, 8}, {22, 30}}, false));
    CHECK(within({{22, 30}, {20, 8}, {17, 30}}, false));
    CHECK(within({{17, 40}, {20, 6}, {23, 40}}, true));
    CHECK(within({{23, 40}, {20, 6}, {17, 40}}, true));
}

// Every pixel of a gradient fill is Brush::colorAt at its middle (over white): each kind, each
// tile mode, hard stops, uneven stops, see-through ones.
void gradientsMatchColorAt(Gpu& g) {
    Color a{0.9f, 0.2f, 0.1f, 1}, b{0.1f, 0.3f, 0.9f, 1}, c{0.1f, 0.8f, 0.2f, 0.4f};
    std::vector<ColorStop> hard{{0, a}, {0.5f, a}, {0.5f, b}, {1, b}}, uneven{{0.1f, a}, {0.3f, c}, {0.9f, b}};
    const Rect area{8, 8, 48, 48};
    struct Case {
        const char* name;
        Brush brush;
    };
    const Case cases[] = {
        {"linear clamp", Brush::linearGradient({a, b}, {0, 0}, {48, 24})},
        {"linear repeated", Brush::linearGradient({a, b}, {10, 10}, {22, 16}, TileMode::Repeated)},
        {"linear mirror", Brush::linearGradient({a, c, b}, {10, 10}, {22, 16}, TileMode::Mirror)},
        {"linear decal", Brush::linearGradient({a, b}, {10, 10}, {30, 30}, TileMode::Decal)},
        {"linear hard stops", Brush::linearGradient(hard, {0, 0}, {48, 0})},
        {"linear uneven stops", Brush::linearGradient(uneven, {0, 48}, {48, 0})},
        {"radial", Brush::radialGradient({a, c, b})},
        {"radial repeated", Brush::radialGradient({a, b}, Offset{12, 30}, 10, TileMode::Repeated)},
        {"radial decal", Brush::radialGradient({a, b}, Offset{30, 20}, 14, TileMode::Decal)},
        {"sweep", Brush::sweepGradient({a, b, c, a})},
        {"sweep off center", Brush::sweepGradient(uneven, Offset{10, 40})},
    };
    for (float scale : {1.f, 2.625f})  // (at a phone's density too: the gradient in dp, drawn in px)
    for (const Case& k : cases) {
        int size = int(std::ceil(64 * scale));
        Pixels p = g.render(size, size, scale, [&](GraphicsContext& gc) { gc.fillGradient(area, 0, k.brush, area); });
        int worst = 0, wx = 0, wy = 0;
        int from = int(std::ceil(9 * scale)), to = int(std::floor(55 * scale));
        for (int y = from; y < to; ++y)  // inside the rect, away from its anti-aliased edge
            for (int x = from; x < to; ++x) {
                Offset at{(float(x) + 0.5f) / scale, (float(y) + 0.5f) / scale};
                // Where the gradient jumps (a hard stop, a repeat starting over) within float error of
                // the pixel's middle, either side's color is right.
                float t = k.brush.parameterAt(at, area);
                Color before = k.brush.colorAtParameter(t - 1e-4f), after = k.brush.colorAtParameter(t + 1e-4f);
                if (std::max({std::abs(before.r - after.r), std::abs(before.g - after.g), std::abs(before.b - after.b),
                              std::abs(before.a - after.a)}) > 0.02f)
                    continue;
                Color want = k.brush.colorAt(at, area);
                auto over = [&](float v) { return int(std::lround((v * want.a + (1 - want.a)) * 255)); };
                const uint8_t expect[4] = {uint8_t(over(want.r)), uint8_t(over(want.g)), uint8_t(over(want.b)), 255};
                int d = maxDifference(p.at(x, y), expect);
                if (d > worst) worst = d, wx = x, wy = y;
            }
        CHECK(worst <= 2);
        if (worst > 2) std::printf("  %s at %.3g px per dp: off by %d at %d,%d\n", k.name, scale, worst, wx, wy);
    }
}

// A straight color premultiplied, as 8-bit channels.
std::array<uint8_t, 4> premultiplied8(Color c) {
    auto b = [](float v) { return uint8_t(std::lround(std::clamp(v, 0.f, 1.f) * 255)); };
    return {b(c.r * c.a), b(c.g * c.a), b(c.b * c.a), b(c.a)};
}

// Every blend mode draws what blend() works out, onto a see-through target too, and an edge half
// a pixel in is half way between what was there and that.
void blendModesMatchBlend(Gpu& g) {
    struct Pair {
        Color dst, src;
    };
    const Pair pairs[] = {{{0.2f, 0.5f, 0.9f, 1}, {0.9f, 0.4f, 0.1f, 1}},          // opaque
                          {{0.3f, 0.8f, 0.2f, 0.6f}, {0.8f, 0.1f, 0.6f, 0.5f}}};   // see-through, both
    for (int m = 0; m <= int(BlendMode::Luminosity); ++m) {
        BlendMode mode = BlendMode(m);
        for (const Pair& pair : pairs) {
            Pixels p = g.render(32, 16, 1, [&](GraphicsContext& c) {
                c.fillRoundRect({0, 0, 32, 16}, 0, pair.dst);
                c.pushBlendMode(mode);
                c.fillRoundRect({8.5f, 0, 16, 16}, 0, pair.src);  // (its left edge half a pixel in)
                c.popBlendMode();
            }, gpu::ClearColor{0, 0, 0, 0});
            // What the target holds where the source didn't go, as the shader reads it back: 8 bits a
            // channel, premultiplied, rounded as this GPU rounds (0.9 * 255 can be 229 or 230).
            std::array<uint8_t, 4> there;
            std::copy_n(p.at(4, 8), 4, there.begin());
            CHECK(maxDifference(there.data(), premultiplied8(pair.dst).data()) <= 1);
            float da = there[3] / 255.f;
            Color stored = da > 0 ? Color{there[0] / 255.f / da, there[1] / 255.f / da, there[2] / 255.f / da, da} : Color{};
            auto want = premultiplied8(blend(pair.src, stored, mode));
            std::array<uint8_t, 4> half;
            for (int k = 0; k < 4; ++k) half[size_t(k)] = uint8_t(std::lround((int(want[size_t(k)]) + int(there[size_t(k)])) / 2.0));
            if (mode == BlendMode::Plus) {  // as Skia (Android's Compose) does Plus: the coverage as the source's alpha, then clamped
                auto s = premultiplied8(pair.src);
                for (int k = 0; k < 4; ++k) half[size_t(k)] = uint8_t(std::min(255L, std::lround(s[size_t(k)] / 2.0) + there[size_t(k)]));
            }
            int inside = maxDifference(p.at(16, 8), want.data()), edge = maxDifference(p.at(8, 8), half.data());
            CHECK(inside <= 2 && edge <= 3);
            if (inside > 2 || edge > 3) std::printf("  blend mode %d (alpha %.1f): off by %d inside, %d at the edge\n", m, pair.src.a, inside, edge);
        }
    }
}

// A blend mode that reads the target reads it where the pixel is whatever size the target is: the
// target's copy is one texture, as big as the biggest target so far.
void targetCopiesFitEveryTarget(Gpu& g) {
    Color under{0.2f, 0.6f, 0.9f, 1}, over{0.9f, 0.5f, 0.2f, 1};
    auto multiplied = [&](int size) {
        return g.render(size, size, 1, [&](GraphicsContext& c) {
            c.fillRoundRect({0, 0, float(size), float(size)}, 0, under);
            c.pushBlendMode(BlendMode::Multiply);
            c.fillRoundRect({0, 0, float(size), float(size)}, 0, over);
            c.popBlendMode();
        });
    };
    Color want = blend(over, under, BlendMode::Multiply);
    for (int size : {200, 16, 120}) {  // big first: the copy stays that big
        Pixels p = multiplied(size);
        for (Offset at : {Offset{1, 1}, Offset{float(size) / 2, float(size) / 2}, Offset{float(size) - 2, float(size) - 2}})
            CHECK(near(p.at(int(at.x), int(at.y)), int(std::lround(want.r * 255)), int(std::lround(want.g * 255)), int(std::lround(want.b * 255))));
    }
}

// Color filters change each pixel as ColorFilter::apply does: an image's, a fill's; nested ones the
// inner one first.
void colorFiltersMatchApply(Gpu& g) {
    std::vector<uint8_t> px(16 * 8 * 4);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 16; ++x) {
            uint8_t* q = &px[size_t(y * 16 + x) * 4];
            q[0] = uint8_t(x * 16), q[1] = uint8_t(y * 32), q[2] = uint8_t(255 - x * 12), q[3] = uint8_t(x < 8 ? 255 : 96 + y * 16);
        }
    ImageBitmap image = ImageBitmap::fromPixels(16, 8, px);
    ColorMatrix gray;
    gray.setToSaturation(0);
    ColorMatrix swap{{0, 0, 1, 0, 0.1f, 0, 1, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0, 0.8f, 0}};
    struct Case {
        const char* name;
        std::vector<ColorFilter> filters;  // outermost first
    };
    const Case cases[] = {
        {"gray", {ColorFilter::colorMatrix(gray)}},
        {"matrix with offsets", {ColorFilter::colorMatrix(swap)}},
        {"lighting", {ColorFilter::lighting(Color{0.5f, 1, 0.8f, 1}, Color{0.2f, 0, 0.1f, 0})}},
        {"tint", {ColorFilter::tint(Color{0.1f, 0.6f, 0.3f, 0.8f})}},
        {"tint multiply", {ColorFilter::tint(Color{0.9f, 0.5f, 0.2f, 1}, BlendMode::Multiply)}},
        {"tint hue", {ColorFilter::tint(Color{0.1f, 0.3f, 0.9f, 1}, BlendMode::Hue)}},
        {"tint dst over", {ColorFilter::tint(Color{1, 1, 0, 0.5f}, BlendMode::DstOver)}},
        {"nested", {ColorFilter::colorMatrix(gray), ColorFilter::tint(Color{1, 0, 0, 1}, BlendMode::Screen)}},
    };
    for (const Case& k : cases) {
        Pixels p = g.render(16, 16, 1, [&](GraphicsContext& c) {
            for (const ColorFilter& f : k.filters) c.pushColorFilter(f);
            c.drawImage(image, {0, 0, 16, 8}, {0, 0, 16, 8}, 1);
            c.fillRoundRect({0, 8, 16, 8}, 0, Color{0.7f, 0.2f, 0.4f, 0.9f});
            for (size_t i = 0; i < k.filters.size(); ++i) c.popColorFilter();
        }, gpu::ClearColor{0, 0, 0, 0});
        auto filtered = [&](Color c) {
            for (auto it = k.filters.rbegin(); it != k.filters.rend(); ++it) c = it->apply(c);
            return c;
        };
        int worst = 0;
        for (int y = 0; y < 8; ++y)
            for (int x = 0; x < 16; ++x) {
                const uint8_t* q = &px[size_t(y * 16 + x) * 4];
                auto want = premultiplied8(filtered(Color{q[0] / 255.f, q[1] / 255.f, q[2] / 255.f, q[3] / 255.f}));
                worst = std::max(worst, maxDifference(p.at(x, y), want.data()));
            }
        auto fill = premultiplied8(filtered(Color{0.7f, 0.2f, 0.4f, 0.9f}));
        worst = std::max(worst, maxDifference(p.at(8, 12), fill.data()));
        CHECK(worst <= 2);
        if (worst > 2) std::printf("  %s: off by %d\n", k.name, worst);
    }
}

// A bit of everything, unevenly placed (96 x 96 dp): what a transform must carry exactly.
void mixed(GraphicsContext& c, bool withShadow = true) {
    static ImageBitmap image = checker();
    Color red{0.86f, 0.15f, 0.15f, 1}, blue{0.15f, 0.3f, 0.86f, 1}, green{0.1f, 0.55f, 0.3f, 1}, ink{0.1f, 0.1f, 0.12f, 1};
    if (withShadow) c.drawShadow({52, 4, 36, 20}, 6, {Color{0, 0, 0, 0.5f}, {0, 3}, 5, 0}, false);
    c.fillGradient({6, 6, 38, 22}, CornerRadii(6), Brush::linearGradient({red, blue}), {6, 6, 38, 22});
    c.strokeRoundRect({52, 4, 36, 20}, CornerRadii(8), 3, green);
    c.drawLine({8, 36}, {44, 48}, 3, ink);
    c.drawArc({54, 32, 30, 30}, -60, 200, 4, Color{0.5f, 0.2f, 0.6f, 1}, true);
    c.drawTextBrush("Kompoz", {6, 92}, Font{13}, Brush::horizontalGradient({ink, red}), {6, 80, 50, 14}, false);
    c.drawImage(image, {0, 0, 16, 16}, {66, 72, 16, 16}, 1);
    Path s = star(26, 64, 13);
    c.drawPath(s, {}, Brush::radialGradient({colors::White, blue}), {13, 51, 26, 26}, {});
    Path curve;
    curve.moveTo(40, 70);
    curve.cubicTo(50, 50, 60, 90, 70, 60);
    c.drawPath(curve, {}, Brush::solid(red), {}, {2.5f, StrokeCap::Round, StrokeJoin::Round, 4});
    c.pushLayer({42, 76, 22, 16}, CornerRadii(4), 0.6f);
    c.fillRoundRect({36, 70, 40, 30}, 0, Color{0.95f, 0.6f, 0.1f, 1});
    c.popLayer();
    c.pushClip({70, 40, 20, 14});
    c.fillRoundRect({60, 30, 40, 40}, CornerRadii(10), Color{0, 0.5f, 0.5f, 1});
    c.popClip();
    c.pushBlendMode(BlendMode::Multiply);  // (read from the target, under where it goes)
    c.fillRoundRect({30, 20, 20, 14}, 0, Color{0.9f, 0.8f, 0.2f, 1});
    c.popBlendMode();
}

// The largest difference between two pictures, the second's pixels found where `at` says.
// Everything drawn through a transform is the same as without it, moved: turned a quarter about
// the middle, each pixel is where the turn takes it; mirrored, where the mirror does; enlarged
// twice, it is what 2 px per dp draws (shadows aside: their blur grows with the transform, as in
// Skia).
void transformsCarryEverything(Gpu& g) {
    const int n = 96;
    Pixels plain = g.render(n, n, 1, [](GraphicsContext& c) { mixed(c); });
    auto through = [&](const Matrix& m, int size, float scale, bool shadow) {
        return g.render(size, size, scale, [&](GraphicsContext& c) {
            c.pushTransform(m);
            mixed(c, shadow);
            c.popTransform();
        });
    };
    Pixels turned = through(about(48, 48, [](Matrix& m) { m.rotateZ(90); }), n, 1, true);
    int worst = worstAgainst(plain, [&](int x, int y) { return turned.at(n - 1 - y, x); });
    CHECK(worst <= 3);
    if (worst > 3) std::printf("  turned a quarter: off by %d\n", worst);
    Pixels mirrored = through(about(48, 48, [](Matrix& m) { m.scale(-1, 1); }), n, 1, true);
    worst = worstAgainst(plain, [&](int x, int y) { return mirrored.at(n - 1 - x, y); });
    CHECK(worst <= 3);
    if (worst > 3) std::printf("  mirrored: off by %d\n", worst);
    // At 2 px per dp, inside translations: the same (the matrix is in dp, the translations are
    // around it and inside it).
    Pixels plain2 = g.render(2 * n, 2 * n, 2, [](GraphicsContext& c) { mixed(c); });
    Pixels turned2 = g.render(2 * n, 2 * n, 2, [&](GraphicsContext& c) {
        c.pushTranslation({-10, 0});
        c.pushTransform(about(58, 48, [](Matrix& m) { m.rotateZ(90); }));  // about (48, 48) once the two moves are undone
        c.pushTranslation({10, 0});
        mixed(c);
        c.popTranslation();
        c.popTransform();
        c.popTranslation();
    });
    worst = worstAgainst(plain2, [&](int x, int y) { return turned2.at(2 * n - 1 - y, x); });
    CHECK(worst <= 3);
    if (worst > 3) std::printf("  turned a quarter at 2 px per dp: off by %d\n", worst);
    Matrix twice;
    twice.scale(2, 2);
    Pixels enlarged = through(twice, 2 * n, 1, false);
    Pixels dense = g.render(2 * n, 2 * n, 2, [](GraphicsContext& c) { mixed(c, false); });
    worst = worstAgainst(dense, [&](int x, int y) { return enlarged.at(x, y); });
    CHECK(worst <= 3);
    if (worst > 3) std::printf("  enlarged twice: off by %d\n", worst);
}

// How much of each pixel is covered, summed (red on white: what the green channel lost).
float coveredArea(const Pixels& p) {
    float sum = 0;
    for (int y = 0; y < p.height; ++y)
        for (int x = 0; x < p.width; ++x) sum += float(255 - p.at(x, y)[1]) / 255;
    return sum;
}

// Stretched more one way than the other, edges are still covered exactly (each by the pixel's
// size across it); turned or seen in perspective, a shape covers as much as its area; a rect
// clipped to a turned one is that one; a shape partly behind the eye shows what is in front.
void transformedEdgesAreExact(Gpu& g) {
    Color red{1, 0, 0, 1};
    Pixels stretched = g.render(32, 32, 1, [&](GraphicsContext& c) {
        Matrix m;
        m.scale(4, 0.5f);
        c.pushTransform(m);
        c.fillRoundRect({1.1f, 2.3f, 1.9f, 40}, 0, red);  // x 4.4 to 12, y 1.15 to 21.15
        c.popTransform();
    });
    CHECK(near(stretched.at(4, 10), 255, 102, 102));  // 0.6 of it
    CHECK(near(stretched.at(8, 10), 255, 0, 0));
    CHECK(near(stretched.at(12, 10), 255, 255, 255));
    CHECK(near(stretched.at(8, 1), 255, 38, 38));     // 0.85
    CHECK(near(stretched.at(8, 21), 255, 217, 217));  // 0.15
    // Shrunk to a quarter down: the quad still reaches a pixel past the edge (y 1.9: 0.1 of the
    // pixel above it).
    Pixels shrunk = g.render(16, 16, 1, [&](GraphicsContext& c) {
        Matrix m;
        m.scale(1, 0.25f);
        c.pushTransform(m);
        c.fillRoundRect({2, 7.6f, 10, 40}, 0, red);
        c.popTransform();
    });
    CHECK(near(shrunk.at(6, 1), 255, 230, 230));
    // An arc and a line stretched: each pixel covered within 5 / 255 of what 64 x 64 samples of
    // the shapes through the transform say (the most at the ring's ends, which bend round in 3 px),
    // but for the line's corners, no longer square on the screen, where its two edges' coverages
    // multiplied are only near.
    Matrix stretch = about(32, 32, [](Matrix& m) { m.scale(2, 0.75f); });
    Pixels shapes = g.render(64, 64, 1, [&](GraphicsContext& c) {
        c.pushTransform(stretch);
        c.drawArc({20, 18, 24, 24}, 0, 360, 4, red, false);  // a ring 10 to 14 from (32, 30)
        c.drawLine({18, 50}, {46, 56}, 3, red);              // square caps: 1.5 past each end
        c.popTransform();
    });
    Matrix back = stretch;
    back.invert();
    auto inside = [&](float x, float y) {
        Offset p = back.map(Offset{x, y});
        float r = std::hypot(p.x - 32, p.y - 30);
        float along = ((p.x - 18) * 28 + (p.y - 50) * 6) / std::hypot(28.f, 6.f), across = ((p.y - 50) * 28 - (p.x - 18) * 6) / std::hypot(28.f, 6.f);
        return (r >= 10 && r <= 14) || (along >= -1.5f && along <= std::hypot(28.f, 6.f) + 1.5f && std::abs(across) <= 1.5f);
    };
    float lineLength = std::hypot(28.f, 6.f), ux = 28 / lineLength * 1.5f, uy = 6 / lineLength * 1.5f;  // along it, half its width
    std::vector<Offset> lineCorners;
    for (Offset c : {Offset{18 - ux + uy, 50 - uy - ux}, Offset{46 + ux + uy, 56 + uy - ux}, Offset{46 + ux - uy, 56 + uy + ux},
                     Offset{18 - ux - uy, 50 - uy + ux}})
        lineCorners.push_back(stretch.map(c));
    auto skip = around(lineCorners);
    int worstShape = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            if (skip(x, y)) continue;
            int in = 0;
            for (int j = 0; j < 64; ++j)
                for (int i = 0; i < 64; ++i) in += inside(float(x) + (float(i) + 0.5f) / 64, float(y) + (float(j) + 0.5f) / 64);
            int want = int(std::lround(255 * (1 - float(in) / 4096)));
            worstShape = std::max(worstShape, std::abs(int(shapes.at(x, y)[1]) - want));
        }
    CHECK(worstShape <= 5);
    if (worstShape > 5) std::printf("  stretched arc and line: off by %d\n", worstShape);
    if (worstShape > 16) std::printf("  stretched arc and line: off by %d at (%g, %g)\n", worstShape, worstAt.x, worstAt.y);
    Matrix turn = about(32, 32, [](Matrix& m) { m.rotateZ(30); });
    Pixels turned = g.render(64, 64, 1, [&](GraphicsContext& c) {
        c.pushTransform(turn);
        c.fillRoundRect({20, 26, 24, 12}, 0, red);
        c.popTransform();
    });
    CHECK(std::abs(coveredArea(turned) - 288) < 1.5f);
    // An image turned: its edges anti-aliased as the rect's are.
    static ImageBitmap solid = ImageBitmap::fromPixels(4, 4, std::vector<uint8_t>(64, 255));  // white
    Pixels turnedImage = g.render(64, 64, 1, [&](GraphicsContext& c) {
        c.pushTransform(turn);
        c.drawImage(solid, {0, 0, 4, 4}, {20, 26, 24, 12}, 1);
        c.popTransform();
    }, {0, 0, 0, 1});
    Pixels turnedBox = g.render(64, 64, 1, [&](GraphicsContext& c) {
        c.pushTransform(turn);
        c.fillRoundRect({20, 26, 24, 12}, 0, colors::White);
        c.popTransform();
    }, {0, 0, 0, 1});
    int worstImage = worstAgainst(turnedBox, [&](int x, int y) { return turnedImage.at(x, y); });
    CHECK(worstImage <= 3);
    if (worstImage > 3) std::printf("  turned image: off by %d\n", worstImage);
    // In perspective: as much as the quad its corners go to (by the shoelace formula).
    Matrix seen;
    seen(0, 3) = 0.004f;  // w = 1 + 0.004 x
    seen(1, 3) = 0.002f;
    Rect r{8, 10, 40, 36};
    Pixels far = g.render(64, 64, 1, [&](GraphicsContext& c) {
        c.pushTransform(seen);
        c.fillRoundRect(r, 0, red);
        c.popTransform();
    });
    Offset q[4] = {seen.map(Offset{r.x, r.y}), seen.map(Offset{r.right(), r.y}), seen.map(Offset{r.right(), r.bottom()}),
                   seen.map(Offset{r.x, r.bottom()})};
    float area = 0;
    for (int i = 0; i < 4; ++i) area += q[i].x * q[(i + 1) % 4].y - q[(i + 1) % 4].x * q[i].y;
    area = std::abs(area) / 2;
    CHECK(std::abs(coveredArea(far) - area) < area * 0.005f + 1);
    // A path through the same transform: projected on the CPU, covered exactly. The same pixels,
    // but at the corners, which perspective has made other than square (there the two edges'
    // coverages multiplied are only near).
    Pixels farPath = g.render(64, 64, 1, [&](GraphicsContext& c) {
        Path p;
        p.addRect(r);
        c.pushTransform(seen);
        c.drawPath(p, {}, Brush::solid(red), {}, {});
        c.popTransform();
    });
    CHECK(worstAgainst(far, [&](int x, int y) { return farPath.at(x, y); }, around({q[0], q[1], q[2], q[3]})) <= 3);
    // Close to the horizon of a perspective along x (w = 1 + x / 50), where it changes fast, an
    // edge up and down stays so: each pixel it crosses is covered by the distance to it over its
    // derivative exactly at the pixel's middle (in the shape's own units), each side of a thin
    // bar on its own, as worked out here.
    Pixels bars = g.render(64, 16, 1, [&](GraphicsContext& c) {
        Matrix deep;
        deep(0, 3) = 0.02f;
        c.pushTransform(deep);
        for (float x0 : {120.f, 170.f, 230.f}) c.fillRoundRect({x0, -100, 12, 500}, 0, red);  // 1 to 0.3 px wide at 35 to 41
        c.popTransform();
    });
    int worstBar = 0;
    for (int x = 20; x < 42; ++x) {
        float px = float(x) + 0.5f, local = px / (1 - px / 50), along = 1 / ((1 - px / 50) * (1 - px / 50));
        float inside = 0;
        for (float x0 : {120.f, 170.f, 230.f})
            inside += std::max(0.f, std::clamp(0.5f - (x0 - local) / along, 0.f, 1.f) + std::clamp(0.5f - (local - x0 - 12) / along, 0.f, 1.f) - 1);
        worstBar = std::max(worstBar, std::abs(int(bars.at(x, 8)[1]) - int(std::lround(255 * (1 - std::min(inside, 1.f))))));
    }
    CHECK(worstBar <= 3);
    if (worstBar > 3) std::printf("  edges near the horizon: off by %d\n", worstBar);
    // Clipped to a turned rect, turned or in perspective: the rect itself.
    for (const Matrix& m : {turn, seen}) {
        Pixels filled = g.render(64, 64, 1, [&](GraphicsContext& c) {
            c.pushTransform(m);
            c.fillRoundRect({14, 12, 30, 22}, 0, red);
            c.popTransform();
        });
        Pixels clipped = g.render(64, 64, 1, [&](GraphicsContext& c) {
            c.pushTransform(m);
            c.pushClip({14, 12, 30, 22});
            c.fillRoundRect({-20, -20, 100, 100}, 0, red);
            c.popClip();
            c.popTransform();
        });
        int worst = worstAgainst(filled, [&](int x, int y) { return clipped.at(x, y); });
        CHECK(worst <= 3);
        if (worst > 3) std::printf("  clipped to a transformed rect: off by %d\n", worst);
    }
    // Partly behind the eye (w = 1 + 0.02 x is below 0 left of x = -50): what is in front shows,
    // as the path cut there shows it, and nothing else.
    Matrix behind;
    behind(0, 3) = 0.02f;
    Rect wide{-100, 8, 140, 40};
    Pixels front = g.render(64, 64, 1, [&](GraphicsContext& c) {
        c.pushTransform(behind);
        c.fillRoundRect(wide, 0, red);
        c.popTransform();
    });
    Pixels frontPath = g.render(64, 64, 1, [&](GraphicsContext& c) {
        Path p;
        p.addRect(wide);
        c.pushTransform(behind);
        c.drawPath(p, {}, Brush::solid(red), {}, {});
        c.popTransform();
    });
    Offset frontCorners[2] = {behind.map(Offset{wide.right(), wide.y}), behind.map(Offset{wide.right(), wide.bottom()})};
    CHECK(worstAgainst(front, [&](int x, int y) { return frontPath.at(x, y); }, around({frontCorners[0], frontCorners[1]})) <= 3);
    CHECK(near(front.at(10, 20), 255, 0, 0));      // x 10 / 1.2 = 8.3: in front, drawn
    CHECK(near(front.at(30, 20), 255, 255, 255));  // past its right edge (40 / 1.8 = 22.2)
    // Turned into view, what was out of it isn't skipped.
    bool skipped = true, skippedPlain = false;
    g.render(96, 48, 1, [&](GraphicsContext& c) {
        skippedPlain = c.quickReject({60, 60, 4, 4});  // below the target
        c.pushTransform(about(48, 24, [](Matrix& m) { m.rotateZ(90); }));
        skipped = c.quickReject({60, 60, 4, 4});  // turned to (10, 38)
        c.popTransform();
    });
    CHECK(skippedPlain && !skipped);
}

// Clips of any shape cut what is drawn in them as that shape covers: a fill clipped to a rounded
// rect or a path (either fill rule) is the shape filled; clipped to what is outside it, the fill
// with the shape over it in the background's color; clips inside clips multiply; a path clip
// turned is the path turned; and the paths of many clips that fill the coverage mask more than
// once (a new generation of it) are each still where they are.
void clipsCutAsTheirShapes(Gpu& g) {
    Color red{0.9f, 0.1f, 0.1f, 1};
    Rect all{-10, -10, 100, 100}, box{10.3f, 12.6f, 44, 30};
    CornerRadii radii{12, 3, 0, 20};
    Path star1 = star(36, 36, 30), star2 = star(36, 36, 30);
    star2.fillType = Path::FillType::EvenOdd;
    auto same = [&](const char* what, const std::function<void(GraphicsContext&)>& a, const std::function<void(GraphicsContext&)>& b, int size = 72) {
        Pixels pa = g.render(size, size, 1, a), pb = g.render(size, size, 1, b);
        int worst = worstAgainst(pa, [&](int x, int y) { return pb.at(x, y); });
        CHECK(worst <= 2);
        if (worst > 2) std::printf("  %s: off by %d at (%g, %g)\n", what, worst, worstAt.x, worstAt.y);
    };
    same("rounded rect", [&](GraphicsContext& c) {
        c.pushClipRoundRect(box, radii);
        c.fillRoundRect(all, 0, red);
        c.popClip();
    }, [&](GraphicsContext& c) { c.fillRoundRect(box, radii, red); });
    same("outside a rounded rect", [&](GraphicsContext& c) {
        c.pushClipRoundRect(box, radii, ClipOp::Difference);
        c.fillRoundRect(all, 0, red);
        c.popClip();
    }, [&](GraphicsContext& c) {
        c.fillRoundRect(all, 0, red);
        c.fillRoundRect(box, radii, colors::White);
    });
    for (const Path* p : {&star1, &star2}) {
        same("path", [&](GraphicsContext& c) {
            c.pushClipPath(*p, {});
            c.fillRoundRect(all, 0, red);
            c.popClip();
        }, [&](GraphicsContext& c) { c.drawPath(*p, {}, Brush::solid(red), {}, {}); });
        same("outside a path", [&](GraphicsContext& c) {
            c.pushClipPath(*p, {2, 1}, ClipOp::Difference);
            c.fillRoundRect(all, 0, red);
            c.popClip();
        }, [&](GraphicsContext& c) {
            c.fillRoundRect(all, 0, red);
            c.drawPath(*p, {2, 1}, Brush::solid(colors::White), {}, {});
        });
    }
    // A shape that isn't star-shaped about its middle (a C), as a clip and as a layer's outline:
    // the path filled.
    Path cee;
    cee.addOval({6, 6, 60, 60});
    Path hole;
    hole.addOval({22, 18, 52, 36});
    cee = Path::combine(PathOperation::Difference, cee, hole);
    Shape ceeShape = GenericShape([cee](Path& p, Size, LayoutDirection) { p.addPath(cee); });
    same("C clip", [&](GraphicsContext& c) {
        c.pushClipShape({0, 0, 72, 72}, ceeShape, false);
        c.fillRoundRect(all, 0, red);
        c.popClip();
    }, [&](GraphicsContext& c) { c.drawPath(cee, {}, Brush::solid(red), {}, {}); });
    same("C layer", [&](GraphicsContext& c) {
        c.pushShapeLayer({0, 0, 72, 72}, ceeShape, false, 1);
        c.fillRoundRect(all, 0, red);
        c.popLayer();
    }, [&](GraphicsContext& c) { c.drawPath(cee, {}, Brush::solid(red), {}, {}); });
    // Inside each other: the coverages multiply.
    Pixels a = g.render(72, 72, 1, [&](GraphicsContext& c) { c.drawPath(star1, {}, Brush::solid(red), {}, {}); });
    Pixels b = g.render(72, 72, 1, [&](GraphicsContext& c) { c.fillRoundRect(box, radii, red); });
    Pixels both = g.render(72, 72, 1, [&](GraphicsContext& c) {
        c.pushClipPath(star1, {});
        c.pushClipRoundRect(box, radii);
        c.fillRoundRect(all, 0, red);
        c.popClip();
        c.popClip();
    });
    int worst = 0;
    for (int y = 0; y < 72; ++y)
        for (int x = 0; x < 72; ++x) {
            float ca = float(255 - a.at(x, y)[1]) / (255 - 25.5f), cb = float(255 - b.at(x, y)[1]) / (255 - 25.5f);
            int want = int(std::lround(255 - ca * cb * (255 - 25.5f)));
            worst = std::max(worst, std::abs(int(both.at(x, y)[1]) - want));
        }
    CHECK(worst <= 2);
    if (worst > 2) std::printf("  clips inside clips: off by %d\n", worst);
    // Turned: the path turned.
    Matrix turn = about(36, 36, [](Matrix& m) { m.rotateZ(25); m.scale(1.2f, 0.8f); });
    same("turned path", [&](GraphicsContext& c) {
        c.pushTransform(turn);
        c.pushClipPath(star1, {});
        c.fillRoundRect(all, 0, red);
        c.popClip();
        c.popTransform();
    }, [&](GraphicsContext& c) {
        c.pushTransform(turn);
        c.drawPath(star1, {}, Brush::solid(red), {}, {});
        c.popTransform();
    });
    // Thirty clips each a disc 500 px across, a disc 5 px bigger filled in each (its own coverage
    // in the same generation as the clip's): two generations of the coverage mask (2048 px
    // square: sixteen such discs), each as the disc drawn.
    auto discs = [&](bool clipped) {
        return [clipped, red](GraphicsContext& c) {
            Path first;  // (one more first, so that a generation fills up as a clip's path goes in)
            first.addOval({400, 400, 500, 500});
            c.drawPath(first, {}, Brush::solid(Color{0.5f, 0.5f, 0.5f, 1}), {}, {});
            for (int i = 0; i < 30; ++i) {
                Path disc;
                float x = float(i % 6) * 90, y = float(i / 6) * 90;
                disc.addOval({x, y, 500, 500});
                Color color{float(i) / 30, 0.5f, 1 - float(i) / 30, 1};
                if (clipped) {
                    Path bigger;
                    bigger.addOval({x - 5, y - 5, 510, 510});
                    c.pushClipPath(disc, {});
                    c.drawPath(bigger, {}, Brush::solid(color), {}, {});
                    c.popClip();
                } else {
                    c.drawPath(disc, {}, Brush::solid(color), {}, {});
                }
            }
            (void)red;
        };
    };
    same("many clips", discs(true), discs(false), 960);
}

// A graphicsLayer's layer on the GPU: turned half way round about y, the scene mirrored about its
// pivot, pixel for pixel; faded offscreen, what overlaps in it fades once, faded each draw
// (ModulateAlpha) twice; its blend mode and color filter go onto what is under it as blend() and
// ColorFilter::apply work them out.
void graphicsLayersCompositeAsCompose(Gpu& g) {
    const int n = 96;
    Pixels plain = g.render(n, n, 1, [](GraphicsContext& c) { mixed(c); });
    Pixels flipped = g.render(n, n, 1, [](GraphicsContext& c) {
        detail::LayerDraw l;
        l.bounds = {0, 0, 96, 96};
        l.geometry.rotationY = 180;
        l.geometry.pivot = {48, 48};
        detail::drawThroughLayer(c, l, [&] { mixed(c); });
    });
    int worst = worstAgainst(plain, [&](int x, int y) { return flipped.at(n - 1 - x, y); });
    CHECK(worst <= 3);
    if (worst > 3) std::printf("  turned half way about y: off by %d at (%g, %g)\n", worst, worstAt.x, worstAt.y);
    auto overlapping = [&](CompositingStrategy strategy) {
        return g.render(32, 32, 1, [strategy](GraphicsContext& c) {
            detail::LayerDraw l;
            l.bounds = {0, 0, 32, 32};
            l.alpha = 0.5f;
            l.strategy = strategy;
            detail::drawThroughLayer(c, l, [&] {
                c.fillRoundRect({0, 0, 20, 32}, 0, Color{1, 0, 0, 1});
                c.fillRoundRect({12, 0, 20, 32}, 0, Color{0, 0, 1, 1});
            });
        });
    };
    Pixels once = overlapping(CompositingStrategy::Auto), twice = overlapping(CompositingStrategy::ModulateAlpha);
    CHECK(near(once.at(16, 16), 128, 128, 255) && near(once.at(4, 16), 255, 128, 128));   // the blue alone over the overlap, at half
    CHECK(near(twice.at(16, 16), 128, 64, 191) && near(twice.at(4, 16), 255, 128, 128));  // the red at half, the blue at half over it
    Color under{0.2f, 0.6f, 0.9f, 1}, over{0.9f, 0.5f, 0.2f, 1};
    Pixels multiplied = g.render(16, 16, 1, [&](GraphicsContext& c) {
        c.fillRoundRect({0, 0, 16, 16}, 0, under);
        detail::LayerDraw l;
        l.bounds = {0, 0, 16, 16};
        l.blendMode = BlendMode::Multiply;
        detail::drawThroughLayer(c, l, [&] { c.fillRoundRect({0, 0, 16, 16}, 0, over); });
    });
    Color want = blend(over, under, BlendMode::Multiply);
    CHECK(near(multiplied.at(8, 8), int(std::lround(want.r * 255)), int(std::lround(want.g * 255)), int(std::lround(want.b * 255))));
    ColorMatrix gray;
    gray.setToSaturation(0);
    Pixels grayed = g.render(16, 16, 1, [&](GraphicsContext& c) {
        detail::LayerDraw l;
        l.bounds = {0, 0, 16, 16};
        l.colorFilter = ColorFilter::colorMatrix(gray);
        detail::drawThroughLayer(c, l, [&] { c.fillRoundRect({0, 0, 16, 16}, 0, over); });
    });
    Color g2 = ColorFilter::colorMatrix(gray).apply(over);
    CHECK(near(grayed.at(8, 8), int(std::lround(g2.r * 255)), int(std::lround(g2.g * 255)), int(std::lround(g2.b * 255))));
}

// What Skia makes of a layer's pixels blurred (premultiplied, w x h): the pixels past `valid` as
// `tile` says, a discrete Gaussian across (weights exp(-i^2 / 2 sigma^2) out to ceil(3 sigma), adding
// up to 1), then one down; nothing past three sigmas beyond `valid`.
struct Image {
    int w, h;
    std::vector<float> rgba;  // premultiplied
    float* at(int x, int y) { return &rgba[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
};
Image painted(int w, int h, const std::vector<std::pair<gpu::IRect, Color>>& fills) {  // opaque, whole pixels
    Image out{w, h, std::vector<float>(size_t(w) * size_t(h) * 4, 0.f)};
    for (const auto& [r, c] : fills)
        for (int y = std::max(0, r.y); y < std::min(h, r.y + r.height); ++y)
            for (int x = std::max(0, r.x); x < std::min(w, r.x + r.width); ++x) {
                float* q = out.at(x, y);
                q[0] = c.r, q[1] = c.g, q[2] = c.b, q[3] = 1;
            }
    return out;
}
Image gaussian(Image in, gpu::IRect valid, TileMode tile, float sigmaX, float sigmaY) {
    auto tiled = [&](int t, int low, int n) {
        if (t >= low && t < low + n) return t;
        int m = t - low;
        switch (tile) {
        case TileMode::Clamp: return std::clamp(t, low, low + n - 1);
        case TileMode::Repeated: return low + ((m % n) + n) % n;
        case TileMode::Mirror: {
            int k = ((m % (2 * n)) + 2 * n) % (2 * n);
            return low + (k < n ? k : 2 * n - 1 - k);
        }
        default: return -1;
        }
    };
    auto kernel = [](float sigma) {
        int reach = sigma > 0 ? int(std::ceil(3 * sigma)) : 0;
        std::vector<double> k(size_t(2 * reach + 1));
        double sum = 0;
        for (int i = -reach; i <= reach; ++i) sum += k[size_t(i + reach)] = sigma > 0 ? std::exp(-double(i * i) / (2.0 * sigma * sigma)) : 1;
        for (double& v : k) v /= sum;
        return k;
    };
    std::vector<double> kx = kernel(sigmaX), ky = kernel(sigmaY);
    int rx = int(kx.size() / 2), ry = int(ky.size() / 2);
    auto source = [&](int x, int y, int c) -> double {
        int sx = tiled(x, valid.x, valid.width), sy = tiled(y, valid.y, valid.height);
        return sx < 0 || sy < 0 || sx >= in.w || sy >= in.h ? 0.0 : double(in.at(sx, sy)[c]);
    };
    Image out{in.w, in.h, std::vector<float>(in.rgba.size(), 0.f)};
    for (int y = std::max(0, valid.y - ry); y < std::min(in.h, valid.y + valid.height + ry); ++y)
        for (int x = std::max(0, valid.x - rx); x < std::min(in.w, valid.x + valid.width + rx); ++x)
            for (int c = 0; c < 4; ++c) {
                double v = 0;
                for (int j = -ry; j <= ry; ++j) {
                    double across = 0;
                    for (int i = -rx; i <= rx; ++i) across += kx[size_t(i + rx)] * source(x + i, y + j, c);
                    v += ky[size_t(j + ry)] * across;
                }
                out.at(x, y)[c] = float(v);
            }
    return out;
}
// As drawn over white, 8 bits a channel.
Pixels overWhite(Image image) {
    Pixels p{image.w, image.h, std::vector<uint8_t>(size_t(image.w) * size_t(image.h) * 4)};
    for (int y = 0; y < image.h; ++y)
        for (int x = 0; x < image.w; ++x) {
            const float* q = image.at(x, y);
            uint8_t* o = &p.rgba[(size_t(y) * size_t(image.w) + size_t(x)) * 4];
            for (int c = 0; c < 3; ++c) o[c] = uint8_t(std::lround(std::clamp(q[c] + (1 - q[3]), 0.f, 1.f) * 255));
            o[3] = 255;
        }
    return p;
}
float sigmaOf(float radiusPx) { return 0.57735f * radiusPx + 0.5f; }  // Android's

// Effect layers blur as Skia does: the edges as the tile mode says, a Gaussian of Android's sigma,
// spreading three sigmas; wide ones (scaled down) close to it; offsets, chains, transforms, a
// clip around them and layers too big for a texture.
void blursAreGaussian(Gpu& g) {
    const int n = 96;
    gpu::IRect bounds{24, 24, 48, 48};
    std::vector<std::pair<gpu::IRect, Color>> fills = {{{24, 24, 20, 48}, Color{0.9f, 0.1f, 0.1f, 1}},
                                                        {{44, 30, 28, 14}, Color{0.1f, 0.2f, 0.9f, 1}},
                                                        {{50, 56, 10, 10}, Color{0.1f, 0.7f, 0.2f, 1}},
                                                        {{60, 66, 12, 6}, Color{0.95f, 0.8f, 0.1f, 1}}};  // (the bottom unlike the top, the right unlike the left)
    auto content = [&](GraphicsContext& c) {
        for (const auto& [r, col] : fills) c.fillRoundRect({float(r.x), float(r.y), float(r.width), float(r.height)}, 0, col);
        c.fillRoundRect({0, 0, 96, 10}, 0, Color{0, 0, 0, 1});  // outside the bounds: cut away
    };
    Image base = painted(n, n, fills);
    auto blurredBy = [&](const RenderEffect& effect, float scale = 1) {
        return g.render(n, n, scale, [&](GraphicsContext& c) {
            c.pushEffectLayer({24 / scale, 24 / scale, 48 / scale, 48 / scale}, effect, 1);
            c.pushTransform([&] {
                Matrix m;
                m.scale(1 / scale, 1 / scale);
                return m;
            }());
            content(c);
            c.popTransform();
            c.popLayer();
        });
    };
    auto report = [&](const char* what, const Pixels& p, const Pixels& want, int tolerance,
                      const std::function<bool(int, int)>& skip = nullptr) {
        int worst = worstAgainst(p, [&](int x, int y) { return want.at(x, y); }, skip);
        if (std::getenv("KOMPOZ_BLUR_WORST")) std::printf("  %s: %d\n", what, worst);
        CHECK(worst <= tolerance);
        if (worst > tolerance) std::printf("  %s: off by %d at (%g, %g)\n", what, worst, worstAt.x, worstAt.y);
        if (worst > tolerance && std::getenv("KOMPOZ_BLUR_WORST")) {
            Pixels a = p, b = want;
            savePng(std::string("blur-got.png"), a), savePng(std::string("blur-want.png"), b);
        }
    };
    // Each tile mode, narrow (no scaling down) and each way its own radius; in bounds of another
    // size too, whose periods a GPU dividing by a reciprocal can come out just under.
    for (TileMode tile : {TileMode::Clamp, TileMode::Decal, TileMode::Repeated, TileMode::Mirror}) {
        Pixels p = blurredBy(BlurEffect(4, 2.5f, tile));
        report("narrow", p, overWhite(gaussian(base, bounds, tile, sigmaOf(4), sigmaOf(2.5f))), 2);
        gpu::IRect small{30, 30, 36, 30};
        Pixels q = g.render(n, n, 1, [&](GraphicsContext& c) {
            c.pushEffectLayer({30, 30, 36, 30}, BlurEffect(3, 3, tile), 1);
            content(c);
            c.popLayer();
        });
        Image within = base;  // (what is drawn within the bounds)
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                if (x < small.x || x >= small.x + small.width || y < small.y || y >= small.y + small.height) std::fill_n(within.at(x, y), 4, 0.f);
        report("narrow, 36 x 30", q, overWhite(gaussian(within, small, tile, sigmaOf(3), sigmaOf(3))), 2);
    }
    // One way only; at 2 px per dp (the sigma in px).
    report("across only", blurredBy(BlurEffect(5, 0)), overWhite(gaussian(base, bounds, TileMode::Clamp, sigmaOf(5), 0)), 2);
    report("at 2 px a dp", blurredBy(BlurEffect(3, 3, TileMode::Decal), 2),
           overWhite(gaussian(base, bounds, TileMode::Decal, sigmaOf(6), sigmaOf(6))), 2);
    // Wide: scaled down 2, 4, 8, 16 times (two passes), close to the Gaussian it stands for (the
    // means of blocks and the bilinear filtering back up aren't quite Gaussians: within 3).
    for (float r : {9.f, 20.f, 40.f, 80.f}) {
        Pixels p = blurredBy(BlurEffect(r, r, TileMode::Decal));
        report("wide", p, overWhite(gaussian(base, bounds, TileMode::Decal, sigmaOf(r), sigmaOf(r))), 3);
        Pixels clamped = blurredBy(BlurEffect(r, r));
        report("wide, clamped", clamped, overWhite(gaussian(base, bounds, TileMode::Clamp, sigmaOf(r), sigmaOf(r))), 3);
    }
    // Wide ones have the variance of their Gaussian. Measured across on the edges of half planes
    // blurred across, each row's a pixel further on (scaling down and back up isn't the same at every phase): the
    // Gaussian at the level, as its kernel cuts it, and the variances of the blocks' means and of
    // the bilinear filtering back up add up to what they stand for (which they do before the cut).
    auto variance = [](const Pixels& p) {
        double total = 0;
        for (int y = 0; y < p.height; ++y) {
            double sum = 0, mean = 0, second = 0;
            for (int x = 0; x + 1 < p.width; ++x) {
                double d = (p.at(x + 1, y)[0] - p.at(x, y)[0]) / 255.0;
                sum += d, mean += d * (x + 1), second += d * (x + 1) * (x + 1);
            }
            mean /= sum;
            total += second / sum - mean * mean;
        }
        return total / p.height;
    };
    auto truncated = [](double g) {  // a discrete Gaussian's variance out to ceil(3 g)
        int reach = int(std::ceil(3 * g));
        double w = 0, m = 0;
        for (int i = -reach; i <= reach; ++i) w += std::exp(-i * i / (2 * g * g)), m += i * i * std::exp(-i * i / (2 * g * g));
        return m / w;
    };
    struct Wide {
        RenderEffect effect;
        int step;
        double level;  // the sigma at the level
    };
    double s30 = sigmaOf(30), s3 = sigmaOf(3), s40 = sigmaOf(40), s80 = sigmaOf(80);
    for (const Wide& w : {Wide{BlurEffect(40, 0), 8, std::sqrt(s40 * s40 - 63.0 / 12 - 63.0 / 6) / 8},
                          Wide{BlurEffect(80, 0), 16, std::sqrt(s80 * s80 - 255.0 / 12 - 255.0 / 6) / 16},
                          // (the second at the first's level, 8: all of its variance gone into scaling back up, the rest from the first)
                          Wide{BlurEffect(BlurEffect(30, 0), 3, 0), 8, std::sqrt(s30 * s30 - 63.0 / 12 - (63.0 / 6 - s3 * s3)) / 8}}) {
        Pixels edges = g.render(400, 16, 1, [&](GraphicsContext& c) {
            c.pushEffectLayer({0, 0, 400, 16}, w.effect, 1);
            for (int y = 0; y < 16; ++y) c.fillRoundRect({0, float(y), float(200 + y), 1}, 0, Color{0, 0, 0, 1});
            c.popLayer();
        });
        // (Clamp: the rows are as they are above and below, the half plane goes on to the left.)
        double got = variance(edges), want = truncated(w.level) * w.step * w.step + (w.step * w.step - 1) / 4.0;
        if (std::getenv("KOMPOZ_BLUR_WORST")) std::printf("  variance: %g, %g\n", got, want);
        CHECK(std::abs(got - want) < 0.004 * want + 1);  // (8 bits a channel: the far tails' least values gone)
    }
    // Moved (before or after the blur: the same), whole pixels and a half.
    Image moved = gaussian(base, bounds, TileMode::Clamp, sigmaOf(3), sigmaOf(3));
    Image shifted{n, n, std::vector<float>(moved.rgba.size(), 0.f)};
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (x - 7 >= 0 && y + 4 < n) std::copy_n(moved.at(x - 7, y + 4), 4, shifted.at(x, y));
    report("moved, then blurred", blurredBy(BlurEffect(OffsetEffect(7, -4), 3, 3)), overWhite(shifted), 2);
    report("blurred, then moved", blurredBy(OffsetEffect(BlurEffect(3, 3), {7, -4})), overWhite(shifted), 2);
    report("moved at 2 px a dp", blurredBy(OffsetEffect(BlurEffect(1.5f, 1.5f), {3.5f, -2}), 2), overWhite(shifted), 2);
    // Twice: as once by the sum of the variances, the second at what the first spread to.
    Image twice = gaussian(base, bounds, TileMode::Decal, std::hypot(sigmaOf(3), sigmaOf(4)), std::hypot(sigmaOf(3), sigmaOf(4)));
    report("twice", blurredBy(BlurEffect(BlurEffect(3, 3, TileMode::Decal), 4, 4, TileMode::Decal)), overWhite(twice), 2);
    // A narrow one after a wide one: on what that scaled down.
    float wide = std::hypot(sigmaOf(30), sigmaOf(3));
    report("narrow after wide", blurredBy(BlurEffect(BlurEffect(30, 30, TileMode::Decal), 3, 3, TileMode::Decal)),
           overWhite(gaussian(base, bounds, TileMode::Decal, wide, wide)), 3);
    // Enlarged by a transform: blurred and moved in the layer's pixels, then enlarged (the layer drawn at twice them).
    Pixels enlarged = g.render(n, n, 1, [&](GraphicsContext& c) {
        Matrix m;
        m.scale(2, 2);
        c.pushTransform(m);
        c.pushEffectLayer({12, 12, 24, 24}, OffsetEffect(BlurEffect(2, 2, TileMode::Decal), {1.5f, -1}), 1);
        c.pushTransform([] {
            Matrix half;
            half.scale(0.5f, 0.5f);
            return half;
        }());
        content(c);
        c.popTransform();
        c.popLayer();
        c.popTransform();
    });
    Image big = gaussian(base, bounds, TileMode::Decal, 2 * sigmaOf(2), 2 * sigmaOf(2)), bigMoved{n, n, std::vector<float>(big.rgba.size(), 0.f)};
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            if (x - 3 >= 0 && y + 2 < n) std::copy_n(big.at(x - 3, y + 2), 4, bigMoved.at(x, y));
    report("enlarged", enlarged, overWhite(bigMoved), 2);
    // Faded, and cut by a rounded clip around it: what the blur made, cut.
    Pixels cut = g.render(n, n, 1, [&](GraphicsContext& c) {
        c.pushClipRoundRect({24, 24, 48, 48}, 16);
        c.pushEffectLayer({24, 24, 48, 48}, BlurEffect(4, 4), 0.5f);
        content(c);
        c.popLayer();
        c.popClip();
    });
    Image half = gaussian(base, bounds, TileMode::Clamp, sigmaOf(4), sigmaOf(4));
    for (float& v : half.rgba) v *= 0.5f;
    Pixels wantHalf = overWhite(half);
    // Inside the clip (a pixel in from its edge) what the blur made; outside it, nothing.
    auto inside = [](int x, int y, float in) {
        float cx = std::clamp(float(x) + 0.5f, 24 + 16.f, 72 - 16.f), cy = std::clamp(float(y) + 0.5f, 24 + 16.f, 72 - 16.f);
        return float(x) + 0.5f > 24 + in && float(x) + 0.5f < 72 - in && float(y) + 0.5f > 24 + in && float(y) + 0.5f < 72 - in &&
               std::hypot(float(x) + 0.5f - cx, float(y) + 0.5f - cy) < 16 - in;
    };
    report("faded, cut", cut, wantHalf, 2, [&](int x, int y) { return !inside(x, y, 1); });
    int outside = worstAgainst(cut, [&](int, int) { static const uint8_t white[4] = {255, 255, 255, 255}; return white; },
                               [&](int x, int y) { return inside(x, y, -1); });
    CHECK(outside == 0);
    // Too big for a texture: blurred around what shows, the same there.
    Pixels tall = g.render(n, n, 1, [&](GraphicsContext& c) {
        c.pushEffectLayer({24, -6000, 48, 12096}, BlurEffect(4, 4, TileMode::Decal), 1);
        content(c);
        c.popLayer();
    });
    gpu::IRect tallBounds{24, -6000, 48, 12096};
    Image tallBase = painted(n, n, {{{0, 0, 96, 10}, Color{0, 0, 0, 1}}});
    for (const auto& [r, col] : fills) tallBase = [&] {
        Image i = tallBase;
        for (int y = r.y; y < r.y + r.height; ++y)
            for (int x = r.x; x < r.x + r.width; ++x) {
                float* q = i.at(x, y);
                q[0] = col.r, q[1] = col.g, q[2] = col.b, q[3] = 1;
            }
        return i;
    }();
    for (int y = 0; y < n; ++y)  // (only what is within its bounds across)
        for (int x = 0; x < n; ++x)
            if (x < 24 || x >= 72) std::fill_n(tallBase.at(x, y), 4, 0.f);
    report("too big for a texture", tall, overWhite(gaussian(tallBase, tallBounds, TileMode::Decal, sigmaOf(4), sigmaOf(4))), 2);
}

// Curves cut into lines stay within kTolerance of the curve, in the target's pixels.
void curvesAreFlattenedClosely() {
    Path circle;
    circle.addOval({0, 0, 100, 100});
    for (float scale : {1.f, 2.625f, 4.f}) {
        std::vector<gpu::Contour> contours = gpu::flattenPath(circle.commands(), {{0, 0}, scale, {0, 0}});
        CHECK(contours.size() == 1);
        float r = 50 * scale, worst = 0;
        const auto& pts = contours[0].points;
        for (size_t i = 0; i + 1 < pts.size(); ++i) {  // each line's middle, the farthest from the arc
            Offset m{(pts[i].x + pts[i + 1].x) / 2 - r, (pts[i].y + pts[i + 1].y) / 2 - r};
            worst = std::max(worst, std::abs(std::hypot(m.x, m.y) - r));
        }
        CHECK(worst <= gpu::kTolerance + 0.05f);  // (the oval is cubics, themselves within 0.03% of the circle)
    }
    // Finer when asked (a transform enlarges it after): within that.
    std::vector<gpu::Contour> fine = gpu::flattenPath(circle.commands(), {{0, 0}, 1, {0, 0}, 0.02f});
    float worst = 0;
    const auto& pts = fine[0].points;
    for (size_t i = 0; i + 1 < pts.size(); ++i) {
        Offset m{(pts[i].x + pts[i + 1].x) / 2 - 50, (pts[i].y + pts[i + 1].y) / 2 - 50};
        worst = std::max(worst, std::abs(std::hypot(m.x, m.y) - 50));
    }
    CHECK(worst <= 0.02f + 0.02f);
}

// The renderer's projective transforms: composed, inverted, how much they stretch; polygons
// projected through one are cut where they pass behind the eye.
void projectionsAreExact() {
    auto at = [](Offset p, float x, float y) { return std::abs(p.x - x) < 1e-3f && std::abs(p.y - y) < 1e-3f; };
    gpu::Projective m = gpu::Projective::translation(10, 0) * gpu::Projective::scaling(3, 0.5f);  // scaled, then moved
    CHECK(at(m.apply(2, 4), 16, 2));
    CHECK(at(m.inverse().apply(16, 2), 2, 4));
    CHECK(std::abs(m.maxScale(0, 0) - 3) < 1e-4f && std::abs(m.minScale(0, 0) - 0.5f) < 1e-4f);
    CHECK(m.axisAligned() && !m.identity());
    Matrix halved;
    halved(3, 3) = 2;  // w = 2 everywhere: half size, and w made 1
    gpu::Projective h = gpu::Projective::of(halved);
    CHECK(h.m[8] == 1 && at(h.apply(8, 6), 4, 3));
    Matrix seen;
    seen(0, 3) = 0.2f;  // w = 1 + x / 5: behind the eye left of x = -5
    gpu::Projective p = gpu::Projective::of(seen);
    CHECK(std::abs(p.maxScale(5, 0) - 0.5f) < 1e-4f);  // twice as far at x = 5: half as big
    gpu::Contour square{{{-10, -10}, {10, -10}, {10, 10}, {-10, 10}}, true};
    std::vector<gpu::Contour> cut = gpu::project({square}, p);
    CHECK(cut.size() == 1);
    bool finite = true, right = true;
    for (Offset q : cut[0].points) {
        finite = finite && std::isfinite(q.x) && std::isfinite(q.y);
        right = right && q.x > -6e4f;  // (the cut is just in front of the eye: far, but finite)
    }
    CHECK(finite && right && cut[0].points.size() == 4);  // the two corners behind: two points where its edges pass w = 0
    CHECK(std::any_of(cut[0].points.begin(), cut[0].points.end(), [&](Offset q) { return at(q, 10.f / 3, 10.f / 3); }));
}

void scenesMatchTheirPictures(Gpu& g) {
    struct Scene {
        const char* name;
        int w, h;
        float scale;
        void (*draw)(GraphicsContext&);
    };
    const Scene scenes[] = {{"rects", 192, 192, 1, rects},   {"lines", 192, 192, 1, lines},   {"shadows", 192, 192, 1, shadows},
                            {"text", 192, 192, 1, text},     {"images", 192, 192, 1, images}, {"layers", 192, 192, 1, layers},
                            {"paths", 192, 192, 1, paths},   {"outline-layers", 192, 192, 1, outlineLayers},
                            {"gradients", 192, 192, 1, gradients},   {"blend-modes", 192, 192, 1, blendModes},
                            {"transforms", 192, 192, 1, transforms},  {"graphics-layers", 192, 196, 1, graphicsLayers},
                            {"effects", 192, 192, 1, effects},
                            {"density", 192, 192, 2.625f, fractionalDensity}};
    for (const Scene& s : scenes) {
        Pixels p = g.render(s.w, s.h, s.scale, s.draw);
        matchesGolden(s.name, p);
    }
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, "offscreen");
    bool required = std::getenv("KOMPOZ_GPU_REQUIRED") && std::string(std::getenv("KOMPOZ_GPU_REQUIRED")) == "1";
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::printf("no SDL video: %s\n", SDL_GetError());
        return required ? 1 : 0;
    }
    int result = 0;
    {
        Gpu g;
        std::string why;
        if (!g.start(&why)) {
            std::printf("skipped: %s\n", why.c_str());
            result = required ? 1 : 0;
        } else {
            std::printf("device: %s\n", g.device->deviceName().c_str());
            edgesAreCoveredExactly(g);
            edgesCutPixelsExactly(g);
            layerAlphaIsExact(g);
            clipsAndTranslationsAreExact(g);
            pathsAreCoveredExactly(g);
            strokesCoverOnce(g);
            curvesAreFlattenedClosely();
            projectionsAreExact();
            gradientsMatchColorAt(g);
            blendModesMatchBlend(g);
            colorFiltersMatchApply(g);
            targetCopiesFitEveryTarget(g);
            transformsCarryEverything(g);
            transformedEdgesAreExact(g);
            clipsCutAsTheirShapes(g);
            graphicsLayersCompositeAsCompose(g);
            blursAreGaussian(g);
            scenesMatchTheirPictures(g);
            CHECK(g.device->validationErrors() == 0);  // (counted with KOMPOZ_GPU_VALIDATION=1)
            std::printf("%d checks, %d failed\n", checks, failures);
            result = failures == 0 ? 0 : 1;
        }
    }
    SDL_Quit();
    return result;
}
