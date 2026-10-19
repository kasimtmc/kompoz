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

`KOMPOZ_SCRIPT` drives the app without a user: clicks, typing, swipes and screenshots, with coordinates in dp. While the script runs, time is virtual, so animations are the same on every run; when it ends, real time takes over and you can go on using the app from where it left off.

```bash
SDL_VIDEODRIVER=offscreen KOMPOZ_SCRIPT="click 160 263; wait 30; shot out.png; quit" ./build/showcase
```

`SDL_VIDEODRIVER=offscreen` runs without a window, which is useful on a build server (on macOS add `SDL_RENDER_DRIVER=software`, or run it in a window: SDL's offscreen driver makes OpenGL contexts with EGL, which macOS doesn't have). A preview becomes a picture the same way:

```bash
SDL_VIDEODRIVER=offscreen KOMPOZ_PREVIEW=CounterCardPreview KOMPOZ_SCRIPT="wait 10; shot preview.png; quit" ./build/myapp
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

`KOMPOZ_PROFILE=1` logs frame times: for each slow frame, the time spent on input, composition and layout, drawing and presenting, with the offscreen layers it drew and the glyphs it rasterized, and a summary every second.

Android has no environment variables, so there the profiler looks for a file in the app's storage, and its output goes to `adb logcat -s SDL/APP`:

```bash
adb shell run-as dev.kompoz.showcase touch files/kompoz-profile
```

Kompoz records each frame first and draws again only what changed since the frame before; a frame in which nothing on the screen changes is neither drawn nor presented. To see or check this:

- `KOMPOZ_DAMAGE_SHOW=1` outlines what was drawn again in each frame.
- `KOMPOZ_DAMAGE_CHECK=1` also draws each frame from scratch and logs any pixel that differs.
- `KOMPOZ_FULL_REDRAW=1` draws every frame from scratch.

## Environment variables

| Variable | What it does |
|---|---|
| `KOMPOZ_PREVIEW=name` | Shows one preview, or all of them with `*`. |
| `KOMPOZ_SCRIPT="…"` | Drives the app with the commands above. |
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
| `KOMPOZ_DAMAGE_SHOW=1`, `KOMPOZ_DAMAGE_CHECK=1`, `KOMPOZ_FULL_REDRAW=1` | Show, check or turn off redrawing only what changed. |
| `KOMPOZ_LAYOUT_CHECK=1` | Checks every incremental layout against a full one (the tests always do). |
| `KOMPOZ_IMAGE_CACHE=dir` | Where `AsyncImage` keeps the images it downloaded. |
| `KOMPOZ_IMAGE_LOG=1` | Logs where each image came from and how big it was. |

## Packaging

`scripts/package.sh` packages the showcase for every platform whose toolchain it finds; see [Setup](setup.md#packaging).

## See also

- [Testing](testing.md): UI tests that run without a window.
- [Phases](phases.md): what composition, layout and drawing cost, and how to keep each cheap.
