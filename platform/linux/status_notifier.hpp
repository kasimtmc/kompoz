#pragma once

// The app's tray icons on a Linux desktop (Tray): each a StatusNotifierItem on the session bus
// (org.kde.StatusNotifierItem, registered with org.kde.StatusNotifierWatcher: KDE's tray, GNOME's
// AppIndicator extension, waybar's, ...), its menu a com.canonical.dbusmenu. libdbus loaded at run
// time, on a thread of its own; what the user does with them told to the platform's loop (a wake),
// whose turn runs the request's callback.

#include "../common/system.hpp"

#include <kompoz/tray.hpp>

#include <functional>
#include <memory>
#include <vector>

namespace kompoz::platform {

class StatusNotifier {
public:
    // `wake`: the platform's loop woken (any thread), to take what came (takeActions).
    explicit StatusNotifier(std::function<void()> wake);
    ~StatusNotifier();
    StatusNotifier(const StatusNotifier&) = delete;
    StatusNotifier& operator=(const StatusNotifier&) = delete;

    // The icons as the composition asks for them now (the platform's thread).
    void sync(const std::vector<std::shared_ptr<kompoz::detail::TrayRequest>>& requests);
    // Whether a tray shows them: libdbus there and a StatusNotifierWatcher on the bus.
    bool available() const;
    // What the user did since (an icon activated, a menu item clicked): run on the platform's thread.
    void takeActions();
    // A notification of the desktop's (org.freedesktop.Notifications' Notify, from the bus's thread),
    // sent by `app` for its icon `icon` (shown with it when the notification has no type).
    void notify(const std::string& app, const kompoz::detail::TrayRequest& icon, const kompoz::Notification& notification);

private:
    struct State;
    std::shared_ptr<State> state_;
};

} // namespace kompoz::platform
