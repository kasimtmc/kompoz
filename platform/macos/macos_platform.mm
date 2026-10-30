#include "macos_platform.hpp"

#include "../app/app_core.hpp"
#include "../common/system.hpp"

#import <AVFoundation/AVFoundation.h>
#import <AppKit/AppKit.h>
#import <QuartzCore/CAMetalLayer.h>
#include <IOKit/pwr_mgt/IOPMLib.h>

#include <csignal>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <deque>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace kompoz::platform {
namespace {
class MacPlatform;
}
} // namespace kompoz::platform

using kompoz::platform::EventType;

// The window's view: its layer the CAMetalLayer drawn into; the mouse, the keyboard, typing
// (NSTextInputClient) and drops, told to the platform.
@interface KompozView : NSView <NSTextInputClient> {
@public
    kompoz::platform::MacPlatform* platform;
    NSTrackingArea* tracking;
    NSTimer* liveResize;  // frames while the window is resized (AppKit's own loop then runs)
    NSString* marked;     // what an input method is composing
}
@end

// The window's and the app's delegate: closing, focus, the scale and the screen, quitting.
@interface KompozDelegate : NSObject <NSWindowDelegate, NSApplicationDelegate> {
@public
    kompoz::platform::MacPlatform* platform;
}
@end

namespace kompoz::platform {
namespace {

int64_t steadyNow() { return std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch()).count(); }

std::string utf8(NSString* s) { return s ? std::string(s.UTF8String) : std::string(); }

// A key as Kompoz's: the layout's letter or digit (what it types without modifiers), else by its
// key code (Carbon's kVK_*).
Key keyOf(NSEvent* e) {
    NSString* typed = e.charactersIgnoringModifiers.lowercaseString;
    if (typed.length == 1) {
        unichar c = [typed characterAtIndex:0];
        if (c >= 'a' && c <= 'z') return Key(int(Key::A) + int(c - 'a'));
        if (c >= '0' && c <= '9') return Key(int(Key::Zero) + int(c - '0'));
    }
    switch (e.keyCode) {
    case 36:
    case 76: return Key::Enter;  // (Return, the keypad's Enter)
    case 53: return Key::Escape;
    case 48: return Key::Tab;
    case 51: return Key::Backspace;
    case 117: return Key::Delete;
    case 49: return Key::Space;
    case 123: return Key::Left;
    case 124: return Key::Right;
    case 126: return Key::Up;
    case 125: return Key::Down;
    case 115: return Key::Home;
    case 119: return Key::End;
    case 116: return Key::PageUp;
    case 121: return Key::PageDown;
    case 122: return Key::F1;
    case 120: return Key::F2;
    case 99: return Key::F3;
    case 118: return Key::F4;
    case 96: return Key::F5;
    case 97: return Key::F6;
    case 98: return Key::F7;
    case 100: return Key::F8;
    case 101: return Key::F9;
    case 109: return Key::F10;
    case 103: return Key::F11;
    case 111: return Key::F12;
    default: return Key::Unknown;
    }
}

// The keys held down: Command is Kompoz's Ctrl (its shortcuts' key), Control too.
KeyModifiers heldKeys(NSEventModifierFlags flags) {
    return {(flags & NSEventModifierFlagShift) != 0, (flags & (NSEventModifierFlagCommand | NSEventModifierFlagControl)) != 0,
            (flags & NSEventModifierFlagOption) != 0};
}

class MacPlatform final : public Platform {
public:
    ~MacPlatform() override {
        closeWindow();
        if (keptOn_) IOPMAssertionRelease(assertion_);
    }

    // The app made a regular one (a Dock icon, a menu bar, the focus when started from a
    // terminal) with its menu.
    bool connect() {
        [NSApplication sharedApplication];
        delegate_ = [[KompozDelegate alloc] init];
        delegate_->platform = this;
        NSApp.delegate = delegate_;
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
        [NSApp finishLaunching];
        // SIGINT and SIGTERM (Ctrl+C in a terminal, scripts/dev.sh's restart) as a quit, as SDL
        // made them: the app writes its state on the way out.
        for (int s : {SIGINT, SIGTERM}) {
            signal(s, SIG_IGN);
            dispatch_source_t source = dispatch_source_create(DISPATCH_SOURCE_TYPE_SIGNAL, uintptr_t(s), 0, dispatch_get_main_queue());
            MacPlatform* self = this;
            dispatch_source_set_event_handler(source, ^{
                self->quitRequested();
                self->wake();
            });
            dispatch_resume(source);
            signalSources_.push_back(source);
        }
        return true;
    }

    int run(int argc, char* argv[]) {
        AppCore core{*this};
        core_ = &core;
        if (!core.start(argc, argv)) return 1;
        [NSApp activateIgnoringOtherApps:YES];
        while (!quit_) {
            @autoreleasepool {
                // What came, each to AppKit (and so to the view); a step; then until an event, a
                // wake, the frame due or a coroutine's delay.
                while (NSEvent* e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:[NSDate distantPast] inMode:NSDefaultRunLoopMode dequeue:YES])
                    dispatch(e);
                if (!step()) break;
                if (needFrame_ && !waitingForDue_) continue;
                int64_t until = waitingForDue_ ? dueAt_ : wakeAt_;
                NSDate* deadline = until >= 0 ? [NSDate dateWithTimeIntervalSinceNow:double(std::max<int64_t>(0, until - now())) / 1e9] : [NSDate distantFuture];
                if (NSEvent* e = [NSApp nextEventMatchingMask:NSEventMaskAny untilDate:deadline inMode:NSDefaultRunLoopMode dequeue:YES]) dispatch(e);
                events_.push_back({.type = EventType::Wake});  // (a delay may be due)
            }
        }
        core.quit();
        core_ = nullptr;
        return 0;
    }

    // What came handed to the core, and a frame if one is due: false once the app is done. The
    // loop's, and the window's while AppKit resizes it in a loop of its own.
    bool step() {
        if (stepping_) return true;
        stepping_ = true;
        for (const Event& e : std::exchange(events_, {})) {
            if (e.type == EventType::Quit) quit_ = true;
            core_->handle(e);
            needFrame_ = true;
        }
        bool going = !quit_;
        waitingForDue_ = false;
        if (going && (needFrame_ || busy_)) {
            int64_t due = core_->frameDue(), at = now();
            if (due >= 0 && at < due) core_->idle(due - at);
            if (due >= 0 && now() < due) {
                waitingForDue_ = true, dueAt_ = due;
            } else {
                Next next = core_->frame();
                busy_ = next.busy, wakeAt_ = next.busy ? -1 : next.wakeAt;
                needFrame_ = busy_;
                going = !next.quit;
            }
        }
        stepping_ = false;
        return going;
    }

    // ---- Platform ------------------------------------------------------------------------

    std::optional<std::string> option(std::string_view name) const override {
        if (const char* v = std::getenv(std::string(name).c_str())) return std::string(v);
        return std::nullopt;
    }
    void log(const std::string& message) const override { platform::log(message); }
    int64_t now() const override { return steadyNow(); }
    std::vector<gpu::Backend> backends() const override { return {gpu::Backend::Metal}; }

    bool openWindow(const Application& app, gpu::Backend backend, gpu::Surface& surface, std::string* error) override {
        if (backend != gpu::Backend::Metal) {
            *error = std::string("no ") + gpu::backendName(backend) + " here";
            return false;
        }
        title_ = app.title;
        makeMenu();
        NSRect frame = NSMakeRect(0, 0, app.width, app.height);  // (points: a dp each, the screen's scale the pixels)
        NSWindowStyleMask style = NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;
        window_ = [[NSWindow alloc] initWithContentRect:frame styleMask:style backing:NSBackingStoreBuffered defer:NO];
        window_.releasedWhenClosed = NO;
        window_.title = [NSString stringWithUTF8String:title_.c_str()];
        window_.delegate = delegate_;
        view_ = [[KompozView alloc] initWithFrame:frame];
        view_->platform = this;
        window_.contentView = view_;
        [window_ makeFirstResponder:view_];
        [window_ center];
        layer_ = (CAMetalLayer*)view_.layer;
        layer_.contentsScale = window_.backingScaleFactor;
        sizes();
        surface.metalLayer = (__bridge void*)layer_;
        surface.drawableSize = [this](int* w, int* h) { *w = pixelWidth_, *h = pixelHeight_; };
        [window_ makeKeyAndOrderFront:nil];
        events_.push_back({.type = EventType::Shown});
        return true;
    }

    void closeWindow() override {
        if (view_ && view_->liveResize) [view_->liveResize invalidate];
        if (window_) {
            window_.delegate = nil;
            [window_ close];
        }
        window_ = nil, view_ = nil, layer_ = nil;
    }

    void showError(const std::string& message) override {
        platform::log(message);
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = @"Kompoz";
        alert.informativeText = [NSString stringWithUTF8String:message.c_str()];
        alert.alertStyle = NSAlertStyleCritical;
        [alert runModal];
    }

    // Points are AppKit's (a dp each); the scale is the screen's backing scale; KOMPOZ_SCALE and
    // KOMPOZ_FONT_SCALE stand in for them.
    Metrics metrics() const override {
        Metrics m;
        m.pixelWidth = pixelWidth_, m.pixelHeight = pixelHeight_;
        m.pointWidth = int(std::lround(pointWidth_)), m.pointHeight = int(std::lround(pointHeight_));
        double backing = window_ ? window_.backingScaleFactor : 1;
        m.scale = float(backing);
        if (auto v = option("KOMPOZ_SCALE"); v && std::atof(v->c_str()) > 0) m.scale = float(std::atof(v->c_str()));
        m.pointToDp = float(backing) / m.scale;
        if (auto v = option("KOMPOZ_FONT_SCALE")) m.fontScale = std::max(0.5f, float(std::atof(v->c_str())));
        NSScreen* screen = window_ ? window_.screen : nil;
        m.refreshRate = screen && screen.maximumFramesPerSecond > 0 ? float(screen.maximumFramesPerSecond) : 60.f;
        m.safeArea = Rect{0, 0, float(pointWidth_), float(pointHeight_)};
        NSAppearanceName look = [NSApp.effectiveAppearance bestMatchFromAppearancesWithNames:@[NSAppearanceNameAqua, NSAppearanceNameDarkAqua]];
        m.darkTheme = [look isEqualToString:NSAppearanceNameDarkAqua];
        m.language = preferredLanguage();
        if (m.language.empty())  // (an app started from the Finder has no LANG: the user's languages)
            if (NSString* first = NSLocale.preferredLanguages.firstObject) {
                m.language = utf8(first);
                m.language = m.language.substr(0, m.language.find_first_of("-_"));
            }
        return m;
    }
    void pixelSize(int* width, int* height) const override { *width = pixelWidth_, *height = pixelHeight_; }
    void setWindowSize(int width, int height) override {
        if (window_) [window_ setContentSize:NSMakeSize(width, height)];
    }

    // A Mac has a mouse or a trackpad, and a keyboard; a microphone and a camera if the system
    // has a default one (asked without asking the user).
    MediaInfo media(bool probe) override {
        if (probe && !probed_) {
            probed_ = true;
            hasMicrophone_ = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeAudio] != nil;
            hasCamera_ = [AVCaptureDevice defaultDeviceWithMediaType:AVMediaTypeVideo] != nil;
        }
        MediaInfo info;
        info.pointerPrecision = PointerPrecision::Fine;
        info.physicalKeyboard = true;
        info.hasMicrophone = hasMicrophone_, info.hasCamera = hasCamera_;
        return info;
    }

    void attach(ComposeHost&, ComposeHost::Callbacks& callbacks) override {
        MacPlatform* self = this;
        callbacks.wake = [self] { self->wake(); };
        callbacks.textInput = [self](bool active, Rect area, const TextEditState&, const ImeOptions&) {
            float k = 1 / self->pointToDp();  // dp -> points
            self->textActive_ = active;
            self->textArea_ = NSMakeRect(area.x * k, area.y * k, area.width * k, area.height * k);
            if (!active && self->view_) [self->view_.inputContext discardMarkedText];
        };
        callbacks.cursor = [self](Cursor cursor) {
            self->cursor_ = cursor;
            if (self->window_) [self->window_ invalidateCursorRectsForView:self->view_];
        };
        callbacks.getClipboard = [] { return utf8([NSPasteboard.generalPasteboard stringForType:NSPasteboardTypeString]); };
        callbacks.setClipboard = [](const std::string& text) {
            [NSPasteboard.generalPasteboard clearContents];
            [NSPasteboard.generalPasteboard setString:[NSString stringWithUTF8String:text.c_str()] forType:NSPasteboardTypeString];
        };
        callbacks.openUri = [self](const std::string& uri) {
            NSURL* url = [NSURL URLWithString:[NSString stringWithUTF8String:uri.c_str()]];
            if (!url || ![NSWorkspace.sharedWorkspace openURL:url]) self->log("kompoz: can't open " + uri);
        };
    }

    void composed(ComposeHost& h, bool, int64_t) override {
        // keepScreenOn: the display kept from sleeping while it is laid out.
        if (bool on = h.keepScreenOn(); on != keptOn_) {
            keptOn_ = on;
            if (on) IOPMAssertionCreateWithName(kIOPMAssertPreventUserIdleDisplaySleep, kIOPMAssertionLevelOn, CFSTR("keepScreenOn"), &assertion_);
            else IOPMAssertionRelease(assertion_);
        }
    }

    // ---- What the view and the delegate tell ---------------------------------------------

    void push(Event e) {
        if (e.time < 0) e.time = now();
        events_.push_back(std::move(e));
    }
    // The view's size changed (or its scale): the drawable's pixels follow.
    void resized() {
        if (!window_) return;
        layer_.contentsScale = window_.backingScaleFactor;
        if (sizes()) push({.type = EventType::Resized});
        if (view_.inLiveResize) step();  // (drawn as it is resized)
    }
    void wakeStep() { step(); }
    void quitRequested() { push({.type = EventType::Quit}); }
    float pointToDp() const {
        double backing = window_ ? window_.backingScaleFactor : 1;
        if (auto v = option("KOMPOZ_SCALE"); v && std::atof(v->c_str()) > 0) return float(backing / std::atof(v->c_str()));
        return 1;
    }
    bool textActive() const { return textActive_; }
    NSRect textArea() const { return textArea_; }
    NSCursor* cursor() const {
        switch (cursor_) {
        case Cursor::Pointer: return NSCursor.pointingHandCursor;
        case Cursor::Text: return NSCursor.IBeamCursor;
        case Cursor::Crosshair: return NSCursor.crosshairCursor;
        case Cursor::Move: return NSCursor.closedHandCursor;
        case Cursor::ResizeHorizontal: return NSCursor.resizeLeftRightCursor;
        case Cursor::ResizeVertical: return NSCursor.resizeUpDownCursor;
        case Cursor::NotAllowed: return NSCursor.operationNotAllowedCursor;
        default: return NSCursor.arrowCursor;
        }
    }

private:
    KompozDelegate* delegate_ = nil;
    NSWindow* window_ = nil;
    KompozView* view_ = nil;
    CAMetalLayer* layer_ = nil;
    std::string title_ = "Kompoz";
    double pointWidth_ = 1, pointHeight_ = 1;
    int pixelWidth_ = 1, pixelHeight_ = 1;

    AppCore* core_ = nullptr;
    std::deque<Event> events_;
    bool quit_ = false, busy_ = false, needFrame_ = true, waitingForDue_ = false, stepping_ = false;
    int64_t dueAt_ = -1, wakeAt_ = -1;
    std::atomic<bool> woken_{false};

    bool textActive_ = false;
    NSRect textArea_ = NSZeroRect;  // the field, in the view's points
    Cursor cursor_ = Cursor::Default;
    bool keptOn_ = false, probed_ = false, hasMicrophone_ = false, hasCamera_ = false;
    IOPMAssertionID assertion_ = 0;
    std::vector<dispatch_source_t> signalSources_;

    // An event to AppKit (and so to the view), or the loop's own wake.
    void dispatch(NSEvent* e) {
        if (e.type == NSEventTypeApplicationDefined) woken_ = false;
        else [NSApp sendEvent:e];
    }

    // From another thread too: an event of the app's own ends the loop's wait.
    void wake() {
        if (woken_.exchange(true)) return;
        dispatch_async(dispatch_get_main_queue(), ^{
            NSEvent* e = [NSEvent otherEventWithType:NSEventTypeApplicationDefined location:NSZeroPoint modifierFlags:0 timestamp:0
                                        windowNumber:0 context:nil subtype:0 data1:0 data2:0];
            [NSApp postEvent:e atStart:NO];
        });
    }

    // The view's size in points and the drawable's in pixels; whether it changed.
    bool sizes() {
        NSSize points = view_.bounds.size;
        NSSize pixels = [view_ convertSizeToBacking:points];
        int w = std::max(1, int(std::lround(pixels.width))), h = std::max(1, int(std::lround(pixels.height)));
        bool changed = w != pixelWidth_ || h != pixelHeight_ || points.width != pointWidth_ || points.height != pointHeight_;
        pointWidth_ = points.width, pointHeight_ = points.height, pixelWidth_ = w, pixelHeight_ = h;
        return changed;
    }

    // The menu bar: the app's menu (Hide, Hide Others, Show All, Quit) and the window's.
    void makeMenu() {
        if (NSApp.mainMenu) return;
        NSString* name = [NSString stringWithUTF8String:title_.c_str()];
        NSMenu* bar = [[NSMenu alloc] init];
        NSMenu* appMenu = [[NSMenu alloc] initWithTitle:name];
        [appMenu addItemWithTitle:[@"Hide " stringByAppendingString:name] action:@selector(hide:) keyEquivalent:@"h"];
        NSMenuItem* others = [appMenu addItemWithTitle:@"Hide Others" action:@selector(hideOtherApplications:) keyEquivalent:@"h"];
        others.keyEquivalentModifierMask = NSEventModifierFlagOption | NSEventModifierFlagCommand;
        [appMenu addItemWithTitle:@"Show All" action:@selector(unhideAllApplications:) keyEquivalent:@""];
        [appMenu addItem:[NSMenuItem separatorItem]];
        [appMenu addItemWithTitle:[@"Quit " stringByAppendingString:name] action:@selector(terminate:) keyEquivalent:@"q"];
        NSMenuItem* appItem = [[NSMenuItem alloc] init];
        appItem.submenu = appMenu;
        [bar addItem:appItem];
        NSMenu* windowMenu = [[NSMenu alloc] initWithTitle:@"Window"];
        [windowMenu addItemWithTitle:@"Minimize" action:@selector(performMiniaturize:) keyEquivalent:@"m"];
        [windowMenu addItemWithTitle:@"Zoom" action:@selector(performZoom:) keyEquivalent:@""];
        NSMenuItem* full = [windowMenu addItemWithTitle:@"Enter Full Screen" action:@selector(toggleFullScreen:) keyEquivalent:@"f"];
        full.keyEquivalentModifierMask = NSEventModifierFlagControl | NSEventModifierFlagCommand;
        NSMenuItem* windowItem = [[NSMenuItem alloc] init];
        windowItem.submenu = windowMenu;
        [bar addItem:windowItem];
        NSApp.mainMenu = bar;
        NSApp.windowsMenu = windowMenu;
    }
};

} // namespace
} // namespace kompoz::platform

using kompoz::platform::Event;
using kompoz::platform::MouseButton;

@implementation KompozView

- (instancetype)initWithFrame:(NSRect)frame {
    if ((self = [super initWithFrame:frame])) {
        self.wantsLayer = YES;
        self.layerContentsRedrawPolicy = NSViewLayerContentsRedrawDuringViewResize;
        [self registerForDraggedTypes:@[NSPasteboardTypeFileURL, NSPasteboardTypeString]];
    }
    return self;
}
- (CALayer*)makeBackingLayer {
    return [CAMetalLayer layer];
}
- (BOOL)wantsUpdateLayer {
    return YES;
}
- (void)updateLayer {
    // (Metal presents to the layer: nothing to draw here.)
}
- (BOOL)isFlipped {
    return YES;  // (y down, as Kompoz's)
}
- (BOOL)acceptsFirstResponder {
    return YES;
}
- (BOOL)acceptsFirstMouse:(NSEvent*)event {
    return YES;
}

- (void)updateTrackingAreas {
    if (tracking) [self removeTrackingArea:tracking];
    tracking = [[NSTrackingArea alloc] initWithRect:NSZeroRect
                                            options:NSTrackingMouseEnteredAndExited | NSTrackingMouseMoved | NSTrackingCursorUpdate |
                                                    NSTrackingActiveAlways | NSTrackingInVisibleRect
                                              owner:self
                                           userInfo:nil];
    [self addTrackingArea:tracking];
    [super updateTrackingAreas];
}
- (void)resetCursorRects {
    if (platform) [self addCursorRect:self.bounds cursor:platform->cursor()];
}
- (void)cursorUpdate:(NSEvent*)event {
    if (platform) [platform->cursor() set];
}

- (void)setFrameSize:(NSSize)size {
    [super setFrameSize:size];
    if (platform) platform->resized();
}
- (void)viewDidChangeBackingProperties {
    [super viewDidChangeBackingProperties];
    if (platform) {
        platform->resized();
        platform->push({.type = EventType::MetricsChanged});
    }
}
- (void)viewDidChangeEffectiveAppearance {
    [super viewDidChangeEffectiveAppearance];
    if (platform) platform->push({.type = EventType::MetricsChanged});
}
- (void)viewWillStartLiveResize {
    [super viewWillStartLiveResize];
    // AppKit resizes in a loop of its own (the event tracking mode): a timer there draws.
    liveResize = [NSTimer timerWithTimeInterval:1.0 / 120 repeats:YES block:^(NSTimer* timer) {
        if (self->platform) self->platform->wakeStep();
    }];
    [NSRunLoop.currentRunLoop addTimer:liveResize forMode:NSEventTrackingRunLoopMode];
}
- (void)viewDidEndLiveResize {
    [super viewDidEndLiveResize];
    [liveResize invalidate];
    liveResize = nil;
}

// ---- The mouse and the trackpad ----

- (void)pointer:(NSEvent*)e type:(EventType)type button:(MouseButton)button {
    if (!platform) return;
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    Event out{.type = type, .x = float(p.x), .y = float(p.y)};
    out.button = button;
    out.modifiers = kompoz::platform::heldKeys(e.modifierFlags);
    platform->push(out);
}
- (void)mouseMoved:(NSEvent*)e {
    [self pointer:e type:EventType::MouseMove button:MouseButton::Left];
}
- (void)mouseDragged:(NSEvent*)e {
    [self pointer:e type:EventType::MouseMove button:MouseButton::Left];
}
- (void)rightMouseDragged:(NSEvent*)e {
    [self pointer:e type:EventType::MouseMove button:MouseButton::Right];
}
- (void)otherMouseDragged:(NSEvent*)e {
    [self pointer:e type:EventType::MouseMove button:MouseButton::Middle];
}
- (void)mouseDown:(NSEvent*)e {
    [self pointer:e type:EventType::MouseDown button:MouseButton::Left];
}
- (void)mouseUp:(NSEvent*)e {
    [self pointer:e type:EventType::MouseUp button:MouseButton::Left];
}
- (void)rightMouseDown:(NSEvent*)e {
    [self pointer:e type:EventType::MouseDown button:MouseButton::Right];
}
- (void)rightMouseUp:(NSEvent*)e {
    [self pointer:e type:EventType::MouseUp button:MouseButton::Right];
}
- (MouseButton)otherButton:(NSEvent*)e {
    return e.buttonNumber == 3 ? MouseButton::Back : e.buttonNumber == 4 ? MouseButton::Forward : MouseButton::Middle;
}
- (void)otherMouseDown:(NSEvent*)e {
    [self pointer:e type:EventType::MouseDown button:[self otherButton:e]];
}
- (void)otherMouseUp:(NSEvent*)e {
    [self pointer:e type:EventType::MouseUp button:[self otherButton:e]];
}
- (void)mouseExited:(NSEvent*)e {
    if (platform) platform->push({.type = EventType::PointerLeft});
}
// The wheel as SDL tells it: a trackpad's scrolling a tenth of its points, a wheel's notches
// whole; flipped back where the system scrolls naturally (as Kompoz takes it).
- (void)scrollWheel:(NSEvent*)e {
    if (!platform) return;
    double x = 0, y = 0;
    if (e.hasPreciseScrollingDeltas) {
        x = -e.scrollingDeltaX * 0.1, y = e.scrollingDeltaY * 0.1;
    } else {
        x = -e.deltaX, y = e.deltaY;
        x = x > 0 ? std::ceil(x) : std::floor(x), y = y > 0 ? std::ceil(y) : std::floor(y);
    }
    double flip = e.directionInvertedFromDevice ? -1 : 1;
    NSPoint p = [self convertPoint:e.locationInWindow fromView:nil];
    Event out{.type = EventType::Wheel, .x = float(p.x), .y = float(p.y), .dx = float(x * flip), .dy = float(y * flip)};
    out.modifiers = kompoz::platform::heldKeys(e.modifierFlags);
    platform->push(out);
}
// A trackpad's pinch: each step's magnification as the change since the last.
- (void)magnifyWithEvent:(NSEvent*)e {
    if (!platform) return;
    NSPoint p = [self convertPoint:[self.window mouseLocationOutsideOfEventStream] fromView:nil];
    if (e.phase == NSEventPhaseBegan) platform->push({.type = EventType::PinchBegin, .x = float(p.x), .y = float(p.y)});
    if (e.phase == NSEventPhaseChanged || e.phase == NSEventPhaseBegan)
        platform->push({.type = EventType::Pinch, .x = float(p.x), .y = float(p.y), .scale = float(1 + e.magnification)});
}

// A trackpad's two fingers turning: the turn since the last (AppKit's is counterclockwise).
- (void)rotateWithEvent:(NSEvent*)e {
    if (!platform) return;
    NSPoint p = [self convertPoint:[self.window mouseLocationOutsideOfEventStream] fromView:nil];
    Event out{.type = EventType::Pinch, .x = float(p.x), .y = float(p.y)};
    out.rotation = -e.rotation;
    platform->push(out);
}

// ---- The keyboard and typing ----

- (void)keyDown:(NSEvent*)e {
    if (!platform) return;
    platform->push(Event{.type = EventType::KeyDown, .key = kompoz::platform::keyOf(e), .modifiers = kompoz::platform::heldKeys(e.modifierFlags)});
    // What it types, through the input method, while a field takes typing (not a shortcut's).
    if (platform->textActive() && !(e.modifierFlags & (NSEventModifierFlagCommand | NSEventModifierFlagControl))) [self interpretKeyEvents:@[e]];
}
- (void)keyUp:(NSEvent*)e {
    if (platform) platform->push(Event{.type = EventType::KeyUp, .key = kompoz::platform::keyOf(e), .modifiers = kompoz::platform::heldKeys(e.modifierFlags)});
}
- (void)insertText:(id)string replacementRange:(NSRange)range {
    NSString* text = [string isKindOfClass:NSAttributedString.class] ? [(NSAttributedString*)string string] : (NSString*)string;
    std::string typed = kompoz::platform::utf8(text);
    if (platform && !typed.empty() && uint8_t(typed[0]) >= 0x20 && typed[0] != 0x7f) platform->push({.type = EventType::Text, .text = typed});
    marked = nil;
}
- (void)doCommandBySelector:(SEL)selector {
    // (Enter, the arrows, Backspace: the keys themselves told Kompoz; nothing beeps.)
}
- (void)setMarkedText:(id)string selectedRange:(NSRange)selected replacementRange:(NSRange)range {
    marked = [string isKindOfClass:NSAttributedString.class] ? [(NSAttributedString*)string string] : (NSString*)string;
}
- (void)unmarkText {
    marked = nil;
}
- (BOOL)hasMarkedText {
    return marked.length > 0;
}
- (NSRange)markedRange {
    return marked.length ? NSMakeRange(0, marked.length) : NSMakeRange(NSNotFound, 0);
}
- (NSRange)selectedRange {
    return NSMakeRange(0, 0);
}
- (NSArray<NSAttributedStringKey>*)validAttributesForMarkedText {
    return @[];
}
- (NSAttributedString*)attributedSubstringForProposedRange:(NSRange)range actualRange:(NSRangePointer)actual {
    return nil;
}
- (NSUInteger)characterIndexForPoint:(NSPoint)point {
    return 0;
}
// Where the field is on the screen: the input method's window goes beside it.
- (NSRect)firstRectForCharacterRange:(NSRange)range actualRange:(NSRangePointer)actual {
    NSRect field = platform ? platform->textArea() : NSZeroRect;
    return [self.window convertRectToScreen:[self convertRect:field toView:nil]];
}

// ---- Drops ----

- (void)drop:(EventType)type at:(id<NSDraggingInfo>)drag text:(std::string)text {
    if (!platform) return;
    NSPoint p = [self convertPoint:drag.draggingLocation fromView:nil];
    Event e{.type = type, .x = float(p.x), .y = float(p.y)};
    e.text = std::move(text);
    platform->push(std::move(e));
}
- (NSDragOperation)draggingEntered:(id<NSDraggingInfo>)drag {
    [self drop:EventType::DropBegin at:drag text:std::string()];
    [self drop:EventType::DropMove at:drag text:std::string()];
    return NSDragOperationCopy;
}
- (NSDragOperation)draggingUpdated:(id<NSDraggingInfo>)drag {
    [self drop:EventType::DropMove at:drag text:std::string()];
    return NSDragOperationCopy;
}
- (void)draggingExited:(id<NSDraggingInfo>)drag {
    if (platform) platform->push({.type = EventType::DropComplete});
}
// Files (their paths), else text.
- (BOOL)performDragOperation:(id<NSDraggingInfo>)drag {
    NSPasteboard* board = drag.draggingPasteboard;
    NSArray<NSURL*>* files = [board readObjectsForClasses:@[NSURL.class] options:@{NSPasteboardURLReadingFileURLsOnlyKey: @YES}];
    if (files.count) {
        for (NSURL* file in files) [self drop:EventType::DropFile at:drag text:kompoz::platform::utf8(file.path)];
    } else if (NSString* text = [board stringForType:NSPasteboardTypeString]) {
        [self drop:EventType::DropText at:drag text:kompoz::platform::utf8(text)];
    }
    [self drop:EventType::DropComplete at:drag text:std::string()];
    return YES;
}

@end

@implementation KompozDelegate

- (BOOL)windowShouldClose:(NSWindow*)window {
    if (platform) platform->quitRequested();
    return NO;  // (the app closes it as it quits)
}
// Quit (its menu item, Cmd+Q, the Dock's): the app quits as it does, not ended here.
- (NSApplicationTerminateReply)applicationShouldTerminate:(NSApplication*)app {
    if (platform) platform->quitRequested();
    return NSTerminateCancel;
}
- (void)windowDidBecomeKey:(NSNotification*)n {
    if (platform) platform->push({.type = EventType::FocusGained});
}
- (void)windowDidResignKey:(NSNotification*)n {
    if (platform) platform->push({.type = EventType::FocusLost});
}
- (void)windowDidMiniaturize:(NSNotification*)n {
    if (platform) platform->push({.type = EventType::Minimized});
}
- (void)windowDidDeminiaturize:(NSNotification*)n {
    if (platform) platform->push({.type = EventType::Restored});
}
- (void)windowDidChangeScreen:(NSNotification*)n {
    if (platform) platform->push({.type = EventType::MetricsChanged});  // (its refresh rate)
}

@end

namespace kompoz::platform {

int runMacOS(int argc, char* argv[]) {
    @autoreleasepool {
        auto platform = std::make_unique<MacPlatform>();
        return platform->connect() ? platform->run(argc, argv) : -1;
    }
}

} // namespace kompoz::platform
