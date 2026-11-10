# Changelog

**English** · [Türkçe](CHANGELOG.tr.md)

## Unreleased

- **Screen readers on Linux**: the semantics tree is served over AT-SPI (libdbus loaded at run time), so Orca reads
  Kompoz apps and can use them: roles, names, states, values and text, actions, and events for what changes. On when
  the desktop's accessibility is; `KOMPOZ_ATSPI=1` / `0` to force it on or off.
- **Screen readers on Windows**: the semantics tree is served through UI Automation, for Narrator and NVDA: control
  types, names, states, positions, the patterns to use the nodes, and events for what changes.
- **Installing and packages**: `cmake --install` and `find_package(kompoz)` (`kompoz::core`, `kompoz::app`,
  `kompoz_copy_assets`) on Linux, macOS and Windows, with the system's FreeType and HarfBuzz or with those built from
  `third_party/`, installed beside it; a vcpkg overlay port and a Conan recipe, with AsyncImage's libraries as
  features and options.
- **JPEG with libjpeg-turbo 2.x** as well as 3.x; AsyncImage's libraries are found from their CMake packages or
  pkg-config, and `KOMPOZ_WITH_JPEG` / `WEBP` / `CURL` make each automatic, required or off.
- **CI** on every platform: the tests, the platform tests, and an app built against each way of installing Kompoz, run
  and clicked (in a real window on Windows and macOS, where Kompoz now runs, and offscreen on macOS too).
  `CONTRIBUTING.md`, issue forms and `scripts/release.sh` for publishing a release's packages. The vcpkg jobs build
  the Kompoz of the commit they test (vcpkg's binary cache, keyed by the port's files and not by the sources, restored
  an earlier one's). The Windows job keeps the showcase with ANGLE's DLLs and the Visual C++ runtime they need beside
  it as a download (`kompoz-showcase-windows-x86_64`): it runs on any Windows 10+, one without a GPU driver too.
  On macOS the app's click must change what the window shows, and the window the script resizes must be photographed
  at its new size.
- **Kompoz's own GPU renderer**, being built (`KOMPOZ_GPU=vulkan`, `metal`, `webgpu`, `gles`, or `auto`: the platform's best, falling back to the next): one core over a thin layer per native graphics
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
- **Shadows of any outline drawn as Compose draws them** (`GraphicsContext::drawShadowPath`'s default, through
  effect layers): the GPU renderer no longer draws a heart's or a cookie's shadow as its bounds'.
- **Elevation shadows as Android draws them**: `graphicsLayer`'s (and `GraphicsLayer`'s) `shadowElevation`,
  `ambientShadowColor` and `spotShadowColor`, an ambient and a spot shadow lit from above the window's top middle
  (`GraphicsContext::drawElevationShadow`, `ShadowLight`); on the GPU renderer as Skia's analytic shadow is on Android's
  GPU, or blurred as Skia does otherwise. `Modifier.shadow` is now Compose's: a `graphicsLayer`, the content clipped to
  the shape by default when the elevation is above 0 (it wasn't before: pass `clip = false` to keep it unclipped).
- **Compose 1.9's `Shadow`**: `brush`, `alpha` and `blendMode` for `dropShadow` and `innerShadow`; a drop shadow ends
  where Compose's bitmap does.
- **`DrawScope` as Compose's**: every draw call takes Compose's parameters in Compose's order (a color or a brush,
  `alpha`, `style`, `colorFilter`, `blendMode`; `drawArc`'s `useCenter`, `drawRoundRect`'s `CornerRadius`, `drawImage`'s
  source and destination rects and `FilterQuality`, `drawText`'s color, brush, alpha, shadow, decoration and draw style);
  `Stroke(width, miter, cap, join, pathEffect)`, its width 0 a hairline. This changes call sites: `drawRoundRect(color,
  radius, topLeft, size)` is now `drawRoundRect(color, topLeft, size, CornerRadius(radius))`, a style comes after the
  alpha, `drawArc` takes `useCenter`, `drawText(layout, topLeft, color)` is `drawText(layout, color, topLeft)`.
- **Images sampled as `FilterQuality` says** on the GPU renderer (the nearest pixel, bilinear, mipmaps, Mitchell's
  cubic), from premultiplied texels: a transparent pixel's color no longer bleeds into its scaled neighbors.
- **`Path` as Compose's** (Skia's SkPath): conics (ovals, round rects and arcs exactly), `Path::Direction` with Skia's
  starting points, `transform(Matrix)` (perspective too), `isConvex()`, `arcToRad`/`addArcRad`, `rewind`,
  `PathIterator`/`PathSegment`, `PathHitTester`, `contains` exact on curves, and `op` keeping curves. Shapes added whole
  now go counterclockwise by default, as Compose's: pass `Path::Direction::Clockwise` for the old way round; a turn of
  `arcTo` is a point, as Skia's (`addArc` or `addOval` for a whole oval).
- **Shaders as Compose's**: `LinearGradientShader`, `RadialGradientShader`, `SweepGradientShader`, `ImageShader`
  (tiled each way) and `CompositeShader`, through transforms; `Brush::shader` (`ShaderBrush`, made again only for
  another size), `Brush::composite`, `withTransform`, `intrinsicSize`, `createShader`, and `lerp` between brushes;
  `BlurRadiusSpec::shader` blurs each pixel by a shader's alpha. `Density` moved from `layout.hpp` to `graphics.hpp`.
- **Compose's `Canvas` and `Paint`** (`graphics::Canvas`: the `Canvas` composable has the name): `save`/`restore`,
  `saveLayer`, transforms with `skew` and `concat`, clips, every draw in a `Paint`; `DrawScope::drawIntoCanvas`, and a
  `DrawScope` over a `Canvas`. **Fixed**: draw scopes over a node (`drawBehind`, `drawWithContent`, `Canvas`,
  `drawWithCache`, recordings, custom painters) saw a density of 1 and left to right; a `DrawScope`'s shapes of
  negative size weren't drawn (they are sorted, as Skia sorts them).
- **`Canvas.drawVertices` and `Vertices`**: triangle meshes as Skia draws them, the paint's shader at the texture
  coordinates through the vertex colors by a blend mode; the GPU renderer draws each triangle as the rasterizer does
  (in perspective too), and works composite shaders out per pixel (before: drawn into a texture first).
- **Bitmaps to draw into**: `ImageBitmap(width, height, config, hasAlpha)` with every `ImageBitmapConfig` kept as
  Android keeps it (Alpha8 drawn as a mask in the paint's color), `graphics::Canvas(bitmap)`, `readPixels` and
  `PixelMap`; the GPU renderer keeps such a bitmap as a texture until its pixels are asked for. **Fixed**: SDL_Renderer
  drew images without the blend mode in effect, and the software renderer lost the background under translucent
  shader fills drawn in a layer.
- **`Paint.isAntiAlias = false`**: drawing not anti-aliased, as Skia decides each pixel (its middle inside the shape;
  a hairline a pixel each step), on the GPU renderer exactly and on SDL_Renderer by its own rasterizers. **Fixed**: a
  `DrawScope` line with butt ends (the default) was drawn half its width longer at each end, as square ones are.
- **Vectors as Compose's**: `ImageVector` is Compose's model, a tree of `VectorGroup`s and `VectorPath`s with names,
  `tintColor`/`tintBlendMode`, equal by value; path data as `PathNode`s (`addPathNodes`, `PathParser`, `PathBuilder`,
  `PathData`, `toPath`), read and drawn as Compose's parser does (arcs in eighths of a turn). A painter draws a vector
  into a bitmap its size in pixels and that 1:1, as Compose's `VectorPainter`: an Alpha8 mask for one in one color,
  through its filter, else its tint, else that color; `Icon(imageVector)` is `Icon(rememberVectorPainter(…))` (its
  default size), Material icons are named and the auto-mirrored ones vectors of their own. The GPU renderer draws a mask
  without a layer and keeps Alpha8 bitmaps as textures. Changed as Compose has them: a path's trim measures its first
  contour, a vector drawable's `strokeWidth` is 1 unset, its `tint`/`tintMode` are read, gradients are in their path's
  units. Removed (Compose has none): `DrawScope::drawVector`, `GraphicsContext::drawVector`, `ImageVector::mirrored`/
  `autoMirrored`/`paths`/`commands`, `VectorPath::parse`, `Brush::scaled`.
- **Composable vector content as Compose's**: `rememberVectorPainter(defaultWidth, defaultHeight, …, content)` with the
  `kompoz::vector` composables `Group` and `Path` (the content given the viewport's size; its name, tint and mirroring
  too; without a viewport, its default size in pixels), and `RenderVectorGroup` drawing a tree through a `VectorConfig`
  of each name (`VectorProperty`). The `ImageVector::Builder&` content it took before is gone.
- **Fixed** (SDL_Renderer): what a transform enlarges is drawn at its size, not enlarged as pixels (a vector's root
  scale made its icons blurred), and text is laid out at the size it is drawn at (in a scaled layer, in a bitmap).
- **Layout in whole pixels, as Compose's**: sizes, constraints and places are rounded to the nearest pixel (a half up)
  where Compose rounds them: a padding's sides and a spacing before they add up, weights shared with the pixels left over
  going to the first, a tab row's tabs the row divided, sizes and places along an animation; a text's size the pixel
  above it, its baselines rounded (`Density::roundToPx`). Icons and edges no longer fall between pixels.
- **Fixed**: a double tap crashed when its composable ran again between the taps (the tap detector held on to the
  settings of an element that had been replaced).
- **Fixed**: a `rememberSaveable` under a `SaveableStateHolder` kept its value when its composable left while its
  provider stayed, and brought it back; it goes now, as in Compose (the showcase's time picker came back in 24 hours).
- **Colors and color spaces as Compose's**: a `Color` is Compose's 64 bits (sRGB 8 bits a channel; another space's
  components half floats, with the space's id), with `convert`, `compositeOver`, `luminance`, `toArgb`, `hsv`/`hsl`
  and `isSpecified`, and the `colorspace` package (`ColorSpace`, the 20 `ColorSpaces`, `Rgb` with transfer functions
  and ICC curves, connectors, adaptation). `lerp(Color)` mixes in Oklab and animated colors go as Compose's
  `Color.VectorConverter`; gradients mix in sRGB, as Skia's; a color of another space is drawn in sRGB, not rounded to
  8 bits on the way. `ImageBitmap` takes a color space. **Changed**: `Color`'s `r`, `g`, `b`, `a` fields are its
  `red()`, `green()`, `blue()`, `alpha()`, and its components are kept as Compose keeps them (8 bits in sRGB).
- **Path effects and PathMeasure as Skia's**: dashes, corners, stamps and chains as Skia makes them (a closed contour's
  dashes joined round its start, curves dashed as pieces of themselves, any chain of effects), applied where the path
  is drawn, at the pixels it is drawn at; `PathMeasure` measures and cuts curves as `SkContourMeasure` does (pieces of
  curves as curves, `isClosed`, `resScale`). **Changed**: `PathEffect` is a value made by its functions (`isNone`,
  `filterPath`); its fields and `apply` are gone, and a path's effect travels with its stroke (`PathStroke::pathEffect`).
- **The GPU renderer on OpenGL ES 3** (`KOMPOZ_GPU=gles`): the same core and shaders over OpenGL ES 3.0 (needing
  float targets: ES 3.2, or EXT_color_buffer_float or _half_float), drawing what Vulkan draws pixel for pixel (the
  tests' pictures alike on Mesa, NVIDIA and two phones' Adrenos); on Android chosen by a `kompoz-gpu` file;
  `kompoz_gpu_tests` runs on it, in CI too, and on a phone from adb shell.
- **Fixed**: the GPU renderer showed the window as one color where its GPU doesn't happen to sample a picture at an
  undefined place as wanted (a phone's Adreno, on Vulkan as on OpenGL ES): the kept frame was copied to the window
  without the shape its texels go across, and filtered, a pixel off on some GPUs (it is copied texel for pixel now).
- **The GPU renderer on the web**, on WebGL 2 (`showcase.html?KOMPOZ_GPU=gles`: a web page's query is now its app's
  environment, through `platform/web/shell.html`); `kompoz_gpu_tests` builds as a page that emrun opens and reports
  on, the same pictures in a browser (in CI on Chrome's SwiftShader).
- **The GPU renderer on Windows through ANGLE** (`KOMPOZ_GPU=gles` with ANGLE's `libEGL.dll` and `libGLESv2.dll` beside
  the app, which vcpkg builds): OpenGL ES over Direct3D 11; `kompoz_gpu_tests` runs on it in CI, on WARP.
- **The GPU renderer on Metal** (`KOMPOZ_GPU=metal`, macOS and iOS): the same pictures as on Vulkan and OpenGL ES, with
  Metal's API validation on in CI; an app draws into its window's layer, `kompoz_gpu_tests` (Metal by default on
  Apple's systems) into textures alone.
- **The GPU renderer on WebGPU** (`showcase.html?KOMPOZ_GPU=webgpu`): Dawn's webgpu.h on the web, the same pictures
  as everywhere (in CI on Chrome's SwiftShader, and on a phone's Chrome: Adreno through Dawn); a web build is linked
  with ASYNCIFY, through which a readback waits for the GPU. `kompoz_gpu_tests` runs on Android's Vulkan too
  (`KOMPOZ_GPU=vulkan`, without a window).
- **The GPU renderer's shaders compiled once**: Vulkan's pipeline cache and OpenGL ES's programs' binaries are kept in
  the app's cache directory (`pipelines-vulkan`, `pipelines-gles`, beside the downloaded images; `Surface::pipelineCache`
  names the file), so only an app's first run compiles them: on a phone's Adreno its first frame was drawn in 465 ms on
  Vulkan and 621 ms on OpenGL ES, and is in 21 and 22 ms after that. On OpenGL ES, pipelines that differ only in
  blending share one program, compiled once.
- **Fixed**: on Android's Vulkan, with the phone turned from its natural way up, the GPU renderer made its swapchain
  anew every frame (the compositor would have had to turn each image, so every present was "suboptimal"): its images
  are now the display's way up, and the frame is turned into them as it is copied there (`Device::swapchainTurns`), as
  Android asks of Vulkan apps. On a phone's Adreno turned sideways, a screen scrolled at 113 frames a second instead of
  38, with 2.4 ms of work a frame instead of 26.
- **`KOMPOZ_PROFILE` measures the GPU**: on Kompoz's GPU renderer each frame's GPU time and its passes' (Vulkan's and
  WebGPU's timestamps, OpenGL ES's timer queries; Metal's frame as a whole), a frame over the display's refresh period
  logged with its costliest passes; the frames shown and the refreshes missed between them; the wait for the GPU apart
  from drawing. A frame is slow past the display's refresh period (it was past 8 ms). `Device::setGpuTiming` and
  `takeGpuTimes` give the times. A script's `realtime` runs the rest of it in real time, frames paced to the display, for
  measuring.
- **The GPU renderer's shader made for what it draws**: its fragment shader is specialized for each batch (Vulkan's
  specialization constants, Metal's function constants, WebGPU's overrides, OpenGL ES's defines) to the kinds, paints,
  clips, filters and blend mode it has, so that a pixel no longer pays for every branch of the whole shader. On a
  Redmi Note 9 Pro's Adreno 618 a frame of the showcase's Components screen took the GPU 30–50 ms instead of 400–460, a
  plain copy to the window 1.3 ms instead of 34. Where a driver can't make a variant (that Adreno's Vulkan compiler
  couldn't one), the whole shader draws it; `PipelineDesc::constants` carries them.
- **What an opaque background hides isn't drawn**: a frame drawn again (on the GPU renderer and SDL_Renderer alike)
  starts from the last opaque solid rect that covers all of what changed, under nothing but translations, scales and rect
  clips: what came before it there, and the white under everything, are left out (`DisplayList::opaqueCover`, `replay`'s
  `hidden`). On Intel's GPU the showcase's Components screen took 5.9 ms to draw instead of 7.3–10.1.
- **The GPU renderer's passes touch only what they draw**, as a tiling GPU (a phone's) wants: a pass drawing into part of
  a target loads and stores only that part (`beginPass`'s `area`: Vulkan's render area, a scissored clear on OpenGL ES;
  scissors kept within it everywhere), the coverage mask clears only what it uses, the copy to the window doesn't load
  what the window held (`Device::discard`: Vulkan's and Metal's "don't care", OpenGL ES's invalidation; a clear cost
  an Adreno 618's OpenGL ES four times the load), OpenGL ES invalidates the screen before its copy into it, and on Vulkan
  the kept frame is submitted before the window's image is waited for. A frame's GPU time is what it was busy with, its
  passes together, not the waits between its submits.
- **A frame's paths in one coverage mask, drawn once**: the paths of every pass until a flush (each layer's too) share
  the mask's shelves and are rasterized in one pass before the passes run, not each layer's in a mask pass of its own
  just before it; past what a mask holds, a generation goes into a mask of its own, so no pass is split for it. On a
  Redmi Note 9 Pro's Vulkan a frame of the Components screen took the GPU 38–40 ms instead of 48–50.
- **Layout looks for popups only while there are some**: the nodes holding popups are counted, and while none does
  the walk of the whole tree after each layout looking for them is left out.
- **The walk after layout finds out once what each element is to it**: a node keeps, for each of its modifier's
  elements, which of the kinds acted on after layout it is (and whether it clips), found out when the elements change,
  instead of trying each kind on every element at every walk. On a Redmi Note 9 Pro scrolling the Components screen,
  compose and layout of a slow frame took 13.7 ms (median) with this and the popup count, instead of 15.9.
- **Scrolling moves the kept pixels** (Kompoz's GPU renderer): what moved by whole pixels within a rect clip, as a
  scrolled list does, is moved in the kept frame, not drawn again; only the strip that came into view is drawn, with what
  didn't move along (where it is and where moving took it). Where moved pixels would be wrong (what is under them isn't
  one color, a clip of another shape or a layer around them, part of a pixel) or as much would be drawn, what changed is
  drawn as before. Damage comes in up to eight separate areas, in whole pixels, not one rect around all of it
  (`DisplayList::areas`, `moves`, `allowMoves`; `gpu::Renderer::drawFrame`, `move`); an opaque rect covers an area
  when it covers the whole pixels the area touches. On a Redmi Note 9 Pro scrolling the Components screen, a frame took
  the GPU 4–14 ms instead of 27–38 and 50–60 frames a second were shown instead of 11–41, a twentieth to a fifth of the
  screen drawn again instead of a sixth to two thirds.
- **Layers whose content stays the same keep their pixels** (Kompoz's GPU renderer): a layer that only fades, turns,
  grows or moves by whole pixels is composited from the texture it had, and what its effect made of it, not drawn
  again; what is drawn in a layer is compared with last frame's in the layer's own space (a brush of one color whatever
  its area), each layer known by its node (`GraphicsContext::keyLayer`, `layerContent`). On a Redmi Note 9 Pro, a list
  pulled past its top took the GPU 27–33 ms a frame instead of 43–44. `KOMPOZ_PROFILE` says how many of a slow frame's
  layers were kept.
- **What a layer holds is its content, whatever its transform**: what is drawn in a layer is recorded where it shows
  at rest (without the layers' own transforms) as well as through them, so a layer that grows past the screen (a list's
  stretch pulled further) holds what it held and stays kept; one that shrinks still gets what it brings onto the screen.
  On a Redmi Note 9 Pro, a list pulled past its top took the GPU 22–24 ms a frame instead of 27–33.
- **The walk after layout goes only where there is something to do**: what a node's elements want of it (positions,
  visibility, gesture exclusion, keeping the screen on, a frame rate, a custom indication) is found out as the node is
  measured, each node counts those in its subtree, and the walk skips subtrees with none. On a Redmi Note 9 Pro it took
  0.65 ms of a frame of the Components screen instead of 3.1.
- **`KOMPOZ_PROFILE`'s summary gives the work's parts**: composition and layout, drawing, and of the drawing the
  recording (the tree drawn into the display list, compared with the last frame's).
- **What a frame keeps where it was is copied, not drawn again**: a node unchanged, in the same place and state, takes
  last frame's commands and what it knew of the nodes in it as they were, without going through them one by one; a
  node drawn writes down only its own place. On a Redmi Note 9 Pro an idle screen's recording took 3.2 ms instead of 4.
- **A bottom sheet scaffold's sheet moves without composing anything**: where the sheet is (dragged, settling) is read
  as the scaffold is laid out, not as it is composed, as Compose's: dragging it lays the scaffold out again and
  composes neither it nor its content.
- **A scaffold's content padding follows its bars without composing the content again**: `Scaffold`'s and
  `BottomSheetScaffold`'s padding is read where it is used, as Compose's. `Modifier::padding`, `consumeWindowInsets`
  and the `contentPadding` of lists, grids, pagers, carousels and buttons read it as they lay out, so a top bar
  collapsing as a list scrolls lays out again what is under it and composes nothing. `PaddingValues`'
  `calculateTopPadding()` (and start, end, bottom) read it now, wherever they are called (a composable reading it is
  composed again when it changes); its fields keep what it was when the content was composed, and two such paddings
  are the same when they follow the same scaffold. (`ButtonDefaults`' paddings are `const`, no longer `constexpr`.)
  Scrolling the showcase's Components screen under its collapsing bar composed the screen once.
- **`animate*AsState` gives a state, read where it is needed**, as Compose's `State<T>`: an `AnimatedState<T>`. Used
  as a value in a composable, that scope is composed again on every frame it moves, as before; read with `.value()`
  while drawing (`drawBehind`, `graphicsLayer { }`), only that node is drawn again, and while laying out only it is
  laid out again; read nowhere, it asks for no frames. A new target on the way goes on from where it is then.
  (Code that kept the result in `auto` and used it as a `float` or `Color`, `std::max` or a member, takes `.value()`.)
- **A composition local can be provided from a state**: `LocalContentColor.provides(animateColorAsState(...))` gives
  the local's readers the state's value where they read it, so as it moves only they are composed again (an `Icon`, a
  `Text`), not the composable providing it.
- **A top app bar takes its scrolled color however it was first composed**: its state's height offset limit is a
  state, as Compose's, so what reads how far content has gone under the bar follows the limit once the bar sets it.
  Worked out before that (in the bar's first composition), the scrolled container color never followed the content,
  and the bar kept its color unless something composed it again for another reason.
- **Navigation items and top app bars animate without composing around them**: the navigation bar's, the short
  navigation bar's and the wide rail's items read their indicator as they lay it out and draw it, and give their icon
  and label their animated colors as states (only those are composed again); a top app bar's container color goes over
  as it is drawn. An animation read while drawing keeps frames coming until it is done, drawn or not (one that never ends,
  as long as it is drawn), and `ComposeTestRule::waitForIdle` waits for it.
- **Transitions compose around them only as they start and end**: `AnimatedVisibility`, `AnimatedContent` (and so
  `NavHost` and `Crossfade`) and `animateEnterExit` read their progress as the content is laid out and drawn, as
  Compose's do, and compose their caller again when they get there (a frame asked for at that time), not on every frame
  on the way. A transition turned on its way goes back from where it is at that frame. With the navigation items' and
  the app bars', switching the showcase's tabs back and forth five times on a Redmi Note 9 Pro took 2.9–3.1 s of the
  main thread instead of 3.9, its composition and layout 2.3 s instead of 3.3.
- **Buttons, FABs and morphing shapes animate without composing around them**: a button's and a FAB's elevation, a
  toggle button's colors, an extended FAB's width and label and a button group's held button are read as they are
  laid out and drawn, and so are the corners of a morphing shape (a pressed or checked button's: a `Shape`'s
  `animatedCorners`, read where its outline is made; so `radiusFor`, `radiiFor` and `cornersFor` are no longer
  `constexpr`). A clickable's and an indication's ripple takes the content color
  as it is drawn (`CompositionLocal::currentLive`), so a color provided from a state doesn't compose them again.
- **Selection controls animate without composing around them**: a checkbox's colors and tick, a switch's thumb, a radio
  button's ring and dot, a list item's container, a segmented button's check and label, a tab's indicator and color, a
  slider's thumb and a drag handle are read as they are laid out and drawn; checked, selected or held, the caller is
  composed again once.
- **Menus, tooltips, fields and indicators animate without composing around them**: a menu's and a tooltip's way in
  and out, the modal wide rail's slide and scrim and the wide rail's width, a modal bottom sheet's scrim, a text
  field's floating label and indicator, a toggle FAB's size, corners and colors, a wavy indicator's wave, the
  scrollbar's color, a marked menu item, a chip's icon, the pull-to-refresh arrow, the dropdown and split button arrows
  and an animated vector's picture (`rememberAnimatedVectorPainter`) are read as they are laid out and drawn; what
  goes away once it is out is composed again when it gets there. `ToggleFloatingActionButtonScope::checkedProgress()`
  reads the progress where it is called (in a `graphicsLayer`, only that is drawn again).
- **Kompoz's GPU renderer is what everything is drawn with**: SDL_Renderer's drawing is gone. Without `KOMPOZ_GPU`
  an app took it, so a phone or a desktop ran none of the GPU renderer's work unless asked for it by name; now
  nothing (or `auto`) is the platform's best: Metal, WebGPU then WebGL 2, Vulkan then OpenGL ES 3. None starting ends
  the app with a message. Without a window (`SDL_VIDEO_DRIVER=offscreen`) Vulkan and Metal draw only into the kept
  frame, which screenshots are taken from. A Windows computer without a Vulkan driver (a virtual machine, Remote
  Desktop, CI) draws through ANGLE shipped with the app.
- **An app run by a script that can't start says so in its log**: with `KOMPOZ_SCRIPT` set (as on CI) no message box
  waits for someone to close it. The Conan test draws on Mesa's lavapipe built for Windows.
- **KOMPOZ_PROFILE counts only refreshes missed by frames that were due**: between two frames shown, it counted the
  refreshes that went by while the app was busy, and a running script (as in the benches) made it busy while it waited
  out its frames with nothing changing on the screen: a pause of half a second came out as some 60 refreshes missed.
  It counts them now only while what the app shows asks for frames (`ComposeHost::wantsFrame`: not a timer, work on
  another thread or a script), and only from when it started asking. (The benches' "missed" in the entries above count
  those pauses too: the first second, the tab opening, had 60–70 of them.) The Xiaomi 14 Ultra's bench, the tab opened
  and the Components screen scrolled down and up ten times, missed 5 refreshes in all.
- **A lazy list's first frame composes what fills its view**: with no item measured yet, it guessed items 48 dp tall
  and composed a view and a half of those, which for tall items (the Components screen's sections) was nearly all of
  them, composed and measured for nothing. It composes the first item now, then as many as the sizes say fill the view,
  in the same frame, as Compose composes items as it measures them. Opening the Components tab the first time took the
  desktop under 5 ms of composition and layout instead of 18. (What is focused or brought into view has to be
  composed, as in Compose: an item half a view past the view no longer is just because the guess was short.)
- **A path's coverage is kept from frame to frame**: a path the GPU renderer draws again as it was, or whole pixels on
  (a scrolled list's, a screen fading in or out), takes the coverage it rasterized before from a mask of its own (2048
  px each way, paths up to 512 × 512 px of it, and half of it each way), placed to a 64th of a pixel; only new paths
  are rasterized, before the frame's passes. Once that mask is full it starts over after the frame, unless that
  frame's paths fill most of it themselves: then those in it are kept and the rest drawn as before, rather than all
  rasterized again each frame. Going between the Home and Components tabs five times on a Redmi Note 9 Pro missed
  20–22 refreshes instead of 36. `KOMPOZ_PROFILE` counts paths drawn and kept in its slow frames.
- **Stale pixels left by scrolling and by path effects, gone**: what changed is drawn again as far as drawing it
  reaches, and `KOMPOZ_DAMAGE_CHECK` now finds no pixel off by more than a 255th on the showcase's Components screen,
  where it found some off by up to 150 in two thirds of the frames. A path drawn through a path effect reaches past
  the path as far as the effect says (`PathEffect::reach`: a stamp's corner farthest from where it is put, as Skia's
  computeFastBounds outsets an effect's bounds): the arrows stamped along a wave left their tips behind as they slid.
  A drop shadow round a path reaches as its drawing does (`detail::shadowPathReach`, a mitered spread's points
  included). And an item with an elevation shadow isn't moved with a scrolled list's pixels any more: its shadow
  falls from the light, which stays where it is, so moved it looked as it had where it was; it is drawn again.
- **Kompoz's own platform layer**, being built in place of SDL3 (the window, input, the frame loop and the app's
  lifecycle, on each platform its own, frames on the display's own beat as Compose's are): the app core takes
  Kompoz's own events now (`platform/app/platform_event.hpp`), SDL's turned into them where they come in, and
  `KOMPOZ_SCRIPT` gives the core its own instead of putting SDL events in SDL's queue. The app core
  (`platform/app/app_core.cpp`: the frame, the drawing, the profile, the script, the checks) is apart from the
  platform it runs on, which it asks for what it needs through `platform::Platform` (its window and GPU surface, its
  measures, its settings, the clock, the log) and which paces its frames; SDL is the first platform under it. On
  Android every `KOMPOZ_` setting can now be given by a file of its name in the app's files, not only a few. The
  text, the images, the screen readers' bridges and the desktop's color readers no longer use SDL: the log, the
  app's files (Android's APK assets through what the platform gives), the program's directory and the cache
  directory are `platform/common/system`'s, and the modules on threads of their own tell the platform with a
  `platform::Notifier` instead of SDL events; the platform tests free what libdbus keeps at their end, as `SDL_Quit`
  did. Each platform module is now where it belongs, in `kompoz::platform`: what every platform shares in
  `platform/common/` (fonts, images, the wallpaper's colors), the desktops' own in `platform/linux/` and
  `platform/windows/`, Android's in `platform/android/`; `platform/sdl3/` holds only the SDL platform. PNG is read,
  and screenshots and the GPU tests' pictures are written, with libpng (the system's, vcpkg's or Conan's, else built
  from `third_party/` with zlib: `scripts/fetch-deps.sh`), a file's gamma and color space honored as Android and the
  browsers do, a 16-bit one that says nothing of its gamma read as sRGB; BMP is read by Kompoz itself (1 to 32 bits a
  pixel, uncompressed or with bit fields); libpng's and zlib's licenses are in `assets/licenses` with the others. On the
  desktops `KOMPOZ_HEADLESS=1` runs an app on Kompoz's own headless platform, in place of SDL's offscreen driver
  (`SDL_VIDEODRIVER=offscreen`): its window a size, shown and focused at once, without a pointer, a keyboard or a
  microphone, reading neither the desktop's accent color nor the wallpaper's, its loop and timers Kompoz's; the GPU
  device without a window, Vulkan from the system's loader or OpenGL ES 3 through EGL (Mesa's surfaceless platform;
  ANGLE's on Windows). The GPU tests make their devices the same way, and on
  the web their WebGL 2 context straight from Emscripten, without SDL. **Android no longer uses SDL**: an app's
  activity extends Kompoz's own `dev.kompoz.KompozActivity`, whose `KompozView` (a `SurfaceView`) Kompoz draws into
  with a Vulkan surface or an EGL window surface on its `ANativeWindow`; the app runs on the activity's UI thread, as
  Compose does, its frames coming from the display's Choreographer (the vsync's time as the frame's) and its touches,
  mouse, keys and lifecycle from the view and the activity; work from other threads wakes the UI thread's looper (an
  eventfd), a coroutine's delay too (a timerfd), and what is left of each beat composes a lazy list's next items
  ahead. The app's statics are made for one app a process: an activity created again in a process that ran one ends
  it, and the system starts the app anew, as SDL's did. Measured against SDL's build on a Xiaomi 14 Ultra (120 Hz)
  and a Redmi Note 9 Pro (60 Hz), as many refreshes are missed scrolling by hand and in flings; SDL's three patches
  for its Android side are gone with it, and the showcase's footer no longer says SDL3.
  **A Wayland session runs on Kompoz's own Wayland platform** (`platform/linux/wayland`): an `xdg_toplevel` that the
  compositor decorates (`xdg-decoration`), or where it doesn't (GNOME; anywhere with `KOMPOZ_DECORATIONS=kompoz`)
  Kompoz itself: a title bar it composes above the app, in the system's colors (its dark theme, its accent), the title
  in the middle and the buttons as the desktop lays them out (GNOME's `button-layout`), dragged to move the window,
  double-clicked to maximize it, the window menu on the secondary button, and shadows around the window that the
  pointer or a finger resizes it by, drawn into with Vulkan (`VK_KHR_wayland_surface`) or OpenGL ES (a
  `wl_egl_window`) at the compositor's scale (fractional-scale and viewporter, else the surface's integer scale),
  frames on its frame callbacks; the pointer, the wheel (a notch is 1), touch, touchpad pinches (`pointer-gestures`)
  and the keyboard (xkbcommon, keys repeated at the compositor's rate), typing through `text-input-v3` too, the
  clipboard and files or text dropped on the window through the data device, the cursor's shape (`cursor-shape`), the
  screen kept on (`idle-inhibit`), `xdg-open` for links, the light or dark preference from the portal's
  `color-scheme`. `libwayland-client`, `libwayland-egl`, `libxkbcommon` and EGL are loaded at run time and the
  protocols' code is generated ahead (`scripts/wayland-protocols.sh`), so building needs only their headers; where
  they aren't there, a Wayland session's apps run on the X11 platform (XWayland). **An X11 session runs on Kompoz's
  own X11 platform** (`platform/linux/x11`): a window the window manager decorates, drawn into with Vulkan
  (`VK_KHR_xlib_surface`) or OpenGL ES (EGL's X11 platform) at the desktop's scale (`Xft.dpi`, followed as it
  changes), frames on the display's beat; the pointer, its wheel (smooth, as XInput 2 tells it), touch and touchpad
  pinches through XInput 2, the keyboard through an input method (XIM, told where the field is where it can be), the
  clipboard (in parts when it is large) and drops of files and text as selections (XDND), the desktop theme's cursors
  (libXcursor), the refresh rate from XRandR, the screen kept on through `org.freedesktop.ScreenSaver`. libX11, libXi,
  libXcursor and libXrandr are loaded at run time; `KOMPOZ_PLATFORM=x11` runs it in a Wayland session too, through
  XWayland. What the two share (keys, links, the desktop's services) is in `platform/linux/desktop_services`, OpenGL
  ES through EGL in `platform/common/egl_window` (no EGL header needed to build it; ANGLE's on Windows too). **Windows
  runs on Kompoz's own Win32 platform** (`platform/windows`): a window aware of each monitor's DPI (per-monitor v2,
  the app's size in dp on the monitor it opens on), its title bar dark with Windows' dark mode, drawn into with Vulkan
  (`VK_KHR_win32_surface`) or OpenGL ES (ANGLE's beside the program, else the driver's through WGL), frames on the
  display's beat and drawn on while Windows resizes or moves the window; the mouse (captured while a button is down)
  and its wheel, touch (`WM_POINTER`; a pen as the mouse), the keyboard (AltGr not taken for Ctrl) and the input
  method (IMM32, at the field), the clipboard, drops of files and text (OLE), the system's cursors, the screen kept
  on, the accent color, the wallpaper's colors and UI Automation as before, a microphone (waveIn) and a camera (its
  device interface) for `mediaQuery`. **The web runs on Kompoz's own web platform** (`platform/web`): the page's
  canvas, its CSS pixels the points and its drawing buffer at the device pixel ratio (followed as it changes), drawn
  into with WebGPU or WebGL 2, frames on the browser's animation frames and the loop paused while there is nothing to
  draw; the mouse (followed outside the canvas while a button is down), the wheel, touch and the keys through
  Emscripten's html5 events; typing, input methods and a phone's keyboard (the field's kind as its `inputmode`)
  through a hidden textarea; the system's clipboard through the copy and paste events (and `navigator.clipboard`),
  where SDL kept one of its own; files (read into the virtual file system) and text dropped on the page; CSS cursors;
  `keepScreenOn` as a screen wake lock; the light or dark preference; links in new tabs, the route in the address's
  #fragment, vibration for haptics, as before. **macOS runs on Kompoz's own macOS platform** (`platform/macos`,
  Objective-C++ over AppKit): an `NSWindow` whose view's layer is the `CAMetalLayer` the Metal device presents to, at
  the screen's backing scale, frames on the display's beat and drawn on while the window is resized; the mouse, a
  trackpad's scrolling (a tenth of its points, a wheel's notches whole, as SDL) and pinch, the keys (Command as
  Kompoz's Ctrl) and input methods (`NSTextInputClient`, told where the field is), the pasteboard, drops of files and
  text, the system's cursors, the light or dark appearance, `keepScreenOn` as an IOKit power assertion, the app's menu
  (Hide, Quit, the window's), links opened by `NSWorkspace`. On all of them Ctrl+C in the terminal (the console on
  Windows) and SIGTERM quit the app as SDL's platform made them, its state written on the way out, so
  `scripts/dev.sh`'s restarts keep the screen again. **SDL3 is gone**: with a platform of Kompoz's own everywhere,
  `platform/main.cpp` picks the one to run on (headless, Wayland, X11, Windows, macOS, the page) and SDL is out of the
  code, the build, the packages and CI. The platform layer's target is `kompoz::app` and its option `KOMPOZ_BUILD_APP`
  (`kompoz::sdl3` and `KOMPOZ_BUILD_SDL3` are gone: link `kompoz::app`); `KOMPOZ_PLATFORM=sdl` is gone; neither
  `scripts/fetch-deps.sh`, the vcpkg port, the Conan recipe nor Homebrew needs SDL3, and its license is no longer
  among an app's. On Linux the build needs Wayland's and xkbcommon's headers or X11's. A touchpad's two fingers
  turning now turn what `transformable` holds on Wayland, X11 and macOS (the gesture's rotation reaches
  `ComposeHost::zoom`, which SDL's pinch never carried).
- **A path's shadow is drawn from the mask it had before** (as Compose keeps a `dropShadow`'s or `innerShadow`'s
  blurred bitmap): the GPU renderer drew a shape's shadow that isn't a rounded rect (a heart, a star, a cookie) from
  scratch each time, a layer and a blur in two passes each; it now draws it white once into a texture of its own and
  then draws that, tinted with the shadow's color, for as long as it is drawn from the same path at the same scale,
  with the same blur and spread (and an inner one's offset), as far into a pixel (to a 64th): moved by whole pixels
  or in another color, it isn't drawn again. Masks undrawn for 10 s go, and all of them when memory is trimmed.
  Overscrolling the showcase's Components screen on a Redmi Note 9 Pro (its top has such shadows, drawn again when
  the list starts and stops stretching), a run had 11–12 frames that took the GPU longer than a refresh instead of
  20–21, 270–298 ms of them instead of 484–496. `KOMPOZ_PROFILE` counts paths' shadows drawn and kept in its slow
  frames.
- **Only what changed is copied onto the window**: the kept frame was copied onto each swapchain image whole, at
  each frame; an image now holds what it was presented with (as Vulkan keeps it, the swapchain no longer clipped),
  and one showing a frame from a few frames ago (`Device::swapchainAge`) has only what the frames since changed copied
  into it (their areas and where their moves went, overlapping ones as one), each part in a pass area of its own
  (`Renderer::blit`'s regions), as Android's renderer draws only a buffer's damage; all of it when that is half of it
  or more, or the image is new. On a Redmi Note 9 Pro at rest on the showcase's moving drawings (Components), the GPU
  took 2.9–4.0 ms a frame instead of 5.8–7.9; a list scrolled, which changes most of the screen, costs as before.
- **An Android app on Vulkan shows its window again when it comes back from the background**: Android gives the
  window a new native window then, and the Vulkan surface made for the old one could no longer be presented to
  ("dequeueBuffer failed: No such device" at each frame): the screen stayed black. The surface and its swapchain are
  made again for a new native window (`Surface::vulkanNativeWindow`, SDL's window pointer property), and when Vulkan
  says the surface is lost; none is presented to while there is no native window.
- **Layer textures come in a few sizes and are kept unused for 10 s** (as Android's renderer keeps them): a texture
  for a layer or an effect is made in one of the sizes Skia's approximate fit gives (a power of two up to 1024 px, by
  half of one past it) and taken again by whatever needs that size, where it was made to 32 px and taken only by what
  needed at most 64 px less: a layer clipped to the screen as it scrolls in (a full-width one, its height changing each
  frame) made one of 6–9 MB each frame. Those unused are kept 10 s instead of 60 frames, the least lately used going
  first past 12 frames' worth of RGBA (the budget of Android's renderer), and go at once when the app goes to the
  background or the system is short of memory (`Renderer::trimMemory`). Scrolling the showcase's Components screen
  with real swipes on a Redmi Note 9 Pro, a run made 39–50 textures (80–93 MB) instead of 74 (142 MB), and 109 (152
  MB) instead of 156–166 (261–377 MB) in the runs that draw less of the screen again; the missed refreshes changed
  little (22–26 instead of 26; 59 instead of 50–68). `KOMPOZ_PROFILE` counts the textures made in its slow frames.
- **The GPU's memory is taken in blocks of 4 MB at most** (as Skia takes it): the Vulkan backend's grew to 256 MB, and
  a driver clears the memory it gives, so a block took as long as it was big: a 128 MB one 9–18 ms on a Xiaomi 14
  Ultra and a Redmi Note 9 Pro, in the frame whose texture needed it, and again each time blocks had emptied and were
  needed again (six times in one run on the 14 Ultra). A texture bigger than half a block has memory of its own.
  Scrolling the showcase's Components screen with real swipes on the 14 Ultra, slow frames fell from 11–19 a run to 8
  and missed refreshes from 54–64 to 45–47; the longest a memory allocation took fell from 12.5 ms to 2.1 there, and
  from 18 ms to 5.6 on the Redmi.
- **Lazy lists and grids compose ahead in the pauses between frames** (as Compose prefetches): while one scrolls, the
  item past those it has composed that way is composed in the time before the next frame is due
  (`ComposeHost::requestIdleWork` and `idle()`, which the app calls there), if what an item of its content type took
  before fits in it, so the frame it scrolls in doesn't compose it. One item ahead of those around the view.
  LazyColumn, LazyRow and the grids built on them, and the staggered grids. Scrolling the showcase's Components screen
  with real swipes on a Redmi Note 9 Pro, frames whose composition took more than 16 ms fell from 11–22 a run to 1–5,
  the longest from 72–109 ms to 41–61.
- **The pipeline cache is kept for the shaders it was made from**: its file is named after them (scripts/shaders.py
  writes each generated header's version, `Renderer::shadersVersion()` puts them together), and an app removes the
  backend's files of other shaders when it starts. A driver keeps what it is given, so an older build's pipelines
  stayed in the cache for good: a Redmi Note 9 Pro's had grown to 6.5 MB, read at every start, where the pipelines of
  the shaders in use took 0.2 MB (the desktop's: 12.7 MB and 1.2).
- **The pipeline cache is read in one go**: the file the GPU renderer keeps its compiled pipelines in was read a
  character at a time and checked byte by byte: 6.5 MB took a Redmi Note 9 Pro 70 ms and 16 ms at every start, before
  the renderer was made. It is read in one read now and checked eight bytes at a time (a file of the old format is
  dropped once, and made again). The first frame came 102–127 ms after the window was shown instead of 191–212 (the
  cache made anew there was 0.2 MB: the old one held mostly older shaders' pipelines).
- **The window's settings are read once for all that changed them**: a change of the window's size, scale, insets,
  theme or language marks them to read again, once before the next frame or input, instead of at each event that says
  so. Each read asks Android for its settings, palettes and view configuration (4 ms on a Redmi Note 9 Pro), and a
  window starts with three such events: the first frame's input took 4 ms instead of 11–16.
- **An Android app's first frame comes half a second sooner**: making its window, SDL took it out of fullscreen mode
  and waited up to 500 ms for the size to change whenever its surface was as big as the display, which a window laid
  out edge to edge (under the status and navigation bars) always is; nothing changed, and the window gaining focus
  meanwhile waited for the same lock, so every start ran the half second out. It waits now only when fullscreen mode
  is on (scripts/patches/SDL-android-window-style-wait.patch). On a Redmi Note 9 Pro the first frame came 180–200 ms
  after the window was shown instead of 680–710.
- **A Morph is made ten times faster**: `polarRadii`, which a Morph takes of both its shapes, tries each edge only
  against the directions it spans seen from the middle (and one more each way; all of them for an edge at or across
  the middle) instead of every direction against every edge: the same radii, exactly, for a tenth of the time (0.011
  ms an outline on the desktop instead of 0.106). It was a third of composing the showcase's Components screen.
- **A fill's and a layer's inside cost the GPU little**: a pixel well inside a solid rounded rect or a layer (half a
  pixel or more in from its edges, and its corners' radii on one axis), with no clip, color filter or perspective,
  takes its color, or the layer's texel at its alpha, from one value the vertex shader works out, not from the 14 the
  rest of the fragment shader reads (what needs a pixel's neighbours, the steps in perspective and a texture's, worked
  out before; after it, every mipmap level comes from those: images, glyphs and image paints through their maps). A
  Redmi Note 9 Pro's Adreno 618 spent most of a fill on reading those: a screen of a fill now takes it 1.5 ms instead
  of 2.3. Going between the Home and Components tabs, its GPU time fell from 13.2–13.8 ms a frame on average to
  11.5–11.7 ms, scrolling the Components screen from 9.5–9.7 to 8.5.
- **Pipelines are made ahead of the frames that need them**: the GPU renderer writes down each pipeline as it first
  needs it and, in the next runs, makes them again on a thread of its own in that order, where the device can (Vulkan).
  A Redmi Note 9 Pro's driver keeps none in its pipeline cache: each took 8–20 ms in the frame that first drew with it,
  45 of them while scrolling the Components screen; it now missed 107–110 refreshes in the bench instead of 137–141, its
  longest frame 46–54 ms instead of 95.
- **The showcase's Components screen is a LazyColumn**: as a long list is in Compose, only the sections in view (and
  a little past them) are composed, laid out and drawn. Going between the Home and Components tabs five times on a
  Redmi Note 9 Pro took 1.6 s of the main thread instead of 4.5, its composition and layout 0.6 s instead of 3.2;
  opening Components again took 19–35 ms of composition and layout instead of 150–220.
- **Line breaking lays each run out once**: wrapping measured the line so far again at every word, each time laying out
  a new piece of text (and filling the font system's cache with them, which it then emptied); it now lays each run out
  once and sums what its clusters add (`TextMeasurer::clusterAdvances`, as Android's line breaker does with a
  paragraph's advances). What is drawn and where lines go are measured as before. Scrolling the Components screen
  on a Redmi Note 9 Pro took 3.8–4.1 ms of composition and layout a frame instead of 4.6–4.8.
- **A layer is kept from its first frame**: a layer seen for the first time (a fade starting) is drawn whole anyway,
  and is kept from then on; one whose content stays the same is composited from those pixels in the next frame, where
  it used to be drawn whole a second time.
- **A pixel's steps are worked out once for each primitive**: how far a device pixel goes in a primitive's own space
  (what its anti-aliasing measures with) came from derivatives at every pixel, which cost an Adreno GPU about as much
  as the rest of a fill. Without perspective it is the same everywhere, and the vertex shader works it out from the
  primitive's placement; a primitive in perspective takes a variant of the shader with the derivatives. Drawing all of
  the Components screen took a Xiaomi 14 Ultra's GPU 4.1–4.2 ms instead of 4.9.
- **`kompoz-full-redraw`** on Android, as `KOMPOZ_FULL_REDRAW` elsewhere: every frame drawn from scratch, for what
  drawing all of the screen costs.
- **What lies clear of a rounded clip isn't clipped**: a primitive inside a rounded clip's edges and clear of its
  corners (a card's content) is drawn without the clip, which would have covered all of each of its pixels anyway:
  no per-pixel clip for it. Drawing all of the Components screen took a Xiaomi 14 Ultra's GPU 4.9 ms instead of 5.3–5.6.
- **The inside of a rounded rect costs the GPU next to nothing**: a pixel's coverage of a rounded rect (fills, layers,
  rounded clips) was worked out exactly at every pixel; one whose box lies inside every edge, past the corners' radii
  on one axis, is wholly covered, as the exact sum says, and is now taken as such. Drawing all of the showcase's
  Components screen took a Xiaomi 14 Ultra's GPU 5.3–5.6 ms instead of 6.1 (its rounded rects alone 4.2 instead of 6).
- **An idle app on Android leaves the CPU alone**: SDL's `SDL_WaitEvent` spun on Android instead of waiting (pumping
  events pushes SDL's poll sentinel, which wakes the wait at once), so whenever there was nothing to do, the app's main
  thread kept a whole core busy: about a million passes a second. `scripts/patches/` fixes it in SDL 3.4.18 (also not
  fixed in SDL's main branch). On a Redmi Note 9 Pro the bench's main thread took 13 s of CPU instead of 30.
- **One frame for each beat of the display**: a frame started less than half a refresh after the last (the last one
  shown without waiting, the swap chain having room, or one that changed nothing on the screen) got the last one's
  time, so a scroll stood still in it and jumped two refreshes in the next: shown, still, still, shown, over and over
  while scrolling on a 120 Hz phone. A frame now waits for the display's next beat instead (a frame not shown too, which
  replaces its own wait). Scrolling the showcase's Components screen on a Redmi Note 9 Pro missed 2–6 refreshes a
  second instead of 5–17.
- **What is drawn out of view asks for no frames**: the frames a node asks for as it draws (an animation drawn on, a
  ripple, an indeterminate indicator) are asked for only if any of what it drew shows (a `DisplayList` tells which of
  its draws its clips leave nothing of: `GraphicsContext::endedNodeShows`). Scrolled out of view, an endless one asks for
  none and is drawn again in the first frame there is, which goes on where its time has taken it once it is back. An
  endless animation drawn once no longer keeps frames coming after it has gone.
- **A marquee scrolls as it is drawn**: `basicMarquee` takes how far its pass has gone from the frame's time as it
  draws, instead of a coroutine writing it every frame; between passes and when a pass ends it waits on a timer. Out of
  view it asks for no frames, only its timers are left.
- **The showcase stops what nobody sees**: its spinning gear and its lifecycle section's player (an `Animatable` in a
  loop, a ticking coroutine) stop while out of view (`onVisibilityChanged`), as an app would; its position section,
  which follows the page on every frame it scrolls, composes only its two lines of numbers again, not the section. With the drawing out of
  view asking for nothing, the showcase's Components screen left alone on a desktop went from 60 frames a second to 2
  (its lifecycle section's clock).
- **Scrolling doesn't walk what scrolls**: what is moved without being measured again (a list's content) leaves the
  places in it as they were written down, worked out when asked for and written down when drawn. Scrolling the
  showcase's Components screen spent 1% of the main thread on places instead of 16%.
- **OpenGL ES draws the frame straight into the window**: its framebuffer, drawn into upside down, instead of a texture
  copied over to it, one copy of the whole window less each frame shown.
- **A composition local's readers are its provider's**: a provider giving another value recomposes the scopes that
  read it from it, found at once, not by going through every reader of the local and up from each; a scope leaving
  the composition leaves the readers at once. Switching the showcase's tabs took a fifth less of the main thread.
- **The system's values recompose only their readers**: the window's size and focus, the insets, the dark theme, the
  layout direction, the library's texts and the desktop's palettes are provided by the root as before, but a change
  in one of them recomposes the scopes that read it and measures again only what they changed, instead of composing
  the app from its root and measuring every node (as a change of density still does, and `ComposeHost::invalidate`).
  On the Redmi Note 9 Pro the keyboard sliding in and out took 1.8 ms of composition and layout a frame instead of
  2.1.
- **Effects in Compose's order, and `RememberObserver`**: what leaves the composition is forgotten after it, not while
  it goes on, and in the reverse of the order it was composed in (it was in no order among siblings); the clean-ups
  of effects whose key changed all run before any new effect starts; then what entered is remembered in order, and
  only then do the `SideEffect`s run (they ran among the effects, in call order). A remembered value whose type
  derives from `RememberObserver` is told `onRemembered` and `onForgotten` at those times.
- **Two or three keys**: `remember(a, b, init)`, `remember(a, b, c, init)`, and the same for `DisposableEffect`,
  `LaunchedEffect` and `produceState`, as in Compose: computed or restarted again when any of them changes.
- **Mutation policies**: `mutableStateOf(v, neverEqualPolicy<T>())` recomposes the readers on every `set`, the same value
  too; `SnapshotMutationPolicy<T>{equivalent}` decides by a function of the app's; `structuralEqualityPolicy<T>()`
  (`==`) stays the default. `derivedStateOf(policy, calc)` takes one too.
- **MSVC builds with `/bigobj`**, given to apps too: the headers' templates made `widgets.cpp` more sections than an
  object file holds without it.
- **`State<T>`**, Compose's read-only state: a composable that only reads a value takes one, given a `MutableState` or
  a derived state (which converts to it); `get()`, `peek()`, compared by which state it is. Derived states have
  `peek()` too.
- **`rememberSaveable` with inputs**, as Compose's `rememberSaveable(inputs)`: `rememberSaveable(inputs, init)` and
  `rememberSaveable(inputs, saver, init)` make the value again, dropping the one it had and its saved one, when the
  inputs change.
- **Structured concurrency**: `launch` gives a `Job` (`cancel`, `isActive`, `co_await join()`), `async` a `Deferred<T>`
  (`co_await await()`); `co_await coroutineScope(block)` goes on once the block and what it launched are done, a child
  failing cancels the others and is thrown, and the awaiting coroutine cancelled cancels them all; `supervisorScope`
  lets children fail on their own; `withTimeout` / `withTimeoutOrNull` cancel what runs too long. A coroutine ending
  with a `CancellationException` (a timeout's too) is cancelled, not failed: it isn't reported.
- **Snapshots**, as Compose's snapshot system: a state keeps a record per snapshot that wrote it and is read as the
  snapshot the code runs in sees it. A composition runs in a snapshot of its own, applied when it is over, so it sees
  one consistent state whatever other threads write; `Snapshot::withMutableSnapshot` writes several states at once
  (`SnapshotApplyConflictException` if one was written meanwhile to a value its policy doesn't count as the same);
  `takeSnapshot`, `takeMutableSnapshot` (nested ones apply to their parent), read and write observers,
  `registerApplyObserver`. Another thread may now write states (a race before) and reads them in a snapshot. Records
  no snapshot reads are dropped between frames.
- **Named arguments and blocks**, as Kotlin's: `Text("Merhaba", fontSize = 22, color = cs.primary)`,
  `Column(Modifier().padding(16), verticalArrangement = Arrangement::spacedBy(8), [=] { … })`. Arguments without a name
  fill the parameters in Compose's order, then named ones come in any order, then the block; anything else (a name the
  composable doesn't take, one given twice, a value of the wrong type) is a compile error. A block runs in a scope of
  its own: when only what it read changes, it alone runs again, so it captures by value. `<kompoz/blocks.hpp>` adds the
  block form Compose writes, `Column(modifier) { … };`, which the showcase and the guide use. `Box`, `Column`, `Row`,
  `Spacer` and `Text` take them now (`ColumnStyle` and `RowStyle` are gone; Text's `style` takes a theme's text style,
  `typography().bodyMedium`, as it is); the other composables follow.
- **Named arguments and blocks for buttons, cards and controls**: the buttons, icon buttons and icon toggle buttons,
  the cards, `Surface`, `Icon`, `Image`, `Checkbox`, `TriStateCheckbox`, `RadioButton`, `Switch` and the sliders take
  them, `Button(onClick, enabled = false) { Text("Gönder"); };`, `Slider(v, set, valueFrom = 0, valueTo = 100,
  steps = 9)`, `Icon(icon, "Sil", tint = cs.error)`, in Compose's order. Their option structs are no longer given
  (`SwitchOptions`, `SliderOptions`...); a slider's colors are `sliderColors =` (`colors` is the color constants'
  namespace), and a callback left out is `nullptr`, as `{}` has no type to take.
- **Named arguments for the text fields**: `TextField`, `OutlinedTextField`, `BasicTextField`, `SecureTextField`,
  `OutlinedSecureTextField` and `BasicSecureTextField`, with a value or a `TextFieldState`, take them in Compose's
  order, `OutlinedTextField(email, setEmail, label = "E-posta", singleLine = true, keyboardOptions =
  KeyboardOptions{.keyboardType = KeyboardType::Email})`; the basic fields only the names that apply to them. A
  field's colors are `textFieldColors =`. A composable takes up to sixteen arguments (twelve before).
  Header comments' examples are written the new way, those of the composables converted earlier too.
- **Named arguments for Scaffold, the app bars, FABs, floating toolbars and snackbars**: `Scaffold`, the top app
  bars (`TopAppBar`, `CenterAlignedTopAppBar`, the medium, large, flexible and two row ones), `BottomAppBar`,
  `FlexibleBottomAppBar`, the floating action buttons, `HorizontalFloatingToolbar`/`VerticalFloatingToolbar`,
  `SnackbarHost` and `Snackbar` take them in Compose's order: `TopAppBar(title = [] { Text("Inbox"); },
  scrollBehavior = s)`, `FloatingActionButton(onClick) { Icon(…); };`. A block given a value is a lambda after the
  call: `Scaffold(topBar = …) [=](PaddingValues padding) { … };`, and it too runs in a scope of its own. An extended
  FAB's `expanded` comes after its modifier, as in Compose; a floating toolbar's colors are `floatingToolbarColors =`.
- **Named arguments for navigation, dialogs, menus and sheets**: `NavigationBar`, `ShortNavigationBar`,
  `NavigationRail`, `WideNavigationRail`, `ModalWideNavigationRail` and their items, the navigation drawers and drawer
  sheets, `NavigationDrawerItem`, `Dialog`, `AlertDialog`, `BasicAlertDialog`, `DropdownMenu`, `DropdownMenuPopup`,
  `DropdownMenuGroup`, `DropdownMenuItem`, `ExposedDropdownMenuBox`, `ExposedDropdownMenu`, `ModalBottomSheet` and
  `BottomSheetScaffold` take them in Compose's order: `AlertDialog(close, confirmButton = …, title = …)`,
  `DropdownMenu(open, close) { DropdownMenuItem(text = …, onClick = …); };`,
  `BottomSheetScaffold(sheetContent, sheetPeekHeight = 128) [=](PaddingValues padding) { … };`. Their content is a
  block in a scope of its own. A rail item's label comes after its modifier, as in Compose; a dialog's properties are
  `properties = DialogProperties{…}`.
- **Lazy lists, grids and pagers take named arguments, and their items are added as in Compose**: `LazyColumn`,
  `LazyRow`, `LazyVerticalGrid`, `LazyHorizontalGrid`, the staggered grids and the pagers take their options in
  Compose's order and names (`verticalArrangement = Arrangement::spacedBy(8)` for the spacing,
  `horizontalAlignment = Alignment::CenterHorizontally` across), and a lazy layout's block adds its items with `item`,
  `items`, `itemsIndexed` and `stickyHeader` called in it, as Compose's receiver: `LazyColumn(m) { items(mails,
  [=](const Mail& m) { MailRow(m); }); };`. That block runs in the call and captures by reference, so a list isn't
  copied; a pager's page is a block given its number, `HorizontalPager(state) [=](int page) { … };`. A pager's page
  key is `pageKey =` (`key` is the composable that gives an identity).
- **Fixed: a crash when a container left the composition before the movable content it held was called again**
  further on (going from one screen to another): the empty place the content left two groups down was taken for a
  group to forget, and
  the content's nodes still had the container's as their parent when it came back.
- **The display's fastest refresh rate while the screen keeps changing, on Android**: while a finger is on it or a
  scroll, a fling or an animation goes on, and for a second after, the app's surface asks for the display's fastest
  rate (the high frame rate category, as Android's views vote): Android 15 kept an app that doesn't ask at 60 Hz.
  On a Xiaomi 14 Ultra (120 Hz) the same 30 swipes showed 2,088 frames instead of 1,565 (83 a second instead of 55).
- **kompozc: @Composable functions in Compose's own syntax, in C++ sources** (first stage). A target given to
  `kompoz_compose(target)` has its sources rewritten by kompozc before the C++ compiler sees them, as Compose's
  compiler plugin rewrites Kotlin: in a `.cpp` file, `@Composable fun Counter(start: Int = 0) { var count by
  remember { mutableStateOf(start) } … }` is written as in Kotlin, the rest of the file is C++. A Unit function
  becomes a restartable, skippable composable; calls to the file's own functions take named arguments in any order
  and leave defaults out; trailing lambdas, `it`, `val`/`var`/`by`, string templates (numbers written as Kotlin
  writes them), `16.dp`, `if`/`when` as values, `for` over ranges (`until`, `downTo`, `step`, `..`) and lists,
  `while`, `?:`, `?.`, `!!` and `null`; Kotlin's standard functions (`listOf`, `filter`, `map`, `forEach`,
  `joinToString`, `trimIndent`, `isNotBlank`, `withIndex`, ...; a type's own member of that name wins); a
  `LaunchedEffect` or `launch` block is a coroutine whose `delay` and other suspending calls are awaited;
  `Modifier.Companion`, `Unit` as a key, and `RowScope.() -> Unit` content. A `data class` in the same file
  becomes a struct with `==` and Kotlin's `toString()`; it is made with named arguments in any order
  (`Note(title = "x", id = 1)`) and changed with `copy(pinned = true)`. A C++ struct (Kompoz's, the file's own or
  one of a header it includes with quotes) is made with named arguments in any order too:
  `DialogProperties(dismissOnClickOutside = false)` becomes designated initializers in its fields' order, as kompozc
  reads them from the headers (a change in one translates the file again). Lambdas capture by value, as Kotlin's do, so a block that runs again on its own
  never holds a dangling reference. The C++ compiler's messages point at the source's own lines (`#line`). A file with Windows' line ends (CRLF) translates as one with LF does. The target's sources are one module, as Kotlin's: a `@Composable` function or a data class of
  one file is called from another with named arguments and defaults, no header needed (kompozc declares there what it
  uses). kompozc runs on the machine that builds: where Kompoz is built for another (Android, the web) it is built
  for this one from its sources, with this machine's C++ compiler (or `KOMPOZC` names one); `cmake --install` installs
  it with `kompoz_compose` and its sources, for `find_package(kompoz)`. vcpkg keeps it among its tools (`tools/kompoz`). The showcase's sources go through it. The guide's ```` ```kotlin ```` examples are
  checked through it too (`scripts/docs.py --check`), each at its own lines of the page. The guide's "Coming from Compose", its first page, "Setup" (`kompoz_compose`, where kompozc is built), the tutorial, "Thinking in Kompoz", "State", "Lifecycle and identity", "Side effects", "Phases", "CompositionLocal", "Modifiers", "Layouts", "Custom layouts", "Lists and grids", "Material components", "Theming", "Text", "Graphics", "Animation", "Gestures", "Architecture", "Navigation", "Resources", "Accessibility", "Testing" and "Tools" are written for kompozc: the whole guide; the C++ forms moved to a page of their own, "Writing in C++".
- **Every argument a composable needs can be named**: `onClick`, `value` and `onValueChange`, `checked` and
  `onCheckedChange`, `selected`, `expanded`, `onDismissRequest`, `state`, `text`, `imageVector`/`painter`/`bitmap` and
  `contentDescription`, … are named arguments too, as in Compose: `TextButton(onClick = save, [=] { … })`,
  `TextField(value = text, onValueChange = setText)`; given without a name they still fill the first places. A
  composable with two forms takes the one whose name is given (`Icon(painter = …)`, `TextField(state = …)`,
  `Card(onClick = …)`) or the one the first argument's type says. The text fields and sliders take Compose's
  `colors =` as well (written `kompoz::arg::colors` in C++, where `kompoz::colors` is the colors' namespace).
- **Compose's colors objects and named members**: `CardDefaults.cardColors(…)`, `TextFieldDefaults.colors(…)`,
  `ButtonDefaults.textButtonColors(…)` (and the other buttons'), `TopAppBarDefaults.topAppBarColors(…)` are the
  `CardColors`, `TextFieldColors`, `ButtonColors` and `TopAppBarColors` structs (`TextFieldDefaults::colors{…}` in
  C++), which the cards, text fields, buttons and top app bars take as `colors =`; those left unset are the theme's.
  The cards take `shape =` and, clickable, `enabled =` (Compose 1.4's disabled colors). `BottomAppBar(…) { … }` with a
  block is Compose's content form (a row of its own, `contentColor` its container's own). `Color.Red`,
  `Color.Transparent`, … are `Color`'s constants as well. Modifier's `padding`, `background`, `border`, `size`,
  `offset` and `graphicsLayer` and `Color::copy` take Compose's named arguments (`border(width = 1, color = c, shape = s)`,
  `graphicsLayer(alpha = 0.5f, rotationZ = 30f)`, `copy(alpha = 0.6f)`), written `kompoz::arg::width = …` in C++. With kompozc, the user's own `AddTextDialog` and
  Compose's Scaffold sample compile as they are written for Android (tests/compose_tests.cpp).
- **What Compose code says most, through kompozc**: `MaterialTheme(colorScheme = …, motionScheme = …) { … }` (a
  composable taking named arguments now; its content still runs in its caller's scope), `Surface(shape = …)` (its
  content clipped to it), `CompositionLocalProvider(LocalX provides x) { … }` with one to three values,
  `DisposableEffect(key) { …; onDispose { … } }`, `contentDescription = null`. A composable is never skipped when an
  argument only looks comparable: a list of a struct without `==` (std::vector's `==` doesn't compile then) counts
  as not comparable, for skipping as for a state's structural equality. kompozc declares each function just before
  its definition (and those it calls before it), not at the top of the file, so a parameter's type may be a struct
  the file declares above it.
- **ListItem takes Compose's named arguments**: `ListItem(headlineContent = { … }, supportingContent = { … },
  leadingContent = …, trailingContent = …, overlineContent = …)`, and the interactive ones with their headline as the
  block: `ListItem(onClick = …) { Text("…") }`, `ListItem(selected = …, onClick = …) { … }`,
  `ListItem(checked = …, onCheckedChange = …) { … }` (`enabled`, `onLongClick`, `shapes`, `colors`, `interactionSource`
  by name). The `ListItemSlots` forms (`ListItem({.headline = …})`) take their arguments as before.
- **A lazy layout's items take Compose's named `key` and `contentType`**: `items(list, key = { it.id }) { … }`,
  `item(key = "header") { … }`, put in their places (written `kompoz::arg::key` in C++, where `kompoz::key` is the
  composable that gives an identity). kompozc gives such a lambda its value, and reads `Int.MAX_VALUE` and Kotlin's
  other number limits.
- **Compose's state forms through kompozc**: a property is read as Compose reads it, through a getter where Kompoz
  has one (`listState.firstVisibleItemIndex`, `pagerState.currentPage`), else the field. A lambda's parameters are
  references, so `todos.update { it.add(x) }` changes the list itself. Kotlin's `add`, `addAll`, `remove` and
  `removeAt` are there for lists. `mutableStateOf(v, neverEqualPolicy())` and `structuralEqualityPolicy()` take no
  type, `mutableStateListOf("a", "b")` takes its items one by one, and `rememberSaveable(saver = …) { … }` takes its
  saver by name.
- **kompozc awaits whatever suspends in a coroutine**: in a `LaunchedEffect`, `launch` or `produceState` block, every
  call that gives a `Task`, a `TaskOf<T>` or another awaiter is awaited, as Kotlin awaits a `suspend fun`. A
  suspending function of your own (a C++ coroutine returning `TaskOf<T>`) needs no list. Any other call gives its
  value at once. `async { … }` and `withBackground { … }` give their block's value. `produceState(initial, key) { … }`'s
  block sets `value`, and `val x by produceState(…)` reads it. A snackbar is `showSnackbar`, as Compose suspends on it.
- **An infinite transition's values are states, as Compose's**: `InfiniteTransition::animateFloat` and
  `animateColor` give an `InfiniteState`, read where it is read (while drawing, it is only drawn again each frame) or
  taken as a `float` or `Color` (read then, in the composition). They take Compose's named arguments too:
  `animateFloat(initialValue = 0.8f, targetValue = 1f, animationSpec = …, label = "…")` (`kompoz::arg::initialValue`
  in C++), so `val pulse by rememberInfiniteTransition().animateFloat(…)` goes through kompozc as written for
  Android. `animateFloatState` and `animateColorState`, which gave the states, are gone: `animateFloat` and
  `animateColor` are them now.
- **Canvas takes Compose's named arguments**: `Canvas(modifier = …) { … }`, and Compose's other form,
  `Canvas(modifier, contentDescription = "…") { … }`, whose description is what a screen reader says of it.
- **kompozc: lambdas with receivers**: in the lambda of `Modifier.graphicsLayer { }`, `drawBehind { }`,
  `drawWithContent { }`, `drawWithCache { }` (and its `onDrawBehind { }`), a `Canvas`, and DrawScope's `rotate`,
  `scale`, `translate`, `inset`, `clipRect`, `clipPath` and `withTransform`, the receiver's members are reached by
  their names, as in Kotlin: `graphicsLayer { scaleX = pulse; alpha = 0.5f }`, `Canvas(m) { drawCircle(c, size.width
  / 4) }`. A local of the same name comes first, `this` is the receiver. kompozc reads the members (fields and
  functions, those of base classes too) from the headers. `Modifier.offset { IntOffset(x.roundToInt(), 0) }` gives
  its lambda's value.
- **A composable's block given by name**: `content = …`, as Compose code passes content on
  (`MaterialTheme(colorScheme = c, content = content)`, `Box(modifier = m, content = { … })`), is the block every
  composable takes (`kompoz::arg::content` in C++), wherever it stands among the named arguments;
  `CompositionLocalProvider(LocalX provides x, content = content)` too.
- **kompozc: top-level vals**: `val LocalSpacing = staticCompositionLocalOf { Spacing() }`, `val Purple80 =
  Color(0xFFD0BCFF)`, `private val …`, as Compose's Color.kt and locals are written: a constant of the namespace
  (`private`: of the file), defined before the first function that uses it, wherever it stands, and after the vals
  its value uses; one of another file of the module is used as Kotlin uses it, no header needed. `error("…")` and
  `TODO()` give Kotlin's `Nothing`, which goes for a value of any type (`compositionLocalOf<User> { error("…") }`,
  `x ?: error("…")`). `MaterialTheme.motionScheme` and `currentCompositionLocalContext` are read as in Compose.
- **Compose's way of measuring and placing**: `Modifier.layout { measurable, constraints -> … }` with
  `layout(width, height) { placeable.placeRelative(x, y) }` (a `MeasureScope` its receiver, a `PlacementScope` the
  block's), and `layout(width, height) { … }` in a `Layout`'s measure policy. `placeRelative` places from the start,
  mirrored right to left; `place` from the left.
- **Semantics set on a receiver, as Compose's**: `Modifier.semantics { heading(); contentDescription = "…" }`,
  `semantics(mergeDescendants = true) { … }`, `clearAndSetSemantics { contentDescription = "…" }`, with
  `stateDescription`, `role` (`Role.Button`, …), `selected`, `liveRegion`, `progressBarRangeInfo`, `customActions`,
  `traversalIndex`, `selectableGroup()`, `hideFromAccessibility()` and `setProgress { … }`.
- **kompozc: plain and extension functions**: `fun shout(text: String) = text.uppercase()`, `private fun …`, and
  `fun Modifier.cardSurface(color: Color) = shadow(…).background(color, …)`, as Compose's own modifiers are written:
  `Modifier.fillMaxWidth().cardSurface(c)` is `cardSurface(Modifier.fillMaxWidth(), c)`, and in its body the
  receiver's members are reached by their names. A function's type is its body's when none is written; each is
  defined before the first function that uses it, and one of another file of the module where it is used. An
  expression body or a val may go on after `=` or an operator at the end of a line. The lambdas of
  `semantics { }`, `layout { }` and `onKeyEvent { }` are what Compose makes them. A call to one of the file's own
  functions or vals names its C++ namespace (`::label(1)`), so one called `label` or `shape` is never a name of
  Kompoz's that `using namespace kompoz` brings in.
- **Layouts as Compose writes them**: `FlexBox(modifier, config = { wrap(FlexWrap.Wrap); gap(8.dp) }) { … }` with
  `Modifier.flex { grow(1f); basis(80.dp) }`, and `Grid(config = { column(96.dp); column(1.fr); gap(row = 8.dp,
  column = 16.dp); area("header", row = 1, columnSpan = 2) }, modifier) { … }` with
  `Modifier.gridItem(row = 3, column = 1)`: their configs run on Compose's scopes while they are laid out, with the
  `constraints`. `GridTrackSize.Auto`, `MinContent` and `MaxContent` are constants, and `Fr` a track's share.
  `FlowRow(horizontalArrangement = Arrangement.spacedBy(8.dp), verticalArrangement = …, maxItemsInEachLine = …)` and
  `BoxWithConstraints(modifier) { if (maxWidth > 400.dp) … }` (a `BoxWithConstraintsScope`). All of them take named
  arguments; in C++ a config given in braces names its type (`FlexBoxConfig{…}`, `GridConfig{…}`, `FlowStyle{…}`).
  `UiMediaScope.PointerPrecision` and its other nested names are Compose's.
- **kompozc: layouts' and media queries' lambdas**: `FlexBox(config = { … })` and `Grid(config = { … })` (a named
  argument's lambda with a receiver), `flex { }`, `BoxWithConstraints { }`, and `mediaQuery { }` and
  `derivedMediaQuery { }` on a `UiMediaScope` only read; `1.fr`; `16.dp.toPx()`, `roundToPx()` and `toDp()`, a dp
  being its own pixel here; `WindowInsets.systemBars` and the other insets, which are properties in Compose.
- **Custom layouts as Compose writes them**: `Layout(content, modifier) { measurables, constraints -> layout(w, h)
  { … } }` (its measure policy on a `MeasureScope`), `SubcomposeLayout(modifier) { constraints -> … }` (its
  `SubcomposeMeasureScope` gives `layout(w, h) { … }` too), `LookaheadScope(modifier) { … }`, and
  `HorizontalDivider` / `VerticalDivider` with `thickness` and `color`, all taking named arguments.
  `constraints.copy(minWidth = 0, minHeight = 0)`, `constrainWidth`, `constrainHeight` and `offset`, and
  `placeable.placeRelative(x = …, y = …)` by name, as Compose's.
- **kompozc: Kotlin's inline functions and measure policies**: the lambda of `forEach`, `map`, `let`, `repeat` and
  Kotlin's other inline functions reaches the locals around it by reference, so `y += h` in `forEach { }` changes
  the `y` outside, as in Kotlin. `Layout`'s measure policy has a `MeasureScope` receiver and gives its value,
  `SubcomposeLayout`'s a `SubcomposeMeasureScope`. `list.maxOf { }`, `minOf { }`, `maxOfOrNull { }` and
  `minOfOrNull { }`.
- **Lists, pagers and carousels take Compose's arguments**: a lazy grid's `item(span = { GridItemSpan(maxLineSpan) })`
  and `items(list, span = { item -> GridItemSpan(2) })`; `rememberPagerState(initialPage = 0) { 3 }` and
  `rememberCarouselState { images.size }` (the count by name or as the trailing lambda); the carousels'
  `(state, preferredItemWidth, modifier, itemSpacing = …, contentPadding = …) { index -> … }`, the content on its
  `CarouselItemScope` and index as Compose's; Coil's `AsyncImage(model, contentDescription, modifier,
  contentScale = …, alpha = …)`; `VerticalScrollbar(adapter, modifier, reverseLayout = …)` and the horizontal one;
  `PullToRefreshBox(isRefreshing, onRefresh, modifier, state = …, contentAlignment = …, enabled = …) { … }`. In C++ a
  braced options struct names its type (`CarouselOptions{…}`, `AsyncImageOptions{…}`).
- **A lazy layout keeps a list made for its call**: `items(listOf("a", "b")) { … }`, as Kotlin writes it, gives the
  items a temporary list; they read it later, when they come into view, so it is kept with them (one given by name
  is still read where it is). The count's forms (`items(50) { index -> … }`) are tried only for a count, so a list's
  content written as a generic lambda (kompozc's) is never built for an int: Clang failed on it.
- **kompozc: object expressions**: `object : NestedScrollConnection { override fun onPreScroll(…): Offset { … } }`
  is a class of its own deriving from the type, made where it stands, with copies of the locals its functions use
  (a delegated state's reads and writes go through as everywhere); it is a `shared_ptr`, as Kompoz takes such
  objects. A lazy grid's `span = { … }` has its `LazyGridItemSpanScope` receiver; a carousel's content its
  `CarouselItemScope`; `pageCount = { … }` and `itemCount = { … }` give their values.
- **A SubcomposeLayout composes a slot as it measures**: `subcompose(slotId) { … }` composes a slot the first time it
  is asked for (or when its dependency changes) there and then, as Compose's does, and gives its measurables to the
  same measure: `subcompose("main", content).map { it.measure(c) }.maxOf { it.width }` works on the first pass.
  Before, a slot was composed after the pass and the layout ran again in the same frame, with no measurables until
  then. The slots are a scope of their own, run alone (`Composer::composeNow`); one no longer asked for leaves after
  the measure.
- **BoxWithConstraints is a SubcomposeLayout, as Compose's**: its content is composed while the box is measured,
  with the constraints it gets, so its very first composition sees them (it saw unbounded ones before, and was
  composed again in the same frame once they were known); it is composed again when they change or when the box is
  composed again.
- **Material's components take Compose's arguments**: the chips, `FilterChip(selected, onClick, label = { Text("…") },
  leadingIcon = …, trailingIcon = …, enabled = …)` and the others (an `InputChip`'s `avatar` too);
  `Modifier.toggleable(value = …, onValueChange = …, role = Role.Checkbox)` and `selectable(selected = …, onClick = …,
  role = …)`; `SnackbarHost(hostState = …, snackbar = { data -> … })`; `showSnackbar(message, actionLabel = …,
  withDismissAction = …, duration = …)`, awaited in a coroutine as Compose's suspends; `CircularProgressIndicator` and
  `LinearProgressIndicator` with `progress = { … }` (a lambda or a value) and `strokeWidth`; `NavigationSuiteScaffold(
  navigationSuiteItems = { item(selected = …, onClick = …, icon = { Icon(…) }, label = { Text("…") }) }, modifier,
  layoutType = …, …) { … }` whose items are composed from their own icon and label, and `NavigationBarItem` with an
  icon and a label of content. In C++ a braced options struct names its type (`ChipOptions{…}`,
  `NavigationSuiteScaffoldOptions{…}`).
- **Theming as Compose writes it**: `lightColorScheme(primary = …, onPrimary = …)`, `darkColorScheme(…)` and
  `scheme.copy(…)` with any of the scheme's colors by name; `typography.copy(headlineSmall = …)` and
  `Typography(displayLarge = …)`; `style.copy(fontFamily = …, fontWeight = …, fontSize = …, fontStyle = …,
  letterSpacing = …, lineHeight = …)` on a `Font`, as Compose's `TextStyle.copy`; `MaterialTheme(colorScheme, typography,
  shapes, motionScheme) { … }`, its typography and shapes given to everything inside and its color scheme, left out,
  the one around it. `Shapes` is Material's shape scale (`MaterialTheme.shapes.medium`, `Shapes(medium = …)`,
  `copy(…)`), for the app's own components; Kompoz's have Material's shapes built in.
- **kompozc: enum classes, and a group for every composable**: `enum class ThemeChoice { System, Light, Dark }` (entries
  only; one with values is C++'s) is C++'s enum class, read as `ThemeChoice.Dark` in a `when`. A `@Composable fun` that
  returns a value takes a call site and is a group of its own, as Compose makes every composable call: what it
  remembers is its own, however often its caller calls it from one place. An `if` as a value may put its `else` on
  the next line.
- **Text as Compose writes it**: a text field's `label = { Text("…") }`, `placeholder = { … }` and `supportingText = { … }`
  are composables (a string still does), composed in their places: the label in its animated style as it floats, the
  placeholder in its color where the text starts, the supporting text under the field;
  `TextFieldLineLimits.MultiLine(maxHeightInLines = 4)`; `LinkAnnotation.Clickable(tag) { … }`;
  `SelectionContainer(modifier) { … }` by name; `measurer.measure(text, style = …, maxWidth = …)` and
  `drawText(layout, topLeft = …, color = …, alpha = …)` by name. kompozc gives `buildAnnotatedString { append(…);
  withStyle(SpanStyle(…)) { … } }` its builder as the receiver, and `edit { … }` its `TextFieldBuffer`.
- **Drawing as Compose writes it**: `DrawScope`'s calls take Compose's names: `drawCircle(color, radius = …, center = …,
  style = Stroke(width = 2f))`, `drawRect(topLeft = …, size = …, alpha = …)`, `drawRoundRect(cornerRadius = …)`,
  `drawOval`, `drawLine(start = …, end = …, strokeWidth = …, cap = …, pathEffect = …)`, `drawArc(color, startAngle,
  sweepAngle, useCenter, topLeft = …)`, `drawPath(path, color, style = …)`, `drawPoints(…)`, each in a brush too;
  `Stroke(width = …, miter = …, cap = …, join = …, pathEffect = …)`; the blocks `rotate(degrees, pivot = …) { }`,
  `rotateRad`, `scale(…, pivot = …) { }`, `translate(left = …, top = …) { }`, `inset(left = …, top = …, right = …,
  bottom = …) { }` or `inset(horizontal = …, vertical = …) { }`, `clipRect(left = …, top = …, right = …, bottom = …,
  clipOp = …) { }` (its edges, as Compose's) and `clipPath(path, clipOp = …) { }`. `AsyncImage(model, placeholder = { },
  error = { }, crossfadeMillis = …)` as Coil's. `floatArrayOf(8f, 6f)` and `intArrayOf(…)` from kotlin.hpp. An `Image`'s, an
  `AsyncImage`'s and an `Icon`'s `contentDescription` is its semantics' contentDescription, as Compose's (`onNodeWithContentDescription` finds it, `onAllNodesWithContentDescription` all of them;
  `onNodeWithText` no longer does).
- **kompozc: a type's values, a struct's fields by name, and more receivers**: `Color.White.copy(alpha = 0.5f)` calls
  on the value (a type's static member is a value, not a scope, so an extension on it is called as one too);
  `addPath(pathData = …, fill = …)`, `group(rotation = …, pivotX = …) { path(stroke = …) { moveTo(…) } }` and a vector
  painter's `Path(pathData = …)` fill the `VectorPath` or `VectorGroup` they take; `GenericShape { size, _ -> moveTo(…) }`
  builds on its path, `blur { blurRadiusSpec = … }` sets its scope, `Brush.shader { size -> … }` returns its shader; a
  lambda's `_` parameters are unnamed; the headers' reader knows a class defined as `class Outer::Inner { }`, a type's
  static data members, and functions whose default arguments are braces.
- **Animation as Compose writes it**: `animateFloatAsState(targetValue, animationSpec = …, label = …)`, `animateColorAsState`
  and `animateDpAsState` (a dp is a float); `AnimatedVisibility(visible, enter = …, exit = …, label = …) { }`;
  `AnimatedContent(targetState, transitionSpec = { if (targetState > initialState) slideInVertically { it } togetherWith
  slideOutVertically { -it } else … }, contentAlignment = …, contentKey = { }, label = …) { state -> }`, the spec's lambda
  on its `AnimatedContentTransitionScope`, `enter togetherWith exit` as a member and `using SizeTransform(clip = false)`;
  `Crossfade(targetState, animationSpec = …, label = …) { state -> }`; the slides take Compose's lambda of the full size
  (`slideInVertically { it / 2 }`); `updateTransition(targetState, label = …)` and its `animateFloat`, `animateDp` and
  `animateColor` with `label = …` and `transitionSpec = { … }` (on a `TransitionSegment`); `rememberInfiniteTransition(label
  = …)`; `SharedTransitionLayout(modifier = …) { }`, `rememberSharedContentState(key = …)`, `sharedElement` and
  `sharedBounds` by name (`sharedContentState`, `animatedVisibilityScope`, `boundsTransform`, `renderInOverlayDuringTransition`,
  `zIndexInOverlay`, `resizeMode`), `animateEnterExit(enter = …, exit = …)`. `AnimatedContent` and `Crossfade` deduce
  their state's type: `AnimatedContent(count, …)`, not `AnimatedContent<int>(…)`; options are an `AnimatedContentOptions<T>`.
- **kompozc: this@Label, a template's scope, `using`**: `this@AnimatedContent` is the receiver of the lambda given to that
  call; a receiver that is a template's (`AnimatedContentTransitionScope<S>`, `TransitionSegment<S>`) is typed `auto`;
  `a using b` is `a.withSizeTransform(b)`; the slides' and the transitions' value lambdas return their value. `Color(0xFF6750A4)`
  is Compose's `Color(Long)`, AARRGGBB (`Color::argb`): it was the packed 64-bit constructor, so the pages' colors were wrong.
  A `by` delegate reads a state's value, else the value itself (`val color by transition.animateColor { }`: a `Color`, not
  its packed `value()`).
- **Gestures as Compose writes them**: `pointerInput(Unit) { detectTapGestures(onTap = { }, onDoubleTap = { }, onLongPress = { }) }`,
  `detectDragGestures(onDragStart = { }, onDrag = { change, amount -> })` and the other detectors with their handlers by
  name, in the block with its `PointerInputScope`; `draggable(orientation = …, state = rememberDraggableState { delta -> })`;
  Compose's `DraggableAnchors { DragValue.Start at 0f; DragValue.End at 120f }` builder for `updateAnchors`
  (`draggableAnchors([](auto& a) { a.at(…, 0); })` in C++, taking the state's value type); `dragAndDropSource { … }`
  giving its data, `dragAndDropTarget(shouldStartDragAndDrop = { }, target = DragAndDropTarget(onDrop = { }))`.
- **kompozc: a scope's functions and infix functions**: in `pointerInput { }` (a coroutine on its `PointerInputScope`) the
  detectors take the scope first and their handlers as a struct; `a at b` inside `DraggableAnchors { }` is the builder's
  `at(a, b)`, as any infix function the receiver has; a struct's field given a lambda returns the lambda's last expression.
- **kompozc: classes**: `class Name(val a: T, b: U = d) : Base(b) { val c = …; var n = 0; init { … }; fun f(x: Int): R { … };
  override fun g() { … }; private fun h() = … }` is a C++ class: its constructor's `val`/`var` parameters and its properties
  members (typed as written, else as their value is), initialized in order in the constructor, `init` blocks its body;
  its functions inline and const, its members mutable (a Kotlin object is reached through references that never make
  it const), an `override` as its base's; `Name(b = …)` by name. A class without a base is a handle to its object
  (`Name::Object`, `kompoz_object()` from C++), shared by its copies as Kotlin's references share it: a copy, a lambda's
  capture or a `const Name&` parameter is the same object, `==` says so. One with a base (`: ViewModel()`) is the
  class itself, handed out by its owner (`viewModel<T>()`) as a pointer, reached as the object (`vm.items`, `vm.toggle(…)`). `flow { emit(…) }` in a function typed
  `Flow<T>`, `state["key"] = v` as `set`, `LifecycleResumeEffect(Unit) { …; onPauseOrDispose { … } }` and
  `LifecycleStartEffect`'s `onStopOrDispose`, `viewModelScope` by name, `update { }`, `flatMapLatest { }` and the other
  value lambdas; a function whose name a local shares (`items(items) { }`) is called, as Kotlin does. `debounce(300)` takes
  milliseconds, as Kotlin's.
- **Navigation as Compose writes it**: `NavHost(navController, startDestination, modifier = …, enterTransition = { },
  exitTransition = { }, popEnterTransition = { }, popExitTransition = { }) { composable("home") { } }`, the transitions'
  lambdas on their `AnimatedContentTransitionScope` of the two entries (`NavHostOptions::transitionFor`,
  `popTransitionFor` in C++); the builder's `composable(route, deepLinks = …) { entry -> }` and `navigation(startDestination
  = …, route = …) { }` by name; `navigate(route) { popUpTo("home") { saveState = true }; launchSingleTop = true }`
  (`NavOptionsBuilder`, `PopUpToBuilder`); `navDeepLink { uriPattern = "…" }`; `entry.destination.route`
  (`NavDestination`); `BackHandler(enabled = …) { }`; `combinedClickable(onClick = …, onLongClick = …, onDoubleClick = …)`.
- **kompozc: routes and objects**: `@Serializable data class Profile(val id: String, val tab: Int? = null)` is a route
  (`KOMPOZ_ROUTE`), `@Serializable object Home` a route without arguments, the name its type and its one value
  (`navigate(Home)`, `composable<Home> { }`); `e.toRoute<Profile>()` and the other calls with type arguments on an
  object; `entry?.destination?.route` reads through getters.
- **kompozc: previews, tests and the platform's locals**: a `@Preview(widthDp = …, heightDp = …, uiMode =
  Configuration.UI_MODE_NIGHT_YES, showBackground = true) @Composable fun` registers a preview under its name (what
  `KOMPOZ_PREVIEW` does in C++), shown with `KOMPOZ_PREVIEW=Name`; an annotated function (`@Test fun`) is read from its
  annotation; `ComposeTestRule { … }` and `AssertionError` are `kompoz::testing`'s; `LocalUriHandler.current`,
  `LocalClipboardManager.current`, `LocalHapticFeedback.current` and `LocalSoftwareKeyboardController.current` are what
  the host gives (`uriHandler()` and the others). The test rule's `mainClock.advanceTimeBy(ms)`, Compose's name.
- **kompozc: what a target's C++ files call of its Kotlin, `String.format`, more of Kotlin's collections**: a function,
  class or `val` written in Compose's syntax in one file is declared in another file's plain C++ too, before the
  statement that first uses it and after the file's own types, where a prototype would stand (a showcase screen still
  in C++ calls a `Section(…)` written in Kotlin next to it, no header needed). `"%.1f km · %d dk".format(x, n)` with Java's specifiers as `std::format`'s (`%d %s %f %x %c
  %%`, a width, a precision, the `0` and `-` flags); `indexOfFirst`, `indexOfLast`, `removeIf`, `removeAll`, `flatMap`,
  `sortedByDescending`, `maxByOrNull`, `minByOrNull`, `mapNotNull`, `filterIndexed`, `distinctBy`, `takeUnless`,
  `groupBy`, `associateBy`, `associateWith`, `chunked`, `partition`; `nextId++` as a value on a state read through `by`.
  Of the headers, a `constexpr` or `inline` constructor is one (`CornerRadius(24f)` constructs, `CornerRadius(x = …)`
  is still the designated form) and an alias declared in a namespace is that namespace's:
  `ListItemDefaults.colors(containerColor = …)` is a `ListItemColors`, which the plain `ListItem(colors = …)` takes too.
- **The showcase in Compose's syntax** (its first part): the shared `Section` and `DesktopNote`, the home screen (the
  uptime, the counter and to-do cards), the material and components screens' frames and the places screen (a
  `BottomSheetScaffold` over a map) are `@Composable fun`s in Kotlin, translated by kompozc beside the files still in
  C++, which include `<kompoz/blocks.hpp>` themselves for the block form (the shared header no longer does: the blocks
  are macros of the composables' names, which a translated file must not see); `kOnAndroid` says where the app runs
  instead of `#ifdef`.
- **kompozc: ViewModel parameters, a map's element, `ordinal`, a header's namespaces; `SegmentedButton` and a staggered
  grid's span as Compose writes them**: a parameter or property typed with a `ViewModel` class, declared in Kotlin or
  in a header, is the shared pointer its owner gives (`viewModel<T>()`); `map[k]` is nullable (`counts[k] ?: 0`) while
  a list's or a text's element is itself, `map.keys` and `map.values` are lists, `e.ordinal` an enum's place;
  `routes.Item(n)` reaches a struct in a C++ namespace; `List(n) { }` and `MutableList(n) { }`. A source's `#include
  "x.hpp"` is found in the target's include directories too (`kompoz_compose` passes them, `--include DIR`), so the
  classes and namespaces it declares are known.
  `SingleChoiceSegmentedButtonRow(modifier = …) { SegmentedButton(selected = …, onClick = …, shape =
  SegmentedButtonDefaults.itemShape(i, n), icon = { }) { Text(…) } }` and `MultiChoiceSegmentedButtonRow {
  SegmentedButton(checked = …, onCheckedChange = …) { } }` (`SegmentedButtonOptions::shape`,
  `SegmentedButtonDefaults::itemShape` in C++; the forms by place stay); `item(span = StaggeredGridItemSpan.FullLine) {
  }` and `items(list, span = { StaggeredGridItemSpan.SingleLane }) { }` in a staggered grid.
- **The showcase's list and item screens in Compose's syntax**: the rows, the window and media lines, the list's
  status, the grid and staggered tiles, `ListScreen` and `ItemScreen` are `@Composable fun`s in Kotlin: the likes
  `ViewModel` a parameter of its class, the segmented buttons by name with `SegmentedButtonDefaults.itemShape`, the
  staggered grid's title an `item(span = StaggeredGridItemSpan.FullLine)`, the likes read as `liked[index] ?: 0` and
  `liked.values.sum()`.
- **M3 Expressive's buttons and the sliders as Compose writes them; a lambda or null**: `ButtonGroup(overflowIndicator = {
  menuState -> … }) { clickableItem(onClick = …, label = "…", icon = { }); toggleableItem(checked = …, onCheckedChange = …, label
  = …) }` (its block declares the items; `ButtonGroupMenuState`'s `isExpanded`, `show()`, `dismiss()`; the items' icons are
  composables now, an item can be disabled) and the connected `ButtonGroup(modifier = …, horizontalArrangement = …,
  expandedRatio = …) { Button(modifier = Modifier.animateWidth(source)) { } }`; `ToggleButton(checked = …, onCheckedChange = …,
  shapes = …, enabled = …) { }` and its Elevated, Tonal and Outlined kinds; `SplitButtonLayout(leadingButton = { }, trailingButton
  = { })` with `SplitButtonDefaults.LeadingButton(onClick = …) { }` and `TrailingButton(checked = …, onCheckedChange = …) { }`;
  `Label(label = { PlainTooltip { } }, interactionSource = …, isPersistent = …) { }`; `ButtonDefaults.shapes()`. `Slider`,
  `VerticalSlider` and `RangeSlider` take `valueRange = 0f..100f`, a `RangeSlider`'s value may be a range (`200f..800f`, told
  back as one) and its `track = { }` is Compose's name; `rememberSliderState(value = …, steps = …, valueRange = …)` and
  `rememberRangeSliderState(activeRangeStart = …, activeRangeEnd = …, steps = …, valueRange = …)`; `DpSize(w, h)`. In kompozc,
  `if (c) { { … } } else null` is a lambda or none (as the function type it goes to: `leadingIcon = if (selected) { { Icon(…) } }
  else null`), `if (c) x else null` a nullable of x; `text.ifEmpty { }`, `ifBlank`; a range's `start` and `endInclusive`;
  `list.remove(x)` on a `SnapshotStateList`; `listOf<String>()` keeps its type argument. A C++ file that declares a Kotlin
  function's prototype itself is left to it (kompozc doesn't declare it again).
- **The showcase's material controls in Compose's syntax**: the buttons (the button groups with `clickableItem` /
  `toggleableItem` and with `Modifier.animateWidth`, the toggle and split buttons), the chips (`leadingIcon = if (on) { {
  Icon(…) } } else null`) and the selection controls (the sliders with `valueRange`, the price `RangeSlider` on a range,
  `Label` around the picked thumb, the slider states by name, the segmented buttons) are `@Composable fun`s in Kotlin.
- **Badges, segmented list items, the loading indicator, extended FABs and bottom app bars as Compose writes them; lazy
  lists over a `SnapshotStateList`**: `BadgedBox(badge = { Badge { Text("8") } }) { Icon(…) }` and `Badge()`;
  `SegmentedListItem(selected = …, onClick = …, shapes = ListItemDefaults.segmentedShapes(i, n), headlineContent = { },
  supportingContent = { }, leadingContent = { })` (or `onClick = …` alone; the C++ forms with a `ListItemSlots` stay);
  `LoadingIndicator(modifier = …, color = …)` and `LoadingIndicator(progress = …)`; `SmallExtendedFloatingActionButton` and the
  medium, large and classic ones take `icon = { Icon(…) }` and `text = { Text("…") }` as well as an `ImageVector` and a string;
  `BottomAppBar` and `FlexibleBottomAppBar` take `windowInsets = WindowInsets(0.dp)` (`BottomAppBarSlots::insets`,
  `FlexibleBottomAppBarOptions::insets`) as well as `false`. A lazy layout's `items(list)`, `items(list, key = …)` and
  `itemsIndexed` take a `SnapshotStateList` (a `mutableStateListOf`) as they take a vector; in kompozc, `list.shuffle()`,
  `list.shuffle(Random(seed))` (`kt::Random`, with `nextInt`, `nextFloat`, `nextBoolean`) and `addAll` on a `SnapshotStateList`.
- **The showcase's material containers in Compose's syntax**: the animated task list (a `mutableStateListOf` shuffled with
  `Random`, in a `LazyColumn` with `animateItem`), the list items with badges and the segmented ones, the dialogs, menus
  (groups with checked and selected items) and sheets, the cards, the FABs and the bottom app bar are `@Composable fun`s in
  Kotlin.
- **Tabs, the FAB menu and the floating toolbars as Compose writes them; a lambda that outlives its block keeps its
  scope**: `PrimaryTabRow(selectedTabIndex = …) { Tab(selected = …, onClick = …, text = { Text("…") }, icon = { Icon(…) }, enabled
  = …, selectedContentColor = …, unselectedContentColor = …) }`, `SecondaryTabRow`, `PrimaryScrollableTabRow` and
  `SecondaryScrollableTabRow` with `edgePadding = …`, `scrollState = …`, `LeadingIconTab(selected, onClick, text = { }, icon = { },
  …)` (the C++ forms by place stay); `FloatingActionButtonMenu(expanded = …, button = { }, horizontalAlignment = …) {
  FloatingActionButtonMenuItem(onClick = …, text = { }, icon = { }) }` (the item an extension of the menu's scope, as Compose's),
  `ToggleFloatingActionButton(checked = …, onCheckedChange = …, containerSize = …, checkedContainerSize = …, containerColor = …,
  checkedContainerColor = …, contentAlignment = …) { … }`; `HorizontalFloatingToolbar` and `VerticalFloatingToolbar` take `colors =
  …` (Compose's name) as well as `floatingToolbarColors = …`. In kompozc, a lambda written inside a block with a read-only
  receiver (an `AnimatedContentScope`, a `ToggleFloatingActionButtonScope`) keeps a copy of it, so one that runs after the
  block (`Modifier.graphicsLayer { rotationZ = 45 * checkedProgress }`, read as it draws) still has its scope.
- **The showcase's material navigation in Compose's syntax**: the tab rows, the FAB menu, the short navigation bar, the wide
  navigation rail and the floating toolbars are `@Composable fun`s in Kotlin (the destinations a data class, the FAB's plus
  turning in a `graphicsLayer` that reads `checkedProgress`).
- **kompozc: a text field's transformations and the focus manager as Compose writes them**: `InputTransformation.byValue {
  current, proposed -> … }` (its lambda gives its value), `OutputTransformation { if (length > 3) insert(3, " ") }` on the
  `TextFieldBuffer`, `LocalFocusManager.current` (`focusManager()`); `text.filter { it.isDigit() }` gives a text, and a
  character's `isDigit`, `isLetter`, `isLetterOrDigit`, `isWhitespace`, `isUpperCase` and `isLowerCase` are Kotlin's.
- **The showcase's text fields screen in Compose's syntax**: the fields, their transformations, colors, undo state,
  decorator and focus are a `@Composable fun` in Kotlin.
- **Swipe boxes, search bars, tooltips, pickers, focus properties and clicks as Compose writes them**:
  `rememberSwipeToDismissBoxState(confirmValueChange = { })`, `SwipeToDismissBox(state = …, modifier = …, backgroundContent = { },
  enableDismissFromStartToEnd = …) { }`; `SearchBar(state = …, inputField = { }, modifier = …)`, `ExpandedFullScreenSearchBar` and
  `ExpandedDockedSearchBar(state = …, inputField = { }) { }`, `SearchBarDefaults.InputField(textFieldState = …, searchBarState = …,
  onSearch = { }, placeholder = { Text("…") } or a string, leadingIcon = { }, trailingIcon = { })` (`InputFieldOptions::placeholderContent`);
  `TooltipBox(positionProvider = …, tooltip = { }, state = …, modifier = …) { }`, `PlainTooltip(modifier = …, caretSize = …) { }`,
  `RichTooltip(modifier = …, title = { }, action = { }, caretSize = …) { text }`; `DatePickerDialog(onDismissRequest = …, confirmButton
  = { }, dismissButton = { }) { }`, `DatePicker(state = …)`, `DateRangePicker(state = …)`, `TimePicker(state = …)`, `TimeInput(state =
  …)`, `TimePickerDialog(onDismissRequest = …, confirmButton = { }, dismissButton = { }, title = { }, modeToggleButton = { }) { }`;
  `Modifier.focusProperties { canFocus = false; next = other }`, `clickable(interactionSource = …, indication = null, enabled = …) {
  }` (`indication = null`: none), `verticalScroll(state, reverseScrolling = true)` and `horizontalScroll`'s; `tween(durationMillis
  = …, delayMillis = …, easing = …)`. The C++ forms by place stay. In kompozc, a class's static function is read as Compose's property
  (`WindowInsets.statusBars`, `WindowInsets.isImeVisible`); `LocalDate` is `std::chrono::year_month_day`; `Pair(a, b)` and
  `Triple(a, b, c)`; a nullable's member is read after its null check (Kotlin's smart cast); an `if` or `when` expression's branches
  take the `val`'s declared type (`val palette: TonalPalette? = if (…) system else null`).
- **The showcase's material sections in Compose's syntax**: search bars, tooltips, exposed dropdowns, interactions, anchored
  drags, ripples, semantics, focus properties, selectable rows, the date and time pickers, dynamic color (and its preview),
  the IME nested scroll, the frame rate and the insets are `@Composable fun`s in Kotlin (a few helpers for Turkish case folding
  and date and time formatting stay C++ in the same file).
- **Text styles as Compose writes them**: a typography style's `copy(brush = …, shadow = …, lineHeightStyle = …, textIndent = …,
  fontFeatureSettings = …, lineBreak = …, drawStyle = …, textGeometricTransform = …, color = …)` is a `TextStyle` of that font
  with those (`Font::copy` with a `TextStyle`'s names); `Modifier.basicMarquee(iterations = …, animationMode = …,
  repeatDelayMillis = …, initialDelayMillis = …, spacing = …, velocity = …)`; `TextAutoSize.StepBased(10.sp, 48.sp, 1.sp)` and
  `TextIndent(24.sp)` take text units; `rememberTextMeasurer().measure(text, style = …)` takes a typography style too.
- **The showcase's text screen in Compose's syntax**: the annotated strings, fonts, overflow, styles, inline content, HTML and
  marquee samples are a `@Composable fun` in Kotlin (the variable-font sample stays C++ in the file).
- **Drawing as Compose writes it, the rest**: `drawText(textMeasurer, text, topLeft = …, style = …, overflow = …, maxLines = …)`
  by name (the style a `TextStyle` or a typography style); `record(graphicsLayer, size = …) { … }` by name, and a
  `GraphicsLayer`'s `alpha`, `scaleY`, `topLeft`… written on the layer itself in Kotlin (`layer.alpha = 0.35f`: its
  `properties()`); `Modifier.onVisibilityChanged(minDurationMs = …, minFractionVisible = …, viewportBounds = …) { visible -> }`
  and `Modifier.draggable2D(state, startDragImmediately = …, onDragStarted = …, …)` by name; `Offset.Zero`, `Size.Zero`,
  `IntOffset.Zero`; `PointerIcon.Move` and the other cursors. kompozc: kotlin.math (`sin`, `cos`, `sqrt`, `abs`, `min`, `max`,
  `floor`, `round`, `pow`, `PI`…), `Rect(left, top, right, bottom)` and `Rect(topLeft, size)` with Compose's meaning (Kompoz's
  `Rect` holds x, y, width, height), `CornerSize(percent)` / `CornerSize(dp)`, a `PathMeasure`'s `length`,
  `withTransform({ translate(…); rotate(…) }) { … }`'s transform block (a `DrawTransform`), `rememberScrollable2DState { }`'s value.
- **The showcase's drawing screen in Compose's syntax**: the Canvas samples (the donut, the sine wave, path effects,
  PathMeasure, Path.combine in drawWithCache, clipPath and withTransform, brushes), the spinning icon, the corner shapes,
  the draggable ball and the long-press card, scrollable2D, the GraphicsLayer reflection, taps and combinedClickable are a
  `@Composable fun` in Kotlin.
- **Desktop input as Compose Multiplatform writes it**: `Popup(popupPositionProvider = object : PopupPositionProvider { … },
  onDismissRequest = …, properties = PopupProperties(focusable = …)) { }` and `Popup(alignment = …, offset = …) { }` (the
  `OverlaySpec` form stays; a non-focusable popup's content still takes presses); `ContextMenuArea(items = { listOf(ContextMenuItem("…")
  { }) }, enabled = …) { }` and `TooltipArea(tooltip = { }, modifier = …, delayMillis = …, tooltipPlacement = …) { }` as Composables
  (the positional forms as before); `Modifier.onClick(enabled = …, matcher = …, keyboardModifiers = { it.isCtrlPressed }, onLongClick = …,
  onDoubleClick = …) { }` and `Modifier.transformable(state, canPan = { }, lockRotationOnZoomPan = …, enabled = …)` by name;
  `clickable(interactionSource = null, …)`; a pointer event's `keyboardModifiers.isCtrlPressed` / `isShiftPressed` / `isAltPressed`;
  `Offset + Offset`, `-`, `*`, `/`; `DpOffset(x, y)`; `IntRect`. kompozc: `event.changes.first()` is the change itself (`consume()` on it
  counts); `items = { }`, `keyboardModifiers = { }` and `canPan = { }` give their value; a destructuring (`val (a, b) = …`,
  `for ((k, v) in …)`) takes its parts with `kt::component<i>` (copies, as Kotlin's) instead of a structured binding, which
  GCC 16 loses inside a composable's nested blocks.
- **The showcase's input screen in Compose's syntax**: selectable text, the context menu, the mouse (pointer events, clicks by
  button, tooltip areas), the custom indications, the app's own text toolbar in a Popup, multi-touch, the keyboard, the
  selectable cards and drag and drop are `@Composable fun`s in Kotlin (the indication nodes, the toolbar class and the key
  names stay C++ in the same file).
- **Channels, lifecycle and padding as Kotlin writes them**: `trySend` gives a `ChannelResult` (`isSuccess` / `isFailure`,
  a bool as before); `CoroutineScope::produce<T>(block)` is Kotlin's `scope.produce<Int> { send(i) }`; `consumeEach` and
  `collect` take a suspending callback; `Lifecycle::State` / `Lifecycle::Event` with Compose's `RESUMED` / `ON_RESUME`
  names; `ViewConfiguration::longPressTimeoutMillis()` and the other timeouts in millis; the semantics receiver's
  `contentType`; `SharingStarted::WhileSubscribed(5000)`; `PaddingValues(all)`, `(horizontal, vertical)`, `(start, top, end,
  bottom)` and by name through kotlin.hpp (Kompoz's struct keeps its fields); `list + list` and `list + element` for the lists
  kotlin.hpp makes. kompozc: a suspending lambda's parameters (`consumeEach { order -> }`, `it`); `scope.produce<Int> { }`'s
  block sends to its channel; `flow.collectAsStateWithLifecycle()` as a member; `LocalLifecycleOwner.current`,
  `LocalClipboard.current`, `LocalAutofillManager.current`.
- **The showcase's state screen in Compose's syntax**: the icon gallery and icon buttons, the StateFlow download, the flow
  search (its ViewModel a Kotlin class with a SavedStateHandle, debounce, flatMapLatest, stateIn and a SharedFlow), movable
  content, the channel kitchen and produce countdown, the lifecycle section, the chat and the platform services are
  `@Composable fun`s in Kotlin (the thread-based download ViewModel, the lifecycle observer ViewModel and the prime count stay
  C++ in the same file).
- **Scrolling, app bars and pull to refresh as Compose writes them**: `AppBarRow(overflowIndicator = …, maxItemCount = …) {
  clickableItem(onClick = …, icon = { }, label = "…") }` and `AppBarColumn`, `AppBarWithSearch(state = …, inputField = { },
  navigationIcon = …, actions = …, scrollBehavior = …)`, `Modifier.pullToRefresh(isRefreshing = …, state = …, onRefresh = …,
  enabled = …, threshold = …)`, `PullToRefreshDefaults.Indicator(state = …, isRefreshing = …, modifier = …, containerColor = …,
  color = …, threshold = …)` and `IndicatorBox(state = …, isRefreshing = …, threshold = …, shape = …, containerColor = …,
  elevation = …) { }` as Composables (the positional forms as before); `LoadingIndicator(progress = { … })`; a list's or pager's
  `overscrollEffect = null` is none, as Compose's; a `VelocityTracker`'s copies share its samples (a gesture's handlers measure
  one fling). kompozc: `try { } catch (e: Exception) { } finally { }` (the finally block runs when the scope ends, a cancelled
  coroutine's too); `OverscrollEffect`, `NestedScrollConnection`, `PopupPositionProvider`, `IndicationNodeFactory`, `TextToolbar`
  and `LifecycleOwner` parameters are shared pointers, a nullable one an empty pointer (`null`); `Animatable<Float,
  AnimationVector1D>` is `Animatable<float>`; `PredictiveBackHandler(enabled) { steps -> … }`.
- **The showcase's scroll screen in Compose's syntax**: the flings, the drawers, the scrollbars and bring-into-view, predictive
  back, pointer passes, the scrollables and the ruler, overscroll (its custom effect stays C++), the bring-into-view spec, nested
  scrolling with the app bars, pagers and the search app bar, and pull to refresh are `@Composable fun`s in Kotlin.
- **Layouts as Compose writes them, the rest**: `ListDetailPaneScaffold(navigator = …, listPane = { }, detailPane = { key -> },
  listPaneWidth = …, paneExpansionState = …, paneExpansionDragHandle = { state -> })` and `SupportingPaneScaffold(navigator = …,
  mainPane = { }, supportingPane = { key -> }, supportingPaneWidth = …)` as Composables, the key type the navigator's (the C++
  `ListDetailPaneScaffold(navigator, …)` deduces it too: no `<int>`); `VerticalDragHandle(modifier = …, interactionSource = …)`;
  `Modifier.paneExpansionDraggable(state, interactionSource)`; `Modifier.onFirstVisible(minDurationMs = …, minFractionVisible = …)
  { }` and `approachLayout(isMeasurementApproachInProgress = { }, approachMeasure = { })` by name; `Arrangement.spacedBy(space,
  Alignment.CenterHorizontally)`; `spring<Float>(…)`; pagers take Compose's `key = { }`; `DeferredTargetAnimation::updateTarget`
  takes its scope by const reference. kompozc: `"ab".repeat(n)`; a list indexed by a nullable after its null check (`cities[city]`).
- **The showcase's layout screen in Compose's syntax**: the pane scaffolds, layout info, the measured bar chart, positions,
  the snapping row, carousels, horizontal grids, pagers, z-index, FlexBox, Grid and the custom layouts (Layout, SubcomposeLayout,
  intrinsic sizes, LookaheadScope, approachLayout) are `@Composable fun`s in Kotlin.
- **Graphics and animation as Compose writes them**: `keyframes { durationMillis = …; 1.35f at 110 using LinearEasing }`
  (`KeyframesConfig`), `spring(dampingRatio = …, stiffness = …)`, the enter and exit transitions by name (`fadeIn(animationSpec =
  …)`, `slideInVertically(animationSpec = …) { it }`, `scaleOut(targetScale = …)`, …), `animateEnterExit(enter = …, exit = …)`,
  `AnimatedVisibility(visibleState = …)`; `LinearWavyProgressIndicator`, `CircularWavyProgressIndicator` and
  `ContainedLoadingIndicator` as Composables (`progress = { }`, the options' fields by name; by place the options struct is
  named: `WavyProgressIndicatorOptions{…}`); `Modifier.shadow`, `paint`, `blur` and `magnifier` by name; `drawRect(brush = …,
  blendMode = …)`, the paint by name too; `Offset::Unspecified`; `ColorMatrix(std::vector<float>)`; `ShapeOutline::toShape()`;
  a `Morph`'s copies share its outlines, `toShape` is const; Kotlin's `list - element` and `list - list`. kompozc: `object :
  Painter() { override val intrinsicSize …; override fun DrawScope.onDraw() { } }` (`Painter::custom`), `Canvas(imageBitmap)`,
  `MaterialShapes.Heart` as a value, `x and y` (`or`, `xor`, `shl`, `shr`), `return@Label`, `X().apply { }`, a property set
  through its setter (`state.targetState = false`, `paint.alpha = 0.6f`), a top-level val's member (`BallVector.root`), and a
  local of a function type called with its trailing lambda.
- **The showcase's graphics screen in Compose's syntax**: progress and loading indicators, the animations, AsyncImage,
  images and painters, Material's shapes and morphs, shapes and shadows, shared elements, blend and tile modes, color spaces,
  the canvas, blur and the color vectors are `@Composable fun`s in Kotlin (the star outline, a pixel-made bitmap and a
  `VectorConfig` stay C++). Every components screen's sections are now in Kotlin.
- **The app shell as Compose writes it**: `NavDestination` carries its graphs: `destination.hierarchy` and
  `destination.hasRoute<T>()` as Compose's (`hierarchy.any { it.hasRoute<Tab>() }` for the tab shown); `navOptions { popUpTo<Home>
  { saveState = true }; … }` and a typed `popUpTo<T> { }`; typed `composable<T>(deepLinks = …) { }` and `navigation<T>(startDestination =
  …) { }` by name. Compose 1.4's `NavigationSuiteScaffold(navigationItems = { … }, navigationSuiteType = …)` and `NavigationSuiteItem`
  as a Composable (`selected`, `onClick`, `icon`, `label`, `enabled`, `navigationSuiteType`, `badge`, `interactionSource` by name; by
  place its options struct is named: `NavigationSuiteItemOptions{…}`). Compose Multiplatform's menu builder: `MenuBar { Menu("…") {
  Item(…); CheckboxItem(…); RadioButtonItem(…); Separator(); Menu("…") { } } }` (`MenuBarScope`, `MenuScope`). `SnackbarData::visuals()`
  (Compose's `data.visuals`, `actionLabel` none when empty). Compose's key names beside Kompoz's: `Key::DirectionLeft`, `MoveHome`,
  `Spacebar`, … kompozc: `a?.f<T>()` keeps its type arguments.
- **The showcase's app shell in Compose's syntax**: `ShowcaseApp` (the theme and its color source, the drawer, the top bar
  and its menu, the FAB and its snackbars, the NavHost with its nested graph and deep links, the navigation suite and, off
  Android, the window's menus) is a `@Composable fun` in Kotlin; only `createApplication` stays C++. Every screen of the showcase
  is now written in Compose's syntax.
- **A language server for files with Compose code**: `kompozc --lsp` stands in front of clangd. In the C++ around the
  Kotlin, clangd answers as it does (it reads the file with the Kotlin blanked and the Kotlin's declarations in its place);
  in the Kotlin, kompozc's own errors and the C++ compiler's on the translation show on the Kotlin's own lines, and clangd's
  completion, hover and formatting stay out. The build directory is found by `compile_commands.json` and the new
  `kompozc_commands.json` that `kompoz_compose` writes (in `build/`, CLion's `cmake-build-*/`, ...). VS Code (the clangd
  extension's `clangd.path`) and Kate (the LSP client's `c` server) are set up in the guide's Tools page. Without a clangd it
  gives kompozc's own diagnostics. kompozc's translation is now a library of its own (`translate.cpp`), and a missing property
  read in Kotlin is reported by its name ("no property, member or field named x").
- **A CLion plugin for files with Compose code** (`editors/clion`): it starts `kompozc --lsp` for the C++ files with
  Kotlin in them and turns CLion's own highlighting off there (its C++ engine would mark the Kotlin); the server's
  diagnostics take its place. kompozc is found in Settings, Tools, Kompoz, or in `KOMPOZC`, or built in the project, or on
  `PATH`. Built against a CLion of yours (`./gradlew buildPlugin -PclionPath=…`) and installed from disk; CI builds it and
  checks it with JetBrains' Plugin Verifier against CLion 2026.3.
- **Image assets are decoded once**: `imageResource` gives the same bitmap for the same path wherever it is asked for
  and however often its screen comes back, as Android keeps its resources' (the host keeps those used last, up to
  32 MB of pixels). On the desktop the showcase's Components screen opened in 28 ms instead of 35 the first time and
  in 9 instead of 18 after.
- **ADPF on Android 13 and later**: each frame's work (its waits left out) is told to the system against the display's
  refresh period (`APerformanceHint`), so that it gives the thread drawing the frames the cores and clock it needs.
- **Frame times fall on the display's beat**, as Android's Choreographer gives them: a whole number of refresh
  periods after the last frame's, the nearest to now, so animations move by equal steps however late in its period
  each frame starts (`detail::frameTimeOnBeat`).
- **`KOMPOZ_PROFILE` counts waiting for the window's image as a wait**, not as drawing: with the display's FIFO
  presentation that waits for the next refresh to give one back.
- **Nodes whose drawing didn't change aren't drawn again**, as Compose's render nodes: a frame records again what such
  a node and all in it drew last frame, moved as far as it moved (a list scrolled by). A node is drawn again when it is
  measured, when a state its drawing read changes, when it asks for a redraw as it draws, and when it is pressed,
  hovered or focused or its ripple moves. `KOMPOZ_KEEP_CHECK` (and the tests) check each frame against one drawing every
  node; `KOMPOZ_PROFILE`'s slow frames count the nodes drawn and kept. On a Xiaomi 14 Ultra a frame of the Components
  screen that changes nothing on it took 0.4 ms instead of 5.
- **Fixed**: a shared element whose partner was drawn after it saw its partner's place only when drawn again: a frame
  drawn twice (a magnifier's) drew it in two places. Where each belongs is noted before any is drawn.
- **Recording a frame allocates nothing a node**: the display list's maps (the times each node was drawn, the last
  frame's segments and layers) are one open-addressing table emptied without being freed, and drawing asks no cast
  whether an element is a scrollable. On a Redmi Note 9 Pro an idle screen's recording took 4.4 ms instead of 5.7.
- **Fixed**: a lifted finger hovered what was under it where the platform didn't say the pointer left: a tooltip's
  anchor tapped showed its tooltip, and every frame drawn hit tested for hover. Hover now follows a resting mouse only.
- **Fixed**: on an Adreno 618 (a Redmi Note 9 Pro's), the GPU renderer drew clips, color filters and gradients from
  other rows of its frame's table than theirs, the further off the more of them came before (a button's corner, cut to
  its shape, up to 220/255 off): the table's texels are fetched, not sampled.
- **Fixed**: the GPU renderer's turned, stretched and leaning edges were up to 10/255 off on GPUs that snap vertices
  coarsely (4 subpixel bits, as SwiftShader does: Chrome's WebGL where it doesn't use the GPU): a quad's corners are
  put where such a GPU would put them, and its pixels shaded from there.
- **Fixed**: on the GPU renderer a turned line's square caps lost their corners' faintest pixels: its quad didn't reach
  them.
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
