// Kompoz platform backend on SDL3: window, input, IME/soft keyboard, and drawing on the GPU renderer.
//
// SDL3 runs on Windows, macOS, Linux, Android, iOS and the web, and the main-callback model
// used here (SDL_AppInit / SDL_AppIterate / SDL_AppEvent) is the one that works on all of
// them, so this single file is what makes a Kompoz app multiplatform.
// Text is rasterized with FreeType into a glyph atlas (font_system.hpp).

#define SDL_MAIN_USE_CALLBACKS 1
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <kompoz/app.hpp>
#include <kompoz/display_list.hpp>
#include <kompoz/host.hpp>
#include <kompoz/preview.hpp>

#include "../gpu/renderer.hpp"
#include "font_system.hpp"
#include "image_loader.hpp"
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
#include "android_performance.hpp"
#endif

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <filesystem>
#include <filesystem>
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
    bool realtime = false;  // `realtime`: from there on the clock is real and frames are paced as without a script

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
    std::unique_ptr<FontSystem> fonts;
    // Kompoz's GPU renderer (gpu/renderer.hpp) on the device KOMPOZ_GPU chose, with the kept frame
    // in a texture of its own.
    std::unique_ptr<gpu::Device> gpu;
    SDL_GLContext glContext = nullptr;  // KOMPOZ_GPU=gles: the OpenGL ES context it draws with
    SDL_MetalView metalView = nullptr;  // KOMPOZ_GPU=metal: the view whose layer it draws into
    bool vulkanLoaded = false;          // headless Vulkan: the library loaded here (no Vulkan window loads it)
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
    bool damageCheck = SDL_getenv("KOMPOZ_DAMAGE_CHECK") != nullptr;  // each frame compared with all of it drawn
    bool keepCheck = SDL_getenv("KOMPOZ_KEEP_CHECK") != nullptr;      // each frame's recording compared with one drawing every node
    bool damageShow = SDL_getenv("KOMPOZ_DAMAGE_SHOW") != nullptr;    // outlines what was drawn again
    bool fullRedraw = SDL_getenv("KOMPOZ_FULL_REDRAW") != nullptr;    // all of every frame drawn, as before
    // A drag from another app coming in: what it drops, gathered until it is complete.
    bool dropping = false;
    std::vector<std::string> dropFiles;
    std::string dropText;
    kompoz::Offset dropAt;
    int damageFrames = 0, damageWrong = 0, keepFrames = 0, keepWrong = 0;
    SDL_TimerID wakeTimer = 0;  // wakes the idle loop when a coroutine delay is due
    int64_t wakeDeadline = -1;
    kompoz::detail::RecompositionStats traced;

    ~App() {
        kompoz::sdl::stopImageLoader();  // its threads end before the rest goes
        if (wakeTimer) SDL_RemoveTimer(wakeTimer);
        if (damageCheck) SDL_Log("kompoz: damage check: %d of %d frames differed", damageWrong, damageFrames);
        if (keepCheck) SDL_Log("kompoz: keep check: %d of %d frames recorded otherwise", keepWrong, keepFrames);
#ifdef SDL_PLATFORM_ANDROID
        android_ime::setHandlers(nullptr, nullptr, nullptr, nullptr);
        kompoz::android_performance::stop();
        android_ime::setMediaHandler(nullptr);
        android_ime::setDeepLinkHandler(nullptr);
        android_ime::setBackHandler(nullptr);
#endif
#ifdef KOMPOZ_A11Y_BRIDGE
        a11y.reset();  // (UI Automation's hooked into the window)
#endif
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
        if (glContext) SDL_GL_DestroyContext(glContext);
        if (metalView) SDL_Metal_DestroyView(metalView);
#ifdef KOMPOZ_GPU_VULKAN
        if (vulkanLoaded) SDL_Vulkan_UnloadLibrary();
#endif
        fonts.reset();
        for (auto* cursor : cursors)
            if (cursor) SDL_DestroyCursor(cursor);
        if (window) SDL_DestroyWindow(window);
    }

    Offset toDp(float x, float y) const { return {x * pointToDp, y * pointToDp}; }
    // Frame and input times are on one clock: a script's virtual one while it runs, SDL's after.
    int64_t clock() const { return int64_t(SDL_GetTicksNS()) + clockOffset; }
    int64_t eventTime(Uint64 timestamp) const { return script.running() && !script.realtime ? script.clock : int64_t(timestamp) + clockOffset; }
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
    // Elevation shadows' light: above the window's top middle, as Android's (its theme's values).
    kompoz::ShadowLight light;
    light.center = {float(pw) / scale / 2, 0};
    light.density = scale;
    for (kompoz::GraphicsContext* c : {static_cast<kompoz::GraphicsContext*>(app.gpuCanvas.get()), static_cast<kompoz::GraphicsContext*>(app.display.get())})
        if (c) c->light = light;
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(app.window));
    app.framePeriodNs = uint64_t(1e9 / (mode && mode->refresh_rate > 0 ? mode->refresh_rate : 60.f));
#ifdef SDL_PLATFORM_ANDROID
    kompoz::android_performance::start(int64_t(app.framePeriodNs));  // (the first time: a frame's work told to ADPF)
    kompoz::android_performance::setTarget(int64_t(app.framePeriodNs));
#endif

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

// Runs script commands until one asks to wait. Returns a screenshot path for this frame, if any.
// KOMPOZ_PROFILE: logs a frame that took long (or came long after the one before) and a
// summary every second. "work" is input + compose/layout + drawing; present waits for vsync.

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
        SDL_Log("kompoz: keep check: %s", why.c_str());
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
        SDL_Log("kompoz: damage check: %d pixels differ (by up to %d) in %d,%d %dx%d px; drawn again: %.0f,%.0f %.0fx%.0f px",
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
    SDL_GetWindowSizeInPixels(app.window, &pw, &ph);
    pw = std::max(pw, 1), ph = std::max(ph, 1);
    if (!app.gpuFrame || app.gpuFrame->width() != pw || app.gpuFrame->height() != ph) {
        app.gpuFrame = device.createTexture(pw, ph, gpu::Format::RGBA8, gpu::TextureUsage::Sampled | gpu::TextureUsage::RenderTarget);
        app.display->invalidate();
        app.showFrame = true;
    }
    r.layerCount = r.keptLayers = r.pathCount = r.keptPaths = r.texturesMade = 0;
    float scale = r.scale;
    Size screen{float(pw) / scale, float(ph) / scale};
    uint64_t recording = SDL_GetTicksNS();
    app.display->begin(screen, 1 / scale);
    app.host->draw(*app.display);
    if (app.fullRedraw) app.display->invalidate();
    std::optional<Rect> damage = app.display->end();
    app.prof.recordNs = SDL_GetTicksNS() - recording;
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
    uint64_t acquiring = SDL_GetTicksNS();
    gpu::Texture* swapchain = device.swapchainTexture();
    app.prof.acquireNs = SDL_GetTicksNS() - acquiring;
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
        SDL_Log("kompoz: frame %.1f ms after the last: input %.1f, compose+layout %.1f, wait %.1f, draw %.1f, present %.1f ms; %d nodes "
                "drawn (%d kept), %d layers (%d kept), %d paths (%d kept), %d textures made, %d glyphs, %.0f%% drawn again",
                interval, events, compose, wait, draw, present, app.display->drawnNodes(), app.display->keptNodes(),
                app.gpuCanvas->layerCount, app.gpuCanvas->keptLayers, app.gpuCanvas->pathCount, app.gpuCanvas->keptPaths, app.gpuCanvas->texturesMade, glyphs,
                (area < 0 ? 1 : area) * 100);
    }
    if (start - p.summaryStart >= 1'000'000'000) {
        char gpu[96] = "";
        if (p.gpuFrames) std::snprintf(gpu, sizeof gpu, ", GPU avg %.1f max %.1f ms", p.gpuTotal / p.gpuFrames, p.gpuMax);
        SDL_Log("kompoz: %d frames in %.0f ms (%d shown, %d refreshes missed; %d changed nothing on the screen; %.0f%% of it drawn again on "
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
        SDL_Log("kompoz: GPU frame %.1f ms%s%s", t.ms, passes.empty() ? "" : ": ", passes.c_str());
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
        } else if (command == "realtime") {  // the rest in real time, frames paced to the display: as a person would see it
            s.realtime = true;
            app.clockOffset = std::max<int64_t>(0, s.clock - int64_t(SDL_GetTicksNS()));  // (never back)
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
    bool metrics = (e.type >= SDL_EVENT_WINDOW_FIRST && e.type <= SDL_EVENT_WINDOW_LAST) || e.type == SDL_EVENT_SYSTEM_THEME_CHANGED ||
                   e.type == SDL_EVENT_LOCALE_CHANGED;
    if (app.metricsChanged && !metrics) {  // (input meets the window as it is now)
        app.metricsChanged = false;
        updateMetrics(app);
    }
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
        if (app.gpuCanvas) app.gpuCanvas->trimMemory();  // (as Android's renderer when the UI is hidden)
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
        app.metricsChanged = true;
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
    case SDL_EVENT_LOW_MEMORY:
        if (app.gpuCanvas) app.gpuCanvas->trimMemory();
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

// KOMPOZ_GPU: the GPU renderer's backends, in the order they are tried. A name is that one; "auto"
// (or nothing) the platform's own first, then OpenGL ES: Metal on Apple's systems; on the web
// WebGPU, then WebGL 2; elsewhere Vulkan (on Android from Android 10, before which its drivers
// were often wrong), then OpenGL ES 3.
std::vector<gpu::Backend> gpuOrder(const std::string& choice) {
    using gpu::Backend;
    if (choice == "vulkan") return {Backend::Vulkan};
    if (choice == "gles") return {Backend::GLES};
    if (choice == "metal") return {Backend::Metal};
    if (choice == "webgpu") return {Backend::WebGPU};
    if (choice != "auto" && !choice.empty()) SDL_Log("kompoz: KOMPOZ_GPU=%s: not vulkan, gles, metal, webgpu or auto: auto", choice.c_str());
#if defined(SDL_PLATFORM_APPLE)
    return {Backend::Metal};
#elif defined(SDL_PLATFORM_EMSCRIPTEN)
    return {Backend::WebGPU, Backend::GLES};
#elif defined(SDL_PLATFORM_ANDROID)
    if (SDL_GetAndroidSDKVersion() < 29) return {Backend::GLES};
    return {Backend::Vulkan, Backend::GLES};
#else
    return {Backend::Vulkan, Backend::GLES};
#endif
}

// Whether there is no screen to show frames on (SDL's offscreen driver: tests, CI, screenshots):
// the GPU renderer then draws only into its kept frame, a texture.
bool headless() {
    const char* driver = SDL_GetCurrentVideoDriver();
    return driver && std::string_view(driver) == "offscreen";
}

// The window, made for `backend`'s graphics API; nullptr if SDL couldn't. Headless, Vulkan and
// Metal draw without the window (it has no graphics API), OpenGL ES still in its context.
SDL_Window* createWindow(App& app, SDL_WindowFlags flags, gpu::Backend backend) {
    if (backend == gpu::Backend::Vulkan && !headless()) flags |= SDL_WINDOW_VULKAN;
    if (backend == gpu::Backend::Metal && !headless()) flags |= SDL_WINDOW_METAL;
    if (backend == gpu::Backend::GLES) {  // an OpenGL ES 3.0 context (a debug one with KOMPOZ_GPU_VALIDATION=1)
        flags |= SDL_WINDOW_OPENGL;
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_ES);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 3);
        SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 0);
        SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 0);
        SDL_GL_SetAttribute(SDL_GL_STENCIL_SIZE, 0);
        if (const char* v = SDL_getenv("KOMPOZ_GPU_VALIDATION"); v && std::string_view(v) == "1")
            SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
#ifdef SDL_PLATFORM_WINDOWS
        // ANGLE's where the app has it beside it (libEGL.dll, libGLESv2.dll): OpenGL ES over
        // Direct3D 11, the same on every Windows GPU. Else the driver's own OpenGL ES.
        if (const char* base = SDL_GetBasePath(); base && SDL_GetPathInfo((std::string(base) + "libEGL.dll").c_str(), nullptr))
            SDL_SetHint(SDL_HINT_OPENGL_ES_DRIVER, "1");
#endif
    }
    SDL_PropertiesID props = SDL_CreateProperties();
    SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, app.config.title.c_str());
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, app.config.width);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, app.config.height);
    SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, static_cast<Sint64>(flags));
    // Headless, a window without a graphics API is just that: on macOS SDL would make it an OpenGL
    // window, which offscreen means EGL, which macOS hasn't.
    if (headless() && backend != gpu::Backend::GLES) SDL_SetBooleanProperty(props, SDL_PROP_WINDOW_CREATE_EXTERNAL_GRAPHICS_CONTEXT_BOOLEAN, true);
    SDL_Window* window = SDL_CreateWindowWithProperties(props);
    SDL_DestroyProperties(props);
    return window;
}

// Kompoz's GPU renderer's device on `backend` for the app's window (and what it needs from the
// window: an OpenGL ES context, a Metal view; headless, Vulkan and Metal nothing: they draw only
// into textures); false (and nothing of it left) if it can't be made.
bool startGpu(App& app, gpu::Backend backend) {
    gpu::setLogger([](const char* message) { SDL_Log("%s", message); });
    SDL_Window* window = app.window;
    gpu::Surface surface;
    surface.drawableSize = [window](int* w, int* h) { SDL_GetWindowSizeInPixels(window, w, h); };
    // The compiled pipelines beside the downloaded images, a file for each backend and build of the
    // shaders; another's goes (a driver keeps what it was given: an older build's pipelines would
    // stay in it, read at every start, the file growing with each update).
    if (std::filesystem::path dir = appCacheDirectory(app.config.title); !dir.empty()) {
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
    std::string error;
    switch (backend) {
    case gpu::Backend::GLES:
        app.glContext = SDL_GL_CreateContext(window);
        if (!app.glContext) {
            error = std::string("no OpenGL ES 3 context: ") + SDL_GetError();
            break;
        }
        SDL_GL_SetSwapInterval(1);
        surface.glGetProcAddress = [](const char* name) { return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name)); };
        surface.glSwap = [window] { SDL_GL_SwapWindow(window); };
        app.gpu = gpu::Device::create(backend, surface, &error);
        break;
    case gpu::Backend::WebGPU: {
        const char* canvas = SDL_GetHint(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR);
        surface.webgpuCanvas = canvas ? canvas : "#canvas";
        app.gpu = gpu::Device::create(backend, surface, &error);
        break;
    }
    case gpu::Backend::Metal:
        if (headless()) {
            app.gpu = gpu::Device::create(backend, surface, &error);
            break;
        }
        app.metalView = SDL_Metal_CreateView(window);
        if (!app.metalView) {
            error = std::string("no Metal view: ") + SDL_GetError();
            break;
        }
        surface.metalLayer = SDL_Metal_GetLayer(app.metalView);
        app.gpu = gpu::Device::create(backend, surface, &error);
        break;
    case gpu::Backend::Vulkan: {
#ifdef KOMPOZ_GPU_VULKAN
        if (headless()) {  // (no surface to present to, so no extensions for one)
            if (!app.vulkanLoaded && !SDL_Vulkan_LoadLibrary(nullptr)) {
                error = std::string("no Vulkan library: ") + SDL_GetError();
                break;
            }
            app.vulkanLoaded = true;
        } else {
            Uint32 count = 0;
            if (const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count)) surface.vulkanInstanceExtensions.assign(names, names + count);
        }
        surface.vulkanGetInstanceProcAddr = reinterpret_cast<void* (*)(void*, const char*)>(SDL_Vulkan_GetVkGetInstanceProcAddr());
        if (!headless()) surface.createVulkanSurface = [window](void* instance, uint64_t* out) {
            VkSurfaceKHR made{};
            if (!SDL_Vulkan_CreateSurface(window, static_cast<VkInstance>(instance), nullptr, &made)) return false;
            std::memcpy(out, &made, sizeof made);
            return true;
        };
#ifdef SDL_PLATFORM_ANDROID
        surface.vulkanNativeWindow = [window] { return SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_ANDROID_WINDOW_POINTER, nullptr); };
#endif
#endif
        app.gpu = gpu::Device::create(backend, surface, &error);
        break;
    }
    }
    if (app.gpu) {
        SDL_Log("kompoz: renderer: Kompoz on %s (%s)", gpu::backendName(backend), app.gpu->deviceName().c_str());
        if (app.profile) {
            app.gpuTimed = app.gpu->setGpuTiming(true);
            if (!app.gpuTimed) SDL_Log("kompoz: profile: this GPU's time can't be measured here");
        }
        return true;
    }
    SDL_Log("kompoz: no %s device: %s", gpu::backendName(backend), error.c_str());
    if (app.glContext) SDL_GL_DestroyContext(std::exchange(app.glContext, nullptr));
    if (app.metalView) SDL_Metal_DestroyView(std::exchange(app.metalView, nullptr));
    return false;
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
    // and kompoz-damage-check, KOMPOZ_DAMAGE_CHECK; kompoz-keep-check, KOMPOZ_KEEP_CHECK;
    // kompoz-full-redraw, KOMPOZ_FULL_REDRAW.
    for (const char* dir : {SDL_GetAndroidInternalStoragePath(), SDL_GetAndroidExternalStoragePath()}) {
        if (!dir) continue;
        app->profile = app->profile || SDL_GetPathInfo((std::string(dir) + "/kompoz-profile").c_str(), nullptr);
        app->damageCheck = app->damageCheck || SDL_GetPathInfo((std::string(dir) + "/kompoz-damage-check").c_str(), nullptr);
        app->keepCheck = app->keepCheck || SDL_GetPathInfo((std::string(dir) + "/kompoz-keep-check").c_str(), nullptr);
        app->fullRedraw = app->fullRedraw || SDL_GetPathInfo((std::string(dir) + "/kompoz-full-redraw").c_str(), nullptr);
    }
#endif
    SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    bool scripted = SDL_getenv("KOMPOZ_SCRIPT") != nullptr;  // (an error is logged, not shown in a box to close)
    // KOMPOZ_GPU=vulkan, gles, metal, webgpu or auto: Kompoz's GPU renderer on Vulkan, OpenGL ES 3,
    // Metal, WebGPU (on the web: the page's canvas), or the platform's best (gpuOrder). On Android,
    // where an app has no environment, the first word of a file named kompoz-gpu in its files (as
    // for kompoz-profile above):
    //   adb shell run-as <package> sh -c 'echo gles > files/kompoz-gpu'
    std::string gpuChoice = SDL_getenv("KOMPOZ_GPU") ? SDL_getenv("KOMPOZ_GPU") : "";
#ifdef SDL_PLATFORM_ANDROID
    for (const char* dir : {SDL_GetAndroidInternalStoragePath(), SDL_GetAndroidExternalStoragePath()}) {
        if (!dir || !gpuChoice.empty()) continue;
        if (void* data = SDL_LoadFile((std::string(dir) + "/kompoz-gpu").c_str(), nullptr)) {
            std::string text = static_cast<const char*>(data);
            SDL_free(data);
            gpuChoice = text.substr(0, text.find_first_of(" \t\r\n"));
        }
    }
#endif
#ifdef SDL_PLATFORM_EMSCRIPTEN
    flags |= SDL_WINDOW_FILL_DOCUMENT;  // the whole page, resized with the browser window
    kompoz_web_watch_wheel();
#endif
    // The backends asked for, each tried on a window of its own: the first that starts draws. Asked
    // for by name, one that doesn't start is the app's failure; "auto" goes on to the next. None at
    // all: the app can't draw (a computer without a GPU has a software one: lavapipe, SwiftShader,
    // llvmpipe, WARP).
    std::vector<kompoz::gpu::Backend> order = kompoz::sdl::gpuOrder(gpuChoice);
    for (kompoz::gpu::Backend backend : order) {
        app->window = kompoz::sdl::createWindow(*app, flags, backend);
        if (!app->window) SDL_Log("kompoz: no window for %s: %s", kompoz::gpu::backendName(backend), SDL_GetError());
        if (app->window && kompoz::sdl::startGpu(*app, backend)) break;
        if (app->window) SDL_DestroyWindow(std::exchange(app->window, nullptr));
        if (order.size() == 1) break;  // (asked for by name)
    }
    if (!app->gpu) {
        SDL_Log("kompoz: no GPU renderer started (KOMPOZ_GPU=%s)", gpuChoice.empty() ? "auto" : gpuChoice.c_str());
        if (!scripted)  // (driven by a script, as on CI: nobody to close a box, only the log)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kompoz", "No graphics device could be started (Vulkan, OpenGL ES, Metal or WebGPU).", nullptr);
        return SDL_APP_FAILURE;
    }
    // Window sizes are in points; make the initial size match the requested dp.
    float pointsPerDp = SDL_GetWindowDisplayScale(app->window) / SDL_GetWindowPixelDensity(app->window);
    if (std::isfinite(pointsPerDp) && std::abs(pointsPerDp - 1) > 0.01f)
        SDL_SetWindowSize(app->window, int(std::lround(app->config.width * pointsPerDp)),
                          int(std::lround(app->config.height * pointsPerDp)));

    if (const char* driver = SDL_GetCurrentVideoDriver()) app->pinchSinceStart = std::string_view(driver) == "wayland";

    app->fonts = std::make_unique<kompoz::sdl::FontSystem>();
    if (!app->fonts->load(kompoz::FontWeight::Normal, app->config.fontPath)) {
        SDL_Log("kompoz: no usable font found (KOMPOZ_FONT)");
        if (!scripted) SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kompoz", "No usable font found. Set KOMPOZ_FONT.", app->window);
        return SDL_APP_FAILURE;
    }
    app->fonts->load(kompoz::FontWeight::Bold, app->config.boldFontPath);  // optional
    app->fonts->load(kompoz::FontWeight::Medium, app->config.mediumFontPath);  // optional
    app->gpuCanvas = std::make_unique<kompoz::gpu::Renderer>(*app->gpu, *app->fonts);
    // The pipelines it made in earlier runs, made again ahead of the frames that need them.
    if (std::filesystem::path dir = kompoz::sdl::appCacheDirectory(app->config.title); !dir.empty()) {
        std::u8string file = (dir / (std::string("pipelines-") + kompoz::gpu::backendName(app->gpu->backend()) + ".list")).u8string();
        app->gpuCanvas->warmPipelines(std::string(file.begin(), file.end()));
    }
    app->display = std::make_unique<kompoz::DisplayList>(*app->fonts);
    app->display->allowMoves(true);  // (the kept frame moves its pixels)
    app->display->keepNodes(true);  // (nodes that didn't change aren't drawn again)
    app->bitmaps = std::make_unique<kompoz::sdl::AppBitmaps>(*app);
    kompoz::detail::setBitmapRenderer(app->bitmaps.get());
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

    std::string script = SDL_getenv("KOMPOZ_SCRIPT") ? SDL_getenv("KOMPOZ_SCRIPT") : "";
#ifdef SDL_PLATFORM_ANDROID  // (no environment: a file named kompoz-script in the app's files, as kompoz-gpu)
    for (const char* dir : {SDL_GetAndroidInternalStoragePath(), SDL_GetAndroidExternalStoragePath()}) {
        if (!dir || !script.empty()) continue;
        if (void* data = SDL_LoadFile((std::string(dir) + "/kompoz-script").c_str(), nullptr)) {
            script = static_cast<const char*>(data);
            SDL_free(data);
        }
    }
#endif
    std::stringstream in(script);
    for (std::string command; std::getline(in, command, ';');)
        if (command.find_first_not_of(" \t\r\n") != std::string::npos) app->script.commands.push_back(command);
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
#ifndef SDL_PLATFORM_EMSCRIPTEN  // (the browser's frames come on its beat)
    // A frame for the display's next beat, never a second one for the beat of the last: one
    // started sooner (the last shown without waiting, the swap chain having room; or one that
    // changed nothing on the screen) would get the last one's time, so a scroll or an animation
    // would stand still in it and jump two beats in the next. (A frame not shown waits so too:
    // an animation out of view doesn't spin as fast as it can.)
    // The pause until then is the host's for work between frames (a lazy list's next item).
    if (app.lastFrameTime >= 0 && !(scripted && !app.script.realtime))
        if (int64_t due = app.lastFrameTime + int64_t(app.framePeriodNs) * 3 / 5, at = app.clock(); at < due) {
            if (app.host->hasIdleWork()) app.host->idle(due - at), at = app.clock();
            if (at < due) SDL_DelayNS(uint64_t(due - at));
            iterationStart = SDL_GetTicksNS();  // (its work starts now)
        }
#endif
    std::string shot = app.script.enabled() ? kompoz::sdl::stepScript(app) : std::string{};
    int64_t now = scripted && !app.script.realtime
                      ? (app.script.clock += 16'666'667)
                      : (app.lastFrameTime = kompoz::detail::frameTimeOnBeat(app.lastFrameTime, app.clock(), int64_t(app.framePeriodNs)));
    if (scripted) app.script.clock = std::max(app.script.clock, now);
    if (scripted && !app.script.running())  // its last frame: real time from here, never back
        app.clockOffset = std::max<int64_t>(0, now - int64_t(SDL_GetTicksNS()));

    if (app.metricsChanged) {
        app.metricsChanged = false;
        kompoz::sdl::updateMetrics(app);
    }
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
    uint64_t waited = 0;  // in beginFrame: for the GPU to be done with a frame before
    if (redraw || !shot.empty() || app.showFrame) {
        uint64_t before = SDL_GetTicksNS();
        app.gpu->beginFrame();
        waited = SDL_GetTicksNS() - before;
        app.gpuInFrame = true;
        app.prof.acquireNs = 0;  // (a frame with nothing to show asks for no image)
        drawn = kompoz::sdl::drawFrame(app, shot);
#ifdef SDL_PLATFORM_ANDROID
        // The frame's work so far (its waits for the GPU and the window's image left out).
        kompoz::android_performance::report(int64_t(SDL_GetTicksNS() - iterationStart - waited - app.prof.acquireNs));
#endif
        t2 = app.profile ? SDL_GetTicksNS() : 0;
        app.gpu->endFrame(drawn.shown);
        app.gpuInFrame = false;
        app.gpuCanvas->endFrame();
        if (drawn.shown && !app.mediaProbed) kompoz::sdl::probeMedia(app);
        app.fonts->endFrame();
        t3 = app.profile ? SDL_GetTicksNS() : 0;
    }
    if (app.profile && redraw) kompoz::sdl::profileFrame(app, t0, t1, waited, t2, t3, drawn.shown, app.fonts->rasterized() - glyphsBefore, drawn.area);
    if (app.gpuTimed) kompoz::sdl::profileGpu(app);
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
#endif
    // (Frames meant to follow each other: what the app shows asks for the next. Not a script
    // waiting its frames out, a semantics publish, a timer or work on another thread: a pause in
    // which nothing changes on the screen missed no refresh.)
    bool due = app.host->wantsFrame();
    if (due && !app.prof.busy) app.prof.dueSince = SDL_GetTicksNS();  // (a pause before it missed nothing)
    app.prof.busy = due;
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
