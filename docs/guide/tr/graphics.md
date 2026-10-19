# Grafik

`Canvas` ve çizim modifier'larıyla kendi grafiklerinizi çizmek, fırçalar ve path'ler, kırpma ve gölgeler, `graphicsLayer`, resim ve vektör göstermek.

## Canvas

`Canvas`, bir `DrawScope` içinde ona ne söylerseniz onu çizen bir composable'dır. Boyutunu modifier'ıyla verin:

```cpp
void Target() {
    Color ring = colorScheme().primary, center = colorScheme().error;
    Canvas(Modifier().size(160), [=](DrawScope& d) {
        d.drawCircle(ring.copy(0.2f));                              // canvas'ı doldurur
        d.drawCircle(ring, d.size().width / 3, std::nullopt, Stroke(6));
        d.drawCircle(center, 12);                                   // merkezde
    });
}
```

Çizim lambda'sı kompozisyon sırasında değil, çizim sırasında çalışır. İhtiyacınız olan renkleri yukarıdaki gibi kompozisyonda temadan okuyun ve değer olarak yakalayın.

### Bir DrawScope neler çizebilir

| Çağrı | Çizdiği |
|---|---|
| `drawRect`, `drawRoundRect`, `drawCircle`, `drawOval`, `drawArc` | Dolu ya da çizgili, bir renk ya da fırçayla şekiller. |
| `drawLine(color, start, end, width)` | Bir çizgi; bir `StrokeCap` ve kesikli çizgi için bir `PathEffect` ile. |
| `drawPoints(points, mode, color, width)` | Noktalar, çiftler arasında çizgiler ya da bir çoklu çizgi. |
| `drawPath(path, color or brush, style)` | Herhangi bir path. |
| `drawText(layout, topLeft)` | Bir metin ölçerle ölçülmüş metin (bkz. [Metin](text.md#metni-ölçmek)). |
| `drawImage(image, topLeft, size)` | Bir bitmap. |
| `drawVector(vector, color, topLeft, size)` | Renklendirilmiş bir `ImageVector`. |

`DrawStyle`, `Fill` (varsayılan) ya da `Stroke(width, cap, join)`'dir. Koordinatlar canvas'ın sol üst köşesinden dp cinsindendir; `d.size()` ve `d.center()` boyutunu ve merkezini verir.

### Dönüştürmek

`translate`, `rotate` (ve `rotateRad`), `scale`, `inset`, `clipRect` ve `clipPath` bir blok alır: bloğun çizdiği taşınır, döndürülür, ölçeklenir ya da kırpılır. `clipRect` ve `clipPath` içeride kalanı, `ClipOp::Difference` ile dışarıda kalanı tutar. `withTransform` birkaçını birden yapar; onun `transform(matrix)`'i herhangi bir `Matrix`'i (Compose'un 4x4'lüğü, perspektif de) alır. Compose'daki gibi dönüşüm çizimin kendisine işler: hiçbir şey çizimin alanına kırpılmaz; GPU renderer'da her şey, nasıl döndürülürse, gerilirse ya da perspektiften görülürse görülsün keskin ve birebir kenar yumuşatmalı kalır (yazı gösterildiği boyutta dizilir).

```cpp
void Clock(float minutes) {
    Color hand = colorScheme().onSurface;
    Canvas(Modifier().size(120), [=](DrawScope& d) {
        d.drawCircle(hand, std::nullopt, std::nullopt, Stroke(2));
        d.rotate(minutes * 6, [&](DrawScope& r) {          // dakika başına 6°, merkez etrafında
            r.drawLine(hand, r.center(), {r.center().x, 12}, 3, StrokeCap::Round);
        }, d.center());
    });
}
```

## Çizim modifier'ları

Var olan bir composable'ın üzerine çizmek için `Canvas`'a ihtiyacınız yok. Bu modifier'lar onun düğümüne çizer:

- `drawBehind(onDraw)` içeriğin arkasına çizer.
- `drawWithContent(onDraw)` içeriğin çevresine çizer: içeriğin geleceği yerde `drawContent()`'i çağırın, önüne ya da arkasına çizin.
- `drawWithCache(build)` nesneleri (path'ler, fırçalar) boyut başına bir kez kurar ve her karede onlarla çizer.

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
        d.drawRect(Brush::verticalGradient({colors::Transparent, colors::White}));   // alt kısımda bir solma
    }), content);
}
```

## Fırçalar

Bir `Brush`, bir rengin gidebileceği her yerde düz bir renkle ya da bir gradyanla boyar: çizim çağrıları, `background`, metin.

```cpp body
Box(Modifier().fillMaxWidth().height(120).background(
    Brush::linearGradient({Color::hex(0x6750A4), Color::hex(0xB58392)}), RoundedCornerShape(16)));
```

Renk durakları ve döşeme modlarıyla `Brush::solid`, `linearGradient`, `horizontalGradient`, `verticalGradient`, `radialGradient` ve `sweepGradient` mevcuttur.

## Path'ler

Bir `Path`, çizgiler, eğriler ve yaylarla herhangi bir dış hattı tanımlar:

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
        Path path = heart(cache.size());   // boyut başına bir kez kurulur
        return cache.onDrawBehind([path, red](DrawScope& d) { d.drawPath(path, red); });
    }));
}
```

`moveTo`, `lineTo`, `quadraticTo`, `cubicTo`, bunların `relative…` biçimleri, `arcTo`, `addRect`, `addOval`, `addRoundRect` ve `close` path'i kurar. `PathMeasure` uzunluğunu ölçer ve parçalarını verir; bir path'i adım adım çizmek için.

## Şekiller ve kırpma

Bir `Shape`, arka planların, çerçevelerin, kırpmaların ve gölgelerin kullandığı dış hattır: `RectangleShape`, `CircleShape`, `RoundedCornerShape(16)` (ya da köşe başına bir yarıçap), `CutCornerShape(8)` ve Material 3 Expressive'in şekilleri (`MaterialShapes::Cookie9Sided()` ve diğerleri, `toShape` ile).

```cpp body
Box(Modifier().size(96).clip(CircleShape).background(colorScheme().tertiaryContainer));
Box(Modifier().size(96).border(2, colorScheme().outline, RoundedCornerShape(12, 12, 0, 0)));
```

Compose'daki gibi bir şekil, bir boyutta ve yerleşim yönünde dış hattını yapar (`createOutline`): bir dikdörtgen, yuvarlak bir dikdörtgen (köşeleri eliptik de olabilen `Outline::Rounded(RoundRect{…})`) ya da herhangi bir yol. `GenericShape` birini bir yoldan kurar:

```cpp body
Shape ticket = GenericShape([](Path& p, Size size, LayoutDirection) {
    p.moveTo(0, 0);
    p.lineTo(size.width, 0);
    p.lineTo(size.width, size.height);
    p.arcTo({size.width / 2 - 12, size.height - 12, 24, 24}, 0, -180, false);  // altta bir çentik
    p.lineTo(0, size.height);
    p.close();
});
Box(Modifier().size(120, 72).clip(ticket).background(colorScheme().primaryContainer));
```

`clip(shape)` içeriği şekle göre, kenarı yumuşatılmış olarak ve katmansız kırpar.

## Gölgeler

`shadow(elevation, shape)` bir düğümün altına Material tarzı bir gölge çizer. `dropShadow(shape, Shadow{…})` ve `innerShadow(shape, Shadow{…})` kendi renginiz, kaydırmanız, bulanıklık yarıçapınız ve yayılmanızla gölgeler çizer:

```cpp body
Box(Modifier().size(120)
        .dropShadow(RoundedCornerShape(24), Shadow{.color = Color::hex(0x6750A4, 0.4f), .offset = {0, 8}, .blurRadius = 24})
        .background(colorScheme().surface, RoundedCornerShape(24)));
```

## graphicsLayer

`graphicsLayer` içeriği, yerleşimini değiştirmeden, bir ekran dışı katman üzerinden ölçeklenmiş, döndürülmüş, taşınmış ya da soldurulmuş olarak çizer:

```cpp body
Box(Modifier().size(80).graphicsLayer({.scaleX = 1.2f, .scaleY = 1.2f, .alpha = 0.8f, .rotationZ = 15})
        .background(colorScheme().primary, RoundedCornerShape(16)));
```

Lambda alan biçimi çizim sırasında değerlendirilir; bu yüzden orada okunan animasyonlu değerler yeniden kompoze etmeden ya da yeniden yerleştirmeden değişir. Her karede değişen her şey için onu kullanın (bkz. [Aşamalar](phases.md#durum-okumalarını-ertelemek)):

```cpp
void Spinner() {
    auto angle = rememberInfiniteTransition().animateFloatState(0, 360, infiniteRepeatable(tween(1200, 0, LinearEasing)));
    Icon(Icons::Filled::Refresh, "Loading", Modifier().graphicsLayer([angle](GraphicsLayerScope& l) { l.rotationZ = angle.get(); }));
}
```

`alpha(a)`, `scale(s)` ve `rotate(degrees)` sık görülen durumlar için kısaltmalardır; `blur(radius)` ise içeriği bulanıklaştırır.

## Resimler

`Image` bir bitmap gösterir; `imageResource(path)` uygulamanın varlıklarından bir tane yükler, bir kez:

```cpp body
Image(imageResource("images/cover.jpg"), "Album cover", Modifier().size(160).clip(RoundedCornerShape(12)), ContentScale::Crop);
```

`AsyncImage` bir resmi varlıklardan ya da web'den (`http://`, `https://`) yükler, gösterildiği boyutta çözer ve indirmeleri diskte saklar. Yüklenirken bir yer tutucu, başarısız olursa başka bir şey gösterir:

```cpp body
AsyncImage("https://example.com/photo.webp", "A photo", Modifier().size(200),
           {.placeholder = [] { CircularProgressIndicator(); },
            .error = [] { Icon(Icons::Filled::Info, "Couldn't load"); },
            .contentScale = ContentScale::Crop,
            .crossfadeMillis = 200});
```

JPEG, PNG ve WebP desteklenir. `ContentScale`, resmi Compose'daki gibi kutusuna yerleştirir: `Fit`, `Crop`, `FillBounds`, `FillWidth`, `FillHeight`, `Inside`, `None`.

## Vektörler ve simgeler

`Icon`, bir `ImageVector`'ü içerik rengiyle renklendirerek, varsayılan olarak 24 dp boyutunda çizer. Kompoz'da `Icons::Filled` ve `Icons::AutoMirrored::Filled` altında Material simgeleri vardır (ikincisi sağdan sola yerleşimlerde aynalanır):

```cpp body
Icon(Icons::Filled::Favorite, "Favorite", Modifier(), Color::hex(0xE53935));
```

Kendi vektörleriniz SVG path verisinden, Android vector drawable dosyalarından gelir ya da kodda kurulur:

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

`vectorResource`, bir Android `<vector>` dosyasından grupları, path'leri, gradyanları ve kırpma path'lerini okur; `Image` ile çizildiğinde kendi renkleriyle, `Icon` ile renklendirilmiş olarak.

## Ayrıca bakın

- [Aşamalar](phases.md): yeniden kompoze etmeden çizmek.
- [Animasyon](animation.md): çizdiklerinizi canlandırmak.
- [Kaynaklar](resources.md): resim ve vektör dosyalarının nereye konacağı.
