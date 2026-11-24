#pragma once

// The app's tray icons on Windows (Tray): Shell_NotifyIcon on a message-only window of their own
// (its messages reach it through the thread's loop, as the app's window's do), the menu a popup menu
// at the pointer. A click on an icon is its onAction; a right click opens its menu, whose item clicked
// runs its onClick, both on the platform's thread. An Explorer restarted (TaskbarCreated): added again.

#include <kompoz/tray.hpp>

#include <memory>
#include <vector>

namespace kompoz::platform {

class Win32Tray {
public:
    Win32Tray();
    ~Win32Tray();
    Win32Tray(const Win32Tray&) = delete;
    Win32Tray& operator=(const Win32Tray&) = delete;

    void sync(const std::vector<std::shared_ptr<kompoz::detail::TrayRequest>>& requests);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace kompoz::platform
