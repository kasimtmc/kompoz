#pragma once

// Kompoz's platform on macOS (app/platform.hpp), in Objective-C++ over AppKit: its window an
// NSWindow whose view's layer is a CAMetalLayer the Metal device presents to, at the screen's
// backing scale, its frames on the display's beat (the layer's next drawable waits for it) and
// drawn on while the window is resized; the mouse, the trackpad's scrolling and pinch, the
// keyboard and input methods (NSTextInputClient), the pasteboard, drops of files and text, the
// system's cursors, the light or dark appearance, the screen kept on (an IOKit power assertion),
// the app's menu (Hide, Quit, the window's), links opened by NSWorkspace.

namespace kompoz::platform {

// The app on macOS until it quits (what main returns), or -1 where it can't run there.
// KOMPOZ_PLATFORM=sdl keeps to SDL's platform.
int runMacOS(int argc, char* argv[]);

} // namespace kompoz::platform
