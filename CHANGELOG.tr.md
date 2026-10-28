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
- **Kompoz'un kendi GPU renderer'ı**, yapım aşamasında (`KOMPOZ_GPU=vulkan`, `metal`, `webgpu`, `gles` ya da `auto`: platformun en iyisi, olmazsa sıradaki): her native grafik API'si için ince bir
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
- **Metal üzerinde GPU renderer** (`KOMPOZ_GPU=metal`, macOS ve iOS): Vulkan'daki ve OpenGL ES'teki resimlerin aynısı,
  CI'da Metal'in API doğrulaması açıkken; bir uygulama penceresinin katmanına, `kompoz_gpu_tests` (Apple'ın
  sistemlerinde varsayılan olarak Metal) yalnızca dokulara çizer.
- **WebGPU üzerinde GPU renderer** (`showcase.html?KOMPOZ_GPU=webgpu`): web'de Dawn'ın webgpu.h'ı, her yerdeki
  resimlerin aynısı (CI'da Chrome'un SwiftShader'ında ve bir telefonun Chrome'unda: Dawn üzerinden Adreno); bir web
  derlemesi, bir okumanın GPU'yu beklediği ASYNCIFY ile bağlanır. `kompoz_gpu_tests` Android'in Vulkan'ında da
  çalışır (`KOMPOZ_GPU=vulkan`, pencere olmadan).
- **GPU renderer'ın shader'ları bir kez derlenir**: Vulkan'ın pipeline önbelleği ve OpenGL ES'in program binary'leri
  uygulamanın önbellek dizininde tutulur (indirilen resimlerin yanında `pipelines-vulkan`, `pipelines-gles`;
  dosyayı `Surface::pipelineCache` söyler), böylece onları yalnızca uygulamanın ilk çalışması derler: bir telefonun
  Adreno'sunda ilk kare Vulkan'da 465, OpenGL ES'te 621 ms'de çiziliyordu, sonraki çalışmalarda 21 ve 22 ms'de
  çizilir. OpenGL ES'te yalnızca karışımı (blending) farklı olan pipeline'lar bir kez derlenen tek bir programı
  paylaşır.
- **Düzeltildi**: Android'in Vulkan'ında, telefon doğal duruşundan döndürülmüşken GPU renderer swapchain'ini her karede
  yeniden yapıyordu (her görüntüyü bileştiricinin döndürmesi gerekecekti, bu yüzden her present "suboptimal" idi):
  görüntüleri artık ekranın kendi yönünde ve kare oraya kopyalanırken döndürülüyor (`Device::swapchainTurns`),
  Android'in Vulkan uygulamalarından istediği gibi. Yana çevrilmiş bir telefonun Adreno'sunda bir ekran saniyede 38
  yerine 113 kareyle kaydı, karede 26 yerine 2.4 ms işle.
- **`KOMPOZ_PROFILE` GPU'yu ölçer**: Kompoz'un GPU renderer'ında her karenin GPU süresi ve geçişlerininki (Vulkan'ın ve
  WebGPU'nun zaman damgaları, OpenGL ES'in zamanlayıcı sorguları; Metal'de karenin tamamı), ekranın yenileme süresini
  aşan kare en pahalı geçişleriyle yazılır; gösterilen kareler ve aralarında kaçan yenilemeler; GPU'yu bekleme çizimden
  ayrı. Bir kare ekranın yenileme süresini aşınca yavaştır (8 ms'yi aşınca idi). Süreleri `Device::setGpuTiming` ve
  `takeGpuTimes` verir. Bir betiğin `realtime` komutu geri kalanını ölçmek için gerçek zamanda, kareleri ekranın hızında
  çalıştırır.
- **GPU renderer'ın shader'ı çizdiğine göre yapılır**: fragment shader'ı her batch için (Vulkan'ın specialization
  constant'ları, Metal'in function constant'ları, WebGPU'nun override'ları, OpenGL ES'in define'larıyla) onun türlerine,
  boyalarına, kırpmalarına, filtrelerine ve karışım biçimine göre özelleştirilir; böylece bir piksel artık bütün
  shader'ın her dalının bedelini ödemez. Bir Redmi Note 9 Pro'nun Adreno 618'inde vitrinin Bileşenler ekranının bir
  karesi GPU'da 400–460 yerine 30–50 ms, pencereye düz bir kopya 34 yerine 1.3 ms sürdü. Bir sürücünün bir varyantı
  yapamadığı yerde (o Adreno'nun Vulkan derleyicisi birini yapamadı) onu bütün shader çizer; sabitleri
  `PipelineDesc::constants` taşır.
- **Opak bir zeminin gizlediği çizilmez**: yeniden çizilen bir kare (GPU renderer'da da SDL_Renderer'da da), değişen
  alanın tamamını kaplayan ve yalnızca öteleme, ölçek ve dikdörtgen kırpma altında olan son opak düz dikdörtgenden
  başlar: oradan önce gelenler ve her şeyin altındaki beyaz atlanır (`DisplayList::opaqueCover`, `replay`'in `hidden`'ı).
  Intel GPU'sunda vitrinin Bileşenler ekranının çizimi 7.3–10.1 yerine 5.9 ms sürdü.
- **GPU renderer'ın geçişleri yalnızca çizdikleri yere dokunur**, döşemeli bir GPU'nun (bir telefonunkinin) istediği gibi:
  bir hedefin bir kısmına çizen geçiş yalnızca o kısmı yükleyip yazar (`beginPass`'in `area`'sı: Vulkan'ın render
  area'sı, OpenGL ES'te kırpılmış temizleme; kırpma dikdörtgenleri her yerde onun içinde kalır), kaplama maskesi
  yalnızca kullandığını temizler, pencereye kopya pencerenin tuttuğunu yüklemez (`Device::discard`: Vulkan'ın ve
  Metal'in "don't care"i, OpenGL ES'in geçersiz kılması; bir Adreno 618'in OpenGL ES'inde temizlemek yüklemenin dört
  katı tuttu), OpenGL ES ekrana kopyasından önce ekranı geçersiz kılar, Vulkan'da da tutulan kare pencerenin görüntüsü
  beklenmeden önce gönderilir. Bir karenin GPU süresi meşgul olduğu süredir, geçişlerinin toplamı; gönderimleri arasındaki beklemeler
  değil.
- **Bir karenin yolları tek bir kaplama maskesinde, bir kez çizilir**: bir flush'a kadar her geçişin (her katmanın da)
  yolları maskenin raflarını paylaşır ve geçişler çalışmadan önce tek bir geçişte rasterleştirilir; her katmanınkiler
  hemen önünde kendi maske geçişinde değil. Bir maskenin alabileceğini aşan bir kuşak kendi maskesine gider, böylece hiçbir
  geçiş onun için bölünmez. Bir Redmi Note 9 Pro'nun Vulkan'ında Bileşenler ekranının bir karesi GPU'da 48–50 yerine
  38–40 ms sürdü.
- **Düzen açılır pencereleri yalnızca varken arar**: açılır pencere taşıyan düğümler sayılır; hiçbiri yokken her
  düzenden sonra onları aramak için bütün ağacın gezilmesi atlanır.
- **Düzenden sonraki gezinti her öğenin ne olduğunu bir kez öğrenir**: bir düğüm, modifier'ının her öğesi için
  düzenden sonra ele alınan türlerden hangisi olduğunu (ve kırpıp kırpmadığını) öğeler değiştiğinde bulup saklar; her
  gezintide her öğede her tür denenmez. Bir Redmi Note 9 Pro'da Bileşenler ekranı kaydırılırken yavaş bir karenin
  compose ve düzeni bununla ve açılır pencere sayacıyla 15.9 yerine 13.7 ms sürdü (medyan).
- **Kaydırma tutulan pikselleri taşır** (Kompoz'un GPU renderer'ı): bir dikdörtgen clip içinde tam piksel kayan,
  kaydırılan bir listedeki gibi, tutulan karede taşınır, yeniden çizilmez; yalnızca görünüme giren şerit ve onunla
  kaymayanlar (bulunduğu yerde ve taşımanın götürdüğü yerde) çizilir. Taşınan piksellerin yanlış olacağı yerde (altları
  tek renk değilse, çevrelerinde başka biçimde bir clip ya da bir katman varsa, piksel parçası kadar kaydıysa) ya da o
  kadar çizilecekse, değişen eskisi gibi çizilir. Hasar, hepsini saran tek bir dikdörtgen değil, tam piksellerde en
  fazla sekiz ayrı alandır (`DisplayList::areas`, `moves`, `allowMoves`; `gpu::Renderer::drawFrame`, `move`); opak bir
  dikdörtgen, bir alanın dokunduğu tam pikselleri örttüğünde onu örter. Bir Redmi Note 9 Pro'da Bileşenler ekranı
  kaydırılırken bir kare GPU'da 27–38 yerine 4–14 ms sürdü, saniyede 11–41 yerine 50–60 kare gösterildi; ekranın altıda
  biri ile üçte ikisi yerine yirmide biri ile beşte biri yeniden çizildi.
- **İçeriği değişmeyen katmanlar piksellerini saklar** (Kompoz'un GPU renderer'ı): yalnızca solan, dönen, büyüyen ya da
  tam piksel kayan bir katman, sahip olduğu dokudan ve efektinin ondan yaptığından birleştirilir, yeniden çizilmez; bir
  katmanda çizilen, katmanın kendi uzayında geçen karedekiyle karşılaştırılır (tek renkli bir fırça alanı ne olursa
  olsun aynıdır), her katman düğümüyle tanınır (`GraphicsContext::keyLayer`, `layerContent`). Bir Redmi Note 9 Pro'da
  tepesinden öte çekilen bir liste karede GPU'da 43–44 yerine 27–33 ms sürdü. `KOMPOZ_PROFILE`, yavaş bir karenin
  katmanlarından kaçının saklandığını söyler.
- **Bir katmanın tuttuğu, dönüşümü ne olursa olsun içeriğidir**: bir katmanda çizilen, katmanların kendi dönüşümleri
  olmadan (dinlenme halinde) göründüğü yerde de, onlardan geçerek göründüğü yerde de kaydedilir; böylece ekranın ötesine
  büyüyen bir katman (daha çok çekilen bir listenin esnemesi) tuttuğunu tutar ve saklı kalır; küçülen bir katman ekrana
  getirdiğini yine alır. Bir Redmi Note 9 Pro'da tepesinden öte çekilen bir liste karede GPU'da 27–33 yerine 22–24 ms
  sürdü.
- **Düzenden sonraki gezinti yalnızca yapılacak bir şey olan yere gider**: bir düğümün öğelerinin ondan istedikleri
  (konumlar, görünürlük, hareket dışlama, ekranı açık tutma, kare hızı, özel bir gösterge) düğüm ölçülürken bulunur, her
  düğüm alt ağacındakileri sayar ve gezinti hiç olmayan alt ağaçları atlar. Bir Redmi Note 9 Pro'da Bileşenler
  ekranının bir karesinde 3.1 yerine 0.65 ms sürdü.
- **`KOMPOZ_PROFILE`'ın özeti işin parçalarını verir**: kompozisyon ve yerleşim, çizim ve çizimin içinde kayıt (ağacın
  görüntü listesine çizilip önceki kareyle karşılaştırılması).
- **Bir karenin yerinde tuttuğu kopyalanır, yeniden çizilmez**: aynı yerde ve durumda, değişmemiş bir düğüm geçen
  karenin komutlarını ve içindeki düğümler hakkında bildiklerini tek tek dolaşmadan olduğu gibi alır; çizilen bir
  düğüm yalnızca kendi yerini yazar. Bir Redmi Note 9 Pro'da boştaki bir ekranın kaydı 4 yerine 3.2 ms sürdü.
- **Bir bottom sheet scaffold'un sayfası hiçbir şey oluşturmadan kayar**: sayfanın nerede olduğu (sürüklenirken,
  yerine otururken) Compose'taki gibi scaffold oluşturulurken değil yerleştirilirken okunur: onu sürüklemek scaffold'u
  yeniden yerleştirir, ne onu ne içeriğini yeniden oluşturur.
- **Bir scaffold'un içerik dolgusu, içerik yeniden oluşturulmadan çubuklarını izler**: `Scaffold`'un ve
  `BottomSheetScaffold`'un dolgusu Compose'taki gibi kullanıldığı yerde okunur. `Modifier::padding`,
  `consumeWindowInsets` ve listelerin, ızgaraların, pager'ların, carousel'lerin ve düğmelerin `contentPadding`'i onu
  yerleşim sırasında okur; böylece bir liste kaydıkça daralan bir üst çubuk altındakini yeniden yerleştirir, hiçbir
  şey oluşturmaz. `PaddingValues`'un `calculateTopPadding()`'i (ve start, end, bottom) onu şimdi okur, nerede
  çağrılırsa (onu okuyan bir composable, dolgu değişince yeniden oluşturulur); alanları içerik oluşturulduğu andaki
  değeri tutar ve böyle iki dolgu aynı scaffold'u izliyorsa aynıdır. (`ButtonDefaults`'un dolguları artık
  `constexpr` değil, `const`.) Showcase'in Bileşenler ekranını daralan çubuğunun altında kaydırmak ekranı bir kez
  oluşturdu.
- **`animate*AsState` bir durum verir, gerektiği yerde okunur**, Compose'taki `State<T>` gibi: bir `AnimatedState<T>`.
  Bir composable'da değer olarak kullanılırsa, önceden olduğu gibi o scope hareket ettiği her karede yeniden
  oluşturulur; çizim sırasında `.value()` ile okunursa (`drawBehind`, `graphicsLayer { }`) yalnızca o düğüm yeniden
  çizilir, yerleşim sırasında okunursa yalnızca o yeniden yerleştirilir; hiçbir yerde okunmazsa kare istemez. Yoldayken
  verilen yeni bir hedefe o anda bulunduğu yerden gider. (Sonucu `auto` ile tutup `float` ya da `Color` gibi, `std::max`
  ile ya da bir üyesiyle kullanan kod `.value()` alır.)
- **Bir composition local bir durumdan sağlanabilir**: `LocalContentColor.provides(animateColorAsState(...))`, local'i
  okuyanlara durumun değerini okudukları yerde verir; böylece o hareket ettikçe onu sağlayan composable değil, yalnızca
  onlar yeniden oluşturulur (bir `Icon`, bir `Text`).
- **Bir üst uygulama çubuğu, ilk nasıl oluşturulmuş olursa olsun kaydırılmış rengini alır**: durumunun yükseklik
  kayması sınırı Compose'taki gibi bir durumdur; böylece içeriğin çubuğun altına ne kadar girdiğini okuyan şey, çubuk
  sınırı koyduğunda onu izler. Bundan önce hesaplandığında (çubuğun ilk oluşturulmasında) kaydırılmış kap rengi içeriği
  hiç izlemiyordu ve başka bir sebeple yeniden oluşturulmadıkça çubuk rengini koruyordu.
- **Gezinme öğeleri ve üst uygulama çubukları çevrelerini oluşturmadan canlanır**: gezinme çubuğunun, kısa gezinme
  çubuğunun ve geniş rayın öğeleri göstergelerini yerleştirip çizerken okur, simgelerine ve etiketlerine canlanan
  renklerini durum olarak verir (yalnızca onlar yeniden oluşturulur); bir üst uygulama çubuğunun kap rengi çizilirken
  geçer. Çizim sırasında okunan bir animasyon, çizilsin çizilmesin bitene kadar kare getirir (hiç bitmeyen biri,
  çizildiği sürece) ve `ComposeTestRule::waitForIdle` onu bekler.
- **Geçişler çevrelerini yalnızca başlarken ve biterken oluşturur**: `AnimatedVisibility`, `AnimatedContent` (dolayısıyla
  `NavHost` ve `Crossfade`) ve `animateEnterExit` ilerlemelerini Compose'takiler gibi içerik yerleştirilip çizilirken
  okur ve çağıranlarını yoldaki her karede değil, vardıklarında (o zamana istenen bir karede) yeniden oluşturur. Yolda
  döndürülen bir geçiş, o karede bulunduğu yerden geri gider. Gezinme öğelerininki ve uygulama çubuklarınınkiyle
  birlikte, bir Redmi Note 9 Pro'da showcase'in sekmeleri arasında beş kez gidip gelmek ana thread'de 3.9 yerine
  2.9–3.1 s, bunun kompozisyonu ve yerleşimi 3.3 yerine 2.3 s sürdü.
- **Düğmeler, FAB'ler ve biçim değiştiren şekiller çevrelerini oluşturmadan canlanır**: bir düğmenin ve bir FAB'in
  yükseltisi, bir açma-kapama düğmesinin renkleri, genişletilmiş bir FAB'in genişliği ve etiketi ve bir düğme
  grubunun basılı tutulan düğmesi yerleştirilip çizilirken okunur; biçim değiştiren bir şeklin köşeleri de öyle
  (basılan ya da seçilen bir düğmeninki: bir `Shape`'in `animatedCorners`'ı, ana hattı çıkarıldığı yerde okunur; bu yüzden
  `radiusFor`, `radiiFor` ve `cornersFor` artık `constexpr` değildir). Bir
  clickable'ın ve bir indication'ın dalgası içerik rengini çizilirken alır (`CompositionLocal::currentLive`); böylece
  bir durumdan sağlanan bir renk onları yeniden oluşturmaz.
- **Seçim denetimleri çevrelerini oluşturmadan canlanır**: bir onay kutusunun renkleri ve işareti, bir anahtarın
  topuzu, bir radyo düğmesinin halkası ve noktası, bir liste öğesinin kabı, bir bölümlü düğmenin işareti ve etiketi,
  bir sekmenin göstergesi ve rengi, bir kaydırıcının topuzu ve bir sürükleme tutamacı yerleştirilip çizilirken okunur;
  işaretlendiklerinde, seçildiklerinde ya da tutulduklarında çağıran bir kez yeniden oluşturulur.
- **Menüler, ipuçları, alanlar ve göstergeler çevrelerini oluşturmadan canlanır**: bir menünün ve bir ipucunun giriş
  ve çıkışı, modal geniş rayın kayması ve perdesi ile geniş rayın genişliği, bir modal alt sayfanın perdesi, bir metin
  alanının yüzen etiketi ve göstergesi, bir açma-kapama FAB'inin boyu, köşeleri ve renkleri, dalgalı bir göstergenin
  dalgası, kaydırma çubuğunun rengi, işaretli bir menü öğesi, bir çipin simgesi, çekip yenileme oku, açılır menü ve
  bölünmüş düğme okları ve canlandırılmış bir vektörün resmi (`rememberAnimatedVectorPainter`) yerleştirilip çizilirken
  okunur; çıktığında giden şey, vardığında yeniden oluşturulur. `ToggleFloatingActionButtonScope::checkedProgress()`
  ilerlemeyi çağrıldığı yerde okur (bir `graphicsLayer` içinde yalnızca o yeniden çizilir).
- **Her şey Kompoz'un GPU renderer'ıyla çizilir**: SDL_Renderer'la çizim kaldırıldı. `KOMPOZ_GPU` verilmezse uygulama
  onu kullanıyordu; böylece bir telefon ya da masaüstü, adıyla istenmedikçe GPU renderer'ının hiçbir işini
  çalıştırmıyordu. Artık verilmezse (ya da `auto`) platformun en iyisi seçilir: Metal, WebGPU sonra WebGL 2, Vulkan sonra
  OpenGL ES 3. Hiçbiri başlamazsa uygulama bir iletiyle biter. Pencere yokken (`SDL_VIDEO_DRIVER=offscreen`) Vulkan ve
  Metal yalnızca saklanan kareye çizer, ekran görüntüleri ondan alınır. Vulkan sürücüsü olmayan bir Windows bilgisayarı
  (sanal makine, Uzak Masaüstü, CI) uygulamayla gelen ANGLE üzerinden çizer.
- **Bir betikle çalışan ve başlayamayan uygulama bunu günlüğünde söyler**: `KOMPOZ_SCRIPT` verildiğinde (CI'daki gibi)
  birinin kapatmasını bekleyen bir ileti kutusu açılmaz. Conan Center'ın SDL'inde Windows'ta OpenGL ES yoktur; bu yüzden
  Conan ile derlenen bir uygulama orada yalnızca Vulkan ile çizer (ANGLE, EGL'li bir SDL ister); Conan testi Windows
  için derlenmiş Mesa lavapipe'ında çizer.
- **Bir katman ilk karesinden itibaren tutulur**: ilk kez görülen bir katman (başlayan bir solma) zaten bütünüyle
  çizilir ve artık o andan itibaren tutulur; içeriği aynı kalan katman sonraki karede bu piksellerden birleştirilir,
  eskiden ikinci kez bütünüyle çiziliyordu.
- **Bir pikselin adımları her ilkel için bir kez hesaplanır**: bir cihaz pikselinin bir ilkelin kendi uzayında ne kadar
  yol aldığı (kenar yumuşatmanın ölçüsü), her pikselde türevlerden geliyordu; bu, bir Adreno GPU'ya bir dolgunun geri
  kalanı kadar pahalıya mal oluyordu. Perspektif yokken her yerde aynıdır ve köşe gölgelendiricisi onu ilkelin
  yerleşiminden hesaplar; perspektifteki bir ilkel, türevli bir gölgelendirici sürümünü alır. Bileşenler ekranının
  tamamını çizmek bir Xiaomi 14 Ultra'nın GPU'sunda 4,9 yerine 4,1–4,2 ms sürdü.
- Android'de **`kompoz-full-redraw`**, başka yerlerdeki `KOMPOZ_FULL_REDRAW` gibi: her kare sıfırdan çizilir; ekranın
  tamamını çizmenin maliyeti için.
- **Yuvarlak bir kırpmanın dışına taşmayan kırpılmaz**: yuvarlak bir kırpmanın kenarlarının içinde ve köşelerinden
  uzakta kalan bir ilkel (bir kartın içeriği), zaten her pikselini tamamen kaplayacak olan kırpma olmadan çizilir:
  onun için piksel başına kırpma yok. Bileşenler ekranının tamamını çizmek bir Xiaomi 14 Ultra'nın GPU'sunda 5,3–5,6
  yerine 4,9 ms sürdü.
- **Yuvarlak köşeli dikdörtgenin içi GPU'ya neredeyse bedava**: bir pikselin yuvarlak köşeli dikdörtgen tarafından ne
  kadar kaplandığı (dolgular, katmanlar, yuvarlak kırpmalar) her pikselde kesin olarak hesaplanıyordu; kutusu bütün
  kenarların içinde kalan ve bir eksende köşe yarıçaplarını aşan piksel, kesin toplamın da söylediği gibi tamamen
  kaplıdır ve artık öyle alınır. Showcase'in Bileşenler ekranının tamamını çizmek bir Xiaomi 14 Ultra'nın GPU'sunda
  6,1 yerine 5,3–5,6 ms sürdü (yalnızca yuvarlak dikdörtgenler 6 yerine 4,2).
- **Android'de boştaki uygulama işlemciyi rahat bırakır**: SDL'in `SDL_WaitEvent`'i Android'de beklemek yerine
  dönüyordu (olayları pompalamak SDL'in yoklama sınır olayını kuyruğa ekler, bu da beklemeyi hemen uyandırır); bu
  yüzden yapacak bir şey olmadığında uygulamanın ana iş parçacığı bir çekirdeği tamamen meşgul ediyordu: saniyede
  yaklaşık bir milyon tur. `scripts/patches/` bunu SDL 3.4.18'de düzeltir (SDL'in ana dalında da düzeltilmemiş). Bir
  Redmi Note 9 Pro'da ölçümün ana iş parçacığı 30 yerine 13 s işlemci zamanı aldı.
- **Ekranın her vuruşuna bir kare**: bir öncekinden yarım tazelemeden kısa süre sonra başlayan bir kare (öncekinin
  beklemeden gösterildiği, takas zincirinde yer olduğu ya da önceki ekranda hiçbir şeyi değiştirmediği için) öncekinin
  zamanını alıyordu; kaydırma o karede yerinde sayıp sonrakinde iki tazeleme birden atlıyordu: 120 Hz'lik bir
  telefonda kaydırırken durmadan gösterilen, duran, duran, gösterilen. Artık kare ekranın bir sonraki vuruşunu bekler
  (gösterilmeyen kare de; kendi beklemesinin yerini alır). Bir Redmi Note 9 Pro'da showcase'in Bileşenler ekranını
  kaydırırken saniyede 5–17 yerine 2–6 tazeleme kaçtı.
- **Görüş dışında çizilen kare istemez**: bir düğümün çizerken istediği kareler (çizilerek ilerleyen bir animasyon, bir
  dalga, belirsiz bir gösterge) yalnızca çizdiklerinden biri görünüyorsa istenir (bir `DisplayList`, kırpmalarının hiçbir
  şey bırakmadığı çizimlerini bilir: `GraphicsContext::endedNodeShows`). Kaydırılıp görüş dışına çıkan hiç bitmeyen bir
  animasyon kare istemez ve olan ilk karede yeniden çizilir; geri geldiğinde zamanının getirdiği yerden sürer. Bir kez
  çizilen hiç bitmeyen bir animasyon, gittikten sonra artık kare getirmeyi sürdürmez.
- **Kayan yazı çizilirken kayar**: `basicMarquee`, geçişinin ne kadar ilerlediğini her karede yazan bir eşyordam
  yerine çizerken karenin zamanından alır; geçişler arasında ve bir geçiş bittiğinde bir zamanlayıcıyla bekler. Görüş
  dışında kare istemez, yalnızca zamanlayıcıları kalır.
- **Showcase kimsenin görmediğini durdurur**: dönen çarkı ve yaşam döngüsü bölümünün oynatıcısı (döngüdeki bir
  `Animatable`, tıklayan bir eşyordam), bir uygulamanın yapacağı gibi görüş dışındayken durur (`onVisibilityChanged`);
  sayfa kaydıkça her karede onu izleyen konum bölümü bölümün tamamını değil, yalnızca iki sayı satırını yeniden oluşturur.
  Görüş dışında çizilenin bir şey istememesiyle birlikte, masaüstünde kendi haline bırakılan showcase'in Bileşenler
  ekranı saniyede 60 kareden 2'ye indi (yaşam döngüsü bölümünün saati).
- **Kaydırmak kayanı dolaşmaz**: yeniden ölçülmeden taşınan bir şey (bir listenin içeriği) içindeki yerleri yazıldıkları
  gibi bırakır; istendiklerinde hesaplanır, çizildiklerinde yazılırlar. Showcase'in Bileşenler ekranını kaydırmak ana
  thread'in %16 yerine %1'ini yerlere harcadı.
- **OpenGL ES kareyi doğrudan pencereye çizer**: üzerine kopyalanan bir dokuya değil, ters çizilen kendi
  framebuffer'ına; gösterilen her karede pencerenin tamamının bir kopyası eksik.
- **Bir composition local'ın okuyucuları sağlayıcısınındır**: başka bir değer veren sağlayıcı, onu kendisinden okuyan
  scope'ları yeniden kompoze eder; bunlar local'ın her okuyucusu dolaşılıp her birinden yukarı çıkılarak değil, hemen
  bulunur. Kompozisyondan çıkan bir scope okuyuculardan da hemen çıkar. Showcase'in sekmelerini değiştirmek ana
  thread'in beşte bir daha azını aldı.
- **Resim varlıkları bir kez çözülür**: `imageResource`, aynı yol için nerede istenirse istensin ve ekranı kaç kez
  geri gelirse gelsin aynı bitmap'i verir, Android'in kaynaklarınınkini tuttuğu gibi (host son kullanılanları 32 MB
  piksele kadar tutar). Masaüstünde showcase'in Bileşenler ekranı ilk seferde 35 yerine 28 ms'de, sonra 18 yerine
  9 ms'de açıldı.
- **Android 13 ve sonrasında ADPF**: her karenin işi (beklemeleri hariç) ekranın yenileme süresine karşı sisteme
  bildirilir (`APerformanceHint`); sistem de kareleri çizen thread'e sığması için gereken çekirdeği ve frekansı verir.
- **Kare zamanları ekranın vuruşuna düşer**, Android'in Choreographer'ının verdiği gibi: son karenin zamanından tam
  sayıda yenileme süresi sonra, şimdiye en yakın olanı; böylece her kare kendi süresinin ne kadar geç bir yerinde
  başlarsa başlasın animasyonlar eşit adımlarla ilerler (`detail::frameTimeOnBeat`).
- **`KOMPOZ_PROFILE` pencerenin görüntüsünü beklemeyi bekleme sayar**, çizim değil: ekranın FIFO sunumunda bu, bir
  sonraki yenilemenin bir görüntüyü geri vermesini bekler.
- **Çizimi değişmeyen düğümler yeniden çizilmez**, Compose'un render düğümleri gibi: kare, böyle bir düğümün ve
  içindeki her şeyin geçen kare çizdiğini, düğüm ne kadar kaydıysa o kadar kaydırarak (kaydırılan bir liste) yeniden
  kaydeder. Bir düğüm ölçüldüğünde, çiziminin okuduğu bir durum değiştiğinde, çizerken yeniden çizim istediğinde ve
  basıldığında, üzerine gelindiğinde ya da odaklandığında veya dalgası ilerlediğinde yeniden çizilir. `KOMPOZ_KEEP_CHECK`
  (ve testler) her kareyi her düğümü çizen bir kareyle karşılaştırır; `KOMPOZ_PROFILE`'ın yavaş kareleri çizilen ve
  tutulan düğümleri sayar. Bir Xiaomi 14 Ultra'da Bileşenler ekranının ekranda hiçbir şeyi değiştirmeyen bir karesi 5
  yerine 0.4 ms sürdü.
- **Düzeltildi**: eşi kendisinden sonra çizilen bir paylaşılan öğe, eşinin yerini ancak yeniden çizildiğinde görüyordu:
  iki kez çizilen bir kare (bir büyütecinki) onu iki yerde çiziyordu. Her birinin ait olduğu yer, hiçbiri çizilmeden
  önce not edilir.
- **Bir kareyi kaydetmek düğüm başına bellek ayırmaz**: görüntü listesinin eşlemleri (her düğümün kaç kez çizildiği,
  önceki karenin segmentleri ve katmanları) boşaltılırken serbest bırakılmayan tek bir açık adresleme tablosudur; çizim
  bir elemanın kaydırılabilir olup olmadığını cast ile sormaz. Bir Redmi Note 9 Pro'da boştaki bir ekranın kaydı 5.7
  yerine 4.4 ms sürdü.
- **Düzeltildi**: Platformun işaretçinin ayrıldığını söylemediği yerde kalkmış bir parmak altındakinin üzerinde
  duruyor sayılıyordu: dokunulan bir tooltip çapası tooltip'ini gösteriyor, çizilen her kare hover için isabet testi
  yapıyordu. Hover artık yalnızca duran bir fareyi izler.
- **Düzeltildi**: Bir Adreno 618'de (bir Redmi Note 9 Pro'nunki) GPU renderer clip'leri, renk filtrelerini ve
  gradyanları karenin tablosunun kendilerininkinden başka satırlarından çiziyordu; önlerinde ne kadar çok varsa o kadar
  uzak (biçimine kırpılmış bir düğmenin köşesi 220/255'e kadar sapıyordu): tablonun texel'leri örneklenmez, olduğu gibi
  okunur.
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
