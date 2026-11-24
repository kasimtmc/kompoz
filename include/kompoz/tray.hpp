#pragma once

// An icon of the app's in the system tray (the notification area), as Compose Desktop's Tray: shown
// while it is composed, its icon, tooltip and menu kept up to date as its caller recomposes, gone as it
// leaves the composition. A click on it (Windows' and the StatusNotifierItem hosts' activation) is its
// onAction; a right click opens its menu, the items MenuBar's Menu takes.
//
//   var shown by remember { mutableStateOf(true) }
//   Tray(icon = imageResource("tray.png"), tooltip = "AirLink", onAction = { shown = true }) {
//       Item("Aç", onClick = { shown = true })
//       Separator()
//       Item("Çık", onClick = { exitApplication() })
//   }
//
// With Application's onCloseRequest the app's window can be hidden instead of the app ending, and
// shown again from here (setAppWindowVisible). Linux: a StatusNotifierItem (KDE; GNOME with the
// AppIndicator extension), its menu over com.canonical.dbusmenu; Windows: Shell_NotifyIcon. Elsewhere
// (Android, the web, macOS for now) nothing is shown.

#include "graphics.hpp"
#include "material.hpp"
#include "named.hpp"
#include "runtime.hpp"

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace kompoz {

namespace detail {
// The tray icon the composition asks for: what the platform shows (KompozHost::trayRequests).
struct TrayRequest {
    uint64_t id = 0;
    ImageBitmap icon;
    std::string tooltip;
    std::function<void()> onAction;
    std::vector<MenuBarItem> menu;
    uint64_t version = 0;  // bumped as any of them changes
};

void Tray(ImageBitmap icon, std::string tooltip, std::function<void()> onAction,
          const std::function<void(MenuScope&)>& menu, CallSite site = CallSite::current());
struct TrayImpl {
    using P = Params<names::Icon, names::Tooltip, names::OnAction, BlockWith<MenuScope&>>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void(MenuScope&)> menu;
        if constexpr (P::template blockGiven<A...>()) menu = std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
        Tray(P::get<names::Icon>(ImageBitmap(), args...), std::string(P::get<names::Tooltip>(std::string(), args...)),
             P::get<names::OnAction>(std::function<void()>(), args...), menu, site);
    }
};
} // namespace detail
inline constexpr Composable<detail::TrayImpl> Tray;

// The app ends (Compose Desktop's exitApplication): its window closed, its loop done. From the
// composition or a callback of its (a tray menu's item).
void exitApplication();

// The app's window hidden (the app goes on: its composition, effects and Tray) or shown again and
// brought to the front, as a tray app does on its window's close and its icon's click. Desktops.
void setAppWindowVisible(bool visible);
bool isAppWindowVisible();

// Whether this platform shows a tray icon (Linux with a StatusNotifierItem host, Windows).
bool isTraySupported();

namespace detail {
// What the platform's app core does for the calls above (set while it runs, on its thread).
struct AppControl {
    std::function<void()> exit;
    std::function<void(bool)> setWindowVisible;
    std::function<bool()> windowVisible;
    std::function<bool()> traySupported;
};
void setAppControl(AppControl control);
} // namespace detail

} // namespace kompoz
