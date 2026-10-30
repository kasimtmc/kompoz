// Kompoz's platform on SDL3 (app/platform.hpp): the window, input, the soft keyboard and the
// frame loop, on Windows, macOS, Linux and the web, for the app core (app/app_core.hpp).
// SDL's main callbacks (SDL_AppInit / SDL_AppIterate / SDL_AppEvent) drive it on all of them; on
// the desktops KOMPOZ_HEADLESS=1 runs the app on Kompoz's headless platform instead.
// (Each platform is getting a layer of its own in its place.)

#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <kompoz/app.hpp>
#include <kompoz/host.hpp>

#include "../app/app_core.hpp"
#include "../common/system.hpp"
#ifdef KOMPOZ_HEADLESS
#include "../headless/headless.hpp"
#endif
#ifdef KOMPOZ_GPU_VULKAN
#include <SDL3/SDL_vulkan.h>
#endif
#ifdef KOMPOZ_DESKTOP_ACCENT
#include "../common/desktop_accent.hpp"
#endif
#ifdef KOMPOZ_WALLPAPER_COLORS
#include "../common/wallpaper_colors.hpp"
#endif
#ifdef KOMPOZ_ATSPI
#include "../linux/atspi_bridge.hpp"
#endif
#ifdef KOMPOZ_UIA
#include "../windows/uia_bridge.hpp"
#endif
#if defined(KOMPOZ_ATSPI) || defined(KOMPOZ_UIA)
#define KOMPOZ_A11Y_BRIDGE 1  // the desktop's screen readers read the semantics tree
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

// The platform on SDL3 (app/platform.hpp), the app core (app/app_core.hpp) on it.
struct SdlPlatform final : platform::Platform {
    SDL_Window* window = nullptr;
    SDL_GLContext glContext = nullptr;  // KOMPOZ_GPU=gles: the OpenGL ES context it draws with
    SDL_MetalView metalView = nullptr;  // KOMPOZ_GPU=metal: the view whose layer it draws into
#ifdef KOMPOZ_DESKTOP_ACCENT
    std::unique_ptr<platform::DesktopAccent> accent;  // the desktop's accent color: dynamic color (Linux)
#endif
#ifdef KOMPOZ_WALLPAPER_COLORS
    std::unique_ptr<platform::WallpaperColors> wallpaper;  // the wallpaper's own colors (desktops)
#endif
#ifdef KOMPOZ_ATSPI
    std::unique_ptr<platform::AtspiBridge> a11y;  // screen readers (Linux: AT-SPI)
#elif defined(KOMPOZ_UIA)
    std::unique_ptr<platform::UiaBridge> a11y;  // screen readers (Windows: UI Automation)
#endif
    ComposeHost* host = nullptr;
    int64_t lastSemantics = 0;   // AT-SPI, UI Automation
    bool semanticsDirty = true;  // the screen changed since the tree was last published
    bool keepScreenOn = false;   // as SDL was last told
    bool hasMicrophone = false, hasCamera = false, probed = false;
    mutable float scale = 1, pointToDp = 1;  // as metrics() last said
    bool focused = true;             // (AT-SPI tells which window is active)
    Uint32 wakeEvent = 0;
    Uint32 accentEvent = Uint32(-1), wallpaperEvent = Uint32(-1), a11yEvent = Uint32(-1);  // what the modules on threads of their own tell with
    std::atomic<bool> wakePending{false};
    bool waiting = false;
    SDL_Cursor* cursors[9] = {};  // by kompoz::Cursor
    float pinchScale = 1;         // the touchpad pinch's scale so far (Wayland reports it since the start)
    bool pinchSinceStart = false;
    SDL_TimerID wakeTimer = 0;  // wakes the idle loop when a coroutine delay is due
    int64_t wakeDeadline = -1;

    SdlPlatform() {
        platform::setLogSink([](const char* message) { SDL_Log("%s", message); });
    }
    ~SdlPlatform() override {
        if (wakeTimer) SDL_RemoveTimer(wakeTimer);
#ifdef KOMPOZ_A11Y_BRIDGE
        a11y.reset();  // (UI Automation's hooked into the window)
#endif
        for (auto* cursor : cursors)
            if (cursor) SDL_DestroyCursor(cursor);
        closeWindow();
    }

    std::optional<std::string> option(std::string_view name) const override {
        std::string key(name);
        if (const char* v = SDL_getenv(key.c_str())) return std::string(v);
        return std::nullopt;
    }
    void log(const std::string& message) const override { SDL_Log("%s", message.c_str()); }
    int64_t now() const override { return int64_t(SDL_GetTicksNS()); }

    // The platform's own first, then OpenGL ES: Metal on Apple's systems; on the web WebGPU, then
    // WebGL 2; elsewhere Vulkan, then OpenGL ES 3.
    std::vector<gpu::Backend> backends() const override {
        using gpu::Backend;
#if defined(SDL_PLATFORM_APPLE)
        return {Backend::Metal};
#elif defined(SDL_PLATFORM_EMSCRIPTEN)
        return {Backend::WebGPU, Backend::GLES};
#else
        return {Backend::Vulkan, Backend::GLES};
#endif
    }

    // The window, made for `backend`'s graphics API, and what the device needs of it (an OpenGL ES
    // context, a Metal view, a Vulkan surface).
    bool openWindow(const Application& config, gpu::Backend backend, gpu::Surface& surface, std::string* error) override {
        SDL_SetAppMetadata(config.title.c_str(), "0.1", "dev.kompoz.app");
        SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
#ifdef SDL_PLATFORM_EMSCRIPTEN
        flags |= SDL_WINDOW_FILL_DOCUMENT;  // the whole page, resized with the browser window
#endif
        if (backend == gpu::Backend::Vulkan) flags |= SDL_WINDOW_VULKAN;
        if (backend == gpu::Backend::Metal) flags |= SDL_WINDOW_METAL;
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
        SDL_SetStringProperty(props, SDL_PROP_WINDOW_CREATE_TITLE_STRING, config.title.c_str());
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, config.width);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, config.height);
        SDL_SetNumberProperty(props, SDL_PROP_WINDOW_CREATE_FLAGS_NUMBER, static_cast<Sint64>(flags));
        window = SDL_CreateWindowWithProperties(props);
        SDL_DestroyProperties(props);
        if (!window) {
            *error = SDL_GetError();
            return false;
        }
        SDL_Window* w = window;
        surface.drawableSize = [w](int* width, int* height) { SDL_GetWindowSizeInPixels(w, width, height); };
        auto failed = [&](std::string why) {
            *error = std::move(why);
            closeWindow();
            return false;
        };
        switch (backend) {
        case gpu::Backend::GLES:
            glContext = SDL_GL_CreateContext(window);
            if (!glContext) return failed(std::string("no OpenGL ES 3 context: ") + SDL_GetError());
            SDL_GL_SetSwapInterval(1);
            surface.glGetProcAddress = [](const char* name) { return reinterpret_cast<void*>(SDL_GL_GetProcAddress(name)); };
            surface.glSwap = [w] { SDL_GL_SwapWindow(w); };
            break;
        case gpu::Backend::WebGPU: {
            const char* canvas = SDL_GetHint(SDL_HINT_EMSCRIPTEN_CANVAS_SELECTOR);
            surface.webgpuCanvas = canvas ? canvas : "#canvas";
            break;
        }
        case gpu::Backend::Metal:
            metalView = SDL_Metal_CreateView(window);
            if (!metalView) return failed(std::string("no Metal view: ") + SDL_GetError());
            surface.metalLayer = SDL_Metal_GetLayer(metalView);
            break;
        case gpu::Backend::Vulkan:
#ifdef KOMPOZ_GPU_VULKAN
            if (Uint32 count = 0; const char* const* names = SDL_Vulkan_GetInstanceExtensions(&count)) surface.vulkanInstanceExtensions.assign(names, names + count);
            surface.vulkanGetInstanceProcAddr = reinterpret_cast<void* (*)(void*, const char*)>(SDL_Vulkan_GetVkGetInstanceProcAddr());
            surface.createVulkanSurface = [w](void* instance, uint64_t* out) {
                VkSurfaceKHR made{};
                if (!SDL_Vulkan_CreateSurface(w, static_cast<VkInstance>(instance), nullptr, &made)) return false;
                std::memcpy(out, &made, sizeof made);
                return true;
            };
#endif
            break;
        }
        // Window sizes are in points; make the initial size match the requested dp.
        float pointsPerDp = SDL_GetWindowDisplayScale(window) / SDL_GetWindowPixelDensity(window);
        if (std::isfinite(pointsPerDp) && std::abs(pointsPerDp - 1) > 0.01f)
            SDL_SetWindowSize(window, int(std::lround(config.width * pointsPerDp)), int(std::lround(config.height * pointsPerDp)));
        return true;
    }

    void closeWindow() override {
        if (glContext) SDL_GL_DestroyContext(std::exchange(glContext, nullptr));
        if (metalView) SDL_Metal_DestroyView(std::exchange(metalView, nullptr));
        if (window) SDL_DestroyWindow(std::exchange(window, nullptr));
    }

    void showError(const std::string& message) override { SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Kompoz", message.c_str(), window); }

    void pixelSize(int* width, int* height) const override { SDL_GetWindowSizeInPixels(window, width, height); }

    platform::Metrics metrics() const override {
        platform::Metrics m;
        SDL_GetWindowSizeInPixels(window, &m.pixelWidth, &m.pixelHeight);
        SDL_GetWindowSize(window, &m.pointWidth, &m.pointHeight);
        float s = SDL_GetWindowDisplayScale(window);
        float density = SDL_GetWindowPixelDensity(window);
        if (const char* env = SDL_getenv("KOMPOZ_SCALE")) s = density = float(std::atof(env));  // a phone's density, say 2.625
        if (s <= 0) s = 1;
        if (density <= 0) density = 1;
        m.scale = s;
        m.pointToDp = density / s;
        scale = m.scale, pointToDp = m.pointToDp;
        if (const char* env = SDL_getenv("KOMPOZ_FONT_SCALE")) m.fontScale = std::max(0.5f, float(std::atof(env)));
        const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
        m.refreshRate = mode && mode->refresh_rate > 0 ? mode->refresh_rate : 60.f;
        // System bars and display cutouts (iOS notch, desktop).
        if (SDL_Rect safe; SDL_GetWindowSafeArea(window, &safe)) m.safeArea = Rect{float(safe.x), float(safe.y), float(safe.w), float(safe.h)};
        m.darkTheme = SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK;
        int count = 0;
        if (SDL_Locale** locales = SDL_GetPreferredLocales(&count)) {
            if (count > 0 && locales[0] && locales[0]->language) m.language = locales[0]->language;
            SDL_free(locales);
        }
        return m;
    }

    void metricsUpdated(ComposeHost& h) override {
#ifdef KOMPOZ_WALLPAPER_COLORS
        if (wallpaper) wallpaper->setDarkTheme(SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK);  // a dark variant
#endif
        (void)h;
    }

    void setWindowSize(int width, int height) override { SDL_SetWindowSize(window, width, height); }

    // The device for mediaQuery: a mouse is a fine pointer, a touch screen a coarse one, a keyboard
    // SDL knows of a physical one, the microphone and camera as probed (not on the web, where it
    // would ask).
    MediaInfo media(bool probe) override {
        kompoz::MediaInfo info;
#ifndef SDL_PLATFORM_EMSCRIPTEN
        if (probe && !probed) {
            probed = true;
            int count = 0;
            if (SDL_InitSubSystem(SDL_INIT_AUDIO)) {
                SDL_free(SDL_GetAudioRecordingDevices(&count));
                hasMicrophone = count > 0;
                SDL_QuitSubSystem(SDL_INIT_AUDIO);
            }
            count = 0;
            if (SDL_InitSubSystem(SDL_INIT_CAMERA)) {
                SDL_free(SDL_GetCameras(&count));
                hasCamera = count > 0;
                SDL_QuitSubSystem(SDL_INIT_CAMERA);
            }
        }
#else
        (void)probe;
#endif
        int mice = 0, keyboards = 0, touch = 0;
        SDL_free(SDL_GetMice(&mice));
        SDL_free(SDL_GetKeyboards(&keyboards));
        SDL_free(SDL_GetTouchDevices(&touch));
        info.pointerPrecision = mice > 0 ? kompoz::PointerPrecision::Fine : touch > 0 ? kompoz::PointerPrecision::Coarse : kompoz::PointerPrecision::None;
        info.physicalKeyboard = keyboards > 0;
        info.hasMicrophone = hasMicrophone;
        info.hasCamera = hasCamera;
        return info;
    }

    void attach(ComposeHost& h, ComposeHost::Callbacks& callbacks) override;
    void started(ComposeHost& h) override;
    std::function<void(const platform::Event&)> deliver;  // to the core, now (SdlApp)
    void handled(ComposeHost& h, const platform::Event& e) override;
    void composed(ComposeHost& h, bool redraw, int64_t frameTime) override;
    bool wantsFrames() const override {
        bool more = false;
#ifdef KOMPOZ_A11Y_BRIDGE
        more = more || (a11y && a11y->active() && semanticsDirty);  // the same
#endif
#ifdef SDL_PLATFORM_EMSCRIPTEN
        more = true;  // the browser's frames don't wait for events there: input would sit until the next timer
#endif
        return more;
    }
};

// SDL's event as the app core takes it (platform::Event), or none for one it doesn't use. What
// is SDL's own is dealt with here: the mouse events it makes up from fingers left out, a finger's
// position as a fraction of the window, a touchpad pinch's scale as Wayland gives it.
std::optional<platform::Event> fromSdl(SdlPlatform& app, const SDL_Event& e) {
    using platform::EventType;
    platform::Event out;
    auto held = [](SDL_Keymod m) { return KeyModifiers{(m & SDL_KMOD_SHIFT) != 0, (m & (SDL_KMOD_CTRL | SDL_KMOD_GUI)) != 0, (m & SDL_KMOD_ALT) != 0}; };
    auto with = [&](EventType type) {
        out.type = type;
        return std::optional(out);
    };
#ifdef KOMPOZ_DESKTOP_ACCENT
    if (e.type == app.accentEvent) return with(EventType::AccentChanged);
#endif
#ifdef KOMPOZ_WALLPAPER_COLORS
    if (e.type == app.wallpaperEvent) return with(EventType::WallpaperChanged);
#endif
#ifdef KOMPOZ_A11Y_BRIDGE
    if (e.type == app.a11yEvent) return with(EventType::AccessibilityRequest);
#endif
    if (e.type == app.wakeEvent) return with(EventType::Wake);
    switch (e.type) {
    case SDL_EVENT_QUIT: return with(EventType::Quit);
    case SDL_EVENT_WINDOW_FOCUS_GAINED: return with(EventType::FocusGained);
    case SDL_EVENT_WINDOW_FOCUS_LOST: return with(EventType::FocusLost);
    case SDL_EVENT_WINDOW_SHOWN: return with(EventType::Shown);
    case SDL_EVENT_WINDOW_HIDDEN: return with(EventType::Hidden);
    case SDL_EVENT_WINDOW_MINIMIZED: return with(EventType::Minimized);
    case SDL_EVENT_WINDOW_MAXIMIZED: return with(EventType::Maximized);
    case SDL_EVENT_WINDOW_RESTORED: return with(EventType::Restored);
    case SDL_EVENT_WINDOW_EXPOSED: return with(EventType::Exposed);
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_RESIZED: return with(EventType::Resized);
    case SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_WINDOW_SAFE_AREA_CHANGED:
    case SDL_EVENT_SYSTEM_THEME_CHANGED:
    case SDL_EVENT_LOCALE_CHANGED: return with(EventType::MetricsChanged);
    case SDL_EVENT_MOUSE_ADDED:
    case SDL_EVENT_MOUSE_REMOVED:
    case SDL_EVENT_KEYBOARD_ADDED:
    case SDL_EVENT_KEYBOARD_REMOVED: return with(EventType::DevicesChanged);
    case SDL_EVENT_RENDER_TARGETS_RESET: return with(EventType::SurfaceLost);
    case SDL_EVENT_WILL_ENTER_BACKGROUND: return with(EventType::WillEnterBackground);
    case SDL_EVENT_DID_ENTER_BACKGROUND: return with(EventType::DidEnterBackground);
    case SDL_EVENT_WILL_ENTER_FOREGROUND: return with(EventType::WillEnterForeground);
    case SDL_EVENT_DID_ENTER_FOREGROUND: return with(EventType::DidEnterForeground);
    case SDL_EVENT_LOW_MEMORY: return with(EventType::LowMemory);
    case SDL_EVENT_WINDOW_MOUSE_LEAVE: return with(EventType::PointerLeft);
    // Touch screens send fingers (each with its own id, for pinch and twist) and SDL also makes up
    // mouse events from them: those are left out.
    case SDL_EVENT_MOUSE_MOTION:
        if (e.motion.which == SDL_TOUCH_MOUSEID) return std::nullopt;
        out.x = e.motion.x, out.y = e.motion.y, out.time = int64_t(e.motion.timestamp), out.modifiers = held(SDL_GetModState());
        return with(EventType::MouseMove);
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        if (e.button.which == SDL_TOUCH_MOUSEID) return std::nullopt;
        switch (e.button.button) {
        case SDL_BUTTON_LEFT: out.button = platform::MouseButton::Left; break;
        case SDL_BUTTON_RIGHT: out.button = platform::MouseButton::Right; break;
        case SDL_BUTTON_MIDDLE: out.button = platform::MouseButton::Middle; break;
        case SDL_BUTTON_X1: out.button = platform::MouseButton::Back; break;
        case SDL_BUTTON_X2: out.button = platform::MouseButton::Forward; break;
        default: return std::nullopt;
        }
        out.x = e.button.x, out.y = e.button.y, out.time = int64_t(e.button.timestamp), out.modifiers = held(SDL_GetModState());
        return with(e.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? EventType::MouseDown : EventType::MouseUp);
    case SDL_EVENT_FINGER_DOWN:
    case SDL_EVENT_FINGER_MOTION:
    case SDL_EVENT_FINGER_UP:
    case SDL_EVENT_FINGER_CANCELED: {
        if (SDL_GetTouchDeviceType(e.tfinger.touchID) != SDL_TOUCH_DEVICE_DIRECT) return std::nullopt;  // touchpads move the mouse
        int w = 0, h = 0;  // finger positions are fractions of the window
        SDL_GetWindowSize(app.window, &w, &h);
        out.x = e.tfinger.x * float(w), out.y = e.tfinger.y * float(h);
        out.pointer = int64_t(e.tfinger.fingerID), out.time = int64_t(e.tfinger.timestamp);
        return with(e.type == SDL_EVENT_FINGER_DOWN ? EventType::TouchDown : e.type == SDL_EVENT_FINGER_MOTION ? EventType::TouchMove : EventType::TouchUp);
    }
    case SDL_EVENT_MOUSE_WHEEL: {
        float flip = e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -1.f : 1.f;
        out.x = e.wheel.mouse_x, out.y = e.wheel.mouse_y, out.dx = e.wheel.x * flip, out.dy = e.wheel.y * flip;
        out.modifiers = held(SDL_GetModState());
#ifdef SDL_PLATFORM_EMSCRIPTEN
        out.modifiers.ctrl = out.modifiers.ctrl || kompoz_web_wheel_ctrl();  // (a touchpad pinch, as the browser sends it)
#endif
        return with(EventType::Wheel);
    }
    case SDL_EVENT_PINCH_BEGIN:
        app.pinchScale = 1;
        return with(EventType::PinchBegin);
    case SDL_EVENT_PINCH_UPDATE: {  // a touchpad pinch (Wayland, macOS): around the mouse pointer
        out.scale = e.pinch.scale;
        // Wayland gives the scale since the pinch began, which SDL passes on as the change since
        // the last update: multiplied up, the smallest finger movement zoomed a lot.
        if (app.pinchSinceStart) {
            out.scale = e.pinch.scale / std::max(app.pinchScale, 1e-3f);
            app.pinchScale = e.pinch.scale;
        }
        SDL_GetMouseState(&out.x, &out.y);
        return with(EventType::Pinch);
    }
    case SDL_EVENT_TEXT_INPUT:
        out.text = e.text.text;
        return with(EventType::Text);
    case SDL_EVENT_KEY_DOWN:
    case SDL_EVENT_KEY_UP:
        out.modifiers = held(e.key.mod);
        if (e.key.key == SDLK_AC_BACK) {  // a keyboard's Back key
            if (e.type == SDL_EVENT_KEY_UP) return std::nullopt;
            return with(EventType::Back);
        }
        out.key = keyOf(e.key.key);  // (Unknown too: the keys it holds down, a Shift, still count)
        return with(e.type == SDL_EVENT_KEY_DOWN ? EventType::KeyDown : EventType::KeyUp);
    case SDL_EVENT_DROP_BEGIN: return with(EventType::DropBegin);
    case SDL_EVENT_DROP_POSITION:
        out.x = e.drop.x, out.y = e.drop.y;
        return with(EventType::DropMove);
    case SDL_EVENT_DROP_FILE:
    case SDL_EVENT_DROP_TEXT:
        out.x = e.drop.x, out.y = e.drop.y;
        if (e.drop.data) out.text = e.drop.data;
        return with(e.type == SDL_EVENT_DROP_FILE ? EventType::DropFile : EventType::DropText);
    case SDL_EVENT_DROP_COMPLETE: return with(EventType::DropComplete);
    default: return std::nullopt;
    }
}


void SdlPlatform::attach(ComposeHost& h, ComposeHost::Callbacks& callbacks) {
    host = &h;
    SdlPlatform* raw = this;
    // A module on a thread of its own tells with an event of a type of its own (SDL's thread-local
    // state let go as its thread ends: SDL didn't start it).
    auto notifier = [](Uint32& type) {
        type = SDL_RegisterEvents(1);
        return platform::Notifier{.notify =
                                      [type] {
                                          SDL_Event e{};
                                          e.type = type;
                                          SDL_PushEvent(&e);
                                      },
                                  .threadDone = [] { SDL_CleanupTLS(); }};
    };
    (void)notifier;
#ifdef KOMPOZ_DESKTOP_ACCENT
    accent = std::make_unique<platform::DesktopAccent>(notifier(accentEvent));  // tells its color, soon
#endif
#ifdef KOMPOZ_WALLPAPER_COLORS
    wallpaper = std::make_unique<platform::WallpaperColors>(SDL_GetSystemTheme() == SDL_SYSTEM_THEME_DARK, notifier(wallpaperEvent));
#endif
#ifdef KOMPOZ_ATSPI
    a11y = std::make_unique<platform::AtspiBridge>(SDL_GetWindowTitle(window), notifier(a11yEvent));  // connects on its thread, if wanted
#elif defined(KOMPOZ_UIA)
    a11y = std::make_unique<platform::UiaBridge>(SDL_GetPointerProperty(SDL_GetWindowProperties(window), SDL_PROP_WINDOW_WIN32_HWND_POINTER, nullptr), notifier(a11yEvent));
#endif
    wakeEvent = SDL_RegisterEvents(1);
    const SDL_SystemCursor systemCursors[] = {SDL_SYSTEM_CURSOR_DEFAULT,   SDL_SYSTEM_CURSOR_POINTER,   SDL_SYSTEM_CURSOR_TEXT,
                                              SDL_SYSTEM_CURSOR_CROSSHAIR, SDL_SYSTEM_CURSOR_MOVE,      SDL_SYSTEM_CURSOR_EW_RESIZE,
                                              SDL_SYSTEM_CURSOR_NS_RESIZE, SDL_SYSTEM_CURSOR_NOT_ALLOWED, SDL_SYSTEM_CURSOR_WAIT};
    for (size_t i = 0; i < std::size(systemCursors); ++i) cursors[i] = SDL_CreateSystemCursor(systemCursors[i]);
    if (const char* driver = SDL_GetCurrentVideoDriver()) pinchSinceStart = std::string_view(driver) == "wayland";
    callbacks.wake = [raw] {
        if (raw->wakePending.exchange(true)) return;
        SDL_Event e{};
        e.type = raw->wakeEvent;
        SDL_PushEvent(&e);
    };
    callbacks.textInput = [raw](bool active, kompoz::Rect area, const kompoz::TextEditState&, const kompoz::ImeOptions& options) {
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
    };
    callbacks.cursor = [raw](kompoz::Cursor cursor) {
        SDL_Cursor* c = raw->cursors[int(cursor)];
        SDL_SetCursor(c ? c : raw->cursors[0]);  // a look the system doesn't have: the arrow
    };
    callbacks.getClipboard = [] {
        char* text = SDL_GetClipboardText();
        std::string result = text ? text : "";
        SDL_free(text);
        return result;
    };
    callbacks.setClipboard = [](const std::string& text) { SDL_SetClipboardText(text.c_str()); };
    callbacks.systemBarsAppearance = [](bool lightStatusBars, bool lightNavigationBars) {
        (void)lightStatusBars, (void)lightNavigationBars;  // desktop windows have no system bars
    };
#ifdef SDL_PLATFORM_EMSCRIPTEN
    callbacks.locationChanged = [](const std::string& path) { kompoz_web_set_hash(path.c_str()); };
#endif
    // The browser, the app for the scheme (xdg-open, ShellExecute, window.open).
    callbacks.openUri = [](const std::string& uri) {
        if (!SDL_OpenURL(uri.c_str())) SDL_Log("kompoz: can't open %s: %s", uri.c_str(), SDL_GetError());
    };
    callbacks.hapticFeedback = [](kompoz::HapticFeedbackType type) {
#if defined(SDL_PLATFORM_EMSCRIPTEN)
        using T = kompoz::HapticFeedbackType;  // a press is felt longer than a tick
        bool strong = type == T::LongPress || type == T::Confirm || type == T::Reject || type == T::GestureThresholdActivate;
        kompoz_web_vibrate(strong ? 25 : 8);
#else
        (void)type;  // nothing to vibrate
#endif
    };
}

void SdlPlatform::started(ComposeHost& h) {
    SdlPlatform* raw = this;
    (void)raw;
#ifdef SDL_PLATFORM_EMSCRIPTEN
    kompoz_web_watch_wheel();
    if (char* hash = kompoz_web_take_hash()) {  // the web address's #fragment: a deep link
        h.openDeepLink(hash);
        free(hash);
    }
#endif
    (void)h;
}

void SdlPlatform::handled(ComposeHost& h, const platform::Event& e) {
    using platform::EventType;
    switch (e.type) {
#ifdef KOMPOZ_DESKTOP_ACCENT
    case EventType::AccentChanged:  // learned or changed
        if (accent) {
            std::optional<Color> color = accent->accent();
            h.setDynamicTonalPalette(color ? std::optional(TonalPalette::fromSeed(*color)) : std::nullopt);
        }
        break;
#endif
#ifdef KOMPOZ_WALLPAPER_COLORS
    case EventType::WallpaperChanged:  // read or changed
        if (wallpaper) h.setWallpaperTonalPalette(wallpaper->palette());
        break;
#endif
#ifdef KOMPOZ_A11Y_BRIDGE
    case EventType::AccessibilityRequest:  // asked for, or connected
        if (a11y)
            for (const auto& r : a11y->takeRequests()) {
                if (r.action == kompoz::SemanticsAction::SetText) h.performSemanticsAction(r.id, r.action, r.text);
                else h.performSemanticsAction(r.id, r.action, r.argument);
            }
        break;
#endif
    case EventType::FocusGained:
    case EventType::FocusLost:
        focused = e.type == EventType::FocusGained;
        semanticsDirty = true;  // (AT-SPI tells which window is active)
        break;
    case EventType::Wake: wakePending = false; break;
    default: break;
    }
}

void SdlPlatform::composed(ComposeHost& h, bool redraw, int64_t now) {
    if (bool on = h.keepScreenOn(); on != keepScreenOn) {  // keepScreenOn: the screen saver off while it is laid out
        keepScreenOn = on;
        if (on) SDL_DisableScreenSaver();
        else SDL_EnableScreenSaver();
    }
#ifdef KOMPOZ_A11Y_BRIDGE
    // The same for AT-SPI and UI Automation, which answer from the tree last published (on threads
    // of their own), with where the window is: once asked for (they want the first), after frames
    // that changed the screen.
    bool treeRequested = a11y && a11y->takeTreeRequest();
    semanticsDirty = semanticsDirty || redraw || treeRequested;
    if (a11y && a11y->active() && semanticsDirty && (treeRequested || now - lastSemantics > (h.hasPendingWork() ? 500'000'000 : 50'000'000))) {
        lastSemantics = now;
        semanticsDirty = false;
#ifdef KOMPOZ_ATSPI
        int x = 0, y = 0, w = 0, ht = 0;
        SDL_GetWindowPosition(window, &x, &y);
        SDL_GetWindowSize(window, &w, &ht);
        const char* title = SDL_GetWindowTitle(window);
        a11y->publish(h.semanticsTree(), {.x = float(x), .y = float(y), .width = float(w), .height = float(ht),
                                          .dpToPoints = 1 / pointToDp, .focused = focused, .title = title ? title : ""});
#else
        a11y->publish(h.semanticsTree(), SDL_GetWindowPixelDensity(window) / pointToDp);
#endif
    }
#endif
    (void)redraw, (void)now;
}

// The SDL app: the platform, and the core on it.
struct SdlApp {
    SdlPlatform platform;
    platform::AppCore core{platform};
    SdlApp() {
        platform.deliver = [this](const platform::Event& e) { core.handle(e); };
    }
};

} // namespace
} // namespace kompoz::sdl

using kompoz::sdl::SdlApp;

SDL_AppResult SDL_AppInit(void** appstate, int argc, char* argv[]) {
    auto app = std::make_unique<SdlApp>();
    // The screen may go dark as usual (SDL keeps it on by default); Modifier::keepScreenOn
    // keeps it on while it is laid out.
    SDL_SetHint(SDL_HINT_VIDEO_ALLOW_SCREENSAVER, "1");
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("kompoz: SDL_Init failed: %s", SDL_GetError());
        return SDL_APP_FAILURE;
    }
    if (!app->core.start(argc, argv)) return SDL_APP_FAILURE;
    *appstate = app.release();
    return SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppIterate(void* appstate) {
    SdlApp& app = *static_cast<SdlApp*>(appstate);
    kompoz::sdl::SdlPlatform& p = app.platform;
#ifdef SDL_PLATFORM_EMSCRIPTEN
    if (char* hash = kompoz_web_take_hash()) {  // typed in the address, or a link to #... followed
        app.core.host().openDeepLink(hash);
        free(hash);
    }
#else  // (the browser's frames come on its beat)
    // The pause until the frame is due is the host's for work between frames (a lazy list's next item).
    if (int64_t due = app.core.frameDue(), at = p.now(); due >= 0 && at < due) {
        app.core.idle(due - at);
        if (at = p.now(); at < due) SDL_DelayNS(uint64_t(due - at));
    }
#endif
    kompoz::platform::Next next = app.core.frame();
    // Sleep until the next event when there is nothing to animate or recompose, or until the
    // next coroutine delay is due.
    if (int64_t deadline = next.wakeAt; !next.busy && deadline >= 0 && deadline != p.wakeDeadline) {
        if (p.wakeTimer) SDL_RemoveTimer(p.wakeTimer);
        p.wakeDeadline = deadline;
        int64_t delayNs = std::max<int64_t>(deadline - p.now(), 0);
        p.wakeTimer = SDL_AddTimerNS(
            Uint64(delayNs),
            [](void* event, SDL_TimerID, Uint64) -> Uint64 {
                SDL_Event e{};
                e.type = Uint32(reinterpret_cast<uintptr_t>(event));
                SDL_PushEvent(&e);
                return 0;
            },
            reinterpret_cast<void*>(uintptr_t(p.wakeEvent)));
    }
    if (next.busy == p.waiting) {
        p.waiting = !next.busy;
        SDL_SetHint(SDL_HINT_MAIN_CALLBACK_RATE, next.busy ? "0" : "waitevent");
    }
    return next.quit ? SDL_APP_SUCCESS : SDL_APP_CONTINUE;
}

SDL_AppResult SDL_AppEvent(void* appstate, SDL_Event* event) {
    SdlApp& app = *static_cast<SdlApp*>(appstate);
    if (event->type == SDL_EVENT_QUIT) return SDL_APP_SUCCESS;
    if (std::optional<kompoz::platform::Event> e = kompoz::sdl::fromSdl(app.platform, *event)) app.core.handle(*e);
    return SDL_APP_CONTINUE;
}

void SDL_AppQuit(void* appstate, SDL_AppResult) {
    auto* app = static_cast<SdlApp*>(appstate);
    if (app) app->core.quit();
    delete app;
}

// (SDL_main.h makes it the program's entry: SDL_main where SDL's own starts the app.)
int main(int argc, char* argv[]) {
#ifdef KOMPOZ_HEADLESS
    if (kompoz::platform::headlessRequested()) return kompoz::platform::runHeadless(argc, argv);
#endif
    return SDL_EnterAppMainCallbacks(argc, argv, SDL_AppInit, SDL_AppIterate, SDL_AppEvent, SDL_AppQuit);
}
