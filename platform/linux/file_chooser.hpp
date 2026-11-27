#pragma once

// File dialogs on Linux (FileKit's launchers, filekit.hpp): the desktop portal's FileChooser
// (org.freedesktop.portal.FileChooser: GNOME's, KDE's dialogs, over the app's window), else zenity's
// or kdialog's where there is no portal.

#include <kompoz/file_dialog.hpp>
#include <kompoz/strings.hpp>

#include <functional>
#include <string>

namespace kompoz::platform {

// Shown on a thread of its own; `done` called from it. `parentWindow`: the portal's ("x11:1c00007",
// "wayland:<exported handle>"), empty if none.
void showFileChooser(const std::string& parentWindow, const Strings& strings, const kompoz::detail::FileDialogRequest& request,
                     std::function<void(kompoz::detail::FileDialogResult)> done);

} // namespace kompoz::platform
