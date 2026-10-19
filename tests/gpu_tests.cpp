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

    // `draw` on a white w x h px target at `scale` px per dp, read back.
    Pixels render(int w, int h, float scale, const std::function<void(GraphicsContext&)>& draw) {
        fonts->setScale(scale);
        renderer->scale = scale;
        device->beginFrame();
        auto target = device->createTexture(w, h, gpu::Format::RGBA8, gpu::TextureUsage::Sampled | gpu::TextureUsage::RenderTarget);
        renderer->beginTarget(*target, gpu::ClearColor{1, 1, 1, 1});
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

void fractionalDensity(GraphicsContext& c) {  // at 2.625 px per dp, as a phone's
    c.fillRoundRect({4, 4, 30, 20}, 6, Color{0.2f, 0.5f, 0.3f, 1});
    c.strokeRoundRect({38, 4, 30, 20}, 6, 1, Color{0.2f, 0.2f, 0.2f, 1});
    c.drawText("2.625x", {4, 44}, Font{14}, Color{0.1f, 0.1f, 0.1f, 1}, false);
    c.drawShadow({4, 52, 30, 14}, 4, {Color{0, 0, 0, 0.4f}, {0, 2}, 4, 0}, false);
    c.fillRoundRect({4, 52, 30, 14}, 4, colors::White);
    c.drawLine({40, 52}, {68, 66}, 1, Color{0.6f, 0.1f, 0.1f, 1});
}

// --- Exact checks --------------------------------------------------------------------------------

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
                            {"gradients", 192, 192, 1, gradients},
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
            layerAlphaIsExact(g);
            clipsAndTranslationsAreExact(g);
            pathsAreCoveredExactly(g);
            curvesAreFlattenedClosely();
            gradientsMatchColorAt(g);
            scenesMatchTheirPictures(g);
            CHECK(g.device->validationErrors() == 0);  // (counted with KOMPOZ_GPU_VALIDATION=1)
            std::printf("%d checks, %d failed\n", checks, failures);
            result = failures == 0 ? 0 : 1;
        }
    }
    SDL_Quit();
    return result;
}
