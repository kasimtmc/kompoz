#pragma once

// File dialogs and content:// files on Android (dev.kompoz.KompozFiles), over JNI: FileKit's launchers
// (filekit.hpp) and their PlatformFiles.

#include <kompoz/file_dialog.hpp>

#include <jni.h>

#include <functional>

namespace kompoz::android_files {

// Once, on the UI thread from Java (its class found with the app's class loader): content:// files
// read and written through it from then on.
void init(JNIEnv* e);
// The system's dialog over `activity`; `done` called (from the UI thread) with what it gave.
void show(JNIEnv* e, jobject activity, const kompoz::detail::FileDialogRequest& request,
          std::function<void(kompoz::detail::FileDialogResult)> done);

} // namespace kompoz::android_files
