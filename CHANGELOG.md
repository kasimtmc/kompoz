# Changelog

**English** · [Türkçe](CHANGELOG.tr.md)

## Unreleased

- **Screen readers on Linux**: the semantics tree is served over AT-SPI (libdbus loaded at run time), so Orca reads
  Kompoz apps and can use them: roles, names, states, values and text, actions, and events for what changes. On when
  the desktop's accessibility is; `KOMPOZ_ATSPI=1` / `0` to force it on or off.
- **Screen readers on Windows**: the semantics tree is served through UI Automation, for Narrator and NVDA: control
  types, names, states, positions, the patterns to use the nodes, and events for what changes.
- **Installing and packages**: `cmake --install` and `find_package(kompoz)` (`kompoz::core`, `kompoz::sdl3`,
  `kompoz_copy_assets`) on Linux, macOS and Windows, with the system's SDL3, FreeType and HarfBuzz or with those built
  from `third_party/`, installed beside it; a vcpkg overlay port and a Conan recipe, with AsyncImage's libraries as
  features and options.
- **JPEG with libjpeg-turbo 2.x** as well as 3.x; AsyncImage's libraries are found from their CMake packages or
  pkg-config, and `KOMPOZ_WITH_JPEG` / `WEBP` / `CURL` make each automatic, required or off.
- **CI** on every platform: the tests, the platform tests, and an app built against each way of installing Kompoz,
  run and clicked (in a real window on Windows and macOS, where Kompoz now runs, and offscreen on macOS too). `CONTRIBUTING.md`, issue forms and
  `scripts/release.sh` for publishing a release's packages.
- **Kompoz's own GPU renderer**, being built (`KOMPOZ_GPU=vulkan`): one core over a thin layer per native graphics
  API: Vulkan on Linux, Windows and Android first, then Metal on Apple's systems, WebGPU on the web, and OpenGL ES 3 where there is none of those (old GPUs, WebGL 2, ANGLE on Windows). Rounded rects, borders, lines, arcs,
  shadows, text, images, clips and layers are drawn on the GPU with exact anti-aliasing (a straight edge at any angle
  cuts each pixel as exactly as a path's polygon does, a square corner as its two edges, a ring or a line thinner than a
  pixel covers as much as it is wide), and paths too: filled by the
  non-zero or even-odd rule and stroked with every cap and join (the inside of a turn outlined without crossing itself,
  so a curve's inner edge is as light as its outer one), their coverage summed on the GPU exactly as FreeType's
  rasterizer works it out, curves within 0.2 px; layers can be cut to any outline. Gradients (linear, radial, sweep,
  every tile mode) are worked out per pixel from their stops, on fills, paths and text alike, as `Brush::colorAt`
  does (`Brush::geometry` says where one lies). All 29 blend modes are drawn, those the blend unit can't do worked out
  in the shader from a copy of what is under the draw, and color filters (matrices, lighting, tints in any mode) are
  applied to each pixel. Everything is drawn through any transform (turned, stretched, skewed, in perspective) sharp
  and exactly anti-aliased: text laid out and layers made at the size they show, paths projected, clips under a turn
  cut exactly. Render effects blur as Skia does (a Gaussian of Android's sigma, wide ones scaled down, every tile
  mode). Its shaders are written once (Slang) and compiled for every API by
  `scripts/shaders.py`; tests draw its scenes on a real device and compare them with pictures.
- **All of Compose's blend modes**: `BlendMode` gains Overlay, ColorDodge, ColorBurn, Hardlight, Softlight,
  Difference, Exclusion, Hue, Saturation, Color and Luminosity; `blend(source, destination, mode)` works any of the 29
  out (W3C's compositing spec, as Skia), and `ColorFilter::tint` takes them all.
- **Color filters are drawing state** (`GraphicsContext::pushColorFilter`): images, painters and `drawImage` draw
  through their filter instead of drawing a filtered copy, so a backend applies it per pixel. A vector painter's paths
  keep their own paints through a filter (a gray matrix made a multi-colored vector one color before).
- **`Matrix`**, Compose's 4x4 transform, operation for operation: `translate`, `scale`, `rotateX`/`Y`/`Z`,
  `timesAssign`, `invert`, `resetToPivotedTransform`, and `map` for points and rects, with their perspective.
- **Transforms without layers**: `GraphicsContext::pushTransform(matrix)`; `DrawScope`'s `rotate`, `scale` and
  `withTransform` go through it (with `rotateRad`, `scale(s)`, `inset(horizontal, vertical)` and
  `DrawTransform::transform(matrix)` added), so, as in Compose, what they draw isn't clipped to the drawing's area
  any more. The display list follows transforms (perspective too) for damage and culling.
- **Clips of any shape**: `GraphicsContext::pushClipRoundRect` and `pushClipPath`, inside or outside (`ClipOp`, now
  in graphics.hpp); `DrawScope::clipPath` and `clipRect(…, op)` go through them instead of a layer whose outside is
  erased. On the GPU a rounded rect clip is worked out in the shader, a path clip read from the coverage mask. With
  SDL's software renderer `clipPath` drew the area it erased black (that renderer has no DstOut); it is now masked on
  the CPU.
- **Shapes make outlines, as in Compose**: `Shape::createOutline(size, layoutDirection)` gives an `Outline` (`Rectangle`,
  `Rounded` with a `RoundRect`, whose corners may be elliptical (`CornerRadius(x, y)`), or `Generic` with any `Path`);
  `GenericShape(builder)` makes a shape from a path built in its size and direction, `OutlineShape(createOutline)` from
  any outline; `Path::addRoundRect(RoundRect)`. Backgrounds, borders, shadows, clips and ripples all go by the outline,
  and `clip` (and a clipping `shadow`, a bounded ripple) clips through the drawing context instead of a layer. A
  polygon shape no longer has to be star-shaped about its middle: SDL_Renderer masks outline layers and clips with the
  shape instead of cutting them with a fan.
- **graphicsLayer as Compose's**: `rotationX`, `rotationY` and `cameraDistance` (3D, the matrix Android's RenderNode
  makes: the camera `cameraDistance` × 72 px away), `blendMode`, `colorFilter`, `outsets`, `size` to read in the lambda,
  and `CompositingStrategy` as in Compose: offscreen only where it must be (faded, a blend mode, a color filter,
  `Offscreen`), `ModulateAlpha` fading each draw instead; otherwise the matrix and the clip go straight into the
  drawing. A `GraphicsLayer` drawn takes the same properties, with `pivotOffset` and `setRectOutline` /
  `setRoundRectOutline` / `setPathOutline` as in Compose.
- **Render effects, as in Compose**: `BlurEffect(radiusX, radiusY, edgeTreatment)` (any `TileMode`) and
  `OffsetEffect`, each taking another to go through first, as a `graphicsLayer`'s or a `GraphicsLayer`'s
  `renderEffect`: what is drawn within its bounds goes through it, then is cut to its shape if it clips.
  `Modifier.blur` is now Compose's: a `graphicsLayer` with a `BlurEffect`, blurred only when both radii are;
  `BlurredEdgeTreatment` holds a shape (`Rectangle`, `Unbounded`, or any shape to cut the blur to).
  `GraphicsContext::pushEffectLayer` replaces `pushBlurLayer`.
- **Progressive blurs, as in Compose 1.13**: `BlurRadiusSpec` (`uniform`, `verticalGradient`, `horizontalGradient`,
  `linearGradient`, `radialGradient`, from one radius to another or through `BlurStop`s) for
  `Modifier.blur(spec, edgeTreatment, alpha)` and `Modifier.blur { … }` (`BlurScope`, read while drawing), or as a
  layer's effect (`createRenderEffect`). The GPU renderer draws them as Compose does.
- **What a blur blurs** (`BlurType`, Kompoz's own, on every `Modifier.blur`): `Family` (Compose's), `Morph` (frosted
  glass: what is behind, within the node's shape; `GraphicsContext::drawBackdrop`) and `Self` (only what the node
  draws itself, its children sharp over it).
- **Fixed**: building with GCC 14 (the text field's caret handle) and with MSVC; the MinGW toolchain file dropped the
  roots it was given; offscreen on macOS the window couldn't be created (SDL made it an OpenGL one, which needs EGL
  there): it now draws with the software renderer.

## 0.1.1 — 2026-10-18

- **Dynamic color from three sources, each an API of its own**, which the app or its user picks between (the library
  never switches by itself): the app's own scheme, the system's palette (`dynamicLightColorScheme()`: the
  wallpaper's on Android 12+, the desktop's accent color on Linux and now on Windows) and the wallpaper itself
  (`wallpaperLightColorScheme()`: Linux desktops, Windows, Android 8.1+). The showcase asks.
- **Colors from any picture**, picked as Android picks a wallpaper's: `TonalPalette::fromImage`, `quantizeColors`
  (the Celebi quantizer) and `seedColors` (System UI's ranking).
- **Documentation site**: `scripts/docs.py` builds an HTML guide in English and Turkish from `docs/guide/` (every C++
  example in it compiled by `--check`), with the API status page.
- **Smaller**: `Offset` compares with `==`; the script's `sleep ms`; `KOMPOZ_NO_WALLPAPER_COLORS`; tests of the
  platform code that runs without a window (`kompoz_platform_tests`).

## 0.1.0 — 2026-10-16

The first public release. Kompoz brings Jetpack Compose's model and API to C++20 and runs on Linux, Windows, Android
and the web.

- **Runtime**: composable functions with smart recomposition and skipping, `remember`/`rememberSaveable` (restored
  after Android ends the process), effects, `CompositionLocal`, C++20 coroutines, `Flow`/`StateFlow`/`SharedFlow`,
  `ViewModel` with `SavedStateHandle`, lifecycle, navigation with type-safe routes, nested graphs and deep links.
- **Layout**: incremental measuring, `Box`/`Row`/`Column`, `FlowRow`/`FlowColumn`, `FlexBox`, `Grid`, `Layout`,
  `SubcomposeLayout`, `LookaheadScope`, intrinsics, window insets, right-to-left layouts.
- **Lists and scrolling**: lazy lists, grids and staggered grids, pagers, carousels, nested scroll, Android's fling
  curve, overscroll stretch, snapping, pull to refresh, the mouse wheel.
- **Material 3** (Compose Material 3 1.4 with the Expressive components) and `MaterialTheme` with dynamic color: from
  the wallpaper on Android 12+, from the desktop's accent color on Linux, from any color with
  `TonalPalette::fromSeed`.
- **Drawing and text**: `Canvas`, brushes, paths, shapes (Material's shape library and morphing), blurred shadows,
  `graphicsLayer`, vectors, images loaded over HTTP, HarfBuzz shaping, bidirectional text, selection, text fields
  with undo, autofill and hardware keyboard editing.
- **Input and accessibility**: touch and multi-touch gestures, mouse and keyboard, drag and drop, the predictive back
  gesture; a semantics tree for TalkBack.
- **Localization**: the library's own texts (context menus, accessibility descriptions, the pickers) in English or
  Turkish by the system language (`LocalStrings`, `KOMPOZ_LANGUAGE`); an app can provide its own.
- **Tools**: headless UI tests, previews, `scripts/dev.sh` (rebuild and restart on save, keeping the state),
  scripted screenshots, frame profiling, `scripts/package.sh` (packages for every platform).

What was built, step by step, is in [docs/components.md](docs/components.md)'s build log.
