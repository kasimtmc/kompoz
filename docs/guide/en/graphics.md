# Graphics

Drawing your own graphics with `Canvas` and the drawing modifiers, brushes and paths, clipping and shadows, `graphicsLayer`, and showing images and vectors.

## Canvas

`Canvas` is a composable that draws whatever you tell it in a `DrawScope`. Give it a size with its modifier:

```cpp
void Target() {
    Color ring = colorScheme().primary, center = colorScheme().error;
    Canvas(Modifier().size(160), [=](DrawScope& d) {
        d.drawCircle(ring.copy(0.2f));                              // fills the canvas
        d.drawCircle(ring, d.size().width / 3, std::nullopt, 1, Stroke(6));
        d.drawCircle(center, 12);                                   // at the center
    });
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

They take Compose's parameters, in Compose's order: a color or a brush, then `alpha`, `style`, `colorFilter` and `blendMode` (`d.drawRect(color, {}, size, 0.5f, Stroke(2))`). `DrawStyle` is `Fill` (the default) or `Stroke(width, miter, cap, join, pathEffect)`; a width of 0 is a hairline, one pixel wide whatever the transform. Coordinates are in dp from the canvas's top-left corner; `d.size()` and `d.center()` give its size and center, `d.density()` the pixels in a dp.

### Transforming

`translate`, `rotate` (and `rotateRad`), `scale`, `inset`, `clipRect` and `clipPath` take a block: what the block draws is moved, turned, scaled or clipped. `clipRect` and `clipPath` keep what is inside, or with `ClipOp::Difference` what is outside. `withTransform` does several at once, and its `transform(matrix)` takes any `Matrix` (Compose's 4x4, perspective too). As in Compose, the transform goes into the drawing itself: nothing is clipped to the drawing's area, and on the GPU renderer everything stays sharp and exactly anti-aliased however it is turned, stretched or seen in perspective (text is laid out at the size it is shown).

```cpp
void Clock(float minutes) {
    Color hand = colorScheme().onSurface;
    Canvas(Modifier().size(120), [=](DrawScope& d) {
        d.drawCircle(hand, std::nullopt, std::nullopt, 1, Stroke(2));
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

## Colors and color spaces

A `Color` is Compose's: 64 bits, in sRGB 8 bits a channel, or in another of Compose's color spaces (`ColorSpaces`: Display P3, BT.2020, Adobe RGB, CIE L\*a\*b\*, Oklab and more) its components as half floats. `convert` turns one into another space and `toArgb()` gives its sRGB as `0xAARRGGBB`; whatever space a color is in, it is drawn in sRGB.

```cpp body
Color p3(0.1f, 0.75f, 0.45f, 1, ColorSpaces::DisplayP3);  // in Display P3
Color lab(55, 60, -40, 1, ColorSpaces::CieLab);           // L*, a*, b*
Color between = lerp(colors::Red, colors::Blue, 0.5f);    // mixed in Oklab
Row(Modifier().height(40), {}, [&] {
    for (Color c : {p3, lab, between}) Box(Modifier().size(40).background(c));
});
```

`lerp` mixes two colors in Oklab, as Compose's does: halfway from red to blue is a violet as light as both, not the dark purple their sRGB numbers make. An animated color goes the same way (`animateColorAsState`). A gradient mixes its colors in sRGB, as Skia's.

## Brushes

A `Brush` paints with a solid color or a gradient, wherever a color would go: drawing calls, `background`, text.

```cpp body
Box(Modifier().fillMaxWidth().height(120).background(
    Brush::linearGradient({Color::hex(0x6750A4), Color::hex(0xB58392)}), RoundedCornerShape(16)));
```

`Brush::solid`, `linearGradient`, `horizontalGradient`, `verticalGradient`, `radialGradient` and `sweepGradient` are there, with color stops and tile modes.

A brush of your own is a shader made for the size it paints, as Compose's `ShaderBrush`: a gradient, an image tiled, or two shaders composited.

```cpp body
Box(Modifier().size(120, 72).background(Brush::shader([](Size size) {
    return CompositeShader(LinearGradientShader({0, 0}, {size.width, 0}, {Color::hex(0x6750A4), Color::hex(0xB58392)}),
                           RadialGradientShader({24, 20}, 48, {colors::White.copy(0.9f), colors::Transparent}), BlendMode::Screen);
})));
```

`Brush::composite` does the same with two brushes, `withTransform` moves what a gradient or a shader paints, and `lerp` goes from one brush to another as an animation does.

## Canvas and Paint

Under a `DrawScope` is Compose's `Canvas` (`graphics::Canvas` here, since the `Canvas` composable has the name), with its stack of saves and its `Paint`:

```cpp body
Canvas(Modifier().size(96), [](DrawScope& d) {
    d.drawIntoCanvas([&](graphics::Canvas& c) {
        Paint spoke{.color = Color::hex(0x6750A4), .style = PaintingStyle::Stroke, .strokeWidth = 4, .strokeCap = StrokeCap::Round};
        for (int i = 0; i < 12; ++i) {
            c.save();
            c.rotate(float(i) * 30, 48, 48);
            c.drawLine({48, 30}, {48, 8}, spoke);
            c.restore();
        }
    });
});
```

What the canvas leaves saved or transformed holds for the scope's later draws until the block it is in ends, as in Compose.

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

`moveTo`, `lineTo`, `quadraticTo`, `conicTo`, `cubicTo`, their `relative…` forms, `arcTo`, `addRect`, `addOval`, `addRoundRect` and `close` build it, as Compose's (an Android path) does: shapes added whole go counterclockwise unless you pass `Path::Direction::Clockwise`, and arcs and ovals are conics, exact. `transform(matrix)` moves it, `op` (or `a + b`, `a - b`, `a & b`) combines two, keeping their curves, `iterator()` walks its segments, `PathHitTester` tests points against it again and again. `PathMeasure` measures its length and gives segments of it (pieces of its curves, as curves), for drawing a path progressively, as Android's does.

A `PathEffect` on a stroke (or a `Paint`) changes the path as it is drawn, as Skia's: dashes, rounded corners, a shape stamped along it, or one after another (`chainPathEffect`).

```cpp body
Path wave;
wave.moveTo(0, 20);
wave.cubicTo(30, -10, 60, 50, 90, 20);
Canvas(Modifier().size(100, 40), [wave](DrawScope& d) {
    d.drawPath(wave, colorScheme().primary, 1, Stroke(3, 4, StrokeCap::Round, StrokeJoin::Round, PathEffect::dashPathEffect({8, 6})));
});
```

It is applied where the path is drawn, at the pixels it is drawn at, so a dash on a curve lands where Compose's does at any scale.

## Shapes and clipping

A `Shape` is the outline used by backgrounds, borders, clips and shadows: `RectangleShape`, `CircleShape`, `RoundedCornerShape(16)` (or one radius per corner), `CutCornerShape(8)`, and Material 3 Expressive's shapes (`MaterialShapes::Cookie9Sided()` and others, with `toShape`).

```cpp body
Box(Modifier().size(96).clip(CircleShape).background(colorScheme().tertiaryContainer));
Box(Modifier().size(96).border(2, colorScheme().outline, RoundedCornerShape(12, 12, 0, 0)));
```

As in Compose, a shape makes its outline in a size and layout direction (`createOutline`): a rect, a rounded rect (`Outline::Rounded(RoundRect{…})`, whose corners may be elliptical) or any path. `GenericShape` builds one from a path:

```cpp body
Shape ticket = GenericShape([](Path& p, Size size, LayoutDirection) {
    p.moveTo(0, 0);
    p.lineTo(size.width, 0);
    p.lineTo(size.width, size.height);
    p.arcTo({size.width / 2 - 12, size.height - 12, 24, 24}, 0, -180, false);  // a notch at the bottom
    p.lineTo(0, size.height);
    p.close();
});
Box(Modifier().size(120, 72).clip(ticket).background(colorScheme().primaryContainer));
```

`clip(shape)` clips the content to the shape, its edge anti-aliased, without a layer.

## Shadows

`shadow(elevation, shape)` raises a node: as Compose's, it is a `graphicsLayer` with that `shadowElevation`, its shape's shadow drawn as Android draws one, lit from above the top of the window (so shadows lengthen down the screen), and the content clipped to the shape (`clip`, by default when the elevation is above 0). `dropShadow(shape, Shadow{…})` and `innerShadow(shape, Shadow{…})` draw shadows with your own color (or `brush`), offset, blur radius, spread, alpha and blend mode:

```cpp body
Box(Modifier().size(120)
        .dropShadow(RoundedCornerShape(24), Shadow{.color = Color::hex(0x6750A4, 0.4f), .offset = {0, 8}, .blurRadius = 24})
        .background(colorScheme().surface, RoundedCornerShape(24)));
```

## graphicsLayer

`graphicsLayer` draws the content scaled, rotated (in 3D too: `rotationX`, `rotationY`), moved, faded, clipped, filtered, blended or blurred, without changing its layout. As in Compose it goes through an offscreen layer only where it must (faded, a blend mode, a color filter, a render effect, or `CompositingStrategy::Offscreen`); otherwise the transform and the clip go straight into the drawing:

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

`alpha(a)`, `scale(s)` and `rotate(degrees)` are shorthands for the common cases.

### Blur and render effects

`blur(radius)` blurs the content, as Compose's does. By default the blur stays within the node's bounds (`BlurredEdgeTreatment::Rectangle`: the pixels past the edges count as the edges'); `BlurredEdgeTreatment::Unbounded` lets it spread out past them, fading; `BlurredEdgeTreatment{shape}` cuts it to a shape:

```cpp body
Image(imageResource("images/cover.jpg"), "Album cover",
      Modifier().size(160).blur(12, BlurredEdgeTreatment{RoundedCornerShape(16)}), ContentScale::Crop);
```

It is a `graphicsLayer` with a render effect, and you can give one a render effect yourself. `BlurEffect(radiusX, radiusY, edgeTreatment)` blurs its pixels (a Gaussian, Android's for the radius; the `TileMode` says what lies past the edges); `OffsetEffect(x, y)` moves them. Each takes another to go through first:

```cpp body
Box(Modifier().size(96).graphicsLayer({.renderEffect = OffsetEffect(BlurEffect(6, 6, TileMode::Decal), {0, 4})})
        .background(colorScheme().primary, CircleShape));
```

What is drawn within the layer's bounds goes through the effect; with `clip` set, what the effect makes is then cut to the layer's shape.

`blur` also blurs progressively, as Compose 1.13's does: more in some places than others, from one radius to another (or through `BlurStop`s) down, across, along a line or out from a center. The form that takes a lambda sets it while drawing:

```cpp body
Image(imageResource("images/cover.jpg"), "Album cover",
      Modifier().size(160).blur(BlurRadiusSpec::verticalGradient(0, 16)), ContentScale::Crop);  // sharp at the top
Box(Modifier().size(120).blur([](BlurScope& b) {
    b.blurRadiusSpec = BlurRadiusSpec::radialGradient(0, 12);  // sharp in the middle
}).background(colorScheme().primary, CircleShape));
```

Kompoz's `blur` also takes what to blur (`BlurType`). `Family`, the default, blurs the node and its children, as Compose's does. `Morph` blurs what is drawn behind the node, within its shape, and draws the node over it sharp: frosted glass. `Self` blurs only what the node draws itself (its background, border, image) and draws its children over it sharp:

```cpp body
Box(Modifier().size(200, 72)
        .blur(16, BlurredEdgeTreatment{RoundedCornerShape(16)}, BlurType::Morph)
        .background(colors::White.copy(0.2f), RoundedCornerShape(16)),
    Alignment::Center, [] { Text("Frosted glass"); });
```

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

// Paths in paints of their own, in groups, as Compose's ImageVector.Builder makes them.
const ImageVector& badge() {
    static const ImageVector v =
        ImageVector::Builder("Badge", 24, 24, 24, 24)
            .addPath({.pathData = addPathNodes("M12,2a10,10 0 1,0 0.01,0z"), .fill = Brush::solid(Color::hex(0x1E88E5))})
            .group({.rotation = -10, .pivotX = 12, .pivotY = 12}, [](ImageVector::Builder& b) {
                b.path({.stroke = Brush::solid(colors::White), .strokeLineWidth = 2, .strokeLineCap = StrokeCap::Round},
                       [](PathBuilder& p) { p.moveTo(7, 12).lineTo(10.5f, 15.5f).lineTo(17, 9); });
            })
            .build();
    return v;
}

void Sparkle() {
    Icon(sparkle(), "");
    Image(rememberVectorPainter(badge()), "Badge");
    Image(rememberVectorPainter(vectorResource("vectors/logo.xml")), "Logo", Modifier().size(48));
}
```

`vectorResource` reads groups, paths, gradients and clip paths from an Android `<vector>` file. A vector is drawn as Compose draws it: into a bitmap of its size in pixels, kept, and that drawn; in its own colors with `Image`, tinted with `Icon` (one all in one color as an Alpha8 mask).

A vector's content can be composable too, as Compose's `rememberVectorPainter { Group { Path() } }`: what it reads makes it again, here a tick drawn in as it is switched on.

```cpp body
#! auto done = mutableStateOf(false);
float drawn = animateFloatAsState(done.get() ? 1.f : 0.f, tween(400));
Painter tick = rememberVectorPainter(24, 24, 24, 24, [&](float, float) {
    vector::Path({.pathData = addPathNodes("M5,12l5,5l9,-10"), .stroke = Brush::solid(colors::Black), .strokeLineWidth = 2,
                  .trimPathEnd = drawn});
});
Image(tick, "Done");
```

`vector::RenderVectorGroup(icon.root(), configs)` draws an existing vector's tree that way, a `VectorConfig` of a group's or path's name giving the values it is drawn with (a turn, a color, a trim).

## See also

- [Phases](phases.md): drawing without recomposing.
- [Animation](animation.md): animating what you draw.
- [Resources](resources.md): where image and vector files go.
