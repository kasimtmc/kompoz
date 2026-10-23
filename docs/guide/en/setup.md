# Setup

Build Kompoz, add it to your own CMake project, and run your app on Linux, Windows, Android and the web.

## Requirements

- A C++20 compiler: GCC 13 or newer, or Clang 17 or newer (Kompoz is tested with GCC 16 and Clang 22). GCC 13 crashes on some coroutine lambdas, with an internal compiler error that GCC 14 doesn't have.
- CMake 3.24 or newer.
- SDL3 and FreeType.
- Optional: HarfBuzz for text shaping (Arabic joining, Indic scripts, ligatures, emoji sequences). Without it, text is drawn character by character.
- Optional on Linux: libcurl, libwebp and libturbojpeg (3.0 or later), which `AsyncImage` uses to download images and decode WebP and JPEG. libdbus is loaded at run time, if present, to read the desktop's accent color.

On Linux, install these from your distribution's packages. For Android, the web and Windows, Kompoz builds SDL3, FreeType and HarfBuzz from source; `scripts/fetch-deps.sh` downloads them (see below).

## Build the library and the showcase

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/showcase
```

This builds the core library (`kompoz_core`), the SDL3 backend (`kompoz_sdl3`), the showcase app, and the headless tests (`./build/kompoz_tests`).

Useful options:

| Option | Default | What it does |
|---|---|---|
| `KOMPOZ_BUILD_SDL3` | `ON` | Build the SDL3 backend. `OFF` builds only the core and the tests, with no window system. |
| `KOMPOZ_BUILD_EXAMPLES` | `ON` | Build the showcase. |
| `KOMPOZ_BUILD_TESTS` | `ON` (`OFF` on Android, iOS and the web) | Build the headless tests. |
| `KOMPOZ_VENDORED_DEPS` | `ON` on Android, iOS, the web and Windows | Build SDL3, FreeType and HarfBuzz from `third_party/` instead of using the system's. |

On desktops that have lld, Kompoz links with it unless you chose another linker: about one second instead of GNU ld's twenty-five for the showcase.

## Use Kompoz in your project

Add Kompoz as a subdirectory, a clone or a git submodule, and link the SDL3 backend. The backend brings the program's entry point.

```cmake
cmake_minimum_required(VERSION 3.24)
project(myapp LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

set(KOMPOZ_BUILD_EXAMPLES OFF)
set(KOMPOZ_BUILD_TESTS OFF)
add_subdirectory(kompoz)        # a clone or a git submodule

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE kompoz::sdl3)
kompoz_copy_assets(myapp)       # the fonts and the licenses next to the binary
```

`kompoz_copy_assets` copies what every app ships, the bundled Noto Sans fonts and the licenses, next to your binary each time it is built.

Or install Kompoz once and find it as a package, on Linux, macOS or Windows. Built with the system's SDL3, FreeType
and HarfBuzz, it finds them there again; built with its own (`scripts/fetch-deps.sh`: Windows, or
`-DKOMPOZ_VENDORED_DEPS=ON`), it installs them beside it:

```bash
cmake -S kompoz -B kompoz/build -DCMAKE_BUILD_TYPE=Release -DKOMPOZ_BUILD_EXAMPLES=OFF -DKOMPOZ_BUILD_TESTS=OFF
cmake --build kompoz/build -j
cmake --install kompoz/build --prefix ~/.local
```

```cmake
find_package(kompoz 0.1 REQUIRED)  # -DCMAKE_PREFIX_PATH=~/.local, if that isn't searched already
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE kompoz::sdl3)
kompoz_copy_assets(myapp)
```

With vcpkg or Conan, from a clone of the repository (its overlay port and its recipe build that clone, with the
package manager's SDL3, FreeType and HarfBuzz), then `find_package(kompoz)` as above:

```bash
vcpkg install kompoz --overlay-ports=kompoz/packaging/vcpkg/ports
conan create kompoz/packaging/conan --build=missing -s compiler.cppstd=20
```

On Linux and macOS `AsyncImage` decodes JPEG with libjpeg-turbo (2.x or 3.x), WebP with libwebp and downloads with
libcurl (Windows, Android and the web have their own). Each is used when found; `-DKOMPOZ_WITH_JPEG=ON` (or `WEBP`,
`CURL`) requires it, `OFF` leaves it out. vcpkg's features `jpeg`, `webp`, `curl` and Conan's options `with_jpeg`,
`with_webp`, `with_curl` (all on by default) are the same.

Your `main.cpp` defines `kompoz::createApplication` instead of `main`:

```cpp
void Counter() {
    auto count = remember([] { return mutableStateOf(0); });

    Column(Modifier().padding(16), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
        Text("Count: " + std::to_string(count.get()), {.font = typography().titleLarge});
        Button([count] { count.set(count.get() + 1); }, [] { Text("Add"); });
    });
}

Application kompoz::createApplication(int, char**) {
    return {.title = "Demo", .content = [] { MaterialTheme(lightColorScheme(), Counter); }};
}
```

All of Kompoz is in one header, `#include <kompoz/kompoz.hpp>`, and in the `kompoz` namespace.

### The Application

`createApplication` gets the command line and returns an `Application`:

| Field | Meaning |
|---|---|
| `title` | The window's title. |
| `width`, `height` | The window's first size in dp, on the desktop. |
| `content` | The composable that fills the window. |
| `fontPath`, `boldFontPath`, `mediumFontPath` | Fonts to use instead of the bundled ones. |
| `imageDiskCacheBytes` | How much of the images `AsyncImage` downloads is kept on disk (64 MB by default; 0 keeps none). |

On the desktop the command line can carry a deep link that the app opens with, such as `./myapp kompoz://item/42` (see [Navigation](navigation.md)).

## Platforms

### Linux

The build above is all it takes. Dark mode and the desktop's accent color come from the XDG desktop portal and follow it as it changes: `isSystemInDarkTheme()` and `dynamicLightColorScheme()` see them. The wallpaper is read from the desktop's settings (KDE Plasma, GNOME, Cinnamon, MATE, Xfce) for `wallpaperLightColorScheme()`.

### Windows

Windows builds are cross-compiled from Linux with MinGW-w64 (on Arch: `pacman -S mingw-w64-gcc`). The dependencies are built from source and linked statically, so the program needs only Windows' own DLLs and runs on Windows 10 and later.

```bash
scripts/fetch-deps.sh
cmake -S . -B build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows -j
```

Ship the executable with the `fonts/` and `licenses/` folders next to it. The tests also run under Wine: `WINEPATH=/usr/x86_64-w64-mingw32/bin wine build-windows/kompoz_tests.exe`.

Kompoz's GPU renderer on OpenGL ES (`KOMPOZ_GPU=gles`) uses ANGLE where the app has ANGLE's `libEGL.dll` and `libGLESv2.dll` next to it: OpenGL ES over Direct3D 11, the same on every Windows GPU. vcpkg builds them from Google's source (`vcpkg install angle:x64-windows-release`); without them, the GPU driver's own OpenGL ES is used.

### Android

You need the Android SDK, NDK 29.0.14206865, the SDK's CMake 4.1.2 (from Android Studio's SDK Manager, or `sdkmanager "ndk;29.0.14206865" "cmake;4.1.2"`) and JDK 17 or newer (Android Studio's bundled JBR will do).

```bash
scripts/fetch-deps.sh
cd examples/showcase/android && ./gradlew installDebug
```

`fetch-deps.sh` downloads SDL3, FreeType and HarfBuzz into `third_party/` at pinned versions, checks their SHA-256 sums and applies the patches in `scripts/patches/`. Gradle builds Kompoz's CMake project with the NDK into `libSDL3.so` and your app's `libmain.so`.

To make your own app, start from the showcase's `android/` folder: it has only the manifest, a theme and a short `MainActivity` built on SDL's `SDLActivity`. The fonts go into the APK from Kompoz's `assets/` folder. On Android, Kompoz supports the soft keyboard with suggestions, edge-to-edge insets, the back gesture and predictive back, the system's dark mode and font size, dynamic color on Android 12+ and TalkBack.

### Web

Kompoz builds for the browser with Emscripten (on Arch: `pacman -S emscripten`). SDL3, FreeType and HarfBuzz are built from source; the fonts and images are packed into a `.data` file next to the page.

```bash
scripts/fetch-deps.sh
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DKOMPOZ_BUILD_TESTS=OFF
cmake --build build-web -j
python3 -m http.server 8765 --directory build-web
```

Then open `http://localhost:8765/showcase.html`. There are no threads on the web, so work you would put on a thread runs in a coroutine instead. Characters the bundled font lacks (emoji, CJK, Arabic) are drawn with the browser's own fonts. The page's query is the app's environment (`showcase.html?KOMPOZ_GPU=gles`, see [Tools](tools.md#environment-variables)).

For the web build of your own app, copy the Emscripten options from `examples/showcase/CMakeLists.txt`.

### macOS and iOS

On macOS Kompoz builds with Homebrew's SDL3, FreeType and HarfBuzz (`brew install sdl3 freetype harfbuzz`) as on Linux, and runs: CI builds it there, runs the tests and an app in a real window (Cocoa, Metal). It hasn't been used by hand on a Mac yet, and screen readers there (NSAccessibility) aren't connected. iOS hasn't been built: it needs an Xcode project or CMake setup like the Android one.

## Packaging

```bash
scripts/package.sh
```

`package.sh` packages the showcase for every platform whose toolchain it finds, into `dist/`: a Linux `.tar.gz`, a Windows `.zip`, an Android `.apk` signed with the debug key, and a web `.zip`. Name the platforms to build only some: `scripts/package.sh linux web`. The packages carry the version from `CMakeLists.txt`, plus the commit when they are not built from the release's tag. Use the script as a model for packaging your own app.

## See also

- [Tutorial](tutorial.md): your first app, step by step.
- [Tools](tools.md): rebuilding on save, previews, screenshots and profiling.
