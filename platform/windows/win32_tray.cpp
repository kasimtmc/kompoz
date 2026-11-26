#include "win32_tray.hpp"

#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0A00
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#define UNICODE 1  // (IDI_APPLICATION as a wide string)
#include <windows.h>
#include <shellapi.h>

#include <string>

namespace kompoz::platform {

namespace {

constexpr wchar_t kTrayClass[] = L"KompozTray";
constexpr UINT kTrayMessage = WM_APP + 0x2A1;  // the icons' callback message (lParam: the mouse's message)

std::wstring wideOf(const std::string& s) {
    if (s.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}

// The bitmap as an icon (32-bit, its alpha kept); none for an empty one (the program's own then).
HICON iconOf(const ImageBitmap& image) {
    if (image.empty() || !image.pixels()) return nullptr;
    int w = image.width(), h = image.height();
    BITMAPV5HEADER header{};
    header.bV5Size = sizeof header, header.bV5Width = w, header.bV5Height = -h, header.bV5Planes = 1, header.bV5BitCount = 32;
    header.bV5Compression = BI_BITFIELDS;
    header.bV5RedMask = 0x00FF0000, header.bV5GreenMask = 0x0000FF00, header.bV5BlueMask = 0x000000FF, header.bV5AlphaMask = 0xFF000000;
    void* bits = nullptr;
    HDC dc = GetDC(nullptr);
    HBITMAP color = CreateDIBSection(dc, reinterpret_cast<BITMAPINFO*>(&header), DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, dc);
    if (!color || !bits) return nullptr;
    const uint8_t* rgba = image.pixels();
    auto* out = static_cast<uint8_t*>(bits);
    for (size_t i = 0; i < size_t(w) * size_t(h); ++i) {  // straight RGBA to BGRA, premultiplied as icons want
        uint8_t a = rgba[i * 4 + 3];
        out[i * 4] = uint8_t(rgba[i * 4 + 2] * a / 255);
        out[i * 4 + 1] = uint8_t(rgba[i * 4 + 1] * a / 255);
        out[i * 4 + 2] = uint8_t(rgba[i * 4] * a / 255);
        out[i * 4 + 3] = a;
    }
    HBITMAP mask = CreateBitmap(w, h, 1, 1, nullptr);
    ICONINFO info{.fIcon = TRUE, .hbmMask = mask, .hbmColor = color};
    HICON icon = CreateIconIndirect(&info);
    DeleteObject(color);
    DeleteObject(mask);
    return icon;
}

// The menu's items, each with the command id it gives back (from 1, in itemAt's order).
HMENU menuOf(const std::vector<MenuBarItem>& items, UINT& next) {
    HMENU menu = CreatePopupMenu();
    for (const MenuBarItem& m : items) {
        UINT id = next++;
        if (m.separator) {
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            continue;
        }
        UINT flags = MF_STRING | (m.enabled ? 0 : MF_GRAYED) | (m.checked.value_or(false) ? MF_CHECKED : 0);
        std::wstring text = wideOf(m.text);
        if (!m.submenu.empty()) AppendMenuW(menu, flags | MF_POPUP, UINT_PTR(menuOf(m.submenu, next)), text.c_str());
        else AppendMenuW(menu, flags, id, text.c_str());
    }
    return menu;
}

const MenuBarItem* itemAt(const std::vector<MenuBarItem>& items, UINT& counter, UINT id) {
    for (const MenuBarItem& m : items) {
        if (++counter == id) return &m;
        if (!m.submenu.empty())
            if (const MenuBarItem* found = itemAt(m.submenu, counter, id)) return found;
    }
    return nullptr;
}

} // namespace

struct Win32Tray::State {
    HWND window = nullptr;
    UINT taskbarCreated = 0;
    struct Shown {
        std::shared_ptr<kompoz::detail::TrayRequest> request;
        UINT uid = 0;
        HICON icon = nullptr;
        bool added = false;
    };
    std::vector<Shown> shown;
    UINT nextUid = 1;

    NOTIFYICONDATAW data(const Shown& s) const {
        NOTIFYICONDATAW n{};
        n.cbSize = sizeof n, n.hWnd = window, n.uID = s.uid;
        n.uFlags = NIF_MESSAGE | NIF_ICON | NIF_TIP | NIF_SHOWTIP;
        n.uCallbackMessage = kTrayMessage;
        n.hIcon = s.icon ? s.icon : LoadIconW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1));
        if (!n.hIcon) n.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        std::wstring tip = wideOf(s.request->tooltip).substr(0, 127);
        wcsncpy_s(n.szTip, tip.c_str(), _TRUNCATE);
        return n;
    }
    void add(Shown& s) {
        NOTIFYICONDATAW n = data(s);
        s.added = Shell_NotifyIconW(NIM_ADD, &n);
        n.uVersion = NOTIFYICON_VERSION_4;
        if (s.added) Shell_NotifyIconW(NIM_SETVERSION, &n);
    }
    void remove(Shown& s) {
        NOTIFYICONDATAW n{};
        n.cbSize = sizeof n, n.hWnd = window, n.uID = s.uid;
        if (s.added) Shell_NotifyIconW(NIM_DELETE, &n);
        if (s.icon) DestroyIcon(s.icon);
        s.icon = nullptr, s.added = false;
    }

    // The icon's menu at the pointer: the item chosen runs its onClick.
    void showMenu(std::shared_ptr<kompoz::detail::TrayRequest> request) {
        if (request->menu.empty()) return;
        UINT next = 1;
        HMENU menu = menuOf(request->menu, next);
        POINT at;
        GetCursorPos(&at);
        SetForegroundWindow(window);  // (the menu closes as the user clicks elsewhere)
        UINT chosen = UINT(TrackPopupMenu(menu, TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, at.x, at.y, 0, window, nullptr));
        PostMessageW(window, WM_NULL, 0, 0);
        DestroyMenu(menu);
        if (!chosen) return;
        UINT counter = 0;
        if (const MenuBarItem* item = itemAt(request->menu, counter, chosen); item && item->enabled && item->onClick) item->onClick();
    }

    LRESULT message(UINT message, WPARAM w, LPARAM l) {
        if (taskbarCreated && message == taskbarCreated) {  // Explorer started again: the icons back
            for (Shown& s : shown) {
                s.added = false;
                add(s);
            }
            return 0;
        }
        if (message != kTrayMessage) return DefWindowProcW(window, message, w, l);
        UINT uid = HIWORD(l), event = LOWORD(l);  // (NOTIFYICON_VERSION_4's)
        for (Shown& s : shown) {
            if (s.uid != uid) continue;
            auto request = s.request;  // (its callbacks may take the icon away)
            if (event == WM_LBUTTONUP || event == NIN_SELECT || event == NIN_KEYSELECT) {
                // Where: version 4 gives the click's (the icon's for the keyboard's) screen point in wParam.
                IntOffset at{short(LOWORD(w)), short(HIWORD(w))};
                if (request->onAction) request->onAction(at);
            } else if (event == WM_CONTEXTMENU || event == WM_RBUTTONUP) {
                showMenu(request);
            }
            break;
        }
        return 0;
    }

    static LRESULT CALLBACK proc(HWND hwnd, UINT message, WPARAM w, LPARAM l) {
        if (message == WM_NCCREATE) SetWindowLongPtrW(hwnd, GWLP_USERDATA, LONG_PTR(reinterpret_cast<CREATESTRUCTW*>(l)->lpCreateParams));
        auto* self = reinterpret_cast<State*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
        return self && self->window ? self->message(message, w, l) : DefWindowProcW(hwnd, message, w, l);
    }
};

Win32Tray::Win32Tray() : state_(std::make_unique<State>()) {
    WNDCLASSEXW c{};
    c.cbSize = sizeof c, c.lpfnWndProc = &State::proc, c.hInstance = GetModuleHandleW(nullptr), c.lpszClassName = kTrayClass;
    RegisterClassExW(&c);
    // (a hidden top-level window, not a message-only one: TaskbarCreated goes to top-level windows)
    state_->window = CreateWindowExW(0, kTrayClass, L"", 0, 0, 0, 0, 0, nullptr, nullptr, c.hInstance, state_.get());
    state_->taskbarCreated = RegisterWindowMessageW(L"TaskbarCreated");
}

Win32Tray::~Win32Tray() {
    for (State::Shown& s : state_->shown) state_->remove(s);
    if (state_->window) DestroyWindow(state_->window);
}

void Win32Tray::notify(const kompoz::detail::TrayRequest& icon, const kompoz::Notification& notification) {
    State& st = *state_;
    for (const State::Shown& s : st.shown) {
        if (s.request.get() != &icon || !s.added) continue;
        NOTIFYICONDATAW n{};
        n.cbSize = sizeof n, n.hWnd = st.window, n.uID = s.uid;
        n.uFlags = NIF_INFO;
        wcsncpy_s(n.szInfoTitle, wideOf(notification.title).substr(0, 63).c_str(), _TRUNCATE);
        wcsncpy_s(n.szInfo, wideOf(notification.message).substr(0, 255).c_str(), _TRUNCATE);
        using Type = kompoz::Notification::Type;
        n.dwInfoFlags = notification.type == Type::Info      ? NIIF_INFO
                        : notification.type == Type::Warning ? NIIF_WARNING
                        : notification.type == Type::Error   ? NIIF_ERROR
                                                             : NIIF_USER | NIIF_LARGE_ICON;
        if (notification.type == Type::None) n.hBalloonIcon = s.icon;
        Shell_NotifyIconW(NIM_MODIFY, &n);
    }
}

void Win32Tray::sync(const std::vector<std::shared_ptr<kompoz::detail::TrayRequest>>& requests) {
    State& st = *state_;
    if (!st.window) return;
    std::erase_if(st.shown, [&](State::Shown& s) {
        for (const auto& r : requests)
            if (r == s.request) return false;
        st.remove(s);
        return true;
    });
    for (const auto& r : requests) {
        State::Shown* s = nullptr;
        for (State::Shown& x : st.shown)
            if (x.request == r) s = &x;
        if (!s) {
            st.shown.push_back({.request = r, .uid = st.nextUid++});
            s = &st.shown.back();
        }
        if (s->icon) DestroyIcon(s->icon);
        s->icon = iconOf(r->icon);
        if (!s->added) {
            st.add(*s);
        } else {
            NOTIFYICONDATAW n = st.data(*s);
            Shell_NotifyIconW(NIM_MODIFY, &n);
        }
    }
}

} // namespace kompoz::platform
