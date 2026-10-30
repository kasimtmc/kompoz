# Tools

Previews, rebuilding on save, scripted screenshots, profiling and the environment variables that help while you work.

## Previews

As Compose's `@Preview`, a preview shows one composable on its own, without the rest of the app, in a frame of the size you choose. Write it next to the composable, in your app's sources:

```cpp
#! void CounterCard() {}
KOMPOZ_PREVIEW(CounterCardPreview, .width = 400, .height = 220) {
    MaterialTheme(lightColorScheme(), [] { CounterCard(); });
}

KOMPOZ_PREVIEW(CounterCardDarkPreview, .width = 400, .height = 220, .darkTheme = true) {
    MaterialTheme(darkColorScheme(), [] { CounterCard(); });
}
```

Start the app with `KOMPOZ_PREVIEW` set to a preview's name and its window shows only that preview, at that size:

```bash
KOMPOZ_PREVIEW=CounterCardPreview ./build/myapp
```

`KOMPOZ_PREVIEW='*'`, or a name no preview has, shows all of them one under another, each with its name and size.

The options after the name are a `PreviewSpec`:

| Option | Meaning |
|---|---|
| `.width`, `.height` | The frame's size in dp (360 × 640 by default). |
| `.darkTheme` | What `isSystemInDarkTheme()` returns inside the preview; unset, the system's. |
| `.background` | A color behind the preview; unset, none. |

The text scale is the window's (`KOMPOZ_FONT_SCALE`), not a preview's own. A preview registers itself when the program starts, so it must be in a source file that is linked into the app.

## Rebuild and restart on save

```bash
scripts/dev.sh
```

`dev.sh` builds the showcase, starts it, and rebuilds and restarts it whenever a source file changes. The app comes back where it was: the screen it was on, scroll positions and `rememberSaveable` values are written when it closes and read when it starts, through `KOMPOZ_STATE_FILE`, the way Android brings an app back after ending its process. A build that fails leaves the running app alone. Closing the window or pressing Ctrl+C ends it.

Its arguments are the target, the build directory and what the app is started with:

```bash
scripts/dev.sh showcase build kompoz://components
```

Together with a preview, it gives you a component on its own that comes back a few seconds after each save:

```bash
KOMPOZ_PREVIEW=CounterCardPreview scripts/dev.sh
```

`dev.sh` watches Kompoz's own folders (`include`, `src`, `platform`, `examples`). For an app of your own, copy it and change the folders it watches and the target it builds.

## Scripted runs and screenshots

`KOMPOZ_SCRIPT` drives the app without a user: clicks, typing, swipes and screenshots, with coordinates in dp. While the script runs, time is virtual, so animations are the same on every run; when it ends, real time takes over and you can go on using the app from where it left off. After `realtime`, the rest of the script runs in real time, its frames paced to the display as a person would see them: for measuring with `KOMPOZ_PROFILE`.

```bash
KOMPOZ_HEADLESS=1 KOMPOZ_SCRIPT="click 160 263; wait 30; shot out.png; quit" ./build/showcase
```

`KOMPOZ_HEADLESS=1` runs the app on Kompoz's headless platform, without a window, which is useful on a build server: the GPU renderer draws only into its kept frame (a computer without a GPU has a software one: lavapipe or llvmpipe on Linux, WARP through ANGLE on Windows). Its window is the app's size until the script's `resize`; it has no pointer, keyboard or microphone, and reads neither the desktop's accent color nor the wallpaper's, so the screenshots are the same on every machine. A preview becomes a picture the same way:

```bash
KOMPOZ_HEADLESS=1 KOMPOZ_PREVIEW=CounterCardPreview KOMPOZ_SCRIPT="wait 10; shot preview.png; quit" ./build/myapp
```

The commands, separated by `;`:

| Command | What it does |
|---|---|
| `click x y`, `rclick x y` | A tap, or a right click. |
| `move x y` | Moves the mouse (hover). |
| `down x y`, `up x y` | Presses and releases, for holding down. |
| `drag x1 y1 x2 y2`, `mdrag x1 y1 x2 y2` | A drag with a finger, or with the mouse. |
| `swipe x1 y1 x2 y2 frames` | A drag over that many frames, fast enough to fling. |
| `fdown id x y`, `fmove id x y`, `fup id x y` | Fingers of a multi-touch gesture. |
| `wheel x y dy`, `zoom x y factor` | The mouse wheel, and Ctrl+wheel or a touchpad pinch. |
| `type text` | Types text into the focused field. |
| `key name` | `enter`, `backspace`, `escape`, `tab`, `shift+tab`, `space`, `up`, `down`, `left`, `right`. |
| `resize w h` | Resizes the window. |
| `wait frames` | Lets frames pass (in virtual time). |
| `realtime` | The rest in real time, frames paced to the display. |
| `sleep ms` | Lets real time pass, for work on other threads: an image loading, the wallpaper's colors being read. |
| `shot file.png` | Saves a screenshot. |
| `quit` | Ends the app. |

For tests that check the UI without any window at all, see [Testing](testing.md).

## Seeing recomposition

`Modifier().recomposeHighlighter()` draws a border around a node showing how many times the composable that made it has run, as the Compose snippets' highlighter does: use it to find what recomposes more often than it should.

```cpp
void Row1(const std::string& title) {
    Text(title, Modifier().recomposeHighlighter());
}
```

`KOMPOZ_TRACE=1` logs, for each recomposition, how many scopes ran and how many were skipped.

## Profiling frames

`KOMPOZ_PROFILE=1` logs frame times. A frame is slow when its work takes longer than the display's refresh period, or it comes more than two periods after the one before; for each slow frame it logs the time spent on input, composition and layout, waiting for the GPU to be done with an earlier frame and for the window to give back an image to draw into (`wait`), drawing and presenting, with the nodes it drew and those it kept as they were (with all in them, see [Phases](phases.md#drawing-only-what-changed)), the offscreen layers it drew (and of them how many were composited from the pixels they kept, their content the same as before), the paths' shadows it drew (and of them how many were drawn from the mask they had before), the textures it made for layers and effects (none when one it kept unused fitted) and the glyphs it rasterized. Every second a summary follows: the frames, how many were shown and how many refreshes of the display went by without a frame between two shown back to back, the work's average with its parts (composition and layout, drawing, and of the drawing the recording: the tree drawn into the display list and compared with the last frame's), the wait's average, and on Kompoz's GPU renderer the GPU's own time for a frame. A frame that took the GPU longer than a refresh period is logged with its costliest passes, each named by what it draws into (`window`, `1080x2400`, `2048x2048 R16F`) or `copy WxH`:

```
kompoz: GPU frame 12.2 ms: 1080x2400 10.5, window 1.6, 2048x2402 R16F 0.1
```

The GPU's time is measured with Vulkan's and WebGPU's timestamps (WebGPU where the adapter has `timestamp-query`) and OpenGL ES's timer queries (`EXT_disjoint_timer_query`, on WebGL 2 its `_webgl2` one); Metal's is a frame's alone, from its command buffers. Where none of them is, the profile says so.

Android has no environment variables, so there the profiler looks for a file in the app's storage, and its output goes to `adb logcat -s kompoz`:

```bash
adb shell run-as dev.kompoz.showcase touch files/kompoz-profile
```

Where `run-as` is refused (SELinux: a LineageOS build, for one), these files (`kompoz-profile`, `kompoz-damage-check`, `kompoz-keep-check`, `kompoz-full-redraw`, `kompoz-script`, `kompoz-gpu`) can go in the app's external files instead: `adb shell touch /sdcard/Android/data/dev.kompoz.showcase/files/kompoz-profile`.

A script, too, comes from a file there (the same commands; a frame benchmark repeats on every device):

```bash
adb shell run-as dev.kompoz.showcase sh -c "'echo \"realtime; wait 60; swipe 200 600 200 200 12; wait 120\" > files/kompoz-script'"
```

Kompoz records each frame first and draws again only what changed since the frame before; a frame in which nothing on the screen changes is neither drawn nor presented. To see or check this:

- `KOMPOZ_DAMAGE_SHOW=1` outlines what was drawn again in each frame (each of its areas).
- `KOMPOZ_DAMAGE_CHECK=1` also draws each frame from scratch and logs any pixel that differs (on Android, a file named `kompoz-damage-check` in the app's files, as `kompoz-profile` above).
- `KOMPOZ_FULL_REDRAW=1` draws every frame from scratch (on Android, a file named `kompoz-full-redraw`): with `KOMPOZ_PROFILE`, what drawing all of the screen costs the GPU.
- `KOMPOZ_KEEP_CHECK=1` also records each frame drawing every node and logs where what the frame kept of the nodes that didn't change differs from it, with the number of such frames at exit (on Android, a file named `kompoz-keep-check`).

## Environment variables

On the web, the page's query sets them: `showcase.html?KOMPOZ_GPU=gles&KOMPOZ_PROFILE=1`. On Android, where an app has no environment, any of them is set by a file in the app's files named as it in lower case with dashes, holding its value (`kompoz-profile`, `kompoz-gpu` holding `gles`), as above.

| Variable | What it does |
|---|---|
| `KOMPOZ_PREVIEW=name` | Shows one preview, or all of them with `*`. |
| `KOMPOZ_SCRIPT="…"` | Drives the app with the commands above. |
| `KOMPOZ_HEADLESS=1` | Runs the app without a window, on the desktops (see above). |
| `KOMPOZ_DECORATIONS=kompoz` | In a Wayland session, Kompoz draws the window's title bar and the shadows its edges are resized by, as it does where the compositor draws none (GNOME), instead of the compositor's. |
| `KOMPOZ_PLATFORM=sdl` | Runs the app on SDL's platform on Linux, Windows and the web (in the page's query: `?KOMPOZ_PLATFORM=sdl`), instead of Kompoz's own. |
| `KOMPOZ_PLATFORM=x11` | Runs the app on Kompoz's X11 platform in a Wayland session too (through XWayland), instead of its Wayland platform. |
| `KOMPOZ_STATE_FILE=file` | Writes the saved state (`rememberSaveable`, the navigation back stack) at exit and reads it at start. |
| `KOMPOZ_FONT_SCALE=1.3` | Stands in for the system's font scale on the desktop. |
| `KOMPOZ_SCALE=2.625` | Stands in for a screen's density, to see a phone's layout on the desktop. |
| `KOMPOZ_LANGUAGE=tr` | The language of the library's own texts, instead of the system's (`en` or `tr`). |
| `KOMPOZ_LAYOUT_DIRECTION=rtl` | Right-to-left or left-to-right layout, instead of the system language's. |
| `KOMPOZ_NO_DESKTOP_ACCENT=1` | Ignores the desktop's accent color (Linux, Windows), so screenshots are the same on every machine. |
| `KOMPOZ_NO_WALLPAPER_COLORS=1` | Doesn't read the wallpaper's colors, for the same reason. |
| `KOMPOZ_ATSPI=1` | Serves the semantics tree to screen readers over AT-SPI even when the desktop's accessibility is off (Linux); `0` never. |
| `KOMPOZ_TRACE=1` | Logs how many scopes ran and were skipped in each recomposition. |
| `KOMPOZ_PROFILE=1` | Logs frame times. |
| `KOMPOZ_GPU=vulkan`, `KOMPOZ_GPU=metal`, `KOMPOZ_GPU=webgpu`, `KOMPOZ_GPU=gles` | The GPU renderer's backend: Vulkan (Linux, Windows, Android), Metal (macOS, iOS), WebGPU (the web) or OpenGL ES 3 (where the system has it: Linux, Android; WebGL 2 on the web; ANGLE's on Windows, see [Setup](setup.md#windows)). Without it (or with `KOMPOZ_GPU=auto`) the platform's best, falling back to the next: Metal on Apple's systems; on the web WebGPU, then WebGL 2; elsewhere Vulkan (on Android from Android 10), then OpenGL ES 3. A backend named alone that doesn't start ends the app, and so does none starting. The shaders it compiles are kept in the app's cache directory (`pipelines-vulkan-<version>`, `pipelines-gles-<version>`, beside the images `AsyncImage` keeps; the version is the shaders' own, and files of other versions are removed), so only the first run after the shaders change compiles them. On Android, where an app has no environment, the first word of a file named `kompoz-gpu` in the app's files: `adb shell run-as <package> sh -c 'echo gles > files/kompoz-gpu'`. |
| `KOMPOZ_GPU_DEVICE=name`, `KOMPOZ_GPU_VALIDATION=1` | With it: the GPU whose name contains `name` (Vulkan, Metal on macOS); Vulkan's validation layers where they are installed, Metal's API validation, OpenGL ES's debug output (a debug context): an error either reports is counted (`kompoz_gpu_tests` fails on one). |
| `KOMPOZ_DAMAGE_SHOW=1`, `KOMPOZ_DAMAGE_CHECK=1`, `KOMPOZ_FULL_REDRAW=1` | Show, check or turn off redrawing only what changed. |
| `KOMPOZ_LAYOUT_CHECK=1` | Checks every incremental layout against a full one (the tests always do). |
| `KOMPOZ_KEEP_CHECK=1` | Checks what each frame kept of the nodes that didn't change against drawing every node (the tests always do). |
| `KOMPOZ_IMAGE_CACHE=dir` | Where `AsyncImage` keeps the images it downloaded. |
| `KOMPOZ_IMAGE_LOG=1` | Logs where each image came from and how big it was. |

## Packaging

`scripts/package.sh` packages the showcase for every platform whose toolchain it finds; see [Setup](setup.md#packaging).

## See also

- [Testing](testing.md): UI tests that run without a window.
- [Phases](phases.md): what composition, layout and drawing cost, and how to keep each cheap.
