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
// onAction may take where the icon was clicked, for a popup window placed at it (WindowPopup):
// screen coordinates as the platform has them (the host's for the icon on Linux, the click's on
// Windows); none where the host didn't say.
//
//   var popupAt by remember { mutableStateOf<WindowPopup?>(null) }
//   Tray(icon = icon, onAction = { at -> popupAt = WindowPopup(anchor = at) })
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
#include <optional>
#include <string>
#include <type_traits>
#include <vector>

namespace kompoz {

namespace detail {
// The tray icon the composition asks for: what the platform shows (KompozHost::trayRequests).
struct TrayRequest {
    uint64_t id = 0;
    ImageBitmap icon;
    std::string tooltip;
    std::function<void(std::optional<IntOffset>)> onAction;  // (where the icon was clicked, if the platform knows)
    std::vector<MenuBarItem> menu;
    uint64_t version = 0;  // bumped as any of them changes
};

void Tray(ImageBitmap icon, std::string tooltip, std::function<void(std::optional<IntOffset>)> onAction,
          const std::function<void(MenuScope&)>& menu, CallSite site = CallSite::current());
// onAction as a request keeps it: one taking the point given it, one taking nothing called without it.
template <class F>
std::function<void(std::optional<IntOffset>)> trayAction(F&& f) {
    using D = std::decay_t<F>;
    if constexpr (std::is_invocable_v<D&, std::optional<IntOffset>>) {
        return std::function<void(std::optional<IntOffset>)>(std::forward<F>(f));
    } else {
        static_assert(std::is_invocable_v<D&>, "kompoz: Tray's onAction takes nothing or the point it was clicked at");
        std::function<void()> g(std::forward<F>(f));
        if (!g) return {};
        return [g = std::move(g)](std::optional<IntOffset>) { g(); };
    }
}
struct TrayImpl {
    using P = Params<names::Icon, names::Tooltip, names::OnAction, BlockWith<MenuScope&>>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void(MenuScope&)> menu;
        if constexpr (P::template blockGiven<A...>()) menu = std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
        std::function<void(std::optional<IntOffset>)> onAction;
        if constexpr (P::template given<names::OnAction, A...>()) onAction = trayAction(P::template arg<names::OnAction>(args...));
        Tray(P::get<names::Icon>(ImageBitmap(), args...), std::string(P::get<names::Tooltip>(std::string(), args...)), std::move(onAction),
             menu, site);
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
