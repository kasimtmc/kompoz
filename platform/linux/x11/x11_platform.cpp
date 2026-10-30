#include "x11_platform.hpp"

#include "../../app/app_core.hpp"
#include "../../common/system.hpp"
#include "../desktop_services.hpp"
#include "../../common/egl_window.hpp"
#include "../media_devices.hpp"

namespace kompoz::platform {
constexpr PointerPrecision kNoPointer = PointerPrecision::None;  // (named before X's None macro hides it)
}

#include "x11_dynamic.hpp"

#include <X11/cursorfont.h>
#ifdef KOMPOZ_GPU_VULKAN
#define VK_USE_PLATFORM_XLIB_KHR 1
#include <vulkan/vulkan.h>
#endif

#include <dlfcn.h>
#include <poll.h>
#include <sys/eventfd.h>
#include <sys/timerfd.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <clocale>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kompoz::platform {
namespace {

// The atoms the window speaks with, by their names.
struct Atoms {
    Atom WM_PROTOCOLS, WM_DELETE_WINDOW, _NET_WM_PING, _NET_WM_NAME, _NET_WM_PID, _NET_WM_WINDOW_TYPE, _NET_WM_WINDOW_TYPE_NORMAL,
        UTF8_STRING, CLIPBOARD, TARGETS, INCR, TEXT, TEXT_PLAIN_UTF8, TEXT_PLAIN, TEXT_URI_LIST, RESOURCE_MANAGER, KOMPOZ_SELECTION,
        XdndAware, XdndEnter, XdndPosition, XdndStatus, XdndLeave, XdndDrop, XdndFinished, XdndSelection, XdndTypeList, XdndActionCopy;
};
constexpr const char* kAtomNames[] = {
    "WM_PROTOCOLS", "WM_DELETE_WINDOW", "_NET_WM_PING", "_NET_WM_NAME", "_NET_WM_PID", "_NET_WM_WINDOW_TYPE", "_NET_WM_WINDOW_TYPE_NORMAL",
    "UTF8_STRING", "CLIPBOARD", "TARGETS", "INCR", "TEXT", "text/plain;charset=utf-8", "text/plain", "text/uri-list", "RESOURCE_MANAGER",
    "KOMPOZ_SELECTION", "XdndAware", "XdndEnter", "XdndPosition", "XdndStatus", "XdndLeave", "XdndDrop", "XdndFinished", "XdndSelection",
    "XdndTypeList", "XdndActionCopy"};
static_assert(sizeof(Atoms) == sizeof(kAtomNames) / sizeof(*kAtomNames) * sizeof(Atom));

// A scroll valuator of a device (XInput 2's smooth scrolling): its axis, how much is a notch, and
// where it was last (none: not known since the pointer came in).
struct ScrollAxis {
    bool vertical = true;
    double increment = 1;
    std::optional<double> last;
};

// An X error (a drag's source gone, a property not there) is the request's own failure, told
// where it is checked: not the process's end, as Xlib's default handler would make it.
int ignoreError(Display*, XErrorEvent*) { return 0; }

class X11Platform final : public Platform {
public:
    ~X11Platform() override {
        desktop_.stop();
        closeWindow();
        if (im_) XCloseIM(im_);
        for (::Cursor c : cursors_)
            if (c) XFreeCursor(display_, c);
        if (display_) XCloseDisplay(display_);
        for (int fd : {wakeFd_, timerFd_})
            if (fd >= 0) close(fd);
    }

    // The display, XInput 2.2 and the atoms; false where the app can't run here.
    bool connect() {
        if (!std::getenv("DISPLAY") || !x11::load()) return false;
        XInitThreads();  // (the GPU drivers' threads use the display too)
        display_ = XOpenDisplay(nullptr);
        if (!display_) return false;
        XSetErrorHandler(ignoreError);
        screen_ = DefaultScreen(display_);
        root_ = RootWindow(display_, screen_);
        int event = 0, error = 0, major = 2, minor = 4;
        if (!XQueryExtension(display_, "XInputExtension", &xiOpcode_, &event, &error) || XIQueryVersion(display_, &major, &minor) != Success ||
            major < 2 || (major == 2 && minor < 2))
            return false;
        xiMinor_ = minor;
        XInternAtoms(display_, const_cast<char**>(kAtomNames), int(std::size(kAtomNames)), False, reinterpret_cast<Atom*>(&atoms_));
        if (x11::fn.xrandr) {
            int rrError = 0;
            if (XRRQueryExtension(display_, &rrEvent_, &rrError)) XRRSelectInput(display_, root_, RRScreenChangeNotifyMask | RRCrtcChangeNotifyMask);
            else rrEvent_ = -1;
        }
        XSelectInput(display_, root_, PropertyChangeMask);  // (the desktop's resources: Xft.dpi)
        unsigned char mask[XIMaskLen(XI_LASTEVENT)] = {};
        XISetMask(mask, XI_HierarchyChanged);
        XIEventMask devices{XIAllDevices, int(sizeof mask), mask};
        XISelectEvents(display_, root_, &devices, 1);
        queryScrollAxes();
        XkbSetDetectableAutoRepeat(display_, True, nullptr);  // (a held key's repeats come as presses alone)
        dpiScale_ = desktopScale();
        wakeFd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        timerFd_ = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
        if (wakeFd_ >= 0) quitOnSignals(wakeFd_);
        return wakeFd_ >= 0 && timerFd_ >= 0;
    }

    int run(int argc, char* argv[]) {
        AppCore core{*this};
        if (!core.start(argc, argv)) return 1;
        bool needFrame = true;
        for (;;) {
            pump();
            desktop_.takeNotifications(events_);
            for (const Event& e : std::exchange(events_, {})) {
                if (e.type == EventType::Quit) quit_ = true;
                core.handle(e);
                needFrame = true;
            }
            if (quit_) break;
            if (needFrame || busy_) {
                // The pause until the frame is due is the host's for work between frames (a lazy
                // list's next item), and what comes meanwhile is handled before it.
                if (int64_t due = core.frameDue(), at = now(); due >= 0 && at < due) {
                    core.idle(due - at);
                    if (at = now(); at < due) {
                        if (!waitForEvents(int((due - at + 999'999) / 1'000'000))) break;
                        continue;
                    }
                }
                needFrame = false;
                Next next = core.frame();
                if (next.quit) break;
                busy_ = next.busy;
                armTimer(next.busy ? -1 : next.wakeAt);
                continue;
            }
            if (!waitForEvents(-1)) break;  // (the X server went away)
        }
        core.quit();
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
        width_ = std::max(1, int(std::lround(app.width * scale()))), height_ = std::max(1, int(std::lround(app.height * scale())));
        XSetWindowAttributes attributes{};
        attributes.event_mask = StructureNotifyMask | FocusChangeMask | ExposureMask | PropertyChangeMask | KeyPressMask | KeyReleaseMask;
        attributes.bit_gravity = NorthWestGravity;
        window_ = XCreateWindow(display_, root_, 0, 0, unsigned(width_), unsigned(height_), 0, CopyFromParent, InputOutput, CopyFromParent,
                                CWEventMask | CWBitGravity, &attributes);
        if (!window_) return failed("the window couldn't be made");
        // Its names, its process, what it answers (closing, the window manager's ping), its kind,
        // and drops (XDND 5).
        auto text = [this](Atom property, Atom type, const std::string& value) {
            XChangeProperty(display_, window_, property, type, 8, PropModeReplace, reinterpret_cast<const unsigned char*>(value.data()), int(value.size()));
        };
        text(atoms_._NET_WM_NAME, atoms_.UTF8_STRING, title_);
        bool ascii = std::all_of(title_.begin(), title_.end(), [](char c) { return uint8_t(c) < 0x80; });
        text(XA_WM_NAME, ascii ? Atom(XA_STRING) : atoms_.UTF8_STRING, title_);  // (older tools read a Latin-1 one)
        if (XClassHint* hint = XAllocClassHint()) {
            hint->res_name = const_cast<char*>("dev.kompoz.app"), hint->res_class = const_cast<char*>("dev.kompoz.app");
            XSetClassHint(display_, window_, hint);
            XFree(hint);
        }
        long pid = long(getpid());
        XChangeProperty(display_, window_, atoms_._NET_WM_PID, XA_CARDINAL, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&pid), 1);
        Atom protocols[] = {atoms_.WM_DELETE_WINDOW, atoms_._NET_WM_PING};
        XSetWMProtocols(display_, window_, protocols, 2);
        XChangeProperty(display_, window_, atoms_._NET_WM_WINDOW_TYPE, XA_ATOM, 32, PropModeReplace,
                        reinterpret_cast<unsigned char*>(&atoms_._NET_WM_WINDOW_TYPE_NORMAL), 1);
        long xdndVersion = 5;
        XChangeProperty(display_, window_, atoms_.XdndAware, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&xdndVersion), 1);
        // The pointer, touch and pinches through XInput 2 (the keyboard's events are the core
        // ones, which the input method filters).
        unsigned char mask[XIMaskLen(XI_LASTEVENT)] = {};
        for (int type : {XI_Motion, XI_ButtonPress, XI_ButtonRelease, XI_Enter, XI_Leave, XI_TouchBegin, XI_TouchUpdate, XI_TouchEnd})
            XISetMask(mask, type);
#ifdef XI_GesturePinchBegin
        if (xiMinor_ >= 4)
            for (int type : {XI_GesturePinchBegin, XI_GesturePinchUpdate, XI_GesturePinchEnd}) XISetMask(mask, type);
#endif
        XIEventMask pointer{XIAllMasterDevices, int(sizeof mask), mask};
        XISelectEvents(display_, window_, &pointer, 1);
        if (im_) makeInputContext();
        else openInputMethod();
        showCursor();
        XMapWindow(display_, window_);
        XFlush(display_);
        events_.push_back({.type = EventType::Shown});
        surface.drawableSize = [this](int* w, int* h) { *w = width_, *h = height_; };
        switch (backend) {
#ifdef KOMPOZ_GPU_VULKAN
        case gpu::Backend::Vulkan: {
            if (!vulkan_) vulkan_ = dlopen("libvulkan.so.1", RTLD_NOW | RTLD_LOCAL);
            auto getProcAddress = vulkan_ ? reinterpret_cast<PFN_vkGetInstanceProcAddr>(dlsym(vulkan_, "vkGetInstanceProcAddr")) : nullptr;
            if (!getProcAddress) return failed("no Vulkan loader");
            surface.vulkanGetInstanceProcAddr = reinterpret_cast<void* (*)(void*, const char*)>(getProcAddress);
            surface.vulkanInstanceExtensions = {VK_KHR_SURFACE_EXTENSION_NAME, VK_KHR_XLIB_SURFACE_EXTENSION_NAME};
            surface.createVulkanSurface = [this, getProcAddress](void* instance, uint64_t* out) {
                auto create = reinterpret_cast<PFN_vkCreateXlibSurfaceKHR>(getProcAddress(static_cast<VkInstance>(instance), "vkCreateXlibSurfaceKHR"));
                VkXlibSurfaceCreateInfoKHR info{.sType = VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR, .dpy = display_, .window = window_};
                VkSurfaceKHR made = VK_NULL_HANDLE;
                if (!create || create(static_cast<VkInstance>(instance), &info, nullptr, &made) != VK_SUCCESS) return false;
                std::memcpy(out, &made, sizeof made);
                return true;
            };
            return true;
        }
#endif
        case gpu::Backend::GLES:
            // (Swap interval 1: the swap waits for the display's beat.)
            return egl_.open({.platform = EglWindow::kX11, .display = display_, .window = &window_,
                              .visual = int(DefaultVisual(display_, screen_)->visualid), .alpha = false, .swapInterval = 1,
                              .debug = option("KOMPOZ_GPU_VALIDATION") == "1"},
                             surface, error) ||
                   failed(*error);
        default: break;
        }
        return failed(std::string("no ") + gpu::backendName(backend) + " here");
    }

    void closeWindow() override {
        egl_.close();
        if (ic_) XDestroyIC(std::exchange(ic_, nullptr));
        if (window_) XDestroyWindow(display_, std::exchange(window_, 0));
        if (vulkan_) dlclose(std::exchange(vulkan_, nullptr));
    }

    void showError(const std::string& message) override { platform::log(message); }

    // Points are the window's pixels; the scale is the desktop's (Xft.dpi / 96); KOMPOZ_SCALE and
    // KOMPOZ_FONT_SCALE stand in for them.
    Metrics metrics() const override {
        Metrics m;
        m.pixelWidth = m.pointWidth = width_, m.pixelHeight = m.pointHeight = height_;
        m.scale = float(scale());
        m.pointToDp = 1 / m.scale;
        if (auto v = option("KOMPOZ_FONT_SCALE")) m.fontScale = std::max(0.5f, float(std::atof(v->c_str())));
        m.refreshRate = refreshRate_;
        m.safeArea = Rect{0, 0, float(width_), float(height_)};
        m.darkTheme = desktop_.darkTheme();
        m.language = preferredLanguage();
        return m;
    }
    void metricsUpdated(ComposeHost&) override { desktop_.metricsUpdated(); }
    void pixelSize(int* width, int* height) const override { *width = width_, *height = height_; }
    void setWindowSize(int width, int height) override {
        if (window_) XResizeWindow(display_, window_, unsigned(std::max(1, width)), unsigned(std::max(1, height)));
    }

    // A mouse is a fine pointer, a touch screen alone a coarse one; the microphone and camera as
    // probed (media_devices.hpp).
    MediaInfo media(bool probe) override {
        if (probe && !probed_) {
            probed_ = true;
            hasMicrophone_ = hasMicrophone(), hasCamera_ = hasCamera();
        }
        MediaInfo info;
        info.pointerPrecision = hasMouse_ ? PointerPrecision::Fine : hasTouch_ ? PointerPrecision::Coarse : kNoPointer;
        info.physicalKeyboard = true;
        info.hasMicrophone = hasMicrophone_, info.hasCamera = hasCamera_;
        return info;
    }

    void attach(ComposeHost&, ComposeHost::Callbacks& callbacks) override {
        X11Platform* self = this;
        desktop_.start(title_, [self] { self->wake(); });
        callbacks.wake = [self] { self->wake(); };
        callbacks.textInput = [self](bool active, Rect area, const TextEditState&, const ImeOptions&) {
            float k = float(self->scale());  // dp -> points
            self->textActive_ = active;
            self->textSpot_ = {short(std::lround(area.x * k)), short(std::lround((area.y + area.height) * k))};
            self->tellInputMethod();
        };
        callbacks.cursor = [self](Cursor cursor) {
            self->cursor_ = cursor;
            self->showCursor();
        };
        callbacks.getClipboard = [self] { return self->readClipboard(); };
        callbacks.setClipboard = [self](const std::string& text) { self->writeClipboard(text); };
        callbacks.openUri = [](const std::string& uri) { openUri(uri); };
    }

    void handled(ComposeHost& h, const Event& e) override { desktop_.handled(h, e); }

    void composed(ComposeHost& h, bool redraw, int64_t frameTime) override {
        // keepScreenOn: the screen saver held off while it is laid out.
        if (bool on = h.keepScreenOn(); on != screenKeptOn_) {
            screenKeptOn_ = on;
            desktop_.keepScreenOn(on, title_);
        }
        desktop_.composed(h, redraw, frameTime, {float(x_), float(y_), float(width_), float(height_)}, float(scale()), title_);
    }

    bool wantsFrames() const override { return desktop_.wantsFrames(); }

private:
    Display* display_ = nullptr;
    int screen_ = 0;
    Window root_ = 0, window_ = 0;
    Atoms atoms_{};
    int xiOpcode_ = 0, xiMinor_ = 0, rrEvent_ = -1;
    std::string title_ = "Kompoz";
    int width_ = 1, height_ = 1;  // pixels
    int x_ = 0, y_ = 0;           // on the screen
    double dpiScale_ = 1;
    float refreshRate_ = 60;
    EglWindow egl_;
    void* vulkan_ = nullptr;

    double pointerX_ = 0, pointerY_ = 0;
    std::map<int, std::map<int, ScrollAxis>> scrollAxes_;  // by device, then valuator
    bool hasMouse_ = true, hasTouch_ = false;
    double pinchScale_ = 1;
    Time lastTime_ = CurrentTime;  // the last input event's (what selections are owned and asked for at)
    Cursor cursor_ = Cursor::Default;
    std::array<::Cursor, 9> cursors_{};  // X's, as made for each of Kompoz's
    XIM im_ = nullptr;
    XIC ic_ = nullptr;
    bool textActive_ = false, focused_ = false, spotStyle_ = false;
    XPoint textSpot_{};  // where the input method shows what is being composed: under the field's start

    std::string clipboard_;
    bool ownsClipboard_ = false;
    // A large clipboard sent in parts (INCR): to which window's property, from where.
    struct Transfer {
        Window requestor;
        Atom property, target;
        size_t offset;
    };
    std::vector<Transfer> transfers_;

    // A drag from another app over the window (XDND): its source, the type taken, where it is.
    Window dragSource_ = 0;
    Atom dragType_ = None;
    bool dragBegun_ = false;
    double dragX_ = 0, dragY_ = 0;

    int wakeFd_ = -1, timerFd_ = -1;
    std::deque<Event> events_;
    bool quit_ = false, busy_ = false;
    bool probed_ = false, hasMicrophone_ = false, hasCamera_ = false, screenKeptOn_ = false;
    DesktopServices desktop_;

    double scale() const {
        if (auto v = option("KOMPOZ_SCALE"); v && std::atof(v->c_str()) > 0) return std::atof(v->c_str());
        return dpiScale_;
    }

    // The desktop's scale: Xft.dpi in the root window's resources (what KDE and GNOME set), / 96.
    double desktopScale() {
        std::string resources = rootProperty(atoms_.RESOURCE_MANAGER);
        for (size_t at = resources.find("Xft.dpi:"); at != std::string::npos; at = resources.find("Xft.dpi:", at + 1))
            if (at == 0 || resources[at - 1] == '\n') {
                double dpi = std::atof(resources.c_str() + at + 8);
                return dpi > 0 ? dpi / 96 : 1;
            }
        return 1;
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

    // The X events that came, each through the input method first.
    void pump() {
        while (XPending(display_)) {
            XEvent e;
            XNextEvent(display_, &e);
            if (XFilterEvent(&e, None)) continue;
            handle(e);
        }
    }

    // Up to `timeout` ms (-1: until one) for an X event, a wake or a timer. False if the
    // connection broke.
    bool waitForEvents(int timeout) {
        XFlush(display_);
        if (XPending(display_)) return true;
        pollfd fds[] = {{ConnectionNumber(display_), POLLIN, 0}, {wakeFd_, POLLIN, 0}, {timerFd_, POLLIN, 0}};
        poll(fds, 3, timeout);
        if (fds[0].revents & (POLLERR | POLLHUP)) return false;
        uint64_t count = 0;
        for (int i : {1, 2})
            if (fds[i].revents & POLLIN) {
                (void)!read(fds[i].fd, &count, sizeof count);
                events_.push_back({.type = EventType::Wake});
            }
        return true;
    }

    // An event from the X server, or for it to answer.
    void handle(XEvent& e) {
        if (e.type == GenericEvent && e.xcookie.extension == xiOpcode_) {
            if (XGetEventData(display_, &e.xcookie)) {
                input(e.xcookie.evtype, e.xcookie.data);
                XFreeEventData(display_, &e.xcookie);
            }
            return;
        }
        if (rrEvent_ >= 0 && (e.type == rrEvent_ + RRScreenChangeNotify || e.type == rrEvent_ + RRNotify)) return displaysChanged();
        switch (e.type) {
        case KeyPress:
        case KeyRelease: key(e.xkey); break;
        case MappingNotify:
            if (e.xmapping.request == MappingKeyboard || e.xmapping.request == MappingModifier) XRefreshKeyboardMapping(&e.xmapping);
            break;
        case FocusIn:
        case FocusOut:
            if (e.xfocus.mode == NotifyGrab || e.xfocus.mode == NotifyUngrab || e.xfocus.detail == NotifyInferior || e.xfocus.detail == NotifyPointer)
                break;
            focused_ = e.type == FocusIn;
            tellInputMethod();
            events_.push_back({.type = focused_ ? EventType::FocusGained : EventType::FocusLost});
            break;
        case ConfigureNotify:
            if (e.xconfigure.window != window_) break;
            if (e.xconfigure.width != width_ || e.xconfigure.height != height_) {
                width_ = e.xconfigure.width, height_ = e.xconfigure.height;
                events_.push_back({.type = EventType::Resized});
            }
            moved(e.xconfigure);
            break;
        case Expose:
            if (e.xexpose.count == 0) events_.push_back({.type = EventType::Exposed});
            break;
        case PropertyNotify:
            if (e.xproperty.window == root_ && e.xproperty.atom == atoms_.RESOURCE_MANAGER) {
                if (double s = desktopScale(); s != dpiScale_) {
                    dpiScale_ = s;
                    events_.push_back({.type = EventType::MetricsChanged});
                }
            } else if (e.xproperty.state == PropertyDelete) {
                sendNextPart(e.xproperty.window, e.xproperty.atom);
            }
            break;
        case ClientMessage: message(e.xclient); break;
        case SelectionRequest: answerSelection(e.xselectionrequest); break;
        case SelectionClear:
            if (e.xselectionclear.selection == atoms_.CLIPBOARD) ownsClipboard_ = false;
            break;
        default: break;
        }
    }

    // ---- Sizes and displays --------------------------------------------------------------

    // Where the window is on the screen (a window manager's own configure tells it; else asked),
    // and the refresh rate of the display it is on.
    void moved(const XConfigureEvent& c) {
        if (c.send_event) {
            x_ = c.x, y_ = c.y;
        } else {
            Window child = 0;
            XTranslateCoordinates(display_, window_, root_, 0, 0, &x_, &y_, &child);
        }
        displaysChanged();
    }

    void displaysChanged() {
        float rate = displayRefreshRate();
        if (rate != refreshRate_) {
            refreshRate_ = rate;
            events_.push_back({.type = EventType::MetricsChanged});
        }
    }

    // The refresh rate of the display under the window's middle (XRandR's mode on its CRTC); 60
    // where XRandR doesn't tell.
    float displayRefreshRate() const {
        if (!x11::fn.xrandr || rrEvent_ < 0) return 60;
        XRRScreenResources* resources = XRRGetScreenResourcesCurrent(display_, root_);
        if (!resources) return 60;
        float best = 0;
        int cx = x_ + width_ / 2, cy = y_ + height_ / 2;
        for (int i = 0; i < resources->ncrtc; ++i) {
            XRRCrtcInfo* crtc = XRRGetCrtcInfo(display_, resources, resources->crtcs[i]);
            if (!crtc) continue;
            bool under = crtc->mode != None && cx >= crtc->x && cy >= crtc->y && cx < crtc->x + int(crtc->width) && cy < crtc->y + int(crtc->height);
            for (int m = 0; under && m < resources->nmode; ++m) {
                const XRRModeInfo& mode = resources->modes[m];
                if (mode.id != crtc->mode || !mode.hTotal || !mode.vTotal) continue;
                double lines = double(mode.vTotal) * ((mode.modeFlags & RR_DoubleScan) ? 2 : 1) / ((mode.modeFlags & RR_Interlace) ? 2 : 1);
                best = float(double(mode.dotClock) / (double(mode.hTotal) * lines));
            }
            XRRFreeCrtcInfo(crtc);
        }
        XRRFreeScreenResources(resources);
        return best > 0 ? best : 60;
    }

    // ---- Input ----------------------------------------------------------------------------

    Event at(EventType type, double x, double y, const XIModifierState& mods) const {
        Event e{.type = type, .time = monotonicNow(), .x = float(x), .y = float(y)};
        e.modifiers = modifiersOf(unsigned(mods.effective));
        return e;
    }
    static KeyModifiers modifiersOf(unsigned state) {
        return {(state & ShiftMask) != 0, (state & ControlMask) != 0 || (state & Mod4Mask) != 0, (state & Mod1Mask) != 0};
    }

    // The devices' scroll valuators (XInput 2.1's smooth scrolling), whether there is a mouse and
    // a touch screen.
    void queryScrollAxes() {
        scrollAxes_.clear();
        hasMouse_ = hasTouch_ = false;
        int count = 0;
        XIDeviceInfo* devices = XIQueryDevice(display_, XIAllDevices, &count);
        for (int i = 0; i < count; ++i) {
            const XIDeviceInfo& d = devices[i];
            bool touch = false;
            for (int c = 0; c < d.num_classes; ++c) {
                if (d.classes[c]->type == XIScrollClass) {
                    const auto* s = reinterpret_cast<const XIScrollClassInfo*>(d.classes[c]);
                    scrollAxes_[d.deviceid][s->number] = {.vertical = s->scroll_type == XIScrollTypeVertical, .increment = s->increment != 0 ? s->increment : 1};
                }
                if (d.classes[c]->type == XITouchClass && reinterpret_cast<const XITouchClassInfo*>(d.classes[c])->mode == XIDirectTouch) touch = true;
            }
            if (d.use == XISlavePointer) (touch ? hasTouch_ : hasMouse_) = true;
        }
        if (devices) XIFreeDeviceInfo(devices);
    }

    // XInput 2's events: the pointer, its wheel, touch, pinches, the devices.
    void input(int type, void* data) {
        if (type == XI_HierarchyChanged) {
            queryScrollAxes();
            events_.push_back({.type = EventType::DevicesChanged});
            return;
        }
#ifdef XI_GesturePinchBegin
        if (type == XI_GesturePinchBegin || type == XI_GesturePinchUpdate || type == XI_GesturePinchEnd) {
            const auto* g = static_cast<const XIGesturePinchEvent*>(data);
            if (type == XI_GesturePinchBegin) {
                pinchScale_ = 1;
                events_.push_back(at(EventType::PinchBegin, g->event_x, g->event_y, g->mods));
            } else if (type == XI_GesturePinchUpdate) {  // (the scale since the pinch began: each step the change since the last)
                Event e = at(EventType::Pinch, g->event_x, g->event_y, g->mods);
                e.scale = float(g->scale / std::max(pinchScale_, 1e-3));
                pinchScale_ = g->scale;
                events_.push_back(e);
            }
            return;
        }
#endif
        const auto* d = static_cast<const XIDeviceEvent*>(data);
        if (type == XI_Enter || type == XI_Leave) {
            const auto* c = static_cast<const XIEnterEvent*>(data);
            for (auto& [valuator, axis] : scrollAxes_[c->sourceid]) axis.last.reset();  // (it may have scrolled elsewhere)
            if (c->mode != XINotifyNormal && c->mode != XINotifyUngrab) return;
            if (type == XI_Leave) events_.push_back({.type = EventType::PointerLeft});
            else events_.push_back(at(EventType::MouseMove, pointerX_ = c->event_x, pointerY_ = c->event_y, c->mods));
            return;
        }
        lastTime_ = d->time;
        bool emulated = (d->flags & XIPointerEmulated) != 0;  // (a touch's own pointer, or the wheel's buttons for smooth scrolling)
        switch (type) {
        case XI_Motion: {
            if (emulated) return;
            pointerX_ = d->event_x, pointerY_ = d->event_y;
            events_.push_back(at(EventType::MouseMove, pointerX_, pointerY_, d->mods));
            // The wheel, smooth: each scroll valuator's change since the last, in notches (its
            // increment); up and right positive.
            double dx = 0, dy = 0;
            const double* value = d->valuators.values;
            std::map<int, ScrollAxis>& axes = scrollAxes_[d->sourceid];
            for (int i = 0; i < d->valuators.mask_len * 8; ++i) {
                if (!XIMaskIsSet(d->valuators.mask, i)) continue;
                double v = *value++;
                auto axis = axes.find(i);
                if (axis == axes.end()) continue;
                if (axis->second.last) {
                    double notches = (v - *axis->second.last) / axis->second.increment;
                    (axis->second.vertical ? dy : dx) += axis->second.vertical ? -notches : notches;
                }
                axis->second.last = v;
            }
            if (dx != 0 || dy != 0) {
                Event e = at(EventType::Wheel, pointerX_, pointerY_, d->mods);
                e.dx = float(dx), e.dy = float(dy);
                events_.push_back(e);
            }
            break;
        }
        case XI_ButtonPress:
        case XI_ButtonRelease: {
            if (d->detail >= 4 && d->detail <= 7) {  // the wheel's notches, where it isn't smooth
                if (emulated || type == XI_ButtonRelease) return;
                Event e = at(EventType::Wheel, d->event_x, d->event_y, d->mods);
                e.dy = d->detail == 4 ? 1.f : d->detail == 5 ? -1.f : 0.f;
                e.dx = d->detail == 6 ? -1.f : d->detail == 7 ? 1.f : 0.f;
                events_.push_back(e);
                return;
            }
            if (emulated) return;
            Event e = at(type == XI_ButtonPress ? EventType::MouseDown : EventType::MouseUp, d->event_x, d->event_y, d->mods);
            switch (d->detail) {
            case 1: e.button = MouseButton::Left; break;
            case 2: e.button = MouseButton::Middle; break;
            case 3: e.button = MouseButton::Right; break;
            case 8: e.button = MouseButton::Back; break;
            case 9: e.button = MouseButton::Forward; break;
            default: return;
            }
            events_.push_back(e);
            break;
        }
        case XI_TouchBegin:
        case XI_TouchUpdate:
        case XI_TouchEnd: {
            Event e = at(type == XI_TouchBegin ? EventType::TouchDown : type == XI_TouchEnd ? EventType::TouchUp : EventType::TouchMove,
                         d->event_x, d->event_y, d->mods);
            e.pointer = d->detail;
            events_.push_back(e);
            break;
        }
        default: break;
        }
    }

    // A key pressed (or repeated) or let go: the key, and the text it types while a field takes it
    // (an input method's commits come as presses of no key).
    void key(XKeyEvent& k) {
        lastTime_ = k.time;
        KeyModifiers mods = modifiersOf(k.state);
        bool down = k.type == KeyPress;
        if (k.keycode != 0) events_.push_back(Event{.type = down ? EventType::KeyDown : EventType::KeyUp, .key = keyOf(uint32_t(XLookupKeysym(&k, 0))), .modifiers = mods});
        if (!down || !textActive_ || mods.ctrl) return;
        std::string text(64, '\0');
        KeySym sym = 0;
        Status status = XLookupNone;
        if (ic_) {
            int n = Xutf8LookupString(ic_, &k, text.data(), int(text.size()), &sym, &status);
            if (status == XBufferOverflow) {
                text.resize(size_t(n));
                n = Xutf8LookupString(ic_, &k, text.data(), int(text.size()), &sym, &status);
            }
            text.resize(status == XLookupChars || status == XLookupBoth ? size_t(std::max(n, 0)) : 0);
        } else {  // (no input method: Latin-1)
            int n = XLookupString(&k, text.data(), int(text.size()), &sym, nullptr);
            std::string utf8;
            for (int i = 0; i < n; ++i) {
                auto c = uint8_t(text[size_t(i)]);
                if (c < 0x80) utf8 += char(c);
                else utf8 += char(0xc0 | (c >> 6)), utf8 += char(0x80 | (c & 0x3f));
            }
            text = utf8;
        }
        if (!text.empty() && uint8_t(text[0]) >= 0x20 && text[0] != 0x7f) events_.push_back({.type = EventType::Text, .text = text});
    }

    // The input method (XIM), as the desktop's locale has it (else X's own, for compose keys):
    // opened now or once it is there, and opened again if it goes.
    void openInputMethod() {
        std::string locale = std::setlocale(LC_ALL, nullptr) ? std::setlocale(LC_ALL, nullptr) : "C";
        std::setlocale(LC_ALL, "");  // (the locale and modifiers set only while it opens, as SDL does)
        XSetLocaleModifiers("");
        im_ = XOpenIM(display_, nullptr, nullptr, nullptr);
        if (!im_) {
            XSetLocaleModifiers("@im=none");
            im_ = XOpenIM(display_, nullptr, nullptr, nullptr);
        }
        std::setlocale(LC_ALL, locale.c_str());
        if (!im_) {
            XRegisterIMInstantiateCallback(display_, nullptr, nullptr, nullptr, imInstantiated, reinterpret_cast<XPointer>(this));
            return;
        }
        XIMCallback destroyed{reinterpret_cast<XPointer>(this), imDestroyed};
        XSetIMValues(im_, XNDestroyCallback, &destroyed, nullptr);
        if (window_) makeInputContext();
    }

    // The window's input context: what is being composed shown by the input method at the field
    // where it can be told where (over the spot), else where it shows it.
    void makeInputContext() {
        XIMStyles* styles = nullptr;
        XIMStyle position = XIMPreeditPosition | XIMStatusNothing;
        spotStyle_ = false;
        if (!XGetIMValues(im_, XNQueryInputStyle, &styles, nullptr) && styles) {
            for (unsigned short i = 0; i < styles->count_styles; ++i) spotStyle_ = spotStyle_ || styles->supported_styles[i] == position;
            XFree(styles);
        }
        if (spotStyle_) {
            XVaNestedList spot = XVaCreateNestedList(0, XNSpotLocation, &textSpot_, nullptr);
            ic_ = XCreateIC(im_, XNInputStyle, position, XNClientWindow, window_, XNFocusWindow, window_, XNPreeditAttributes, spot, nullptr);
            XFree(spot);
        } else {
            ic_ = XCreateIC(im_, XNInputStyle, XIMPreeditNothing | XIMStatusNothing, XNClientWindow, window_, XNFocusWindow, window_, nullptr);
        }
        tellInputMethod();
    }
    static void imInstantiated(Display* display, XPointer self, XPointer) {
        auto* p = reinterpret_cast<X11Platform*>(self);
        XUnregisterIMInstantiateCallback(display, nullptr, nullptr, nullptr, imInstantiated, self);
        p->openInputMethod();
    }
    static void imDestroyed(XIM, XPointer self, XPointer) {
        auto* p = reinterpret_cast<X11Platform*>(self);
        p->im_ = nullptr, p->ic_ = nullptr;
        XRegisterIMInstantiateCallback(p->display_, nullptr, nullptr, nullptr, imInstantiated, self);
    }

    // The input context focused while a field takes typing in the focused window, told where.
    void tellInputMethod() {
        if (!ic_) return;
        if (textActive_ && focused_) {
            if (spotStyle_) {
                XVaNestedList spot = XVaCreateNestedList(0, XNSpotLocation, &textSpot_, nullptr);
                XSetICValues(ic_, XNPreeditAttributes, spot, nullptr);
                XFree(spot);
            }
            XSetICFocus(ic_);
        } else {
            XUnsetICFocus(ic_);
        }
    }

    // The desktop theme's cursor for Kompoz's (libXcursor), else X's own.
    void showCursor() {
        if (!window_) return;
        size_t i = std::min(size_t(cursor_), cursors_.size() - 1);
        if (!cursors_[i]) {
            static constexpr const char* names[] = {"default", "pointer", "text", "crosshair", "move", "ew-resize", "ns-resize", "not-allowed", "wait"};
            static constexpr unsigned shapes[] = {XC_left_ptr, XC_hand2, XC_xterm, XC_crosshair, XC_fleur, XC_sb_h_double_arrow, XC_sb_v_double_arrow,
                                                  XC_X_cursor, XC_watch};
            if (x11::fn.xcursor) cursors_[i] = XcursorLibraryLoadCursor(display_, names[i]);
            if (!cursors_[i]) cursors_[i] = XCreateFontCursor(display_, shapes[i]);
        }
        XDefineCursor(display_, window_, cursors_[i]);
        XFlush(display_);
    }

    // ---- Selections: the clipboard and drops --------------------------------------------

    bool isText(Atom type) const {
        return type == atoms_.UTF8_STRING || type == XA_STRING || type == atoms_.TEXT || type == atoms_.TEXT_PLAIN_UTF8 || type == atoms_.TEXT_PLAIN;
    }

    // A property of a window as bytes (32-bit items as longs, as Xlib gives them), and its type;
    // deleted after if asked (a selection's answer, a part of one).
    std::optional<std::string> property(Window window, Atom name, Atom* type = nullptr, bool remove = false) {
        Atom actual = None;
        int format = 0;
        unsigned long count = 0, after = 0;
        unsigned char* data = nullptr;
        if (XGetWindowProperty(display_, window, name, 0, 0x1fffffff, remove ? True : False, AnyPropertyType, &actual, &format, &count, &after, &data) !=
                Success ||
            actual == None) {
            if (data) XFree(data);
            return std::nullopt;
        }
        size_t unit = format == 32 ? sizeof(long) : size_t(format / 8);
        std::string bytes(reinterpret_cast<const char*>(data), count * unit);
        XFree(data);
        if (type) *type = actual;
        return bytes;
    }
    std::string rootProperty(Atom name) { return property(root_, name).value_or(""); }

    // An event matching `match` waited for (what else comes stays queued), a second at most.
    template <class Match> bool await(XEvent& out, Match match) {
        int64_t deadline = monotonicNow() + 1'000'000'000;
        auto predicate = [](Display*, XEvent* e, XPointer m) -> Bool { return (*reinterpret_cast<Match*>(m))(*e) ? True : False; };
        for (;;) {
            if (XCheckIfEvent(display_, &out, predicate, reinterpret_cast<XPointer>(&match))) return true;
            int64_t left = deadline - monotonicNow();
            if (left <= 0) return false;
            pollfd fd{ConnectionNumber(display_), POLLIN, 0};
            poll(&fd, 1, int(left / 1'000'000) + 1);
        }
    }

    // `selection` asked for as `target` (into our KOMPOZ_SELECTION) and its answer read, in parts
    // if it comes in parts (INCR); none: refused, or no answer within a second.
    std::optional<std::string> convert(Atom selection, Atom target, Time time) {
        XDeleteProperty(display_, window_, atoms_.KOMPOZ_SELECTION);
        XConvertSelection(display_, selection, target, atoms_.KOMPOZ_SELECTION, window_, time);
        XEvent e;
        if (!await(e, [&](const XEvent& x) { return x.type == SelectionNotify && x.xselection.requestor == window_ && x.xselection.selection == selection; }) ||
            e.xselection.property == None)
            return std::nullopt;
        Atom type = None;
        std::optional<std::string> data = property(window_, atoms_.KOMPOZ_SELECTION, &type, true);
        if (!data || type != atoms_.INCR) return data;
        std::string all;  // (INCR: each part a new value of the property, deleted once read; an empty one ends it)
        for (;;) {
            auto newValue = [&](const XEvent& x) {
                return x.type == PropertyNotify && x.xproperty.window == window_ && x.xproperty.atom == atoms_.KOMPOZ_SELECTION &&
                       x.xproperty.state == PropertyNewValue;
            };
            if (!await(e, newValue)) return std::nullopt;
            std::optional<std::string> part = property(window_, atoms_.KOMPOZ_SELECTION, &type, true);
            if (!part || part->empty()) return all;
            all += *part;
        }
    }

    std::string readClipboard() {
        Window owner = XGetSelectionOwner(display_, atoms_.CLIPBOARD);
        if (owner == window_ && ownsClipboard_) return clipboard_;
        if (owner == None) return {};
        for (Atom target : {atoms_.UTF8_STRING, Atom(XA_STRING)})
            if (std::optional<std::string> text = convert(atoms_.CLIPBOARD, target, lastTime_)) return *text;
        return {};
    }

    void writeClipboard(const std::string& text) {
        clipboard_ = text;
        XSetSelectionOwner(display_, atoms_.CLIPBOARD, window_, lastTime_);
        ownsClipboard_ = XGetSelectionOwner(display_, atoms_.CLIPBOARD) == window_;
    }

    // Another app asking for the clipboard we hold: what types it can have, or the text (in parts
    // when it is larger than a request can carry).
    void answerSelection(const XSelectionRequestEvent& r) {
        XSelectionEvent reply{};
        reply.type = SelectionNotify, reply.display = r.display, reply.requestor = r.requestor, reply.selection = r.selection;
        reply.target = r.target, reply.time = r.time, reply.property = None;
        Atom into = r.property != None ? r.property : r.target;  // (an obsolete client's)
        if (r.selection == atoms_.CLIPBOARD && ownsClipboard_) {
            if (r.target == atoms_.TARGETS) {
                Atom targets[] = {atoms_.TARGETS, atoms_.UTF8_STRING, XA_STRING, atoms_.TEXT, atoms_.TEXT_PLAIN_UTF8, atoms_.TEXT_PLAIN};
                XChangeProperty(display_, r.requestor, into, XA_ATOM, 32, PropModeReplace, reinterpret_cast<unsigned char*>(targets), int(std::size(targets)));
                reply.property = into;
            } else if (isText(r.target)) {
                Atom type = r.target == atoms_.TEXT ? atoms_.UTF8_STRING : r.target;
                if (clipboard_.size() > partSize()) {
                    XSelectInput(display_, r.requestor, PropertyChangeMask);
                    long size = long(clipboard_.size());
                    XChangeProperty(display_, r.requestor, into, atoms_.INCR, 32, PropModeReplace, reinterpret_cast<unsigned char*>(&size), 1);
                    transfers_.push_back({r.requestor, into, type, 0});
                } else {
                    XChangeProperty(display_, r.requestor, into, type, 8, PropModeReplace, reinterpret_cast<const unsigned char*>(clipboard_.data()),
                                    int(clipboard_.size()));
                }
                reply.property = into;
            }
        }
        XSendEvent(display_, r.requestor, False, 0, reinterpret_cast<XEvent*>(&reply));
        XFlush(display_);
    }

    size_t partSize() const {
        long most = XExtendedMaxRequestSize(display_);
        if (most <= 0) most = XMaxRequestSize(display_);
        return std::min<size_t>(size_t(most) * 4 - 256, size_t(1) << 18);
    }

    // The requestor took the last part of a transfer (deleted the property): the next one, and an
    // empty one after the last.
    void sendNextPart(Window window, Atom property) {
        for (auto t = transfers_.begin(); t != transfers_.end(); ++t) {
            if (t->requestor != window || t->property != property) continue;
            size_t n = std::min(partSize(), clipboard_.size() - std::min(t->offset, clipboard_.size()));
            XChangeProperty(display_, window, property, t->target, 8, PropModeReplace,
                            reinterpret_cast<const unsigned char*>(clipboard_.data() + t->offset), int(n));
            t->offset += n;
            if (n == 0) {
                XSelectInput(display_, window, NoEventMask);
                transfers_.erase(t);
            }
            XFlush(display_);
            return;
        }
    }

    // The window manager's messages and drags from other apps (XDND).
    void message(const XClientMessageEvent& m) {
        if (m.message_type == atoms_.WM_PROTOCOLS) {
            Atom what = Atom(m.data.l[0]);
            if (what == atoms_.WM_DELETE_WINDOW) {
                events_.push_back({.type = EventType::Quit});
            } else if (what == atoms_._NET_WM_PING) {
                XClientMessageEvent pong = m;
                pong.window = root_;
                XSendEvent(display_, root_, False, SubstructureNotifyMask | SubstructureRedirectMask, reinterpret_cast<XEvent*>(&pong));
            }
            return;
        }
        if (m.message_type == atoms_.XdndEnter) {
            // The types it offers (three here, or its XdndTypeList): files, else UTF-8 text.
            dragSource_ = Window(m.data.l[0]);
            std::vector<Atom> types;
            if (m.data.l[1] & 1) {
                if (std::optional<std::string> list = property(dragSource_, atoms_.XdndTypeList))
                    types.assign(reinterpret_cast<const long*>(list->data()), reinterpret_cast<const long*>(list->data() + list->size()));
            } else {
                for (int i = 2; i < 5; ++i)
                    if (m.data.l[i]) types.push_back(Atom(m.data.l[i]));
            }
            dragType_ = None;
            for (Atom wanted : {atoms_.TEXT_URI_LIST, atoms_.TEXT_PLAIN_UTF8, atoms_.UTF8_STRING, atoms_.TEXT_PLAIN})
                if (dragType_ == None && std::find(types.begin(), types.end(), wanted) != types.end()) dragType_ = wanted;
            dragBegun_ = false;
        } else if (m.message_type == atoms_.XdndPosition && Window(m.data.l[0]) == dragSource_) {
            int rootX = int((m.data.l[2] >> 16) & 0xffff), rootY = int(m.data.l[2] & 0xffff), x = 0, y = 0;
            Window child = 0;
            XTranslateCoordinates(display_, root_, window_, rootX, rootY, &x, &y, &child);
            dragX_ = x, dragY_ = y;
            if (!dragBegun_) events_.push_back({.type = EventType::DropBegin});
            dragBegun_ = true;
            Event move{.type = EventType::DropMove, .time = monotonicNow(), .x = float(x), .y = float(y)};
            events_.push_back(move);
            dragReply(atoms_.XdndStatus, dragType_ != None ? 1 | 2 : 0, dragType_ != None ? atoms_.XdndActionCopy : None);  // (1: taken; 2: tell each move)
        } else if (m.message_type == atoms_.XdndLeave && Window(m.data.l[0]) == dragSource_) {
            if (dragBegun_) events_.push_back({.type = EventType::DropComplete});
            dragSource_ = 0, dragBegun_ = false;
        } else if (m.message_type == atoms_.XdndDrop && Window(m.data.l[0]) == dragSource_) {
            std::optional<std::string> data = dragType_ != None ? convert(atoms_.XdndSelection, dragType_, Time(m.data.l[2])) : std::nullopt;
            if (data && dragType_ == atoms_.TEXT_URI_LIST) {
                for (const std::string& path : filesInUriList(*data)) {
                    Event e{.type = EventType::DropFile, .time = monotonicNow(), .x = float(dragX_), .y = float(dragY_)};
                    e.text = path;
                    events_.push_back(e);
                }
            } else if (data) {
                Event e{.type = EventType::DropText, .time = monotonicNow(), .x = float(dragX_), .y = float(dragY_)};
                e.text = *data;
                events_.push_back(e);
            }
            dragReply(atoms_.XdndFinished, data ? 1 : 0, data ? atoms_.XdndActionCopy : None);
            events_.push_back({.type = EventType::DropComplete});
            dragSource_ = 0, dragBegun_ = false;
        }
    }

    // A message to the drag's source: whether we take it (XdndStatus) or took it (XdndFinished).
    void dragReply(Atom type, long flags, Atom action) {
        XClientMessageEvent r{};
        r.type = ClientMessage, r.display = display_, r.window = dragSource_, r.message_type = type, r.format = 32;
        r.data.l[0] = long(window_), r.data.l[1] = flags;
        r.data.l[type == atoms_.XdndStatus ? 4 : 2] = long(action);
        XSendEvent(display_, dragSource_, False, NoEventMask, reinterpret_cast<XEvent*>(&r));
        XFlush(display_);
    }
};

} // namespace

int runX11(int argc, char* argv[]) {
    if (const char* forced = std::getenv("KOMPOZ_PLATFORM"); forced && std::string_view(forced) == "sdl") return -1;
    return withDbus([&] {
        auto platform = std::make_unique<X11Platform>();
        return platform->connect() ? platform->run(argc, argv) : -1;
    });
}

} // namespace kompoz::platform
