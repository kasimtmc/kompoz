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

void detail::Tray(ImageBitmap icon, std::string tooltip, std::function<void(std::optional<IntOffset>)> onAction,
                  const std::function<void(MenuScope&)>& menu, CallSite site) {
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
    SideEffect([request, changed, &host] {
        if (!changed) return;
        ++request->version;
        host.requestRedraw();
    });
    DisposableEffect(0, [request, &host]() -> std::function<void()> {
        host.addTrayRequest(request);
        return [request, &host] { host.removeTrayRequest(request.get()); };
    });
}

} // namespace kompoz
