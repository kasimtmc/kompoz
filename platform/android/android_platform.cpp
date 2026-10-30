// Kompoz's platform on Android (app/platform.hpp): the activity (dev.kompoz.KompozActivity) and its
// view (KompozView) on the Java side, the app core on their UI thread, as Compose runs. Frames come
// from the display's Choreographer (the vsync's time as the frame's), touches and keys from the
// view's events as the framework hands them to it (resampled to the frame), the lifecycle from the
// activity; the GPU draws into the view's surface (a Vulkan surface or an EGL window surface on its
// ANativeWindow). Work from other threads wakes the UI thread's looper through an eventfd, a
// coroutine's delay through a timerfd.

#include "../app/app_core.hpp"
#include "../common/system.hpp"
#include "android_ime.hpp"
#include "android_performance.hpp"

#include <android/api-level.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>
#include <android/looper.h>
#include <android/native_window_jni.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <dlfcn.h>
#include <jni.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>
#ifdef KOMPOZ_GPU_VULKAN
#define VK_USE_PLATFORM_ANDROID_KHR 1
#include <vulkan/vulkan.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <ctime>
#include <fstream>
#include <iterator>
#include <memory>
#include <string>
#include <vector>

namespace kompoz::platform {
namespace {

JNIEnv* env() { return static_cast<JNIEnv*>(android_ime::jniEnv()); }

void check(JNIEnv* e) {
    if (e->ExceptionCheck()) {
        e->ExceptionDescribe();
        e->ExceptionClear();
    }
}

jbyteArray toBytes(JNIEnv* e, const std::string& s) {
    jbyteArray array = e->NewByteArray(jsize(s.size()));
    e->SetByteArrayRegion(array, 0, jsize(s.size()), reinterpret_cast<const jbyte*>(s.data()));
    return array;
}

std::string fromBytes(JNIEnv* e, jbyteArray array) {
    if (!array) return {};
    std::string s(size_t(e->GetArrayLength(array)), '\0');
    e->GetByteArrayRegion(array, 0, jsize(s.size()), reinterpret_cast<jbyte*>(s.data()));
    return s;
}

std::string fromString(JNIEnv* e, jstring string) {
    if (!string) return {};
    const char* chars = e->GetStringUTFChars(string, nullptr);
    std::string s = chars ? chars : "";
    if (chars) e->ReleaseStringUTFChars(string, chars);
    return s;
}

// Android's key codes (android.view.KeyEvent) as Kompoz's keys.
Key keyOf(int code) {
    if (code >= 29 && code <= 54) return Key(int(Key::A) + code - 29);      // KEYCODE_A..Z
    if (code >= 7 && code <= 16) return Key(int(Key::Zero) + code - 7);     // KEYCODE_0..9
    if (code >= 131 && code <= 142) return Key(int(Key::F1) + code - 131);  // KEYCODE_F1..F12
    switch (code) {
    case 66:   // ENTER
    case 160:  // NUMPAD_ENTER
        return Key::Enter;
    case 111: return Key::Escape;
    case 61: return Key::Tab;
    case 67: return Key::Backspace;  // DEL
    case 112: return Key::Delete;    // FORWARD_DEL
    case 62: return Key::Space;
    case 21: return Key::Left;  // DPAD_*
    case 22: return Key::Right;
    case 19: return Key::Up;
    case 20: return Key::Down;
    case 122: return Key::Home;  // MOVE_HOME
    case 123: return Key::End;   // MOVE_END
    case 92: return Key::PageUp;
    case 93: return Key::PageDown;
    default: return Key::Unknown;
    }
}

// KeyEvent's META_SHIFT_ON, META_ALT_ON, META_CTRL_ON and META_META_ON (Ctrl's on other systems: Cmd).
KeyModifiers modifiersOf(int meta) { return {(meta & 0x1) != 0, (meta & 0x11000) != 0, (meta & 0x2) != 0}; }

// The platform on Android, the app core on it.
class AndroidPlatform final : public Platform {
public:
    std::unique_ptr<AppCore> core;

    AndroidPlatform(JNIEnv* e, jobject activityObject, jobject assets, std::string files, std::string externalFiles, std::string cache)
        : filesDir_(std::move(files)), externalFilesDir_(std::move(externalFiles)) {
        activity_ = e->NewGlobalRef(activityObject);
        jclass activityClass = e->GetObjectClass(activity_);
        view_ = e->NewGlobalRef(e->GetObjectField(activity_, e->GetFieldID(activityClass, "view", "Ldev/kompoz/KompozView;")));
        jclass viewClass = e->GetObjectClass(view_);
        displayInfo_ = e->GetMethodID(activityClass, "displayInfo", "()[F");
        language_ = e->GetMethodID(activityClass, "language", "()Ljava/lang/String;");
        clipboard_ = e->GetMethodID(activityClass, "clipboard", "()[B");
        setClipboard_ = e->GetMethodID(activityClass, "setClipboard", "([B)V");
        openUri_ = e->GetMethodID(activityClass, "openUri", "([B)Z");
        keepScreenOnMethod_ = e->GetMethodID(activityClass, "keepScreenOn", "(Z)V");
        leave_ = e->GetMethodID(activityClass, "leave", "()V");
        finish_ = e->GetMethodID(activityClass, "finish", "()V");
        requestFrame_ = e->GetMethodID(viewClass, "requestFrame", "()V");
        setCursor_ = e->GetMethodID(viewClass, "setCursor", "(I)V");
        e->DeleteLocalRef(viewClass);
        e->DeleteLocalRef(activityClass);
        check(e);

        setLogSink([](const char* message) { __android_log_write(ANDROID_LOG_INFO, "kompoz", message); });
        // The APK's assets, where a path isn't a file; the app's cache directory.
        assets_ = e->NewGlobalRef(assets);
        AAssetManager* manager = AAssetManager_fromJava(e, assets_);
        setAssetReader([manager](const std::string& path) {
            std::vector<uint8_t> bytes;
            if (AAsset* asset = AAssetManager_open(manager, path.c_str(), AASSET_MODE_BUFFER)) {
                auto data = static_cast<const uint8_t*>(AAsset_getBuffer(asset));
                if (data) bytes.assign(data, data + AAsset_getLength(asset));
                AAsset_close(asset);
            }
            return bytes;
        });
        setCacheDirectory(cache);

        // Work from other threads (callbacks.wake) and a coroutine's delay wake the UI thread's looper.
        looper_ = ALooper_forThread();
        ALooper_acquire(looper_);
        wakeFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        timerFd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        for (int fd : {wakeFd_, timerFd_})
            ALooper_addFd(looper_, fd, ALOOPER_POLL_CALLBACK, ALOOPER_EVENT_INPUT, [](int fd, int, void* self) {
                uint64_t count = 0;
                (void)!read(fd, &count, sizeof count);
                static_cast<AndroidPlatform*>(self)->requestFrame();
                return 1;
            }, this);
    }

    ~AndroidPlatform() override {
        if (core) core->quit();
        core.reset();  // (the device goes before the window)
        android_ime::setHandlers(nullptr, nullptr, nullptr, nullptr);
        android_performance::stop();
        android_ime::setMediaHandler(nullptr);
        android_ime::setDeepLinkHandler(nullptr);
        android_ime::setBackHandler(nullptr);
        android_ime::setSemanticsActionHandler(nullptr);
        android_ime::setTextActionHandler(nullptr);
        closeWindow();
        if (window_) ANativeWindow_release(window_);
        for (int fd : {wakeFd_, timerFd_}) {
            ALooper_removeFd(looper_, fd);
            close(fd);
        }
        ALooper_release(looper_);
        setAssetReader(nullptr);  // (before the asset manager it reads goes)
        JNIEnv* e = env();
        for (jobject ref : {activity_, view_, assets_}) e->DeleteGlobalRef(ref);
    }

    // ---- What the Java side tells ------------------------------------------------------

    // The view's surface came, changed size or went (none). The app starts with the first: it
    // has a window to open then.
    void surface(JNIEnv* e, jobject surfaceObject, int width, int height) {
        ANativeWindow* now = surfaceObject ? ANativeWindow_fromSurface(e, surfaceObject) : nullptr;
        bool another = now != window_;
        if (!another && now) ANativeWindow_release(now);  // (the same, resized)
        width_ = now ? width : 0, height_ = now ? height : 0;
        if (another) {
            ANativeWindow* old = std::exchange(window_, now);
            windowChanged();
            if (old) ANativeWindow_release(old);
        }
        if (!core) {
            if (window_) startApp();
            return;
        }
        if (!window_) {
            deliver({.type = EventType::Hidden});
            return;
        }
        if (another) deliver({.type = EventType::Shown});  // (back: all of it drawn again)
        deliver({.type = EventType::Resized});
    }

    void lifecycle(int event) {
        // KompozActivity's EVENT_*.
        static const EventType types[] = {EventType::WillEnterBackground, EventType::DidEnterBackground, EventType::WillEnterForeground,
                                          EventType::DidEnterForeground, EventType::LowMemory, EventType::FocusGained,
                                          EventType::FocusLost, EventType::MetricsChanged, EventType::Back};
        if (event < 0 || event >= int(std::size(types))) return;
        EventType type = types[event];
        if (type == EventType::WillEnterForeground || type == EventType::DidEnterBackground) stopped_ = type == EventType::DidEnterBackground;
        if (type == EventType::DidEnterForeground || type == EventType::WillEnterBackground) resumed_ = type == EventType::DidEnterForeground;
        if (type == EventType::FocusGained || type == EventType::FocusLost) focused_ = type == EventType::FocusGained;
        deliver({.type = type});
    }

    // A MotionEvent: fingers (and a stylus) as touches; a mouse's buttons, moves and wheel.
    void motion(int action, int index, int count, const int* ids, const float* xy, int64_t time, int tool, int button, float scrollX,
                float scrollY, int meta) {
        constexpr int Down = 0, Up = 1, Move = 2, Cancel = 3, PointerDown = 5, PointerUp = 6, HoverMove = 7, Scroll = 8, HoverExit = 10,
                      ButtonPress = 11, ButtonRelease = 12, Mouse = 3;
        Event ev;
        ev.time = time;
        ev.modifiers = modifiersOf(meta);
        auto at = [&](int i) { ev.x = xy[i * 2], ev.y = xy[i * 2 + 1], ev.pointer = ids[i]; };
        if (tool == Mouse || action == HoverMove || action == HoverExit || action == Scroll || action == ButtonPress || action == ButtonRelease) {
            at(index);
            switch (action) {
            case Move:
            case HoverMove: ev.type = EventType::MouseMove; break;
            case HoverExit: ev.type = EventType::PointerLeft; break;
            case Scroll:
                ev.type = EventType::Wheel, ev.dx = scrollX, ev.dy = scrollY;
                break;
            case ButtonPress:
            case ButtonRelease:  // (MotionEvent's BUTTON_PRIMARY, SECONDARY, TERTIARY, BACK, FORWARD)
                switch (button) {
                case 1: ev.button = MouseButton::Left; break;
                case 2: ev.button = MouseButton::Right; break;
                case 4: ev.button = MouseButton::Middle; break;
                case 8: ev.button = MouseButton::Back; break;
                case 16: ev.button = MouseButton::Forward; break;
                default: return;
                }
                ev.type = action == ButtonPress ? EventType::MouseDown : EventType::MouseUp;
                break;
            default: return;  // (a button's down and up come as its press and release)
            }
            deliver(ev);
            return;
        }
        switch (action) {
        case Down:
        case PointerDown:
            at(index);
            ev.type = EventType::TouchDown;
            deliver(ev);
            break;
        case Up:
        case PointerUp:
            at(index);
            ev.type = EventType::TouchUp;
            deliver(ev);
            break;
        case Move:
            for (int i = 0; i < count; ++i) {
                at(i);
                ev.type = EventType::TouchMove;
                deliver(ev);
            }
            break;
        case Cancel:
            for (int i = 0; i < count; ++i) {
                at(i);
                ev.type = EventType::TouchUp;
                deliver(ev);
            }
            break;
        default: break;
        }
    }

    bool key(int code, bool down, int meta) {
        Event ev{.type = down ? EventType::KeyDown : EventType::KeyUp, .key = keyOf(code), .modifiers = modifiersOf(meta)};
        deliver(ev);  // (Unknown too: the keys it holds down, a Shift, still count)
        return ev.key != Key::Unknown;
    }

    // The display's beat: a frame, unless there is nothing to show it on.
    void frame(int64_t frameTime) {
        if (!core || !window_ || stopped_) return;
        Next next = core->frame(frameTime);
        if (next.quit) {
            callVoid(finish_);
            return;
        }
        // A wake due before the display's next beat (a coroutine's delay that ends with it, as
        // scrolling leaves them) is that beat's frame: a timer would make it the one after.
        bool soon = next.wakeAt >= 0 && next.wakeAt - now() < period_;
        if (next.busy || soon) requestFrame();
        armTimer(next.busy || soon ? -1 : next.wakeAt);
        // What is left until the next beat (less 2 ms for its input) is the host's for work between
        // frames: a lazy list's next items composed ahead, so that a frame scrolling them in doesn't.
        if (int64_t until = frameTime + period_ - 2'000'000, at = now(); until > at) core->idle(until - at);
    }

    // ---- Platform ------------------------------------------------------------------------

    // Android has no environment: a setting is a file of its name in lower case (KOMPOZ_PROFILE:
    // kompoz-profile) in the app's files, or where run-as is refused (SELinux) in its external
    // storage, which the app creates; what it holds.
    std::optional<std::string> option(std::string_view name) const override {
        std::string file(name);
        std::transform(file.begin(), file.end(), file.begin(), [](unsigned char c) { return c == '_' ? '-' : char(std::tolower(c)); });
        for (const std::string& dir : {filesDir_, externalFilesDir_}) {
            if (dir.empty()) continue;
            std::ifstream in(dir + "/" + file, std::ios::binary);
            if (in) return std::string(std::istreambuf_iterator<char>(in), {});
        }
        return std::nullopt;
    }
    void log(const std::string& message) const override { __android_log_write(ANDROID_LOG_INFO, "kompoz", message.c_str()); }
    int64_t now() const override {
        timespec t{};
        clock_gettime(CLOCK_MONOTONIC, &t);  // (System.nanoTime's: MotionEvent's and Choreographer's times)
        return int64_t(t.tv_sec) * 1'000'000'000 + t.tv_nsec;
    }

    // Vulkan from Android 10 (before which its drivers were often wrong), then OpenGL ES 3.
    std::vector<gpu::Backend> backends() const override {
        if (android_get_device_api_level() < 29) return {gpu::Backend::GLES};
        return {gpu::Backend::Vulkan, gpu::Backend::GLES};
    }

    bool openWindow(const Application&, gpu::Backend backend, gpu::Surface& surface, std::string* error) override {
        surface.drawableSize = [this](int* w, int* h) { *w = width_, *h = height_; };
        switch (backend) {
#ifdef KOMPOZ_GPU_VULKAN
        case gpu::Backend::Vulkan: {
            vulkan_ = dlopen("libvulkan.so", RTLD_NOW | RTLD_LOCAL);
            auto getProcAddress = vulkan_ ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(vulkan_, "vkGetInstanceProcAddr")) : nullptr;
            if (!getProcAddress) {
                *error = "no Vulkan loader";
                closeWindow();
                return false;
            }
            surface.vulkanGetInstanceProcAddr = reinterpret_cast<void* (*)(void*, const char*)>(getProcAddress);
            surface.vulkanInstanceExtensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_ANDROID_SURFACE_EXTENSION_NAME};
            surface.createVulkanSurface = [this, getProcAddress](void* instance, uint64_t* out) {
                auto create = reinterpret_cast<PFN_vkCreateAndroidSurfaceKHR>(getProcAddress(static_cast<VkInstance>(instance), "vkCreateAndroidSurfaceKHR"));
                VkAndroidSurfaceCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_ANDROID_SURFACE_CREATE_INFO_KHR, .window = window_};
                VkSurfaceKHR made = VK_NULL_HANDLE;
                if (!create || !window_ || create(static_cast<VkInstance>(instance), &info, nullptr, &made) != VK_SUCCESS) return false;
                std::memcpy(out, &made, sizeof made);
                return true;
            };
            surface.vulkanNativeWindow = [this] { return static_cast<void*>(window_); };  // (another after the background)
            return true;
        }
#endif
        case gpu::Backend::GLES: return openGles(surface, error);
        default: break;
        }
        *error = std::string("no ") + gpu::backendName(backend) + " here";
        return false;
    }

    void closeWindow() override {
        if (display_ != EGL_NO_DISPLAY) {
            eglMakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
            if (windowSurface_ != EGL_NO_SURFACE) eglDestroySurface(display_, windowSurface_);
            if (pbuffer_ != EGL_NO_SURFACE) eglDestroySurface(display_, pbuffer_);
            if (context_ != EGL_NO_CONTEXT) eglDestroyContext(display_, context_);
            eglTerminate(display_);
        }
        display_ = EGL_NO_DISPLAY, context_ = EGL_NO_CONTEXT, windowSurface_ = pbuffer_ = EGL_NO_SURFACE;
        if (vulkan_) dlclose(std::exchange(vulkan_, nullptr));
    }

    void showError(const std::string& message) override { log(message); }

    // Window pixels are its points; a dp is the screen's density of them.
    Metrics metrics() const override {
        Metrics m;
        m.pixelWidth = m.pointWidth = width_;
        m.pixelHeight = m.pointHeight = height_;
        JNIEnv* e = env();
        float info[3] = {1, 60, 0};
        if (auto array = static_cast<jfloatArray>(e->CallObjectMethod(activity_, displayInfo_))) {
            if (e->GetArrayLength(array) >= 3) e->GetFloatArrayRegion(array, 0, 3, info);
            e->DeleteLocalRef(array);
        }
        check(e);
        m.scale = info[0] > 0 ? info[0] : 1;
        m.pointToDp = 1 / m.scale;
        scale_ = m.scale;
        m.refreshRate = info[1] > 0 ? info[1] : 60;
        period_ = int64_t(1e9 / m.refreshRate);
        m.darkTheme = info[2] != 0;
        m.fontScale = android_ime::fontScale();  // Settings > Display > Font size
        auto language = static_cast<jstring>(e->CallObjectMethod(activity_, language_));
        m.language = fromString(e, language);
        if (language) e->DeleteLocalRef(language);
        check(e);
        android_performance::start(int64_t(1e9 / m.refreshRate));  // (the first time: a frame's work told to ADPF)
        android_performance::setTarget(int64_t(1e9 / m.refreshRate));
        return m;
    }
    void pixelSize(int* width, int* height) const override { *width = width_, *height = height_; }
    void setWindowSize(int, int) override {}  // (the system's)
    MediaInfo media(bool) override {
        if (auto told = android_ime::mediaInfo()) return *told;
        return {};
    }

    void metricsUpdated(ComposeHost& h) override {
        h.setDynamicTonalPalette(android_ime::dynamicPalette());
        h.setWallpaperTonalPalette(android_ime::wallpaperPalette());
        if (auto config = android_ime::viewConfiguration(scale_)) h.setViewConfiguration(*config);
    }

    void attach(ComposeHost& h, ComposeHost::Callbacks& callbacks) override {
        host_ = &h;
        AndroidPlatform* self = this;
        callbacks.wake = [fd = wakeFd_] {
            uint64_t one = 1;
            (void)!write(fd, &one, sizeof one);
        };
        // Kompoz's own keyboard bridge (KompozInput): the whole editing state each time.
        callbacks.textInput = [self](bool active, Rect area, const TextEditState& state, const ImeOptions& options) {
            if (!active) {
                android_ime::hide();
                return;
            }
            float k = self->scale_;  // dp -> window pixels
            android_ime::show(state, {area.x * k, area.y * k, area.width * k, area.height * k}, options);
        };
        callbacks.textInputChanged = [](const TextEditState& state) { android_ime::update(state); };
        callbacks.cursor = [self](Cursor cursor) { self->callVoid(self->setCursor_, jint(cursor), self->view_); };
        callbacks.getClipboard = [self] {
            JNIEnv* e = env();
            auto bytes = static_cast<jbyteArray>(e->CallObjectMethod(self->activity_, self->clipboard_));
            std::string text = fromBytes(e, bytes);
            if (bytes) e->DeleteLocalRef(bytes);
            check(e);
            return text;
        };
        callbacks.setClipboard = [self](const std::string& text) {
            JNIEnv* e = env();
            jbyteArray bytes = toBytes(e, text);
            e->CallVoidMethod(self->activity_, self->setClipboard_, bytes);
            e->DeleteLocalRef(bytes);
            check(e);
        };
        callbacks.systemBarsAppearance = [](bool lightStatusBars, bool lightNavigationBars) {
            android_ime::setSystemBarsAppearance(lightStatusBars, lightNavigationBars);
        };
        // The browser, or the app for the scheme (an intent).
        callbacks.openUri = [self](const std::string& uri) {
            JNIEnv* e = env();
            jbyteArray bytes = toBytes(e, uri);
            bool opened = e->CallBooleanMethod(self->activity_, self->openUri_, bytes);
            e->DeleteLocalRef(bytes);
            check(e);
            if (!opened) self->log("kompoz: nothing opens " + uri);
        };
        callbacks.hapticFeedback = [](HapticFeedbackType type) { android_ime::performHapticFeedback(int(type)); };
        callbacks.hideSoftwareKeyboard = [] { android_ime::hide(); };
        callbacks.imeControlBy = [self](float dp) { android_ime::imeControlBy(dp * self->scale_); };  // dp -> window pixels
        callbacks.imeControlEnd = [self](float velocity) { android_ime::imeControlEnd(velocity * self->scale_); };
        callbacks.autofillFocus = [self](int id, Rect bounds, bool entered) { android_ime::autofillFocus(id, bounds, entered, self->scale_); };
        callbacks.autofillValue = [](int id, const std::string& text) { android_ime::autofillValue(id, text); };
        callbacks.autofillVisibility = [](int id, bool visible) { android_ime::autofillVisibility(id, visible); };
        callbacks.autofillDone = [](bool commit) { android_ime::autofillDone(commit); };
    }

    void started(ComposeHost& h) override {
        AndroidPlatform* self = this;
        // Created again after Android ended the process: rememberSaveable values come back.
        if (std::vector<uint8_t> state = android_ime::takeRestoredState(); !state.empty()) h.restoreState(state);
        android_ime::setDeepLinkHandler([self](std::string link) { self->host_->openDeepLink(std::move(link)); });
        android_ime::setHandlers(
            [self](const TextEditState& s) { self->host_->textEdit(s); }, [self](ImeAction action) { self->host_->imeAction(action); },
            [self](const std::array<float, 46>& v) {
                float k = 1 / self->scale_;  // window pixels -> dp
                auto kind = [&](size_t i) { return PaddingValues{v[i] * k, v[i + 1] * k, v[i + 2] * k, v[i + 3] * k}; };
                self->host_->setSystemInsets({.statusBars = kind(0), .navigationBars = kind(4), .ime = kind(8),
                                              .displayCutout = kind(12), .systemGestures = kind(16), .captionBar = kind(30),
                                              .mandatorySystemGestures = kind(34), .tappableElement = kind(38), .waterfall = kind(42),
                                              .imeAnimating = v[29] != 0,
                                              .imeVisible = v[28] != 0, .imeAnimationSource = kind(20), .imeAnimationTarget = kind(24)});
            },
            [self](float) { self->deliver({.type = EventType::MetricsChanged}); });  // Settings > Display > Font size changed while running
        android_ime::setMediaHandler([self] { self->host_->setMediaInfo(self->media(false)); });  // a mouse or keyboard attached, docking
        android_ime::setSemanticsActionHandler([self](int id, SemanticsAction action, float argument) {
            self->host_->performSemanticsAction(id, action, argument);
        });
        android_ime::setTextActionHandler([self](int id, std::string text) {
            self->host_->performSemanticsAction(id, SemanticsAction::SetText, text);
        });
        android_ime::setBackHandler([self](android_ime::BackPhase phase, BackEventCompat event) {
            using Phase = android_ime::BackPhase;
            event.touchX /= self->scale_, event.touchY /= self->scale_;  // window pixels -> dp
            switch (phase) {
            case Phase::Started: self->host_->backStarted(event); break;
            case Phase::Progressed: self->host_->backProgressed(event); break;
            case Phase::Cancelled: self->host_->backCancelled(); break;
            case Phase::Invoked:
                if (!self->host_->backInvoked()) self->leaveApp();  // nothing took it after all: leave the app
                break;
            }
        });
        android_ime::attach();
    }

    void handled(ComposeHost& h, const Event& e) override {
        switch (e.type) {
        case EventType::WillEnterBackground:  // between frames: Android may end the app from now on
            android_ime::setSavedState(h.saveState());
            break;
        case EventType::FocusGained:
        case EventType::FocusLost: semanticsDirty_ = true; break;
        default: break;
        }
    }

    void composed(ComposeHost& h, bool redraw, int64_t frameTime) override {
        if (bool on = h.keepScreenOn(); on != screenKeptOn_) {  // keepScreenOn: the window's KEEP_SCREEN_ON while it is laid out
            screenKeptOn_ = on;
            JNIEnv* e = env();
            e->CallVoidMethod(activity_, keepScreenOnMethod_, jboolean(on));
            check(e);
        }
        // Accessibility services read the semantics tree from the UI thread: publish it after
        // frames that changed the screen, at most every 50 ms, and only every half second while it
        // keeps changing (a scroll, an animation): building the tree takes a frame's time.
        semanticsDirty_ = semanticsDirty_ || redraw || android_ime::takePublishRequest();
        if (android_ime::accessibilityEnabled() && semanticsDirty_ && frameTime - lastSemantics_ > (h.hasPendingWork() ? 500'000'000 : 50'000'000)) {
            lastSemantics_ = frameTime;
            semanticsDirty_ = false;
            android_ime::publishSemantics(h.semanticsTree(), scale_);
        }
        // Android 13+ goes back itself (leaving the app, with its animation) unless the app's
        // callback is there: it is while something here takes back.
        if (bool wants = h.wantsBack(); wants != wantsBack_) {
            wantsBack_ = wants;
            android_ime::setBackEnabled(wants);
        }
        // Something sensitive laid out: the window kept out of screen shares (Android 15+).
        if (bool sensitive = h.sensitiveContent(); sensitive != sensitiveContent_) {
            sensitiveContent_ = sensitive;
            android_ime::setContentSensitive(sensitive);
        }
        // The refresh rate it would like (preferredFrameRate), when that changes.
        if (float rate = h.preferredFrameRate(); rate != frameRate_ || h.preferredFrameRateCategory() != frameRateCategory_) {
            frameRate_ = rate;
            frameRateCategory_ = h.preferredFrameRateCategory();
            android_ime::setFrameRate(rate, int(frameRateCategory_));
        }
        // Where the system's edge swipes are kept off, when that changes.
        const std::vector<Rect>& exclusion = h.systemGestureExclusion();
        auto same = [](const Rect& a, const Rect& b) { return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height; };
        if (!std::ranges::equal(exclusion, gestureExclusion_, same)) {
            gestureExclusion_ = exclusion;
            android_ime::setSystemGestureExclusion(exclusion, scale_);
        }
    }

    bool wantsFrames() const override { return android_ime::accessibilityEnabled() && semanticsDirty_; }  // one more frame publishes it
    void frameWork(int64_t ns) override { android_performance::report(ns); }
    void leaveApp() override { callVoid(leave_); }

private:
    jobject activity_ = nullptr, view_ = nullptr, assets_ = nullptr;
    jmethodID displayInfo_ = nullptr, language_ = nullptr, clipboard_ = nullptr, setClipboard_ = nullptr, openUri_ = nullptr,
              keepScreenOnMethod_ = nullptr, leave_ = nullptr, finish_ = nullptr, requestFrame_ = nullptr, setCursor_ = nullptr;
    std::string filesDir_, externalFilesDir_;
    ALooper* looper_ = nullptr;
    int wakeFd_ = -1, timerFd_ = -1;
    ANativeWindow* window_ = nullptr;
    int width_ = 0, height_ = 0;
    mutable float scale_ = 1;  // as metrics() last said
    mutable int64_t period_ = 16'666'667;  // the display's refresh period, the same
    bool stopped_ = false, resumed_ = false, focused_ = false;  // the activity's, before the app starts too
    void* vulkan_ = nullptr;
    EGLDisplay display_ = EGL_NO_DISPLAY;
    EGLContext context_ = EGL_NO_CONTEXT;
    EGLConfig config_ = nullptr;
    EGLSurface windowSurface_ = EGL_NO_SURFACE, pbuffer_ = EGL_NO_SURFACE;  // (the pbuffer current while there is no window)
    ComposeHost* host_ = nullptr;
    int64_t lastSemantics_ = 0;
    bool semanticsDirty_ = true, wantsBack_ = false, screenKeptOn_ = false, sensitiveContent_ = false;
    float frameRate_ = 0;
    FrameRateCategory frameRateCategory_ = FrameRateCategory::Default;
    std::vector<Rect> gestureExclusion_;

    void callVoid(jmethodID method) {
        JNIEnv* e = env();
        e->CallVoidMethod(activity_, method);
        check(e);
    }
    void callVoid(jmethodID method, jint argument, jobject on) {
        JNIEnv* e = env();
        e->CallVoidMethod(on, method, argument);
        check(e);
    }

    // To the core (when it has started), and a frame after it.
    void deliver(const Event& event) {
        if (!core) return;
        core->handle(event);
        if (!stopped_) requestFrame();
    }

    void requestFrame() {
        if (!core || !window_ || stopped_) return;
        JNIEnv* e = env();
        e->CallVoidMethod(view_, requestFrame_);  // (KompozView posts one Choreographer callback at a time)
        check(e);
    }

    // A wake at `at` (CLOCK_MONOTONIC), or none.
    void armTimer(int64_t at) {
        itimerspec spec{};
        if (at >= 0) {
            at = std::max<int64_t>(at, 1);
            spec.it_value = {time_t(at / 1'000'000'000), long(at % 1'000'000'000)};
        }
        timerfd_settime(timerFd_, TFD_TIMER_ABSTIME, &spec, nullptr);
    }

    void startApp() {
        core = std::make_unique<AppCore>(*this);
        char name[] = "kompoz";
        char* argv[] = {name, nullptr};
        if (!core->start(1, argv)) {
            core.reset();
            callVoid(finish_);
            return;
        }
        // What the activity went through before its surface came.
        deliver({.type = EventType::Shown});
        deliver({.type = EventType::Resized});
        if (focused_) deliver({.type = EventType::FocusGained});
        if (!stopped_) deliver({.type = EventType::WillEnterForeground});
        if (resumed_) deliver({.type = EventType::DidEnterForeground});
    }

    // The window went or is another: the GPU device lets go of the old one first.
    void windowChanged() {
        if (core) core->surfaceChanged();  // (Vulkan: the swapchain and the surface)
        if (display_ == EGL_NO_DISPLAY) return;
        eglMakeCurrent(display_, pbuffer_, pbuffer_, context_);
        if (windowSurface_ != EGL_NO_SURFACE) eglDestroySurface(display_, std::exchange(windowSurface_, EGL_NO_SURFACE));
        if (!window_) return;
        windowSurface_ = eglCreateWindowSurface(display_, config_, window_, nullptr);
        if (windowSurface_ == EGL_NO_SURFACE || !eglMakeCurrent(display_, windowSurface_, windowSurface_, context_)) {
            log("kompoz: no EGL surface for the window");
            eglMakeCurrent(display_, pbuffer_, pbuffer_, context_);
        }
    }

    // An OpenGL ES 3.0 context (a debug one with KOMPOZ_GPU_VALIDATION=1), current on the window's
    // surface, or on a small pbuffer while there is none.
    bool openGles(gpu::Surface& surface, std::string* error) {
        auto failed = [&](const char* why) {
            *error = why;
            closeWindow();
            return false;
        };
        display_ = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (display_ == EGL_NO_DISPLAY || !eglInitialize(display_, nullptr, nullptr)) {
            display_ = EGL_NO_DISPLAY;
            return failed("no EGL display");
        }
        const EGLint attributes[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
                                     EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8, EGL_DEPTH_SIZE, 0,
                                     EGL_STENCIL_SIZE, 0, EGL_NONE};
        EGLint count = 0;
        if (!eglChooseConfig(display_, attributes, &config_, 1, &count) || count < 1) return failed("no OpenGL ES 3 configuration");
        bool debug = option("KOMPOZ_GPU_VALIDATION") == "1";
        const EGLint contextAttributes[] = {EGL_CONTEXT_MAJOR_VERSION_KHR, 3, EGL_CONTEXT_MINOR_VERSION_KHR, 0,
                                            EGL_CONTEXT_FLAGS_KHR, debug ? EGL_CONTEXT_OPENGL_DEBUG_BIT_KHR : 0, EGL_NONE};
        context_ = eglCreateContext(display_, config_, EGL_NO_CONTEXT, contextAttributes);
        const EGLint pbufferAttributes[] = {EGL_WIDTH, 1, EGL_HEIGHT, 1, EGL_NONE};
        pbuffer_ = context_ != EGL_NO_CONTEXT ? eglCreatePbufferSurface(display_, config_, pbufferAttributes) : EGL_NO_SURFACE;
        if (pbuffer_ == EGL_NO_SURFACE) return failed("no OpenGL ES 3 context");
        windowSurface_ = window_ ? eglCreateWindowSurface(display_, config_, window_, nullptr) : EGL_NO_SURFACE;
        EGLSurface current = windowSurface_ != EGL_NO_SURFACE ? windowSurface_ : pbuffer_;
        if (!eglMakeCurrent(display_, current, current, context_)) return failed("the OpenGL ES 3 context can't be made current");
        static void* gles = dlopen("libGLESv3.so", RTLD_NOW | RTLD_LOCAL);
        surface.glGetProcAddress = [](const char* name) -> void* {
            if (void* f = gles ? dlsym(gles, name) : nullptr) return f;
            return reinterpret_cast<void*>(eglGetProcAddress(name));
        };
        surface.glSwap = [this] {
            if (windowSurface_ != EGL_NO_SURFACE) eglSwapBuffers(display_, windowSurface_);
        };
        return true;
    }
};

std::unique_ptr<AndroidPlatform> gApp;  // the app, once its activity is created
bool gRan = false;                      // (one a process: Kompoz's statics aren't made for a second)

} // namespace
} // namespace kompoz::platform

using kompoz::platform::gApp;

extern "C" {

JNIEXPORT jboolean JNICALL Java_dev_kompoz_KompozActivity_nativeCreate(JNIEnv* e, jclass, jobject activity, jobject assets, jstring files,
                                                                     jstring externalFiles, jstring cache) {
    using namespace kompoz::platform;
    if (gRan) return JNI_FALSE;
    gRan = true;
    gApp = std::make_unique<AndroidPlatform>(e, activity, assets, fromString(e, files), fromString(e, externalFiles), fromString(e, cache));
    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_dev_kompoz_KompozActivity_nativeDestroy(JNIEnv*, jclass) { gApp.reset(); }

JNIEXPORT void JNICALL Java_dev_kompoz_KompozActivity_nativeLifecycle(JNIEnv*, jclass, jint event) {
    if (gApp) gApp->lifecycle(event);
}

JNIEXPORT void JNICALL Java_dev_kompoz_KompozView_nativeSurface(JNIEnv* e, jclass, jobject surface, jint width, jint height) {
    if (gApp) gApp->surface(e, surface, width, height);
}

JNIEXPORT void JNICALL Java_dev_kompoz_KompozView_nativeFrame(JNIEnv*, jclass, jlong frameTimeNanos) {
    if (gApp) gApp->frame(frameTimeNanos);
}

JNIEXPORT void JNICALL Java_dev_kompoz_KompozView_nativeMotion(JNIEnv* e, jclass, jint action, jint index, jint count, jintArray ids,
                                                               jfloatArray xy, jlong time, jint tool, jint button, jfloat scrollX,
                                                               jfloat scrollY, jint meta) {
    if (!gApp || count <= 0) return;
    std::vector<jint> pointerIds(static_cast<size_t>(count));
    std::vector<jfloat> positions(size_t(count) * 2);
    e->GetIntArrayRegion(ids, 0, count, pointerIds.data());
    e->GetFloatArrayRegion(xy, 0, count * 2, positions.data());
    gApp->motion(action, index, count, pointerIds.data(), positions.data(), time, tool, button, scrollX, scrollY, meta);
}

JNIEXPORT jboolean JNICALL Java_dev_kompoz_KompozView_nativeKey(JNIEnv*, jclass, jint code, jboolean down, jint meta) {
    return gApp && gApp->key(code, down, meta);
}

} // extern "C"
