#include "wayland_platform.hpp"

#include "../../app/app_core.hpp"
#include "../../common/system.hpp"
#include "../media_devices.hpp"
#include "wayland_dynamic.hpp"
// (after wayland_dynamic.hpp: their inline functions call through its pointers)
#include "generated/cursor-shape-v1-client-protocol.h"
#include "generated/fractional-scale-v1-client-protocol.h"
#include "generated/idle-inhibit-unstable-v1-client-protocol.h"
#include "generated/pointer-gestures-unstable-v1-client-protocol.h"
#include "generated/text-input-unstable-v3-client-protocol.h"
#include "generated/viewporter-client-protocol.h"
#include "generated/wayland-client-protocol.h"
#include "generated/xdg-decoration-unstable-v1-client-protocol.h"
#include "generated/xdg-shell-client-protocol.h"

#ifdef KOMPOZ_DESKTOP_ACCENT
#include "../../common/desktop_accent.hpp"
#endif
#ifdef KOMPOZ_WALLPAPER_COLORS
#include "../../common/wallpaper_colors.hpp"
#endif
#ifdef KOMPOZ_ATSPI
#include "../atspi_bridge.hpp"
#endif
#ifdef KOMPOZ_GPU_VULKAN
#define VK_USE_PLATFORM_WAYLAND_KHR 1
#include <vulkan/vulkan.h>
#endif
#define EGL_NO_X11 1
#include <EGL/egl.h>
#include <EGL/eglext.h>

#include <dlfcn.h>
#include <fcntl.h>
#include <poll.h>
#include <spawn.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/timerfd.h>
#include <sys/wait.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <deque>
#include <initializer_list>
#include <list>
#include <map>
#include <memory>
#include <set>
#include <string>
#include <thread>
#include <vector>

extern char** environ;

namespace kompoz::platform {
namespace {

// xkbcommon's keysyms as Kompoz's keys.
Key keyOf(xkb_keysym_t sym) {
    if (sym >= 'a' && sym <= 'z') return Key(int(Key::A) + int(sym - 'a'));
    if (sym >= 'A' && sym <= 'Z') return Key(int(Key::A) + int(sym - 'A'));
    if (sym >= '0' && sym <= '9') return Key(int(Key::Zero) + int(sym - '0'));
    if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12) return Key(int(Key::F1) + int(sym - XKB_KEY_F1));
    switch (sym) {
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter: return Key::Enter;
    case XKB_KEY_Escape: return Key::Escape;
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab: return Key::Tab;
    case XKB_KEY_BackSpace: return Key::Backspace;
    case XKB_KEY_Delete: return Key::Delete;
    case XKB_KEY_space: return Key::Space;
    case XKB_KEY_Left: return Key::Left;
    case XKB_KEY_Right: return Key::Right;
    case XKB_KEY_Up: return Key::Up;
    case XKB_KEY_Down: return Key::Down;
    case XKB_KEY_Home: return Key::Home;
    case XKB_KEY_End: return Key::End;
    case XKB_KEY_Page_Up: return Key::PageUp;
    case XKB_KEY_Page_Down: return Key::PageDown;
    default: return Key::Unknown;
    }
}

int64_t monotonicNow() {
    timespec t{};
    clock_gettime(CLOCK_MONOTONIC, &t);
    return int64_t(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
}

// EGL, loaded at run time as libwayland is (its functions by their declarations' types).
struct Egl {
    decltype(&eglGetPlatformDisplay) getPlatformDisplay = nullptr;
    decltype(&eglInitialize) initialize = nullptr;
    decltype(&eglBindAPI) bindApi = nullptr;
    decltype(&eglChooseConfig) chooseConfig = nullptr;
    decltype(&eglCreateContext) createContext = nullptr;
    decltype(&eglCreatePlatformWindowSurface) createPlatformWindowSurface = nullptr;
    decltype(&eglMakeCurrent) makeCurrent = nullptr;
    decltype(&eglSwapBuffers) swapBuffers = nullptr;
    decltype(&eglSwapInterval) swapInterval = nullptr;
    decltype(&eglDestroySurface) destroySurface = nullptr;
    decltype(&eglDestroyContext) destroyContext = nullptr;
    decltype(&eglTerminate) terminate = nullptr;
    decltype(&eglGetProcAddress) getProcAddress = nullptr;
    void* library = nullptr;

    bool load() {
        if (!library) library = dlopen("libEGL.so.1", RTLD_NOW | RTLD_LOCAL);
        if (!library) return false;
        auto get = [this](auto& fn, const char* name) {
            fn = reinterpret_cast<std::remove_reference_t<decltype(fn)>>(dlsym(library, name));
            return fn != nullptr;
        };
        return get(getPlatformDisplay, "eglGetPlatformDisplay") && get(initialize, "eglInitialize") && get(bindApi, "eglBindAPI") &&
               get(chooseConfig, "eglChooseConfig") && get(createContext, "eglCreateContext") &&
               get(createPlatformWindowSurface, "eglCreatePlatformWindowSurface") && get(makeCurrent, "eglMakeCurrent") &&
               get(swapBuffers, "eglSwapBuffers") && get(swapInterval, "eglSwapInterval") && get(destroySurface, "eglDestroySurface") &&
               get(destroyContext, "eglDestroyContext") && get(terminate, "eglTerminate") && get(getProcAddress, "eglGetProcAddress");
    }
};
Egl gEgl;
void* gGles = nullptr;

// A display output: its scale and refresh rate, and whether the window is on it.
struct Output {
    wl_output* output = nullptr;
    uint32_t name = 0;
    int32_t scale = 1;
    int32_t refreshMhz = 0;
    bool entered = false;
};

class WaylandPlatform final : public Platform {
public:
    ~WaylandPlatform() override {
#ifdef KOMPOZ_ATSPI
        a11y_.reset();
#endif
        closeWindow();
        auto destroy = [](auto* proxy) {
            if (proxy) wl_proxy_destroy(reinterpret_cast<wl_proxy*>(proxy));
        };
        destroy(textInput_), destroy(pinch_), destroy(cursorDevice_), destroy(dataDevice_), destroy(selectionOffer_), destroy(dragOffer_);
        destroy(source_), destroy(gestures_);
        destroy(pointer_), destroy(keyboard_), destroy(touch_), destroy(seat_);
        for (Output& o : outputs_) destroy(o.output);
        destroy(compositor_), destroy(wmBase_), destroy(decorations_), destroy(fractionalManager_), destroy(viewporter_);
        destroy(shm_), destroy(subcompositor_);
        destroy(cursorShapes_), destroy(textInputs_), destroy(dataDevices_), destroy(idleInhibits_), destroy(registry_);
        if (xkbState_) xkb_state_unref(xkbState_);
        if (keymap_) xkb_keymap_unref(keymap_);
        if (xkb_) xkb_context_unref(xkb_);
        if (display_) wl_display_disconnect(display_);
        for (int fd : {wakeFd_, timerFd_, repeatFd_})
            if (fd >= 0) close(fd);
    }

    // The display, its globals and the seat; false where the app can't run here.
    bool connect() {
        if (!std::getenv("WAYLAND_DISPLAY") || !wayland::load()) return false;
        display_ = wl_display_connect(nullptr);
        if (!display_) return false;
        registry_ = wl_display_get_registry(display_);
        wl_registry_add_listener(registry_, &kRegistry, this);
        wl_display_roundtrip(display_);  // the globals
        if (!compositor_ || !wmBase_ || !shm_ || !subcompositor_) return false;
        wl_display_roundtrip(display_);  // the seat's capabilities, the outputs
        xkb_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        wakeFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        timerFd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        repeatFd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        return xkb_ && wakeFd_ >= 0 && timerFd_ >= 0 && repeatFd_ >= 0;
    }

    int run(int argc, char* argv[]) {
        AppCore core{*this};
        core_ = &core;
        if (!core.start(argc, argv)) return 1;
        bool needFrame = true;
        for (;;) {
            wl_display_dispatch_pending(display_);
            takeNotifications();
            for (const Event& e : std::exchange(events_, {})) {
                if (e.type == EventType::Quit) quit_ = true;
                core.handle(e);
                needFrame = true;
            }
            if (quit_) break;
            if (callbackDone_) {
                callbackDone_ = false;
                needFrame = needFrame || busy_;
            }
            if (needFrame && !awaitingCallback_ && configured_) {
                needFrame = false;
                // Asked for before the frame: it goes with the commit the frame presents with, else
                // with the one after it (a frame that showed nothing still hears when the next is due).
                frameCallback_ = wl_surface_frame(surface_);
                wl_callback_add_listener(frameCallback_, &kFrameCallback, this);
                awaitingCallback_ = true;
                Next next = core.frame();
                wl_surface_commit(surface_);
                if (next.quit) break;
                busy_ = next.busy;
                armTimer(next.busy ? -1 : next.wakeAt);
            }
            int timeout = needFrame && !awaitingCallback_ && configured_ ? 0 : -1;
            if (!waitForEvents(timeout)) break;  // (the compositor went away)
        }
        core.quit();
        core_ = nullptr;
        return 0;
    }

    // ---- Platform ------------------------------------------------------------------------

    std::optional<std::string> option(std::string_view name) const override {
        if (const char* v = std::getenv(std::string(name).c_str())) return std::string(v);
        return std::nullopt;
    }
    void log(const std::string& message) const override { platform::log(message); }
    int64_t now() const override { return monotonicNow(); }
    std::vector<gpu::Backend> backends() const override { return {gpu::Backend::Vulkan, gpu::Backend::GLES}; }

    bool openWindow(const Application& app, gpu::Backend backend, gpu::Surface& surface, std::string* error) override {
        auto failed = [&](std::string why) {
            *error = std::move(why);
            closeWindow();
            return false;
        };
        title_ = app.title;
        width_ = app.width, height_ = app.height;  // (in points: the compositor's logical pixels, a dp each at its own scale)
        surface_ = wl_compositor_create_surface(compositor_);
        wl_surface_add_listener(surface_, &kSurface, this);
        if (fractionalManager_ && viewporter_) {
            fractional_ = wp_fractional_scale_manager_v1_get_fractional_scale(fractionalManager_, surface_);
            wp_fractional_scale_v1_add_listener(fractional_, &kFractional, this);
            viewport_ = wp_viewporter_get_viewport(viewporter_, surface_);
        }
        xdgSurface_ = xdg_wm_base_get_xdg_surface(wmBase_, surface_);
        xdg_surface_add_listener(xdgSurface_, &kXdgSurface, this);
        toplevel_ = xdg_surface_get_toplevel(xdgSurface_);
        xdg_toplevel_add_listener(toplevel_, &kToplevel, this);
        xdg_toplevel_set_title(toplevel_, app.title.c_str());
        xdg_toplevel_set_app_id(toplevel_, "dev.kompoz.app");
        // The compositor's decorations where it has them (unless KOMPOZ_DECORATIONS=kompoz), else
        // Kompoz's own: as the decoration's configure says.
        if (decorations_) {
            decoration_ = zxdg_decoration_manager_v1_get_toplevel_decoration(decorations_, toplevel_);
            zxdg_toplevel_decoration_v1_add_listener(decoration_, &kDecoration, this);
            bool own = option("KOMPOZ_DECORATIONS") == "kompoz";
            zxdg_toplevel_decoration_v1_set_mode(decoration_, own ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE : ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
        } else {
            ownDecorations_ = true;
        }
        wl_surface_commit(surface_);
        while (!configured_)  // (the first configure, acknowledged before anything is drawn)
            if (wl_display_roundtrip(display_) < 0) return failed("the compositor didn't configure the window");
        applySize();
        updateEdges();
        surface.drawableSize = [this](int* w, int* h) { *w = pixelWidth(), *h = pixelHeight(); };
        switch (backend) {
#ifdef KOMPOZ_GPU_VULKAN
        case gpu::Backend::Vulkan: {
            if (!vulkan_) vulkan_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
            auto getProcAddress = vulkan_ ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(vulkan_, "vkGetInstanceProcAddr")) : nullptr;
            if (!getProcAddress) return failed("no Vulkan loader");
            surface.vulkanGetInstanceProcAddr = reinterpret_cast<void* (*)(void*, const char*)>(getProcAddress);
            surface.vulkanInstanceExtensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WAYLAND_SURFACE_EXTENSION_NAME};
            surface.createVulkanSurface = [this, getProcAddress](void* instance, uint64_t* out) {
                auto create = reinterpret_cast<PFN_vkCreateWaylandSurfaceKHR>(getProcAddress(static_cast<VkInstance>(instance), "vkCreateWaylandSurfaceKHR"));
                VkWaylandSurfaceCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR, .display = display_, .surface = surface_};
                VkSurfaceKHR made = VK_NULL_HANDLE;
                if (!create || create(static_cast<VkInstance>(instance), &info, nullptr, &made) != VK_SUCCESS) return false;
                std::memcpy(out, &made, sizeof made);
                return true;
            };
            return true;
        }
#endif
        case gpu::Backend::GLES: return openGles(surface, error) || failed(*error);
        default: break;
        }
        return failed(std::string("no ") + gpu::backendName(backend) + " here");
    }

    void closeWindow() override {
        if (eglDisplay_ != EGL_NO_DISPLAY) {
            gEgl.makeCurrent(eglDisplay_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (eglSurface_ != EGL_NO_SURFACE) gEgl.destroySurface(eglDisplay_, eglSurface_);
            if (eglContext_ != EGL_NO_CONTEXT) gEgl.destroyContext(eglDisplay_, eglContext_);
            gEgl.terminate(eglDisplay_);
        }
        eglDisplay_ = EGL_NO_DISPLAY, eglSurface_ = EGL_NO_SURFACE, eglContext_ = EGL_NO_CONTEXT;
        if (eglWindow_) wl_egl_window_destroy(std::exchange(eglWindow_, nullptr));
        auto destroy = [](auto*& proxy) {
            if (proxy) wl_proxy_destroy(reinterpret_cast<wl_proxy*>(std::exchange(proxy, nullptr)));
        };
        for (Edge& e : edges_) {
            destroy(e.subsurface), destroy(e.surface);
            e = {};
        }
        for (ShmBuffer& b : shmBuffers_) wl_buffer_destroy(b.buffer);
        shmBuffers_.clear();
        destroy(frameCallback_), destroy(inhibitor_), destroy(decoration_), destroy(viewport_), destroy(fractional_), destroy(toplevel_), destroy(xdgSurface_);
        destroy(surface_);
        configured_ = false, awaitingCallback_ = false;
        if (vulkan_) dlclose(std::exchange(vulkan_, nullptr));
    }

    void showError(const std::string& message) override { platform::log(message); }

    // Points are the compositor's logical pixels; the scale is its own (fractional-scale's, else
    // the buffer scale); KOMPOZ_SCALE and KOMPOZ_FONT_SCALE stand in for them.
    Metrics metrics() const override {
        Metrics m;
        m.pixelWidth = pixelWidth(), m.pixelHeight = pixelHeight();
        m.pointWidth = width_, m.pointHeight = height_;
        m.scale = float(scale());
        if (auto v = option("KOMPOZ_SCALE"); v && std::atof(v->c_str()) > 0) m.scale = float(std::atof(v->c_str()));
        m.pointToDp = float(scale()) / m.scale;
        if (auto v = option("KOMPOZ_FONT_SCALE")) m.fontScale = std::max(0.5f, float(std::atof(v->c_str())));
        m.refreshRate = refreshRate();
        m.safeArea = Rect{0, 0, float(width_), float(height_)};
#ifdef KOMPOZ_DESKTOP_ACCENT
        m.darkTheme = accent_ && accent_->darkScheme().value_or(false);
#endif
        m.language = preferredLanguage();
        if (ownDecorations_ && !state_.fullscreen) m.titleBar = titleBar();
        return m;
    }
    void metricsUpdated(ComposeHost&) override {
#ifdef KOMPOZ_WALLPAPER_COLORS
        if (wallpaper_ && accent_) wallpaper_->setDarkTheme(accent_->darkScheme().value_or(false));
#endif
    }
    void pixelSize(int* width, int* height) const override { *width = pixelWidth(), *height = pixelHeight(); }
    void setWindowSize(int width, int height) override {
        width_ = width, height_ = height;
        applySize();
        updateEdges();
        events_.push_back({.type = EventType::Resized});
    }

    void windowAction(WindowAction action, float x, float y) override {
        if (!toplevel_) return;
        switch (action) {
        case WindowAction::Move:
            if (!seat_) return;
            letGo();
            xdg_toplevel_move(toplevel_, seat_, pressSerial_);
            break;
        case WindowAction::Minimize: xdg_toplevel_set_minimized(toplevel_); break;
        case WindowAction::ToggleMaximize:
            if (state_.maximized) xdg_toplevel_unset_maximized(toplevel_);
            else xdg_toplevel_set_maximized(toplevel_);
            break;
        case WindowAction::Close: events_.push_back({.type = EventType::Quit}); break;
        case WindowAction::Menu:
            if (seat_) xdg_toplevel_show_window_menu(toplevel_, seat_, pressSerial_, int(x), int(y));
            break;
        }
    }

    // A mouse is a fine pointer, a touch screen a coarse one, a keyboard the seat has a physical
    // one; the microphone and camera as probed (media_devices.hpp).
    MediaInfo media(bool probe) override {
        if (probe && !probed_) {
            probed_ = true;
            hasMicrophone_ = hasMicrophone(), hasCamera_ = hasCamera();
        }
        MediaInfo info;
        info.pointerPrecision = pointer_ ? PointerPrecision::Fine : touch_ ? PointerPrecision::Coarse : PointerPrecision::None;
        info.physicalKeyboard = keyboard_ != nullptr;
        info.hasMicrophone = hasMicrophone_, info.hasCamera = hasCamera_;
        return info;
    }

    void attach(ComposeHost& h, ComposeHost::Callbacks& callbacks) override {
        host_ = &h;
        WaylandPlatform* self = this;
        auto notifier = [self](std::atomic<bool>& flag) {
            return Notifier{.notify = [self, &flag] {
                flag = true;
                self->wake();
            }};
        };
        (void)notifier;
#ifdef KOMPOZ_DESKTOP_ACCENT
        accent_ = std::make_unique<DesktopAccent>(notifier(accentChanged_));
#endif
#ifdef KOMPOZ_WALLPAPER_COLORS
        wallpaper_ = std::make_unique<WallpaperColors>(false, notifier(wallpaperChanged_));
#endif
#ifdef KOMPOZ_ATSPI
        a11y_ = std::make_unique<AtspiBridge>(title_, notifier(a11yRequested_));
#endif
        callbacks.wake = [self] { self->wake(); };
        callbacks.textInput = [self](bool active, Rect area, const TextEditState&, const ImeOptions& options) {
            self->textActive_ = active;
            float k = 1 / self->pointToDp();  // dp -> points
            self->textArea_ = {int(area.x * k), int(area.y * k), int(area.width * k), int(area.height * k)};
            self->textOptions_ = options;
            self->tellTextInput();
        };
        callbacks.cursor = [self](Cursor cursor) {
            self->cursor_ = cursor;
            self->showCursor();
        };
        callbacks.getClipboard = [self] { return self->readClipboard(); };
        callbacks.setClipboard = [self](const std::string& text) { self->writeClipboard(text); };
        callbacks.openUri = [self](const std::string& uri) { self->open(uri); };
    }

    void handled(ComposeHost& h, const Event& e) override {
        switch (e.type) {
#ifdef KOMPOZ_DESKTOP_ACCENT
        case EventType::AccentChanged:
            if (accent_) {
                std::optional<Color> color = accent_->accent();
                h.setDynamicTonalPalette(color ? std::optional(TonalPalette::fromSeed(*color)) : std::nullopt);
            }
            break;
#endif
#ifdef KOMPOZ_WALLPAPER_COLORS
        case EventType::WallpaperChanged:
            if (wallpaper_) h.setWallpaperTonalPalette(wallpaper_->palette());
            break;
#endif
#ifdef KOMPOZ_ATSPI
        case EventType::AccessibilityRequest:
            if (a11y_)
                for (const auto& r : a11y_->takeRequests()) {
                    if (r.action == SemanticsAction::SetText) h.performSemanticsAction(r.id, r.action, r.text);
                    else h.performSemanticsAction(r.id, r.action, r.argument);
                }
            break;
#endif
        case EventType::FocusGained:
        case EventType::FocusLost:
            focused_ = e.type == EventType::FocusGained;
            semanticsDirty_ = true;
            break;
        default: break;
        }
        (void)h;
    }

    void composed(ComposeHost& h, bool redraw, int64_t frameTime) override {
        // keepScreenOn: an idle inhibitor on the window while it is laid out.
        if (bool on = h.keepScreenOn(); on != (inhibitor_ != nullptr) && idleInhibits_ && surface_) {
            if (on) inhibitor_ = zwp_idle_inhibit_manager_v1_create_inhibitor(idleInhibits_, surface_);
            else zwp_idle_inhibitor_v1_destroy(std::exchange(inhibitor_, nullptr));
        }
#ifdef KOMPOZ_ATSPI
        // Screen readers (AT-SPI) answer from the tree last published: once asked for, after frames
        // that changed the screen. Wayland doesn't tell a window where it is: at 0, 0.
        bool treeRequested = a11y_ && a11y_->takeTreeRequest();
        semanticsDirty_ = semanticsDirty_ || redraw || treeRequested;
        if (a11y_ && a11y_->active() && semanticsDirty_ &&
            (treeRequested || frameTime - lastSemantics_ > (h.hasPendingWork() ? 500'000'000 : 50'000'000))) {
            lastSemantics_ = frameTime;
            semanticsDirty_ = false;
            a11y_->publish(h.semanticsTree(), {.x = 0, .y = 0, .width = float(width_), .height = float(height_),
                                               .dpToPoints = 1 / pointToDp(), .focused = focused_, .title = title_});
        }
#else
        (void)redraw, (void)frameTime;
#endif
    }

    bool wantsFrames() const override {
#ifdef KOMPOZ_ATSPI
        return a11y_ && a11y_->active() && semanticsDirty_;  // one more frame publishes it
#else
        return false;
#endif
    }

private:
    wl_display* display_ = nullptr;
    wl_registry* registry_ = nullptr;
    wl_compositor* compositor_ = nullptr;
    xdg_wm_base* wmBase_ = nullptr;
    zxdg_decoration_manager_v1* decorations_ = nullptr;
    wp_fractional_scale_manager_v1* fractionalManager_ = nullptr;
    wp_viewporter* viewporter_ = nullptr;
    wp_cursor_shape_manager_v1* cursorShapes_ = nullptr;
    zwp_text_input_manager_v3* textInputs_ = nullptr;
    wl_data_device_manager* dataDevices_ = nullptr;
    zwp_idle_inhibit_manager_v1* idleInhibits_ = nullptr;
    zwp_pointer_gestures_v1* gestures_ = nullptr;
    wl_shm* shm_ = nullptr;
    wl_subcompositor* subcompositor_ = nullptr;
    wl_seat* seat_ = nullptr;
    std::vector<Output> outputs_;

    wl_surface* surface_ = nullptr;
    xdg_surface* xdgSurface_ = nullptr;
    xdg_toplevel* toplevel_ = nullptr;
    zxdg_toplevel_decoration_v1* decoration_ = nullptr;
    wp_fractional_scale_v1* fractional_ = nullptr;
    wp_viewport* viewport_ = nullptr;
    zwp_idle_inhibitor_v1* inhibitor_ = nullptr;
    wl_callback* frameCallback_ = nullptr;  // the frame callback asked for, until it comes
    wl_egl_window* eglWindow_ = nullptr;
    void* vulkan_ = nullptr;
    EGLDisplay eglDisplay_ = EGL_NO_DISPLAY;
    EGLContext eglContext_ = EGL_NO_CONTEXT;
    EGLSurface eglSurface_ = EGL_NO_SURFACE;
    std::string title_ = "Kompoz";
    int width_ = 1, height_ = 1;              // points
    int pendingWidth_ = 0, pendingHeight_ = 0;  // a configure's, 0: the app's own
    bool configured_ = false;
    int scale120_ = 0;     // fractional-scale's, in 120ths; 0: none told
    int bufferScale_ = 1;  // else the integer scale the surface is given

    // The window's state, as the last configure told it (the next one's until its xdg_surface
    // configure); tiled: on which sides (Edge's order) it meets another window or the screen's edge.
    struct WindowState {
        bool maximized = false, fullscreen = false, activated = false;
        std::array<bool, 4> tiled{};
        bool operator==(const WindowState&) const = default;
    };
    WindowState pendingState_, state_;
    // What the window manager does (xdg_toplevel's wm_capabilities; all until it says).
    bool canMinimize_ = true, canMaximize_ = true;
    // Kompoz's own decorations (the compositor has none, as GNOME, or KOMPOZ_DECORATIONS=kompoz):
    // the title bar the app core draws (Metrics::titleBar), and around the window its edges, a
    // shadow each, the pointer or a finger resizing the window by them (subsurfaces of the
    // window's, below it, outside its geometry).
    bool ownDecorations_ = false;
    struct ShmBuffer;
    struct Edge {
        wl_surface* surface = nullptr;
        wl_subsurface* subsurface = nullptr;
        ShmBuffer* buffer = nullptr;  // what it shows
        int x = 0, y = 0, width = 0, height = 0;  // points, from the window's top left
        bool shown = false, activated = false;     // (what its shadow was drawn for)
    };
    enum { kTop, kBottom, kLeft, kRight };
    std::array<Edge, 4> edges_;
    // Buffers the edges were drawn into: each let go once the compositor no longer reads it.
    struct ShmBuffer {
        wl_buffer* buffer = nullptr;
        bool busy = true, retired = false;
    };
    std::list<ShmBuffer> shmBuffers_;
    static constexpr int kEdgeSize = 12;    // points beyond the window
    static constexpr int kCornerSize = 16;  // along each side from a corner: the corner's (two ways)

    wl_pointer* pointer_ = nullptr;
    wl_keyboard* keyboard_ = nullptr;
    wl_touch* touch_ = nullptr;
    wp_cursor_shape_device_v1* cursorDevice_ = nullptr;
    zwp_pointer_gesture_pinch_v1* pinch_ = nullptr;
    double pinchScale_ = 1;  // the touchpad pinch's scale so far (Wayland tells it since the pinch began)
    uint32_t pointerSerial_ = 0, lastSerial_ = 0;
    uint32_t pressSerial_ = 0;   // the last button pressed or finger put down: what a move or the window menu goes by
    int32_t pressTouch_ = -1;    // its finger; -1: the pointer's
    wl_surface* pointerOn_ = nullptr;  // the surface the pointer is over (the window's or an edge's)
    std::set<int32_t> edgeTouches_;    // fingers that resize the window from an edge
    double edgeX_ = 0, edgeY_ = 0;     // the pointer over an edge, in the window's points
    uint32_t edgeCursor_ = 0;          // the resize cursor shown over it (XDG_TOPLEVEL_RESIZE_EDGE_*)
    double pointerX_ = 0, pointerY_ = 0, wheelX_ = 0, wheelY_ = 0;
    bool wheelDiscrete_ = false;
    std::map<int32_t, std::pair<double, double>> touches_;
    Cursor cursor_ = Cursor::Default;
    xkb_context* xkb_ = nullptr;
    xkb_keymap* keymap_ = nullptr;
    xkb_state* xkbState_ = nullptr;
    KeyModifiers modifiers_;
    int32_t repeatRate_ = 25, repeatDelay_ = 600;
    uint32_t repeatKey_ = 0;
    zwp_text_input_v3* textInput_ = nullptr;
    bool textActive_ = false, textEntered_ = false;  // a field takes typing; the input method is on the window
    std::array<int, 4> textArea_{};                   // the field, in points
    ImeOptions textOptions_;
    std::string pendingCommit_;

    wl_data_device* dataDevice_ = nullptr;
    wl_data_offer* selectionOffer_ = nullptr;
    wl_data_offer* dragOffer_ = nullptr;  // what is dragged over the window
    double dragX_ = 0, dragY_ = 0;
    std::map<wl_data_offer*, std::vector<std::string>> offerTypes_;
    wl_data_source* source_ = nullptr;
    std::string clipboard_;

    int wakeFd_ = -1, timerFd_ = -1, repeatFd_ = -1;
    std::deque<Event> events_;
    bool quit_ = false, busy_ = false, awaitingCallback_ = false, callbackDone_ = false;
    AppCore* core_ = nullptr;
    ComposeHost* host_ = nullptr;
    bool probed_ = false, hasMicrophone_ = false, hasCamera_ = false, focused_ = false, semanticsDirty_ = true;
    int64_t lastSemantics_ = 0;
    std::atomic<bool> accentChanged_{false}, wallpaperChanged_{false}, a11yRequested_{false};
#ifdef KOMPOZ_DESKTOP_ACCENT
    std::unique_ptr<DesktopAccent> accent_;
    std::optional<bool> dark_;
    std::optional<std::string> buttonLayout_;
#endif
#ifdef KOMPOZ_WALLPAPER_COLORS
    std::unique_ptr<WallpaperColors> wallpaper_;
#endif
#ifdef KOMPOZ_ATSPI
    std::unique_ptr<AtspiBridge> a11y_;
#endif

    // ---- Sizes and scale -------------------------------------------------------------------

    double scale() const {
        if (scale120_ > 0 && viewport_) return scale120_ / 120.0;
        return bufferScale_;
    }
    float pointToDp() const {
        float s = float(scale());
        if (auto v = option("KOMPOZ_SCALE"); v && std::atof(v->c_str()) > 0) return s / float(std::atof(v->c_str()));
        return 1;
    }
    int pixelWidth() const { return std::max(1, int(std::lround(width_ * scale()))); }
    int pixelHeight() const { return std::max(1, int(std::lround(height_ * scale()))); }
    float refreshRate() const {
        int32_t best = 0;
        for (const Output& o : outputs_)
            if (o.entered || best == 0) best = std::max(best, o.refreshMhz);
        return best > 0 ? best / 1000.f : 60.f;
    }

    // The surface's size for its scale: a viewport's destination (the buffer at the scale's
    // pixels), else the integer buffer scale.
    void applySize() {
        if (!surface_) return;
        if (viewport_ && scale120_ > 0) {
            wl_surface_set_buffer_scale(surface_, 1);
            wp_viewport_set_destination(viewport_, width_, height_);
        } else {
            wl_surface_set_buffer_scale(surface_, bufferScale_);
        }
        if (eglWindow_) wl_egl_window_resize(eglWindow_, pixelWidth(), pixelHeight(), 0, 0);
    }

    void scaleChanged() {
        applySize();
        events_.push_back({.type = EventType::MetricsChanged});
        events_.push_back({.type = EventType::Resized});
    }

    // ---- The loop ------------------------------------------------------------------------

    void wake() {
        uint64_t one = 1;
        (void)!write(wakeFd_, &one, sizeof one);
    }

    void armTimer(int64_t at) {
        itimerspec spec{};
        if (at >= 0) {
            at = std::max<int64_t>(at, 1);
            spec.it_value = {time_t(at / 1'000'000'000), long(at % 1'000'000'000)};
        }
        timerfd_settime(timerFd_, TFD_TIMER_ABSTIME, &spec, nullptr);
    }

    void takeNotifications() {
        if (accentChanged_.exchange(false)) {
            events_.push_back({.type = EventType::AccentChanged});
#ifdef KOMPOZ_DESKTOP_ACCENT
            // (The same reader tells the light/dark preference and the title bar's buttons.)
            if (accent_ && (accent_->darkScheme() != dark_ || accent_->buttonLayout() != buttonLayout_)) {
                dark_ = accent_->darkScheme(), buttonLayout_ = accent_->buttonLayout();
                events_.push_back({.type = EventType::MetricsChanged});
            }
#endif
        }
        if (wallpaperChanged_.exchange(false)) events_.push_back({.type = EventType::WallpaperChanged});
        if (a11yRequested_.exchange(false)) events_.push_back({.type = EventType::AccessibilityRequest});
    }

    // The display's events read and dispatched as they come, a wake, a timer or a key's repeat; up
    // to `timeout` ms (-1: until one). False if the connection broke.
    bool waitForEvents(int timeout) {
        while (wl_display_prepare_read(display_) != 0)
            if (wl_display_dispatch_pending(display_) < 0) return false;
        if (wl_display_flush(display_) < 0 && errno != EAGAIN) {
            wl_display_cancel_read(display_);
            return false;
        }
        pollfd fds[] = {{wl_display_get_fd(display_), POLLIN, 0}, {wakeFd_, POLLIN, 0}, {timerFd_, POLLIN, 0}, {repeatFd_, POLLIN, 0}};
        int n = poll(fds, 4, timeout);
        if (n > 0 && (fds[0].revents & POLLIN)) {
            if (wl_display_read_events(display_) < 0) return false;
        } else {
            wl_display_cancel_read(display_);
        }
        if (fds[0].revents & (POLLERR | POLLHUP)) return false;
        uint64_t count = 0;
        if (fds[1].revents & POLLIN) {
            (void)!read(wakeFd_, &count, sizeof count);
            events_.push_back({.type = EventType::Wake});
        }
        if (fds[2].revents & POLLIN) {
            (void)!read(timerFd_, &count, sizeof count);
            events_.push_back({.type = EventType::Wake});
        }
        if (fds[3].revents & POLLIN) {
            (void)!read(repeatFd_, &count, sizeof count);
            if (repeatKey_) key(repeatKey_, true);
        }
        return wl_display_dispatch_pending(display_) >= 0;
    }

    // ---- Input ----------------------------------------------------------------------------

    Event at(EventType type, double x, double y) {
        Event e{.type = type, .time = monotonicNow(), .x = float(x), .y = float(y)};
        e.modifiers = modifiers_;
        return e;
    }

    void showCursor() {
        if (!cursorDevice_ || !pointerSerial_) return;
        static constexpr wp_cursor_shape_device_v1_shape shapes[] = {
            WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT,   WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_POINTER,     WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_TEXT,
            WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_CROSSHAIR, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_MOVE,        WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_EW_RESIZE,
            WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NS_RESIZE, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NOT_ALLOWED, WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_WAIT};
        size_t i = size_t(cursor_);
        wp_cursor_shape_device_v1_set_shape(cursorDevice_, pointerSerial_, shapes[i < std::size(shapes) ? i : 0]);
    }

    // A key pressed (or repeated) or let go: the key, and the text it types while a field takes it.
    void key(uint32_t code, bool down) {
        if (!xkbState_) return;
        xkb_keysym_t sym = xkb_state_key_get_one_sym(xkbState_, code);
        events_.push_back(Event{.type = down ? EventType::KeyDown : EventType::KeyUp, .key = keyOf(sym), .modifiers = modifiers_});
        if (!down || !textActive_ || modifiers_.ctrl) return;
        char utf8[64] = {};
        if (xkb_state_key_get_utf8(xkbState_, code, utf8, sizeof utf8) > 0 && uint8_t(utf8[0]) >= 0x20 && utf8[0] != 0x7f)
            events_.push_back({.type = EventType::Text, .text = utf8});
    }

    // The input method told whether a field takes typing, where it is and what it takes (the
    // keyboard's type), while it is on the window (text-input-v3's enter).
    void tellTextInput() {
        if (!textInput_ || !textEntered_) return;
        if (textActive_) {
            zwp_text_input_v3_enable(textInput_);
            zwp_text_input_v3_set_cursor_rectangle(textInput_, textArea_[0], textArea_[1], textArea_[2], textArea_[3]);
            uint32_t hint = textOptions_.autoCorrect ? ZWP_TEXT_INPUT_V3_CONTENT_HINT_COMPLETION | ZWP_TEXT_INPUT_V3_CONTENT_HINT_SPELLCHECK
                                                     : ZWP_TEXT_INPUT_V3_CONTENT_HINT_NONE;
            uint32_t purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NORMAL;
            switch (textOptions_.keyboardType) {
            case KeyboardType::Text:
            case KeyboardType::Ascii: break;
            case KeyboardType::Number: purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_DIGITS; break;
            case KeyboardType::Decimal: purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_NUMBER; break;
            case KeyboardType::Phone: purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PHONE; break;
            case KeyboardType::Uri: purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_URL; break;
            case KeyboardType::Email: purpose = ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_EMAIL; break;
            case KeyboardType::Password:
            case KeyboardType::NumberPassword:
                purpose = textOptions_.keyboardType == KeyboardType::Password ? ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PASSWORD
                                                                             : ZWP_TEXT_INPUT_V3_CONTENT_PURPOSE_PIN;
                hint = ZWP_TEXT_INPUT_V3_CONTENT_HINT_HIDDEN_TEXT | ZWP_TEXT_INPUT_V3_CONTENT_HINT_SENSITIVE_DATA;
                break;
            }
            if (!textOptions_.singleLine) hint |= ZWP_TEXT_INPUT_V3_CONTENT_HINT_MULTILINE;
            zwp_text_input_v3_set_content_type(textInput_, hint, purpose);
        } else {
            zwp_text_input_v3_disable(textInput_);
        }
        zwp_text_input_v3_commit(textInput_);
    }

    void startRepeat(uint32_t code) {
        itimerspec spec{};
        if (repeatRate_ > 0 && keymap_ && xkb_keymap_key_repeats(keymap_, code)) {
            repeatKey_ = code;
            spec.it_value = {repeatDelay_ / 1000, long(repeatDelay_ % 1000) * 1'000'000};
            long interval = 1'000'000'000L / repeatRate_;
            spec.it_interval = {interval / 1'000'000'000, interval % 1'000'000'000};
        } else {
            repeatKey_ = 0;
        }
        timerfd_settime(repeatFd_, 0, &spec, nullptr);
    }
    void stopRepeat() {
        repeatKey_ = 0;
        itimerspec spec{};
        timerfd_settime(repeatFd_, 0, &spec, nullptr);
    }

    // ---- Kompoz's decorations --------------------------------------------------------------

    // The title bar's buttons as the desktop lays them out (GNOME's button-layout; elsewhere
    // Minimize, Maximize and Close on the right), those the window manager does.
    TitleBar titleBar() const {
        std::string layout = "appmenu:minimize,maximize,close";
#ifdef KOMPOZ_DESKTOP_ACCENT
        if (accent_)
            if (std::optional<std::string> desktop = accent_->buttonLayout()) layout = *desktop;
#endif
        TitleBar bar{.maximized = state_.maximized};
        auto side = [this](std::string_view names, std::vector<WindowButton>& out) {
            while (!names.empty()) {
                size_t comma = names.find(',');
                std::string_view name = names.substr(0, comma);
                names = comma == std::string_view::npos ? std::string_view() : names.substr(comma + 1);
                if (name == "close") out.push_back(WindowButton::Close);
                else if (name == "minimize" && canMinimize_) out.push_back(WindowButton::Minimize);
                else if (name == "maximize" && canMaximize_) out.push_back(WindowButton::Maximize);
            }
        };
        std::string_view all = layout;
        size_t colon = all.find(':');
        side(all.substr(0, colon), bar.start);
        if (colon != std::string_view::npos) side(all.substr(colon + 1), bar.end);
        return bar;
    }

    // The pointer or the finger last pressed let go, as far as the app knows: the window manager
    // takes it to move the window (no release comes to the window after).
    void letGo() {
        if (pressTouch_ < 0) {
            events_.push_back(at(EventType::MouseUp, pointerX_, pointerY_));  // (the left button's)
            return;
        }
        auto touch = touches_.find(pressTouch_);
        if (touch == touches_.end()) return;
        Event e = at(EventType::TouchUp, touch->second.first, touch->second.second);
        e.pointer = pressTouch_;
        events_.push_back(e);
        touches_.erase(touch);
    }

    int edgeOf(wl_surface* surface) const {
        for (int i = 0; i < 4; ++i)
            if (surface && edges_[i].surface == surface) return i;
        return -1;
    }

    // Where the window is resized from at (x, y), a point of an edge (the window's points): a
    // side, or a corner within kCornerSize of it.
    uint32_t resizeEdgeAt(double x, double y) const {
        bool aboveOrBelow = y < 0 || y >= height_, beside = x < 0 || x >= width_;
        uint32_t edge = XDG_TOPLEVEL_RESIZE_EDGE_NONE;
        if (y < (beside ? kCornerSize : 0)) edge |= XDG_TOPLEVEL_RESIZE_EDGE_TOP;
        else if (y >= height_ - (beside ? kCornerSize : 0)) edge |= XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM;
        if (x < (aboveOrBelow ? kCornerSize : 0)) edge |= XDG_TOPLEVEL_RESIZE_EDGE_LEFT;
        else if (x >= width_ - (aboveOrBelow ? kCornerSize : 0)) edge |= XDG_TOPLEVEL_RESIZE_EDGE_RIGHT;
        return edge;
    }

    // The pointer over an edge at (x, y) of it: the resize cursor for where it would resize from.
    void overEdge(int i, double x, double y) {
        edgeX_ = edges_[i].x + x, edgeY_ = edges_[i].y + y;
        uint32_t edge = resizeEdgeAt(edgeX_, edgeY_);
        if (!cursorDevice_ || !pointerSerial_ || edge == edgeCursor_) return;
        edgeCursor_ = edge;
        wp_cursor_shape_device_v1_shape shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_DEFAULT;
        switch (edge) {
        case XDG_TOPLEVEL_RESIZE_EDGE_TOP: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_N_RESIZE; break;
        case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_S_RESIZE; break;
        case XDG_TOPLEVEL_RESIZE_EDGE_LEFT: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_W_RESIZE; break;
        case XDG_TOPLEVEL_RESIZE_EDGE_RIGHT: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_E_RESIZE; break;
        case XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NW_RESIZE; break;
        case XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_NE_RESIZE; break;
        case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SW_RESIZE; break;
        case XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT: shape = WP_CURSOR_SHAPE_DEVICE_V1_SHAPE_SE_RESIZE; break;
        default: break;
        }
        wp_cursor_shape_device_v1_set_shape(cursorDevice_, pointerSerial_, shape);
    }

    // The edges as the window is now: around it while it floats (not maximized or fullscreen,
    // nor on its tiled sides) and Kompoz decorates it, else none. Its geometry is the window's
    // surface, without them.
    void updateEdges() {
        if (!surface_ || !xdgSurface_) return;
        std::erase_if(shmBuffers_, [](ShmBuffer& b) {
            if (!b.retired || b.busy) return false;
            wl_buffer_destroy(b.buffer);
            return true;
        });
        bool floating = ownDecorations_ && !state_.maximized && !state_.fullscreen;
        std::array<bool, 4> shown{};
        for (int i = 0; i < 4; ++i) shown[i] = floating && !state_.tiled[i];
        int m = kEdgeSize, left = shown[kLeft] ? m : 0, across = left + width_ + (shown[kRight] ? m : 0);
        const std::array<std::array<int, 4>, 4> places = {{{-left, -m, across, m}, {-left, height_, across, m}, {-m, 0, m, height_}, {width_, 0, m, height_}}};
        for (int i = 0; i < 4; ++i) {
            Edge& e = edges_[i];
            if (!shown[i]) {
                if (e.shown) {
                    wl_surface_attach(e.surface, nullptr, 0, 0);
                    wl_surface_commit(e.surface);
                    if (e.buffer) e.buffer->retired = true;
                    e.buffer = nullptr, e.shown = false;
                }
                continue;
            }
            if (!e.surface) {
                e.surface = wl_compositor_create_surface(compositor_);
                e.subsurface = wl_subcompositor_get_subsurface(subcompositor_, e.surface, surface_);
                wl_subsurface_place_below(e.subsurface, surface_);
            }
            auto [x, y, w, h] = places[i];
            wl_subsurface_set_position(e.subsurface, x, y);
            if (e.shown && x == e.x && y == e.y && w == e.width && h == e.height && e.activated == state_.activated) continue;
            e.x = x, e.y = y, e.width = w, e.height = h, e.activated = state_.activated;
            e.shown = drawEdge(e);
        }
        xdg_surface_set_window_geometry(xdgSurface_, 0, 0, width_, height_);
    }

    // An edge's shadow into a buffer of its own, shown on it: a pixel a point (a blur scaled up
    // looks the same), black fading out from the window, stronger while it is the active one.
    bool drawEdge(Edge& e) {
        int stride = e.width * 4;
        size_t size = size_t(stride) * size_t(e.height);
        int fd = memfd_create("kompoz-edge", MFD_CLOEXEC);
        if (fd < 0) return false;
        void* map = ftruncate(fd, off_t(size)) == 0 ? mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0) : MAP_FAILED;
        if (map == MAP_FAILED) {
            close(fd);
            return false;
        }
        auto* pixels = static_cast<uint32_t*>(map);
        double strength = state_.activated ? 0.32 : 0.16;
        for (int j = 0; j < e.height; ++j)
            for (int i = 0; i < e.width; ++i) {
                double x = e.x + i + 0.5, y = e.y + j + 0.5;
                double t = std::min(1.0, std::hypot(std::max({0.0, -x, x - width_}), std::max({0.0, -y, y - height_})) / kEdgeSize);
                pixels[size_t(j) * size_t(e.width) + size_t(i)] = uint32_t(std::lround(255 * strength * (1 - t) * (1 - t))) << 24;  // (ARGB, premultiplied)
            }
        munmap(map, size);
        wl_shm_pool* pool = wl_shm_create_pool(shm_, fd, int32_t(size));
        wl_buffer* buffer = wl_shm_pool_create_buffer(pool, 0, e.width, e.height, stride, WL_SHM_FORMAT_ARGB8888);
        wl_shm_pool_destroy(pool);
        close(fd);
        if (e.buffer) e.buffer->retired = true;
        e.buffer = &shmBuffers_.emplace_back(ShmBuffer{.buffer = buffer});
        wl_buffer_add_listener(buffer, &kBuffer, e.buffer);
        wl_surface_attach(e.surface, buffer, 0, 0);
        wl_surface_damage(e.surface, 0, 0, e.width, e.height);
        wl_surface_commit(e.surface);  // (applied with the window's next commit: a subsurface's state waits for its parent's)
        return true;
    }

    // ---- The clipboard ---------------------------------------------------------------------

    // The first of `types` an offer has, or null.
    const char* offered(wl_data_offer* offer, std::initializer_list<const char*> types) {
        const std::vector<std::string>& has = offerTypes_[offer];
        for (const char* type : types)
            if (std::find(has.begin(), has.end(), type) != has.end()) return type;
        return nullptr;
    }

    std::string readClipboard() {
        if (source_) return clipboard_;  // (ours: we would have to answer ourselves)
        if (!selectionOffer_) return {};
        const char* type = offered(selectionOffer_, {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING", "STRING", "TEXT"});
        return type ? receive(selectionOffer_, type) : std::string();
    }

    // What an offer holds as `type`, read through a pipe from the app that offers it (which may
    // take its time: a second at most between reads).
    std::string receive(wl_data_offer* offer, const char* type) {
        int fds[2];
        if (pipe2(fds, O_CLOEXEC) != 0) return {};
        wl_data_offer_receive(offer, type, fds[1]);
        close(fds[1]);
        wl_display_flush(display_);
        std::string text;
        char buffer[4096];
        for (;;) {
            pollfd p{fds[0], POLLIN, 0};
            if (poll(&p, 1, 1000) <= 0) break;  // (an app that doesn't answer)
            ssize_t n = read(fds[0], buffer, sizeof buffer);
            if (n <= 0) break;
            text.append(buffer, size_t(n));
        }
        close(fds[0]);
        return text;
    }

    void writeClipboard(const std::string& text) {
        if (!dataDevices_ || !dataDevice_) return;
        clipboard_ = text;
        if (source_) wl_data_source_destroy(source_);
        source_ = wl_data_device_manager_create_data_source(dataDevices_);
        wl_data_source_add_listener(source_, &kSource, this);
        for (const char* type : {"text/plain;charset=utf-8", "text/plain", "UTF8_STRING"}) wl_data_source_offer(source_, type);
        wl_data_device_set_selection(dataDevice_, source_, lastSerial_);
    }

    // The browser, or the app for the scheme (xdg-open), its process waited for on a thread.
    void open(const std::string& uri) {
        pid_t pid = 0;
        const char* argv[] = {"xdg-open", uri.c_str(), nullptr};
        if (posix_spawnp(&pid, "xdg-open", nullptr, nullptr, const_cast<char* const*>(argv), environ) != 0) {
            log("kompoz: can't open " + uri);
            return;
        }
        std::thread([pid] { waitpid(pid, nullptr, 0); }).detach();
    }

    // ---- OpenGL ES -------------------------------------------------------------------------

    bool openGles(gpu::Surface& surface, std::string* error) {
        if (!wayland::fn.egl || !gEgl.load()) {
            *error = "no EGL or libwayland-egl";
            return false;
        }
        eglDisplay_ = gEgl.getPlatformDisplay(EGL_PLATFORM_WAYLAND_KHR, display_, nullptr);
        if (eglDisplay_ == EGL_NO_DISPLAY || !gEgl.initialize(eglDisplay_, nullptr, nullptr)) {
            eglDisplay_ = EGL_NO_DISPLAY;
            *error = "no EGL display";
            return false;
        }
        const EGLint attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT, EGL_SURFACE_TYPE, EGL_WINDOW_BIT, EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8,
                                     EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_NONE};
        EGLConfig config = nullptr;
        EGLint count = 0;
        if (!gEgl.chooseConfig(eglDisplay_, attributes, &config, 1, &count) || count < 1) {
            *error = "no OpenGL ES 3 configuration";
            return false;
        }
        gEgl.bindApi(EGL_OPENGL_ES_API);
        bool debug = option("KOMPOZ_GPU_VALIDATION") == "1";
        const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION, 3, EGL_CONTEXT_MINOR_VERSION, 0,
                                            EGL_CONTEXT_OPENGL_DEBUG, debug ? EGL_TRUE : EGL_FALSE, EGL_NONE};
        eglContext_ = gEgl.createContext(eglDisplay_, config, EGL_NO_CONTEXT, contextAttributes);
        eglWindow_ = eglContext_ != EGL_NO_CONTEXT ? wl_egl_window_create(surface_, pixelWidth(), pixelHeight()) : nullptr;
        eglSurface_ = eglWindow_ ? gEgl.createPlatformWindowSurface(eglDisplay_, config, eglWindow_, nullptr) : EGL_NO_SURFACE;
        if (eglSurface_ == EGL_NO_SURFACE || !gEgl.makeCurrent(eglDisplay_, eglSurface_, eglSurface_, eglContext_)) {
            *error = "no OpenGL ES 3 context on the window";
            return false;
        }
        gEgl.swapInterval(eglDisplay_, 0);  // (the frames are paced by the compositor's frame callbacks)
        if (!gGles) gGles = dlopen("libGLESv2.so.2", RTLD_NOW | RTLD_LOCAL);
        surface.glGetProcAddress = [](const char* name) -> void* {
            if (void* f = gGles ? dlsym(gGles, name) : nullptr) return f;
            return reinterpret_cast<void*>(gEgl.getProcAddress(name));
        };
        surface.glSwap = [this] { gEgl.swapBuffers(eglDisplay_, eglSurface_); };
        return true;
    }

    // ---- Listeners -------------------------------------------------------------------------

    static WaylandPlatform& self(void* data) { return *static_cast<WaylandPlatform*>(data); }
    template <class T> static T* bind(wl_registry* registry, uint32_t name, const wl_interface* interface, uint32_t version, uint32_t most) {
        return static_cast<T*>(wl_registry_bind(registry, name, interface, std::min(version, most)));
    }

    // The seat's objects that need a manager as well (whichever came first).
    void attachSeat() {
        if (seat_ && dataDevices_ && !dataDevice_) {
            dataDevice_ = wl_data_device_manager_get_data_device(dataDevices_, seat_);
            wl_data_device_add_listener(dataDevice_, &kDataDevice, this);
        }
        if (seat_ && textInputs_ && !textInput_) {
            textInput_ = zwp_text_input_manager_v3_get_text_input(textInputs_, seat_);
            zwp_text_input_v3_add_listener(textInput_, &kTextInput, this);
        }
        if (pointer_ && cursorShapes_ && !cursorDevice_) cursorDevice_ = wp_cursor_shape_manager_v1_get_pointer(cursorShapes_, pointer_);
        if (pointer_ && gestures_ && !pinch_) {
            pinch_ = zwp_pointer_gestures_v1_get_pinch_gesture(gestures_, pointer_);
            zwp_pointer_gesture_pinch_v1_add_listener(pinch_, &kPinch, this);
        }
    }

    // Without fractional-scale or the surface's preferred scale: the largest scale of the
    // outputs it is on.
    void outputScaleChanged() {
        if (scale120_ > 0 || (surface_ && wl_surface_get_version(surface_) >= 6)) return;
        int best = 1;
        for (const Output& o : outputs_)
            if (o.entered) best = std::max(best, int(o.scale));
        if (best != bufferScale_) {
            bufferScale_ = best;
            scaleChanged();
        }
    }

    void forget(wl_data_offer* offer) {
        offerTypes_.erase(offer);
        if (offer == selectionOffer_) selectionOffer_ = nullptr;
        if (offer == dragOffer_) dragOffer_ = nullptr;
        wl_data_offer_destroy(offer);
    }

    // The local files a text/uri-list names (file:// addresses, percent-decoded; comments and other
    // schemes left out).
    static std::vector<std::string> filesIn(const std::string& list) {
        std::vector<std::string> files;
        size_t start = 0;
        while (start < list.size()) {
            size_t end = list.find('\n', start);
            std::string line = list.substr(start, end == std::string::npos ? std::string::npos : end - start);
            start = end == std::string::npos ? list.size() : end + 1;
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (!line.starts_with("file://")) continue;
            line.erase(0, 7);
            if (size_t slash = line.find('/'); slash != 0) line.erase(0, slash);  // (a host before the path)
            std::string path;
            for (size_t i = 0; i < line.size(); ++i) {
                if (line[i] == '%' && i + 2 < line.size() && std::isxdigit(uint8_t(line[i + 1])) && std::isxdigit(uint8_t(line[i + 2]))) {
                    path += char(std::stoi(line.substr(i + 1, 2), nullptr, 16));
                    i += 2;
                } else {
                    path += line[i];
                }
            }
            if (!path.empty()) files.push_back(path);
        }
        return files;
    }

    // ---- Listeners (defined after the class, where its members are complete) ---------------

    static const xdg_wm_base_listener kWmBase;
    static const wl_output_listener kOutput;
    static const wl_surface_listener kSurface;
    static const wp_fractional_scale_v1_listener kFractional;
    static const xdg_surface_listener kXdgSurface;
    static const xdg_toplevel_listener kToplevel;
    static const zxdg_toplevel_decoration_v1_listener kDecoration;
    static const wl_buffer_listener kBuffer;
    static const wl_callback_listener kFrameCallback;
    static const wl_pointer_listener kPointer;
    static const wl_keyboard_listener kKeyboard;
    static const wl_touch_listener kTouch;
    static const zwp_text_input_v3_listener kTextInput;
    static const wl_data_offer_listener kOffer;
    static const wl_data_device_listener kDataDevice;
    static const wl_data_source_listener kSource;
    static const zwp_pointer_gesture_pinch_v1_listener kPinch;
    static const wl_seat_listener kSeat;
    static const wl_registry_listener kRegistry;
};

const xdg_wm_base_listener WaylandPlatform::kWmBase = {
    .ping = [](void*, xdg_wm_base* base, uint32_t serial) { xdg_wm_base_pong(base, serial); },
};

const wl_output_listener WaylandPlatform::kOutput = {
    .geometry = [](void*, wl_output*, int32_t, int32_t, int32_t, int32_t, int32_t, const char*, const char*, int32_t) {},
    .mode = [](void* data, wl_output* output, uint32_t flags, int32_t, int32_t, int32_t refresh) {
        if (!(flags & WL_OUTPUT_MODE_CURRENT)) return;
        for (Output& o : self(data).outputs_)
            if (o.output == output) o.refreshMhz = refresh;
    },
    .done = [](void*, wl_output*) {},
    .scale = [](void* data, wl_output* output, int32_t scale) {
        WaylandPlatform& p = self(data);
        for (Output& o : p.outputs_)
            if (o.output == output) o.scale = scale;
        p.outputScaleChanged();
    },
};

const wl_surface_listener WaylandPlatform::kSurface = {
    .enter = [](void* data, wl_surface*, wl_output* output) {
        WaylandPlatform& p = self(data);
        for (Output& o : p.outputs_)
            if (o.output == output) o.entered = true;
        p.outputScaleChanged();
        p.events_.push_back({.type = EventType::MetricsChanged});  // (its refresh rate)
    },
    .leave = [](void* data, wl_surface*, wl_output* output) {
        WaylandPlatform& p = self(data);
        for (Output& o : p.outputs_)
            if (o.output == output) o.entered = false;
        p.outputScaleChanged();
    },
    .preferred_buffer_scale = [](void* data, wl_surface*, int32_t scale) {
        WaylandPlatform& p = self(data);
        if (scale != p.bufferScale_) {
            p.bufferScale_ = scale;
            if (p.scale120_ == 0) p.scaleChanged();
        }
    },
    .preferred_buffer_transform = [](void*, wl_surface*, uint32_t) {},
};

const wp_fractional_scale_v1_listener WaylandPlatform::kFractional = {
    .preferred_scale = [](void* data, wp_fractional_scale_v1*, uint32_t scale) {
        WaylandPlatform& p = self(data);
        if (int(scale) != p.scale120_) {
            p.scale120_ = int(scale);
            p.scaleChanged();
        }
    },
};

const xdg_surface_listener WaylandPlatform::kXdgSurface = {
    .configure = [](void* data, xdg_surface* surface, uint32_t serial) {
        WaylandPlatform& p = self(data);
        xdg_surface_ack_configure(surface, serial);
        bool resized = false;
        if (p.pendingWidth_ > 0 && p.pendingHeight_ > 0 && (p.pendingWidth_ != p.width_ || p.pendingHeight_ != p.height_)) {
            p.width_ = p.pendingWidth_, p.height_ = p.pendingHeight_;
            resized = true;
        }
        bool stateChanged = p.pendingState_ != p.state_;
        p.state_ = p.pendingState_;
        if (resized && p.configured_) {
            p.applySize();
            p.events_.push_back({.type = EventType::Resized});
        }
        if ((resized || stateChanged) && p.configured_) p.updateEdges();
        if (stateChanged && p.configured_) p.events_.push_back({.type = EventType::MetricsChanged});  // (the title bar's)
        if (!p.configured_) {
            p.configured_ = true;
            p.events_.push_back({.type = EventType::Shown});
            p.events_.push_back({.type = EventType::Resized});
        }
    },
};

const xdg_toplevel_listener WaylandPlatform::kToplevel = {
    .configure = [](void* data, xdg_toplevel*, int32_t width, int32_t height, wl_array* states) {
        WaylandPlatform& p = self(data);
        p.pendingWidth_ = width, p.pendingHeight_ = height;  // (0: the app's own size)
        WindowState state;
        const auto* told = static_cast<const uint32_t*>(states->data);
        for (size_t i = 0; i < states->size / sizeof(uint32_t); ++i) switch (told[i]) {
            case XDG_TOPLEVEL_STATE_MAXIMIZED: state.maximized = true; break;
            case XDG_TOPLEVEL_STATE_FULLSCREEN: state.fullscreen = true; break;
            case XDG_TOPLEVEL_STATE_ACTIVATED: state.activated = true; break;
            case XDG_TOPLEVEL_STATE_TILED_TOP: state.tiled[kTop] = true; break;
            case XDG_TOPLEVEL_STATE_TILED_BOTTOM: state.tiled[kBottom] = true; break;
            case XDG_TOPLEVEL_STATE_TILED_LEFT: state.tiled[kLeft] = true; break;
            case XDG_TOPLEVEL_STATE_TILED_RIGHT: state.tiled[kRight] = true; break;
            default: break;
            }
        p.pendingState_ = state;
    },
    .close = [](void* data, xdg_toplevel*) { self(data).events_.push_back({.type = EventType::Quit}); },
    .configure_bounds = [](void*, xdg_toplevel*, int32_t, int32_t) {},
    .wm_capabilities = [](void* data, xdg_toplevel*, wl_array* capabilities) {
        WaylandPlatform& p = self(data);
        p.canMinimize_ = p.canMaximize_ = false;
        const auto* told = static_cast<const uint32_t*>(capabilities->data);
        for (size_t i = 0; i < capabilities->size / sizeof(uint32_t); ++i) {
            if (told[i] == XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE) p.canMinimize_ = true;
            if (told[i] == XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE) p.canMaximize_ = true;
        }
        p.events_.push_back({.type = EventType::MetricsChanged});
    },
};

// Whether the compositor decorates the window, or Kompoz.
const zxdg_toplevel_decoration_v1_listener WaylandPlatform::kDecoration = {
    .configure = [](void* data, zxdg_toplevel_decoration_v1*, uint32_t mode) {
        WaylandPlatform& p = self(data);
        bool own = mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE;
        if (own == p.ownDecorations_) return;
        p.ownDecorations_ = own;
        if (!p.configured_) return;
        p.updateEdges();
        p.events_.push_back({.type = EventType::MetricsChanged});
    },
};

const wl_buffer_listener WaylandPlatform::kBuffer = {
    .release = [](void* data, wl_buffer*) { static_cast<ShmBuffer*>(data)->busy = false; },
};

const wl_callback_listener WaylandPlatform::kFrameCallback = {
    .done = [](void* data, wl_callback* callback, uint32_t) {
        WaylandPlatform& p = self(data);
        wl_callback_destroy(callback);
        p.frameCallback_ = nullptr;
        p.awaitingCallback_ = false;
        p.callbackDone_ = true;
    },
};

const wl_pointer_listener WaylandPlatform::kPointer = {
    .enter = [](void* data, wl_pointer*, uint32_t serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y) {
        WaylandPlatform& p = self(data);
        p.pointerSerial_ = serial;
        p.pointerOn_ = surface;
        if (int edge = p.edgeOf(surface); edge >= 0) {
            p.edgeCursor_ = 0;
            p.overEdge(edge, wl_fixed_to_double(x), wl_fixed_to_double(y));
            return;
        }
        p.pointerX_ = wl_fixed_to_double(x), p.pointerY_ = wl_fixed_to_double(y);
        p.showCursor();
        p.events_.push_back(p.at(EventType::MouseMove, p.pointerX_, p.pointerY_));
    },
    .leave = [](void* data, wl_pointer*, uint32_t, wl_surface* surface) {
        WaylandPlatform& p = self(data);
        p.pointerOn_ = nullptr;
        if (p.edgeOf(surface) < 0) p.events_.push_back({.type = EventType::PointerLeft});
    },
    .motion = [](void* data, wl_pointer*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
        WaylandPlatform& p = self(data);
        if (int edge = p.edgeOf(p.pointerOn_); edge >= 0) return p.overEdge(edge, wl_fixed_to_double(x), wl_fixed_to_double(y));
        p.pointerX_ = wl_fixed_to_double(x), p.pointerY_ = wl_fixed_to_double(y);
        p.events_.push_back(p.at(EventType::MouseMove, p.pointerX_, p.pointerY_));
    },
    .button = [](void* data, wl_pointer*, uint32_t serial, uint32_t, uint32_t button, uint32_t state) {
        WaylandPlatform& p = self(data);
        p.lastSerial_ = serial;
        if (state == WL_POINTER_BUTTON_STATE_PRESSED) p.pressSerial_ = serial, p.pressTouch_ = -1;
        if (p.edgeOf(p.pointerOn_) >= 0) {  // (an edge: the left button resizes the window by it)
            if (state == WL_POINTER_BUTTON_STATE_PRESSED && button == 0x110 && p.seat_)
                xdg_toplevel_resize(p.toplevel_, p.seat_, serial, p.resizeEdgeAt(p.edgeX_, p.edgeY_));
            return;
        }
        Event e = p.at(state == WL_POINTER_BUTTON_STATE_PRESSED ? EventType::MouseDown : EventType::MouseUp, p.pointerX_, p.pointerY_);
        switch (button) {  // (linux/input-event-codes.h's BTN_LEFT...)
        case 0x110: e.button = MouseButton::Left; break;
        case 0x111: e.button = MouseButton::Right; break;
        case 0x112: e.button = MouseButton::Middle; break;
        case 0x113: e.button = MouseButton::Back; break;
        case 0x114: e.button = MouseButton::Forward; break;
        default: return;
        }
        p.events_.push_back(e);
    },
    // The wheel as SDL tells it: a notch is 1 (value120 / 120), a touchpad's scroll a tenth of
    // its surface units; up and right positive. Gathered until the pointer's frame.
    .axis = [](void* data, wl_pointer*, uint32_t, uint32_t axis, wl_fixed_t value) {
        WaylandPlatform& p = self(data);
        if (p.wheelDiscrete_) return;
        double v = wl_fixed_to_double(value) / 10;
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) p.wheelY_ -= v;
        else p.wheelX_ += v;
    },
    .frame = [](void* data, wl_pointer*) {
        WaylandPlatform& p = self(data);
        if ((p.wheelX_ != 0 || p.wheelY_ != 0) && p.edgeOf(p.pointerOn_) < 0) {
            Event e = p.at(EventType::Wheel, p.pointerX_, p.pointerY_);
            e.dx = float(p.wheelX_), e.dy = float(p.wheelY_);
            p.events_.push_back(e);
        }
        p.wheelX_ = p.wheelY_ = 0;
        p.wheelDiscrete_ = false;
    },
    .axis_source = [](void*, wl_pointer*, uint32_t) {},
    .axis_stop = [](void*, wl_pointer*, uint32_t, uint32_t) {},
    .axis_discrete = [](void*, wl_pointer*, uint32_t, int32_t) {},
    .axis_value120 = [](void* data, wl_pointer*, uint32_t axis, int32_t value) {
        WaylandPlatform& p = self(data);
        if (!p.wheelDiscrete_) p.wheelX_ = p.wheelY_ = 0;  // (the notches, not the axis' distance)
        p.wheelDiscrete_ = true;
        if (axis == WL_POINTER_AXIS_VERTICAL_SCROLL) p.wheelY_ -= value / 120.0;
        else p.wheelX_ += value / 120.0;
    },
};

const wl_keyboard_listener WaylandPlatform::kKeyboard = {
    .keymap = [](void* data, wl_keyboard*, uint32_t format, int32_t fd, uint32_t size) {
        WaylandPlatform& p = self(data);
        if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
            close(fd);
            return;
        }
        void* map = mmap(nullptr, size, PROT_READ, MAP_PRIVATE, fd, 0);
        close(fd);
        if (map == MAP_FAILED) return;
        xkb_keymap* keymap = xkb_keymap_new_from_string(p.xkb_, static_cast<const char*>(map), XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
        munmap(map, size);
        if (!keymap) return;
        if (p.xkbState_) xkb_state_unref(p.xkbState_);
        if (p.keymap_) xkb_keymap_unref(p.keymap_);
        p.keymap_ = keymap;
        p.xkbState_ = xkb_state_new(keymap);
    },
    .enter = [](void* data, wl_keyboard*, uint32_t serial, wl_surface*, wl_array*) {
        WaylandPlatform& p = self(data);
        p.lastSerial_ = serial;
        p.events_.push_back({.type = EventType::FocusGained});
    },
    .leave = [](void* data, wl_keyboard*, uint32_t, wl_surface*) {
        WaylandPlatform& p = self(data);
        p.stopRepeat();
        p.modifiers_ = {};
        p.events_.push_back({.type = EventType::FocusLost});
    },
    .key = [](void* data, wl_keyboard*, uint32_t serial, uint32_t, uint32_t key, uint32_t state) {
        WaylandPlatform& p = self(data);
        p.lastSerial_ = serial;
        uint32_t code = key + 8;  // (evdev's codes are 8 below X's, which xkbcommon takes)
        bool down = state == WL_KEYBOARD_KEY_STATE_PRESSED;
        p.key(code, down);
        if (down) p.startRepeat(code);
        else if (code == p.repeatKey_) p.stopRepeat();
    },
    .modifiers = [](void* data, wl_keyboard*, uint32_t, uint32_t depressed, uint32_t latched, uint32_t locked, uint32_t group) {
        WaylandPlatform& p = self(data);
        if (!p.xkbState_) return;
        xkb_state_update_mask(p.xkbState_, depressed, latched, locked, 0, 0, group);
        auto on = [&](const char* name) { return xkb_state_mod_name_is_active(p.xkbState_, name, XKB_STATE_MODS_EFFECTIVE) > 0; };
        p.modifiers_ = KeyModifiers{on(XKB_MOD_NAME_SHIFT), on(XKB_MOD_NAME_CTRL) || on(XKB_MOD_NAME_LOGO), on(XKB_MOD_NAME_ALT)};
    },
    .repeat_info = [](void* data, wl_keyboard*, int32_t rate, int32_t delay) {
        WaylandPlatform& p = self(data);
        p.repeatRate_ = rate, p.repeatDelay_ = delay;
    },
};

const wl_touch_listener WaylandPlatform::kTouch = {
    .down = [](void* data, wl_touch*, uint32_t serial, uint32_t, wl_surface* surface, int32_t id, wl_fixed_t x, wl_fixed_t y) {
        WaylandPlatform& p = self(data);
        p.lastSerial_ = serial;
        if (int edge = p.edgeOf(surface); edge >= 0) {  // (an edge: the finger resizes the window by it)
            p.edgeTouches_.insert(id);
            if (p.seat_) {
                const Edge& e = p.edges_[size_t(edge)];
                xdg_toplevel_resize(p.toplevel_, p.seat_, serial, p.resizeEdgeAt(e.x + wl_fixed_to_double(x), e.y + wl_fixed_to_double(y)));
            }
            return;
        }
        p.pressSerial_ = serial, p.pressTouch_ = id;
        p.touches_[id] = {wl_fixed_to_double(x), wl_fixed_to_double(y)};
        Event e = p.at(EventType::TouchDown, wl_fixed_to_double(x), wl_fixed_to_double(y));
        e.pointer = id;
        p.events_.push_back(e);
    },
    .up = [](void* data, wl_touch*, uint32_t, uint32_t, int32_t id) {
        WaylandPlatform& p = self(data);
        auto touch = p.touches_.find(id);
        if (p.edgeTouches_.erase(id) || touch == p.touches_.end()) return;  // (an edge's, or one let go for a move)
        auto [x, y] = touch->second;
        p.touches_.erase(touch);
        Event e = p.at(EventType::TouchUp, x, y);
        e.pointer = id;
        p.events_.push_back(e);
    },
    .motion = [](void* data, wl_touch*, uint32_t, int32_t id, wl_fixed_t x, wl_fixed_t y) {
        WaylandPlatform& p = self(data);
        if (!p.touches_.contains(id)) return;
        p.touches_[id] = {wl_fixed_to_double(x), wl_fixed_to_double(y)};
        Event e = p.at(EventType::TouchMove, wl_fixed_to_double(x), wl_fixed_to_double(y));
        e.pointer = id;
        p.events_.push_back(e);
    },
    .frame = [](void*, wl_touch*) {},
    .cancel = [](void* data, wl_touch*) {
        WaylandPlatform& p = self(data);
        for (auto& [id, xy] : p.touches_) {
            Event e = p.at(EventType::TouchUp, xy.first, xy.second);
            e.pointer = id;
            p.events_.push_back(e);
        }
        p.touches_.clear();
        p.edgeTouches_.clear();
    },
    .shape = [](void*, wl_touch*, int32_t, wl_fixed_t, wl_fixed_t) {},
    .orientation = [](void*, wl_touch*, int32_t, wl_fixed_t) {},
};

// An input method's typing (text-input-v3): the text it commits, at its done.
const zwp_text_input_v3_listener WaylandPlatform::kTextInput = {
    .enter = [](void* data, zwp_text_input_v3*, wl_surface*) {
        WaylandPlatform& p = self(data);
        p.textEntered_ = true;
        p.tellTextInput();
    },
    .leave = [](void* data, zwp_text_input_v3* input, wl_surface*) {
        WaylandPlatform& p = self(data);
        p.textEntered_ = false;
        zwp_text_input_v3_disable(input);
        zwp_text_input_v3_commit(input);
    },
    .preedit_string = [](void*, zwp_text_input_v3*, const char*, int32_t, int32_t) {},
    .commit_string = [](void* data, zwp_text_input_v3*, const char* text) { self(data).pendingCommit_ = text ? text : ""; },
    .delete_surrounding_text = [](void*, zwp_text_input_v3*, uint32_t, uint32_t) {},
    .done = [](void* data, zwp_text_input_v3*, uint32_t) {
        WaylandPlatform& p = self(data);
        if (!p.pendingCommit_.empty() && p.textActive_) p.events_.push_back({.type = EventType::Text, .text = std::exchange(p.pendingCommit_, {})});
        p.pendingCommit_.clear();
    },
};

const wl_data_offer_listener WaylandPlatform::kOffer = {
    .offer = [](void* data, wl_data_offer* offer, const char* type) { self(data).offerTypes_[offer].push_back(type); },
    .source_actions = [](void*, wl_data_offer*, uint32_t) {},
    .action = [](void*, wl_data_offer*, uint32_t) {},
};

const wl_data_device_listener WaylandPlatform::kDataDevice = {
    .data_offer = [](void* data, wl_data_device*, wl_data_offer* offer) { wl_data_offer_add_listener(offer, &kOffer, data); },
    // Files from a file manager, text from a browser: taken as a copy, told to the core as SDL
    // told them (begin, moves, each file or the text at the drop, complete; complete alone: it left).
    .enter = [](void* data, wl_data_device*, uint32_t serial, wl_surface* surface, wl_fixed_t x, wl_fixed_t y, wl_data_offer* offer) {
        WaylandPlatform& p = self(data);
        if (p.dragOffer_) p.forget(p.dragOffer_);
        if (offer && p.edgeOf(surface) >= 0) {  // (over an edge: not the window's to take)
            p.forget(offer);
            offer = nullptr;
        }
        p.dragOffer_ = offer;
        if (!offer) return;
        p.dragX_ = wl_fixed_to_double(x), p.dragY_ = wl_fixed_to_double(y);
        const char* type = p.offered(offer, {"text/uri-list", "text/plain;charset=utf-8", "text/plain"});
        wl_data_offer_accept(offer, serial, type);
        if (wl_data_offer_get_version(offer) >= 3)
            wl_data_offer_set_actions(offer, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY, WL_DATA_DEVICE_MANAGER_DND_ACTION_COPY);
        p.events_.push_back({.type = EventType::DropBegin});
        p.events_.push_back(p.at(EventType::DropMove, p.dragX_, p.dragY_));
    },
    .leave = [](void* data, wl_data_device*) {
        WaylandPlatform& p = self(data);
        if (!p.dragOffer_) return;
        p.forget(p.dragOffer_);
        p.events_.push_back({.type = EventType::DropComplete});
    },
    .motion = [](void* data, wl_data_device*, uint32_t, wl_fixed_t x, wl_fixed_t y) {
        WaylandPlatform& p = self(data);
        if (!p.dragOffer_) return;
        p.dragX_ = wl_fixed_to_double(x), p.dragY_ = wl_fixed_to_double(y);
        p.events_.push_back(p.at(EventType::DropMove, p.dragX_, p.dragY_));
    },
    .drop = [](void* data, wl_data_device*) {
        WaylandPlatform& p = self(data);
        wl_data_offer* offer = p.dragOffer_;
        if (!offer) return;
        if (p.offered(offer, {"text/uri-list"})) {
            for (const std::string& path : filesIn(p.receive(offer, "text/uri-list"))) {
                Event e = p.at(EventType::DropFile, p.dragX_, p.dragY_);
                e.text = path;
                p.events_.push_back(e);
            }
        } else if (const char* type = p.offered(offer, {"text/plain;charset=utf-8", "text/plain"})) {
            Event e = p.at(EventType::DropText, p.dragX_, p.dragY_);
            e.text = p.receive(offer, type);
            p.events_.push_back(e);
        }
        if (wl_data_offer_get_version(offer) >= 3) wl_data_offer_finish(offer);
        p.forget(offer);
        p.events_.push_back({.type = EventType::DropComplete});
    },
    .selection = [](void* data, wl_data_device*, wl_data_offer* offer) {
        WaylandPlatform& p = self(data);
        if (p.selectionOffer_ && p.selectionOffer_ != offer) p.forget(p.selectionOffer_);
        p.selectionOffer_ = offer;
    },
};

// Ours on the clipboard: its text written to who asks.
const wl_data_source_listener WaylandPlatform::kSource = {
    .target = [](void*, wl_data_source*, const char*) {},
    .send = [](void* data, wl_data_source*, const char*, int32_t fd) {
        const std::string& text = self(data).clipboard_;
        for (size_t done = 0; done < text.size();) {
            ssize_t n = write(fd, text.data() + done, text.size() - done);
            if (n <= 0) break;
            done += size_t(n);
        }
        close(fd);
    },
    .cancelled = [](void* data, wl_data_source* source) {
        WaylandPlatform& p = self(data);
        if (p.source_ == source) p.source_ = nullptr;
        wl_data_source_destroy(source);
    },
    .dnd_drop_performed = [](void*, wl_data_source*) {},
    .dnd_finished = [](void*, wl_data_source*) {},
    .action = [](void*, wl_data_source*, uint32_t) {},
};

// A touchpad pinch (pointer-gestures), about the pointer: each step as the change since the
// last (Wayland tells the scale since the pinch began).
const zwp_pointer_gesture_pinch_v1_listener WaylandPlatform::kPinch = {
    .begin = [](void* data, zwp_pointer_gesture_pinch_v1*, uint32_t, uint32_t, wl_surface*, uint32_t) {
        WaylandPlatform& p = self(data);
        p.pinchScale_ = 1;
        p.events_.push_back(p.at(EventType::PinchBegin, p.pointerX_, p.pointerY_));
    },
    .update = [](void* data, zwp_pointer_gesture_pinch_v1*, uint32_t, wl_fixed_t, wl_fixed_t, wl_fixed_t scale, wl_fixed_t) {
        WaylandPlatform& p = self(data);
        double s = wl_fixed_to_double(scale);
        Event e = p.at(EventType::Pinch, p.pointerX_, p.pointerY_);
        e.scale = float(s / std::max(p.pinchScale_, 1e-3));
        p.pinchScale_ = s;
        p.events_.push_back(e);
    },
    .end = [](void*, zwp_pointer_gesture_pinch_v1*, uint32_t, uint32_t, int32_t) {},
};

const wl_seat_listener WaylandPlatform::kSeat = {
    .capabilities = [](void* data, wl_seat* seat, uint32_t caps) {
        WaylandPlatform& p = self(data);
        bool changed = false;
        if ((caps & WL_SEAT_CAPABILITY_POINTER) && !p.pointer_) {
            p.pointer_ = wl_seat_get_pointer(seat);
            wl_pointer_add_listener(p.pointer_, &kPointer, &p);
            changed = true;
        } else if (!(caps & WL_SEAT_CAPABILITY_POINTER) && p.pointer_) {
            if (p.cursorDevice_) wp_cursor_shape_device_v1_destroy(std::exchange(p.cursorDevice_, nullptr));
            if (p.pinch_) zwp_pointer_gesture_pinch_v1_destroy(std::exchange(p.pinch_, nullptr));
            wl_pointer_destroy(std::exchange(p.pointer_, nullptr));
            changed = true;
        }
        if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !p.keyboard_) {
            p.keyboard_ = wl_seat_get_keyboard(seat);
            wl_keyboard_add_listener(p.keyboard_, &kKeyboard, &p);
            changed = true;
        } else if (!(caps & WL_SEAT_CAPABILITY_KEYBOARD) && p.keyboard_) {
            wl_keyboard_destroy(std::exchange(p.keyboard_, nullptr));
            changed = true;
        }
        if ((caps & WL_SEAT_CAPABILITY_TOUCH) && !p.touch_) {
            p.touch_ = wl_seat_get_touch(seat);
            wl_touch_add_listener(p.touch_, &kTouch, &p);
            changed = true;
        } else if (!(caps & WL_SEAT_CAPABILITY_TOUCH) && p.touch_) {
            wl_touch_destroy(std::exchange(p.touch_, nullptr));
            changed = true;
        }
        p.attachSeat();
        if (changed) p.events_.push_back({.type = EventType::DevicesChanged});
    },
    .name = [](void*, wl_seat*, const char*) {},
};

const wl_registry_listener WaylandPlatform::kRegistry = {
    .global = [](void* data, wl_registry* registry, uint32_t name, const char* interface, uint32_t version) {
        WaylandPlatform& p = self(data);
        std::string_view i(interface);
        if (i == wl_compositor_interface.name) p.compositor_ = bind<wl_compositor>(registry, name, &wl_compositor_interface, version, 6);
        else if (i == xdg_wm_base_interface.name) {
            p.wmBase_ = bind<xdg_wm_base>(registry, name, &xdg_wm_base_interface, version, 6);
            xdg_wm_base_add_listener(p.wmBase_, &kWmBase, &p);
        } else if (i == zxdg_decoration_manager_v1_interface.name)
            p.decorations_ = bind<zxdg_decoration_manager_v1>(registry, name, &zxdg_decoration_manager_v1_interface, version, 1);
        else if (i == wp_fractional_scale_manager_v1_interface.name)
            p.fractionalManager_ = bind<wp_fractional_scale_manager_v1>(registry, name, &wp_fractional_scale_manager_v1_interface, version, 1);
        else if (i == wp_viewporter_interface.name) p.viewporter_ = bind<wp_viewporter>(registry, name, &wp_viewporter_interface, version, 1);
        else if (i == wp_cursor_shape_manager_v1_interface.name)
            p.cursorShapes_ = bind<wp_cursor_shape_manager_v1>(registry, name, &wp_cursor_shape_manager_v1_interface, version, 1);
        else if (i == zwp_text_input_manager_v3_interface.name)
            p.textInputs_ = bind<zwp_text_input_manager_v3>(registry, name, &zwp_text_input_manager_v3_interface, version, 1);
        else if (i == wl_data_device_manager_interface.name)
            p.dataDevices_ = bind<wl_data_device_manager>(registry, name, &wl_data_device_manager_interface, version, 3);
        else if (i == wl_shm_interface.name) p.shm_ = bind<wl_shm>(registry, name, &wl_shm_interface, version, 1);
        else if (i == wl_subcompositor_interface.name) p.subcompositor_ = bind<wl_subcompositor>(registry, name, &wl_subcompositor_interface, version, 1);
        else if (i == zwp_pointer_gestures_v1_interface.name)
            p.gestures_ = bind<zwp_pointer_gestures_v1>(registry, name, &zwp_pointer_gestures_v1_interface, version, 1);
        else if (i == zwp_idle_inhibit_manager_v1_interface.name)
            p.idleInhibits_ = bind<zwp_idle_inhibit_manager_v1>(registry, name, &zwp_idle_inhibit_manager_v1_interface, version, 1);
        else if (i == wl_seat_interface.name && !p.seat_) {
            p.seat_ = bind<wl_seat>(registry, name, &wl_seat_interface, version, 8);
            wl_seat_add_listener(p.seat_, &kSeat, &p);
        } else if (i == wl_output_interface.name) {
            Output o{.output = bind<wl_output>(registry, name, &wl_output_interface, version, 2), .name = name};
            p.outputs_.push_back(o);
            wl_output_add_listener(o.output, &kOutput, &p);
        }
        p.attachSeat();
    },
    .global_remove = [](void* data, wl_registry*, uint32_t name) {
        WaylandPlatform& p = self(data);
        std::erase_if(p.outputs_, [name](Output& o) {
            if (o.name != name) return false;
            wl_output_destroy(o.output);
            return true;
        });
    },
};

} // namespace

int runWayland(int argc, char* argv[]) {
    if (const char* forced = std::getenv("KOMPOZ_PLATFORM"); forced && std::string_view(forced) == "sdl") return -1;
    // libdbus (the accent color, the screen readers) kept loaded until what it keeps for the
    // process is freed at the end, its users all done, as SDL_Quit does.
    void* dbus = dlopen("libdbus-1.so.3", RTLD_NOW | RTLD_LOCAL);
    int result = -1;
    {
        auto platform = std::make_unique<WaylandPlatform>();
        if (platform->connect()) result = platform->run(argc, argv);
    }
    if (dbus) {
        if (auto shutdown = reinterpret_cast<void (*)()>(dlsym(dbus, "dbus_shutdown")); shutdown && result >= 0) shutdown();
        dlclose(dbus);
    }
    return result;
}

} // namespace kompoz::platform
