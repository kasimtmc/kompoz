#include "win32_platform.hpp"

#include "../app/app_core.hpp"
#include "../common/desktop_accent.hpp"
#include "../common/egl_window.hpp"
#include "../common/system.hpp"
#include "../common/wallpaper_colors.hpp"
#include "uia_bridge.hpp"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef UNICODE
#define UNICODE 1  // (IDC_ARROW and the others as wide strings)
#endif
#include <windows.h>

#include <cfgmgr32.h>
#include <dbt.h>
#include <dwmapi.h>
#include <imm.h>
#include <mmsystem.h>
#include <ole2.h>
#include <shellapi.h>
#ifdef KOMPOZ_GPU_VULKAN
#define VK_USE_PLATFORM_WIN32_KHR 1
#include <vulkan/vulkan.h>
#endif

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kompoz::platform {
namespace {

std::wstring wide(const std::string& s) {
    std::wstring out(size_t(MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0)), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), int(out.size()));
    return out;
}
std::string narrow(const wchar_t* s, int length = -1) {
    if (length < 0) length = int(wcslen(s));
    std::string out(size_t(WideCharToMultiByte(CP_UTF8, 0, s, length, nullptr, 0, nullptr, nullptr)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, length, out.data(), int(out.size()), nullptr, nullptr);
    return out;
}

int64_t steadyNow() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

// A virtual key as Kompoz's key.
Key keyOf(WPARAM vk) {
    if (vk >= 'A' && vk <= 'Z') return Key(int(Key::A) + int(vk - 'A'));
    if (vk >= '0' && vk <= '9') return Key(int(Key::Zero) + int(vk - '0'));
    if (vk >= VK_F1 && vk <= VK_F12) return Key(int(Key::F1) + int(vk - VK_F1));
    switch (vk) {
    case VK_RETURN: return Key::Enter;
    case VK_ESCAPE: return Key::Escape;
    case VK_TAB: return Key::Tab;
    case VK_BACK: return Key::Backspace;
    case VK_DELETE: return Key::Delete;
    case VK_SPACE: return Key::Space;
    case VK_LEFT: return Key::Left;
    case VK_RIGHT: return Key::Right;
    case VK_UP: return Key::Up;
    case VK_DOWN: return Key::Down;
    case VK_HOME: return Key::Home;
    case VK_END: return Key::End;
    case VK_PRIOR: return Key::PageUp;
    case VK_NEXT: return Key::PageDown;
    default: return Key::Unknown;
    }
}

// The keys held down. AltGr comes as Ctrl and Alt (the right Alt and a left Ctrl Windows makes
// up): not Ctrl, so that what it types isn't a shortcut.
KeyModifiers heldKeys() {
    auto down = [](int vk) { return (GetKeyState(vk) & 0x8000) != 0; };
    bool altGr = down(VK_RMENU) && down(VK_LCONTROL) && !down(VK_RCONTROL);
    bool ctrl = (down(VK_CONTROL) && !altGr) || down(VK_LWIN) || down(VK_RWIN);
    return {down(VK_SHIFT), ctrl, down(VK_MENU) && !altGr};
}

// Whether the user prefers dark apps (Settings > Personalization > Colors).
bool darkMode() {
    DWORD light = 1, size = sizeof light;
    RegGetValueW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\Themes\\Personalize", L"AppsUseLightTheme",
                 RRF_RT_REG_DWORD, nullptr, &light, &size);
    return light == 0;
}

class Win32Platform;

// Drops from other apps (OLE): files, else text, told to the platform as they come.
class DropTarget final : public IDropTarget {
public:
    explicit DropTarget(Win32Platform& platform) : platform_(platform) {}
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID id, void** out) override {
        if (id == IID_IUnknown || id == IID_IDropTarget) {
            *out = static_cast<IDropTarget*>(this);
            AddRef();
            return S_OK;
        }
        *out = nullptr;
        return E_NOINTERFACE;
    }
    ULONG STDMETHODCALLTYPE AddRef() override { return ++references_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG left = --references_;
        if (left == 0) delete this;
        return left;
    }
    HRESULT STDMETHODCALLTYPE DragEnter(IDataObject* data, DWORD, POINTL at, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragOver(DWORD, POINTL at, DWORD* effect) override;
    HRESULT STDMETHODCALLTYPE DragLeave() override;
    HRESULT STDMETHODCALLTYPE Drop(IDataObject* data, DWORD, POINTL at, DWORD* effect) override;

private:
    Win32Platform& platform_;
    std::atomic<ULONG> references_{1};
    bool taken_ = false;  // files or text in the drag
};

// OpenGL ES through the driver's OpenGL (WGL_EXT_create_context_es2_profile), where ANGLE isn't
// beside the program: opengl32.dll loaded at run time.
class WglEs {
public:
    ~WglEs() { close(); }
    bool open(HWND hwnd, bool debug, gpu::Surface& surface, std::string* error) {
        library_ = LoadLibraryW(L"opengl32.dll");
        auto get = [this](const char* name) { return reinterpret_cast<void*>(GetProcAddress(library_, name)); };
        createContext_ = reinterpret_cast<HGLRC(WINAPI*)(HDC)>(library_ ? get("wglCreateContext") : nullptr);
        deleteContext_ = reinterpret_cast<BOOL(WINAPI*)(HGLRC)>(library_ ? get("wglDeleteContext") : nullptr);
        makeCurrent_ = reinterpret_cast<BOOL(WINAPI*)(HDC, HGLRC)>(library_ ? get("wglMakeCurrent") : nullptr);
        getProcAddress_ = reinterpret_cast<PROC(WINAPI*)(LPCSTR)>(library_ ? get("wglGetProcAddress") : nullptr);
        if (!createContext_ || !deleteContext_ || !makeCurrent_ || !getProcAddress_) return failed(error, "no opengl32.dll");
        hwnd_ = hwnd, dc_ = GetDC(hwnd);
        PIXELFORMATDESCRIPTOR format{};
        format.nSize = sizeof format, format.nVersion = 1;
        format.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
        format.iPixelType = PFD_TYPE_RGBA, format.cColorBits = 32, format.cAlphaBits = 8, format.iLayerType = PFD_MAIN_PLANE;
        int chosen = ChoosePixelFormat(dc_, &format);
        if (!chosen || !SetPixelFormat(dc_, chosen, &format)) return failed(error, "no pixel format for OpenGL");
        // An OpenGL context first, to ask for the OpenGL ES one through.
        HGLRC first = createContext_(dc_);
        if (!first || !makeCurrent_(dc_, first)) return failed(error, "no OpenGL context");
        auto extensions = reinterpret_cast<const char*(WINAPI*)(HDC)>(getProcAddress_("wglGetExtensionsStringARB"));
        auto createAttribs = reinterpret_cast<HGLRC(WINAPI*)(HDC, HGLRC, const int*)>(getProcAddress_("wglCreateContextAttribsARB"));
        const char* list = extensions ? extensions(dc_) : nullptr;
        if (createAttribs && list && std::strstr(list, "WGL_EXT_create_context_es2_profile")) {
            const int attributes[] = {0x2091, 3, 0x2092, 0, 0x9126, 0x4, 0x2094, debug ? 0x1 : 0, 0};  // (3.0, the ES profile, debug)
            context_ = createAttribs(dc_, nullptr, attributes);
        }
        makeCurrent_(nullptr, nullptr);
        deleteContext_(first);
        if (!context_ || !makeCurrent_(dc_, context_)) return failed(error, "no OpenGL ES 3 context through WGL");
        if (auto interval = reinterpret_cast<BOOL(WINAPI*)(int)>(getProcAddress_("wglSwapIntervalEXT"))) interval(1);
        static WglEs* current = nullptr;
        current = this;
        surface.glGetProcAddress = [](const char* name) -> void* {
            auto f = reinterpret_cast<intptr_t>(current->getProcAddress_(name));
            if (f > 3 && f != -1) return reinterpret_cast<void*>(f);  // (0..3 and -1: OpenGL 1.1's own, from opengl32.dll)
            return reinterpret_cast<void*>(GetProcAddress(current->library_, name));
        };
        surface.glSwap = [this] { SwapBuffers(dc_); };
        return true;
    }
    void close() {
        if (context_) {
            makeCurrent_(nullptr, nullptr);
            deleteContext_(std::exchange(context_, nullptr));
        }
        if (dc_) ReleaseDC(hwnd_, std::exchange(dc_, nullptr));
        if (library_) FreeLibrary(std::exchange(library_, nullptr));
    }

private:
    bool failed(std::string* error, const char* why) {
        *error = why;
        close();
        return false;
    }
    HMODULE library_ = nullptr;
    HWND hwnd_ = nullptr;
    HDC dc_ = nullptr;
    HGLRC context_ = nullptr;
    HGLRC(WINAPI* createContext_)(HDC) = nullptr;
    BOOL(WINAPI* deleteContext_)(HGLRC) = nullptr;
    BOOL(WINAPI* makeCurrent_)(HDC, HGLRC) = nullptr;
    PROC(WINAPI* getProcAddress_)(LPCSTR) = nullptr;
};

// Ctrl+C or Ctrl+Break in the console the app was started from: a quit, as SDL made it (the app
// writes its state on the way out).
std::atomic<bool> gQuitAsked{false};
HANDLE gQuitWake = nullptr;

constexpr UINT_PTR kModalTimer = 1;  // frames while Windows resizes or moves the window in a loop of its own
constexpr wchar_t kClassName[] = L"KompozWindow";
// KSCATEGORY_VIDEO_CAMERA: the cameras' device interfaces.
constexpr GUID kVideoCameras = {0xe5323777, 0xf976, 0x4f5b, {0x9b, 0x55, 0xb9, 0x46, 0x99, 0xc4, 0x6e, 0x44}};

class Win32Platform final : public Platform {
public:
    Win32Platform() {
        wakeEvent_ = CreateEventW(nullptr, FALSE, FALSE, nullptr);
        // A timer of a millisecond's resolution where Windows has one (10 1803+), for frames on time.
        timer_ = CreateWaitableTimerExW(nullptr, nullptr, 0x2 /* CREATE_WAITABLE_TIMER_HIGH_RESOLUTION */, TIMER_ALL_ACCESS);
        if (!timer_) timer_ = CreateWaitableTimerW(nullptr, FALSE, nullptr);
    }
    ~Win32Platform() override {
        a11y_.reset();  // (UI Automation's hooked into the window)
        wallpaper_.reset();
        accent_.reset();
        closeWindow();
        if (oleStarted_) OleUninitialize();
        if (wakeEvent_) CloseHandle(wakeEvent_);
        if (timer_) CloseHandle(timer_);
        if (keptOn_) SetThreadExecutionState(ES_CONTINUOUS);
    }

    // The process aware of each monitor's DPI, the window class and OLE; false where the app
    // can't run here.
    bool connect() {
        if (auto set = reinterpret_cast<BOOL(WINAPI*)(HANDLE)>(GetProcAddress(GetModuleHandleW(L"user32.dll"), "SetProcessDpiAwarenessContext")))
            set(reinterpret_cast<HANDLE>(-4) /* DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 */);
        else
            SetProcessDPIAware();
        oleStarted_ = SUCCEEDED(OleInitialize(nullptr));
        gQuitWake = wakeEvent_;
        SetConsoleCtrlHandler(
            [](DWORD type) -> BOOL {
                if (type != CTRL_C_EVENT && type != CTRL_BREAK_EVENT) return FALSE;
                gQuitAsked = true;
                SetEvent(gQuitWake);
                return TRUE;
            },
            TRUE);
        WNDCLASSEXW c{};
        c.cbSize = sizeof c, c.style = CS_HREDRAW | CS_VREDRAW, c.lpfnWndProc = &windowProc, c.hInstance = GetModuleHandleW(nullptr);
        c.hCursor = LoadCursorW(nullptr, IDC_ARROW), c.lpszClassName = kClassName;
        c.hIcon = LoadIconW(c.hInstance, MAKEINTRESOURCEW(1));  // (the program's own, where it has one)
        if (!c.hIcon) c.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        return wakeEvent_ && timer_ && (RegisterClassExW(&c) || GetLastError() == ERROR_CLASS_ALREADY_EXISTS);
    }

    int run(int argc, char* argv[]) {
        AppCore core{*this};
        core_ = &core;
        if (!core.start(argc, argv)) return 1;
        while (!quit_) {
            MSG m;
            while (PeekMessageW(&m, nullptr, 0, 0, PM_REMOVE)) {
                if (m.message == WM_QUIT) quit_ = true;
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            if (quit_ || !step()) break;
            // Until a message, a wake, or the timer (the frame due, or a coroutine's delay).
            HANDLE handles[] = {wakeEvent_, timer_};
            if (!needFrame_ || waitingForDue_)
                if (DWORD woken = MsgWaitForMultipleObjectsEx(2, handles, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE); woken < WAIT_OBJECT_0 + 2)
                    events_.push_back({.type = EventType::Wake});
        }
        core.quit();
        core_ = nullptr;
        return 0;
    }

    // ---- Platform ------------------------------------------------------------------------

    std::optional<std::string> option(std::string_view name) const override {
        std::wstring key = wide(std::string(name));
        DWORD size = GetEnvironmentVariableW(key.c_str(), nullptr, 0);
        if (size == 0) return std::nullopt;
        std::wstring value(size, L'\0');
        value.resize(GetEnvironmentVariableW(key.c_str(), value.data(), size));
        return narrow(value.c_str(), int(value.size()));
    }
    void log(const std::string& message) const override { platform::log(message); }
    int64_t now() const override { return steadyNow(); }
    std::vector<gpu::Backend> backends() const override { return {gpu::Backend::Vulkan, gpu::Backend::GLES}; }

    bool openWindow(const Application& app, gpu::Backend backend, gpu::Surface& surface, std::string* error) override {
        auto failed = [&](std::string why) {
            *error = std::move(why);
            closeWindow();
            return false;
        };
        title_ = app.title;
        hwnd_ = CreateWindowExW(0, kClassName, wide(title_).c_str(), WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT, 100, 100, nullptr, nullptr,
                                GetModuleHandleW(nullptr), this);
        if (!hwnd_) return failed("the window couldn't be made");
        // The app's size in dp at its monitor's DPI, the frame around it.
        dpi_ = GetDpiForWindow(hwnd_);
        RECT r{0, 0, LONG(std::lround(app.width * scale())), LONG(std::lround(app.height * scale()))};
        AdjustWindowRectExForDpi(&r, WS_OVERLAPPEDWINDOW, FALSE, 0, dpi_);
        SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
        clientSize();
        dark_ = darkMode();
        darkTitleBar();
        refreshRate_ = monitorRefreshRate();
        if (oleStarted_) {
            dropTarget_ = new DropTarget(*this);
            RegisterDragDrop(hwnd_, dropTarget_);
        }
        ImmAssociateContextEx(hwnd_, nullptr, 0);  // (the input method only while a field takes typing)
        surface.drawableSize = [this](int* w, int* h) { *w = width_, *h = height_; };
        switch (backend) {
#ifdef KOMPOZ_GPU_VULKAN
        case gpu::Backend::Vulkan: {
            if (!vulkan_) vulkan_ = LoadLibraryW(L"vulkan-1.dll");
            auto getProcAddress = vulkan_ ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vulkan_, "vkGetInstanceProcAddr")) : nullptr;
            if (!getProcAddress) return failed("no Vulkan loader");
            surface.vulkanGetInstanceProcAddr = reinterpret_cast<void* (*)(void*, const char*)>(getProcAddress);
            surface.vulkanInstanceExtensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_WIN32_SURFACE_EXTENSION_NAME};
            surface.createVulkanSurface = [this, getProcAddress](void* instance, uint64_t* out) {
                auto create = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(getProcAddress(static_cast<VkInstance>(instance), "vkCreateWin32SurfaceKHR"));
                VkWin32SurfaceCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR, .hinstance = GetModuleHandleW(nullptr), .hwnd = hwnd_};
                VkSurfaceKHR made = VK_NULL_HANDLE;
                if (!create || create(static_cast<VkInstance>(instance), &info, nullptr, &made) != VK_SUCCESS) return false;
                std::memcpy(out, &made, sizeof made);
                return true;
            };
            break;
        }
#endif
        case gpu::Backend::GLES: {
            // ANGLE's where the app has it beside it (libEGL.dll, libGLESv2.dll): OpenGL ES over
            // Direct3D 11, the same on every GPU. Else the driver's own OpenGL ES.
            bool debug = option("KOMPOZ_GPU_VALIDATION") == "1";
            bool made = EglWindow::available() ? egl_.open({.window = hwnd_, .swapInterval = 1, .debug = debug}, surface, error)
                                               : wgl_.open(hwnd_, debug, surface, error);
            if (!made) return failed(*error);
            break;
        }
        default: return failed(std::string("no ") + gpu::backendName(backend) + " here");
        }
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        events_.push_back({.type = EventType::Shown});
        return true;
    }

    void closeWindow() override {
        egl_.close();
        wgl_.close();
        if (hwnd_) {
            if (dropTarget_) RevokeDragDrop(hwnd_);
            DestroyWindow(std::exchange(hwnd_, nullptr));
        }
        if (dropTarget_) std::exchange(dropTarget_, nullptr)->Release();
        if (vulkan_) FreeLibrary(std::exchange(vulkan_, nullptr));
    }

    void showError(const std::string& message) override { MessageBoxW(hwnd_, wide(message).c_str(), L"Kompoz", MB_OK | MB_ICONERROR); }

    // Points are the window's pixels; the scale is its monitor's DPI / 96; KOMPOZ_SCALE and
    // KOMPOZ_FONT_SCALE stand in for them.
    Metrics metrics() const override {
        Metrics m;
        m.pixelWidth = m.pointWidth = width_, m.pixelHeight = m.pointHeight = height_;
        m.scale = float(scale());
        m.pointToDp = 1 / m.scale;
        if (auto v = option("KOMPOZ_FONT_SCALE")) m.fontScale = std::max(0.5f, float(std::atof(v->c_str())));
        m.refreshRate = refreshRate_;
        m.safeArea = Rect{0, 0, float(width_), float(height_)};
        m.darkTheme = dark_;
        m.language = preferredLanguage();
        return m;
    }
    void metricsUpdated(ComposeHost&) override {
        if (wallpaper_) wallpaper_->setDarkTheme(dark_);  // (a dark variant)
    }
    void pixelSize(int* width, int* height) const override { *width = width_, *height = height_; }
    void setWindowSize(int width, int height) override {
        if (!hwnd_) return;
        RECT r{0, 0, width, height};
        AdjustWindowRectExForDpi(&r, DWORD(GetWindowLongPtrW(hwnd_, GWL_STYLE)), FALSE, 0, dpi_);
        SetWindowPos(hwnd_, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
    }

    // A mouse is a fine pointer, a touch screen alone a coarse one, a keyboard the system has a
    // physical one; a microphone (waveIn) and a camera (its device interface) as probed.
    MediaInfo media(bool probe) override {
        if (probe && !probed_) {
            probed_ = true;
            hasMicrophone_ = waveInGetNumDevs() > 0;
            ULONG size = 0;
            hasCamera_ = CM_Get_Device_Interface_List_SizeW(&size, const_cast<GUID*>(&kVideoCameras), nullptr, CM_GET_DEVICE_INTERFACE_LIST_PRESENT) ==
                             CR_SUCCESS &&
                         size > 1;
        }
        int digitizer = GetSystemMetrics(SM_DIGITIZER);
        bool touch = (digitizer & NID_READY) && (digitizer & (NID_INTEGRATED_TOUCH | NID_EXTERNAL_TOUCH));
        UINT devices = 0;
        GetRawInputDeviceList(nullptr, &devices, sizeof(RAWINPUTDEVICELIST));
        std::vector<RAWINPUTDEVICELIST> list(devices);
        bool keyboard = false;
        if (devices && GetRawInputDeviceList(list.data(), &devices, sizeof(RAWINPUTDEVICELIST)) != UINT(-1))
            for (const RAWINPUTDEVICELIST& d : list) keyboard = keyboard || d.dwType == RIM_TYPEKEYBOARD;
        MediaInfo info;
        info.pointerPrecision = GetSystemMetrics(SM_MOUSEPRESENT) ? PointerPrecision::Fine : touch ? PointerPrecision::Coarse : PointerPrecision::None;
        info.physicalKeyboard = keyboard;
        info.hasMicrophone = hasMicrophone_, info.hasCamera = hasCamera_;
        return info;
    }

    void attach(ComposeHost&, ComposeHost::Callbacks& callbacks) override {
        Win32Platform* self = this;
        auto notifier = [self](std::atomic<bool>& flag) {
            return Notifier{.notify = [self, &flag] {
                flag = true;
                self->wake();
            }};
        };
        accent_ = std::make_unique<DesktopAccent>(notifier(accentChanged_));
        wallpaper_ = std::make_unique<WallpaperColors>(dark_, notifier(wallpaperChanged_));
        a11y_ = std::make_unique<UiaBridge>(hwnd_, notifier(a11yRequested_));
        callbacks.wake = [self] { self->wake(); };
        callbacks.textInput = [self](bool active, Rect area, const TextEditState&, const ImeOptions&) {
            float k = float(self->scale());  // dp -> pixels
            self->textActive_ = active;
            self->textArea_ = {LONG(std::lround(area.x * k)), LONG(std::lround(area.y * k)), LONG(std::lround((area.x + area.width) * k)),
                               LONG(std::lround((area.y + area.height) * k))};
            self->tellInputMethod();
        };
        callbacks.cursor = [self](Cursor cursor) {
            self->cursor_ = cursor;
            self->showCursor();
        };
        callbacks.getClipboard = [self] { return self->readClipboard(); };
        callbacks.setClipboard = [self](const std::string& text) { self->writeClipboard(text); };
        callbacks.openUri = [self](const std::string& uri) {
            if (reinterpret_cast<INT_PTR>(ShellExecuteW(nullptr, L"open", wide(uri).c_str(), nullptr, nullptr, SW_SHOWNORMAL)) <= 32)
                self->log("kompoz: can't open " + uri);
        };
    }

    void handled(ComposeHost& h, const Event& e) override {
        switch (e.type) {
        case EventType::AccentChanged:
            if (accent_) {
                std::optional<Color> color = accent_->accent();
                h.setDynamicTonalPalette(color ? std::optional(TonalPalette::fromSeed(*color)) : std::nullopt);
            }
            break;
        case EventType::WallpaperChanged:
            if (wallpaper_) h.setWallpaperTonalPalette(wallpaper_->palette());
            break;
        case EventType::AccessibilityRequest:
            if (a11y_)
                for (const auto& r : a11y_->takeRequests()) {
                    if (r.action == SemanticsAction::SetText) h.performSemanticsAction(r.id, r.action, r.text);
                    else h.performSemanticsAction(r.id, r.action, r.argument);
                }
            break;
        default: break;
        }
    }

    void composed(ComposeHost& h, bool redraw, int64_t frameTime) override {
        // keepScreenOn: the display kept on while it is laid out.
        if (bool on = h.keepScreenOn(); on != keptOn_) {
            keptOn_ = on;
            SetThreadExecutionState(on ? ES_CONTINUOUS | ES_DISPLAY_REQUIRED : ES_CONTINUOUS);
        }
        // UI Automation answers from the tree last published: once asked for, after frames that
        // changed the screen.
        bool treeRequested = a11y_ && a11y_->takeTreeRequest();
        semanticsDirty_ = semanticsDirty_ || redraw || treeRequested;
        if (a11y_ && a11y_->active() && semanticsDirty_ &&
            (treeRequested || frameTime - lastSemantics_ > (h.hasPendingWork() ? 500'000'000 : 50'000'000))) {
            lastSemantics_ = frameTime;
            semanticsDirty_ = false;
            a11y_->publish(h.semanticsTree(), float(scale()));
        }
    }

    bool wantsFrames() const override { return a11y_ && a11y_->active() && semanticsDirty_; }  // one more frame publishes it

    // What a drop tells (DropTarget), at a point on the screen.
    void dropped(EventType type, POINTL at, std::string text = {}) {
        POINT p{at.x, at.y};
        ScreenToClient(hwnd_, &p);
        Event e{.type = type, .time = now(), .x = float(p.x), .y = float(p.y)};
        e.text = std::move(text);
        events_.push_back(std::move(e));
    }

private:
    HWND hwnd_ = nullptr;
    std::string title_ = "Kompoz";
    int width_ = 1, height_ = 1;  // pixels
    UINT dpi_ = 96;
    float refreshRate_ = 60;
    bool dark_ = false;
    EglWindow egl_;
    WglEs wgl_;
    HMODULE vulkan_ = nullptr;
    bool oleStarted_ = false;
    DropTarget* dropTarget_ = nullptr;

    AppCore* core_ = nullptr;
    std::deque<Event> events_;
    HANDLE wakeEvent_ = nullptr, timer_ = nullptr;
    bool quit_ = false, busy_ = false, needFrame_ = true, waitingForDue_ = false, inModalLoop_ = false, stepping_ = false;
    int64_t wakeAt_ = -1;

    bool trackingLeave_ = false;
    int buttonsDown_ = 0;          // (the mouse captured while any is)
    wchar_t highSurrogate_ = 0;    // a typed character's first half
    bool textActive_ = false;
    RECT textArea_{};              // the field, in pixels
    Cursor cursor_ = Cursor::Default;
    std::array<HCURSOR, 9> cursors_{};

    std::atomic<bool> accentChanged_{false}, wallpaperChanged_{false}, a11yRequested_{false};
    std::unique_ptr<DesktopAccent> accent_;
    std::unique_ptr<WallpaperColors> wallpaper_;
    std::unique_ptr<UiaBridge> a11y_;
    bool semanticsDirty_ = true, keptOn_ = false, probed_ = false, hasMicrophone_ = false, hasCamera_ = false;
    int64_t lastSemantics_ = 0;

    double scale() const {
        if (auto v = option("KOMPOZ_SCALE"); v && std::atof(v->c_str()) > 0) return std::atof(v->c_str());
        return dpi_ / 96.0;
    }

    // ---- The loop ------------------------------------------------------------------------

    void wake() { SetEvent(wakeEvent_); }

    // The timer set for `at` on the steady clock (-1: none).
    void armTimer(int64_t at) {
        if (at < 0) {
            CancelWaitableTimer(timer_);
            return;
        }
        LARGE_INTEGER due;
        due.QuadPart = -std::max<int64_t>(1, (at - now()) / 100);  // (relative, in 100 ns)
        SetWaitableTimer(timer_, &due, 0, nullptr, nullptr, FALSE);
    }

    // What came handed to the core, and a frame if one is due: false once the app is done. The
    // main loop's, and the window's while Windows resizes or moves it in a loop of its own.
    bool step() {
        if (stepping_) return true;
        stepping_ = true;
        if (accentChanged_.exchange(false)) events_.push_back({.type = EventType::AccentChanged});
        if (wallpaperChanged_.exchange(false)) events_.push_back({.type = EventType::WallpaperChanged});
        if (a11yRequested_.exchange(false)) events_.push_back({.type = EventType::AccessibilityRequest});
        if (gQuitAsked.exchange(false)) events_.push_back({.type = EventType::Quit});
        for (const Event& e : std::exchange(events_, {})) {
            if (e.type == EventType::Quit) quit_ = true;
            core_->handle(e);
            needFrame_ = true;
        }
        bool going = !quit_;
        waitingForDue_ = false;
        if (going && (needFrame_ || busy_)) {
            // The pause until the frame is due is the host's for work between frames (a lazy
            // list's next item); then the timer waits for the rest of it.
            int64_t due = core_->frameDue(), at = now();
            if (due >= 0 && at < due) core_->idle(due - at);
            if (due >= 0 && now() < due) {
                waitingForDue_ = true;
                armTimer(due);
            } else {
                needFrame_ = false;
                Next next = core_->frame();
                busy_ = next.busy, wakeAt_ = next.wakeAt;
                going = !next.quit;
                needFrame_ = busy_;
                if (!busy_) armTimer(wakeAt_);
            }
        }
        stepping_ = false;
        return going;
    }

    static LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        if (message == WM_NCCREATE) SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
        auto* p = reinterpret_cast<Win32Platform*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        if (!p || !p->hwnd_) return DefWindowProcW(hwnd, message, w, l);
        std::optional<LRESULT> handled = p->message(message, w, l);
        return handled ? *handled : DefWindowProcW(hwnd, message, w, l);
    }

    // A window message: its answer, or none for Windows' own handling.
    std::optional<LRESULT> message(UINT message, WPARAM w, LPARAM l) {
        switch (message) {
        case WM_CLOSE: events_.push_back({.type = EventType::Quit}); return 0;
        case WM_SIZE:
            if (w == SIZE_MINIMIZED) {
                events_.push_back({.type = EventType::Minimized});
                return 0;
            }
            if (w == SIZE_RESTORED || w == SIZE_MAXIMIZED) events_.push_back({.type = w == SIZE_MAXIMIZED ? EventType::Maximized : EventType::Restored});
            if (clientSize()) events_.push_back({.type = EventType::Resized});
            if (inModalLoop_) step();  // (drawn as it is resized)
            return 0;
        case WM_ENTERSIZEMOVE:
            inModalLoop_ = true;
            SetTimer(hwnd_, kModalTimer, USER_TIMER_MINIMUM, nullptr);
            return 0;
        case WM_EXITSIZEMOVE:
            inModalLoop_ = false;
            KillTimer(hwnd_, kModalTimer);
            return 0;
        case WM_TIMER:
            if (w != kModalTimer) break;
            step();
            return 0;
        case WM_PAINT:
            ValidateRect(hwnd_, nullptr);  // (Kompoz draws it)
            events_.push_back({.type = EventType::Exposed});
            return 0;
        case WM_ERASEBKGND: return 1;
        case WM_DPICHANGED: {
            dpi_ = HIWORD(w);
            const RECT* suggested = reinterpret_cast<const RECT*>(l);
            SetWindowPos(hwnd_, nullptr, suggested->left, suggested->top, suggested->right - suggested->left, suggested->bottom - suggested->top,
                         SWP_NOZORDER | SWP_NOACTIVATE);
            events_.push_back({.type = EventType::MetricsChanged});
            return 0;
        }
        case WM_WINDOWPOSCHANGED:
        case WM_DISPLAYCHANGE:
            if (float rate = monitorRefreshRate(); rate != refreshRate_) {
                refreshRate_ = rate;
                events_.push_back({.type = EventType::MetricsChanged});
            }
            break;
        case WM_SETTINGCHANGE:
            if (l && std::wstring_view(reinterpret_cast<const wchar_t*>(l)) == L"ImmersiveColorSet" && darkMode() != dark_) {
                dark_ = !dark_;
                darkTitleBar();
                events_.push_back({.type = EventType::MetricsChanged});
            }
            break;
        case WM_DEVICECHANGE:
            if (w == DBT_DEVNODES_CHANGED) events_.push_back({.type = EventType::DevicesChanged});
            break;
        case WM_SETFOCUS:
        case WM_KILLFOCUS:
            semanticsDirty_ = true;
            events_.push_back({.type = message == WM_SETFOCUS ? EventType::FocusGained : EventType::FocusLost});
            break;
        case WM_SETCURSOR:
            if (LOWORD(l) != HTCLIENT) break;
            showCursor();
            return TRUE;
        case WM_MOUSEMOVE:
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP:
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP:
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP:
        case WM_XBUTTONDOWN:
        case WM_XBUTTONUP: return mouse(message, w, l);
        case WM_MOUSELEAVE:
            trackingLeave_ = false;
            events_.push_back({.type = EventType::PointerLeft});
            return 0;
        case WM_MOUSEWHEEL:
        case WM_MOUSEHWHEEL: {  // (a notch is 120; a precision touchpad's less: up and right positive)
            POINT at{short(LOWORD(l)), short(HIWORD(l))};
            ScreenToClient(hwnd_, &at);
            Event e = this->at(EventType::Wheel, at.x, at.y);
            float notches = float(GET_WHEEL_DELTA_WPARAM(w)) / WHEEL_DELTA;
            (message == WM_MOUSEWHEEL ? e.dy : e.dx) = notches;
            events_.push_back(e);
            return 0;
        }
        case WM_POINTERDOWN:
        case WM_POINTERUPDATE:
        case WM_POINTERUP:
        case WM_POINTERCAPTURECHANGED: return touch(message, w, l);
        case WM_KEYDOWN:
        case WM_SYSKEYDOWN:
        case WM_KEYUP:
        case WM_SYSKEYUP: {
            bool down = message == WM_KEYDOWN || message == WM_SYSKEYDOWN;
            events_.push_back(Event{.type = down ? EventType::KeyDown : EventType::KeyUp, .time = now(), .key = keyOf(w), .modifiers = heldKeys()});
            // Alt+F4 and Alt+Space are Windows' (closing, the window's menu); the other Alt keys
            // and F10 aren't (they would open a menu the window hasn't).
            if (message == WM_SYSKEYDOWN && (w == VK_F4 || w == VK_SPACE)) break;
            if (message == WM_SYSKEYDOWN || message == WM_SYSKEYUP) return 0;
            break;
        }
        case WM_SYSCHAR:
            if (w == VK_SPACE) break;
            return 0;
        case WM_CHAR: {  // a character typed (the input method's too), in UTF-16 halves
            auto c = wchar_t(w);
            if (c >= 0xD800 && c < 0xDC00) {
                highSurrogate_ = c;
                return 0;
            }
            if (!textActive_ || c < 0x20 || c == 0x7f) return 0;
            wchar_t text[2] = {highSurrogate_, c};
            bool pair = c >= 0xDC00 && c < 0xE000 && highSurrogate_;
            highSurrogate_ = 0;
            events_.push_back({.type = EventType::Text, .text = pair ? narrow(text, 2) : narrow(&c, 1)});
            return 0;
        }
        case WM_IME_STARTCOMPOSITION:
            tellInputMethod();
            break;
        default: break;
        }
        return std::nullopt;
    }

    Event at(EventType type, double x, double y) const {
        Event e{.type = type, .time = now(), .x = float(x), .y = float(y)};
        e.modifiers = heldKeys();
        return e;
    }

    // The mouse (not what Windows makes up from a finger): moves, its leaving, its buttons, held
    // captured while any is down.
    std::optional<LRESULT> mouse(UINT message, WPARAM w, LPARAM l) {
        LPARAM extra = GetMessageExtraInfo();
        if ((extra & 0xFFFFFF80) == 0xFF515780) return 0;  // (a touch's, which WM_POINTER told)
        double x = short(LOWORD(l)), y = short(HIWORD(l));
        if (message == WM_MOUSEMOVE) {
            if (!trackingLeave_) {
                TRACKMOUSEEVENT track{sizeof track, TME_LEAVE, hwnd_, 0};
                trackingLeave_ = TrackMouseEvent(&track);
            }
            events_.push_back(at(EventType::MouseMove, x, y));
            return 0;
        }
        bool down = message == WM_LBUTTONDOWN || message == WM_RBUTTONDOWN || message == WM_MBUTTONDOWN || message == WM_XBUTTONDOWN;
        Event e = at(down ? EventType::MouseDown : EventType::MouseUp, x, y);
        switch (message) {
        case WM_LBUTTONDOWN:
        case WM_LBUTTONUP: e.button = MouseButton::Left; break;
        case WM_RBUTTONDOWN:
        case WM_RBUTTONUP: e.button = MouseButton::Right; break;
        case WM_MBUTTONDOWN:
        case WM_MBUTTONUP: e.button = MouseButton::Middle; break;
        default: e.button = GET_XBUTTON_WPARAM(w) == XBUTTON1 ? MouseButton::Back : MouseButton::Forward; break;
        }
        buttonsDown_ = std::max(0, buttonsDown_ + (down ? 1 : -1));
        if (down && buttonsDown_ == 1) SetCapture(hwnd_);
        if (!down && buttonsDown_ == 0) ReleaseCapture();
        events_.push_back(e);
        return message == WM_XBUTTONDOWN || message == WM_XBUTTONUP ? std::optional<LRESULT>(TRUE) : std::optional<LRESULT>(0);
    }

    // A finger on a touch screen (WM_POINTER); a pen and the mouse are left to Windows, which makes
    // mouse messages of them.
    std::optional<LRESULT> touch(UINT message, WPARAM w, LPARAM l) {
        UINT32 id = GET_POINTERID_WPARAM(w);
        POINTER_INPUT_TYPE type = PT_POINTER;
        if (!GetPointerType(id, &type) || type != PT_TOUCH) return std::nullopt;
        POINT p{short(LOWORD(l)), short(HIWORD(l))};
        ScreenToClient(hwnd_, &p);
        EventType kind = message == WM_POINTERDOWN ? EventType::TouchDown : message == WM_POINTERUPDATE ? EventType::TouchMove : EventType::TouchUp;
        Event e = at(kind, p.x, p.y);
        e.pointer = int64_t(id);
        events_.push_back(e);
        return 0;
    }

    // The input method on while a field takes typing, its composition at the field and its
    // candidates kept off it.
    void tellInputMethod() {
        if (!hwnd_) return;
        ImmAssociateContextEx(hwnd_, nullptr, textActive_ ? IACE_DEFAULT : 0);
        if (!textActive_) return;
        if (HIMC context = ImmGetContext(hwnd_)) {
            COMPOSITIONFORM composition{CFS_FORCE_POSITION, {textArea_.left, textArea_.top}, {}};
            ImmSetCompositionWindow(context, &composition);
            CANDIDATEFORM candidates{0, CFS_EXCLUDE, {textArea_.left, textArea_.bottom}, textArea_};
            ImmSetCandidateWindow(context, &candidates);
            ImmReleaseContext(hwnd_, context);
        }
    }

    void showCursor() {
        static const LPCWSTR shapes[] = {IDC_ARROW, IDC_HAND, IDC_IBEAM, IDC_CROSS, IDC_SIZEALL, IDC_SIZEWE, IDC_SIZENS, IDC_NO, IDC_WAIT};
        size_t i = std::min(size_t(cursor_), cursors_.size() - 1);
        if (!cursors_[i]) cursors_[i] = LoadCursorW(nullptr, shapes[i]);
        SetCursor(cursors_[i]);
    }

    // The client area's size, as it is now; whether it changed. (Minimized: kept as it was.)
    bool clientSize() {
        RECT r{};
        GetClientRect(hwnd_, &r);
        int w = std::max(1, int(r.right - r.left)), h = std::max(1, int(r.bottom - r.top));
        if (w == width_ && h == height_) return false;
        width_ = w, height_ = h;
        return true;
    }

    // The title bar dark with the system's dark mode (DWMWA_USE_IMMERSIVE_DARK_MODE).
    void darkTitleBar() {
        BOOL on = dark_ ? TRUE : FALSE;
        DwmSetWindowAttribute(hwnd_, 20, &on, sizeof on);
    }

    // The refresh rate of the monitor the window is on.
    float monitorRefreshRate() const {
        MONITORINFOEXW info{};
        info.cbSize = sizeof info;
        DEVMODEW mode{};
        mode.dmSize = sizeof mode;
        if (!hwnd_ || !GetMonitorInfoW(MonitorFromWindow(hwnd_, MONITOR_DEFAULTTONEAREST), &info) ||
            !EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) || mode.dmDisplayFrequency <= 1)
            return 60;
        return float(mode.dmDisplayFrequency);
    }

    // ---- The clipboard -------------------------------------------------------------------

    std::string readClipboard() {
        std::string text;
        if (!OpenClipboard(hwnd_)) return text;
        if (HANDLE data = GetClipboardData(CF_UNICODETEXT))
            if (auto* chars = static_cast<const wchar_t*>(GlobalLock(data))) {
                text = narrow(chars);
                GlobalUnlock(data);
            }
        CloseClipboard();
        return text;
    }

    void writeClipboard(const std::string& text) {
        std::wstring chars = wide(text);
        HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, (chars.size() + 1) * sizeof(wchar_t));
        if (!data) return;
        std::memcpy(GlobalLock(data), chars.c_str(), (chars.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(data);
        if (OpenClipboard(hwnd_)) {
            EmptyClipboard();
            if (SetClipboardData(CF_UNICODETEXT, data)) data = nullptr;  // (the clipboard's now)
            CloseClipboard();
        }
        if (data) GlobalFree(data);
    }
};

// What a drag holds: files (CF_HDROP), else text (CF_UNICODETEXT).
bool hasFormat(IDataObject* data, CLIPFORMAT format) {
    FORMATETC f{format, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    return data->QueryGetData(&f) == S_OK;
}

HRESULT DropTarget::DragEnter(IDataObject* data, DWORD, POINTL at, DWORD* effect) {
    taken_ = hasFormat(data, CF_HDROP) || hasFormat(data, CF_UNICODETEXT);
    *effect = taken_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    platform_.dropped(EventType::DropBegin, at);
    platform_.dropped(EventType::DropMove, at);
    return S_OK;
}

HRESULT DropTarget::DragOver(DWORD, POINTL at, DWORD* effect) {
    *effect = taken_ ? DROPEFFECT_COPY : DROPEFFECT_NONE;
    platform_.dropped(EventType::DropMove, at);
    return S_OK;
}

HRESULT DropTarget::DragLeave() {
    platform_.dropped(EventType::DropComplete, {});
    return S_OK;
}

HRESULT DropTarget::Drop(IDataObject* data, DWORD, POINTL at, DWORD* effect) {
    *effect = DROPEFFECT_NONE;
    FORMATETC files{CF_HDROP, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL}, text{CF_UNICODETEXT, nullptr, DVASPECT_CONTENT, -1, TYMED_HGLOBAL};
    STGMEDIUM medium{};
    if (data->GetData(&files, &medium) == S_OK) {
        auto drop = static_cast<HDROP>(GlobalLock(medium.hGlobal));
        UINT count = drop ? DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0) : 0;
        for (UINT i = 0; i < count; ++i) {
            std::wstring path(DragQueryFileW(drop, i, nullptr, 0) + 1, L'\0');
            path.resize(DragQueryFileW(drop, i, path.data(), UINT(path.size())));
            platform_.dropped(EventType::DropFile, at, narrow(path.c_str(), int(path.size())));
        }
        if (drop) GlobalUnlock(medium.hGlobal);
        ReleaseStgMedium(&medium);
        *effect = DROPEFFECT_COPY;
    } else if (data->GetData(&text, &medium) == S_OK) {
        if (auto* chars = static_cast<const wchar_t*>(GlobalLock(medium.hGlobal))) {
            platform_.dropped(EventType::DropText, at, narrow(chars));
            GlobalUnlock(medium.hGlobal);
        }
        ReleaseStgMedium(&medium);
        *effect = DROPEFFECT_COPY;
    }
    platform_.dropped(EventType::DropComplete, at);
    return S_OK;
}

} // namespace

int runWin32(int argc, char* argv[]) {
    auto platform = std::make_unique<Win32Platform>();
    return platform->connect() ? platform->run(argc, argv) : -1;
}

} // namespace kompoz::platform
