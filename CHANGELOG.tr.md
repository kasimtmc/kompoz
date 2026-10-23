# Değişiklikler

[English](CHANGELOG.md) · **Türkçe**

## Yayımlanmadı

- **Linux'ta ekran okuyucular**: semantik ağaç AT-SPI üzerinden sunuluyor (libdbus çalışırken yükleniyor); Orca Kompoz
  uygulamalarını okuyor ve kullanabiliyor: roller, adlar, durumlar, değerler ve metin, eylemler, değişenler için
  olaylar. Masaüstünün erişilebilirliği açıkken açık; zorla açmak ya da kapatmak için `KOMPOZ_ATSPI=1` / `0`.
- **Windows'ta ekran okuyucular**: semantik ağaç UI Automation üzerinden Ekran Okuyucusu'na ve NVDA'ya sunuluyor:
  denetim türleri, adlar, durumlar, konumlar, düğümleri kullanmak için desenler ve değişenler için olaylar.
- **Kurulum ve paketler**: Linux'ta, macOS'ta ve Windows'ta `cmake --install` ve `find_package(kompoz)`
  (`kompoz::core`, `kompoz::sdl3`, `kompoz_copy_assets`); sistemin SDL3, FreeType ve HarfBuzz'ıyla ya da
  `third_party/`'den derlenip yanına kurulanlarla. vcpkg overlay port'u ve Conan reçetesi; AsyncImage'ın kütüphaneleri
  özellik ve seçenek olarak.
- **libjpeg-turbo 2.x ile de JPEG** (3.x'in yanında); AsyncImage'ın kütüphaneleri CMake paketlerinden ya da
  pkg-config'ten bulunuyor, `KOMPOZ_WITH_JPEG` / `WEBP` / `CURL` her birini otomatik, zorunlu ya da kapalı yapıyor.
- **Her platformda CI**: testler, platform testleri ve Kompoz'un her kurulum yoluyla derlenip çalıştırılan, tıklanan
  bir uygulama (Windows'ta ve artık Kompoz'un çalıştığı macOS'ta gerçek pencerede, macOS'ta ekransız da). `CONTRIBUTING.md`, issue formları
  ve bir sürümün paketlerini yayımlayan `scripts/release.sh`.
- **Kompoz'un kendi GPU renderer'ı**, yapım aşamasında (`KOMPOZ_GPU=vulkan` ya da `gles`): her native grafik API'si için ince bir
  katmanın üstünde tek bir çekirdek; önce Linux, Windows ve Android'de Vulkan, ardından Apple'ın sistemlerinde Metal, web'de WebGPU ve bunların hiçbiri olmayan yerlerde OpenGL ES 3 (eski GPU'lar, WebGL 2, Windows'ta ANGLE).
  Yuvarlak dikdörtgenler, kenarlıklar, çizgiler, yaylar, gölgeler, yazı, resimler, clip'ler ve katmanlar GPU'da birebir
  kenar yumuşatmayla çiziliyor (her açıdaki düz bir kenar her pikseli bir path'in çokgeni kadar birebir kesiyor, kare
  bir köşe iki kenarı kadar; bir pikselden ince bir halka ya da çizgi eni kadar kaplıyor); path'ler de: nonzero ya da evenodd kuralıyla dolduruluyor, her uç ve birleşim türüyle
  çiziliyor (bir dönüşün içi kendini kesmeden çiziliyor; böylece bir eğrinin iç kenarı dış kenarı kadar açık), kapsamaları GPU'da FreeType'ın rasterizer'ının hesapladığı gibi birebir toplanıyor, eğriler 0.2 px
  içinde; katmanlar herhangi bir dış çizgiyle kesilebiliyor. Gradyanlar (doğrusal, radyal, sweep, her döşeme moduyla)
  dolgularda, path'lerde ve yazıda aynı biçimde, duraklarından piksel başına `Brush::colorAt`'in hesapladığı gibi
  çiziliyor (`Brush::geometry` birinin nerede durduğunu söylüyor). 29 blend modunun hepsi çiziliyor; blend biriminin
  yapamadıkları shader'da, çizimin altındakinin bir kopyasından hesaplanıyor; renk süzgeçleri (matrisler, ışıklandırma,
  her moddaki tint'ler) her piksele uygulanıyor. Her şey herhangi bir dönüşümün (döndürme, germe, eğme, perspektif)
  içinden keskin ve birebir kenar yumuşatmayla çiziliyor: yazı ve katmanlar görüldükleri boyutta hazırlanıyor, path'ler
  izdüşürülüyor, dönüşüm altındaki clip'ler birebir kesiliyor. Render effect'ler Skia'nın yaptığı gibi bulanıklaştırıyor
  (Android'in sigmasıyla bir Gauss, geniş olanlar küçültülerek, her döşeme moduyla). Shader'ları bir kez (Slang ile) yazılıp
  `scripts/shaders.py` ile her API için derleniyor; testler sahnelerini gerçek bir cihazda çizip resimlerle
  karşılaştırıyor.
- **Compose'un bütün blend modları**: `BlendMode`'a Overlay, ColorDodge, ColorBurn, Hardlight, Softlight, Difference,
  Exclusion, Hue, Saturation, Color ve Luminosity eklendi; `blend(kaynak, hedef, mod)` 29'undan herhangi birini
  hesaplıyor (W3C'nin kompozisyon belirtimi, Skia'daki gibi), `ColorFilter::tint` hepsini alıyor.
- **Renk süzgeçleri çizim durumu** (`GraphicsContext::pushColorFilter`): resimler, painter'lar ve `drawImage` süzülmüş
  bir kopya çizmek yerine süzgeçlerinin içinden çiziyor; böylece bir backend onu piksel başına uyguluyor. Bir vektör
  painter'ın path'leri süzgeçten kendi boyalarıyla geçiyor (gri bir matris çok renkli bir vektörü önceden tek renk
  yapıyordu).
- **`Matrix`**, Compose'un 4x4 dönüşümü, işlemi işlemine: `translate`, `scale`, `rotateX`/`Y`/`Z`, `timesAssign`,
  `invert`, `resetToPivotedTransform`, noktalar ve dikdörtgenler için perspektifiyle birlikte `map`.
- **Katmansız dönüşümler**: `GraphicsContext::pushTransform(matris)`; `DrawScope`'un `rotate`, `scale` ve
  `withTransform`'u ondan geçiyor (`rotateRad`, `scale(s)`, `inset(yatay, dikey)` ve `DrawTransform::transform(matris)`
  eklendi); böylece Compose'daki gibi çizdikleri artık çizimin alanına kırpılmıyor. Display list hasar ve ayıklama için
  dönüşümleri (perspektif de) izliyor.
- **Her şekilde clip**: `GraphicsContext::pushClipRoundRect` ve `pushClipPath`, içi ya da dışı (`ClipOp`, artık
  graphics.hpp'de); `DrawScope::clipPath` ve `clipRect(…, op)` dışı silinen bir katman yerine onlardan geçiyor. GPU'da
  yuvarlak dikdörtgen clip shader'da hesaplanıyor, path clip kapsama maskesinden okunuyor. SDL'in software
  renderer'ında `clipPath` sildiği alanı siyah çiziyordu (o renderer'da DstOut yok); artık CPU'da maskeleniyor.
- **Şekiller Compose'daki gibi dış hat yapıyor**: `Shape::createOutline(boyut, yön)` bir `Outline` veriyor
  (`Rectangle`; köşeleri eliptik de olabilen (`CornerRadius(x, y)`) bir `RoundRect` ile `Rounded`; ya da herhangi bir
  `Path` ile `Generic`); `GenericShape(kurucu)` boyutunda ve yönünde kurulan bir yoldan, `OutlineShape(createOutline)`
  herhangi bir dış hattan şekil yapıyor; `Path::addRoundRect(RoundRect)`. Arka planlar, çerçeveler, gölgeler, clip'ler
  ve dalgalar dış hatta göre çiziliyor; `clip` (ve kırpan bir `shadow`, sınırlı bir dalga) katman yerine çizim
  bağlamından kırpıyor. Çokgen bir şeklin artık ortasına göre yıldız biçimli olması gerekmiyor: SDL_Renderer dış hat
  katmanlarını ve clip'leri yelpazeyle kesmek yerine şekille maskeliyor.
- **Compose'unki gibi graphicsLayer**: `rotationX`, `rotationY` ve `cameraDistance` (3B, Android'in RenderNode'unun
  yaptığı matris: kamera `cameraDistance` × 72 px uzakta), `blendMode`, `colorFilter`, `outsets`, lambdada okunacak `size`
  ve Compose'daki gibi `CompositingStrategy`: yalnızca gereken yerde offscreen (soluk, bir blend modu, bir renk süzgeci,
  `Offscreen`), `ModulateAlpha` bunun yerine her çizimi soldurur; yoksa matris ve clip doğrudan çizime gider. Çizilen bir
  `GraphicsLayer` aynı özellikleri, Compose'daki gibi `pivotOffset` ve `setRectOutline` / `setRoundRectOutline` /
  `setPathOutline` ile alıyor.
- **Compose'daki gibi render effect'ler**: `BlurEffect(radiusX, radiusY, edgeTreatment)` (herhangi bir `TileMode`) ve
  `OffsetEffect`; her biri önce içinden geçilecek bir başkasını alır; bir `graphicsLayer`'ın ya da bir `GraphicsLayer`'ın
  `renderEffect`'i olarak: sınırları içinde çizilen onun içinden geçer, katman kırpıyorsa sonra şekline göre kesilir.
  `Modifier.blur` artık Compose'unki: `BlurEffect`'li bir `graphicsLayer`; yalnızca iki yarıçap da sıfırdan büyükse
  bulanıklaştırır; `BlurredEdgeTreatment` bir şekil tutar (`Rectangle`, `Unbounded` ya da bulanıklığın kesileceği
  herhangi bir şekil). `pushBlurLayer`'ın yerini `GraphicsContext::pushEffectLayer` alıyor.
- **Compose 1.13'teki gibi kademeli bulanıklıklar**: `Modifier.blur(spec, edgeTreatment, alpha)` ve
  `Modifier.blur { … }` (`BlurScope`, çizimde okunur) için ya da bir katmanın effect'i olarak (`createRenderEffect`)
  `BlurRadiusSpec` (`uniform`, `verticalGradient`, `horizontalGradient`, `linearGradient`, `radialGradient`; bir
  yarıçaptan ötekine ya da `BlurStop`'lar üzerinden). GPU renderer'ı onları Compose'un çizdiği gibi çiziyor.
- **Bir bulanıklığın neyi bulanıklaştırdığı** (`BlurType`, Kompoz'un kendi, her `Modifier.blur`'da): `Family`
  (Compose'unki), `Morph` (buzlu cam: arkadaki, düğümün şekli içinde; `GraphicsContext::drawBackdrop`) ve `Self`
  (yalnızca düğümün kendi çizdiği, çocukları üstünde keskin).
- **Her dış hatlı şeklin gölgesi Compose'un çizdiği gibi** (`GraphicsContext::drawShadowPath`'in varsayılanı, effect
  katmanlarıyla): GPU renderer'ı bir kalbin ya da kurabiyenin gölgesini artık sınırlarınınki gibi çizmiyor.
- **Android'in çizdiği gibi yükselti gölgeleri**: `graphicsLayer`'ın (ve `GraphicsLayer`'ın) `shadowElevation`,
  `ambientShadowColor` ve `spotShadowColor`'ı; pencerenin üst ortasının yukarısından gelen ışıkla bir ortam ve bir spot
  gölgesi (`GraphicsContext::drawElevationShadow`, `ShadowLight`); GPU renderer'ında Android GPU'sundaki Skia'nın
  analitik gölgesi gibi, yoksa Skia'nın bulanıklaştırdığı gibi. `Modifier.shadow` artık Compose'unki: bir
  `graphicsLayer`; yükseklik 0'dan büyükse içerik varsayılan olarak şekle göre kırpılır (önceden kırpılmıyordu:
  kırpılmasın diye `clip = false` verin).
- **Compose 1.9'un `Shadow`'u**: `dropShadow` ve `innerShadow` için `brush`, `alpha` ve `blendMode`; bir dış gölge
  Compose'un bitmap'inin bittiği yerde biter.
- **Compose'unki gibi `DrawScope`**: her çizim Compose'un parametrelerini Compose'un sırasıyla alıyor (bir renk ya da
  fırça, `alpha`, `style`, `colorFilter`, `blendMode`; `drawArc`'ın `useCenter`'ı, `drawRoundRect`'in `CornerRadius`'u,
  `drawImage`'in kaynak ve hedef dikdörtgenleri ve `FilterQuality`'si, `drawText`'in rengi, fırçası, alfası, gölgesi,
  süslemesi ve çizim stili); `Stroke(width, miter, cap, join, pathEffect)`, 0 genişliği kıl kalınlığında. Çağrılar
  değişiyor: `drawRoundRect(color, radius, topLeft, size)` artık `drawRoundRect(color, topLeft, size,
  CornerRadius(radius))`, stil alfadan sonra geliyor, `drawArc` `useCenter` alıyor, `drawText(layout, topLeft, color)`
  artık `drawText(layout, color, topLeft)`.
- **Görüntüler `FilterQuality`'nin dediği gibi örnekleniyor** (GPU renderer'ında: en yakın piksel, bilinear, mipmap'ler,
  Mitchell'in kübiği), premultiplied texellerden: saydam bir pikselin rengi artık büyütülen komşularına sızmıyor.
- **Compose'unki gibi `Path`** (Skia'nın SkPath'i): conic'ler (oval, yuvarlak dikdörtgen ve yaylar birebir), Skia'nın
  başlangıç noktalarıyla `Path::Direction`, `transform(Matrix)` (perspektif de), `isConvex()`, `arcToRad`/`addArcRad`,
  `rewind`, `PathIterator`/`PathSegment`, `PathHitTester`, eğrilerde kesin `contains` ve eğrileri koruyan `op`. Bütün
  olarak eklenen şekiller artık Compose'daki gibi varsayılan olarak saat yönünün tersine dönüyor: eski yön için
  `Path::Direction::Clockwise` verin; `arcTo`'nun bir turu Skia'daki gibi bir nokta (tüm oval için `addArc` ya da
  `addOval`).
- **Compose'unki gibi shader'lar**: `LinearGradientShader`, `RadialGradientShader`, `SweepGradientShader`, `ImageShader`
  (iki yönde döşenir) ve `CompositeShader`, dönüşümlerden geçerek; `Brush::shader` (`ShaderBrush`, yalnızca başka
  bir boyut için yeniden yapılır), `Brush::composite`, `withTransform`, `intrinsicSize`, `createShader` ve fırçalar
  arasında `lerp`; `BlurRadiusSpec::shader` her pikseli bir shader'ın alfası kadar bulanıklaştırır. `Density`,
  `layout.hpp`'den `graphics.hpp`'ye taşındı.
- **Compose'un `Canvas`'ı ve `Paint`'i** (`graphics::Canvas`: ad `Canvas` composable'ının): `save`/`restore`,
  `saveLayer`, `skew` ve `concat`'le dönüşümler, clip'ler, her çizim bir `Paint`'le; `DrawScope::drawIntoCanvas` ve
  bir `Canvas` üzerinde `DrawScope`. **Düzeltildi**: düğüm üzerindeki çizim kapsamları (`drawBehind`,
  `drawWithContent`, `Canvas`, `drawWithCache`, kayıtlar, özel painter'lar) yoğunluğu 1 ve yönü soldan sağa
  görüyordu; bir `DrawScope`'un negatif boyutlu şekilleri çizilmiyordu (artık Skia'daki gibi sıralanıyor).
- **`Canvas.drawVertices` ve `Vertices`**: Skia'nın çizdiği gibi üçgen ağları, boyanın shader'ı doku koordinatlarında,
  köşe renkleriyle bir karışım moduyla; GPU renderer'ı her üçgeni rasterleştiricinin yaptığı gibi çizer (perspektifte
  de) ve birleşik shader'ları piksel piksel hesaplar (önceden önce bir dokuya çiziliyordu).
- **İçine çizilen bitmap'ler**: her `ImageBitmapConfig`'i Android'in sakladığı gibi saklayan
  `ImageBitmap(width, height, config, hasAlpha)` (Alpha8 boyanın renginde maske olarak çizilir),
  `graphics::Canvas(bitmap)`, `readPixels` ve `PixelMap`; GPU renderer'ı böyle bir bitmap'i pikselleri istenene dek
  doku olarak tutar. **Düzeltildi**: SDL_Renderer görüntüleri geçerli karışım moduyla çizmiyordu; yazılım çizicisi bir
  katmanda çizilen yarı saydam shader dolgularının altındaki arka planı kaybediyordu.
- **`Paint.isAntiAlias = false`**: Skia'nın her piksele karar verdiği gibi yumuşatmasız çizim (ortası şeklin içinde;
  kıl çizgi her adımda bir piksel), GPU renderer'ında birebir, SDL_Renderer'da kendi rasterleştiricileriyle.
  **Düzeltildi**: butt uçlu (varsayılan) bir `DrawScope` çizgisi, kare uçlular gibi, her ucunda yarım genişliği kadar uzun
  çiziliyordu.
- **Compose'unki gibi vektörler**: `ImageVector` Compose'un modeli: adlı `VectorGroup` ve `VectorPath`'lerden bir ağaç,
  `tintColor`/`tintBlendMode`, değeriyle eşit; yol verisi `PathNode`'lar (`addPathNodes`, `PathParser`, `PathBuilder`,
  `PathData`, `toPath`), Compose'un ayrıştırıcısının okuyup çizdiği gibi (yaylar sekizde bir turlarla). Painter bir
  vektörü piksellerdeki boyutu kadar bir bitmap'e çizip onu 1:1 çizer, Compose'un `VectorPainter`'ı gibi: tek renkliyse
  Alpha8 maske olarak, süzgecinden, yoksa tint'inden, yoksa o renkte; `Icon(imageVector)` `Icon(rememberVectorPainter(…))`'dır
  (varsayılan boyutunda), Material ikonlarının adları var, kendiliğinden aynalananlar ayrı vektörler. GPU renderer maskeyi
  katmansız çizer ve Alpha8 bitmap'leri doku olarak tutar. Compose'daki gibi değişti: yolun kırpılması ilk konturunu
  ölçer, vector drawable'ın `strokeWidth`'i verilmezse 1, `tint`/`tintMode`'u okunur, degradeler yolunun birimlerinde.
  Kaldırıldı (Compose'da yok): `DrawScope::drawVector`, `GraphicsContext::drawVector`, `ImageVector::mirrored`/
  `autoMirrored`/`paths`/`commands`, `VectorPath::parse`, `Brush::scaled`.
- **Compose'unki gibi composable vektör içeriği**: `kompoz::vector` composable'ları `Group` ve `Path` ile
  `rememberVectorPainter(varsayılanGenişlik, yükseklik, …, içerik)` (içeriğe viewport'un boyutu verilir; adı, tint'i ve
  aynalanması da; viewport verilmezse varsayılan boyutu piksel olarak) ve bir ağacı her adın `VectorConfig`'iyle
  (`VectorProperty`) çizen `RenderVectorGroup`. Önceden aldığı `ImageVector::Builder&` içeriği kalktı.
- **Düzeltildi** (SDL_Renderer): bir dönüşümün büyüttüğü şey piksel olarak büyütülmeden kendi boyutunda çizilir
  (vektörün kök ölçeği ikonlarını bulanıklaştırıyordu) ve metin çizildiği boyutta dizilir (ölçekli katmanda, bitmap'te).
- **Compose'unki gibi tam piksel yerleşim**: boyutlar, kısıtlar ve konumlar Compose'un yuvarladığı yerlerde en yakın
  piksele (yarım yukarı) yuvarlanır: padding'in kenarları ve aralık toplanmadan önce, ağırlıklar artan pikseller ilklere
  verilerek, sekme satırının sekmeleri satırın bölümüyle, animasyon boyunca boyut ve konumlar; metnin boyutu üstündeki
  piksele, baseline'ları yuvarlanır (`Density::roundToPx`). İkonlar ve kenarlar artık piksellerin arasına düşmez.
- **Düzeltildi**: composable'ı iki dokunuş arasında yeniden çalışınca çift dokunma çöküyordu (dokunma algılayıcı,
  yerine yenisi gelmiş bir öğenin ayarlarını tutuyordu).
- **Düzeltildi**: `SaveableStateHolder` altındaki bir `rememberSaveable`, sağlayıcısı yerindeyken composable'ı çıkınca
  değerini tutup geri getiriyordu; artık Compose'daki gibi gidiyor (showcase'in saat seçicisi 24 saatlik geri geliyordu).
- **Compose'unki gibi renkler ve renk uzayları**: bir `Color` Compose'un 64 bitidir (sRGB'de kanal başına 8 bit; başka
  bir uzayın bileşenleri yarım float'lar, uzayın kimliğiyle); `convert`, `compositeOver`, `luminance`, `toArgb`,
  `hsv`/`hsl` ve `isSpecified` ile, ve `colorspace` paketiyle (`ColorSpace`, 20 `ColorSpaces`, aktarım fonksiyonları ve
  ICC eğrileriyle `Rgb`, connector'lar, adaptasyon). `lerp(Color)` Oklab'da karıştırır, canlandırılan renkler Compose'un
  `Color.VectorConverter`'ı gibi gider; gradyanlar Skia'nınkiler gibi sRGB'de karıştırır; başka bir uzayın rengi sRGB'de
  çizilir, yolda 8 bite yuvarlanmadan. `ImageBitmap` bir renk uzayı alır. **Değişti**: `Color`'ın `r`, `g`, `b`, `a`
  alanları artık `red()`, `green()`, `blue()`, `alpha()`; bileşenleri Compose'un tuttuğu gibi tutulur (sRGB'de 8 bit).
- **Skia'nınki gibi yol efektleri ve PathMeasure**: kesikler, köşeler, damgalar ve zincirler Skia'nın yaptığı gibi
  (kapalı bir konturun kesikleri başlangıcında birleşir, eğriler kendi parçaları olarak kesilir, efektler istendiği kadar
  zincirlenir), yolun çizildiği yerde, çizildiği piksellerde uygulanır; `PathMeasure` eğrileri `SkContourMeasure` gibi
  ölçer ve keser (eğri parçaları eğri olarak, `isClosed`, `resScale`). **Değişti**: `PathEffect` kendi fonksiyonlarıyla
  yapılan bir değerdir (`isNone`, `filterPath`); alanları ve `apply` kalktı, bir yolun efekti çizgisiyle gider
  (`PathStroke::pathEffect`).
- **OpenGL ES 3 üzerinde GPU renderer** (`KOMPOZ_GPU=gles`): aynı çekirdek ve shader'lar OpenGL ES 3.0 üzerinde
  (float hedefler gerekir: ES 3.2 ya da EXT_color_buffer_float veya _half_float), Vulkan'ın çizdiğini pikseli pikseline
  çizer (testlerin resimleri Mesa'da, NVIDIA'da ve iki telefonun Adreno'sunda aynı); Android'de bir `kompoz-gpu`
  dosyasıyla seçilir; `kompoz_gpu_tests` onun üzerinde çalışır, CI'da da, bir telefonda adb shell'den de.
- **Düzeltildi**: GPU renderer, bir resmi tanımsız bir yerde istenen gibi örneklemeyen GPU'larda (bir telefonun
  Adreno'su; Vulkan'da da OpenGL ES'te de) pencereyi tek renk gösteriyordu: saklanan kare pencereye, texel'lerinin
  üzerine yayıldığı şekil verilmeden ve filtrelenerek (bazı GPU'larda bir piksel farkla) kopyalanıyordu; artık texel
  texel kopyalanıyor.
- **Web'de GPU renderer**, WebGL 2 üzerinde (`showcase.html?KOMPOZ_GPU=gles`: bir web sayfasının sorgusu artık
  uygulamasının ortamıdır, `platform/web/shell.html` aracılığıyla); `kompoz_gpu_tests`, emrun'ın açıp sonucunu
  bildirdiği bir sayfa olarak derlenir, tarayıcıda aynı resimler (CI'da Chrome'un SwiftShader'ında).
- **Windows'ta ANGLE üzerinden GPU renderer** (`KOMPOZ_GPU=gles`, uygulamanın yanında vcpkg'nin derlediği ANGLE
  `libEGL.dll` ve `libGLESv2.dll` dosyalarıyla): Direct3D 11 üzerinde OpenGL ES; `kompoz_gpu_tests` CI'da onun
  üzerinde, WARP'ta çalışır.
- **Düzeltildi**: GPU renderer'ın döndürülmüş, gerilmiş ve eğik kenarları, köşe noktalarını kaba bir ızgaraya oturtan
  GPU'larda (4 alt piksel biti, SwiftShader gibi: Chrome'un GPU kullanmadığı yerdeki WebGL'i) 10/255'e kadar
  sapıyordu: bir dörtgenin köşeleri böyle bir GPU'nun koyacağı yere konur, pikselleri oradan boyanır.
- **Düzeltildi**: GPU renderer'da döndürülmüş bir çizginin kare uçları köşelerinin en soluk piksellerini kaybediyordu:
  dörtgeni onlara uzanmıyordu.
- **Düzeltildi**: GCC 14 ile (metin alanının imleç tutamacı) ve MSVC ile derleme; MinGW toolchain dosyası verilen kök
  yolları siliyordu; macOS'ta ekransızken pencere kurulamıyordu (SDL onu OpenGL penceresi yapıyordu, bu orada EGL
  ister): artık software renderer ile çiziyor.

## 0.1.1 — 2026-10-18

- **Her biri ayrı bir API olan üç kaynaktan dinamik renk**; aralarında uygulama ya da kullanıcısı seçer (kütüphane
  kendiliğinden geçiş yapmaz): uygulamanın kendi şeması, sistemin paleti (`dynamicLightColorScheme()`: Android 12+'da
  duvar kağıdınınki, Linux'ta ve artık Windows'ta masaüstünün vurgu rengi) ve duvar kağıdının kendisi
  (`wallpaperLightColorScheme()`: Linux masaüstleri, Windows, Android 8.1+). Vitrin soruyor.
- **Herhangi bir resimden renkler**, Android'in duvar kağıdından seçtiği gibi: `TonalPalette::fromImage`,
  `quantizeColors` (Celebi nicemleyicisi) ve `seedColors` (System UI'ın sıralaması).
- **Belge sitesi**: `scripts/docs.py`, `docs/guide/`'dan İngilizce ve Türkçe bir HTML rehber üretir (içindeki her C++
  örneği `--check` ile derlenir); API durum sayfası da içinde.
- **Küçükler**: `Offset` `==` ile karşılaştırılır; betiğin `sleep ms` komutu; `KOMPOZ_NO_WALLPAPER_COLORS`; pencere
  gerektirmeyen platform kodunun testleri (`kompoz_platform_tests`).

## 0.1.0 — 2026-10-16

İlk açık yayın. Kompoz, Jetpack Compose'un modelini ve API'sini C++20'ye getirir; Linux, Windows, Android ve web'de
çalışır.

- **Runtime**: akıllı recomposition'lı ve atlamalı composable fonksiyonlar, `remember`/`rememberSaveable` (Android süreci
  bitirince de geri gelir), efektler, `CompositionLocal`, C++20 coroutine'leri, `Flow`/`StateFlow`/`SharedFlow`,
  `SavedStateHandle`'lı `ViewModel`, yaşam döngüsü, tür güvenli rotalar, iç içe grafikler ve derin bağlantılarla gezinme.
- **Yerleşim**: artımlı ölçüm, `Box`/`Row`/`Column`, `FlowRow`/`FlowColumn`, `FlexBox`, `Grid`, `Layout`,
  `SubcomposeLayout`, `LookaheadScope`, intrinsic'ler, pencere kenar boşlukları, sağdan sola yerleşim.
- **Listeler ve kaydırma**: tembel listeler, ızgaralar ve kademeli ızgaralar, pager'lar, carousel'ler, iç içe kaydırma,
  Android'in fling eğrisi, kenar esnemesi, yakalama, yenilemek için çekme, fare tekerleği.
- **Material 3** (Expressive bileşenleriyle Compose Material 3 1.4) ve dinamik renkli `MaterialTheme`: Android 12+'da
  duvar kağıdından, Linux'ta masaüstünün vurgu renginden, `TonalPalette::fromSeed` ile herhangi bir renkten.
- **Çizim ve metin**: `Canvas`, fırçalar, path'ler, şekiller (Material'in şekil kitaplığı ve biçim değiştirme), bulanık
  gölgeler, `graphicsLayer`, vektörler, HTTP'den yüklenen resimler, HarfBuzz ile şekillendirme, iki yönlü metin, seçim;
  geri almalı, otomatik doldurmalı ve donanım klavyesiyle düzenlenen metin alanları.
- **Girdi ve erişilebilirlik**: dokunma ve çoklu dokunma hareketleri, fare ve klavye, sürükle-bırak, tahmine dayalı geri
  hareketi; TalkBack için semantik ağaç.
- **Yerelleştirme**: kütüphanenin kendi metinleri (bağlam menüleri, erişilebilirlik açıklamaları, seçiciler) sistemin
  diline göre İngilizce ya da Türkçe (`LocalStrings`, `KOMPOZ_LANGUAGE`); uygulama kendininkini verebilir.
- **Araçlar**: pencere açmadan UI testleri, önizlemeler, `scripts/dev.sh` (kaydedince durumu koruyarak yeniden derleyip
  başlatma), betikle ekran görüntüsü, kare profili, `scripts/package.sh` (her platform için paket).

Neyin adım adım nasıl yapıldığı [docs/components.tr.md](docs/components.tr.md)'deki yapılış günlüğünde.
