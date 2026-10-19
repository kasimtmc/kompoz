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
                            {"transforms", 192, 192, 1, transforms},  {"density", 192, 192, 2.625f, fractionalDensity}};
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
            transformsCarryEverything(g);
            transformedEdgesAreExact(g);
            scenesMatchTheirPictures(g);
            CHECK(g.device->validationErrors() == 0);  // (counted with KOMPOZ_GPU_VALIDATION=1)
            std::printf("%d checks, %d failed\n", checks, failures);
            result = failures == 0 ? 0 : 1;
        }
    }
    SDL_Quit();
    return result;
}
