#pragma once

// libX11 with XInput 2 (libXi), libXcursor and XRandR (libXrandr), loaded at run time (where they
// aren't, as on a Wayland-only desktop, the app runs on another platform): each function through
// a pointer that its own name stands for, as SDL does it. libXcursor and libXrandr are optional
// (the cursors are then X's own, the refresh rate 60 Hz). Include this, not Xlib.h.

#include <X11/Xatom.h>
#include <X11/XKBlib.h>
#include <X11/Xcursor/Xcursor.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/extensions/XInput2.h>
#include <X11/extensions/Xrandr.h>

namespace kompoz::platform::x11 {

#define KOMPOZ_X11_FUNCTIONS(X)                                                                                       \
    X(XInitThreads) X(XOpenDisplay) X(XCloseDisplay) X(XInternAtom) X(XInternAtoms) X(XCreateWindow)                  \
    X(XDestroyWindow)                                                                                                 \
    X(XMapWindow) X(XChangeProperty) X(XDeleteProperty) X(XGetWindowProperty) X(XFree) X(XSetWMProtocols)             \
    X(XAllocSizeHints) X(XSetWMNormalHints) X(XAllocClassHint) X(XSetClassHint) X(XPending) X(XNextEvent)             \
    X(XFilterEvent) X(XFlush) X(XSendEvent) X(XResizeWindow) X(XTranslateCoordinates) X(XDefineCursor)                \
    X(XCreateFontCursor) X(XFreeCursor) X(XSetSelectionOwner) X(XGetSelectionOwner) X(XConvertSelection)              \
    X(XCheckTypedWindowEvent) X(XLookupKeysym) X(Xutf8LookupString) X(XOpenIM) X(XCloseIM) X(XCreateIC)               \
    X(XDestroyIC) X(XSetICFocus) X(XUnsetICFocus) X(XSetICValues) X(XSetIMValues) X(XVaCreateNestedList)              \
    X(XSetLocaleModifiers) X(XRegisterIMInstantiateCallback) X(XUnregisterIMInstantiateCallback)                      \
    X(XResourceManagerString) X(XkbSetDetectableAutoRepeat) X(XSelectInput) X(XQueryExtension) X(XGetEventData)       \
    X(XFreeEventData) X(XExtendedMaxRequestSize) X(XMaxRequestSize) X(XRefreshKeyboardMapping) X(XSetErrorHandler)  \
    X(XCheckIfEvent) X(XLookupString) X(XGetIMValues)
#define KOMPOZ_XI_FUNCTIONS(X)                                                                                        \
    X(XIQueryVersion) X(XISelectEvents) X(XIQueryDevice) X(XIFreeDeviceInfo)
#define KOMPOZ_XCURSOR_FUNCTIONS(X)                                                                                   \
    X(XcursorLibraryLoadCursor)
#define KOMPOZ_XRANDR_FUNCTIONS(X)                                                                                    \
    X(XRRQueryExtension) X(XRRSelectInput) X(XRRGetScreenResourcesCurrent) X(XRRFreeScreenResources)                  \
    X(XRRGetCrtcInfo) X(XRRFreeCrtcInfo)

// The functions, by their real declarations' types.
struct Functions {
#define KOMPOZ_X11_POINTER(name) decltype(&::name) p_##name = nullptr;
    KOMPOZ_X11_FUNCTIONS(KOMPOZ_X11_POINTER)
    KOMPOZ_XI_FUNCTIONS(KOMPOZ_X11_POINTER)
    KOMPOZ_XCURSOR_FUNCTIONS(KOMPOZ_X11_POINTER)
    KOMPOZ_XRANDR_FUNCTIONS(KOMPOZ_X11_POINTER)
#undef KOMPOZ_X11_POINTER
    bool xcursor = false, xrandr = false;  // libXcursor's, libXrandr's
};
extern Functions fn;

// The libraries loaded, once; false if libX11 or libXi isn't there.
bool load();

} // namespace kompoz::platform::x11

#define KOMPOZ_X11_RENAME(name) (*kompoz::platform::x11::fn.p_##name)
#define XInitThreads KOMPOZ_X11_RENAME(XInitThreads)
#define XOpenDisplay KOMPOZ_X11_RENAME(XOpenDisplay)
#define XCloseDisplay KOMPOZ_X11_RENAME(XCloseDisplay)
#define XInternAtom KOMPOZ_X11_RENAME(XInternAtom)
#define XInternAtoms KOMPOZ_X11_RENAME(XInternAtoms)
#define XCreateWindow KOMPOZ_X11_RENAME(XCreateWindow)
#define XDestroyWindow KOMPOZ_X11_RENAME(XDestroyWindow)
#define XMapWindow KOMPOZ_X11_RENAME(XMapWindow)
#define XChangeProperty KOMPOZ_X11_RENAME(XChangeProperty)
#define XDeleteProperty KOMPOZ_X11_RENAME(XDeleteProperty)
#define XGetWindowProperty KOMPOZ_X11_RENAME(XGetWindowProperty)
#define XFree KOMPOZ_X11_RENAME(XFree)
#define XSetWMProtocols KOMPOZ_X11_RENAME(XSetWMProtocols)
#define XAllocSizeHints KOMPOZ_X11_RENAME(XAllocSizeHints)
#define XSetWMNormalHints KOMPOZ_X11_RENAME(XSetWMNormalHints)
#define XAllocClassHint KOMPOZ_X11_RENAME(XAllocClassHint)
#define XSetClassHint KOMPOZ_X11_RENAME(XSetClassHint)
#define XPending KOMPOZ_X11_RENAME(XPending)
#define XNextEvent KOMPOZ_X11_RENAME(XNextEvent)
#define XFilterEvent KOMPOZ_X11_RENAME(XFilterEvent)
#define XFlush KOMPOZ_X11_RENAME(XFlush)
#define XSendEvent KOMPOZ_X11_RENAME(XSendEvent)
#define XResizeWindow KOMPOZ_X11_RENAME(XResizeWindow)
#define XTranslateCoordinates KOMPOZ_X11_RENAME(XTranslateCoordinates)
#define XDefineCursor KOMPOZ_X11_RENAME(XDefineCursor)
#define XCreateFontCursor KOMPOZ_X11_RENAME(XCreateFontCursor)
#define XFreeCursor KOMPOZ_X11_RENAME(XFreeCursor)
#define XSetSelectionOwner KOMPOZ_X11_RENAME(XSetSelectionOwner)
#define XGetSelectionOwner KOMPOZ_X11_RENAME(XGetSelectionOwner)
#define XConvertSelection KOMPOZ_X11_RENAME(XConvertSelection)
#define XCheckTypedWindowEvent KOMPOZ_X11_RENAME(XCheckTypedWindowEvent)
#define XLookupKeysym KOMPOZ_X11_RENAME(XLookupKeysym)
#define Xutf8LookupString KOMPOZ_X11_RENAME(Xutf8LookupString)
#define XOpenIM KOMPOZ_X11_RENAME(XOpenIM)
#define XCloseIM KOMPOZ_X11_RENAME(XCloseIM)
#define XCreateIC KOMPOZ_X11_RENAME(XCreateIC)
#define XDestroyIC KOMPOZ_X11_RENAME(XDestroyIC)
#define XSetICFocus KOMPOZ_X11_RENAME(XSetICFocus)
#define XUnsetICFocus KOMPOZ_X11_RENAME(XUnsetICFocus)
#define XSetICValues KOMPOZ_X11_RENAME(XSetICValues)
#define XSetIMValues KOMPOZ_X11_RENAME(XSetIMValues)
#define XVaCreateNestedList KOMPOZ_X11_RENAME(XVaCreateNestedList)
#define XSetLocaleModifiers KOMPOZ_X11_RENAME(XSetLocaleModifiers)
#define XRegisterIMInstantiateCallback KOMPOZ_X11_RENAME(XRegisterIMInstantiateCallback)
#define XUnregisterIMInstantiateCallback KOMPOZ_X11_RENAME(XUnregisterIMInstantiateCallback)
#define XResourceManagerString KOMPOZ_X11_RENAME(XResourceManagerString)
#define XkbSetDetectableAutoRepeat KOMPOZ_X11_RENAME(XkbSetDetectableAutoRepeat)
#define XSelectInput KOMPOZ_X11_RENAME(XSelectInput)
#define XQueryExtension KOMPOZ_X11_RENAME(XQueryExtension)
#define XGetEventData KOMPOZ_X11_RENAME(XGetEventData)
#define XFreeEventData KOMPOZ_X11_RENAME(XFreeEventData)
#define XExtendedMaxRequestSize KOMPOZ_X11_RENAME(XExtendedMaxRequestSize)
#define XMaxRequestSize KOMPOZ_X11_RENAME(XMaxRequestSize)
#define XRefreshKeyboardMapping KOMPOZ_X11_RENAME(XRefreshKeyboardMapping)
#define XSetErrorHandler KOMPOZ_X11_RENAME(XSetErrorHandler)
#define XCheckIfEvent KOMPOZ_X11_RENAME(XCheckIfEvent)
#define XLookupString KOMPOZ_X11_RENAME(XLookupString)
#define XGetIMValues KOMPOZ_X11_RENAME(XGetIMValues)
#define XIQueryVersion KOMPOZ_X11_RENAME(XIQueryVersion)
#define XISelectEvents KOMPOZ_X11_RENAME(XISelectEvents)
#define XIQueryDevice KOMPOZ_X11_RENAME(XIQueryDevice)
#define XIFreeDeviceInfo KOMPOZ_X11_RENAME(XIFreeDeviceInfo)
#define XcursorLibraryLoadCursor KOMPOZ_X11_RENAME(XcursorLibraryLoadCursor)
#define XRRQueryExtension KOMPOZ_X11_RENAME(XRRQueryExtension)
#define XRRSelectInput KOMPOZ_X11_RENAME(XRRSelectInput)
#define XRRGetScreenResourcesCurrent KOMPOZ_X11_RENAME(XRRGetScreenResourcesCurrent)
#define XRRFreeScreenResources KOMPOZ_X11_RENAME(XRRFreeScreenResources)
#define XRRGetCrtcInfo KOMPOZ_X11_RENAME(XRRGetCrtcInfo)
#define XRRFreeCrtcInfo KOMPOZ_X11_RENAME(XRRFreeCrtcInfo)
