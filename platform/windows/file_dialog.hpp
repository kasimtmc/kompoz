#pragma once

// File dialogs on Windows (FileKit's launchers, filekit.hpp): the common item dialogs (IFileOpenDialog,
// IFileSaveDialog), owned by the app's window.

#include <kompoz/file_dialog.hpp>

#include <functional>

namespace kompoz::platform {

// `owner`: the window's HWND. Shown on a thread of its own; `done` called from it.
void showWin32FileDialog(void* owner, const kompoz::detail::FileDialogRequest& request,
                         std::function<void(kompoz::detail::FileDialogResult)> done);

} // namespace kompoz::platform
