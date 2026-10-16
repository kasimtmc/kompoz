# Graphics

Drawing your own graphics with `Canvas` and the drawing modifiers, brushes and paths, clipping and shadows, `graphicsLayer`, and showing images and vectors.

## Canvas

`Canvas` is a composable that draws whatever you tell it in a `DrawScope`. Give it a size with its modifier:

```cpp
void Target() {
    Color ring = colorScheme().primary, center = colorScheme().error;
    Canvas(Modifier().size(160), [=](DrawScope& d) {
        d.drawCircle(ring.copy(0.2f));                              // fills the canvas
        d.drawCircle(ring, d.size().width / 3, std::nullopt, Stroke(6));
        d.drawCircle(center, 12);                                   // at the center
    });
}
```

The draw lambda runs while drawing, not while composing. Read the colors you need from the theme in composition, as above, and capture them by value.

### What a DrawScope can draw

| Call | Draws |
|---|---|
| `drawRect`, `drawRoundRect`, `drawCircle`, `drawOval`, `drawArc` | Shapes, filled or stroked, with a color or a brush. |
| `drawLine(color, start, end, width)` | A line; with a `StrokeCap` and a dash `PathEffect`. |
| `drawPoints(points, mode, color, width)` | Points, lines between pairs, or a polyline. |
| `drawPath(path, color or brush, style)` | Any path. |
| `drawText(layout, topLeft)` | Text measured with a text measurer (see [Text](text.md#measuring-text)). |
| `drawImage(image, topLeft, size)` | A bitmap. |
| `drawVector(vector, color, topLeft, size)` | An `ImageVector`, tinted. |

`DrawStyle` is `Fill` (the default) or `Stroke(width, cap, join)`. Coordinates are in dp from the canvas's top-left corner; `d.size()` and `d.center()` give its size and center.

### Transforming

`translate`, `rotate`, `scale`, `inset`, `clipRect` and `clipPath` take a block: what the block draws is moved, turned, scaled or clipped. `withTransform` does several at once. Rotating and scaling go through an offscreen layer of the drawing's area.

```cpp
void Clock(float minutes) {
    Color hand = colorScheme().onSurface;
    Canvas(Modifier().size(120), [=](DrawScope& d) {
        d.drawCircle(hand, std::nullopt, std::nullopt, Stroke(2));
        d.rotate(minutes * 6, [&](DrawScope& r) {          // 6° per minute, around the center
            r.drawLine(hand, r.center(), {r.center().x, 12}, 3, StrokeCap::Round);
        }, d.center());
    });
}
```

## Drawing modifiers

You don't need a `Canvas` to draw on an existing composable. These modifiers draw on its node:

- `drawBehind(onDraw)` draws behind the content.
- `drawWithContent(onDraw)` draws around it: call `drawContent()` where the content goes, and draw before or after it.
- `drawWithCache(build)` builds objects once per size (paths, brushes) and draws with them every frame.

```cpp
void Highlighted(const std::string& text) {
    Color marker = Color::hex(0xFFEB3B, 0.6f);
    Text(text, Modifier().drawBehind([marker](DrawScope& d) {
        d.drawRect(marker, {0, d.size().height * 0.55f}, Size{d.size().width, d.size().height * 0.4f});
    }));
}

void Faded(const Content& content) {
    Box(Modifier().drawWithContent([](ContentDrawScope& d) {
        d.drawContent();
        d.drawRect(Brush::verticalGradient({colors::Transparent, colors::White}));   // a fade over the bottom
    }), content);
}
```

## Brushes

A `Brush` paints with a solid color or a gradient, wherever a color would go: drawing calls, `background`, text.

```cpp body
Box(Modifier().fillMaxWidth().height(120).background(
    Brush::linearGradient({Color::hex(0x6750A4), Color::hex(0xB58392)}), RoundedCornerShape(16)));
```

`Brush::solid`, `linearGradient`, `horizontalGradient`, `verticalGradient`, `radialGradient` and `sweepGradient` are there, with color stops and tile modes.

## Paths

A `Path` describes any outline with lines, curves and arcs:

```cpp
Path heart(Size s) {
    Path p;
    float w = s.width, h = s.height;
    p.moveTo(w / 2, h * 0.9f);
    p.cubicTo(w * 0.1f, h * 0.6f, 0, h * 0.25f, w * 0.25f, h * 0.1f);
    p.cubicTo(w * 0.4f, 0, w / 2, h * 0.15f, w / 2, h * 0.25f);
    p.cubicTo(w / 2, h * 0.15f, w * 0.6f, 0, w * 0.75f, h * 0.1f);
    p.cubicTo(w, h * 0.25f, w * 0.9f, h * 0.6f, w / 2, h * 0.9f);
    p.close();
    return p;
}

void Heart() {
    Color red = Color::hex(0xE53935);
    Box(Modifier().size(64).drawWithCache([red](CacheDrawScope& cache) {
        Path path = heart(cache.size());   // built once per size
        return cache.onDrawBehind([path, red](DrawScope& d) { d.drawPath(path, red); });
    }));
}
```

`moveTo`, `lineTo`, `quadraticTo`, `cubicTo`, their `relative…` forms, `arcTo`, `addRect`, `addOval`, `addRoundRect` and `close` build it. `PathMeasure` measures its length and gives segments of it, for drawing a path progressively.

## Shapes and clipping

A `Shape` is the outline used by backgrounds, borders, clips and shadows: `RectangleShape`, `CircleShape`, `RoundedCornerShape(16)` (or one radius per corner), `CutCornerShape(8)`, and Material 3 Expressive's shapes (`MaterialShapes::Cookie9Sided()` and others, with `toShape`).

```cpp body
Box(Modifier().size(96).clip(CircleShape).background(colorScheme().tertiaryContainer));
Box(Modifier().size(96).border(2, colorScheme().outline, RoundedCornerShape(12, 12, 0, 0)));
```

`clip(shape)` clips the content to the shape; the content is drawn through a layer so the edge is smooth.

## Shadows

`shadow(elevation, shape)` draws a Material-style shadow under a node. `dropShadow(shape, Shadow{…})` and `innerShadow(shape, Shadow{…})` draw shadows with your own color, offset, blur radius and spread:

```cpp body
Box(Modifier().size(120)
        .dropShadow(RoundedCornerShape(24), Shadow{.color = Color::hex(0x6750A4, 0.4f), .offset = {0, 8}, .blurRadius = 24})
        .background(colorScheme().surface, RoundedCornerShape(24)));
```

## graphicsLayer

`graphicsLayer` draws the content scaled, rotated, moved or faded, through an offscreen layer, without changing its layout:

```cpp body
Box(Modifier().size(80).graphicsLayer({.scaleX = 1.2f, .scaleY = 1.2f, .alpha = 0.8f, .rotationZ = 15})
        .background(colorScheme().primary, RoundedCornerShape(16)));
```

The form that takes a lambda is evaluated while drawing, so animated values read there change without recomposing or laying out again. Use it for anything that changes every frame (see [Phases](phases.md#deferring-state-reads)):

```cpp
void Spinner() {
    auto angle = rememberInfiniteTransition().animateFloatState(0, 360, infiniteRepeatable(tween(1200, 0, LinearEasing)));
    Icon(Icons::Filled::Refresh, "Loading", Modifier().graphicsLayer([angle](GraphicsLayerScope& l) { l.rotationZ = angle.get(); }));
}
```

`alpha(a)`, `scale(s)` and `rotate(degrees)` are shorthands for the common cases, and `blur(radius)` blurs the content.

## Images

`Image` shows a bitmap; `imageResource(path)` loads one from the app's assets, once:

```cpp body
Image(imageResource("images/cover.jpg"), "Album cover", Modifier().size(160).clip(RoundedCornerShape(12)), ContentScale::Crop);
```

`AsyncImage` loads an image from the assets or from the web (`http://`, `https://`), decodes it at the size it is shown at, and keeps downloads on disk. It shows a placeholder while loading and something else if it fails:

```cpp body
AsyncImage("https://example.com/photo.webp", "A photo", Modifier().size(200),
           {.placeholder = [] { CircularProgressIndicator(); },
            .error = [] { Icon(Icons::Filled::Info, "Couldn't load"); },
            .contentScale = ContentScale::Crop,
            .crossfadeMillis = 200});
```

JPEG, PNG and WebP are supported. `ContentScale` fits the image into its box as Compose's does: `Fit`, `Crop`, `FillBounds`, `FillWidth`, `FillHeight`, `Inside`, `None`.

## Vectors and icons

`Icon` draws an `ImageVector` tinted with the content color, at 24 dp by default. Kompoz has Material icons under `Icons::Filled` and `Icons::AutoMirrored::Filled` (the latter mirror in right-to-left layouts):

```cpp body
Icon(Icons::Filled::Favorite, "Favorite", Modifier(), Color::hex(0xE53935));
```

Your own vectors come from SVG path data, from Android vector drawable files, or built in code:

```cpp
const ImageVector& sparkle() {
    static const ImageVector v = ImageVector::fromSvgPath("M12,2l2.4,7.6L22,12l-7.6,2.4L12,22l-2.4-7.6L2,12l7.6-2.4z");
    return v;
}

void Sparkle() {
    Icon(sparkle(), "");
    Image(rememberVectorPainter(vectorResource("vectors/logo.xml")), "Logo", Modifier().size(48));
}
```

`vectorResource` reads groups, paths, gradients and clip paths from an Android `<vector>` file, in its own colors when drawn with `Image`, or tinted with `Icon`.

## See also

- [Phases](phases.md): drawing without recomposing.
- [Animation](animation.md): animating what you draw.
- [Resources](resources.md): where image and vector files go.
