#pragma once

// Kompoz's platform on the web (app/platform.hpp), with Emscripten: the page's canvas, its CSS
// pixels the points and its drawing buffer at the device pixel ratio, drawn into with WebGPU or
// WebGL 2, frames on the browser's animation frames (the loop paused while there is nothing to
// draw); the mouse, its wheel, touch and the keyboard through Emscripten's html5 events, typing
// and input methods (and a phone's keyboard) through a hidden textarea, the clipboard through the
// copy and paste events and navigator.clipboard, files and text dropped on the page, CSS cursors,
// the screen kept on (the Wake Lock API), the light or dark preference, links in new tabs, the
// address's #fragment as the route (deep links), vibration for haptics.

namespace kompoz::platform {

// The app started on the page (it goes on in the browser's event loop after main returns), or -1
// where it can't run there.
int runWeb(int argc, char* argv[]);

} // namespace kompoz::platform
