# Grafik

`Canvas` ve çizim modifier'larıyla kendi grafiklerinizi çizmek, fırçalar ve path'ler, kırpma ve gölgeler, `graphicsLayer`, resim ve vektör göstermek.

## Canvas

`Canvas`, bir `DrawScope` içinde ona ne söylerseniz onu çizen bir composable'dır. Boyutunu modifier'ıyla verin:

```kotlin
@Kompozable
fun Target() {
    val ring = MaterialTheme.colorScheme.primary
    val center = MaterialTheme.colorScheme.error
    Canvas(modifier = Modifier.size(160.dp)) {
        drawCircle(ring.copy(alpha = 0.2f))                                  // canvas'ı doldurur
        drawCircle(ring, radius = size.width / 3, style = Stroke(width = 6f))
        drawCircle(center, radius = 12f)                                     // merkezde
    }
}
```

Çizim lambda'sı kompozisyon sırasında değil, çizim sırasında çalışır. İhtiyacınız olan renkleri yukarıdaki gibi kompozisyonda temadan okuyun ve değer olarak yakalayın.

### Bir DrawScope neler çizebilir

| Çağrı | Çizdiği |
|---|---|
| `drawRect`, `drawRoundRect`, `drawCircle`, `drawOval` | Dolu ya da çizgili şekiller; yuvarlak dikdörtgenin `CornerRadius(x, y)`'si köşelerini çeyrek elips yapabilir. |
| `drawArc(color, start, sweep, useCenter, …)` | Bir ovalin parçası: merkezinden geçen bir pasta dilimi (`useCenter`), yoksa düz kapatılmış ya da açık bir yay. |
| `drawLine(color, start, end, width, cap, pathEffect)` | Bir çizgi; bir `StrokeCap` ve kesikli çizgi için bir `PathEffect` ile. |
| `drawPoints(points, mode, color, width)` | Noktalar, çiftler arasında çizgiler ya da bir çoklu çizgi. |
| `drawPath(path, color, alpha, style)` | Herhangi bir path. |
| `drawText(layout, color, topLeft)`, `drawText(measurer, text, topLeft, style)` | Metin (bkz. [Metin](text.md#metni-ölçmek)). |
| `drawImage(image, topLeft)`, `drawImage(image, srcOffset, srcSize, dstOffset, dstSize, …, filterQuality)` | Bir bitmap ya da bir parçası bir dikdörtgene, `FilterQuality`'nin dediği gibi örneklenerek. |
| `painter.draw(d, size, alpha, colorFilter)` | Bir `Painter` (bir `ImageVector`'ünkü: `VectorPainter(vector)`), Compose'un `with(painter) { draw(…) }`'u gibi. |

Compose'un parametrelerini Compose'unki gibi adla alırlar: bir renk ya da fırça, sonra `topLeft`, `size`, `radius`, `center`, `alpha`, `style`, `colorFilter` ve `blendMode` (`drawRect(color, size = size, alpha = 0.5f, style = Stroke(width = 2f))`). `DrawStyle`, `Fill` (varsayılan) ya da `Stroke(width, miter, cap, join, pathEffect)`'tir; 0 genişlik kıl kalınlığındadır, dönüşüm ne olursa olsun bir piksel. Koordinatlar canvas'ın sol üst köşesinden dp cinsindendir; `d.size()` ve `d.center()` boyutunu ve merkezini, `d.density()` bir dp'deki pikselleri verir.

### Dönüştürmek

`translate`, `rotate` (ve `rotateRad`), `scale`, `inset`, `clipRect` ve `clipPath` bir blok alır: bloğun çizdiği taşınır, döndürülür, ölçeklenir ya da kırpılır. `clipRect` ve `clipPath` içeride kalanı, `ClipOp.Difference` ile dışarıda kalanı tutar. `withTransform` birkaçını birden yapar; onun `transform(matrix)`'i herhangi bir `Matrix`'i (Compose'un 4x4'lüğü, perspektif de) alır. Compose'daki gibi dönüşüm çizimin kendisine işler: hiçbir şey çizimin alanına kırpılmaz; GPU renderer'da her şey, nasıl döndürülürse, gerilirse ya da perspektiften görülürse görülsün keskin ve birebir kenar yumuşatmalı kalır (yazı gösterildiği boyutta dizilir).

```kotlin
@Kompozable
fun Clock(minutes: Float) {
    val hand = MaterialTheme.colorScheme.onSurface
    Canvas(modifier = Modifier.size(120.dp)) {
        drawCircle(hand, style = Stroke(width = 2f))
        rotate(minutes * 6) {   // dakika başına 6°, merkez etrafında
            drawLine(hand, start = center, end = Offset(center.x, 12f), strokeWidth = 3f, cap = StrokeCap.Round)
        }
    }
}
```

## Çizim modifier'ları

Var olan bir composable'ın üzerine çizmek için `Canvas`'a ihtiyacınız yok. Bu modifier'lar onun düğümüne çizer:

- `drawBehind(onDraw)` içeriğin arkasına çizer.
- `drawWithContent(onDraw)` içeriğin çevresine çizer: içeriğin geleceği yerde `drawContent()`'i çağırın, önüne ya da arkasına çizin.
- `drawWithCache(build)` nesneleri (path'ler, fırçalar) boyut başına bir kez kurar ve her karede onlarla çizer.

```kotlin
@Kompozable
fun Highlighted(text: String) {
    val marker = Color(0xFFFFEB3B).copy(alpha = 0.6f)
    Text(text, modifier = Modifier.drawBehind {
        drawRect(marker, topLeft = Offset(0f, size.height * 0.55f), size = Size(size.width, size.height * 0.4f))
    })
}

@Kompozable
fun Faded(content: @Kompozable () -> Unit) {
    Box(modifier = Modifier.drawWithContent {
        drawContent()
        drawRect(Brush.verticalGradient(listOf(Color.Transparent, Color.White)))   // alt kısımda bir solma
    }) { content() }
}
```

## Renkler ve renk uzayları

Bir `Color` Compose'unkidir: 64 bit; sRGB'de kanal başına 8 bit, Compose'un başka bir renk uzayında (`ColorSpaces`: Display P3, BT.2020, Adobe RGB, CIE L\*a\*b\*, Oklab ve daha fazlası) bileşenleri yarım float'lar. `convert` birini başka bir uzaya çevirir, `toArgb()` sRGB'sini `0xAARRGGBB` olarak verir; bir renk hangi uzayda olursa olsun sRGB'de çizilir.

```kotlin body
val p3 = Color(0.1f, 0.75f, 0.45f, 1f, ColorSpaces.DisplayP3)   // Display P3'te
val lab = Color(55f, 60f, -40f, 1f, ColorSpaces.CieLab)        // L*, a*, b*
val between = lerp(Color.Red, Color.Blue, 0.5f)                 // Oklab'da karıştırılır
Row(modifier = Modifier.height(40.dp)) {
    for (c in listOf(p3, lab, between)) Box(modifier = Modifier.size(40.dp).background(c))
}
```

`lerp`, Compose'unki gibi iki rengi Oklab'da karıştırır: kırmızıdan maviye yarı yol, sRGB sayılarının verdiği koyu mor değil, ikisi kadar açık bir menekşedir. Canlandırılan bir renk de böyle gider (`animateColorAsState`). Bir gradyan renklerini Skia'nınki gibi sRGB'de karıştırır.

## Fırçalar

Bir `Brush`, bir rengin gidebileceği her yerde düz bir renkle ya da bir gradyanla boyar: çizim çağrıları, `background`, metin.

```kotlin body
Box(modifier = Modifier.fillMaxWidth().height(120.dp).background(
    Brush.linearGradient(listOf(Color(0xFF6750A4), Color(0xFFB58392))), RoundedCornerShape(16.dp)))
```

Renk durakları ve döşeme modlarıyla `Brush.solid` (Compose'un `SolidColor`'ı), `linearGradient`, `horizontalGradient`, `verticalGradient`, `radialGradient` ve `sweepGradient` mevcuttur.

Kendi fırçanız, Compose'un `ShaderBrush`'ı gibi, boyadığı boyut için yapılan bir shader'dır: bir degrade, döşenmiş bir resim ya da birleştirilmiş iki shader.

```kotlin body
Box(modifier = Modifier.size(120.dp, 72.dp).background(Brush.shader { size ->
    CompositeShader(
        LinearGradientShader(Offset(0f, 0f), Offset(size.width, 0f), listOf(Color(0xFF6750A4), Color(0xFFB58392))),
        RadialGradientShader(Offset(24f, 20f), 48f, listOf(Color.White.copy(alpha = 0.9f), Color.Transparent)),
        BlendMode.Screen,
    )
}))
```

`Brush.composite` aynısını iki fırçayla yapar, `withTransform` bir degradenin ya da shader'ın boyadığını taşır, `lerp` de bir animasyonun yaptığı gibi bir fırçadan ötekine geçer.

## Canvas ve Paint

Bir `DrawScope`'un altında kaydetme yığını ve `Paint`'iyle Compose'un `Canvas`'ı vardır (`Paint` burada bir struct: `Paint(color = …, style = …)`; Compose'unkinde ayarlayıcılar vardır):

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

Canvas'ın kaydedip dönüştürdüğü, Compose'daki gibi, içinde bulunduğu blok bitene dek kapsamın sonraki çizimleri için geçerlidir.

`Canvas(image)` ise bir `ImageBitmap`'e çizer, birimi bir piksel; bir kez çizilip `Image` ile gösterilen bir desen için. Bir `Paint`'in alanları Compose'un özellikleri gibi atanır (`paint.alpha = 0.6f`):

```kotlin body
val rings = remember {
    val bitmap = ImageBitmap(96, 96)
    val canvas = Canvas(bitmap)
    val paint = Paint(color = Color(0xFF6750A4))
    for (i in 0 until 6) {
        paint.alpha = 1f - i * 0.15f
        canvas.drawCircle(Offset(48f, 48f), 46f - i * 8, paint)
    }
    bitmap
}
Image(rings, contentDescription = "Halkalar")
```

## Path'ler

Bir `Path`, çizgiler, eğriler ve yaylarla herhangi bir dış hattı tanımlar:

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

@Kompozable
fun Heart() {
    val red = Color(0xFFE53935)
    Box(modifier = Modifier.size(64.dp).drawWithCache {
        val path = heart(size)   // boyut başına bir kez kurulur
        onDrawBehind { drawPath(path, red) }
    })
}
```

`moveTo`, `lineTo`, `quadraticTo`, `conicTo`, `cubicTo`, bunların `relative…` biçimleri, `arcTo`, `addRect`, `addOval`, `addRoundRect` ve `close` path'i Compose'unki (bir Android path'i) gibi kurar: bütün olarak eklenen şekiller `Path.Direction.Clockwise` vermezseniz saat yönünün tersine döner, yaylar ve ovaller birebir conic'lerdir. Bir `Rect` Compose'daki gibi verilir: `Rect(left, top, right, bottom)` ya da `Rect(topLeft, size)`. `transform(matrix)` onu taşır, `op` (ya da `a + b`, `a - b`, `a & b`) iki path'i eğrilerini koruyarak birleştirir, `iterator()` segmentlerini gezer, `PathHitTester` noktaları ona tekrar tekrar sınar. `PathMeasure` uzunluğunu ölçer ve parçalarını verir (eğrilerinin parçalarını eğri olarak); Android'inki gibi, bir path'i adım adım çizmek için.

Bir çizgideki (ya da bir `Paint`'teki) `PathEffect` path'i çizilirken değiştirir, Skia'nınki gibi: kesikler, yuvarlak köşeler, boyunca basılan bir şekil ya da biri ardından öteki (`chainPathEffect`).

```kotlin body
val primary = MaterialTheme.colorScheme.primary
val wave = Path()
wave.moveTo(0f, 20f)
wave.cubicTo(30f, -10f, 60f, 50f, 90f, 20f)
Canvas(modifier = Modifier.size(100.dp, 40.dp)) {
    drawPath(wave, primary, style = Stroke(width = 3f, cap = StrokeCap.Round, join = StrokeJoin.Round, pathEffect = PathEffect.dashPathEffect(floatArrayOf(8f, 6f))))
}
```

Path'in çizildiği yerde, çizildiği piksellerde uygulanır; böylece bir eğrideki kesik her ölçekte Compose'unkinin düştüğü yere düşer.

## Şekiller ve kırpma

Bir `Shape`, arka planların, çerçevelerin, kırpmaların ve gölgelerin kullandığı dış hattır: `RectangleShape`, `CircleShape`, `RoundedCornerShape(16.dp)` (ya da köşe başına bir yarıçap), `CutCornerShape(8.dp)` ve Material 3 Expressive'in şekilleri (`MaterialShapes.Cookie9Sided.toShape()` ve diğerleri).

```kotlin body
Box(modifier = Modifier.size(96.dp).clip(CircleShape).background(MaterialTheme.colorScheme.tertiaryContainer))
Box(modifier = Modifier.size(96.dp).border(2.dp, MaterialTheme.colorScheme.outline, RoundedCornerShape(12.dp, 12.dp, 0.dp, 0.dp)))
```

Compose'daki gibi bir şekil, bir boyutta ve yerleşim yönünde dış hattını yapar (`createOutline`): bir dikdörtgen, yuvarlak bir dikdörtgen (köşeleri eliptik de olabilen `Outline.Rounded(RoundRect(…))`) ya da herhangi bir yol. `GenericShape` birini bir yoldan kurar (burada bir `Rect` Compose'un sol, üst, sağ, alt'ı değil `(x, y, width, height)`'tır):

```kotlin body
val ticket = GenericShape { size, _ ->
    moveTo(0f, 0f)
    lineTo(size.width, 0f)
    lineTo(size.width, size.height)
    arcTo(Rect(size.width / 2 - 12, size.height - 12, 24f, 24f), 0f, -180f, false)  // altta bir çentik
    lineTo(0f, size.height)
    close()
}
Box(modifier = Modifier.size(120.dp, 72.dp).clip(ticket).background(MaterialTheme.colorScheme.primaryContainer))
```

`clip(shape)` içeriği şekle göre, kenarı yumuşatılmış olarak ve katmansız kırpar.

## Gölgeler

`shadow(elevation, shape)` bir düğümü yükseltir: Compose'unki gibi o `shadowElevation`'lı bir `graphicsLayer`'dır; şeklinin gölgesi Android'in çizdiği gibi, pencerenin üstünden gelen bir ışıkla çizilir (gölgeler ekranın aşağısına doğru uzar), içerik de şekle göre kırpılır (`clip`, yükseklik 0'dan büyükse varsayılan). `dropShadow(shape, Shadow{…})` ve `innerShadow(shape, Shadow{…})` kendi renginiz (ya da `brush`), kaydırmanız, bulanıklık yarıçapınız, yayılmanız, alfanız ve blend modunuzla gölgeler çizer:

```kotlin body
Box(modifier = Modifier.size(120.dp)
    .dropShadow(RoundedCornerShape(24.dp), Shadow(color = Color(0xFF6750A4).copy(alpha = 0.4f), offset = Offset(0f, 8f), blurRadius = 24f))
    .background(MaterialTheme.colorScheme.surface, RoundedCornerShape(24.dp)))
```

## graphicsLayer

`graphicsLayer` içeriği, yerleşimini değiştirmeden ölçeklenmiş, döndürülmüş (3B de: `rotationX`, `rotationY`), taşınmış, soldurulmuş, kırpılmış, süzülmüş, karıştırılmış ya da bulanıklaştırılmış olarak çizer. Compose'daki gibi yalnızca gereken yerde ekran dışı bir katmandan geçer (soluk, bir blend modu, bir renk süzgeci, bir render effect ya da `CompositingStrategy.Offscreen`); yoksa dönüşüm ve kırpma doğrudan çizime gider:

```kotlin body
Box(modifier = Modifier.size(80.dp).graphicsLayer(scaleX = 1.2f, scaleY = 1.2f, alpha = 0.8f, rotationZ = 15f)
    .background(MaterialTheme.colorScheme.primary, RoundedCornerShape(16.dp)))
```

Lambda alan biçimi çizim sırasında değerlendirilir; bu yüzden orada okunan animasyonlu değerler yeniden kompoze etmeden ya da yeniden yerleştirmeden değişir. Her karede değişen her şey için onu kullanın (bkz. [Aşamalar](phases.md#durum-okumalarını-ertelemek)):

```kotlin
@Kompozable
fun Spinner() {
    val angle by rememberInfiniteTransition().animateFloat(0f, 360f, infiniteRepeatable(tween(1200, 0, LinearEasing)))
    Icon(Icons.Filled.Refresh, contentDescription = "Loading", modifier = Modifier.graphicsLayer { rotationZ = angle })
}
```

`alpha(a)`, `scale(s)` ve `rotate(degrees)` sık görülen durumlar için kısaltmalardır.

### Bir GraphicsLayer'a kaydetmek

`rememberGraphicsLayer()`, Compose'daki gibi, çizimin içine kaydedilip yeniden çizileceği bir katman verir: `drawWithContent` içinde `record(layer) { this@drawWithContent.drawContent() }` içeriği kaydeder (`size = …` ile kendi boyutunda), `drawLayer(layer)` kaydı çizer; katmanın `alpha`, `scaleX`, `scaleY`, `translationX`, `rotationZ`, `topLeft`… özellikleri nasıl çizileceğini söyler. Bir kart ve yansıması, içerik bir kez çizilip iki kez gösterilir:

```kotlin
@Kompozable
fun Reflected() {
    val layer = rememberGraphicsLayer()
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
        Box(modifier = Modifier.size(150.dp, 55.dp).background(MaterialTheme.colorScheme.primary, RoundedCornerShape(12.dp)))
    }
}
```

`layer.toImageBitmap()` kaydı piksele çevirir (`Image` için bir `ImageBitmap`, ya da paylaşmak için), Compose'unki gibi.

### Bulanıklık ve render effect'ler

`blur(radius)` içeriği Compose'unki gibi bulanıklaştırır. Varsayılan olarak bulanıklık düğümün sınırları içinde kalır (`BlurredEdgeTreatment.Rectangle`: kenarların ötesindeki pikseller kenarınki sayılır); `BlurredEdgeTreatment.Unbounded` solarak dışarı yayılmasına izin verir; `BlurredEdgeTreatment{shape}` onu bir şekle göre keser:

```kotlin body
Image(imageResource("images/cover.jpg"), contentDescription = "Album cover",
      modifier = Modifier.size(160.dp).blur(12.dp, BlurredEdgeTreatment(RoundedCornerShape(16.dp))), contentScale = ContentScale.Crop)
```

Bu, render effect'i olan bir `graphicsLayer`'dır; bir katmana render effect'i kendiniz de verebilirsiniz. `BlurEffect(radiusX, radiusY, edgeTreatment)` piksellerini bulanıklaştırır (bir Gauss, yarıçap için Android'inki; `TileMode` kenarların ötesinde ne olduğunu söyler); `OffsetEffect(x, y)` onları taşır. Her biri önce içinden geçilecek bir başkasını alır:

```kotlin body
Box(modifier = Modifier.size(96.dp).graphicsLayer(renderEffect = OffsetEffect(BlurEffect(6f, 6f, TileMode.Decal), Offset(0f, 4f)))
    .background(MaterialTheme.colorScheme.primary, CircleShape))
```

Katmanın sınırları içinde çizilen, effect'in içinden geçer; `clip` açıksa effect'in yaptığı sonra katmanın şekline göre kesilir.

`blur`, Compose 1.13'ünki gibi kademeli de bulanıklaştırır: bazı yerlerde ötekilerden daha çok; bir yarıçaptan ötekine (ya da `BlurStop`'lar üzerinden) aşağı, yana, bir doğru boyunca ya da bir merkezden dışarı. Lambda alan biçimi bunu çizim sırasında ayarlar:

```kotlin body
Image(imageResource("images/cover.jpg"), contentDescription = "Album cover",
      modifier = Modifier.size(160.dp).blur(BlurRadiusSpec.verticalGradient(0.dp, 16.dp)), contentScale = ContentScale.Crop)  // üstte keskin
Box(modifier = Modifier.size(120.dp).blur {
    blurRadiusSpec = BlurRadiusSpec.radialGradient(0.dp, 12.dp)   // ortada keskin
}.background(MaterialTheme.colorScheme.primary, CircleShape))
```

Kompoz'un `blur`'u neyin bulanıklaşacağını da alır (`BlurType`). Varsayılan `Family`, Compose'unki gibi düğümü ve çocuklarını bulanıklaştırır. `Morph` düğümün arkasında çizileni şekli içinde bulanıklaştırır ve düğümü üstüne keskin çizer: buzlu cam. `Self` yalnızca düğümün kendi çizdiğini (arka planı, kenarlığı, resmi) bulanıklaştırır ve çocuklarını üstüne keskin çizer:

```kotlin body
Box(
    modifier = Modifier.size(200.dp, 72.dp)
        .blur(16.dp, BlurredEdgeTreatment(RoundedCornerShape(16.dp)), BlurType.Morph)
        .background(Color.White.copy(alpha = 0.2f), RoundedCornerShape(16.dp)),
    contentAlignment = Alignment.Center,
) { Text("Frosted glass") }
```

## Resimler

`Image` bir bitmap gösterir; `imageResource(path)` uygulamanın varlıklarından bir tane yükler, bir kez:

```kotlin body
Image(imageResource("images/cover.jpg"), contentDescription = "Album cover", modifier = Modifier.size(160.dp).clip(RoundedCornerShape(12.dp)), contentScale = ContentScale.Crop)
```

`AsyncImage` bir resmi varlıklardan ya da web'den (`http://`, `https://`) yükler, gösterildiği boyutta çözer ve indirmeleri diskte saklar. Yüklenirken bir yer tutucu, başarısız olursa başka bir şey gösterir:

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

JPEG, PNG ve WebP desteklenir. `ContentScale`, resmi Compose'daki gibi kutusuna yerleştirir: `Fit`, `Crop`, `FillBounds`, `FillWidth`, `FillHeight`, `Inside`, `None`.

Kendi `Painter`'ınız Compose'un `object : Painter()`'ıdır: `intrinsicSize`'ı (`Image` ve `Modifier.paint` boyutlarını ona göre alır) ve o boyuttaki bir `DrawScope`'ta çizdikleri:

```kotlin body
val target = remember {
    object : Painter() {
        override val intrinsicSize: Size get() = Size(56f, 56f)
        override fun DrawScope.onDraw() {
            for (i in 0 until 4) drawCircle(if (i % 2 == 0) Color.Red else Color.White, size.width / 2 - i * 7)
        }
    }
}
Image(target, contentDescription = "Hedef")
```

## Vektörler ve simgeler

`Icon`, bir `ImageVector`'ü içerik rengiyle renklendirerek, varsayılan olarak 24 dp boyutunda çizer. Kompoz'da `Icons.Filled` ve `Icons.AutoMirrored.Filled` altında Material simgeleri vardır (ikincisi sağdan sola yerleşimlerde aynalanır):

```kotlin body
Icon(Icons.Filled.Favorite, contentDescription = "Favorite", tint = Color(0xFFE53935))
```

Kendi vektörleriniz SVG path verisinden, Android vector drawable dosyalarından gelir ya da kodda kurulur:

```kotlin
val SparkleIcon = ImageVector.fromSvgPath("M12,2l2.4,7.6L22,12l-7.6,2.4L12,22l-2.4-7.6L2,12l7.6-2.4z")

// Kendi boyalarıyla yollar, gruplar içinde, Compose'un ImageVector.Builder'ının yaptığı gibi.
val BadgeIcon = ImageVector.Builder("Rozet", 24.dp, 24.dp, 24f, 24f)
    .addPath(pathData = addPathNodes("M12,2a10,10 0 1,0 0.01,0z"), fill = Brush.solid(Color(0xFF1E88E5)))
    .group(rotation = -10f, pivotX = 12f, pivotY = 12f) {
        path(stroke = Brush.solid(Color.White), strokeLineWidth = 2f, strokeLineCap = StrokeCap.Round) {
            moveTo(7f, 12f)
            lineTo(10.5f, 15.5f)
            lineTo(17f, 9f)
        }
    }
    .build()

@Kompozable
fun Sparkle() {
    Icon(SparkleIcon, contentDescription = null)
    Image(rememberVectorPainter(BadgeIcon), contentDescription = "Rozet")
    Image(rememberVectorPainter(vectorResource("vectors/logo.xml")), contentDescription = "Logo", modifier = Modifier.size(48.dp))
}
```

`vectorResource`, bir Android `<vector>` dosyasından grupları, path'leri, gradyanları ve kırpma path'lerini okur. Bir vektör Compose'un çizdiği gibi çizilir: piksellerdeki boyutu kadar bir bitmap'e çizilip saklanır ve o çizilir; `Image` ile kendi renkleriyle, `Icon` ile renklendirilmiş olarak (hepsi tek renkse Alpha8 maske olarak).

Bir vektörün içeriği Compose'un `rememberVectorPainter { Group { Path() } }`'u gibi composable da olabilir: okuduğu şey onu yeniden yapar; burada açıldıkça kendini çizen bir onay işareti.

```kotlin body
#! val done = remember { mutableStateOf(false) }
val drawn by animateFloatAsState(if (done.value) 1f else 0f, tween(400))
val tick = rememberVectorPainter(24.dp, 24.dp, 24f, 24f) { _, _ ->
    Path(pathData = addPathNodes("M5,12l5,5l9,-10"), stroke = Brush.solid(Color.Black), strokeLineWidth = 2f, trimPathEnd = drawn)
}
Image(tick, contentDescription = "Tamam")
```

`vector.RenderVectorGroup(icon.root, configs)` var olan bir vektörün ağacını böyle çizer; bir grubun ya da yolun adına verilen `VectorConfig` onun çizileceği değerleri verir (dönüş, renk, kırpma).

## Ayrıca bakın

- [Aşamalar](phases.md): yeniden kompoze etmeden çizmek.
- [Animasyon](animation.md): çizdiklerinizi canlandırmak.
- [Kaynaklar](resources.md): resim ve vektör dosyalarının nereye konacağı.
