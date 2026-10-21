// Components screen: progress, animations, images, shapes, blend and tile modes, blur, color vectors, shared elements.

#include "showcase.hpp"

// Progress indicators, an Animatable driven by a coroutine, and a bouncy spring.
void ProgressSection(CallSite site) {
    composable(site, [=] {
        auto scope = rememberCoroutineScope();
        auto progress = remember([] { return Animatable<float>(0.f); });
        auto right = remember([] { return mutableStateOf(false); });
        float x = animateFloatAsState(right.get() ? 1.f : 0.f,
                                      spring(Spring::DampingRatioHighBouncy, Spring::StiffnessLow));
        Section("İlerleme ve animasyon", [&] {
            Row(Modifier().fillMaxWidth(),
                {.horizontalArrangement = Arrangement::spacedBy(16), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    CircularProgressIndicator();
                    CircularProgressIndicator(progress.value());
                    Column(Modifier().weight(1), {.verticalArrangement = Arrangement::spacedBy(14)}, [&] {
                        LinearProgressIndicator(Modifier().fillMaxWidth());
                        LinearProgressIndicator(progress.value(), Modifier().fillMaxWidth());
                    });
                });
            // M3 Expressive: the indicator waves while it moves (flat near the start and the end).
            Text("Dalgalı", {.font = typography().labelLarge});
            Row(Modifier().fillMaxWidth(),
                {.horizontalArrangement = Arrangement::spacedBy(16), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    CircularWavyProgressIndicator();
                    CircularWavyProgressIndicator(progress.value());
                    Column(Modifier().weight(1), {.verticalArrangement = Arrangement::spacedBy(10)}, [&] {
                        LinearWavyProgressIndicator(Modifier().fillMaxWidth());
                        LinearWavyProgressIndicator(progress.value(), Modifier().fillMaxWidth());
                    });
                });
            // M3 Expressive's loading indicator: a shape that turns and morphs (the determinate one into a burst).
            Text("Yükleme göstergesi", {.font = typography().labelLarge});
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(16), .verticalAlignment = Alignment::CenterVertically}, [&] {
                LoadingIndicator();
                ContainedLoadingIndicator();
                LoadingIndicator(progress.value());
                ContainedLoadingIndicator(progress.value());
            });
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                Button(
                    [scope, progress] {
                        scope.launch([progress]() -> Task {
                            progress.snapTo(0.f);
                            co_await progress.animateTo(1.f, tween(2000, 0, LinearEasing));
                        });
                    },
                    [] { Text("Yükle (Animatable)"); });
                OutlinedButton([right] { right.set(!right.get()); }, Modifier(), {}, [] { Text("Yay (spring)"); });
            });
            Box(Modifier().fillMaxWidth().height(40), [&] {
                Box(Modifier().offset(x * 220, 0).size(40).background(colorScheme().primary, 20));
            });
        });
    });
}

void AnimationsSection(CallSite site) {
    composable(site, [=] {
        auto expanded = remember([] { return mutableStateOf(false); });
        auto count = remember([] { return mutableStateOf(0); });
        auto liked = remember([] { return mutableStateOf(false); });
        auto details = remember([] { return mutableStateOf(false); });
        auto pulse = remember([] { return Animatable<float>(1.f); });  // the heart's beat: keyframes
        struct Feed {
            bool loaded;
            int posts;
            bool operator==(const Feed&) const = default;
        };
        auto feed = remember([] { return mutableStateOf(Feed{false, 3}); });
        auto panel = remember([] { return mutableStateOf(false); });
        auto rows = remember([] { return mutableStateOf(std::vector<int>{1, 2}); });
        auto nextRow = remember([] { return mutableStateOf(3); });
        auto scope = rememberCoroutineScope();
        const ColorScheme& cs = colorScheme();
        Section("AnimatedVisibility · AnimatedContent · Crossfade", [&] {
            Row(Modifier().fillMaxWidth(),
                {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    Button([expanded] { expanded.set(!expanded.get()); },
                           [&] { Text(expanded.get() ? "Gizle" : "Detaylar"); });
                    FilledTonalIconButton([count] { count.set(count.get() - 1); },
                                          [] { Icon(Icons::Filled::Remove, "Azalt"); });
                    // The digits come in from below when it grows, from above when it shrinks.
                    AnimatedContent<int>(
                        count.get(),
                        [&](const int& n) {
                            Text(std::to_string(n), Modifier().width(48),
                                 {.fontSize = 28, .fontWeight = FontWeight::Bold, .color = cs.primary,
                                  .textAlign = TextAlign::Center});
                        },
                        Modifier(), [](const int& from, const int& to) {
                            float way = to > from ? 1.f : -1.f;
                            return togetherWith(slideInVertically(way) + fadeIn(), slideOutVertically(-way) + fadeOut());
                        });
                    FilledTonalIconButton([count] { count.set(count.get() + 1); },
                                          [] { Icon(Icons::Filled::Add, "Artır"); });
                    Crossfade<bool>(liked.get(), [&](const bool& on) {
                        IconButton([liked, pulse, scope] {
                            liked.set(!liked.get());
                            scope.launch([pulse]() -> Task { co_await pulse.animateTo(1.f, keyframes(420, {{110, 1.35f}, {260, 0.9f}})); });
                        }, Modifier().graphicsLayer([pulse](GraphicsLayerScope& l) { l.scaleX = l.scaleY = pulse.value(); }), {}, [&] {
                            Icon(on ? Icons::Filled::Favorite : Icons::Filled::FavoriteBorder, "Beğen", {},
                                 on ? std::optional(cs.error) : std::nullopt);
                        });
                    });
                });
            AnimatedVisibility(expanded.get(), [&] {
                Column(Modifier().fillMaxWidth().background(cs.primaryContainer, 12).padding(16),
                       {.verticalArrangement = Arrangement::spacedBy(6)}, [&] {
                           Text("AnimatedVisibility", {.font = typography().titleMedium, .color = cs.onPrimaryContainer});
                           Text("Bu kart fadeIn + expandVertically ile açılır, fadeOut + shrinkVertically ile kapanır. "
                                "Kapanınca içeriği kompozisyondan çıkar.",
                                {.font = typography().bodyMedium, .color = cs.onPrimaryContainer});
                       });
            });
            // SizeTransform: AnimatedContent grows and shrinks between its contents' sizes.
            AnimatedContent<bool>(
                details.get(),
                [&](const bool& open) {
                    if (!open) {
                        Box(Modifier().padding(0, 8, 0, 0).background(cs.secondaryContainer, 20).clickable([details] { details.set(true); }).padding(16, 10),
                            [&] { Text("Özeti aç (SizeTransform)", {.font = typography().labelLarge, .color = cs.onSecondaryContainer}); });
                    } else {
                        Column(Modifier().padding(0, 8, 0, 0).fillMaxWidth().background(cs.secondaryContainer, 16).clickable([details] { details.set(false); })
                                   .padding(16),
                               {.verticalArrangement = Arrangement::spacedBy(6)}, [&] {
                                   Text("AnimatedContent · SizeTransform", {.font = typography().titleMedium, .color = cs.onSecondaryContainer});
                                   Text("Kutu, eski içeriğin boyutundan yenisininkine yayla büyür; içerik o boyuta kırpılır. Kapatmak için dokunun.",
                                        {.font = typography().bodyMedium, .color = cs.onSecondaryContainer});
                               });
                    }
                },
                Modifier().fillMaxWidth(), togetherWith(fadeIn(0, tween(220, 90)), fadeOut(0, tween(90))).withSizeTransform(SizeTransform{}),
                {.contentAlignment = Alignment::TopCenter});  // the closed one in the middle, the card grows from it
            // contentKey: loading → loaded slides; new posts on the loaded screen just update it.
            LaunchedEffect(feed.get().loaded, [feed]() -> Task {  // a load takes a moment
                if (feed.peek().loaded) co_return;
                co_await delay(std::chrono::milliseconds(1500));
                feed.set({true, feed.peek().posts});
            });
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                OutlinedButton([feed] { feed.set({false, feed.get().posts}); }, Modifier(), {.enabled = feed.get().loaded},
                               [] { Text("Yeniden yükle"); });
                OutlinedButton([feed] { feed.set({true, feed.get().posts + 1}); }, Modifier(), {.enabled = feed.get().loaded},
                               [] { Text("Yeni gönderi"); });
            });
            AnimatedContent<Feed>(
                feed.get(),
                [&](const Feed& f) {
                    Row(Modifier().fillMaxWidth().height(40), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically},
                        [&] {
                            if (!f.loaded) {
                                CircularProgressIndicator(Modifier().size(20));
                                Text("Yükleniyor…", {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
                            } else {
                                Icon(Icons::Filled::CheckCircle, "Yüklendi", {}, cs.primary);
                                Text(std::format("{} gönderi (contentKey: yeni veri geçişsiz)", f.posts), {.font = typography().bodyMedium});
                            }
                        });
                },
                Modifier().fillMaxWidth(), togetherWith(slideInVertically(1) + fadeIn(), slideOutVertically(-1) + fadeOut()),
                {.contentKey = [](const Feed& f) { return uint64_t(f.loaded); }});
            // animateEnterExit: the content itself comes and goes at once; inside it the scrim
            // fades and the panel slides, each its own way, and it stays until both are out.
            OutlinedButton([panel] { panel.set(!panel.get()); }, Modifier(), {},
                           [&] { Text(panel.get() ? "Paneli kapat" : "Panel (animateEnterExit)"); });
            AnimatedVisibility(panel.get(), Modifier().fillMaxWidth(), EnterTransition::None, ExitTransition::None,
                               [&](const AnimatedVisibilityScope& s) {
                Box(Modifier().fillMaxWidth().height(140).clip(RoundedCornerShape(16)), [&] {
                    Box(Modifier().matchParentSize().background(cs.onSurface.copy(0.2f)).then(s.animateEnterExit(fadeIn(0, tween(300)), fadeOut(0, tween(300)))));
                    Box(s.animateEnterExit(slideInVertically(1, tween(450)), slideOutVertically(1, tween(450)))
                            .align(Alignment::BottomCenter)
                            .fillMaxWidth()
                            .height(90)
                            .background(cs.primaryContainer, 16)
                            .padding(16),
                        [&] {
                            Text("Panel aşağıdan kayar, arka plan ayrıca solar; kapanırken ikisi de bitene dek kalır.",
                                 {.font = typography().bodyMedium, .color = cs.onPrimaryContainer});
                        });
                });
            });
            // MutableTransitionState: a row made hidden with a visible target comes in as it first
            // shows (each one added); its currentState and isIdle say where it is, and a row taken
            // away is removed from the list once it is all gone.
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8), .verticalAlignment = Alignment::CenterVertically}, [&] {
                OutlinedButton([rows, nextRow] {
                    auto list = rows.get();
                    list.push_back(nextRow.get());
                    rows.set(list);
                    nextRow.set(nextRow.get() + 1);
                }, Modifier(), {}, [] { Text("Satır ekle"); });
                Text("MutableTransitionState", {.font = typography().labelLarge, .color = cs.onSurfaceVariant});
            });
            for (int row : rows.get())
                key(row, [&] {
                    auto shown = remember([] {
                        MutableTransitionState<bool> s(false);
                        s.setTargetState(true);
                        return s;
                    });
                    if (shown.isIdle() && !shown.currentState())  // all gone: off the list
                        SideEffect([rows, row] {
                            auto list = rows.peek();
                            std::erase(list, row);
                            rows.set(list);
                        });
                    AnimatedVisibility(shown, Modifier().fillMaxWidth(), fadeIn(0, tween(500)) + expandVertically(tween(500)),
                                       fadeOut(0, tween(500)) + shrinkVertically(tween(500)), [&] {
                        Row(Modifier().fillMaxWidth().padding(0, 4).background(cs.surfaceContainerHigh, 12).padding(16, 6, 4, 6),
                            {.verticalAlignment = Alignment::CenterVertically}, [&] {
                                const char* where = shown.isIdle() ? "duruyor" : shown.targetState() ? "beliriyor" : "kayboluyor";
                                Text(std::format("Satır {} · {}", row, where), Modifier().weight(1), {.font = typography().bodyMedium});
                                IconButton([shown] { shown.setTargetState(false); }, [] { Icon(Icons::Filled::Close, "Kaldır"); });
                            });
                    });
                });
        });
    });
}

// Images from assets and from code, ContentScale, shapes and clipping.
// AsyncImage: JPEG and WebP assets loaded away from the UI thread, cross-fading in from a
// placeholder, and a big photo from the network when asked for, decoded at its box's size and
// kept on disk.
void AsyncImageSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto url = remember([] { return mutableStateOf(std::string("https://picsum.photos/seed/kompoz/2400/1600")); });
        auto requested = remember([] { return mutableStateOf(std::string()); });
        auto decoded = remember([] { return mutableStateOf(std::string()); });
        Content placeholder = [cs] {
            Box(Modifier().fillMaxSize().background(cs.surfaceContainerHighest), Alignment::Center,
                [] { CircularProgressIndicator(Modifier().size(24)); });
        };
        AsyncImageOptions options{.placeholder = placeholder, .contentScale = ContentScale::Crop, .crossfadeMillis = 300};
        Section("AsyncImage · JPEG · WebP · ağ", [&] {
            Text("Resimler arka planda yüklenip çözülür: masaüstünde libjpeg-turbo, libwebp ve libcurl, Android'de "
                 "BitmapFactory, web'de tarayıcı, Windows'ta WIC. Gelene kadar yer tutucu, sonra geçişle belirir. "
                 "Büyük resim kutusunun boyutunda çözülür; yüklenenler bellekte, indirilenler diskte kalır "
                 "(uygulama yeniden açılınca ağsız gelir).",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
                AsyncImage("images/mountains.jpg", "Dağlar (JPEG)", Modifier().weight(1).height(110).clip(RoundedCornerShape(12)), options);
                AsyncImage("images/lake.webp", "Göl (WebP)", Modifier().weight(1).height(110).clip(RoundedCornerShape(12)), options);
            });
            OutlinedTextField(url.get(), [url](std::string v) { url.set(std::move(v)); }, Modifier().fillMaxWidth(),
                              {.singleLine = true, .label = "Adres"});
            Button([url, requested] { requested.set(url.peek()); }, [] { Text("Ağdan yükle"); });
            if (!requested.get().empty()) {
                AsyncImageOptions network = options;
                network.error = [cs] {
                    Box(Modifier().fillMaxSize().background(cs.surfaceContainerHighest), Alignment::Center,
                        [cs] { Text("Yüklenemedi", {.color = cs.error}); });
                };
                network.onState = [decoded](const AsyncImageState& s) {
                    if (s.status == AsyncImageState::Status::Success)
                        decoded.set(std::format("Çözülen boyut: {}×{} piksel", s.image.width(), s.image.height()));
                };
                AsyncImage(requested.get(), "Ağdan bir resim", Modifier().fillMaxWidth().height(170).clip(RoundedCornerShape(12)), network);
                if (!decoded.get().empty()) Text(decoded.get(), {.font = typography().bodySmall, .color = cs.onSurfaceVariant});
            }
        });
    });
}

void ImagesSection(CallSite site) {
    composable(site, [=] {
        ImageBitmap photo = imageResource("images/landscape.png");
        ImageBitmap generated = remember([] {
            std::vector<uint8_t> px(64 * 64 * 4);
            for (int y = 0; y < 64; ++y)
                for (int x = 0; x < 64; ++x) {
                    uint8_t* p = &px[size_t(y * 64 + x) * 4];
                    bool check = ((x / 8) + (y / 8)) % 2 == 0;
                    p[0] = uint8_t(x * 4);
                    p[1] = uint8_t(check ? 200 : 120);
                    p[2] = uint8_t(255 - y * 4);
                    p[3] = 255;
                }
            return ImageBitmap::fromPixels(64, 64, std::move(px));
        });
        const ColorScheme& cs = colorScheme();
        Section("Image, Shape, clip · Painter", [&] {
            Image(photo, "Manzara", Modifier().fillMaxWidth().height(150).clip(RoundedCornerShape(16)),
                  ContentScale::Crop);
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::SpaceBetween}, [&] {
                const std::pair<ContentScale, const char*> scales[] = {{ContentScale::Fit, "Fit"},
                                                                       {ContentScale::Crop, "Crop"},
                                                                       {ContentScale::FillBounds, "FillBounds"},
                                                                       {ContentScale::None, "None"}};
                for (const auto& [scale, name] : scales) {
                    Column(Modifier(), {.verticalArrangement = Arrangement::spacedBy(4),
                                        .horizontalAlignment = Alignment::CenterHorizontally},
                           [&] {
                               Image(photo, name, Modifier().size(72, 56).background(cs.outlineVariant).border(1, cs.outline),
                                     scale);
                               Text(name, {.fontSize = 10, .color = cs.onSurfaceVariant});
                           });
                }
            });
            // ColorFilter: gray (saturation 0), sepia (a color matrix), a tint (SrcIn: the shape in
            // one color, SrcAtop: a wash over it) and a light that multiplies and adds.
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::SpaceBetween}, [&] {
                ColorMatrix gray, sepia;
                gray.setToSaturation(0);
                sepia.values = {0.393f, 0.769f, 0.189f, 0, 0, 0.349f, 0.686f, 0.168f, 0, 0, 0.272f, 0.534f, 0.131f, 0, 0, 0, 0, 0, 1, 0};
                const std::pair<std::optional<ColorFilter>, const char*> filters[] = {
                    {std::nullopt, "yok"},
                    {ColorFilter::colorMatrix(gray), "gri"},
                    {ColorFilter::colorMatrix(sepia), "sepya"},
                    {ColorFilter::tint(cs.primary.copy(0.45f), BlendMode::SrcAtop), "SrcAtop"},
                    {ColorFilter::lighting(Color{1, 0.7f, 0.7f, 1}, Color{0.15f, 0, 0, 0}), "lighting"}};
                for (const auto& [filter, name] : filters)
                    Column(Modifier(), {.verticalArrangement = Arrangement::spacedBy(4), .horizontalAlignment = Alignment::CenterHorizontally},
                           [&] {
                               Image(photo, name, Modifier().size(56).clip(RoundedCornerShape(12)), ContentScale::Crop, 1, filter);
                               Text(name, {.fontSize = 10, .color = cs.onSurfaceVariant});
                           });
            });
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(16), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    Image(photo, "Avatar", Modifier().size(64).clip(CircleShape).border(2, cs.primary, CircleShape),
                          ContentScale::Crop);
                    Image(generated, "Kodla üretilmiş", Modifier().size(64).clip(RoundedCornerShape(12)),
                          ContentScale::FillBounds);
                    Box(Modifier().size(64).alpha(0.5f).background(cs.primary, CircleShape), Alignment::Center,
                        [&] { Icon(Icons::Filled::Star, "Yarı saydam", Modifier().size(32), cs.onPrimary); });
                    Text("CircleShape, RoundedCornerShape, alpha(0.5)", Modifier().weight(1),
                         {.fontSize = 12, .color = cs.onSurfaceVariant});
                });
            // Corners cut straight (CutCornerShape), and a ticket with its start corners cut further.
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(16), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    Image(photo, "Kesik köşeli", Modifier().size(64).clip(CutCornerShape(16)).border(2, cs.tertiary, CutCornerShape(16)),
                          ContentScale::Crop);
                    Box(Modifier().height(48).width(120).background(cs.tertiaryContainer, CutCornerShape(20, 6, 6, 20)), Alignment::Center,
                        [&] { Text("Bilet", {.color = cs.onTertiaryContainer}); });
                    Text("CutCornerShape", Modifier().weight(1), {.fontSize = 12, .color = cs.onSurfaceVariant});
                });
            // Modifier.magnifier: a finger on the photo shows what is under it, bigger, above it.
            auto lens = remember([] { return mutableStateOf(Offset{std::nanf(""), 0}); });
            Box(Modifier()
                    .fillMaxWidth()
                    .height(120)
                    .clip(RoundedCornerShape(16))
                    .magnifier([lens] { return lens.get(); }, {.zoom = 2, .size = {120, 80}, .cornerRadius = 16})
                    .pointerInput(0,
                                  [lens](PointerInputScope& scope) -> Task {
                                      co_await detectDragGestures(scope, {.onDragStart = [lens](Offset p) { lens.set(p); },
                                                                          .onDrag = [lens](PointerInputChange& c, Offset) { lens.set(c.position); },
                                                                          .onDragEnd = [lens] { lens.set({std::nanf(""), 0}); },
                                                                          .onDragCancel = [lens] { lens.set({std::nanf(""), 0}); }});
                                  }),
                [&] {
                    Image(photo, "Büyüteçle bakın", Modifier().fillMaxSize(), ContentScale::Crop);
                    Text("Modifier.magnifier: parmağınızı sürükleyin", Modifier().align(Alignment::BottomStart).padding(8),
                         {.fontSize = 12, .color = colors::White, .shadow = Shadow{Color{0, 0, 0, 0.8f}, {1, 1}, 3}});
                });
            // Painter: an asset (painterResource), a vector, a brush and drawing of your own, as
            // an Image, an Icon or Modifier.paint behind content.
            Row(Modifier(), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    Image(painterResource("images/landscape.png"), "painterResource", Modifier().size(64).clip(RoundedCornerShape(12)),
                          Alignment::Center, ContentScale::Crop);
                    Icon(rememberVectorPainter(Icons::Filled::Favorite), "VectorPainter", Modifier().size(40), cs.tertiary);
                    Box(Modifier().size(64, 40).clip(RoundedCornerShape(8))
                            .paint(BrushPainter(Brush::horizontalGradient(std::vector<Color>{cs.primary, cs.tertiary})), false),
                        Alignment::Center, [&] { Text("paint", {.font = typography().labelLarge, .color = cs.onPrimary}); });
                    Image(Painter::custom(
                              [](DrawScope& d) {
                                  Size s = d.size();
                                  for (int i = 0; i < 4; ++i)
                                      d.drawCircle(hueColor(i * 5), s.width / 2 - float(i) * 7, Offset{s.width / 2, s.height / 2});
                              },
                              Size{56, 56}),
                          "Painter::custom", Modifier(), Alignment::Center, ContentScale::Fit, 0.8f);
                });
        });
    });
}

// Shapes that aren't rounded rects (OutlineShape): an image clipped to a star, a button whose
// background, shadow and ripple follow it, a bordered triangle.
const ShapeOutline& starOutline() {
    static const ShapeOutline star = [] {
        ShapeOutline o;
        constexpr int kPoints = 200;
        for (int i = 0; i < kPoints; ++i) {
            float a = 2 * std::numbers::pi_v<float> * float(i) / kPoints - std::numbers::pi_v<float> / 2;  // from the top
            float r = 0.62f + 0.38f * std::pow(std::abs(std::cos(2.5f * (a + std::numbers::pi_v<float> / 2))), 3.f);  // five soft points
            o.points.push_back({0.5f + 0.5f * r * std::cos(a), 0.5f + 0.5f * r * std::sin(a)});
        }
        return o;
    }();
    return star;
}

// Material's 35 shapes (MaterialShapes): tap one for its name; the big one morphs to the next of
// a few on each tap.
void MaterialShapesSection(CallSite site) {
    composable(site, [=] {
        using namespace MaterialShapes;
        static const std::array<const ShapeOutline& (*)(), 6> cycle{Cookie9Sided, Clover4Leaf, Sunny, Heart, SoftBurst, Pill};
        const ColorScheme& cs = colorScheme();
        auto picked = remember([] { return mutableStateOf(std::string("Cookie9Sided")); });
        auto step = remember([] { return mutableStateOf(0); });
        auto progress = remember([] { return Animatable<float>(1.f); });
        auto scope = rememberCoroutineScope();
        int i = step.get();
        auto& morph = remember(i, [i] {
            size_t n = cycle.size();
            return std::make_shared<Morph>(cycle[(size_t(i) + n - 1) % n](), cycle[size_t(i) % n]());
        });
        Section("MaterialShapes · Morph", [&] {
            Text("Material 3 Expressive'in 35 şekli; birine dokunun, adı görünsün. Büyük şekle dokununca sıradakine dönüşür (Morph).",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(16), .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    Box(Modifier()
                            .size(96)
                            .background(cs.tertiaryContainer, morph->toShape(progress.value()))
                            .clickable([step, progress, scope] {
                                step.set(step.get() + 1);
                                scope.launch([progress]() -> Task {
                                    progress.snapTo(0);
                                    co_await progress.animateTo(1, spring(0.6f, Spring::StiffnessLow));
                                });
                            }),
                        Alignment::Center,
                        [&] { Text(cycle[size_t(i) % cycle.size()] == cycle[0] ? "Dokun" : "", {.color = cs.onTertiaryContainer}); });
                    Text("Seçilen: " + picked.get(), Modifier().weight(1), {.font = typography().titleMedium});
                });
            FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 10, .verticalSpacing = 10}, [&] {
                for (const auto& shape : all()) {
                    Box(Modifier()
                            .size(44)
                            .background(picked.get() == shape.name ? cs.primary : cs.secondary, toShape(shape.outline()))
                            .clickable([picked, name = std::string(shape.name)] { picked.set(name); })
                            .semantics(shape.name));
                }
            });
        });
    });
}

void ShapesSection(CallSite site) {
    composable(site, [=] {
        static const ShapeOutline triangle{{{0.5f, 0}, {1, 1}, {0, 1}}};
        const ColorScheme& cs = colorScheme();
        auto taps = remember([] { return mutableStateOf(0); });
        Section("Şekiller (OutlineShape)", [&] {
            // dropShadow / innerShadow: a soft shadow under a card, a glow (spread) in a gradient
            // (a brush), and a shadow inside a well, as if its edge cast it.
            Row(Modifier().fillMaxWidth().padding(0, 8), {.horizontalArrangement = Arrangement::SpaceEvenly}, [&] {
                Box(Modifier()
                        .size(88, 64)
                        .dropShadow(RoundedCornerShape(16), Shadow{Color{0, 0, 0, 0.35f}, {0, 6}, 10})
                        .background(cs.surface, 16),
                    Alignment::Center, [&] { Text("dropShadow", {.font = typography().labelSmall}); });
                Box(Modifier()
                        .size(88, 64)
                        .dropShadow(CircleShape, Shadow{.blurRadius = 12, .spread = 4,
                                                        .brush = Brush::horizontalGradient({cs.primary, cs.tertiary}), .alpha = 0.7f})
                        .background(cs.primary, CircleShape),
                    Alignment::Center, [&] { Text("parıltı", {.font = typography().labelSmall, .color = cs.onPrimary}); });
                Box(Modifier()
                        .size(88, 64)
                        .background(cs.surfaceContainerHigh, 16)
                        .innerShadow(RoundedCornerShape(16), Shadow{Color{0, 0, 0, 0.45f}, {2, 4}, 6}),
                    Alignment::Center, [&] { Text("innerShadow", {.font = typography().labelSmall}); });
            });
            // The same shadows following an outline shape: a glow round a heart, a well in a cookie.
            Row(Modifier().fillMaxWidth().padding(0, 8), {.horizontalArrangement = Arrangement::SpaceEvenly}, [&] {
                Box(Modifier()
                        .size(72)
                        .dropShadow(OutlineShape(MaterialShapes::Heart()), Shadow{cs.tertiary.copy(0.7f), {}, 10, 3})
                        .background(cs.tertiary, OutlineShape(MaterialShapes::Heart())));
                Box(Modifier()
                        .size(72)
                        .dropShadow(OutlineShape(MaterialShapes::Cookie9Sided()), Shadow{Color{0, 0, 0, 0.4f}, {0, 6}, 8})
                        .background(cs.surface, OutlineShape(MaterialShapes::Cookie9Sided())));
                Box(Modifier()
                        .size(72)
                        .background(cs.surfaceContainerHigh, OutlineShape(MaterialShapes::Cookie9Sided()))
                        .innerShadow(OutlineShape(MaterialShapes::Cookie9Sided()), Shadow{Color{0, 0, 0, 0.5f}, {2, 4}, 6}));
            });
            // shadow(elevation, shape, clip, ambientColor, spotColor): a tinted shadow, the content
            // clipped to the shape; BorderStroke with a brush: a gradient border.
            Row(Modifier().fillMaxWidth().padding(0, 8), {.horizontalArrangement = Arrangement::SpaceEvenly}, [&] {
                Box(Modifier().size(120, 64).shadow(12, RoundedCornerShape(20), true, cs.primary, cs.primary).background(cs.surface),
                    Alignment::Center, [&] { Text("renkli gölge", {.font = typography().labelSmall}); });
                Box(Modifier()
                        .size(120, 64)
                        .border(BorderStroke(3, Brush::linearGradient({cs.primary, cs.tertiary, Color::hex(0xFFB300)})), RoundedCornerShape(20)),
                    Alignment::Center, [&] { Text("BorderStroke", {.font = typography().labelSmall}); });
            });
            Text("Köşeleri yuvarlak dikdörtgen olmayan şekiller: resim yıldıza kırpılır; düğmenin arka planı, gölgesi ve dalgası "
                 "şeklin içinde kalır; çerçeve şekli izler.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::SpaceEvenly, .verticalAlignment = Alignment::CenterVertically},
                [&] {
                    AsyncImage("images/mountains.jpg", "Yıldız dağlar", Modifier().size(96).clip(OutlineShape(starOutline())),
                               {.contentScale = ContentScale::Crop});
                    Box(Modifier()
                            .size(88)
                            .shadow(6, OutlineShape(starOutline()))
                            .background(cs.primaryContainer, OutlineShape(starOutline()))
                            .clickable([taps] { taps.set(taps.get() + 1); }),
                        Alignment::Center, [&] { Text(std::to_string(taps.get()), {.font = typography().titleLarge, .color = cs.onPrimaryContainer}); });
                    Box(Modifier().size(80, 70).border(3, cs.tertiary, OutlineShape(triangle)));
                });
        });
    });
}

// SharedTransitionLayout: a list and the detail of a row; the picture and the title move
// between their places in the two.
void SharedElementSection(CallSite site) {
    composable(site, [=] {
        auto selected = remember([] { return mutableStateOf(-1); });
        static const Color palette[] = {Color::hex(0xB8705F), Color::hex(0x6C9F6A), Color::hex(0x5F82B8)};
        static const char* names[] = {"Kızıl kayalar", "Yeşil vadi", "Mavi göl"};
        Section("SharedTransitionLayout · sharedElement · sharedBounds", [&] {
            SharedTransitionLayout(Modifier().fillMaxWidth().height(340), [&](const SharedTransitionScope& shared) {
                AnimatedContent<int>(selected.get(), [&](const AnimatedContentScope& scope, const int& item) {
                    auto photo = [&](int i) { return shared.rememberSharedContentState("photo" + std::to_string(i)); };
                    auto title = [&](int i) { return shared.rememberSharedContentState("title" + std::to_string(i)); };
                    if (item < 0) {
                        Column(Modifier().fillMaxWidth(), {.verticalArrangement = Arrangement::spacedBy(4)}, [&] {
                            for (int i = 0; i < 3; ++i)
                                key(i, [&] {
                                    Row(Modifier().fillMaxWidth().clickable([selected, i] { selected.set(i); }).padding(8),
                                        {.horizontalArrangement = Arrangement::spacedBy(14), .verticalAlignment = Alignment::CenterVertically},
                                        [&] {
                                            Box(Modifier().size(64).sharedElement(photo(i), scope).background(palette[i], 14));
                                            Text(names[i], Modifier().sharedBounds(title(i), scope), {.font = typography().titleMedium});
                                        });
                                });
                            Text("Bir satıra dokunun: resim ve başlık ayrıntıdaki yerlerine taşınır.",
                                 Modifier().padding(8, 4), {.font = typography().bodyMedium, .color = colorScheme().onSurfaceVariant});
                        });
                    } else {
                        Column(Modifier().fillMaxWidth(), {.verticalArrangement = Arrangement::spacedBy(10)}, [&] {
                            Box(Modifier().fillMaxWidth().height(170).sharedElement(photo(item), scope).background(palette[item], 24));
                            Text(names[item], Modifier().sharedBounds(title(item), scope), {.font = typography().headlineSmall});
                            Text("Geri dönünce ikisi listedeki yerlerine uçar.", {.font = typography().bodyMedium});
                            TextButton([selected] { selected.set(-1); }, [] { Text("Geri"); });
                        });
                    }
                });
            });
        });
    });
}

// Modifier.scrollable: a sideways drag turns a knob (rotate) and grows a square (scale).
// BlendMode in an offscreen layer: a list whose top and bottom fade out (DstIn), and a hole
// punched through a card (DstOut).
void BlendModeSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        Section("BlendMode · CompositingStrategy.Offscreen", [&] {
            Text("Liste kendi katmanında: üstte ve altta bir degradeyle (DstIn) kaybolur. Sağdaki kartta DstOut bir delik açar.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Row(Modifier().fillMaxWidth().height(180), {.horizontalArrangement = Arrangement::spacedBy(12)}, [&] {
                Box(Modifier().weight(1).fillMaxHeight()
                        .graphicsLayer([](GraphicsLayerScope& l) { l.compositingStrategy = CompositingStrategy::Offscreen; })
                        .drawWithContent([](ContentDrawScope& d) {
                            d.drawContent();
                            d.drawRect(Brush::verticalGradient({colors::Transparent, colors::Black, colors::Black, colors::Transparent}), {}, std::nullopt, 1, Fill, {}, BlendMode::DstIn);
                        }),
                    [&] {
                        LazyColumn(Modifier().fillMaxSize(), {.contentPadding = {0, 16, 0, 16}}, [&](LazyListScope& list) {
                            list.items(40, [&](int i) {
                                Text(std::format("Satır {}", i + 1), Modifier().fillMaxWidth().padding(8, 6), {.font = typography().bodyLarge});
                            });
                        });
                    });
                Box(Modifier().weight(1).fillMaxHeight()
                        .graphicsLayer([](GraphicsLayerScope& l) { l.compositingStrategy = CompositingStrategy::Offscreen; })
                        .drawWithContent([](ContentDrawScope& d) {
                            d.drawContent();
                            d.drawCircle(colors::Black, 36, std::nullopt, 1, Fill, {}, BlendMode::DstOut);
                        })
                        .background(Brush::linearGradient({cs.primary, cs.tertiary}), RoundedCornerShape(16)),
                    Alignment::BottomCenter, [&] {
                        Text("DstOut", Modifier().padding(12), {.font = typography().titleMedium, .color = cs.onPrimary});
                    });
            });
        });
    });
}

// Modifier.blur: a photo blurred as much as the slider says, kept to its bounds or spreading out;
// another blurred progressively, more towards the bottom; one blurred by a shader's alpha, more
// towards its edges; a frosted glass panel (Morph) and a card whose picture alone is blurred
// (Self).
// TileMode: a 60 dp gradient across 300 dp, past its end as each mode goes. Shaders: a photo
// mirrored as tiles (ImageShader), a gradient lit by a radial one (Brush.composite, Screen), a
// sweep made for each size (ShaderBrush), turned round its center.
void TileModeSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        ImageBitmap photo = imageResource("images/landscape.png");
        Section("Brush · TileMode · Shader", [&] {
            const std::pair<TileMode, const char*> modes[] = {
                {TileMode::Clamp, "Clamp"}, {TileMode::Repeated, "Repeated"}, {TileMode::Mirror, "Mirror"}, {TileMode::Decal, "Decal"}};
            for (const auto& [mode, name] : modes)
                Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(12), .verticalAlignment = Alignment::CenterVertically}, [&] {
                    Text(name, Modifier().width(76), {.font = typography().labelLarge});
                    Box(Modifier()
                            .weight(1)
                            .height(32)
                            .border(1, cs.outlineVariant, 8)
                            .background(Brush::horizontalGradient({cs.primary, cs.tertiaryContainer}, 0, 60, mode), RoundedCornerShape(8)));
                });
            Row(Modifier().fillMaxWidth().height(72), {.horizontalArrangement = Arrangement::spacedBy(12)}, [&] {
                Matrix small;
                small.scale(0.1f, 0.1f);
                Box(Modifier().weight(1).fillMaxHeight().background(
                    Brush::shader(ImageShader(photo, TileMode::Mirror, TileMode::Mirror).transformed(small)), RoundedCornerShape(12)));
                Box(Modifier().weight(1).fillMaxHeight().background(
                    Brush::composite(Brush::linearGradient({cs.primary, cs.tertiary}),
                                     Brush::radialGradient({colors::White.copy(0.9f), colors::Transparent}, Offset{24, 20}, 48), BlendMode::Screen),
                    RoundedCornerShape(12)));
                Box(Modifier().weight(1).fillMaxHeight().background(
                    Brush::shader([cs](Size size) {
                        Matrix turn, center;
                        turn.rotateZ(-90);
                        center.translate(size.width / 2, size.height / 2);
                        return SweepGradientShader({0, 0}, {cs.primary, Color::hex(0xFFB300), cs.tertiary, cs.primary}).transformed(turn).transformed(center);
                    }),
                    RoundedCornerShape(12)));
            });
        });
    });
}

// graphics::Canvas through drawIntoCanvas: twelve spokes each drawn after save, rotate (about the
// middle) and restore; a grid skewed; two circles faded together in a layer (saveLayer: where they
// overlap only the one on top shows); a dashed stroke and a corner-rounded fill in Paints. Meshes
// (drawVertices): a fan of colors round a white middle, and the photo on a waving strip, its
// colors multiplied by the vertices'.
void CanvasSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        ImageBitmap photo = imageResource("images/landscape.png");
        Section("Canvas · Paint · drawVertices", [&] {
            Row(Modifier().fillMaxWidth().height(96), {.horizontalArrangement = Arrangement::spacedBy(12)}, [&] {
                Canvas(Modifier().weight(1).fillMaxHeight(), [primary = cs.primary, tertiary = cs.tertiary](DrawScope& d) {
                    Size s = d.size();
                    d.drawIntoCanvas([&](graphics::Canvas& c) {
                        Paint spoke{.color = primary, .style = PaintingStyle::Stroke, .strokeWidth = 4, .strokeCap = StrokeCap::Round};
                        for (int i = 0; i < 12; ++i) {
                            spoke.color = lerp(primary, tertiary, float(i) / 11);
                            c.save();
                            c.rotate(float(i) * 30, s.width / 2, s.height / 2);
                            c.drawLine({s.width / 2, s.height / 2 - 18}, {s.width / 2, s.height / 2 - 40}, spoke);
                            c.restore();
                        }
                    });
                });
                Canvas(Modifier().weight(1).fillMaxHeight(), [outline = cs.outline](DrawScope& d) {
                    d.drawIntoCanvas([&](graphics::Canvas& c) {
                        Paint line{.color = outline, .style = PaintingStyle::Stroke, .strokeWidth = 1};
                        c.translate(24, 8);
                        c.skew(-0.4f, 0);
                        for (int i = 0; i <= 8; ++i) {
                            c.drawLine({float(i) * 10, 0}, {float(i) * 10, 80}, line);
                            c.drawLine({0, float(i) * 10}, {80, float(i) * 10}, line);
                        }
                    });
                });
                Canvas(Modifier().weight(1).fillMaxHeight().background(cs.secondaryContainer, 12), [primary = cs.primary](DrawScope& d) {
                    Size s = d.size();
                    d.drawIntoCanvas([&](graphics::Canvas& c) {
                        Paint faded;
                        faded.setAlpha(0.6f);
                        c.saveLayer({0, 0, s.width, s.height}, faded);
                        c.drawCircle({s.width / 2 - 14, s.height / 2}, 26, Paint{.color = primary});
                        c.drawCircle({s.width / 2 + 14, s.height / 2}, 26, Paint{.color = Color::hex(0xFFB300)});
                        c.restore();
                    });
                });
                Canvas(Modifier().weight(1).fillMaxHeight(), [primary = cs.primary, tertiary = cs.tertiary](DrawScope& d) {
                    Size s = d.size();
                    d.drawIntoCanvas([&](graphics::Canvas& c) {
                        c.drawRect({8, 8, s.width - 16, s.height / 2 - 12},
                                   Paint{.color = tertiary, .pathEffect = PathEffect::cornerPathEffect(10)});
                        c.drawRect({8, s.height / 2 + 4, s.width - 16, s.height / 2 - 12},
                                   Paint{.color = primary, .style = PaintingStyle::Stroke, .strokeWidth = 3,
                                         .pathEffect = PathEffect::dashPathEffect({8, 5})});
                    });
                });
            });
            Row(Modifier().fillMaxWidth().height(96), {.horizontalArrangement = Arrangement::spacedBy(12)}, [&] {
                Canvas(Modifier().size(96), [primary = cs.primary, tertiary = cs.tertiary](DrawScope& d) {
                    std::vector<Offset> at{{48, 48}};
                    std::vector<Color> colors{colors::White};
                    const Color ring[] = {primary, Color::hex(0xFFB300), tertiary, Color::hex(0x26A69A), primary};
                    for (int i = 0; i <= 24; ++i) {
                        float a = float(i) / 24 * 2 * std::numbers::pi_v<float>;
                        at.push_back({48 + 44 * std::cos(a), 48 + 44 * std::sin(a)});
                        float t = float(i) / 24 * 4;
                        int k = std::min(3, int(t));
                        colors.push_back(lerp(ring[k], ring[k + 1], t - float(k)));
                    }
                    Vertices fan(VertexMode::TriangleFan, at, {}, colors);
                    d.drawIntoCanvas([&](graphics::Canvas& c) { c.drawVertices(fan, BlendMode::Modulate, Paint{.color = colors::White}); });
                });
                Canvas(Modifier().weight(1).fillMaxHeight(), [photo, primary = cs.primary](DrawScope& d) {
                    if (photo.empty()) return;
                    Size s = d.size();
                    std::vector<Offset> at, tex;
                    std::vector<Color> colors;
                    for (int i = 0; i <= 16; ++i) {
                        float t = float(i) / 16, x = 8 + t * (s.width - 16), wave = 8 * std::sin(t * 2 * std::numbers::pi_v<float>);
                        at.push_back({x, 12 + wave}), at.push_back({x, s.height - 12 + wave});
                        tex.push_back({t * float(photo.width()), 0}), tex.push_back({t * float(photo.width()), float(photo.height())});
                        Color c = lerp(colors::White, primary, t);
                        colors.push_back(c), colors.push_back(c);
                    }
                    Vertices strip(VertexMode::TriangleStrip, at, tex, colors);
                    d.drawIntoCanvas([&](graphics::Canvas& c) {
                        c.drawVertices(strip, BlendMode::Multiply, Paint{.shader = ImageShader(photo)});
                    });
                });
            });
        });
    });
}

void BlurSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        auto radius = remember([] { return mutableStateOf(8.f); });
        auto unbounded = remember([] { return mutableStateOf(false); });
        ImageBitmap photo = imageResource("images/landscape.png");
        Section("Modifier.blur", [&] {
            Text(std::format("Yarıçap: {:.0f} dp", radius.get()), {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Slider(radius.get(), [radius](float v) { radius.set(v); }, Modifier().fillMaxWidth(), {.valueFrom = 0, .valueTo = 32});
            Row(Modifier(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                Switch(unbounded.get(), [unbounded](bool v) { unbounded.set(v); });
                Text("Unbounded (dışa taşar)", Modifier().padding(12, 0, 0, 0));
            });
            Box(Modifier().fillMaxWidth().height(160).padding(16), Alignment::Center, [&] {
                Image(photo, "Manzara",
                      Modifier().fillMaxSize()
                          .blur(radius.get(), unbounded.get() ? BlurredEdgeTreatment::Unbounded : BlurredEdgeTreatment::Rectangle)
                          .clip(RoundedCornerShape(16)),
                      ContentScale::Crop);
                Text("Buzlu cam", {.font = typography().headlineSmall, .color = colors::White});
            });
            // Progressive (Compose 1.13): sharp at the top, as blurred as the slider says at the bottom.
            Text("Kademeli: üstte keskin, altta bulanık", {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Box(Modifier().fillMaxWidth().height(160).padding(16), [&] {
                Image(photo, "Manzara",
                      Modifier().fillMaxSize()
                          .blur(BlurRadiusSpec::verticalGradient(0, radius.get()),
                                unbounded.get() ? BlurredEdgeTreatment::Unbounded : BlurredEdgeTreatment::Rectangle)
                          .clip(RoundedCornerShape(16)),
                      ContentScale::Crop);
            });
            // By a shader: sharp in the middle, as blurred as the slider says at the edges.
            Text("Shader ile: ortası keskin, kenarları bulanık", {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Box(Modifier().fillMaxWidth().height(160).padding(16), [&] {
                Image(photo, "Manzara",
                      Modifier().fillMaxSize()
                          .blur(BlurRadiusSpec::shader(radius.get(),
                                                       [](Density, Size size) {
                                                           return RadialGradientShader({size.width / 2, size.height / 2}, std::hypot(size.width, size.height) / 2,
                                                                                       {colors::Transparent, colors::Transparent, colors::Black}, {0, 0.35f, 1});
                                                       }),
                                unbounded.get() ? BlurredEdgeTreatment::Unbounded : BlurredEdgeTreatment::Rectangle)
                          .clip(RoundedCornerShape(16)),
                      ContentScale::Crop);
            });
            // Morph: what is behind a glass panel blurred, the panel's text sharp; Self: a card's
            // own picture blurred, its text sharp.
            Text("Morph: arkadaki bulanık, cam keskin · Self: kartın resmi bulanık, yazısı keskin",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
            Box(Modifier().fillMaxWidth().height(160).padding(16), Alignment::Center, [&] {
                Image(photo, "Manzara", Modifier().fillMaxSize().clip(RoundedCornerShape(16)), ContentScale::Crop);
                Box(Modifier().size(200, 72)
                        .blur(radius.get(), BlurredEdgeTreatment{RoundedCornerShape(16)}, BlurType::Morph)
                        .background(colors::White.copy(0.18f), RoundedCornerShape(16))
                        .border(1, colors::White.copy(0.5f), RoundedCornerShape(16)),
                    Alignment::Center, [&] { Text("Buzlu cam", {.font = typography().titleMedium, .color = colors::White}); });
            });
            Box(Modifier().fillMaxWidth().height(120).padding(16, 0, 16, 16)
                    .blur(radius.get(), BlurredEdgeTreatment{RoundedCornerShape(16)}, BlurType::Self)
                    .paint(BitmapPainter(photo), false, Alignment::Center, ContentScale::Crop),
                Alignment::Center, [&] { Text("Yazı keskin kalır", {.font = typography().headlineSmall, .color = colors::White}); });
        });
    });
}

// ImageVector::Builder: vectors of paths in paints of their own (fills, strokes, a gradient),
// drawn as they are by Image and tinted by Icon.
void ColorVectorSection(CallSite site) {
    composable(site, [=] {
        const ColorScheme& cs = colorScheme();
        static const ImageVector badge = [] {
            VectorPath check;
            check.commands = VectorPath::parse("M7,12.5L10.5,16L17,9");
            check.fill.reset();
            check.stroke = Brush::solid(colors::White);
            check.strokeLineWidth = 2.2f;
            check.strokeLineCap = StrokeCap::Round;
            check.strokeLineJoin = StrokeJoin::Round;
            return ImageVector::Builder(48, 48, 24, 24)
                .path("M2,12a10,10 0 1,0 20,0a10,10 0 1,0 -20,0z", Brush::solid(Color::hex(0x1E88E5)))  // a circle from two arcs
                .path(check)
                .build();
        }();
        static const ImageVector sun = [] {
            VectorPath rays;
            rays.commands = VectorPath::parse("M12,1v3M12,20v3M1,12h3M20,12h3M4.2,4.2l2.1,2.1M17.7,17.7l2.1,2.1M4.2,19.8l2.1,-2.1M17.7,6.3l2.1,-2.1");
            rays.fill.reset();
            rays.stroke = Brush::solid(Color::hex(0xF57C00));
            rays.strokeLineWidth = 2;
            rays.strokeLineCap = StrokeCap::Round;
            return ImageVector::Builder(48, 48, 24, 24)
                .path("M12,7C14.76,7 17,9.24 17,12C17,14.76 14.76,17 12,17C9.24,17 7,14.76 7,12C7,9.24 9.24,7 12,7Z", Brush::solid(Color::hex(0xFFCA28)))
                .path(rays)
                .build();
        }();
        static const ImageVector tile = ImageVector::Builder(48, 48, 24, 24)
                                            .path("M3,3h18v18h-18z", Brush::horizontalGradient({Color::hex(0x7E57C2), Color::hex(0xEC407A)}))
                                            .build();
        // A group turned about the middle and clipped to the circle: diagonal stripes on a ball.
        static const ImageVector ball = [] {
            const char* circle = "M2,12a10,10 0 1,0 20,0a10,10 0 1,0 -20,0z";
            ImageVector::Builder b(48, 48, 24, 24);
            b.path(circle, Brush::solid(Color::hex(0x26A69A)));
            b.group({.rotate = -35, .pivotX = 12, .pivotY = 12, .clipPathData = circle}, [](ImageVector::Builder& g) {
                for (int i = 0; i < 4; ++i) g.path(std::format("M-4,{}h32v2.5h-32z", 3 + i * 5.5), Brush::solid(colors::White.copy(0.85f)));
            });
            return b.build();
        }();
        Section("ImageVector::Builder · renkli vektörler", [&] {
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(16), .verticalAlignment = Alignment::CenterVertically}, [&] {
                Image(VectorPainter(badge), "Onaylı");
                Image(VectorPainter(sun), "Güneş");
                Image(VectorPainter(tile), "Degrade");
                Image(VectorPainter(ball), "Çizgili top");
                Icon(sun, "Tonlu güneş", Modifier().size(48), cs.primary);  // its rays (strokes) tinted too
            });
            // An Android vector drawable from the assets (vectors/compass.xml), read once.
            ImageVector compass = vectorResource("vectors/compass.xml");
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(16), .verticalAlignment = Alignment::CenterVertically}, [&] {
                Image(VectorPainter(compass), "Pusula");
                Image(VectorPainter(compass), "Büyük pusula", Modifier().size(96));
                Text("vectorResource: Android'in vector drawable XML'i (yaylar, döndürülmüş grup, çizgiler, degradeler)", Modifier().weight(1),
                     {.font = typography().bodyMedium});
            });
            // Composable vector content (rememberVectorPainter { … }): a ring, then a tick, drawn in
            // (trimPathEnd) as it is switched on.
            auto done = remember([] { return mutableStateOf(false); });
            float ring = animateFloatAsState(done.get() ? 1.f : 0.f, tween(500));
            float tick = animateFloatAsState(done.get() ? 1.f : 0.f, tween(350, done.get() ? 350 : 0));
            Painter check = rememberVectorPainter(48, 48, 24, 24, [&](ImageVector::Builder& v) {
                v.path(VectorPath{.commands = VectorPath::parse("M12,2a10,10 0 1,1 0,20a10,10 0 1,1 0,-20z"), .fill = std::nullopt,
                                  .stroke = Brush::solid(cs.primary), .strokeLineWidth = 2, .strokeLineCap = StrokeCap::Round, .trimPathEnd = ring});
                v.path(VectorPath{.commands = VectorPath::parse("M7,12.5l3.5,3.5l6.5,-7"), .fill = std::nullopt, .stroke = Brush::solid(cs.primary),
                                  .strokeLineWidth = 2.4f, .strokeLineCap = StrokeCap::Round, .strokeLineJoin = StrokeJoin::Round, .trimPathEnd = tick});
            });
            // An animated vector drawable from the assets: play turning into pause, and back.
            AnimatedImageVector playPause = animatedVectorResource("vectors/play_pause.xml");
            auto playing = remember([] { return mutableStateOf(false); });
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(16), .verticalAlignment = Alignment::CenterVertically}, [&] {
                Image(rememberAnimatedVectorPainter(playPause, playing.get()), playing.get() ? "Duraklat" : "Oynat",
                      Modifier().clip(CircleShape).clickable([playing] { playing.set(!playing.peek()); }));
                Text("animatedVectorResource + rememberAnimatedVectorPainter: <animated-vector> (yol morfu, dönüş, renk); dokunun",
                     Modifier().weight(1), {.font = typography().bodyMedium});
            });
            Row(Modifier().fillMaxWidth(), {.horizontalArrangement = Arrangement::spacedBy(16), .verticalAlignment = Alignment::CenterVertically}, [&] {
                Image(check, "Onay");
                Switch(done.get(), [done](bool on) { done.set(on); });
                Text("rememberVectorPainter { … }: trimPathEnd ile kendini çizen onay", Modifier().weight(1), {.font = typography().bodyMedium});
            });
            Text("Image kendi renkleriyle çizer (dolgu, çizgi, degrade, döndürülüp daireye kırpılmış grup); Icon tek tonla boyar.",
                 {.font = typography().bodyMedium, .color = cs.onSurfaceVariant});
        });
    });
}
