// Components screen: drawing on a Canvas.

#include "showcase.hpp"

// Canvas, drawBehind and drawWithCache, Brush and Path, graphicsLayer, pointerInput gestures,
// combinedClickable, aspectRatio and scaleIn.
void DrawingSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto spin = remember([] { return Animatable<float>(0.f); });
        auto spinning = remember([] { return mutableStateOf(true); });
        auto inView = remember([] { return mutableStateOf(false); });  // (out of view it stops: frames for nobody to see)
        LaunchedEffect(spinning.get() && inView.get(), [spin, on = spinning.peek() && inView.peek()]() -> Task {
            while (on) {  // a turn every 2.4 s, on from where it stopped
                co_await spin.animateTo(360.f, tween(int(2400 * (1 - spin.value() / 360)), 0, LinearEasing));
                spin.snapTo(0.f);
            }
        });
        auto starShown = remember([] { return mutableStateOf(false); });
        auto ball = remember([] { return mutableStateOf(Offset{24, 24}); });
        auto padWidth = remember([] { return std::make_shared<float>(300.f); });
        auto gesture = remember([] { return mutableStateOf(std::string("Dokun, çift dokun ya da basılı tut")); });
        auto touch = remember([] { return mutableStateOf(Offset{-100, -100}); });
        auto clicks = remember([] { return mutableStateOf(std::array<int, 3>{}); });

        Section("Çizim ve hareket", [&] {
            Row(Modifier().fillMaxWidth(),
                {.horizontalArrangement = Arrangement::spacedBy(16), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    // A donut chart: pie slices (filled arcs) with a hole on top.
                    Canvas(Modifier().size(96), [hole = cs.surfaceContainer, primary = cs.primary](DrawScope& d) {
                        const float parts[] = {0.45f, 0.30f, 0.25f};
                        const Color colors[] = {primary, Color::hex(0x7D5260), Color::hex(0x625B71)};
                        float start = -90;
                        for (int i = 0; i < 3; ++i) {
                            d.drawArc(colors[i], start, parts[i] * 360 - 3, true);
                            start += parts[i] * 360;
                        }
                        d.drawCircle(hole, d.size().width * 0.28f);
                    });
                    // A sine wave in a 16:9 box: a stroked Path over a gradient-filled area.
                    Canvas(Modifier().weight(1).aspectRatio(16.f / 9).background(cs.surfaceContainerHigh, 12),
                           [line = cs.primary, axis = cs.outlineVariant](DrawScope& d) {
                               Size s = d.size();
                               d.drawLine(axis, {8, s.height / 2}, {s.width - 8, s.height / 2}, 1);
                               Path curve;
                               for (int i = 0; i <= 64; ++i) {
                                   float t = float(i) / 64;
                                   float x = 8 + t * (s.width - 16);
                                   float y = s.height / 2 - std::sin(t * 2 * std::numbers::pi_v<float> * 1.5f) * s.height * 0.35f;
                                   if (i == 0)
                                       curve.moveTo(x, y);
                                   else
                                       curve.lineTo(x, y);
                               }
                               Path area = curve;
                               area.lineTo(s.width - 8, s.height);
                               area.lineTo(8, s.height);
                               area.close();
                               d.drawPath(area, Brush::verticalGradient({line.copy(0.35f), line.copy(0)}));
                               d.drawPath(curve, line, 1, Stroke(3, 4, StrokeCap::Round, StrokeJoin::Round));
                               d.drawText("sin(x)", {10, 6}, Font{11}, axis);
                           });
                });
            // PathEffect (dashes, rounded corners), drawPoints, and a wave drawn bit by bit through
            // PathMeasure (getSegment up to a share of its length, getPosition for its tip).
            // (The phases are read while drawing: the Canvas is drawn again, nothing composed again.)
            auto tracing = rememberInfiniteTransition().animateFloatState(0, 1, infiniteRepeatable(tween(2400, 0, LinearEasing), RepeatMode::Reverse));
            Canvas(Modifier().fillMaxWidth().height(84).background(cs.surfaceContainerHigh, 12),
                   [c = cs.primary, o = cs.outline, t = cs.tertiary, tracing](DrawScope& d) {
                       Size s = d.size();
                       float traced = tracing.get();
                       d.drawLine(o, {12, 10}, {s.width - 12, 10}, 2, StrokeCap::Round, PathEffect::dashPathEffect({8, 6}));
                       d.drawCircle(c, 22, Offset{40, 50}, 1, Stroke(3, 4, StrokeCap::Round, StrokeJoin::Round, PathEffect::dashPathEffect({7, 5})));
                       Path triangle;
                       triangle.moveTo(100, 72);
                       triangle.lineTo(124, 26);
                       triangle.lineTo(148, 72);
                       triangle.close();
                       d.drawPath(triangle, t, 1, Stroke(3, 4, StrokeCap::Butt, StrokeJoin::Miter, PathEffect::cornerPathEffect(8)));
                       d.drawPoints({{166, 30}, {178, 40}, {166, 50}, {178, 60}, {166, 70}}, PointMode::Points, o, 6, StrokeCap::Round);
                       Path wave;
                       for (int i = 0; i <= 48; ++i) {
                           float x = 196 + float(i) / 48 * std::max(0.f, s.width - 212);
                           float y = 50 - std::sin(float(i) / 48 * 4 * std::numbers::pi_v<float>) * 18;
                           i == 0 ? wave.moveTo(x, y) : wave.lineTo(x, y);
                       }
                       PathMeasure measure(wave);
                       Path part;
                       if (measure.getSegment(0, measure.length() * traced, part))
                           d.drawPath(part, c, 1, Stroke(3, 4, StrokeCap::Round, StrokeJoin::Round));
                       d.drawCircle(c, 5, measure.getPosition(measure.length() * traced));
                   });
            // stampedPathEffect: arrows turned along a wave (Rotate), and a short bar bent round a
            // circle (Morph) sliding with the phase.
            auto sliding = rememberInfiniteTransition().animateFloatState(0, 24, infiniteRepeatable(tween(1200, 0, LinearEasing)));
            Canvas(Modifier().fillMaxWidth().height(84).background(cs.surfaceContainerHigh, 12),
                   [c = cs.primary, t = cs.tertiary, sliding](DrawScope& d) {
                       Size s = d.size();
                       float slide = sliding.get();
                       Path arrow;  // pointing along +x, its tip at the origin
                       arrow.moveTo(0, 0);
                       arrow.lineTo(-8, -5);
                       arrow.lineTo(-6, 0);
                       arrow.lineTo(-8, 5);
                       arrow.close();
                       Path wave;
                       for (int i = 0; i <= 48; ++i) {
                           float x = 16 + float(i) / 48 * std::max(0.f, s.width - 120);
                           float y = 42 - std::sin(float(i) / 48 * 3 * std::numbers::pi_v<float>) * 22;
                           i == 0 ? wave.moveTo(x, y) : wave.lineTo(x, y);
                       }
                       d.drawPath(wave, c, 1, Stroke(1, 4, StrokeCap::Butt, StrokeJoin::Miter,
                                                  PathEffect::stampedPathEffect(arrow, 24, slide, StampedPathEffectStyle::Rotate)));
                       Path ring, bar;
                       ring.addOval({s.width - 92, 8, 68, 68});
                       bar.addRect({0, -3, 18, 6});
                       d.drawPath(ring, t, 1, Stroke(1, 4, StrokeCap::Butt, StrokeJoin::Miter,
                                                  PathEffect::stampedPathEffect(bar, 26.7f, slide, StampedPathEffectStyle::Morph)));
                   });
            // Path.op: two circles combined four ways (union, intersect, difference, xor), worked out
            // once for its size (drawWithCache), not every frame.
            Box(Modifier().fillMaxWidth().height(96).background(cs.surfaceContainerHigh, 12).drawWithCache(
                [c = cs.primary, o = cs.onSurfaceVariant](CacheDrawScope& cache) {
                    float cell = cache.size().width / 4;
                    const PathOperation ops[] = {PathOperation::Union, PathOperation::Intersect, PathOperation::Difference, PathOperation::Xor};
                    std::vector<Path> combined;
                    for (int i = 0; i < 4; ++i) {
                        float x = cell * float(i) + cell / 2;
                        Path left, right;
                        left.addOval({x - 30, 14, 40, 40});
                        right.addOval({x - 10, 14, 40, 40});
                        combined.push_back(Path::combine(ops[i], left, right));
                    }
                    return cache.onDrawBehind([combined, cell, c, o](DrawScope& d) {
                        const char* names[] = {"Union", "Intersect", "Difference", "Xor"};
                        for (int i = 0; i < 4; ++i) {
                            d.drawPath(combined[size_t(i)], c);
                            d.drawText(names[i], {cell * float(i) + 8, 72}, Font{11}, o);
                        }
                    });
                }));
            // Path.addArc slices of a pie, drawOval, a gradient clipped to a star (clipPath: any path,
            // its edge smooth) and withTransform (moved, then turned).
            Canvas(Modifier().fillMaxWidth().height(96).background(cs.surfaceContainerHigh, 12),
                   [c = cs.primary, t = cs.tertiary, s2 = cs.secondary](DrawScope& d) {
                       float shares[] = {0.45f, 0.3f, 0.25f};
                       Color fills[] = {c, t, s2};
                       float angle = -90;
                       for (int i = 0; i < 3; ++i) {
                           Path slice;
                           slice.moveTo(48, 48);
                           slice.arcTo({8, 8, 80, 80}, angle, shares[i] * 360, false);
                           slice.close();
                           d.drawPath(slice, fills[i]);
                           angle += shares[i] * 360;
                       }
                       d.drawOval(s2.copy(0.5f), {100, 20}, Size{70, 56});
                       Path star;
                       for (int k = 0; k < 10; ++k) {
                           float a = -1.5708f + float(k) * 0.6283f, r = k % 2 ? 16.f : 38.f;
                           float x = 230 + r * std::cos(a), y = 48 + r * std::sin(a);
                           k ? star.lineTo(x, y) : star.moveTo(x, y);
                       }
                       star.close();
                       d.clipPath(star, [&](DrawScope& in) {
                           in.drawRect(Brush::linearGradient({Color::hex(0xFF7043), Color::hex(0xFFEE58), Color::hex(0x66BB6A)}, {190, 10}, {270, 86}));
                       });
                       d.withTransform([](DrawTransform& tr) {
                           tr.translate(285, 48);
                           tr.rotate(30, Offset{0, 0});
                       }, [&](DrawScope& in) { in.drawRoundRect(t, {-20, -20}, Size{40, 40}, CornerRadius(6)); });
                   });
            // Brushes: sweep, radial and linear gradients; a star Path built once per size.
            Row(Modifier().fillMaxWidth(),
                {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    Box(Modifier().size(56).background(
                        Brush::sweepGradient({Color::hex(0xF44336), Color::hex(0xFFEB3B), Color::hex(0x4CAF50),
                                              Color::hex(0x00BCD4), Color::hex(0x3F51B5), Color::hex(0xE91E63),
                                              Color::hex(0xF44336)}),
                        CircleShape));
                    Box(Modifier().size(56).background(
                        Brush::radialGradient({Color::hex(0xFFF59D), Color::hex(0xFFB300), Color::hex(0xFFB300, 0)}),
                        CircleShape));
                    Box(Modifier().size(56).drawWithCache([](CacheDrawScope& cache) {
                        Size s = cache.size();
                        Path star;
                        for (int i = 0; i < 10; ++i) {
                            float a = (-90 + 36.f * float(i)) * std::numbers::pi_v<float> / 180;
                            float r = (i % 2 == 0 ? 0.5f : 0.22f) * std::min(s.width, s.height);
                            float x = s.width / 2 + std::cos(a) * r, y = s.height / 2 + 2 + std::sin(a) * r;
                            if (i == 0)
                                star.moveTo(x, y);
                            else
                                star.lineTo(x, y);
                        }
                        star.close();
                        Brush fill = Brush::linearGradient({Color::hex(0xFFD54F), Color::hex(0xF4511E)});
                        return cache.onDrawBehind([star, fill](DrawScope& d) {
                            d.drawPath(star, fill);
                            d.drawPath(star, Color::hex(0xBF360C), 1, Stroke(2, 4, StrokeCap::Round, StrokeJoin::Round));
                        });
                    }));
                    Box(Modifier().weight(1).height(56).background(
                            Brush::horizontalGradient({Color::hex(0x6750A4), Color::hex(0x7D5260)}), RoundedCornerShape(16)),
                        Alignment::Center,
                        [] { Text("Brush · Path", {.font = typography().titleMedium, .color = colors::White}); });
                });
            Row(Modifier().fillMaxWidth(),
                {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    // Rotated while drawing: the angle never recomposes this section.
                    Icon(Icons::Filled::Settings, "Dönen çark",
                         Modifier()
                             .size(40)
                             .onVisibilityChanged([inView](bool visible) { inView.set(visible); }, {.minFractionVisible = 0})
                             .graphicsLayer([spin](GraphicsLayerScope& l) { l.rotationZ = spin.value(); }),
                         cs.primary);
                    OutlinedButton([spinning] { spinning.set(!spinning.get()); }, Modifier(), {},
                                   [&] { Text(spinning.get() ? "Durdur" : "Döndür"); });
                    Button([starShown] { starShown.set(!starShown.get()); }, [] { Text("scaleIn"); });
                    Box(Modifier().size(40), Alignment::Center, [&] {
                        AnimatedVisibility(starShown.get(), Modifier(), scaleIn() + fadeIn(), scaleOut() + fadeOut(),
                                           [&] { Icon(Icons::Filled::Star, "Yıldız", Modifier().size(40), cs.error); });
                    });
                });
            // Corners of their own (topStart, topEnd, bottomEnd, bottomStart): mirrored right to left.
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(12)}, [&] {
                struct Look {
                    const char* label;
                    Shape shape;
                };
                for (const Look& look : {Look{"Sekme", RoundedCornerShape(16, 16, 0, 0)}, Look{"Balon", RoundedCornerShape(18, 18, 18, 4)},
                                         Look{"Yaprak", RoundedCornerShape(24, 0, 24, 0)},
                                         Look{"Hap ucu", RoundedCornerShape(CornerPercent(50), {6}, {6}, CornerPercent(50))}})
                    Box(Modifier().weight(1).height(48).background(cs.tertiaryContainer, look.shape).border(1, cs.tertiary, look.shape),
                        Alignment::Center, [&] { Text(look.label, {.font = typography().labelLarge, .color = cs.onTertiaryContainer}); });
            });
            // Drag the ball: Modifier.draggable2D (from the press: startDragImmediately) moving an
            // offset { } (layout only, no recomposition).
            auto ballDrag = rememberDraggable2DState([ball, padWidth](Offset amount) {
                ball.update([&](Offset& o) {
                    o.x = std::clamp(o.x + amount.x, 0.f, std::max(0.f, *padWidth - 56));
                    o.y = std::clamp(o.y + amount.y, 0.f, 160.f - 56);
                });
            });
            Box(Modifier()
                    .fillMaxWidth()
                    .height(160)
                    .clip(RoundedCornerShape(16))
                    .background(cs.surfaceContainerHigh)
                    .pointerHoverIcon(Cursor::Move)  // the desktop's move pointer over the pad
                    .drawBehind([padWidth, dot = cs.outlineVariant](DrawScope& d) {
                        *padWidth = d.size().width;
                        for (float y = 12; y < d.size().height; y += 24)
                            for (float x = 12; x < d.size().width; x += 24) d.drawCircle(dot, 1.5f, Offset{x, y});
                    }),
                [&] {
                    Text("Sürükle", Modifier().align(Alignment::TopEnd).padding(12),
                         {.fontSize = 12, .color = cs.onSurfaceVariant});
                    Box(Modifier()
                            .offset([ball] { return ball.get(); })
                            .size(56)
                            .shadow(4, 28)
                            .background(cs.primary, CircleShape)
                            .draggable2D(ballDrag, {.startDragImmediately = true}),
                        Alignment::Center, [&] { Icon(Icons::Filled::Place, "Top", {}, cs.onPrimary); });
                    // detectDragGesturesAfterLongPress: the card moves only once held (and grows while it is).
                    auto card = remember([] { return mutableStateOf(Offset{160, 90}); });
                    auto held = remember([] { return mutableStateOf(false); });
                    Box(Modifier()
                            .offset([card] { return card.get(); })
                            .size(88, 56)
                            .graphicsLayer([held](GraphicsLayerScope& l) { l.scaleX = l.scaleY = held.get() ? 1.1f : 1.f; })
                            .shadow(held.get() ? 8.f : 2.f, 12)
                            .background(cs.tertiaryContainer, 12)
                            .pointerInput(0, [card, held, padWidth](PointerInputScope& scope) -> Task {
                                co_await detectDragGesturesAfterLongPress(scope, {
                                    .onDragStart = [held](Offset) { held.set(true); },
                                    .onDrag = [card, padWidth](PointerInputChange&, Offset amount) {
                                        card.update([&](Offset& o) {
                                            o.x = std::clamp(o.x + amount.x, 0.f, std::max(0.f, *padWidth - 88));
                                            o.y = std::clamp(o.y + amount.y, 0.f, 160.f - 56);
                                        });
                                    },
                                    .onDragEnd = [held] { held.set(false); },
                                    .onDragCancel = [held] { held.set(false); },
                                });
                            }),
                        Alignment::Center, [&] { Text("Uzun bas", {.font = typography().labelMedium, .color = cs.onTertiaryContainer}); });
                });
            // Modifier.scrollable2D: a 900 x 600 grid panned both ways at once, flung and wheeled.
            auto pan = remember([] { return mutableStateOf(Offset{}); });
            auto panState = rememberScrollable2DState([pan, padWidth](Offset d) {
                Offset p = pan.peek();
                Offset to{std::clamp(p.x + d.x, std::min(0.f, *padWidth - 900.f), 0.f), std::clamp(p.y + d.y, 160.f - 600.f, 0.f)};
                pan.set(to);
                return Offset{to.x - p.x, to.y - p.y};
            });
            Box(Modifier()
                    .fillMaxWidth()
                    .height(160)
                    .clip(RoundedCornerShape(16))
                    .background(cs.surfaceContainerHigh)
                    .scrollable2D(panState)
                    .drawBehind([pan](DrawScope& d) {
                        Offset p = pan.get();
                        for (int row = 0; row < 10; ++row)
                            for (int col = 0; col < 15; ++col) {
                                Offset at{p.x + float(col) * 60 + 4, p.y + float(row) * 60 + 4};
                                if (at.x > d.size().width || at.y > d.size().height || at.x < -60 || at.y < -60) continue;
                                d.drawRoundRect(hueColor(row * 15 + col), at, Size{52, 52}, CornerRadius(10));
                            }
                    }),
                Alignment::TopEnd, [&] {
                    Text("scrollable2D: iki yöne kaydır", Modifier().padding(8).background(cs.surface.copy(0.8f), 8).padding(6, 2),
                         {.fontSize = 12, .color = cs.onSurface});
                });
            // rememberGraphicsLayer: the card recorded once a frame, drawn as it is and again upside
            // down, faded (a reflection); "Yakala" turns the recording into pixels (toImageBitmap).
            auto layer = rememberGraphicsLayer();
            auto shot = remember([] { return mutableStateOf(ImageBitmap{}); });
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    Box(Modifier().size(150, 110).drawWithContent([layer](ContentDrawScope& d) {
                            d.record(layer, [&](DrawScope&) { d.drawContent(); }, Size{150, 55});
                            d.drawLayer(layer);
                            GraphicsLayer::Properties& p = layer.properties();
                            p.topLeft = {0, 55};
                            p.scaleY = -1;
                            p.alpha = 0.35f;
                            d.drawLayer(layer);
                            p = {};
                        }),
                        [&] {
                            Box(Modifier().size(150, 55).background(Brush::horizontalGradient(std::vector<Color>{cs.primary, cs.tertiary}), RoundedCornerShape(12)),
                                Alignment::Center, [&] { Text("GraphicsLayer", {.font = typography().titleMedium, .color = cs.onPrimary}); });
                        });
                    Column(Modifier(), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
                        FilledTonalButton([layer, shot] { shot.set(layer.toImageBitmap()); }, [] { Text("Yakala"); });
                        if (!shot.get().empty())
                            Image(shot.get(), "Yakalanan kart", Modifier().size(120, 44).border(1, cs.outline), ContentScale::Fit);
                    });
                });
            // Taps: detectTapGestures with the press point drawn by drawBehind.
            Box(Modifier()
                    .fillMaxWidth()
                    .height(96)
                    .background(cs.secondaryContainer, 16)
                    .drawBehind([touch, c = cs.onSecondaryContainer](DrawScope& d) {
                        Offset p = touch.get();
                        d.drawCircle(c.copy(0.12f), 28.f, p);
                        d.drawCircle(c.copy(0.6f), 5.f, p);
                    })
                    .pointerInput(0,
                                  [gesture, touch](PointerInputScope& scope) -> Task {
                                      co_await detectTapGestures(
                                          scope, {
                                                     .onPress = [touch](Offset p) { touch.set(p); },
                                                     .onTap = [gesture](Offset p) {
                                                         gesture.set("Dokunuldu (" + std::to_string(int(p.x)) + ", " +
                                                                     std::to_string(int(p.y)) + ")");
                                                     },
                                                     .onDoubleTap = [gesture](Offset) { gesture.set("Çift dokunuldu"); },
                                                     .onLongPress = [gesture](Offset) { gesture.set("Uzun basıldı"); },
                                                 });
                                  }),
                Alignment::Center,
                [&] { Text(gesture.get(), {.font = typography().bodyLarge, .color = cs.onSecondaryContainer}); });
            // combinedClickable: click, double click and long click on one surface.
            std::array<int, 3> n = clicks.get();
            Row(Modifier()
                    .fillMaxWidth()
                    .border(1, cs.outlineVariant, 12)
                    .combinedClickable([clicks] { clicks.update([](auto& c) { ++c[0]; }); },
                                       [clicks] { clicks.update([](auto& c) { ++c[1]; }); },
                                       [clicks] { clicks.update([](auto& c) { ++c[2]; }); })
                    .padding(16),
                {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    Icon(Icons::Filled::Info, "combinedClickable", {}, cs.primary);
                    Column(Modifier().weight(1), {.verticalArrangement = Arrangement::spacedBy(2)}, [&] {
                        Text("combinedClickable", {.font = typography().titleMedium});
                        Text("Tık: " + std::to_string(n[0]) + " · Uzun: " + std::to_string(n[1]) +
                                 " · Çift: " + std::to_string(n[2]),
                             {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                    });
                });
        });
    });
}
