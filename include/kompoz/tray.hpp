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
// A notification of the desktop's, as Compose Desktop sends one through its tray icon's state:
//
//   val trayState = rememberTrayState()
//   Tray(icon = icon, state = trayState, tooltip = "AirLink")
//   Button(onClick = { trayState.sendNotification(Notification("AirLink", "3 dosya alındı", Notification.Type.Info)) }) { … }
//
// Shown by a Tray composed with that state (one sent before it is composed waits for it): on Linux
// the desktop's notifications (org.freedesktop.Notifications: KDE's, GNOME's, mako, dunst; there
// with no tray too), on Windows the icon's notification (a toast from Windows 10 on). Elsewhere none.
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

// A desktop notification (Compose Desktop's): its title, its text, and its kind (the icon it shows
// on Windows and Linux: none, information, a warning, an error; with None the tray icon's on Linux).
struct Notification {
    enum class Type { None, Info, Warning, Error };
    std::string title;
    std::string message;
    Type type = Type::None;
    Notification(std::string title, std::string message, Type type = Type::None)
        : title(std::move(title)), message(std::move(message)), type(type) {}
    bool operator==(const Notification&) const = default;
};
// (as Compose Desktop's: the notification made once where it is called)
Notification rememberNotification(std::string title, std::string message, Notification::Type type = Notification::Type::None,
                                  CallSite site = CallSite::current());

namespace detail {
// The notifications a TrayState was sent, waiting for the Tray composed with it (its app core shows
// them, on its thread) and what wakes that app's loop for them.
struct TrayNotifications {
    std::vector<Notification> pending;
    std::function<void()> wake;
};
} // namespace detail

// A tray icon's state (Compose Desktop's TrayState), for its notifications: sendNotification shows
// one through the Tray composed with it. From the composition's thread (its callbacks, effects).
class TrayState {
public:
    TrayState() : notifications_(std::make_shared<detail::TrayNotifications>()) {}
    void sendNotification(const Notification& notification) const {
        notifications_->pending.push_back(notification);
        if (notifications_->wake) notifications_->wake();
    }
    const std::shared_ptr<detail::TrayNotifications>& notifications() const { return notifications_; }
    bool operator==(const TrayState& other) const { return notifications_ == other.notifications_; }

private:
    std::shared_ptr<detail::TrayNotifications> notifications_;
};
TrayState rememberTrayState(CallSite site = CallSite::current());

namespace detail {
// The tray icon the composition asks for: what the platform shows (KompozHost::trayRequests).
struct TrayRequest {
    uint64_t id = 0;
    ImageBitmap icon;
    std::string tooltip;
    std::function<void(std::optional<IntOffset>)> onAction;  // (where the icon was clicked, if the platform knows)
    std::vector<MenuBarItem> menu;
    uint64_t version = 0;  // bumped as any of them changes
    std::shared_ptr<TrayNotifications> notifications;  // its state's (none: no state given)
};

void Tray(ImageBitmap icon, std::string tooltip, std::function<void(std::optional<IntOffset>)> onAction,
          const std::function<void(MenuScope&)>& menu, std::optional<TrayState> state = std::nullopt,
          CallSite site = CallSite::current());
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
    using P = Params<names::Icon, names::State, names::Tooltip, names::OnAction, BlockWith<MenuScope&>>;
    template <class... A>
    static void call(CallSite site, A&&... args) {
        static_assert(P::check<A...>());
        std::function<void(MenuScope&)> menu;
        if constexpr (P::template blockGiven<A...>()) menu = std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
        std::function<void(std::optional<IntOffset>)> onAction;
        if constexpr (P::template given<names::OnAction, A...>()) onAction = trayAction(P::template arg<names::OnAction>(args...));
        Tray(P::get<names::Icon>(ImageBitmap(), args...), std::string(P::get<names::Tooltip>(std::string(), args...)), std::move(onAction),
             menu, P::get<names::State>(std::optional<TrayState>(), args...), site);
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
