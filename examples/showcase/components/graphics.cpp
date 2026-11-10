// Components screen: progress, animations, images, shapes, blend and tile modes, blur, color vectors, shared elements.

#include "showcase.hpp"

// Progress indicators, an Animatable driven by a coroutine, and a bouncy spring.
@Composable
fun ProgressSection() {
    val scope = rememberCoroutineScope()
    val progress = remember { Animatable(0f) }
    var right by remember { mutableStateOf(false) }
    val x by animateFloatAsState(if (right) 1f else 0f, spring(dampingRatio = Spring.DampingRatioHighBouncy, stiffness = Spring.StiffnessLow))
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    Section("İlerleme ve animasyon") {
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
            CircularProgressIndicator()
            CircularProgressIndicator(progress = { progress.value })
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(14.dp)) {
                LinearProgressIndicator(modifier = Modifier.fillMaxWidth())
                LinearProgressIndicator(progress = { progress.value }, modifier = Modifier.fillMaxWidth())
            }
        }
        // M3 Expressive: the indicator waves while it moves (flat near the start and the end).
        Text("Dalgalı", style = typography.labelLarge)
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
            CircularWavyProgressIndicator()
            CircularWavyProgressIndicator(progress = { progress.value })
            Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                LinearWavyProgressIndicator(modifier = Modifier.fillMaxWidth())
                LinearWavyProgressIndicator(progress = { progress.value }, modifier = Modifier.fillMaxWidth())
            }
        }
        // M3 Expressive's loading indicator: a shape that turns and morphs (the determinate one into a burst).
        Text("Yükleme göstergesi", style = typography.labelLarge)
        Row(horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
            LoadingIndicator()
            ContainedLoadingIndicator()
            LoadingIndicator(progress = { progress.value })
            ContainedLoadingIndicator(progress = { progress.value })
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            Button(onClick = {
                scope.launch {
                    progress.snapTo(0f)
                    progress.animateTo(1f, tween(2000, easing = LinearEasing))
                }
            }) { Text("Yükle (Animatable)") }
            OutlinedButton(onClick = { right = !right }) { Text("Yay (spring)") }
        }
        Box(modifier = Modifier.fillMaxWidth().height(40.dp)) {
            Box(modifier = Modifier.offset(x = (x * 220).dp).size(40.dp).background(cs.primary, CircleShape))
        }
    }
}

// What the feed shows: loading, or loaded with so many posts.
data class Feed(val loaded: Boolean, val posts: Int)

@Composable
fun AnimationsSection() {
    var expanded by remember { mutableStateOf(false) }
    var count by remember { mutableStateOf(0) }
    var liked by remember { mutableStateOf(false) }
    var details by remember { mutableStateOf(false) }
    val pulse = remember { Animatable(1f) }  // the heart's beat: keyframes
    var feed by remember { mutableStateOf(Feed(false, 3)) }
    var panel by remember { mutableStateOf(false) }
    var rows by remember { mutableStateOf(listOf(1, 2)) }
    var nextRow by remember { mutableStateOf(3) }
    val scope = rememberCoroutineScope()
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    Section("AnimatedVisibility · AnimatedContent · Crossfade") {
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            Button(onClick = { expanded = !expanded }) { Text(if (expanded) "Gizle" else "Detaylar") }
            FilledTonalIconButton(onClick = { count-- }) { Icon(Icons.Filled.Remove, contentDescription = "Azalt") }
            // The digits come in from below when it grows, from above when it shrinks.
            AnimatedContent(
                targetState = count,
                transitionSpec = {
                    val way = if (targetState > initialState) 1f else -1f
                    (slideInVertically { way * it } + fadeIn()) togetherWith (slideOutVertically { -way * it } + fadeOut())
                },
            ) { n ->
                Text("$n", modifier = Modifier.width(48.dp), fontSize = 28.sp, fontWeight = FontWeight.Bold, color = cs.primary, textAlign = TextAlign.Center)
            }
            FilledTonalIconButton(onClick = { count++ }) { Icon(Icons.Filled.Add, contentDescription = "Artır") }
            Crossfade(targetState = liked) { on ->
                IconButton(onClick = {
                    liked = !liked
                    scope.launch {
                        pulse.animateTo(1f, keyframes {
                            durationMillis = 420
                            1.35f at 110
                            0.9f at 260
                        })
                    }
                }, modifier = Modifier.graphicsLayer { scaleX = pulse.value; scaleY = pulse.value }) {
                    Icon(if (on) Icons.Filled.Favorite else Icons.Filled.FavoriteBorder, contentDescription = "Beğen",
                         tint = if (on) cs.error else LocalContentColor.current)
                }
            }
        }
        AnimatedVisibility(visible = expanded) {
            Column(modifier = Modifier.fillMaxWidth().background(cs.primaryContainer, RoundedCornerShape(12.dp)).padding(16.dp),
                   verticalArrangement = Arrangement.spacedBy(6.dp)) {
                Text("AnimatedVisibility", color = cs.onPrimaryContainer, style = typography.titleMedium)
                Text("Bu kart fadeIn + expandVertically ile açılır, fadeOut + shrinkVertically ile kapanır. " +
                         "Kapanınca içeriği kompozisyondan çıkar.",
                     color = cs.onPrimaryContainer, style = typography.bodyMedium)
            }
        }
        // SizeTransform: AnimatedContent grows and shrinks between its contents' sizes.
        AnimatedContent(
            targetState = details,
            modifier = Modifier.fillMaxWidth(),
            transitionSpec = { (fadeIn(animationSpec = tween(220, delayMillis = 90)) togetherWith fadeOut(animationSpec = tween(90))) using SizeTransform() },
            contentAlignment = Alignment.TopCenter,  // the closed one in the middle, the card grows from it
        ) { open ->
            if (!open) {
                Box(modifier = Modifier.padding(top = 8.dp).background(cs.secondaryContainer, RoundedCornerShape(20.dp)).clickable { details = true }.padding(16.dp, 10.dp)) {
                    Text("Özeti aç (SizeTransform)", color = cs.onSecondaryContainer, style = typography.labelLarge)
                }
            } else {
                Column(modifier = Modifier.padding(top = 8.dp).fillMaxWidth().background(cs.secondaryContainer, RoundedCornerShape(16.dp)).clickable { details = false }.padding(16.dp),
                       verticalArrangement = Arrangement.spacedBy(6.dp)) {
                    Text("AnimatedContent · SizeTransform", color = cs.onSecondaryContainer, style = typography.titleMedium)
                    Text("Kutu, eski içeriğin boyutundan yenisininkine yayla büyür; içerik o boyuta kırpılır. Kapatmak için dokunun.",
                         color = cs.onSecondaryContainer, style = typography.bodyMedium)
                }
            }
        }
        // contentKey: loading → loaded slides; new posts on the loaded screen just update it.
        LaunchedEffect(feed.loaded) {  // a load takes a moment
            if (feed.loaded) return@LaunchedEffect
            delay(1500)
            feed = feed.copy(loaded = true)
        }
        Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            OutlinedButton(onClick = { feed = feed.copy(loaded = false) }, enabled = feed.loaded) { Text("Yeniden yükle") }
            OutlinedButton(onClick = { feed = feed.copy(posts = feed.posts + 1) }, enabled = feed.loaded) { Text("Yeni gönderi") }
        }
        AnimatedContent(
            targetState = feed,
            modifier = Modifier.fillMaxWidth(),
            transitionSpec = { (slideInVertically { it } + fadeIn()) togetherWith (slideOutVertically { -it } + fadeOut()) },
            contentKey = { it.loaded },
        ) { f ->
            Row(modifier = Modifier.fillMaxWidth().height(40.dp), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
                if (!f.loaded) {
                    CircularProgressIndicator(modifier = Modifier.size(20.dp))
                    Text("Yükleniyor…", color = cs.onSurfaceVariant, style = typography.bodyMedium)
                } else {
                    Icon(Icons.Filled.CheckCircle, contentDescription = "Yüklendi", tint = cs.primary)
                    Text("${f.posts} gönderi (contentKey: yeni veri geçişsiz)", style = typography.bodyMedium)
                }
            }
        }
        // animateEnterExit: the content itself comes and goes at once; inside it the scrim
        // fades and the panel slides, each its own way, and it stays until both are out.
        OutlinedButton(onClick = { panel = !panel }) { Text(if (panel) "Paneli kapat" else "Panel (animateEnterExit)") }
        AnimatedVisibility(visible = panel, modifier = Modifier.fillMaxWidth(), enter = EnterTransition.None, exit = ExitTransition.None) {
            Box(modifier = Modifier.fillMaxWidth().height(140.dp).clip(RoundedCornerShape(16.dp))) {
                Box(modifier = Modifier.animateEnterExit(enter = fadeIn(animationSpec = tween(300)), exit = fadeOut(animationSpec = tween(300)))
                        .matchParentSize().background(cs.onSurface.copy(alpha = 0.2f)))
                Box(modifier = Modifier.animateEnterExit(enter = slideInVertically(animationSpec = tween(450)) { it }, exit = slideOutVertically(animationSpec = tween(450)) { it })
                        .align(Alignment.BottomCenter)
                        .fillMaxWidth()
                        .height(90.dp)
                        .background(cs.primaryContainer, RoundedCornerShape(16.dp))
                        .padding(16.dp)) {
                    Text("Panel aşağıdan kayar, arka plan ayrıca solar; kapanırken ikisi de bitene dek kalır.",
                         color = cs.onPrimaryContainer, style = typography.bodyMedium)
                }
            }
        }
        // MutableTransitionState: a row made hidden with a visible target comes in as it first
        // shows (each one added); its currentState and isIdle say where it is, and a row taken
        // away is removed from the list once it is all gone.
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp), verticalAlignment = Alignment.CenterVertically) {
            OutlinedButton(onClick = {
                rows = rows + nextRow
                nextRow++
            }) { Text("Satır ekle") }
            Text("MutableTransitionState", color = cs.onSurfaceVariant, style = typography.labelLarge)
        }
        for (row in rows)
            key(row) {
                val shown = remember { MutableTransitionState(false).apply { targetState = true } }
                if (shown.isIdle && !shown.currentState)  // all gone: off the list
                    SideEffect { rows = rows - row }
                AnimatedVisibility(visibleState = shown, modifier = Modifier.fillMaxWidth(),
                                   enter = fadeIn(animationSpec = tween(500)) + expandVertically(animationSpec = tween(500)),
                                   exit = fadeOut(animationSpec = tween(500)) + shrinkVertically(animationSpec = tween(500))) {
                    Row(modifier = Modifier.fillMaxWidth().padding(vertical = 4.dp).background(cs.surfaceContainerHigh, RoundedCornerShape(12.dp)).padding(16.dp, 6.dp, 4.dp, 6.dp),
                        verticalAlignment = Alignment.CenterVertically) {
                        val where = if (shown.isIdle) "duruyor" else if (shown.targetState) "beliriyor" else "kayboluyor"
                        Text("Satır $row · $where", modifier = Modifier.weight(1f), style = typography.bodyMedium)
                        IconButton(onClick = { shown.targetState = false }) { Icon(Icons.Filled.Close, contentDescription = "Kaldır") }
                    }
                }
            }
    }
}

// Images from assets and from code, ContentScale, shapes and clipping.
// AsyncImage: JPEG and WebP assets loaded away from the UI thread, cross-fading in from a
// placeholder, and a big photo from the network when asked for, decoded at its box's size and
// kept on disk.
@Composable
fun AsyncImageSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var url by remember { mutableStateOf("https://picsum.photos/seed/kompoz/2400/1600") }
    var requested by remember { mutableStateOf("") }
    var decoded by remember { mutableStateOf("") }
    val placeholder: @Composable () -> Unit = {
        Box(modifier = Modifier.fillMaxSize().background(cs.surfaceContainerHighest), contentAlignment = Alignment.Center) {
            CircularProgressIndicator(modifier = Modifier.size(24.dp))
        }
    }
    Section("AsyncImage · JPEG · WebP · ağ") {
        Text("Resimler arka planda yüklenip çözülür: masaüstünde libjpeg-turbo, libwebp ve libcurl, Android'de " +
                 "BitmapFactory, web'de tarayıcı, Windows'ta WIC. Gelene kadar yer tutucu, sonra geçişle belirir. " +
                 "Büyük resim kutusunun boyutunda çözülür; yüklenenler bellekte, indirilenler diskte kalır " +
                 "(uygulama yeniden açılınca ağsız gelir).",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(8.dp)) {
            AsyncImage(model = "images/mountains.jpg", contentDescription = "Dağlar (JPEG)", modifier = Modifier.weight(1f).height(110.dp).clip(RoundedCornerShape(12.dp)),
                       contentScale = ContentScale.Crop, placeholder = placeholder, crossfadeMillis = 300)
            AsyncImage(model = "images/lake.webp", contentDescription = "Göl (WebP)", modifier = Modifier.weight(1f).height(110.dp).clip(RoundedCornerShape(12.dp)),
                       contentScale = ContentScale.Crop, placeholder = placeholder, crossfadeMillis = 300)
        }
        OutlinedTextField(value = url, onValueChange = { url = it }, modifier = Modifier.fillMaxWidth(), singleLine = true, label = { Text("Adres") })
        Button(onClick = { requested = url }) { Text("Ağdan yükle") }
        if (requested.isNotEmpty()) {
            AsyncImage(model = requested, contentDescription = "Ağdan bir resim", modifier = Modifier.fillMaxWidth().height(170.dp).clip(RoundedCornerShape(12.dp)),
                       contentScale = ContentScale.Crop, placeholder = placeholder, crossfadeMillis = 300,
                       error = {
                           Box(modifier = Modifier.fillMaxSize().background(cs.surfaceContainerHighest), contentAlignment = Alignment.Center) { Text("Yüklenemedi", color = cs.error) }
                       },
                       onState = { s ->
                           if (s.status == AsyncImageState.Status.Success) decoded = "Çözülen boyut: ${s.image.width}×${s.image.height} piksel"
                       })
            if (decoded.isNotEmpty()) Text(decoded, color = cs.onSurfaceVariant, style = typography.bodySmall)
        }
    }
}

// A 64 × 64 checkerboard made pixel by pixel (ImageBitmap::fromPixels takes straight RGBA bytes).
ImageBitmap checkerBitmap() {
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
}

@Composable
fun ImagesSection() {
    val photo = imageResource("images/landscape.png")
    val generated = remember { checkerBitmap() }
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    Section("Image, Shape, clip · Painter") {
        Image(bitmap = photo, contentDescription = "Manzara", modifier = Modifier.fillMaxWidth().height(150.dp).clip(RoundedCornerShape(16.dp)),
              contentScale = ContentScale.Crop)
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            for ((scale, name) in listOf(ContentScale.Fit to "Fit", ContentScale.Crop to "Crop", ContentScale.FillBounds to "FillBounds", ContentScale.None to "None")) {
                Column(verticalArrangement = Arrangement.spacedBy(4.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                    Image(bitmap = photo, contentDescription = name, modifier = Modifier.size(72.dp, 56.dp).background(cs.outlineVariant).border(1.dp, cs.outline),
                          contentScale = scale)
                    Text(name, fontSize = 10.sp, color = cs.onSurfaceVariant)
                }
            }
        }
        // ColorFilter: gray (saturation 0), sepia (a color matrix), a tint (SrcIn: the shape in
        // one color, SrcAtop: a wash over it) and a light that multiplies and adds.
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceBetween) {
            val gray = ColorMatrix().apply { setToSaturation(0f) }
            val sepia = ColorMatrix(floatArrayOf(0.393f, 0.769f, 0.189f, 0f, 0f, 0.349f, 0.686f, 0.168f, 0f, 0f, 0.272f, 0.534f, 0.131f, 0f, 0f, 0f, 0f, 0f, 1f, 0f))
            val filters = listOf<Pair<ColorFilter?, String>>(
                null to "yok",
                ColorFilter.colorMatrix(gray) to "gri",
                ColorFilter.colorMatrix(sepia) to "sepya",
                ColorFilter.tint(cs.primary.copy(alpha = 0.45f), BlendMode.SrcAtop) to "SrcAtop",
                ColorFilter.lighting(Color(1f, 0.7f, 0.7f), Color(0.15f, 0f, 0f, 0f)) to "lighting")
            for ((filter, name) in filters)
                Column(verticalArrangement = Arrangement.spacedBy(4.dp), horizontalAlignment = Alignment.CenterHorizontally) {
                    Image(bitmap = photo, contentDescription = name, modifier = Modifier.size(56.dp).clip(RoundedCornerShape(12.dp)), contentScale = ContentScale.Crop, colorFilter = filter)
                    Text(name, fontSize = 10.sp, color = cs.onSurfaceVariant)
                }
        }
        Row(horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Image(bitmap = photo, contentDescription = "Avatar", modifier = Modifier.size(64.dp).clip(CircleShape).border(2.dp, cs.primary, CircleShape),
                  contentScale = ContentScale.Crop)
            Image(bitmap = generated, contentDescription = "Kodla üretilmiş", modifier = Modifier.size(64.dp).clip(RoundedCornerShape(12.dp)),
                  contentScale = ContentScale.FillBounds)
            Box(modifier = Modifier.size(64.dp).alpha(0.5f).background(cs.primary, CircleShape), contentAlignment = Alignment.Center) {
                Icon(Icons.Filled.Star, contentDescription = "Yarı saydam", modifier = Modifier.size(32.dp), tint = cs.onPrimary)
            }
            Text("CircleShape, RoundedCornerShape, alpha(0.5)", modifier = Modifier.weight(1f), fontSize = 12.sp, color = cs.onSurfaceVariant)
        }
        // Corners cut straight (CutCornerShape), and a ticket with its start corners cut further.
        Row(horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Image(bitmap = photo, contentDescription = "Kesik köşeli", modifier = Modifier.size(64.dp).clip(CutCornerShape(16.dp)).border(2.dp, cs.tertiary, CutCornerShape(16.dp)),
                  contentScale = ContentScale.Crop)
            Box(modifier = Modifier.height(48.dp).width(120.dp).background(cs.tertiaryContainer, CutCornerShape(20.dp, 6.dp, 6.dp, 20.dp)), contentAlignment = Alignment.Center) {
                Text("Bilet", color = cs.onTertiaryContainer)
            }
            Text("CutCornerShape", modifier = Modifier.weight(1f), fontSize = 12.sp, color = cs.onSurfaceVariant)
        }
        // Modifier.magnifier: a finger on the photo shows what is under it, bigger, above it.
        var lens by remember { mutableStateOf(Offset.Unspecified) }
        Box(modifier = Modifier
                .fillMaxWidth()
                .height(120.dp)
                .clip(RoundedCornerShape(16.dp))
                .magnifier(sourceCenter = { lens }, zoom = 2f, size = DpSize(120.dp, 80.dp), cornerRadius = 16.dp)
                .pointerInput(Unit) {
                    detectDragGestures(onDragStart = { lens = it },
                                       onDrag = { change, _ -> lens = change.position },
                                       onDragEnd = { lens = Offset.Unspecified },
                                       onDragCancel = { lens = Offset.Unspecified })
                }) {
            Image(bitmap = photo, contentDescription = "Büyüteçle bakın", modifier = Modifier.fillMaxSize(), contentScale = ContentScale.Crop)
            Text("Modifier.magnifier: parmağınızı sürükleyin", modifier = Modifier.align(Alignment.BottomStart).padding(8.dp),
                 fontSize = 12.sp, color = Color.White, style = TextStyle(shadow = Shadow(Color(0f, 0f, 0f, 0.8f), Offset(1f, 1f), 3f)))
        }
        // Painter: an asset (painterResource), a vector, a brush and drawing of your own, as
        // an Image, an Icon or Modifier.paint behind content.
        Row(horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Image(painter = painterResource("images/landscape.png"), contentDescription = "painterResource", modifier = Modifier.size(64.dp).clip(RoundedCornerShape(12.dp)),
                  alignment = Alignment.Center, contentScale = ContentScale.Crop)
            Icon(painter = rememberVectorPainter(Icons.Filled.Favorite), contentDescription = "VectorPainter", modifier = Modifier.size(40.dp), tint = cs.tertiary)
            Box(modifier = Modifier.size(64.dp, 40.dp).clip(RoundedCornerShape(8.dp))
                    .paint(BrushPainter(Brush.horizontalGradient(listOf(cs.primary, cs.tertiary))), sizeToIntrinsics = false),
                contentAlignment = Alignment.Center) { Text("paint", color = cs.onPrimary, style = typography.labelLarge) }
            val rings = remember {
                object : Painter() {
                    override val intrinsicSize: Size get() = Size(56f, 56f)
                    override fun DrawScope.onDraw() {
                        for (i in 0 until 4)
                            drawCircle(hueColor(i * 5), size.width / 2 - i * 7, Offset(size.width / 2, size.height / 2))
                    }
                }
            }
            Image(painter = rings, contentDescription = "Painter", alignment = Alignment.Center, contentScale = ContentScale.Fit, alpha = 0.8f)
        }
    }
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
@Composable
fun MaterialShapesSection() {
    val cycle = listOf(MaterialShapes.Cookie9Sided, MaterialShapes.Clover4Leaf, MaterialShapes.Sunny, MaterialShapes.Heart, MaterialShapes.SoftBurst, MaterialShapes.Pill)
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var picked by remember { mutableStateOf("Cookie9Sided") }
    var step by remember { mutableStateOf(0) }
    val progress = remember { Animatable(1f) }
    val scope = rememberCoroutineScope()
    val n = cycle.size
    val morph = remember(step) { Morph(cycle[(step + n - 1) % n], cycle[step % n]) }
    Section("MaterialShapes · Morph") {
        Text("Material 3 Expressive'in 35 şekli; birine dokunun, adı görünsün. Büyük şekle dokununca sıradakine dönüşür (Morph).",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(modifier = Modifier
                    .size(96.dp)
                    .background(cs.tertiaryContainer, morph.toShape(progress.value))
                    .clickable {
                        step++
                        scope.launch {
                            progress.snapTo(0f)
                            progress.animateTo(1f, spring(0.6f, Spring.StiffnessLow))
                        }
                    },
                contentAlignment = Alignment.Center) { Text(if (step % n == 0) "Dokun" else "", color = cs.onTertiaryContainer) }
            Text("Seçilen: $picked", modifier = Modifier.weight(1f), style = typography.titleMedium)
        }
        FlowRow(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(10.dp), verticalArrangement = Arrangement.spacedBy(10.dp)) {
            for (shape in MaterialShapes.all()) {
                Box(modifier = Modifier
                        .size(44.dp)
                        .background(if (picked == shape.name) cs.primary else cs.secondary, shape.outline().toShape())
                        .clickable { picked = shape.name }
                        .semantics { contentDescription = shape.name })
            }
        }
    }
}

@Composable
fun ShapesSection() {
    val triangle = ShapeOutline(points = listOf(Offset(0.5f, 0f), Offset(1f, 1f), Offset(0f, 1f)))
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var taps by remember { mutableStateOf(0) }
    Section("Şekiller (OutlineShape)") {
        // dropShadow / innerShadow: a soft shadow under a card, a glow (spread) in a gradient
        // (a brush), and a shadow inside a well, as if its edge cast it.
        Row(modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp), horizontalArrangement = Arrangement.SpaceEvenly) {
            Box(modifier = Modifier
                    .size(88.dp, 64.dp)
                    .dropShadow(RoundedCornerShape(16.dp), Shadow(color = Color(0f, 0f, 0f, 0.35f), offset = Offset(0f, 6f), blurRadius = 10f))
                    .background(cs.surface, RoundedCornerShape(16.dp)),
                contentAlignment = Alignment.Center) { Text("dropShadow", style = typography.labelSmall) }
            Box(modifier = Modifier
                    .size(88.dp, 64.dp)
                    .dropShadow(CircleShape, Shadow(blurRadius = 12f, spread = 4f, brush = Brush.horizontalGradient(listOf(cs.primary, cs.tertiary)), alpha = 0.7f))
                    .background(cs.primary, CircleShape),
                contentAlignment = Alignment.Center) { Text("parıltı", color = cs.onPrimary, style = typography.labelSmall) }
            Box(modifier = Modifier
                    .size(88.dp, 64.dp)
                    .background(cs.surfaceContainerHigh, RoundedCornerShape(16.dp))
                    .innerShadow(RoundedCornerShape(16.dp), Shadow(color = Color(0f, 0f, 0f, 0.45f), offset = Offset(2f, 4f), blurRadius = 6f)),
                contentAlignment = Alignment.Center) { Text("innerShadow", style = typography.labelSmall) }
        }
        // The same shadows following an outline shape: a glow round a heart, a well in a cookie.
        Row(modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp), horizontalArrangement = Arrangement.SpaceEvenly) {
            Box(modifier = Modifier
                    .size(72.dp)
                    .dropShadow(MaterialShapes.Heart.toShape(), Shadow(color = cs.tertiary.copy(alpha = 0.7f), blurRadius = 10f, spread = 3f))
                    .background(cs.tertiary, MaterialShapes.Heart.toShape()))
            Box(modifier = Modifier
                    .size(72.dp)
                    .dropShadow(MaterialShapes.Cookie9Sided.toShape(), Shadow(color = Color(0f, 0f, 0f, 0.4f), offset = Offset(0f, 6f), blurRadius = 8f))
                    .background(cs.surface, MaterialShapes.Cookie9Sided.toShape()))
            Box(modifier = Modifier
                    .size(72.dp)
                    .background(cs.surfaceContainerHigh, MaterialShapes.Cookie9Sided.toShape())
                    .innerShadow(MaterialShapes.Cookie9Sided.toShape(), Shadow(color = Color(0f, 0f, 0f, 0.5f), offset = Offset(2f, 4f), blurRadius = 6f)))
        }
        // shadow(elevation, shape, clip, ambientColor, spotColor): a tinted shadow, the content
        // clipped to the shape; BorderStroke with a brush: a gradient border.
        Row(modifier = Modifier.fillMaxWidth().padding(vertical = 8.dp), horizontalArrangement = Arrangement.SpaceEvenly) {
            Box(modifier = Modifier.size(120.dp, 64.dp).shadow(elevation = 12.dp, shape = RoundedCornerShape(20.dp), clip = true, ambientColor = cs.primary, spotColor = cs.primary).background(cs.surface),
                contentAlignment = Alignment.Center) { Text("renkli gölge", style = typography.labelSmall) }
            Box(modifier = Modifier
                    .size(120.dp, 64.dp)
                    .border(BorderStroke(3.dp, Brush.linearGradient(listOf(cs.primary, cs.tertiary, Color(0xFFFFB300)))), RoundedCornerShape(20.dp)),
                contentAlignment = Alignment.Center) { Text("BorderStroke", style = typography.labelSmall) }
        }
        Text("Köşeleri yuvarlak dikdörtgen olmayan şekiller: resim yıldıza kırpılır; düğmenin arka planı, gölgesi ve dalgası " +
                 "şeklin içinde kalır; çerçeve şekli izler.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.SpaceEvenly, verticalAlignment = Alignment.CenterVertically) {
            AsyncImage(model = "images/mountains.jpg", contentDescription = "Yıldız dağlar", modifier = Modifier.size(96.dp).clip(OutlineShape(starOutline())),
                       contentScale = ContentScale.Crop)
            Box(modifier = Modifier
                    .size(88.dp)
                    .shadow(6.dp, OutlineShape(starOutline()))
                    .background(cs.primaryContainer, OutlineShape(starOutline()))
                    .clickable { taps++ },
                contentAlignment = Alignment.Center) { Text("$taps", color = cs.onPrimaryContainer, style = typography.titleLarge) }
            Box(modifier = Modifier.size(80.dp, 70.dp).border(3.dp, cs.tertiary, OutlineShape(triangle)))
        }
    }
}

// SharedTransitionLayout: a list and the detail of a row; the picture and the title move
// between their places in the two.
@Composable
fun SharedElementSection() {
    var selected by remember { mutableStateOf(-1) }
    val palette = listOf(Color(0xFFB8705F), Color(0xFF6C9F6A), Color(0xFF5F82B8))
    val titles = listOf("Kızıl kayalar", "Yeşil vadi", "Mavi göl")
    val typography = MaterialTheme.typography
    Section("SharedTransitionLayout · sharedElement · sharedBounds") {
        SharedTransitionLayout(modifier = Modifier.fillMaxWidth().height(340.dp)) {
            AnimatedContent(targetState = selected) { item ->
                if (item < 0) {
                    Column(modifier = Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                        for (i in 0 until 3)
                            key(i) {
                                Row(modifier = Modifier.fillMaxWidth().clickable { selected = i }.padding(8.dp),
                                    horizontalArrangement = Arrangement.spacedBy(14.dp), verticalAlignment = Alignment.CenterVertically) {
                                    Box(modifier = Modifier.size(64.dp)
                                            .sharedElement(rememberSharedContentState(key = "photo$i"), animatedVisibilityScope = this@AnimatedContent)
                                            .background(palette[i], RoundedCornerShape(14.dp)))
                                    Text(titles[i], modifier = Modifier.sharedBounds(rememberSharedContentState(key = "title$i"), animatedVisibilityScope = this@AnimatedContent),
                                         style = typography.titleMedium)
                                }
                            }
                        Text("Bir satıra dokunun: resim ve başlık ayrıntıdaki yerlerine taşınır.",
                             modifier = Modifier.padding(8.dp, 4.dp), color = MaterialTheme.colorScheme.onSurfaceVariant, style = typography.bodyMedium)
                    }
                } else {
                    Column(modifier = Modifier.fillMaxWidth(), verticalArrangement = Arrangement.spacedBy(10.dp)) {
                        Box(modifier = Modifier.fillMaxWidth().height(170.dp)
                                .sharedElement(rememberSharedContentState(key = "photo$item"), animatedVisibilityScope = this@AnimatedContent)
                                .background(palette[item], RoundedCornerShape(24.dp)))
                        Text(titles[item], modifier = Modifier.sharedBounds(rememberSharedContentState(key = "title$item"), animatedVisibilityScope = this@AnimatedContent),
                             style = typography.headlineSmall)
                        Text("Geri dönünce ikisi listedeki yerlerine uçar.", style = typography.bodyMedium)
                        TextButton(onClick = { selected = -1 }) { Text("Geri") }
                    }
                }
            }
        }
    }
}

// BlendMode in an offscreen layer: a list whose top and bottom fade out (DstIn), and a hole
// punched through a card (DstOut).
@Composable
fun BlendModeSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    Section("BlendMode · CompositingStrategy.Offscreen") {
        Text("Liste kendi katmanında: üstte ve altta bir degradeyle (DstIn) kaybolur. Sağdaki kartta DstOut bir delik açar.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Row(modifier = Modifier.fillMaxWidth().height(180.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Box(modifier = Modifier.weight(1f).fillMaxHeight()
                    .graphicsLayer { compositingStrategy = CompositingStrategy.Offscreen }
                    .drawWithContent {
                        drawContent()
                        drawRect(brush = Brush.verticalGradient(listOf(Color.Transparent, Color.Black, Color.Black, Color.Transparent)), blendMode = BlendMode.DstIn)
                    }) {
                LazyColumn(modifier = Modifier.fillMaxSize(), contentPadding = PaddingValues(vertical = 16.dp)) {
                    items(40) { i ->
                        Text("Satır ${i + 1}", modifier = Modifier.fillMaxWidth().padding(8.dp, 6.dp), style = typography.bodyLarge)
                    }
                }
            }
            Box(modifier = Modifier.weight(1f).fillMaxHeight()
                    .graphicsLayer { compositingStrategy = CompositingStrategy.Offscreen }
                    .drawWithContent {
                        drawContent()
                        drawCircle(Color.Black, radius = 36f, blendMode = BlendMode.DstOut)
                    }
                    .background(Brush.linearGradient(listOf(cs.primary, cs.tertiary)), RoundedCornerShape(16.dp)),
                contentAlignment = Alignment.BottomCenter) {
                Text("DstOut", modifier = Modifier.padding(12.dp), color = cs.onPrimary, style = typography.titleMedium)
            }
        }
    }
}

// TileMode: a 60 dp gradient across 300 dp, past its end as each mode goes. Shaders: a photo
// mirrored as tiles (ImageShader), a gradient lit by a radial one (Brush.composite, Screen), a
// sweep made for each size (ShaderBrush), turned round its center.
@Composable
fun TileModeSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val photo = imageResource("images/landscape.png")
    Section("Brush · TileMode · Shader") {
        for ((mode, name) in listOf(TileMode.Clamp to "Clamp", TileMode.Repeated to "Repeated", TileMode.Mirror to "Mirror", TileMode.Decal to "Decal"))
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(name, modifier = Modifier.width(76.dp), style = typography.labelLarge)
                Box(modifier = Modifier
                        .weight(1f)
                        .height(32.dp)
                        .border(1.dp, cs.outlineVariant, RoundedCornerShape(8.dp))
                        .background(Brush.horizontalGradient(listOf(cs.primary, cs.tertiaryContainer), 0f, 60f, mode), RoundedCornerShape(8.dp)))
            }
        Row(modifier = Modifier.fillMaxWidth().height(72.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            val small = Matrix().apply { scale(0.1f, 0.1f) }
            Box(modifier = Modifier.weight(1f).fillMaxHeight().background(
                Brush.shader(ImageShader(photo, TileMode.Mirror, TileMode.Mirror).transformed(small)), RoundedCornerShape(12.dp)))
            Box(modifier = Modifier.weight(1f).fillMaxHeight().background(
                Brush.composite(Brush.linearGradient(listOf(cs.primary, cs.tertiary)),
                                Brush.radialGradient(listOf(Color.White.copy(alpha = 0.9f), Color.Transparent), Offset(24f, 20f), 48f), BlendMode.Screen),
                RoundedCornerShape(12.dp)))
            Box(modifier = Modifier.weight(1f).fillMaxHeight().background(
                Brush.shader { size ->
                    val turn = Matrix().apply { rotateZ(-90f) }
                    val center = Matrix().apply { translate(size.width / 2, size.height / 2) }
                    SweepGradientShader(Offset.Zero, listOf(cs.primary, Color(0xFFFFB300), cs.tertiary, cs.primary)).transformed(turn).transformed(center)
                },
                RoundedCornerShape(12.dp)))
        }
    }
}

// Colors as Compose's: red to blue mixed by lerp (in Oklab, lightness and hue along) and, beside
// it, by their sRGB numbers as a gradient mixes them; colors made in other color spaces (Display
// P3, CIE L*a*b*, Oklab) shown in sRGB, their sRGB said; a color animated (animateColorAsState,
// in Oklab as Compose's Color.VectorConverter).
@Composable
fun ColorSpaceSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var warm by remember { mutableStateOf(false) }
    val animated by animateColorAsState(if (warm) Color(0xFFFF6D00) else Color(0xFF2962FF), tween(1200))
    Section("Color · ColorSpace · Oklab") {
        val strip: @Composable (String, (Float) -> Color) -> Unit = { name, colorAt ->
            Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
                Text(name, modifier = Modifier.width(104.dp), style = typography.labelLarge)
                Row(modifier = Modifier.weight(1f).height(32.dp).clip(RoundedCornerShape(8.dp))) {
                    for (i in 0..8) Box(modifier = Modifier.weight(1f).fillMaxHeight().background(colorAt(i / 8f)))
                }
            }
        }
        strip("lerp (Oklab)") { t -> lerp(Color.Red, Color.Blue, t) }
        strip("sRGB sayıları") { t -> Color(1 - t, 0f, t) }
        val made = listOf(
            Color(0.1f, 0.75f, 0.45f, 1f, ColorSpaces.DisplayP3) to "Display P3",
            Color(55f, 60f, -40f, 1f, ColorSpaces.CieLab) to "CIE L*a*b*",
            Color(0.7f, -0.1f, 0.15f, 1f, ColorSpaces.Oklab) to "Oklab")
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            for ((color, name) in made)
                Column(modifier = Modifier.weight(1f), verticalArrangement = Arrangement.spacedBy(4.dp)) {
                    Box(modifier = Modifier.fillMaxWidth().height(40.dp).background(color, RoundedCornerShape(8.dp)))
                    Text(name, style = typography.labelLarge)
                    Text("sRGB #%06X".format(color.toArgb() and 0xFFFFFF), color = cs.onSurfaceVariant, style = typography.bodySmall)
                }
        }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(12.dp), verticalAlignment = Alignment.CenterVertically) {
            Box(modifier = Modifier.size(40.dp).background(animated, CircleShape))
            Button(onClick = { warm = !warm }) { Text("Rengi canlandır (Oklab'da)") }
        }
    }
}

// graphics::Canvas through drawIntoCanvas: twelve spokes each drawn after save, rotate (about the
// middle) and restore; a grid skewed; two circles faded together in a layer (saveLayer: where they
// overlap only the one on top shows); a dashed stroke and a corner-rounded fill in Paints. Meshes
// (drawVertices): a fan of colors round a white middle, and the photo on a waving strip, its
// colors multiplied by the vertices'. Bitmaps drawn into: rings drawn once into one, shown by
// Image; a circle and a line anti-aliased and not (isAntiAlias false); a star in an Alpha8 one,
// drawn in two paints' colors.
@Composable
fun CanvasSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    val photo = imageResource("images/landscape.png")
    val primary = cs.primary
    val tertiary = cs.tertiary
    val outline = cs.outline
    Section("Canvas · Paint · drawVertices") {
        Row(modifier = Modifier.fillMaxWidth().height(96.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Canvas(modifier = Modifier.weight(1f).fillMaxHeight()) {
                drawIntoCanvas { c ->
                    val spoke = Paint(color = primary, style = PaintingStyle.Stroke, strokeWidth = 4f, strokeCap = StrokeCap.Round)
                    for (i in 0 until 12) {
                        spoke.color = lerp(primary, tertiary, i / 11f)
                        c.save()
                        c.rotate(i * 30f, size.width / 2, size.height / 2)
                        c.drawLine(Offset(size.width / 2, size.height / 2 - 18), Offset(size.width / 2, size.height / 2 - 40), spoke)
                        c.restore()
                    }
                }
            }
            Canvas(modifier = Modifier.weight(1f).fillMaxHeight()) {
                drawIntoCanvas { c ->
                    val line = Paint(color = outline, style = PaintingStyle.Stroke, strokeWidth = 1f)
                    c.translate(24f, 8f)
                    c.skew(-0.4f, 0f)
                    for (i in 0..8) {
                        c.drawLine(Offset(i * 10f, 0f), Offset(i * 10f, 80f), line)
                        c.drawLine(Offset(0f, i * 10f), Offset(80f, i * 10f), line)
                    }
                }
            }
            Canvas(modifier = Modifier.weight(1f).fillMaxHeight().background(cs.secondaryContainer, RoundedCornerShape(12.dp))) {
                drawIntoCanvas { c ->
                    val faded = Paint()
                    faded.alpha = 0.6f
                    c.saveLayer(Rect(0f, 0f, size.width, size.height), faded)
                    c.drawCircle(Offset(size.width / 2 - 14, size.height / 2), 26f, Paint(color = primary))
                    c.drawCircle(Offset(size.width / 2 + 14, size.height / 2), 26f, Paint(color = Color(0xFFFFB300)))
                    c.restore()
                }
            }
            Canvas(modifier = Modifier.weight(1f).fillMaxHeight()) {
                drawIntoCanvas { c ->
                    c.drawRect(Rect(8f, 8f, size.width - 8, size.height / 2 - 4),
                               Paint(color = tertiary, pathEffect = PathEffect.cornerPathEffect(10f)))
                    c.drawRect(Rect(8f, size.height / 2 + 4, size.width - 8, size.height - 8),
                               Paint(color = primary, style = PaintingStyle.Stroke, strokeWidth = 3f, pathEffect = PathEffect.dashPathEffect(floatArrayOf(8f, 5f))))
                }
            }
        }
        Row(modifier = Modifier.fillMaxWidth().height(96.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Canvas(modifier = Modifier.size(96.dp)) {
                // A fan of 72 triangles, a little bigger than the circle it is cut to: a mesh isn't
                // anti-aliased (as Skia's), a clip is.
                val points = mutableListOf(Offset(48f, 48f))
                val colors = mutableListOf(Color.White)
                val ring = listOf(primary, Color(0xFFFFB300), tertiary, Color(0xFF26A69A), primary)
                for (i in 0..72) {
                    val a = i / 72f * 2 * PI.toFloat()
                    points.add(Offset(48 + 46 * cos(a), 48 + 46 * sin(a)))
                    val t = i / 72f * 4
                    val k = min(3, t.toInt())
                    colors.add(lerp(ring[k], ring[k + 1], t - k))
                }
                val fan = Vertices(VertexMode.TriangleFan, points, emptyList(), colors)
                val round = Path()
                round.addOval(Rect(4f, 4f, 92f, 92f))
                drawIntoCanvas { c ->
                    c.save()
                    c.clipPath(round)
                    c.drawVertices(fan, BlendMode.Modulate, Paint(color = Color.White))
                    c.restore()
                }
            }
            Canvas(modifier = Modifier.weight(1f).fillMaxHeight()) {
                if (photo.width == 0) return@Canvas
                val points = mutableListOf<Offset>()
                val tex = mutableListOf<Offset>()
                val colors = mutableListOf<Color>()
                for (i in 0..16) {
                    val t = i / 16f
                    val x = 8 + t * (size.width - 16)
                    val wave = 8 * sin(t * 2 * PI.toFloat())
                    points.add(Offset(x, 12 + wave))
                    points.add(Offset(x, size.height - 12 + wave))
                    tex.add(Offset(t * photo.width, 0f))
                    tex.add(Offset(t * photo.width, photo.height.toFloat()))
                    val c = lerp(Color.White, primary, t)
                    colors.add(c)
                    colors.add(c)
                }
                val strip = Vertices(VertexMode.TriangleStrip, points, tex, colors)
                drawIntoCanvas { c -> c.drawVertices(strip, BlendMode.Multiply, Paint(shader = ImageShader(photo))) }
            }
        }
        // Bitmaps drawn into (Canvas(ImageBitmap)): a pattern drawn once and shown by Image;
        // an Alpha8 one (its alpha alone) drawn in the paint's color, twice.
        val pattern = remember {
            val b = ImageBitmap(96, 96)
            val c = Canvas(b)
            for (i in 0 until 6)  // (inside the bitmap: an edge at its own edge would be cut flat there)
                c.drawCircle(Offset(48f, 48f), 46f - i * 8, Paint(color = if (i % 2 == 1) tertiary else primary))
            b
        }
        val stencil = remember {
            val b = ImageBitmap(96, 96, ImageBitmapConfig.Alpha8)
            val c = Canvas(b)
            val star = Path()
            for (i in 0 until 10) {
                val a = i * PI.toFloat() / 5 - PI.toFloat() / 2
                val r = if (i % 2 == 1) 18f else 44f
                if (i == 0) star.moveTo(48 + r * cos(a), 48 + r * sin(a)) else star.lineTo(48 + r * cos(a), 48 + r * sin(a))
            }
            star.close()
            c.drawPath(star, Paint(color = Color.Black.copy(alpha = 0.9f)))
            b
        }
        Row(modifier = Modifier.fillMaxWidth().height(96.dp), horizontalArrangement = Arrangement.spacedBy(12.dp)) {
            Image(bitmap = pattern, contentDescription = "Bitmap", modifier = Modifier.size(96.dp))
            // Not anti-aliased (isAntiAlias false) beside anti-aliased: a circle and a line.
            Canvas(modifier = Modifier.size(72.dp, 96.dp)) {
                drawIntoCanvas { c ->
                    for (smooth in listOf(true, false)) {
                        val top = if (smooth) 0f else 48f
                        val p = Paint(color = tertiary, isAntiAlias = smooth)
                        c.drawCircle(Offset(20f, top + 22), 17f, p)
                        p.strokeWidth = 2f
                        c.drawLine(Offset(42f, top + 40), Offset(70f, top + 6), p)
                    }
                }
            }
            Canvas(modifier = Modifier.size(200.dp, 96.dp)) {
                drawIntoCanvas { c ->
                    c.drawImage(stencil, Offset.Zero, Paint(color = primary))
                    c.drawImage(stencil, Offset(100f, 0f), Paint(color = Color(0xFFFFB300)))
                }
            }
        }
        Text("Renk yelpazesi: köşe renkli drawVertices (Skia gibi örgü yumuşatılmaz; daireye kırpıldı) · fotoğraf: doku " +
                 "koordinatlı şerit · halkalar: bitmap'e çizildi · daire ve çizgi: üstte yumuşatmalı, altta isAntiAlias = false " +
                 "(her piksel ya tam ya hiç, Skia gibi) · yıldızlar: Alpha8 maske, boyanın renginde",
             color = cs.onSurfaceVariant, style = typography.bodySmall)
    }
}

// Modifier.blur: a photo blurred as much as the slider says, kept to its bounds or spreading out;
// another blurred progressively, more towards the bottom; one blurred by a shader's alpha, more
// towards its edges; a frosted glass panel (Morph) and a card whose picture alone is blurred
// (Self).
@Composable
fun BlurSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    var radius by remember { mutableStateOf(8f) }
    var unbounded by remember { mutableStateOf(false) }
    val photo = imageResource("images/landscape.png")
    Section("Modifier.blur") {
        Text("Yarıçap: %.0f dp".format(radius), color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Slider(value = radius, onValueChange = { radius = it }, modifier = Modifier.fillMaxWidth(), valueRange = 0f..32f)
        Row(verticalAlignment = Alignment.CenterVertically) {
            Switch(checked = unbounded, onCheckedChange = { unbounded = it })
            Text("Unbounded (dışa taşar)", modifier = Modifier.padding(start = 12.dp))
        }
        val edge = if (unbounded) BlurredEdgeTreatment.Unbounded else BlurredEdgeTreatment.Rectangle
        Box(modifier = Modifier.fillMaxWidth().height(160.dp).padding(16.dp), contentAlignment = Alignment.Center) {
            Image(bitmap = photo, contentDescription = "Manzara",
                  modifier = Modifier.fillMaxSize().blur(radius.dp, edge).clip(RoundedCornerShape(16.dp)),
                  contentScale = ContentScale.Crop)
            Text("Buzlu cam", color = Color.White, style = typography.headlineSmall)
        }
        // Progressive (Compose 1.13): sharp at the top, as blurred as the slider says at the bottom.
        Text("Kademeli: üstte keskin, altta bulanık", color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Box(modifier = Modifier.fillMaxWidth().height(160.dp).padding(16.dp)) {
            Image(bitmap = photo, contentDescription = "Manzara",
                  modifier = Modifier.fillMaxSize().blur(BlurRadiusSpec.verticalGradient(0.dp, radius.dp), edge).clip(RoundedCornerShape(16.dp)),
                  contentScale = ContentScale.Crop)
        }
        // By a shader: sharp in the middle, as blurred as the slider says at the edges.
        Text("Shader ile: ortası keskin, kenarları bulanık", color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Box(modifier = Modifier.fillMaxWidth().height(160.dp).padding(16.dp)) {
            Image(bitmap = photo, contentDescription = "Manzara",
                  modifier = Modifier.fillMaxSize()
                      .blur(BlurRadiusSpec.shader(radius.dp) { _, size ->
                                RadialGradientShader(Offset(size.width / 2, size.height / 2), hypot(size.width, size.height) / 2,
                                                     listOf(Color.Transparent, Color.Transparent, Color.Black), listOf(0f, 0.35f, 1f))
                            }, edge)
                      .clip(RoundedCornerShape(16.dp)),
                  contentScale = ContentScale.Crop)
        }
        // Morph: what is behind a glass panel blurred, the panel's text sharp; Self: a card's
        // own picture blurred, its text sharp.
        Text("Morph: arkadaki bulanık, cam keskin · Self: kartın resmi bulanık, yazısı keskin",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
        Box(modifier = Modifier.fillMaxWidth().height(160.dp).padding(16.dp), contentAlignment = Alignment.Center) {
            Image(bitmap = photo, contentDescription = "Manzara", modifier = Modifier.fillMaxSize().clip(RoundedCornerShape(16.dp)), contentScale = ContentScale.Crop)
            Box(modifier = Modifier.size(200.dp, 72.dp)
                    .blur(radius.dp, BlurredEdgeTreatment(RoundedCornerShape(16.dp)), BlurType.Morph)
                    .background(Color.White.copy(alpha = 0.18f), RoundedCornerShape(16.dp))
                    .border(1.dp, Color.White.copy(alpha = 0.5f), RoundedCornerShape(16.dp)),
                contentAlignment = Alignment.Center) { Text("Buzlu cam", color = Color.White, style = typography.titleMedium) }
        }
        Box(modifier = Modifier.fillMaxWidth().height(120.dp).padding(16.dp, 0.dp, 16.dp, 16.dp)
                .blur(radius.dp, BlurredEdgeTreatment(RoundedCornerShape(16.dp)), BlurType.Self)
                .paint(BitmapPainter(photo), sizeToIntrinsics = false, alignment = Alignment.Center, contentScale = ContentScale.Crop),
            contentAlignment = Alignment.Center) { Text("Yazı keskin kalır", color = Color.White, style = typography.headlineSmall) }
    }
}

// ImageVector::Builder: vectors of paths in paints of their own (fills, strokes, a gradient),
// drawn as they are by Image and tinted by Icon.
val BadgeVector = ImageVector.Builder("badge", 48.dp, 48.dp, 24f, 24f)
    .addPath(pathData = addPathNodes("M2,12a10,10 0 1,0 20,0a10,10 0 1,0 -20,0z"),  // a circle from two arcs
             fill = Brush.solid(Color(0xFF1E88E5)))
    .path(stroke = Brush.solid(Color.White), strokeLineWidth = 2.2f, strokeLineCap = StrokeCap.Round, strokeLineJoin = StrokeJoin.Round) {
        moveTo(7f, 12.5f)
        lineTo(10.5f, 16f)
        lineTo(17f, 9f)
    }
    .build()
val SunVector = ImageVector.Builder("sun", 48.dp, 48.dp, 24f, 24f)
    .addPath(pathData = addPathNodes("M12,7C14.76,7 17,9.24 17,12C17,14.76 14.76,17 12,17C9.24,17 7,14.76 7,12C7,9.24 9.24,7 12,7Z"),
             fill = Brush.solid(Color(0xFFFFCA28)))
    .addPath(pathData = addPathNodes("M12,1v3M12,20v3M1,12h3M20,12h3M4.2,4.2l2.1,2.1M17.7,17.7l2.1,2.1M4.2,19.8l2.1,-2.1M17.7,6.3l2.1,-2.1"),
             stroke = Brush.solid(Color(0xFFF57C00)), strokeLineWidth = 2f, strokeLineCap = StrokeCap.Round)
    .build()
val TileVector = ImageVector.Builder("tile", 48.dp, 48.dp, 24f, 24f)
    .addPath(pathData = addPathNodes("M3,3h18v18h-18z"), fill = Brush.horizontalGradient(listOf(Color(0xFF7E57C2), Color(0xFFEC407A))))
    .build()
// A group turned about the middle and clipped to the circle: diagonal stripes on a ball.
val BallCircle = addPathNodes("M2,12a10,10 0 1,0 20,0a10,10 0 1,0 -20,0z")
val BallVector = ImageVector.Builder("ball", 48.dp, 48.dp, 24f, 24f)
    .addPath(pathData = BallCircle, fill = Brush.solid(Color(0xFF26A69A)))
    .group(name = "çizgiler", rotation = -35f, pivotX = 12f, pivotY = 12f, clipPathData = BallCircle) {
        for (i in 0 until 4)
            addPath(pathData = addPathNodes("M-4,${3 + i * 5.5f}h32v2.5h-32z"), fill = Brush.solid(Color.White.copy(alpha = 0.85f)))
    }
    .build()

// A VectorConfig that turns a group: what RenderVectorGroup draws "çizgiler" with.
struct Turn final : vector::VectorConfig {
    float degrees;
    explicit Turn(float d) : degrees(d) {}
    using VectorConfig::getOrDefault;
    float getOrDefault(vector::VectorPropertyOf<float> property, float defaultValue) const override {
        return property == vector::VectorProperty::Rotation ? degrees : defaultValue;
    }
};
std::shared_ptr<const vector::VectorConfig> turnConfig(float degrees) { return std::make_shared<Turn>(degrees); }

@Composable
fun ColorVectorSection() {
    val cs = MaterialTheme.colorScheme
    val typography = MaterialTheme.typography
    Section("ImageVector::Builder · renkli vektörler") {
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Image(painter = rememberVectorPainter(BadgeVector), contentDescription = "Onaylı")
            Image(painter = rememberVectorPainter(SunVector), contentDescription = "Güneş")
            Image(painter = rememberVectorPainter(TileVector), contentDescription = "Degrade")
            Image(painter = rememberVectorPainter(BallVector), contentDescription = "Çizgili top")
            Icon(SunVector, contentDescription = "Tonlu güneş", modifier = Modifier.size(48.dp), tint = cs.primary)  // its rays (strokes) tinted too
        }
        // An Android vector drawable from the assets (vectors/compass.xml), read once.
        val compass = vectorResource("vectors/compass.xml")
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Image(painter = rememberVectorPainter(compass), contentDescription = "Pusula")
            Image(painter = rememberVectorPainter(compass), contentDescription = "Büyük pusula", modifier = Modifier.size(96.dp))
            Text("vectorResource: Android'in vector drawable XML'i (yaylar, döndürülmüş grup, çizgiler, degradeler)", modifier = Modifier.weight(1f),
                 style = typography.bodyMedium)
        }
        // Composable vector content (rememberVectorPainter { Path() }): a ring, then a tick, drawn
        // in (trimPathEnd) as it is switched on; the ball's stripes turned with it (its group
        // drawn by RenderVectorGroup with a config of its own).
        var done by remember { mutableStateOf(false) }
        val ring by animateFloatAsState(if (done) 1f else 0f, tween(500))
        val tick by animateFloatAsState(if (done) 1f else 0f, tween(350, delayMillis = if (done) 350 else 0))
        val check = rememberVectorPainter(48.dp, 48.dp, 24f, 24f) { _, _ ->
            Path(pathData = addPathNodes("M12,2a10,10 0 1,1 0,20a10,10 0 1,1 0,-20z"), stroke = Brush.solid(cs.primary),
                 strokeLineWidth = 2f, strokeLineCap = StrokeCap.Round, trimPathEnd = ring)
            Path(pathData = addPathNodes("M7,12.5l3.5,3.5l6.5,-7"), stroke = Brush.solid(cs.primary), strokeLineWidth = 2.4f,
                 strokeLineCap = StrokeCap.Round, strokeLineJoin = StrokeJoin.Round, trimPathEnd = tick)
        }
        val turning = rememberVectorPainter(48.dp, 48.dp, 24f, 24f) { _, _ ->
            vector.RenderVectorGroup(BallVector.root, mapOf("çizgiler" to turnConfig(-35 + 90 * ring)))
        }
        // An animated vector drawable from the assets: play turning into pause, and back.
        val playPause = animatedVectorResource("vectors/play_pause.xml")
        var playing by remember { mutableStateOf(false) }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Image(painter = rememberAnimatedVectorPainter(playPause, playing), contentDescription = if (playing) "Duraklat" else "Oynat",
                  modifier = Modifier.clip(CircleShape).clickable { playing = !playing })
            Text("animatedVectorResource + rememberAnimatedVectorPainter: <animated-vector> (yol morfu, dönüş, renk); dokunun",
                 modifier = Modifier.weight(1f), style = typography.bodyMedium)
        }
        Row(modifier = Modifier.fillMaxWidth(), horizontalArrangement = Arrangement.spacedBy(16.dp), verticalAlignment = Alignment.CenterVertically) {
            Image(painter = check, contentDescription = "Onay")
            Image(painter = turning, contentDescription = "Dönen çizgiler")
            Switch(checked = done, onCheckedChange = { done = it })
            Text("rememberVectorPainter { Path() }: trimPathEnd ile kendini çizen onay; RenderVectorGroup: grubu döndüren VectorConfig",
                 modifier = Modifier.weight(1f), style = typography.bodyMedium)
        }
        Text("Image kendi renkleriyle çizer (dolgu, çizgi, degrade, döndürülüp daireye kırpılmış grup); Icon tek tonla boyar.",
             color = cs.onSurfaceVariant, style = typography.bodyMedium)
    }
}
