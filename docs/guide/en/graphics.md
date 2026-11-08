# Graphics

Drawing your own graphics with `Canvas` and the drawing modifiers, brushes and paths, clipping and shadows, `graphicsLayer`, and showing images and vectors.

## Canvas

`Canvas` is a composable that draws whatever you tell it in a `DrawScope`. Give it a size with its modifier:

```kotlin
@Composable
fun Target() {
    val ring = MaterialTheme.colorScheme.primary
    val center = MaterialTheme.colorScheme.error
    Canvas(modifier = Modifier.size(160.dp)) {
        drawCircle(ring.copy(alpha = 0.2f))                                  // fills the canvas
        drawCircle(ring, radius = size.width / 3, style = Stroke(width = 6f))
        drawCircle(center, radius = 12f)                                     // at the center
    }
}
```

The draw lambda runs while drawing, not while composing. Read the colors you need from the theme in composition, as above, and capture them by value.

### What a DrawScope can draw

| Call | Draws |
|---|---|
| `drawRect`, `drawRoundRect`, `drawCircle`, `drawOval` | Shapes, filled or stroked; a rounded rect's `CornerRadius(x, y)` can make its corners quarter ellipses. |
| `drawArc(color, start, sweep, useCenter, …)` | Part of an oval: a pie slice through its center (`useCenter`), else closed straight across or an open arc. |
| `drawLine(color, start, end, width, cap, pathEffect)` | A line; with a `StrokeCap` and a dash `PathEffect`. |
| `drawPoints(points, mode, color, width)` | Points, lines between pairs, or a polyline. |
| `drawPath(path, color, alpha, style)` | Any path. |
| `drawText(layout, color, topLeft)`, `drawText(measurer, text, topLeft, style)` | Text (see [Text](text.md#measuring-text)). |
| `drawImage(image, topLeft)`, `drawImage(image, srcOffset, srcSize, dstOffset, dstSize, …, filterQuality)` | A bitmap, or part of it into a rect, sampled as `FilterQuality` says. |
| `painter.draw(d, size, alpha, colorFilter)` | A `Painter` (an `ImageVector`'s: `VectorPainter(vector)`), as Compose's `with(painter) { draw(…) }`. |

They take Compose's parameters, by name as Compose's: a color or a brush, then `topLeft`, `size`, `radius`, `center`, `alpha`, `style`, `colorFilter` and `blendMode` (`drawRect(color, size = size, alpha = 0.5f, style = Stroke(width = 2f))`). `DrawStyle` is `Fill` (the default) or `Stroke(width, miter, cap, join, pathEffect)`; a width of 0 is a hairline, one pixel wide whatever the transform. Coordinates are in dp from the canvas's top-left corner; `d.size()` and `d.center()` give its size and center, `d.density()` the pixels in a dp.

### Transforming

`translate`, `rotate` (and `rotateRad`), `scale`, `inset`, `clipRect` and `clipPath` take a block: what the block draws is moved, turned, scaled or clipped. `clipRect` and `clipPath` keep what is inside, or with `ClipOp.Difference` what is outside. `withTransform` does several at once, and its `transform(matrix)` takes any `Matrix` (Compose's 4x4, perspective too). As in Compose, the transform goes into the drawing itself: nothing is clipped to the drawing's area, and on the GPU renderer everything stays sharp and exactly anti-aliased however it is turned, stretched or seen in perspective (text is laid out at the size it is shown).

```kotlin
@Composable
fun Clock(minutes: Float) {
    val hand = MaterialTheme.colorScheme.onSurface
    Canvas(modifier = Modifier.size(120.dp)) {
        drawCircle(hand, style = Stroke(width = 2f))
        rotate(minutes * 6) {   // 6° per minute, around the center
            drawLine(hand, start = center, end = Offset(center.x, 12f), strokeWidth = 3f, cap = StrokeCap.Round)
        }
    }
}
```

## Drawing modifiers

You don't need a `Canvas` to draw on an existing composable. These modifiers draw on its node:

- `drawBehind(onDraw)` draws behind the content.
- `drawWithContent(onDraw)` draws around it: call `drawContent()` where the content goes, and draw before or after it.
- `drawWithCache(build)` builds objects once per size (paths, brushes) and draws with them every frame.

```kotlin
@Composable
fun Highlighted(text: String) {
    val marker = Color(0xFFFFEB3B).copy(alpha = 0.6f)
    Text(text, modifier = Modifier.drawBehind {
        drawRect(marker, topLeft = Offset(0f, size.height * 0.55f), size = Size(size.width, size.height * 0.4f))
    })
}

@Composable
fun Faded(content: @Composable () -> Unit) {
    Box(modifier = Modifier.drawWithContent {
        drawContent()
        drawRect(Brush.verticalGradient(listOf(Color.Transparent, Color.White)))   // a fade over the bottom
    }) { content() }
}
```

## Colors and color spaces

A `Color` is Compose's: 64 bits, in sRGB 8 bits a channel, or in another of Compose's color spaces (`ColorSpaces`: Display P3, BT.2020, Adobe RGB, CIE L\*a\*b\*, Oklab and more) its components as half floats. `convert` turns one into another space and `toArgb()` gives its sRGB as `0xAARRGGBB`; whatever space a color is in, it is drawn in sRGB.

```kotlin body
val p3 = Color(0.1f, 0.75f, 0.45f, 1f, ColorSpaces.DisplayP3)   // in Display P3
val lab = Color(55f, 60f, -40f, 1f, ColorSpaces.CieLab)        // L*, a*, b*
val between = lerp(Color.Red, Color.Blue, 0.5f)                 // mixed in Oklab
Row(modifier = Modifier.height(40.dp)) {
    for (c in listOf(p3, lab, between)) Box(modifier = Modifier.size(40.dp).background(c))
}
```

`lerp` mixes two colors in Oklab, as Compose's does: halfway from red to blue is a violet as light as both, not the dark purple their sRGB numbers make. An animated color goes the same way (`animateColorAsState`). A gradient mixes its colors in sRGB, as Skia's.

## Brushes

A `Brush` paints with a solid color or a gradient, wherever a color would go: drawing calls, `background`, text.

```kotlin body
Box(modifier = Modifier.fillMaxWidth().height(120.dp).background(
    Brush.linearGradient(listOf(Color(0xFF6750A4), Color(0xFFB58392))), RoundedCornerShape(16.dp)))
```

`Brush.solid` (Compose's `SolidColor`), `linearGradient`, `horizontalGradient`, `verticalGradient`, `radialGradient` and `sweepGradient` are there, with color stops and tile modes.

A brush of your own is a shader made for the size it paints, as Compose's `ShaderBrush`: a gradient, an image tiled, or two shaders composited.

```kotlin body
Box(modifier = Modifier.size(120.dp, 72.dp).background(Brush.shader { size ->
    CompositeShader(
        LinearGradientShader(Offset(0f, 0f), Offset(size.width, 0f), listOf(Color(0xFF6750A4), Color(0xFFB58392))),
        RadialGradientShader(Offset(24f, 20f), 48f, listOf(Color.White.copy(alpha = 0.9f), Color.Transparent)),
        BlendMode.Screen,
    )
}))
```

`Brush.composite` does the same with two brushes, `withTransform` moves what a gradient or a shader paints, and `lerp` goes from one brush to another as an animation does.

## Canvas and Paint

Under a `DrawScope` is Compose's `Canvas`, with its stack of saves and its `Paint` (a struct here: `Paint(color = …, style = …)`, where Compose's has setters):

```kotlin body
Canvas(modifier = Modifier.size(96.dp)) {
    drawIntoCanvas { canvas ->
        val spoke = Paint(color = Color(0xFF6750A4), style = PaintingStyle.Stroke, strokeWidth = 4f, strokeCap = StrokeCap.Round)
        for (i in 0 until 12) {
            canvas.save()
            canvas.rotate(i * 30f, 48f, 48f)
            canvas.drawLine(Offset(48f, 30f), Offset(48f, 8f), spoke)
            canvas.restore()
        }
    }
}
```

What the canvas leaves saved or transformed holds for the scope's later draws until the block it is in ends, as in Compose.

## Paths

A `Path` describes any outline with lines, curves and arcs:

```kotlin
fun heart(s: Size): Path {
    val p = Path()
    val w = s.width
    val h = s.height
    p.moveTo(w / 2, h * 0.9f)
    p.cubicTo(w * 0.1f, h * 0.6f, 0f, h * 0.25f, w * 0.25f, h * 0.1f)
    p.cubicTo(w * 0.4f, 0f, w / 2, h * 0.15f, w / 2, h * 0.25f)
    p.cubicTo(w / 2, h * 0.15f, w * 0.6f, 0f, w * 0.75f, h * 0.1f)
    p.cubicTo(w, h * 0.25f, w * 0.9f, h * 0.6f, w / 2, h * 0.9f)
    p.close()
    return p
}

@Composable
fun Heart() {
    val red = Color(0xFFE53935)
    Box(modifier = Modifier.size(64.dp).drawWithCache {
        val path = heart(size)   // built once per size
        onDrawBehind { drawPath(path, red) }
    })
}
```

`moveTo`, `lineTo`, `quadraticTo`, `conicTo`, `cubicTo`, their `relative…` forms, `arcTo`, `addRect`, `addOval`, `addRoundRect` and `close` build it, as Compose's (an Android path) does: shapes added whole go counterclockwise unless you pass `Path.Direction.Clockwise`, and arcs and ovals are conics, exact. `transform(matrix)` moves it, `op` (or `a + b`, `a - b`, `a & b`) combines two, keeping their curves, `iterator()` walks its segments, `PathHitTester` tests points against it again and again. `PathMeasure` measures its length and gives segments of it (pieces of its curves, as curves), for drawing a path progressively, as Android's does.

A `PathEffect` on a stroke (or a `Paint`) changes the path as it is drawn, as Skia's: dashes, rounded corners, a shape stamped along it, or one after another (`chainPathEffect`).

```kotlin body
val primary = MaterialTheme.colorScheme.primary
val wave = Path()
wave.moveTo(0f, 20f)
wave.cubicTo(30f, -10f, 60f, 50f, 90f, 20f)
Canvas(modifier = Modifier.size(100.dp, 40.dp)) {
    drawPath(wave, primary, style = Stroke(width = 3f, cap = StrokeCap.Round, join = StrokeJoin.Round, pathEffect = PathEffect.dashPathEffect(floatArrayOf(8f, 6f))))
}
```

It is applied where the path is drawn, at the pixels it is drawn at, so a dash on a curve lands where Compose's does at any scale.

## Shapes and clipping

A `Shape` is the outline used by backgrounds, borders, clips and shadows: `RectangleShape`, `CircleShape`, `RoundedCornerShape(16.dp)` (or one radius per corner), `CutCornerShape(8.dp)`, and Material 3 Expressive's shapes (`MaterialShapes.Cookie9Sided()` and others, with `toShape`).

```kotlin body
Box(modifier = Modifier.size(96.dp).clip(CircleShape).background(MaterialTheme.colorScheme.tertiaryContainer))
Box(modifier = Modifier.size(96.dp).border(2.dp, MaterialTheme.colorScheme.outline, RoundedCornerShape(12.dp, 12.dp, 0.dp, 0.dp)))
```

As in Compose, a shape makes its outline in a size and layout direction (`createOutline`): a rect, a rounded rect (`Outline.Rounded(RoundRect(…))`, whose corners may be elliptical) or any path. `GenericShape` builds one from a path (a `Rect` is `(x, y, width, height)` here, not Compose's left, top, right, bottom):

```kotlin body
val ticket = GenericShape { size, _ ->
    moveTo(0f, 0f)
    lineTo(size.width, 0f)
    lineTo(size.width, size.height)
    arcTo(Rect(size.width / 2 - 12, size.height - 12, 24f, 24f), 0f, -180f, false)  // a notch at the bottom
    lineTo(0f, size.height)
    close()
}
Box(modifier = Modifier.size(120.dp, 72.dp).clip(ticket).background(MaterialTheme.colorScheme.primaryContainer))
```

`clip(shape)` clips the content to the shape, its edge anti-aliased, without a layer.

## Shadows

`shadow(elevation, shape)` raises a node: as Compose's, it is a `graphicsLayer` with that `shadowElevation`, its shape's shadow drawn as Android draws one, lit from above the top of the window (so shadows lengthen down the screen), and the content clipped to the shape (`clip`, by default when the elevation is above 0). `dropShadow(shape, Shadow{…})` and `innerShadow(shape, Shadow{…})` draw shadows with your own color (or `brush`), offset, blur radius, spread, alpha and blend mode:

```kotlin body
Box(modifier = Modifier.size(120.dp)
    .dropShadow(RoundedCornerShape(24.dp), Shadow(color = Color(0xFF6750A4).copy(alpha = 0.4f), offset = Offset(0f, 8f), blurRadius = 24f))
    .background(MaterialTheme.colorScheme.surface, RoundedCornerShape(24.dp)))
```

## graphicsLayer

`graphicsLayer` draws the content scaled, rotated (in 3D too: `rotationX`, `rotationY`), moved, faded, clipped, filtered, blended or blurred, without changing its layout. As in Compose it goes through an offscreen layer only where it must (faded, a blend mode, a color filter, a render effect, or `CompositingStrategy.Offscreen`); otherwise the transform and the clip go straight into the drawing:

```kotlin body
Box(modifier = Modifier.size(80.dp).graphicsLayer(scaleX = 1.2f, scaleY = 1.2f, alpha = 0.8f, rotationZ = 15f)
    .background(MaterialTheme.colorScheme.primary, RoundedCornerShape(16.dp)))
```

The form that takes a lambda is evaluated while drawing, so animated values read there change without recomposing or laying out again. Use it for anything that changes every frame (see [Phases](phases.md#deferring-state-reads)):

```kotlin
@Composable
fun Spinner() {
    val angle by rememberInfiniteTransition().animateFloat(0f, 360f, infiniteRepeatable(tween(1200, 0, LinearEasing)))
    Icon(Icons.Filled.Refresh, contentDescription = "Loading", modifier = Modifier.graphicsLayer { rotationZ = angle })
}
```

`alpha(a)`, `scale(s)` and `rotate(degrees)` are shorthands for the common cases.

### Blur and render effects

`blur(radius)` blurs the content, as Compose's does. By default the blur stays within the node's bounds (`BlurredEdgeTreatment.Rectangle`: the pixels past the edges count as the edges'); `BlurredEdgeTreatment.Unbounded` lets it spread out past them, fading; `BlurredEdgeTreatment{shape}` cuts it to a shape:

```kotlin body
Image(imageResource("images/cover.jpg"), contentDescription = "Album cover",
      modifier = Modifier.size(160.dp).blur(12.dp, BlurredEdgeTreatment(RoundedCornerShape(16.dp))), contentScale = ContentScale.Crop)
```

It is a `graphicsLayer` with a render effect, and you can give one a render effect yourself. `BlurEffect(radiusX, radiusY, edgeTreatment)` blurs its pixels (a Gaussian, Android's for the radius; the `TileMode` says what lies past the edges); `OffsetEffect(x, y)` moves them. Each takes another to go through first:

```kotlin body
Box(modifier = Modifier.size(96.dp).graphicsLayer(renderEffect = OffsetEffect(BlurEffect(6f, 6f, TileMode.Decal), Offset(0f, 4f)))
    .background(MaterialTheme.colorScheme.primary, CircleShape))
```

What is drawn within the layer's bounds goes through the effect; with `clip` set, what the effect makes is then cut to the layer's shape.

`blur` also blurs progressively, as Compose 1.13's does: more in some places than others, from one radius to another (or through `BlurStop`s) down, across, along a line or out from a center. The form that takes a lambda sets it while drawing:

```kotlin body
Image(imageResource("images/cover.jpg"), contentDescription = "Album cover",
      modifier = Modifier.size(160.dp).blur(BlurRadiusSpec.verticalGradient(0.dp, 16.dp)), contentScale = ContentScale.Crop)  // sharp at the top
Box(modifier = Modifier.size(120.dp).blur {
    blurRadiusSpec = BlurRadiusSpec.radialGradient(0.dp, 12.dp)   // sharp in the middle
}.background(MaterialTheme.colorScheme.primary, CircleShape))
```

Kompoz's `blur` also takes what to blur (`BlurType`). `Family`, the default, blurs the node and its children, as Compose's does. `Morph` blurs what is drawn behind the node, within its shape, and draws the node over it sharp: frosted glass. `Self` blurs only what the node draws itself (its background, border, image) and draws its children over it sharp:

```kotlin body
Box(
    modifier = Modifier.size(200.dp, 72.dp)
        .blur(16.dp, BlurredEdgeTreatment(RoundedCornerShape(16.dp)), BlurType.Morph)
        .background(Color.White.copy(alpha = 0.2f), RoundedCornerShape(16.dp)),
    contentAlignment = Alignment.Center,
) { Text("Frosted glass") }
```

## Images

`Image` shows a bitmap; `imageResource(path)` loads one from the app's assets, once:

```kotlin body
Image(imageResource("images/cover.jpg"), contentDescription = "Album cover", modifier = Modifier.size(160.dp).clip(RoundedCornerShape(12.dp)), contentScale = ContentScale.Crop)
```

`AsyncImage` loads an image from the assets or from the web (`http://`, `https://`), decodes it at the size it is shown at, and keeps downloads on disk. It shows a placeholder while loading and something else if it fails:

```kotlin body
AsyncImage(
    model = "https://example.com/photo.webp",
    contentDescription = "A photo",
    modifier = Modifier.size(200.dp),
    placeholder = { CircularProgressIndicator() },
    error = { Icon(Icons.Filled.Info, contentDescription = "Couldn't load") },
    contentScale = ContentScale.Crop,
    crossfadeMillis = 200,
)
```

JPEG, PNG and WebP are supported. `ContentScale` fits the image into its box as Compose's does: `Fit`, `Crop`, `FillBounds`, `FillWidth`, `FillHeight`, `Inside`, `None`.

## Vectors and icons

`Icon` draws an `ImageVector` tinted with the content color, at 24 dp by default. Kompoz has Material icons under `Icons.Filled` and `Icons.AutoMirrored.Filled` (the latter mirror in right-to-left layouts):

```kotlin body
Icon(Icons.Filled.Favorite, contentDescription = "Favorite", tint = Color(0xFFE53935))
```

Your own vectors come from SVG path data, from Android vector drawable files, or built in code:

```kotlin
val SparkleIcon = ImageVector.fromSvgPath("M12,2l2.4,7.6L22,12l-7.6,2.4L12,22l-2.4-7.6L2,12l7.6-2.4z")

// Paths in paints of their own, in groups, as Compose's ImageVector.Builder makes them.
val BadgeIcon = ImageVector.Builder("Badge", 24.dp, 24.dp, 24f, 24f)
    .addPath(pathData = addPathNodes("M12,2a10,10 0 1,0 0.01,0z"), fill = Brush.solid(Color(0xFF1E88E5)))
    .group(rotation = -10f, pivotX = 12f, pivotY = 12f) {
        path(stroke = Brush.solid(Color.White), strokeLineWidth = 2f, strokeLineCap = StrokeCap.Round) {
            moveTo(7f, 12f)
            lineTo(10.5f, 15.5f)
            lineTo(17f, 9f)
        }
    }
    .build()

@Composable
fun Sparkle() {
    Icon(SparkleIcon, contentDescription = null)
    Image(rememberVectorPainter(BadgeIcon), contentDescription = "Badge")
    Image(rememberVectorPainter(vectorResource("vectors/logo.xml")), contentDescription = "Logo", modifier = Modifier.size(48.dp))
}
```

`vectorResource` reads groups, paths, gradients and clip paths from an Android `<vector>` file. A vector is drawn as Compose draws it: into a bitmap of its size in pixels, kept, and that drawn; in its own colors with `Image`, tinted with `Icon` (one all in one color as an Alpha8 mask).

A vector's content can be composable too, as Compose's `rememberVectorPainter { Group { Path() } }`: what it reads makes it again, here a tick drawn in as it is switched on.

```kotlin body
#! val done = remember { mutableStateOf(false) }
val drawn by animateFloatAsState(if (done.value) 1f else 0f, tween(400))
val tick = rememberVectorPainter(24.dp, 24.dp, 24f, 24f) { _, _ ->
    Path(pathData = addPathNodes("M5,12l5,5l9,-10"), stroke = Brush.solid(Color.Black), strokeLineWidth = 2f, trimPathEnd = drawn)
}
Image(tick, contentDescription = "Done")
```

`vector.RenderVectorGroup(icon.root, configs)` draws an existing vector's tree that way, a `VectorConfig` of a group's or path's name giving the values it is drawn with (a turn, a color, a trim).

## See also

- [Phases](phases.md): drawing without recomposing.
- [Animation](animation.md): animating what you draw.
- [Resources](resources.md): where image and vector files go.
