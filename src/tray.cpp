#include <kompoz/host.hpp>
#include <kompoz/state.hpp>
#include <kompoz/tray.hpp>

#include <atomic>

namespace kompoz {

namespace {
detail::AppControl& control() {
    static detail::AppControl c;
    return c;
}
} // namespace

void detail::setAppControl(AppControl c) { control() = std::move(c); }

void exitApplication() {
    if (control().exit) control().exit();
}

void setAppWindowVisible(bool visible) {
    if (control().setWindowVisible) control().setWindowVisible(visible);
}

bool isAppWindowVisible() { return control().windowVisible ? control().windowVisible() : true; }

bool isTraySupported() { return control().traySupported && control().traySupported(); }

namespace {
bool sameItems(const std::vector<MenuBarItem>& a, const std::vector<MenuBarItem>& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        const MenuBarItem &x = a[i], &y = b[i];
        if (x.text != y.text || x.enabled != y.enabled || x.checked != y.checked || x.separator != y.separator ||
            !sameItems(x.submenu, y.submenu))
            return false;
    }
    return true;
}
} // namespace

Notification rememberNotification(std::string title, std::string message, Notification::Type type, CallSite site) {
    return remember([&] { return Notification(std::move(title), std::move(message), type); }, site);
}

TrayState rememberTrayState(CallSite site) { return remember([] { return TrayState(); }, site); }

void detail::Tray(ImageBitmap icon, std::string tooltip, std::function<void(std::optional<IntOffset>)> onAction,
                  const std::function<void(MenuScope&)>& menu, std::optional<TrayState> state, CallSite site) {
    KompozableScope scope(site);
    static std::atomic<uint64_t> next{1};
    auto& request = remember([] {
        auto r = std::make_shared<TrayRequest>();
        r->id = next++;
        return r;
    });
    KompozHost& host = Kompozer::require().host();
    MenuScope items;
    if (menu) menu(items);
    std::vector<MenuBarItem> built = items.takeItems();
    // Kept up to date as the caller recomposes: the latest callbacks always (the menu's clicks too),
    // the version bumped only when what is shown changed (the platform shows it again then).
    bool changed = !(request->icon == icon) || request->tooltip != tooltip || !sameItems(request->menu, built);
    request->icon = std::move(icon);
    request->tooltip = std::move(tooltip);
    request->onAction = std::move(onAction);
    request->menu = std::move(built);
    // Its state's notifications, shown by the app core after the frame its sendNotification asks for.
    auto notifications = state ? state->notifications() : nullptr;
    SideEffect([request, changed, notifications, &host] {
        if (request->notifications != notifications) {
            if (request->notifications) request->notifications->wake = nullptr;
            request->notifications = notifications;
            if (notifications) {
                notifications->wake = [&host] { host.requestRedraw(); };
                if (!notifications->pending.empty()) host.requestRedraw();  // (sent before it was composed)
            }
        }
        if (!changed) return;
        ++request->version;
        host.requestRedraw();
    });
    DisposableEffect(0, [request, &host]() -> std::function<void()> {
        host.addTrayRequest(request);
        return [request, &host] {
            if (request->notifications) request->notifications->wake = nullptr;
            host.removeTrayRequest(request.get());
        };
    });
}

} // namespace kompoz
