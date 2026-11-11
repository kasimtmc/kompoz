// Components screen: drawing on a Canvas.

#include "showcase.hpp"

// A shape's corners and the name it goes by.
data class Look(val label: String, val shape: Shape)

// Canvas, drawBehind and drawWithCache, Brush and Path, graphicsLayer, pointerInput gestures,
// combinedClickable, aspectRatio and scaleIn.
@Kompozable
fun DrawingSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val measurer = rememberTextMeasurer()
    val spin = remember { Animatable(0f) }
    var spinning by remember { mutableStateOf(true) }
    var inView by remember { mutableStateOf(false) }  // (out of view it stops: frames for nobody to see)
    val on = spinning && inView
    LaunchedEffect(on) {
        while (on) {  // a turn every 2.4 s, on from where it stopped
            spin.animateTo(360f, tween(durationMillis = (2400 * (1 - spin.value / 360)).toInt(), easing = LinearEasing))
            spin.snapTo(0f)
        }
    }
    var starShown by remember { mutableStateOf(false) }
    var ball by remember { mutableStateOf(Offset(24f, 24f)) }
    var padWidth by remember { mutableStateOf(300f) }
    var gesture by remember { mutableStateOf("Dokun, çift dokun ya da basılı tut") }
    var touch by remember { mutableStateOf(Offset(-100f, -100f)) }
    var taps by remember { mutableStateOf(0) }
    var longPresses by remember { mutableStateOf(0) }
    var doubleTaps by remember { mutableStateOf(0) }

    Section("Çizim ve hareket") {
        Row(modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
            // A donut chart: pie slices (filled arcs) with a hole on top.
            Canvas(modifier = Modifier.size(96.dp)) {
                val parts = floatArrayOf(0.45f, 0.30f, 0.25f)
                val colors = listOf(cs.primary, Color(0xFF7D5260), Color(0xFF625B71))
                var start = -90f
                for (i in 0 until 3) {
                    drawArc(colors[i], start, parts[i] * 360 - 3, true)
                    start += parts[i] * 360
                }
                drawCircle(cs.surfaceContainer, size.width * 0.28f)
            }
            // A sine wave in a 16:9 box: a stroked Path over a gradient-filled area.
            Canvas(modifier = Modifier.weight(1f).aspectRatio(16f / 9).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp))) {
                val s = size
                val line = cs.primary
                val axis = cs.outlineVariant
                drawLine(axis, Offset(8f, s.height / 2), Offset(s.width - 8, s.height / 2), strokeWidth = 1f)
                val curve = Path()
                for (i in 0..64) {
                    val t = i / 64f
                    val x = 8 + t * (s.width - 16)
                    val y = s.height / 2 - sin(t * 2 * PI.toFloat() * 1.5f) * s.height * 0.35f
                    if (i == 0) curve.moveTo(x, y) else curve.lineTo(x, y)
                }
                val area = Path()
                area.addPath(curve)
                area.lineTo(s.width - 8, s.height)
                area.lineTo(8f, s.height)
                area.close()
                drawPath(area, Brush.verticalGradient(listOf(line.copy(alpha = 0.35f), line.copy(alpha = 0f))))
                drawPath(curve, line, style = Stroke(width = 3f, cap = StrokeCap.Round, join = StrokeJoin.Round))
                drawText(measurer, "sin(x)", topLeft = Offset(10f, 6f), style = TextStyle(fontSize = 11.sp, color = axis))
            }
        }
        // PathEffect (dashes, rounded corners), drawPoints, and a wave drawn bit by bit through
        // PathMeasure (getSegment up to a share of its length, getPosition for its tip).
        // (The phases are read while drawing: the Canvas is drawn again, nothing composed again.)
        val tracing by rememberInfiniteTransition().animateFloat(
            initialValue = 0f, targetValue = 1f,
            animationSpec = infiniteRepeatable(tween(2400, easing = LinearEasing), RepeatMode.Reverse))
        Canvas(modifier = Modifier.fillMaxWidth().height(84.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp))) {
            val s = size
            val c = cs.primary
            val o = cs.outline
            val traced = tracing
            drawLine(o, Offset(12f, 10f), Offset(s.width - 12, 10f), strokeWidth = 2f, cap = StrokeCap.Round,
                     pathEffect = PathEffect.dashPathEffect(floatArrayOf(8f, 6f)))
            drawCircle(c, radius = 22f, center = Offset(40f, 50f),
                       style = Stroke(width = 3f, cap = StrokeCap.Round, join = StrokeJoin.Round, pathEffect = PathEffect.dashPathEffect(floatArrayOf(7f, 5f))))
            val triangle = Path()
            triangle.moveTo(100f, 72f)
            triangle.lineTo(124f, 26f)
            triangle.lineTo(148f, 72f)
            triangle.close()
            drawPath(triangle, cs.tertiary, style = Stroke(width = 3f, pathEffect = PathEffect.cornerPathEffect(8f)))
            drawPoints(listOf(Offset(166f, 30f), Offset(178f, 40f), Offset(166f, 50f), Offset(178f, 60f), Offset(166f, 70f)),
                       PointMode.Points, o, strokeWidth = 6f, cap = StrokeCap.Round)
            val wave = Path()
            for (i in 0..48) {
                val x = 196 + i / 48f * max(0f, s.width - 212)
                val y = 50 - sin(i / 48f * 4 * PI.toFloat()) * 18
                if (i == 0) wave.moveTo(x, y) else wave.lineTo(x, y)
            }
            val measure = PathMeasure()
            measure.setPath(wave, false)
            val part = Path()
            if (measure.getSegment(0f, measure.length * traced, part))
                drawPath(part, c, style = Stroke(width = 3f, cap = StrokeCap.Round, join = StrokeJoin.Round))
            drawCircle(c, 5f, measure.getPosition(measure.length * traced))
        }
        // stampedPathEffect: arrows turned along a wave (Rotate), and a short bar bent round a
        // circle (Morph) sliding with the phase.
        val sliding by rememberInfiniteTransition().animateFloat(
            initialValue = 0f, targetValue = 24f, animationSpec = infiniteRepeatable(tween(1200, easing = LinearEasing)))
        Canvas(modifier = Modifier.fillMaxWidth().height(84.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp))) {
            val s = size
            val slide = sliding
            val arrow = Path()  // pointing along +x, its tip at the origin
            arrow.moveTo(0f, 0f)
            arrow.lineTo(-8f, -5f)
            arrow.lineTo(-6f, 0f)
            arrow.lineTo(-8f, 5f)
            arrow.close()
            val wave = Path()
            for (i in 0..48) {
                val x = 16 + i / 48f * max(0f, s.width - 120)
                val y = 42 - sin(i / 48f * 3 * PI.toFloat()) * 22
                if (i == 0) wave.moveTo(x, y) else wave.lineTo(x, y)
            }
            drawPath(wave, cs.primary, style = Stroke(width = 1f, pathEffect = PathEffect.stampedPathEffect(arrow, 24f, slide, StampedPathEffectStyle.Rotate)))
            val ring = Path()
            ring.addOval(Rect(Offset(s.width - 92, 8f), Size(68f, 68f)))
            val bar = Path()
            bar.addRect(Rect(0f, -3f, 18f, 3f))
            drawPath(ring, cs.tertiary, style = Stroke(width = 1f, pathEffect = PathEffect.stampedPathEffect(bar, 26.7f, slide, StampedPathEffectStyle.Morph)))
        }
        // Path.op: two circles combined four ways (union, intersect, difference, xor), worked out
        // once for its size (drawWithCache), not every frame.
        Box(modifier = Modifier.fillMaxWidth().height(96.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).drawWithCache {
            val cell = size.width / 4
            val ops = listOf(PathOperation.Union, PathOperation.Intersect, PathOperation.Difference, PathOperation.Xor)
            val combined = mutableListOf<Path>()
            for (i in 0 until 4) {
                val x = cell * i + cell / 2
                val left = Path()
                left.addOval(Rect(Offset(x - 30, 14f), Size(40f, 40f)))
                val right = Path()
                right.addOval(Rect(Offset(x - 10, 14f), Size(40f, 40f)))
                combined.add(Path.combine(ops[i], left, right))
            }
            val names = listOf("Union", "Intersect", "Difference", "Xor")
            onDrawBehind {
                for (i in 0 until 4) {
                    drawPath(combined[i], cs.primary)
                    drawText(measurer, names[i], topLeft = Offset(cell * i + 8, 72f), style = TextStyle(fontSize = 11.sp, color = cs.onSurfaceVariant))
                }
            }
        })
        // Path.addArc slices of a pie, drawOval, a gradient clipped to a star (clipPath: any path,
        // its edge smooth) and withTransform (moved, then turned).
        Canvas(modifier = Modifier.fillMaxWidth().height(96.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp))) {
            val shares = floatArrayOf(0.45f, 0.3f, 0.25f)
            val fills = listOf(cs.primary, cs.tertiary, cs.secondary)
            var angle = -90f
            for (i in 0 until 3) {
                val slice = Path()
                slice.moveTo(48f, 48f)
                slice.arcTo(Rect(8f, 8f, 88f, 88f), angle, shares[i] * 360, false)
                slice.close()
                drawPath(slice, fills[i])
                angle += shares[i] * 360
            }
            drawOval(cs.secondary.copy(alpha = 0.5f), topLeft = Offset(100f, 20f), size = Size(70f, 56f))
            val star = Path()
            for (k in 0 until 10) {
                val a = -1.5708f + k * 0.6283f
                val r = if (k % 2 == 1) 16f else 38f
                val x = 230 + r * cos(a)
                val y = 48 + r * sin(a)
                if (k == 0) star.moveTo(x, y) else star.lineTo(x, y)
            }
            star.close()
            clipPath(star) {
                drawRect(Brush.linearGradient(listOf(Color(0xFFFF7043), Color(0xFFFFEE58), Color(0xFF66BB6A)), Offset(190f, 10f), Offset(270f, 86f)))
            }
            withTransform({
                translate(285f, 48f)
                rotate(30f, Offset(0f, 0f))
            }) { drawRoundRect(cs.tertiary, topLeft = Offset(-20f, -20f), size = Size(40f, 40f), cornerRadius = CornerRadius(6f)) }
        }
        // Brushes: sweep, radial and linear gradients; a star Path built once per size.
        Row(modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(modifier = Modifier.size(56.dp).background(
                Brush.sweepGradient(listOf(Color(0xFFF44336), Color(0xFFFFEB3B), Color(0xFF4CAF50),
                                           Color(0xFF00BCD4), Color(0xFF3F51B5), Color(0xFFE91E63),
                                           Color(0xFFF44336))),
                CircleShape))
            Box(modifier = Modifier.size(56.dp).background(
                Brush.radialGradient(listOf(Color(0xFFFFF59D), Color(0xFFFFB300), Color(0x00FFB300))),
                CircleShape))
            Box(modifier = Modifier.size(56.dp).drawWithCache {
                val s = size
                val star = Path()
                for (i in 0 until 10) {
                    val a = (-90 + 36f * i) * PI.toFloat() / 180
                    val r = (if (i % 2 == 0) 0.5f else 0.22f) * min(s.width, s.height)
                    val x = s.width / 2 + cos(a) * r
                    val y = s.height / 2 + 2 + sin(a) * r
                    if (i == 0) star.moveTo(x, y) else star.lineTo(x, y)
                }
                star.close()
                val fill = Brush.linearGradient(listOf(Color(0xFFFFD54F), Color(0xFFF4511E)))
                onDrawBehind {
                    drawPath(star, fill)
                    drawPath(star, Color(0xFFBF360C), style = Stroke(width = 2f, cap = StrokeCap.Round, join = StrokeJoin.Round))
                }
            })
            Box(modifier = Modifier.weight(1f).height(56.dp).background(
                    Brush.horizontalGradient(listOf(Color(0xFF6750A4), Color(0xFF7D5260))), RoundedCornerShape(16.dp)),
                contentAlignment = Alignment.Center) {
                Text("Brush · Path", color = Color.White, style = typography.titleMedium)
            }
        }
        Row(modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            // Rotated while drawing: the angle never recomposes this section.
            Icon(Icons.Filled.Settings, contentDescription = "Dönen çark",
                 modifier = Modifier
                     .size(40.dp)
                     .onVisibilityChanged(minFractionVisible = 0f) { inView = it }
                     .graphicsLayer { rotationZ = spin.value },
                 tint = cs.primary)
            OutlinedButton(onClick = { spinning = !spinning }) { Text(if (spinning) "Durdur" else "Döndür") }
            Button(onClick = { starShown = !starShown }) { Text("scaleIn") }
            Box(modifier = Modifier.size(40.dp), contentAlignment = Alignment.Center) {
                AnimatedVisibility(visible = starShown, enter = scaleIn() + fadeIn(), exit = scaleOut() + fadeOut()) {
                    Icon(Icons.Filled.Star, contentDescription = "Yıldız", modifier = Modifier.size(40.dp), tint = cs.error)
                }
            }
        }
        // Corners of their own (topStart, topEnd, bottomEnd, bottomStart): mirrored right to left.
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            for (look in listOf(Look("Sekme", RoundedCornerShape(16.dp, 16.dp, 0.dp, 0.dp)), Look("Balon", RoundedCornerShape(18.dp, 18.dp, 18.dp, 4.dp)),
                                Look("Yaprak", RoundedCornerShape(24.dp, 0.dp, 24.dp, 0.dp)),
                                Look("Hap ucu", RoundedCornerShape(CornerSize(50), CornerSize(6.dp), CornerSize(6.dp), CornerSize(50)))))
                Box(modifier = Modifier.weight(1f).height(48.dp).background(cs.tertiaryContainer, look.shape).border(1.dp, cs.tertiary, look.shape),
                    contentAlignment = Alignment.Center) {
                    Text(look.label, color = cs.onTertiaryContainer, style = typography.labelLarge)
                }
        }
        // Drag the ball: Modifier.draggable2D (from the press: startDragImmediately) moving an
        // offset { } (layout only, no recomposition).
        val ballDrag = rememberDraggable2DState { amount ->
            ball = Offset((ball.x + amount.x).coerceIn(0f, max(0f, padWidth - 56)), (ball.y + amount.y).coerceIn(0f, 160f - 56))
        }
        Box(modifier = Modifier
                .fillMaxWidth()
                .height(160.dp)
                .clip(RoundedCornerShape(16.dp))
                .background(cs.surfaceContainerHigh)
                .pointerHoverIcon(PointerIcon.Move)  // the desktop's move pointer over the pad
                .drawBehind {
                    padWidth = size.width
                    for (y in 12 until size.height.toInt() step 24)
                        for (x in 12 until size.width.toInt() step 24) drawCircle(cs.outlineVariant, 1.5f, Offset(x.toFloat(), y.toFloat()))
                }) {
            Text("Sürükle", modifier = Modifier.align(Alignment.TopEnd).padding(12.dp),
                 fontSize = 12.sp, color = cs.onSurfaceVariant)
            Box(modifier = Modifier
                    .offset { IntOffset(ball.x.roundToInt(), ball.y.roundToInt()) }
                    .size(56.dp)
                    .shadow(4.dp, CircleShape)
                    .background(cs.primary, CircleShape)
                    .draggable2D(ballDrag, startDragImmediately = true),
                contentAlignment = Alignment.Center) { Icon(Icons.Filled.Place, contentDescription = "Top", tint = cs.onPrimary) }
            // detectDragGesturesAfterLongPress: the card moves only once held (and grows while it is).
            var card by remember { mutableStateOf(Offset(160f, 90f)) }
            var held by remember { mutableStateOf(false) }
            Box(modifier = Modifier
                    .offset { IntOffset(card.x.roundToInt(), card.y.roundToInt()) }
                    .size(88.dp, 56.dp)
                    .graphicsLayer {
                        val grown = if (held) 1.1f else 1f
                        scaleX = grown
                        scaleY = grown
                    }
                    .shadow(if (held) 8.dp else 2.dp, RoundedCornerShape(12.dp))
                    .background(cs.tertiaryContainer, RoundedCornerShape(12.dp))
                    .pointerInput(Unit) {
                        detectDragGesturesAfterLongPress(
                            onDragStart = { held = true },
                            onDrag = { _, amount ->
                                card = Offset((card.x + amount.x).coerceIn(0f, max(0f, padWidth - 88)), (card.y + amount.y).coerceIn(0f, 160f - 56))
                            },
                            onDragEnd = { held = false },
                            onDragCancel = { held = false })
                    },
                contentAlignment = Alignment.Center) { Text("Uzun bas", color = cs.onTertiaryContainer, style = typography.labelMedium) }
        }
        // Modifier.scrollable2D: a 900 x 600 grid panned both ways at once, flung and wheeled.
        var pan by remember { mutableStateOf(Offset.Zero) }
        val panState = rememberScrollable2DState { d ->
            val p = pan
            val to = Offset((p.x + d.x).coerceIn(min(0f, padWidth - 900f), 0f), (p.y + d.y).coerceIn(160f - 600f, 0f))
            pan = to
            Offset(to.x - p.x, to.y - p.y)
        }
        Box(modifier = Modifier
                .fillMaxWidth()
                .height(160.dp)
                .clip(RoundedCornerShape(16.dp))
                .background(cs.surfaceContainerHigh)
                .scrollable2D(panState)
                .drawBehind {
                    val p = pan
                    for (row in 0 until 10)
                        for (col in 0 until 15) {
                            val at = Offset(p.x + col * 60 + 4, p.y + row * 60 + 4)
                            if (at.x > size.width || at.y > size.height || at.x < -60 || at.y < -60) continue
                            drawRoundRect(hueColor(row * 15 + col), topLeft = at, size = Size(52f, 52f), cornerRadius = CornerRadius(10f))
                        }
                },
            contentAlignment = Alignment.TopEnd) {
            Text("scrollable2D: iki yöne kaydır", modifier = Modifier.padding(8.dp).background(cs.surface.copy(alpha = 0.8f), RoundedCornerShape(8.dp)).padding(6.dp, 2.dp),
                 fontSize = 12.sp, color = cs.onSurface)
        }
        // rememberGraphicsLayer: the card recorded once a frame, drawn as it is and again upside
        // down, faded (a reflection); "Yakala" turns the recording into pixels (toImageBitmap).
        val layer = rememberGraphicsLayer()
        var shot by remember { mutableStateOf<ImageBitmap?>(null) }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(modifier = Modifier.size(150.dp, 110.dp).drawWithContent {
                record(layer, size = IntSize(150, 55)) { this@drawWithContent.drawContent() }
                drawLayer(layer)
                layer.topLeft = IntOffset(0, 55)
                layer.scaleY = -1f
                layer.alpha = 0.35f
                drawLayer(layer)
                layer.topLeft = IntOffset.Zero
                layer.scaleY = 1f
                layer.alpha = 1f
            }) {
                Box(modifier = Modifier.size(150.dp, 55.dp).background(Brush.horizontalGradient(listOf(cs.primary, cs.tertiary)), RoundedCornerShape(12.dp)),
                    contentAlignment = Alignment.Center) { Text("GraphicsLayer", color = cs.onPrimary, style = typography.titleMedium) }
            }
            Column(verticalArrangement = Arrangement.spacedBy(8.dp)) {
                FilledTonalButton(onClick = { shot = layer.toImageBitmap() }) { Text("Yakala") }
                shot?.let { Image(it, contentDescription = "Yakalanan kart", modifier = Modifier.size(120.dp, 44.dp).border(1.dp, cs.outline), contentScale = ContentScale.Fit) }
            }
        }
        // Taps: detectTapGestures with the press point drawn by drawBehind.
        Box(modifier = Modifier
                .fillMaxWidth()
                .height(96.dp)
                .background(cs.secondaryContainer, RoundedCornerShape(16.dp))
                .drawBehind {
                    val p = touch
                    drawCircle(cs.onSecondaryContainer.copy(alpha = 0.12f), 28f, p)
                    drawCircle(cs.onSecondaryContainer.copy(alpha = 0.6f), 5f, p)
                }
                .pointerInput(Unit) {
                    detectTapGestures(
                        onPress = { touch = it },
                        onTap = { p -> gesture = "Dokunuldu (${p.x.toInt()}, ${p.y.toInt()})" },
                        onDoubleTap = { gesture = "Çift dokunuldu" },
                        onLongPress = { gesture = "Uzun basıldı" })
                },
            contentAlignment = Alignment.Center) { Text(gesture, color = cs.onSecondaryContainer, style = typography.bodyLarge) }
        // combinedClickable: click, double click and long click on one surface.
        Row(modifier = Modifier
                .fillMaxWidth()
                .border(1.dp, cs.outlineVariant, RoundedCornerShape(12.dp))
                .combinedClickable(onClick = { taps++ }, onLongClick = { longPresses++ }, onDoubleClick = { doubleTaps++ })
                .padding(16.dp),
            horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Icon(Icons.Filled.Info, contentDescription = "combinedClickable", tint = cs.primary)
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(2.dp)) {
                Text("combinedClickable", style = typography.titleMedium)
                Text("Tık: $taps · Uzun: $longPresses · Çift: $doubleTaps", color = cs.onSurfaceVariant, style = typography.bodyMedium)
            }
        }
    }
}
