// The app core: a Kompoz app on any platform (app_core.hpp). What is the platform's own comes
// through Platform; nothing here knows which one it is on.

#include "app_core.hpp"

#include <kompoz/display_list.hpp>
#include <kompoz/host.hpp>
#include <kompoz/preview.hpp>

#include "../common/system.hpp"
#include "../gpu/renderer.hpp"
#include "../common/font_system.hpp"
#include "../common/image_loader.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <deque>
#include <filesystem>
#include <format>
#include <fstream>
#include <sstream>
#include <thread>

#if defined(__ANDROID__) || (defined(__APPLE__) && defined(KOMPOZ_IOS))
#define KOMPOZ_PHONE 1  // the app's lifecycle follows it going to the background and back, not its window
#endif

#if defined(__GNUC__) || defined(__clang__)
#define KOMPOZ_PRINTF(at, from) __attribute__((format(printf, at, from)))  // (its arguments checked against its format)
#else
#define KOMPOZ_PRINTF(at, from)
#endif

namespace kompoz::platform {

// KOMPOZ_SCRIPT="click 100 200; type hello; key enter; wait 30; shot out.png; quit"
// drives the app without a user, for UI tests and screenshots. Time is virtual while a
// script runs (16.7 ms per frame), so animations are deterministic; then it goes on in real
// time, for whoever takes over where the script left the app.
struct Script {
    std::vector<std::string> commands;
    size_t next = 0;
    int waitFrames = 0;
    int64_t sleepUntilNs = 0;  // `sleep`: real time going by (the platform's clock), for work on other threads
    int64_t clock = 0;
    // A swipe in progress: one pointer move per frame, so it has a real velocity.
    Offset swipeFrom, swipeTo;
    int swipeFrames = 0, swipeStep = 0;
    bool realtime = false;  // `realtime`: from there on the clock is real and frames are paced as without a script

    bool enabled() const { return !commands.empty(); }
    bool running() const { return next < commands.size() || waitFrames > 0 || sleepUntilNs > 0 || swipeStep < swipeFrames; }
};

struct AppState {
    explicit AppState(Platform& p) : platform(p) {}
    Platform& platform;
    Application config;
    bool mediaProbed = false;  // a microphone and a camera looked for (after the first frame)
    std::unique_ptr<FontSystem> fonts;
    // Kompoz's GPU renderer (gpu/renderer.hpp) on the device KOMPOZ_GPU chose, with the kept frame
    // in a texture of its own.
    std::unique_ptr<gpu::Device> gpu;
    bool windowOpen = false;
    std::unique_ptr<gpu::Renderer> gpuCanvas;
    std::unique_ptr<gpu::Texture> gpuFrame;
    // What changed in it (px) in each frame shown, the last first (none: all of it), for a
    // swapchain image holding an earlier frame (Device::swapchainAge): only that copied over.
    std::deque<std::optional<std::vector<gpu::IRect>>> shownChanges;
    bool gpuInFrame = false;
    std::unique_ptr<kompoz::detail::BitmapRenderer> bitmaps;  // draws into bitmaps (AppBitmaps)
    GraphicsContext& graphics() { return *gpuCanvas; }
    float pixelScale() const { return gpuCanvas->scale; }
    std::unique_ptr<ComposeHost> host;
    float scale = 1, pointToDp = 1;
    bool quit = false;
    Script script;
    int64_t clockOffset = 0;  // added to the platform's clock after a script whose virtual time got ahead of it
    bool trace = false;       // KOMPOZ_TRACE: log what each recomposition ran
    // KOMPOZ_PROFILE: where frame time goes. Each slow frame is logged (input handling, compose +
    // layout, drawing, present, offscreen layers, glyphs rasterized), and a summary every second.
    bool profile = false;
    struct Profile {
        uint64_t lastStart = 0, eventsNs = 0, summaryStart = 0;
        uint64_t lastShown = 0;  // when the last frame shown was handed to the display
        uint64_t dueSince = 0;   // when frames started being due (busy since)
        bool busy = false;       // the last iteration's frame asked for the next (frames meant to follow each other)
        int frames = 0, slow = 0, unchanged = 0;  // unchanged: recorded, nothing on the screen differed
        int shown = 0, missed = 0;                // missed: display refreshes between frames shown back to back
        double workTotal = 0, workMax = 0, waitTotal = 0, intervalMax = 0, drawnArea = 0;  // drawnArea: fractions of the screen, summed
        double composeTotal = 0, drawTotal = 0, recordTotal = 0;  // the work's parts (recording: of the drawing)
        uint64_t recordNs = 0;   // this frame's recording (drawFrame)
        uint64_t acquireNs = 0;  // and its wait for an image of the window to draw into (a wait, not work)
        int gpuFrames = 0;
        double gpuTotal = 0, gpuMax = 0;
    } prof;
    bool gpuTimed = false;  // KOMPOZ_PROFILE: the GPU's time measured (Device::setGpuTiming)
    // The frame is recorded, and only what changed on the screen is drawn again, into pixels
    // kept in a texture (`gpuFrame`) that is then shown; a frame that changes nothing isn't shown.
    std::unique_ptr<kompoz::DisplayList> display;
    std::unique_ptr<kompoz::DisplayList> fresh;  // KOMPOZ_KEEP_CHECK's: each frame recorded drawing every node
    bool showFrame = true;  // the window needs the kept pixels again (exposed, first frame)
    // Frames still to draw whole and show after the window was resized. Android resizes its
    // surface a frame or two after telling us (the keyboard coming or going): what is shown
    // meanwhile goes into buffers of the old size, which it would keep stretched over the new
    // one until something changed.
    int resizeFrames = 0;
    // The window's size, scale, insets, theme or language changed (updateMetrics to do): read once
    // before the next frame or input, however many events said so. (Each update asks Android for
    // its settings, palettes and view configuration: 4 ms on a Redmi Note 9 Pro, and a window
    // starts with three such events.)
    bool metricsChanged = false;
    bool focused = true, minimized = false, hidden = false;  // desktops' and browsers' lifecycle
    uint64_t framePeriodNs = 16'666'667;  // the display's refresh interval: frames not shown are paced to it
    int64_t lastFrameTime = -1;           // the last frame's time, on the display's beat (frameTimeOnBeat)
    bool damageCheck = false;  // KOMPOZ_DAMAGE_CHECK: each frame compared with all of it drawn
    bool keepCheck = false;    // KOMPOZ_KEEP_CHECK: each frame's recording compared with one drawing every node
    bool damageShow = false;   // KOMPOZ_DAMAGE_SHOW: outlines what was drawn again
    bool fullRedraw = false;   // KOMPOZ_FULL_REDRAW: all of every frame drawn, as before
    // A drag from another app coming in: what it drops, gathered until it is complete.
    bool dropping = false;
    std::vector<std::string> dropFiles;
    std::string dropText;
    kompoz::Offset dropAt;
    int damageFrames = 0, damageWrong = 0, keepFrames = 0, keepWrong = 0;
    kompoz::detail::RecompositionStats traced;

    ~AppState() {
        stopImageLoader();  // its threads end before the rest goes
        if (damageCheck) logf("kompoz: damage check: %d of %d frames differed", damageWrong, damageFrames);
        if (keepCheck) logf("kompoz: keep check: %d of %d frames recorded otherwise", keepWrong, keepFrames);
        host.reset();  // runs effect cleanups while the window still exists
        kompoz::detail::setBitmapRenderer(nullptr);
        if (gpuCanvas) {  // what only it has of bitmaps still about: back into them
            gpu->beginFrame();
            gpuCanvas->releaseBitmaps();
            gpu->endFrame(false);
        }
        bitmaps.reset();
        gpuFrame.reset();
        gpuCanvas.reset();
        gpu.reset();
        fonts.reset();
        if (windowOpen) platform.closeWindow();
    }

    // printf-style, into the platform's log.
    void logf(const char* format, ...) const KOMPOZ_PRINTF(2, 3) {
        char line[1024];
        va_list args;
        va_start(args, format);
        std::vsnprintf(line, sizeof line, format, args);
        va_end(args);
        platform.log(line);
    }
    bool flag(std::string_view name) const { return platform.option(name).has_value(); }
    uint64_t now() const { return uint64_t(platform.now()); }
    Offset toDp(float x, float y) const { return {x * pointToDp, y * pointToDp}; }
    // Frame and input times are on one clock: a script's virtual one while it runs, the platform's after.
    int64_t clock() const { return platform.now() + clockOffset; }
    int64_t eventTime(int64_t timestamp) const {
        return script.running() && !script.realtime ? script.clock : (timestamp < 0 ? platform.now() : timestamp) + clockOffset;
    }
};
using App = AppState;

namespace {

// The device for mediaQuery, as the platform knows it (the microphone and the camera looked for
// only after the first frame: starting the systems that tell takes a few tens of milliseconds).
void refreshMedia(App& app) { app.host->setMediaInfo(app.platform.media(app.mediaProbed)); }

void updateMetrics(App& app) {
    Metrics m = app.platform.metrics();
    float scale = m.scale > 0 ? m.scale : 1;
    app.scale = scale;
    app.pointToDp = m.pointToDp > 0 ? m.pointToDp : 1;
    app.fonts->setScale(scale);
    if (app.gpuCanvas) app.gpuCanvas->scale = scale;
    app.fonts->setFontScale(m.fontScale);
    app.host->setDensity({scale, m.fontScale});
    app.host->setViewportSize({float(m.pixelWidth) / scale, float(m.pixelHeight) / scale});
    // Elevation shadows' light: above the window's top middle, as Android's (its theme's values).
    kompoz::ShadowLight light;
    light.center = {float(m.pixelWidth) / scale / 2, 0};
    light.density = scale;
    for (kompoz::GraphicsContext* c : {static_cast<kompoz::GraphicsContext*>(app.gpuCanvas.get()), static_cast<kompoz::GraphicsContext*>(app.display.get())})
        if (c) c->light = light;
    app.framePeriodNs = uint64_t(1e9 / (m.refreshRate > 0 ? m.refreshRate : 60.f));
    // System bars and display cutouts (iOS notch, desktop); Android reports its own insets.
    if (m.safeArea) {
        float k = app.pointToDp;
        const Rect& safe = *m.safeArea;
        app.host->setWindowInsets({safe.x * k, safe.y * k, (float(m.pointWidth) - safe.x - safe.width) * k, (float(m.pointHeight) - safe.y - safe.height) * k});
    }
    app.host->setSystemDarkTheme(m.darkTheme);
    // The preferred language: right to left when it is written so (KOMPOZ_LAYOUT_DIRECTION=rtl|ltr
    // decides instead), and the library's texts in it (KOMPOZ_LANGUAGE=tr|en decides instead).
    std::string language = m.language;
    bool rtl = kompoz::isRtlLanguage(language);
    if (auto forced = app.platform.option("KOMPOZ_LAYOUT_DIRECTION")) rtl = *forced == "rtl";
    app.host->setLayoutDirection(rtl ? kompoz::LayoutDirection::Rtl : kompoz::LayoutDirection::Ltr);
    if (auto forced = app.platform.option("KOMPOZ_LANGUAGE")) language = *forced;
    app.host->setStrings(kompoz::Strings::forLanguage(language));
    app.platform.metricsUpdated(*app.host);
    refreshMedia(app);
    app.host->requestRedraw();
}

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

// Bitmaps drawn into (graphics::Canvas(ImageBitmap)) by the GPU renderer (kept as its texture
// where the bitmap's config lets it).
class AppBitmaps final : public detail::BitmapRenderer {
public:
    explicit AppBitmaps(App& app) : app_(app) {}
    void draw(const ImageBitmap& bitmap, const DisplayList& draws) override {
        bool ownFrame = !app_.gpuInFrame;  // (between frames: a frame of its own)
        if (ownFrame) app_.gpu->beginFrame();
        app_.gpuCanvas->drawIntoBitmap(bitmap, draws);
        if (ownFrame) {
            app_.gpuCanvas->flush();
            app_.gpu->endFrame(false);
        }
    }
    void readBack(const ImageBitmap& bitmap) override {
        bool ownFrame = !app_.gpuInFrame;
        if (ownFrame) app_.gpu->beginFrame();
        app_.gpuCanvas->readBackBitmap(bitmap);
        if (ownFrame) app_.gpu->endFrame(false);
    }
    TextMeasurer* measurer() override { return app_.fonts.get(); }

private:
    App& app_;
};

ImageBitmap renderImage(App& app, Size size, const std::function<void(GraphicsContext&)>& draw) {
    return renderImageGpu(app, size, draw);
}

// KOMPOZ_KEEP_CHECK: the frame just recorded, which kept the nodes that didn't change, against
// one drawing every node (what was kept must be what drawing them records).
void checkKept(App& app, Size screen, float pixel) {
    if (!app.fresh) app.fresh = std::make_unique<kompoz::DisplayList>(*app.fonts);
    app.fresh->light = app.display->light;  // (the elevation shadows')
    app.fresh->begin(screen, pixel);
    app.host->draw(*app.fresh);
    app.fresh->end();
    ++app.keepFrames;
    std::string why;
    if (!app.display->sameFrame(*app.fresh, &why)) {
        ++app.keepWrong;
        app.logf("kompoz: keep check: %s", why.c_str());
    }
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
        app.logf("kompoz: damage check: %d pixels differ (by up to %d) in %d,%d %dx%d px; drawn again: %.0f,%.0f %.0fx%.0f px",
                count, worst, x0, y0, x1 - x0 + 1, y1 - y0 + 1, damage.x * s, damage.y * s, damage.width * s, damage.height * s);
    }
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
    float area = 0;      // drawn again, of the screen
};

// How much of the screen the last frame drew again (its areas), 0 if nothing (a little if anything).
float drawnArea(const DisplayList& display, Size screen) {
    float drawn = 0;
    for (const Rect& a : display.areas()) drawn += a.width * a.height;
    return display.areas().empty() ? 0 : std::max(1e-4f, drawn / (screen.width * screen.height));
}

// The kept frame is a texture drawn into in a frame of the device's (begun by the caller), copied
// onto the swapchain image (none without a window to show it in: drawn only into the texture).
Drawn drawFrame(App& app, const std::string& shot) {
    gpu::Device& device = *app.gpu;
    gpu::Renderer& r = *app.gpuCanvas;
    int pw = 0, ph = 0;
    app.platform.pixelSize(&pw, &ph);
    pw = std::max(pw, 1), ph = std::max(ph, 1);
    if (!app.gpuFrame || app.gpuFrame->width() != pw || app.gpuFrame->height() != ph) {
        app.gpuFrame = device.createTexture(pw, ph, gpu::Format::RGBA8, gpu::TextureUsage::Sampled | gpu::TextureUsage::RenderTarget);
        app.display->invalidate();
        app.showFrame = true;
    }
    r.layerCount = r.keptLayers = r.pathCount = r.keptPaths = r.texturesMade = r.pathShadows = r.keptShadows = 0;
    float scale = r.scale;
    Size screen{float(pw) / scale, float(ph) / scale};
    uint64_t recording = app.now();
    app.display->begin(screen, 1 / scale);
    app.host->draw(*app.display);
    if (app.fullRedraw) app.display->invalidate();
    std::optional<Rect> damage = app.display->end();
    app.prof.recordNs = app.now() - recording;
    if (app.keepCheck) checkKept(app, screen, 1 / scale);
    bool keptChanges = true;  // (drawn only where they changed, into the frame before)
    if (damage) keptChanges = r.drawFrame(*app.gpuFrame, *app.display, true);
    if (damage && app.damageCheck) {
        auto full = device.createTexture(pw, ph, gpu::Format::RGBA8, gpu::TextureUsage::Sampled | gpu::TextureUsage::RenderTarget);
        r.drawFrame(*full, *app.display, false);
        r.flush();
        std::vector<uint8_t> kept = device.readPixels(*app.gpuFrame, {0, 0, pw, ph}), fresh = device.readPixels(*full, {0, 0, pw, ph});
        if (kept.size() == fresh.size() && !kept.empty()) compareDamage(app, *damage, kept.data(), pw * 4, fresh.data(), pw * 4, pw, ph);
    }
    float area = drawnArea(*app.display, screen);
    if (!damage && shot.empty() && !app.showFrame) {
        r.flush();
        return {false, 0};
    }
    uint64_t acquiring = app.now();
    gpu::Texture* swapchain = device.swapchainTexture();
    app.prof.acquireNs = app.now() - acquiring;
    if (swapchain) {
        // What this frame changed in the kept pixels: its areas and where its moves went, out to
        // whole pixels (all of it when the window needs it all, or the outlines of what was drawn
        // again are shown, which stay in the image).
        std::optional<std::vector<gpu::IRect>> changed = std::vector<gpu::IRect>{};
        if (app.showFrame || app.damageShow || !keptChanges) changed.reset();
        else if (damage) {
            for (const Rect& a : app.display->areas())
                changed->push_back({int(std::floor(a.x * scale)), int(std::floor(a.y * scale)), int(std::ceil(a.right() * scale)) - int(std::floor(a.x * scale)),
                                    int(std::ceil(a.bottom() * scale)) - int(std::floor(a.y * scale))});
            for (const DisplayList::Move& m : app.display->moves())  // (as Renderer::drawFrame moves them)
                changed->push_back({int(std::lround(m.from.x * scale)) + int(std::lround(m.by.x * scale)), int(std::lround(m.from.y * scale)) + int(std::lround(m.by.y * scale)),
                                    int(std::lround(m.from.width * scale)), int(std::lround(m.from.height * scale))});
        }
        app.shownChanges.push_front(changed);
        if (app.shownChanges.size() > 8) app.shownChanges.pop_back();
        // A swapchain image holding the frame `age` frames ago: what changed since copied over.
        std::optional<std::vector<gpu::IRect>> copied;
        if (int age = device.swapchainAge(); age > 0 && size_t(age) <= app.shownChanges.size()) {
            copied.emplace();
            for (int i = 0; i < age && copied; ++i)
                if (!app.shownChanges[size_t(i)]) copied.reset();
                else copied->insert(copied->end(), app.shownChanges[size_t(i)]->begin(), app.shownChanges[size_t(i)]->end());
        }
        // Each in the frame, those overlapping as one (each pixel copied once); all of it once they
        // take half of it (an image's pixels kept cost loading, on a tiling GPU, as all copied don't).
        if (copied) {
            std::vector<gpu::IRect>& c = *copied;
            for (gpu::IRect& r : c) {
                int x0 = std::max(r.x, 0), y0 = std::max(r.y, 0), x1 = std::min(r.x + r.width, pw), y1 = std::min(r.y + r.height, ph);
                r = {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
            }
            std::erase_if(c, [](const gpu::IRect& r) { return r.width == 0 || r.height == 0; });
            for (bool merged = true; merged;) {
                merged = false;
                for (size_t i = 0; i < c.size() && !merged; ++i)
                    for (size_t j = i + 1; j < c.size() && !merged; ++j)
                        if (c[i].x < c[j].x + c[j].width && c[j].x < c[i].x + c[i].width && c[i].y < c[j].y + c[j].height && c[j].y < c[i].y + c[i].height) {
                            int x0 = std::min(c[i].x, c[j].x), y0 = std::min(c[i].y, c[j].y);
                            c[i] = {x0, y0, std::max(c[i].x + c[i].width, c[j].x + c[j].width) - x0, std::max(c[i].y + c[i].height, c[j].y + c[j].height) - y0};
                            c.erase(c.begin() + std::ptrdiff_t(j));
                            merged = true;
                        }
            }
            int64_t area = 0;
            for (const gpu::IRect& r : c) area += int64_t(r.width) * r.height;
            if (area * 2 > int64_t(pw) * ph) copied.reset();
        }
        app.showFrame = false;
        if (!copied) device.discard(*swapchain);  // (all of it copied over: what it held isn't loaded, on a tiling GPU)
        r.beginTarget(*swapchain);
        int turns = device.swapchainTurns();
        bool flipped = device.swapchainFlipped();
        r.blit(*app.gpuFrame, turns, flipped, copied ? &*copied : nullptr);
        if (damage && app.damageShow) {  // what was drawn again, outlined (only in the window, not the kept pixels)
            Matrix turned;  // (as the frame is: a point's x and y from these, then moved)
            float c[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};  // (cos, sin) of the turns
            float cs = c[turns & 3][0], sn = c[turns & 3][1];
            turned(0, 0) = cs, turned(0, 1) = sn, turned(1, 0) = -sn, turned(1, 1) = cs;
            turned(3, 0) = (turns & 3) == 1 ? screen.height : (turns & 3) == 2 ? screen.width : 0;
            turned(3, 1) = (turns & 3) == 2 ? screen.height : (turns & 3) == 3 ? screen.width : 0;
            if (flipped) {  // (upside down after that)
                turned(0, 1) = -turned(0, 1), turned(1, 1) = -turned(1, 1);
                turned(3, 1) = ((turns & 1) ? screen.width : screen.height) - turned(3, 1);
            }
            r.pushTransform(turned);
            for (const Rect& a : app.display->areas()) r.strokeRoundRect(a, CornerRadii(), 3 / scale, Color{1, 0, 80 / 255.f, 1});
            r.popTransform();
        }
        r.endTarget();
    }
    r.flush();
    if (!shot.empty()) {
        std::vector<uint8_t> pixels = device.readPixels(*app.gpuFrame, {0, 0, pw, ph});
        savePixels(shot, pw, ph, pixels);
    }
    return {swapchain != nullptr, area};
}

void profileFrame(App& app, uint64_t start, uint64_t composed, uint64_t waited, uint64_t drawn, uint64_t presented, bool shown, int glyphs,
                  float area) {
    auto ms = [](uint64_t ns) { return double(ns) / 1e6; };
    App::Profile& p = app.prof;
    double period = ms(app.framePeriodNs);
    double interval = p.lastStart ? ms(start - p.lastStart) : 0;
    double acquire = ms(p.acquireNs);
    double events = ms(p.eventsNs), compose = ms(composed - start), wait = ms(waited) + acquire,
           draw = ms(drawn - composed - waited) - acquire, present = ms(presented - drawn);
    double work = events + compose + draw;
    p.lastStart = start;
    p.eventsNs = 0;
    if (!p.summaryStart) p.summaryStart = start;
    ++p.frames;
    p.workTotal += work;
    p.composeTotal += compose, p.drawTotal += draw, p.recordTotal += ms(p.recordNs);
    p.recordNs = p.acquireNs = 0;
    p.workMax = std::max(p.workMax, work);
    p.waitTotal += wait;
    p.intervalMax = std::max(p.intervalMax, interval);
    if (area == 0) ++p.unchanged;
    p.drawnArea += area < 0 ? 1 : area;
    if (shown) {
        // Refreshes of the display that went by without a frame, between two shown back to back
        // (the app busy all along: not those it waited through for an event).
        if (p.lastShown && p.busy) p.missed += std::max(0, int(std::lround(ms(presented - std::max(p.lastShown, p.dueSince)) / period)) - 1);
        p.lastShown = presented;
        ++p.shown;
    }
    if (work > period || interval > 2 * period + 1) {
        ++p.slow;
        app.logf("kompoz: frame %.1f ms after the last: input %.1f, compose+layout %.1f, wait %.1f, draw %.1f, present %.1f ms; %d nodes "
                "drawn (%d kept), %d layers (%d kept), %d paths (%d kept), %d path shadows (%d kept), %d textures made, %d glyphs, %.0f%% drawn again",
                interval, events, compose, wait, draw, present, app.display->drawnNodes(), app.display->keptNodes(),
                app.gpuCanvas->layerCount, app.gpuCanvas->keptLayers, app.gpuCanvas->pathCount, app.gpuCanvas->keptPaths, app.gpuCanvas->pathShadows, app.gpuCanvas->keptShadows, app.gpuCanvas->texturesMade, glyphs,
                (area < 0 ? 1 : area) * 100);
    }
    if (start - p.summaryStart >= 1'000'000'000) {
        char gpu[96] = "";
        if (p.gpuFrames) std::snprintf(gpu, sizeof gpu, ", GPU avg %.1f max %.1f ms", p.gpuTotal / p.gpuFrames, p.gpuMax);
        app.logf("kompoz: %d frames in %.0f ms (%d shown, %d refreshes missed; %d changed nothing on the screen; %.0f%% of it drawn again on "
                "average), work avg %.1f max %.1f ms (compose+layout %.1f, draw %.1f, of it recording %.1f), wait avg %.1f ms%s, longest gap "
                "%.1f ms, %d slow",
                p.frames, ms(start - p.summaryStart), p.shown, p.missed, p.unchanged, p.drawnArea / p.frames * 100, p.workTotal / p.frames,
                p.workMax, p.composeTotal / p.frames, p.drawTotal / p.frames, p.recordTotal / p.frames, p.waitTotal / p.frames, gpu,
                p.intervalMax, p.slow);
        uint64_t lastShown = p.lastShown, dueSince = p.dueSince;
        bool busy = p.busy;
        p = {};
        p.lastStart = p.summaryStart = start;
        p.lastShown = lastShown, p.busy = busy, p.dueSince = dueSince;
    }
}

// KOMPOZ_PROFILE on the GPU renderer: the GPU's time for each frame it has done (Device::
// setGpuTiming) into the summary, and a frame that took the GPU longer than the display's
// period logged with its costliest passes.
void profileGpu(App& app) {
    App::Profile& p = app.prof;
    double period = double(app.framePeriodNs) / 1e6;
    for (gpu::GpuFrameTime& t : app.gpu->takeGpuTimes()) {
        ++p.gpuFrames;
        p.gpuTotal += t.ms;
        p.gpuMax = std::max(p.gpuMax, t.ms);
        if (t.ms <= period) continue;
        std::ranges::sort(t.passes, std::greater<>(), &gpu::GpuPassTime::ms);
        std::string passes;
        for (size_t i = 0; i < t.passes.size() && i < 6; ++i)
            passes += std::format("{}{} {:.1f}", i ? ", " : "", t.passes[i].label, t.passes[i].ms);
        if (t.passes.size() > 6) passes += std::format(" (+{} more)", t.passes.size() - 6);
        app.logf("kompoz: GPU frame %.1f ms%s%s", t.ms, passes.empty() ? "" : ": ", passes.c_str());
    }
}


void handleEvent(App& app, const Event& e);

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
        if (app.platform.now() < s.sleepUntilNs) {
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
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
        } else if (command == "realtime") {  // the rest in real time, frames paced to the display: as a person would see it
            s.realtime = true;
            app.clockOffset = std::max<int64_t>(0, s.clock - app.platform.now());  // (never back)
        } else if (command == "sleep") {  // real milliseconds: what other threads do (an image read, the wallpaper's colors)
            int ms = 0;
            in >> ms;
            s.sleepUntilNs = app.platform.now() + int64_t(std::max(ms, 0)) * 1'000'000;
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
            float k = 1 / app.pointToDp;  // dp -> window points
            for (platform::EventType type : {platform::EventType::DropBegin, platform::EventType::DropMove,
                                             command == "dropfile" ? platform::EventType::DropFile : platform::EventType::DropText,
                                             platform::EventType::DropComplete}) {
                platform::Event e{.type = type, .x = x * k, .y = y * k};
                if (type == platform::EventType::DropFile || type == platform::EventType::DropText) e.text = what;
                handleEvent(app, e);
            }
        } else if (command == "window") {  // window focuslost|focusgained|minimized|restored: as the window manager would
            std::string what;
            in >> what;
            static const std::pair<std::string_view, platform::EventType> names[] = {
                {"focuslost", platform::EventType::FocusLost}, {"focusgained", platform::EventType::FocusGained},
                {"minimized", platform::EventType::Minimized}, {"restored", platform::EventType::Restored}};
            for (const auto& [n, type] : names)
                if (what == n) handleEvent(app, {.type = type});
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
            if (w > 0 && h > 0) app.platform.setWindowSize(w, h);
            updateMetrics(app);
        } else if (command == "shot") {
            std::string path;
            in >> path;
            return path;
        } else if (command == "quit") {
            app.quit = true;
            return {};
        } else if (!command.empty()) {
            app.logf("kompoz: unknown script command '%s'", command.c_str());
        }
    }
    return {};
}

// The app's lifecycle on desktops and in browsers: resumed while the window has the focus,
// started while it is on the screen without it, created while it is minimized or hidden.
// (Phones follow the app going to the background and back instead.)
void updateLifecycle(App& app) {
#ifndef KOMPOZ_PHONE
    app.host->setLifecycleState(app.minimized || app.hidden ? LifecycleState::Created
                                : app.focused                ? LifecycleState::Resumed
                                                             : LifecycleState::Started);
#else
    (void)app;
#endif
}



// The mouse's buttons other than the left one, as onPointerEvent knows them.
PointerButton otherButton(platform::MouseButton button) {
    switch (button) {
    case platform::MouseButton::Right: return PointerButton::Secondary;
    case platform::MouseButton::Middle: return PointerButton::Tertiary;
    case platform::MouseButton::Back: return PointerButton::Back;
    default: return PointerButton::Forward;
    }
}

void handleEvent(App& app, const platform::Event& e) {
    using platform::EventType;
    ComposeHost& host = *app.host;
    bool window = e.type == EventType::FocusGained || e.type == EventType::FocusLost || e.type == EventType::Shown || e.type == EventType::Hidden ||
                  e.type == EventType::Minimized || e.type == EventType::Maximized || e.type == EventType::Restored || e.type == EventType::Exposed ||
                  e.type == EventType::Resized || e.type == EventType::MetricsChanged || e.type == EventType::PointerLeft;
    if (app.metricsChanged && !window) {  // (input meets the window as it is now)
        app.metricsChanged = false;
        updateMetrics(app);
    }
    switch (e.type) {  // pointer events carry the keys held down
    case EventType::MouseDown:
    case EventType::MouseUp:
    case EventType::MouseMove:
    case EventType::Wheel:
    case EventType::KeyDown:
    case EventType::KeyUp: host.setKeyboardModifiers(e.modifiers); break;
    default: break;
    }
    Offset at = app.toDp(e.x, e.y);
    switch (e.type) {
    // (AccentChanged, WallpaperChanged, AccessibilityRequest, Wake: the platform's own, Platform::handled.)
    case EventType::Quit: app.quit = true; break;
    case EventType::FocusGained:
    case EventType::FocusLost:
        app.focused = e.type == EventType::FocusGained;
        host.setWindowFocused(app.focused);
        updateLifecycle(app);
        break;
    case EventType::Minimized:
    case EventType::Maximized:
        app.minimized = e.type == EventType::Minimized;
        updateLifecycle(app);
        break;
    case EventType::Hidden:
        app.hidden = true;
        updateLifecycle(app);
        break;
#ifdef KOMPOZ_PHONE
    case EventType::DidEnterBackground:
        host.setLifecycleState(LifecycleState::Created);
        if (app.gpuCanvas) app.gpuCanvas->trimMemory();  // (as Android's renderer when the UI is hidden)
        break;
    case EventType::WillEnterForeground:
        host.setLifecycleState(LifecycleState::Started);
        break;
#endif
    case EventType::DevicesChanged:  // what mediaQuery tells of the pointer and the keyboard
        refreshMedia(app);
        break;
    case EventType::Resized:
        app.resizeFrames = 4;
        [[fallthrough]];
    case EventType::MetricsChanged:
        app.metricsChanged = true;
        break;
    case EventType::DidEnterForeground:
        updateMetrics(app);  // the wallpaper (and so the dynamic colors) may have changed meanwhile
#ifdef KOMPOZ_PHONE
        host.setLifecycleState(LifecycleState::Resumed);
#endif
        [[fallthrough]];
    case EventType::Shown:
    case EventType::Restored:
        if (e.type == EventType::Shown) app.hidden = false;
        if (e.type == EventType::Restored) app.minimized = false;
        updateLifecycle(app);
        // Back on the screen: Android gives the window a new, empty surface, which shows nothing
        // until a frame is put in it, and with nothing changed none would be (black until a tap).
        // All of it drawn again, in case the kept pixels went with the old one.
        if (app.display) app.display->invalidate();
        app.showFrame = true;
        host.requestRedraw();
        break;
#ifdef KOMPOZ_PHONE
    case EventType::WillEnterBackground:  // the system may end the app from now on
        host.setLifecycleState(LifecycleState::Started);  // paused: what that saves goes in the saved state (Platform::handled keeps it)
        break;
#endif
    case EventType::Exposed:
        app.showFrame = true;
        host.requestRedraw();
        break;
    case EventType::LowMemory:
        if (app.gpuCanvas) app.gpuCanvas->trimMemory();
        break;
    case EventType::SurfaceLost:  // the kept pixels are gone
        if (app.display) app.display->invalidate();
        app.showFrame = true;
        host.requestRedraw();
        break;
    case EventType::PointerLeft: host.pointerExit(); break;
    case EventType::MouseMove: host.pointerMove(at, app.eventTime(e.time)); break;
    // Files from a file manager, text from a browser: the dragAndDropTargets take them.
    case EventType::DropBegin:
        app.dropping = true;
        app.dropFiles.clear();
        app.dropText.clear();
        host.externalDragEntered({"text/uri-list", "text/plain"});  // which one isn't told until the drop
        break;
    case EventType::DropMove:
        if (!app.dropping) {
            app.dropping = true;
            host.externalDragEntered({"text/uri-list", "text/plain"});
        }
        host.externalDragMoved(at);
        break;
    case EventType::DropFile:
        if (!e.text.empty()) app.dropFiles.push_back(e.text);
        app.dropAt = at;
        break;
    case EventType::DropText:
        app.dropText += e.text;
        app.dropAt = at;
        break;
    case EventType::DropComplete: {
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
    case EventType::MouseDown:
        if (e.button == platform::MouseButton::Left) {
            host.pointerDown(at, app.eventTime(e.time), 0, kompoz::ComposeHost::PointerType::Mouse);
        } else {
            host.mouseButton(at, otherButton(e.button), true);
            if (e.button == platform::MouseButton::Right) host.secondaryClick(at);  // context menus
        }
        break;
    case EventType::MouseUp:
        if (e.button == platform::MouseButton::Left) host.pointerUp(at, app.eventTime(e.time));
        else host.mouseButton(at, otherButton(e.button), false);
        break;
    case EventType::TouchDown: host.pointerDown(at, app.eventTime(e.time), e.pointer); break;
    case EventType::TouchMove: host.pointerMove(at, app.eventTime(e.time), e.pointer); break;
    case EventType::TouchUp:
        host.pointerUp(at, app.eventTime(e.time), e.pointer);
        host.pointerExit();  // a lifted finger doesn't hover
        break;
    case EventType::Wheel:
        // Ctrl+wheel zooms (a transformable), as touchpad pinches do on Windows and the web.
        if (e.modifiers.ctrl && e.modifiers.shift && host.zoom(at, 1, -15 * e.dy)) break;  // Ctrl+Shift turns it, 15 degrees a notch
        if (e.modifiers.ctrl && host.zoom(at, std::pow(1.1f, e.dy))) break;
        host.scroll(at, e.dy, e.dx);
        break;
    case EventType::Pinch: host.zoom(at, e.scale); break;
    case EventType::Text: host.textInput(e.text); break;
    case EventType::Back:
        if (!host.keyDown(Key::Escape)) app.platform.leaveApp();  // as usual (Android leaves the app)
        break;
    case EventType::KeyDown:
    case EventType::KeyUp:
        if (e.key == Key::Unknown) break;
        if (e.type == EventType::KeyDown) host.keyDown(e.key, e.modifiers);
        else host.keyUp(e.key, e.modifiers);
        break;
    default: break;
    }
}


// KOMPOZ_GPU: the GPU renderer's backends, in the order they are tried. A name is that one; "auto"
// (or nothing) the platform's own order (Platform::backends).
std::vector<gpu::Backend> gpuOrder(App& app, const std::string& choice) {
    using gpu::Backend;
    if (choice == "vulkan") return {Backend::Vulkan};
    if (choice == "gles") return {Backend::GLES};
    if (choice == "metal") return {Backend::Metal};
    if (choice == "webgpu") return {Backend::WebGPU};
    if (choice != "auto" && !choice.empty()) app.logf("kompoz: KOMPOZ_GPU=%s: not vulkan, gles, metal, webgpu or auto: auto", choice.c_str());
    return app.platform.backends();
}

// Kompoz's GPU renderer's device on `backend`, in a window the platform opens for it; false (and
// the window closed again) if it can't be made.
bool startGpu(App& app, gpu::Backend backend) {
    gpu::Surface surface;
    std::string error;
    if (!app.platform.openWindow(app.config, backend, surface, &error)) {
        app.logf("kompoz: no window for %s: %s", gpu::backendName(backend), error.c_str());
        return false;
    }
    app.windowOpen = true;
    // The compiled pipelines beside the downloaded images, a file for each backend and build of the
    // shaders; another's goes (a driver keeps what it was given: an older build's pipelines would
    // stay in it, read at every start, the file growing with each update).
    if (std::filesystem::path dir = platform::appCacheDirectory(app.config.title); !dir.empty()) {
        std::string prefix = std::string("pipelines-") + gpu::backendName(backend);
        std::string name = std::format("{}-{:016x}", prefix, gpu::Renderer::shadersVersion());
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(dir, ec)) {
            std::string other = entry.path().filename().string();
            if (other == prefix || (other.starts_with(prefix + "-") && !other.starts_with(name))) std::filesystem::remove(entry.path(), ec);
        }
        std::u8string file = (dir / name).u8string();
        surface.pipelineCache.assign(file.begin(), file.end());
    }
    app.gpu = gpu::Device::create(backend, surface, &error);
    if (app.gpu) {
        app.logf("kompoz: renderer: Kompoz on %s (%s)", gpu::backendName(backend), app.gpu->deviceName().c_str());
        if (app.profile) {
            app.gpuTimed = app.gpu->setGpuTiming(true);
            if (!app.gpuTimed) app.logf("kompoz: profile: this GPU's time can't be measured here");
        }
        return true;
    }
    app.logf("kompoz: no %s device: %s", gpu::backendName(backend), error.c_str());
    app.platform.closeWindow();
    app.windowOpen = false;
    return false;
}

} // namespace

AppCore::AppCore(Platform& platform) : app_(std::make_unique<AppState>(platform)) {}
AppCore::~AppCore() = default;
ComposeHost& AppCore::host() { return *app_->host; }

bool AppCore::start(int argc, char** argv) {
    App& app = *app_;
    Platform& platform = app.platform;
    static App* logged = nullptr;  // (the GPU layer's messages into the platform's log)
    logged = &app;
    gpu::setLogger([](const char* message) { logged->platform.log(message); });
    app.config = kompoz::createApplication(argc, argv);
    if (auto preview = platform.option("KOMPOZ_PREVIEW")) kompoz::detail::showPreview(app.config, preview->c_str());  // a preview instead of the app
    startImageDiskCache(app.config.title, app.config.imageDiskCacheBytes);
    // On Android, where an app has no environment, each a file in its files (Platform::option):
    //   adb shell run-as <package> touch files/kompoz-profile
    // or, where run-as is refused (SELinux), in its external storage, which the app creates:
    //   adb shell touch /sdcard/Android/data/<package>/files/kompoz-profile
    app.trace = app.flag("KOMPOZ_TRACE");
    app.profile = app.flag("KOMPOZ_PROFILE");
    app.damageCheck = app.flag("KOMPOZ_DAMAGE_CHECK");
    app.keepCheck = app.flag("KOMPOZ_KEEP_CHECK");
    app.damageShow = app.flag("KOMPOZ_DAMAGE_SHOW");
    app.fullRedraw = app.flag("KOMPOZ_FULL_REDRAW");
    bool scripted = app.flag("KOMPOZ_SCRIPT");  // (an error is logged, not shown in a box to close)
    // KOMPOZ_GPU=vulkan, gles, metal, webgpu or auto: Kompoz's GPU renderer on Vulkan, OpenGL ES 3,
    // Metal, WebGPU (on the web: the page's canvas), or the platform's best (gpuOrder); its first
    // word (on Android: adb shell run-as <package> sh -c 'echo gles > files/kompoz-gpu').
    std::string gpuChoice = platform.option("KOMPOZ_GPU").value_or("");
    gpuChoice = gpuChoice.substr(0, gpuChoice.find_first_of(" \t\r\n"));
    // The backends asked for, each tried on a window of its own: the first that starts draws. Asked
    // for by name, one that doesn't start is the app's failure; "auto" goes on to the next. None at
    // all: the app can't draw (a computer without a GPU has a software one: lavapipe, SwiftShader,
    // llvmpipe, WARP).
    std::vector<gpu::Backend> order = gpuOrder(app, gpuChoice);
    for (gpu::Backend backend : order) {
        if (startGpu(app, backend)) break;
        if (order.size() == 1) break;  // (asked for by name)
    }
    if (!app.gpu) {
        app.logf("kompoz: no GPU renderer started (KOMPOZ_GPU=%s)", gpuChoice.empty() ? "auto" : gpuChoice.c_str());
        if (!scripted)  // (driven by a script, as on CI: nobody to close a box, only the log)
            platform.showError("No graphics device could be started (Vulkan, OpenGL ES, Metal or WebGPU).");
        return false;
    }

    app.fonts = std::make_unique<FontSystem>();
    if (!app.fonts->load(kompoz::FontWeight::Normal, app.config.fontPath)) {
        app.logf("kompoz: no usable font found (KOMPOZ_FONT)");
        if (!scripted) platform.showError("No usable font found. Set KOMPOZ_FONT.");
        return false;
    }
    app.fonts->load(kompoz::FontWeight::Bold, app.config.boldFontPath);  // optional
    app.fonts->load(kompoz::FontWeight::Medium, app.config.mediumFontPath);  // optional
    app.gpuCanvas = std::make_unique<gpu::Renderer>(*app.gpu, *app.fonts);
    // The pipelines it made in earlier runs, made again ahead of the frames that need them.
    if (std::filesystem::path dir = platform::appCacheDirectory(app.config.title); !dir.empty()) {
        std::u8string file = (dir / (std::string("pipelines-") + gpu::backendName(app.gpu->backend()) + ".list")).u8string();
        app.gpuCanvas->warmPipelines(std::string(file.begin(), file.end()));
    }
    app.display = std::make_unique<kompoz::DisplayList>(*app.fonts);
    app.display->allowMoves(true);  // (the kept frame moves its pixels)
    app.display->keepNodes(true);  // (nodes that didn't change aren't drawn again)
    app.bitmaps = std::make_unique<AppBitmaps>(app);
    kompoz::detail::setBitmapRenderer(app.bitmaps.get());
    app.host = std::make_unique<kompoz::ComposeHost>(*app.fonts, app.config.content);

    App* raw = &app;
    ComposeHost::Callbacks callbacks{
        .loadImage = [](std::string_view path) { return loadImageFile(path); },
        .loadImageAsync = [](const std::string& model, kompoz::ImageRequestSize size, std::function<void(kompoz::ImageBitmap)> done) {
            loadImageAsync(model, size, std::move(done));
        },
        .renderImage = [raw](kompoz::Size size, const std::function<void(kompoz::GraphicsContext&)>& draw) { return renderImage(*raw, size, draw); },
        .readAsset = [](std::string_view path) { return platform::readFile(std::string(path)); },
    };
    platform.attach(*app.host, callbacks);  // (its own: waking, text input, the clipboard, cursors...)
    app.host->setCallbacks(std::move(callbacks));
    // Deep links: an address given on the command line (the platform's own: a web address's
    // #fragment, an Android VIEW intent).
    for (int i = 1; i < argc; ++i)
        if (std::string_view(argv[i]).find("://") != std::string_view::npos) app.host->openDeepLink(argv[i]);
    if (auto path = platform.option("KOMPOZ_STATE_FILE"))
        if (std::vector<uint8_t> state = platform::readFile(*path); !state.empty()) app.host->restoreState(state);
    platform.started(*app.host);

    std::stringstream in(platform.option("KOMPOZ_SCRIPT").value_or(""));
    for (std::string command; std::getline(in, command, ';');)
        if (command.find_first_not_of(" \t\r\n") != std::string::npos) app.script.commands.push_back(command);
    updateMetrics(app);
    return true;
}

void AppCore::handle(const Event& event) {
    App& app = *app_;
    uint64_t start = app.profile ? app.now() : 0;
    handleEvent(app, event);
    app.platform.handled(*app.host, event);
    if (app.profile) app.prof.eventsNs += app.now() - start;
}

// A frame for the display's next beat, never a second one for the beat of the last: one started
// sooner (the last shown without waiting, the swap chain having room; or one that changed nothing
// on the screen) would get the last one's time, so a scroll or an animation would stand still in
// it and jump two beats in the next. (A frame not shown waits so too: an animation out of view
// doesn't spin as fast as it can.) A script in virtual time has its frames at once.
int64_t AppCore::frameDue() const {
    const App& app = *app_;
    if (app.lastFrameTime < 0 || (app.script.running() && !app.script.realtime)) return -1;
    return app.lastFrameTime - app.clockOffset + int64_t(app.framePeriodNs) * 3 / 5;  // (on the platform's clock)
}

void AppCore::idle(int64_t budget) {
    if (app_->host->hasIdleWork()) app_->host->idle(budget);  // (a lazy list's next item)
}

Next AppCore::frame(int64_t displayTime) {
    App& app = *app_;
    uint64_t frameStart = app.now();
    bool scripted = app.script.running();
    std::string shot = app.script.enabled() ? stepScript(app) : std::string{};
    int64_t now = scripted && !app.script.realtime ? (app.script.clock += 16'666'667)
                  : displayTime >= 0             ? (app.lastFrameTime = displayTime + app.clockOffset)
                                                 : (app.lastFrameTime = kompoz::detail::frameTimeOnBeat(app.lastFrameTime, app.clock(), int64_t(app.framePeriodNs)));
    if (scripted) app.script.clock = std::max(app.script.clock, now);
    if (scripted && !app.script.running())  // its last frame: real time from here, never back
        app.clockOffset = std::max<int64_t>(0, now - app.platform.now());

    if (app.metricsChanged) {
        app.metricsChanged = false;
        updateMetrics(app);
    }
    uint64_t t0 = app.profile ? app.now() : 0;
    int glyphsBefore = app.profile ? app.fonts->rasterized() : 0;
    bool redraw = app.host->frame(now);
    uint64_t t1 = app.profile ? app.now() : 0, t2 = t1, t3 = t1;
    app.platform.composed(*app.host, redraw, now);
    if (const auto& stats = app.host->stats(); app.trace && stats.recompositions != app.traced.recompositions) {
        app.logf("kompoz: recomposition %llu: %llu scopes ran, %llu skipped", (unsigned long long)stats.recompositions,
                 (unsigned long long)(stats.scopesRun - app.traced.scopesRun), (unsigned long long)(stats.scopesSkipped - app.traced.scopesSkipped));
        app.traced = stats;
    }
    if (app.resizeFrames > 0) {
        --app.resizeFrames;
        if (app.display) app.display->invalidate();
        app.showFrame = true;
    }
    Drawn drawn;
    uint64_t waited = 0;  // in beginFrame: for the GPU to be done with a frame before
    if (redraw || !shot.empty() || app.showFrame) {
        uint64_t before = app.now();
        app.gpu->beginFrame();
        waited = app.now() - before;
        app.gpuInFrame = true;
        app.prof.acquireNs = 0;  // (a frame with nothing to show asks for no image)
        drawn = drawFrame(app, shot);
        app.platform.frameWork(int64_t(app.now() - frameStart - waited - app.prof.acquireNs));  // (its waits for the GPU and the window's image left out)
        t2 = app.profile ? app.now() : 0;
        app.gpu->endFrame(drawn.shown);
        app.gpuInFrame = false;
        app.gpuCanvas->endFrame();
        if (drawn.shown && !app.mediaProbed) {
            app.mediaProbed = true;
            refreshMedia(app);
        }
        app.fonts->endFrame();
        t3 = app.profile ? app.now() : 0;
    }
    if (app.profile && redraw) profileFrame(app, t0, t1, waited, t2, t3, drawn.shown, app.fonts->rasterized() - glyphsBefore, drawn.area);
    if (app.gpuTimed) profileGpu(app);
    // Waiting for the next event when there is nothing to animate or recompose, or until the next
    // coroutine delay is due.
    Next next;
    next.busy = app.host->hasPendingWork() || app.script.running() || app.resizeFrames > 0 || app.platform.wantsFrames();
    // (Frames meant to follow each other: what the app shows asks for the next. Not a script
    // waiting its frames out, a semantics publish, a timer or work on another thread: a pause in
    // which nothing changes on the screen missed no refresh.)
    bool due = app.host->wantsFrame();
    if (due && !app.prof.busy) app.prof.dueSince = app.now();  // (a pause before it missed nothing)
    app.prof.busy = due;
    if (int64_t deadline = app.host->nextTimerDeadline(); deadline >= 0) next.wakeAt = deadline - app.clockOffset + 1'000'000;
    next.quit = app.quit;
    return next;
}

void AppCore::surfaceChanged() {
    if (app_->gpu) app_->gpu->surfaceChanged();
}

void AppCore::quit() {
    App& app = *app_;
    // KOMPOZ_STATE_FILE: the saved state written out at exit and read at start, as Android does
    // when it ends and brings back an app (to try rememberSaveable on the desktop).
    if (auto path = app.platform.option("KOMPOZ_STATE_FILE"); path && app.host) {
        std::vector<uint8_t> state = app.host->saveState();
        std::ofstream(std::filesystem::path(*path), std::ios::binary).write(reinterpret_cast<const char*>(state.data()), std::streamsize(state.size()));
    }
}

} // namespace kompoz::platform
