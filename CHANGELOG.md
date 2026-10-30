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
  `scripts/release.sh` for publishing a release's packages. The vcpkg jobs build the Kompoz of the commit they test
  (vcpkg's binary cache, keyed by the port's files and not by the sources, restored an earlier one's).
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
  waits for someone to close it. Conan Center's SDL has no OpenGL ES on Windows, so an app built with Conan draws with
  Vulkan only there (ANGLE needs an SDL with EGL); the Conan test draws on Mesa's lavapipe built for Windows.
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
  they aren't there, or with `KOMPOZ_PLATFORM=sdl`, apps run on SDL's platform. X11 comes next.
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
