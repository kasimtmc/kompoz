# Değişiklikler

[English](CHANGELOG.md) · **Türkçe**

## Yayımlanmadı

- **Linux'ta ekran okuyucular**: semantik ağaç AT-SPI üzerinden sunuluyor (libdbus çalışırken yükleniyor); Orca Kompoz
  uygulamalarını okuyor ve kullanabiliyor: roller, adlar, durumlar, değerler ve metin, eylemler, değişenler için
  olaylar. Masaüstünün erişilebilirliği açıkken açık; zorla açmak ya da kapatmak için `KOMPOZ_ATSPI=1` / `0`.
- **Windows'ta ekran okuyucular**: semantik ağaç UI Automation üzerinden Ekran Okuyucusu'na ve NVDA'ya sunuluyor:
  denetim türleri, adlar, durumlar, konumlar, düğümleri kullanmak için desenler ve değişenler için olaylar.
- **Kurulum ve paketler**: Linux'ta, macOS'ta ve Windows'ta `cmake --install` ve `find_package(kompoz)`
  (`kompoz::core`, `kompoz::app`, `kompoz_copy_assets`); sistemin FreeType ve HarfBuzz'ıyla ya da `third_party/`'den
  derlenip yanına kurulanlarla. vcpkg overlay port'u ve Conan reçetesi; AsyncImage'ın kütüphaneleri özellik ve seçenek
  olarak.
- **libjpeg-turbo 2.x ile de JPEG** (3.x'in yanında); AsyncImage'ın kütüphaneleri CMake paketlerinden ya da
  pkg-config'ten bulunuyor, `KOMPOZ_WITH_JPEG` / `WEBP` / `CURL` her birini otomatik, zorunlu ya da kapalı yapıyor.
- **Her platformda CI**: testler, platform testleri ve Kompoz'un her kurulum yoluyla derlenip çalıştırılan, tıklanan
  bir uygulama (Windows'ta ve artık Kompoz'un çalıştığı macOS'ta gerçek pencerede, macOS'ta ekransız da).
  `CONTRIBUTING.md`, issue formları ve bir sürümün paketlerini yayımlayan `scripts/release.sh`. vcpkg işleri
  sınadıkları kaydın Kompoz'unu derliyor (vcpkg'nin ikili önbelleği port dosyalarına göre anahtarlandığından,
  kaynaklara göre değil, daha önceki bir kaydınkini geri yüklüyordu). Windows işi vitrini yanında ANGLE'ın DLL'leri ve
  onların ihtiyaç duyduğu Visual C++ çalışma zamanıyla indirilebilir olarak saklıyor
  (`kompoz-showcase-windows-x86_64`): GPU sürücüsü olmayanlar dahil her Windows 10+'da çalışıyor.
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
  birinin kapatmasını bekleyen bir ileti kutusu açılmaz. Conan testi Windows için derlenmiş Mesa lavapipe'ında çizer.
- **KOMPOZ_PROFILE yalnızca beklenen karelerin kaçırdığı tazelemeleri sayar**: gösterilen iki kare arasında
  uygulama meşgulken geçen tazelemeleri sayıyordu; çalışan bir betik de (ölçümlerdeki gibi) ekranda hiçbir şey
  değişmezken karelerini beklerken uygulamayı meşgul gösteriyordu: yarım saniyelik bir duraklama 60 kadar kaçan
  tazeleme çıkıyordu. Artık yalnızca uygulamanın gösterdiği şey kare isterken (`ComposeHost::wantsFrame`: bir
  zamanlayıcı, başka bir iş parçacığındaki iş ya da bir betik değil) ve istemeye başladığı andan itibaren sayar.
  (Yukarıdaki girdilerdeki ölçümlerin "kaçan" sayıları bu duraklamaları da sayar: ilk saniye, sekmenin açılışı, 60–70
  tanesini içeriyordu.) Xiaomi 14 Ultra'nın ölçümü, sekme açılıp Bileşenler ekranı on kez aşağı ve yukarı kaydırılınca,
  toplam 5 tazeleme kaçırdı.
- **Tembel bir listenin ilk karesi görünümünü dolduracak kadarını oluşturur**: henüz hiçbir öğe ölçülmemişken
  öğeleri 48 dp sayıp bir buçuk görünüm dolusunu oluşturuyordu; uzun öğelerde (Bileşenler ekranının bölümleri) bu,
  neredeyse hepsini boşuna oluşturup ölçmek demekti. Artık ilk öğeyi oluşturur, sonra aynı karede boyların söylediği
  kadarını; Compose'un öğeleri ölçerken oluşturması gibi. Bileşenler sekmesini ilk kez açmak masaüstünde 18 ms yerine
  5 ms'nin altında kompozisyon ve yerleşim sürdü. (Odaklanan ya da görünüme getirilen şeyin oluşturulmuş olması
  gerekir, Compose'daki gibi: görünümün yarım görünüm ötesindeki bir öğe artık tahmin kısa kaldı diye oluşturulmuş olmaz.)
- **Bir yolun kapsaması kareden kareye tutulur**: GPU renderer'ının olduğu gibi ya da tam piksellerle kaymış olarak
  yeniden çizdiği bir yol (kaydırılan bir listedeki, beliren ya da kaybolan bir ekrandaki), daha önce taradığı
  kapsamayı kendine ait bir maskeden alır (her yönde 2048 piksel; ondan 512 × 512 piksele kadar, her yönde yarısına
  kadar yer kaplayan yollar); yol pikselin 64'te birine yerleştirilir. Yalnızca yeni yollar taranır, karenin
  geçişlerinden önce. Bu maske dolunca kareden sonra baştan başlar; o karenin yolları maskenin çoğunu kendileri
  dolduruyorsa başlamaz: içindekiler tutulur, kalanlar eskisi gibi çizilir, her karede hepsi yeniden taranmaz. Bir
  Redmi Note 9 Pro'da Ana sayfa ile Bileşenler sekmeleri arasında beş kez gidip gelmek 36 yerine 20–22 tazeleme
  kaçırdı. `KOMPOZ_PROFILE` yavaş karelerinde çizilen ve tutulan yolları sayar.
- **Kaydırmanın ve yol efektlerinin bıraktığı eskimiş pikseller giderildi**: değişen, çiziminin ulaştığı yere
  kadar yeniden çiziliyor; `KOMPOZ_DAMAGE_CHECK` artık showcase'in Bileşenler ekranında 255'te birden fazla sapan
  piksel bulmuyor, oysa karelerin üçte ikisinde 150'ye kadar sapanlar buluyordu. Bir yol efektinden geçerek çizilen
  yol, efektin söylediği kadar yolun ötesine ulaşıyor (`PathEffect::reach`: bir damganın konduğu yere en uzak köşesi,
  Skia'nın computeFastBounds'unun bir efektin sınırlarını genişletmesi gibi): bir dalga boyunca damgalanan oklar
  kayarken uçlarını geride bırakıyordu. Bir yolun çevresindeki gölge, çiziminin ulaştığı yere ulaşıyor
  (`detail::shadowPathReach`, sivri yayılmanın uçlarıyla). Yükselti gölgesi olan bir öğe de artık kaydırılan bir
  listenin pikselleriyle taşınmıyor: gölgesi yerinde kalan ışıktan düşüyor, taşınınca eski yerindeki gibi
  görünüyordu; yeniden çiziliyor.
- **Kompoz'un kendi platform katmanı**, SDL3'ün yerine kuruluyor (pencere, girdi, kare döngüsü ve uygulamanın
  yaşam döngüsü, her platformda kendisininki; kareler Compose'unkiler gibi ekranın kendi vuruşunda): uygulama
  çekirdeği artık Kompoz'un kendi olaylarını alıyor (`platform/app/platform_event.hpp`), SDL'inkiler geldikleri yerde
  bunlara çevriliyor; `KOMPOZ_SCRIPT` de SDL'in kuyruğuna SDL olayı koymak yerine çekirdeğe kendi olaylarını veriyor.
  Uygulama çekirdeği (`platform/app/app_core.cpp`: kare, çizim, profil, betik, denetimler) üzerinde çalıştığı
  platformdan ayrıldı; ihtiyaç duyduğunu ondan `platform::Platform` üzerinden istiyor (penceresi ve GPU yüzeyi,
  ölçüleri, ayarları, saat, günlük), karelerinin zamanını da o belirliyor; altındaki ilk platform SDL. Android'de artık
  yalnızca birkaçı değil, her `KOMPOZ_` ayarı uygulamanın dosyalarındaki adını taşıyan bir dosyayla verilebiliyor.
  Metin, resimler, ekran okuyucu köprüleri ve masaüstünün renk okuyucuları artık SDL kullanmıyor: günlük, uygulamanın
  dosyaları (Android'in APK varlıkları platformun verdiğiyle), programın klasörü ve önbellek klasörü
  `platform/common/system`'in; kendi iş parçacıklarındaki modüller platforma SDL olayları yerine bir
  `platform::Notifier` ile haber veriyor; platform testleri sonlarında libdbus'un tuttuğunu `SDL_Quit`'in yaptığı gibi
  serbest bırakıyor. Her platform modülü artık `kompoz::platform` içinde, ait olduğu yerde: her platformun
  paylaştıkları `platform/common/`'da (fontlar, resimler, duvar kâğıdının renkleri), masaüstlerinin kendilerine özgü
  olanları `platform/linux/` ve `platform/windows/`'da, Android'inkiler `platform/android/`'da; `platform/sdl3/`'te
  yalnızca SDL platformu kaldı.
  PNG libpng ile okunuyor, ekran görüntüleri ve GPU testlerinin resimleri de onunla yazılıyor (sistemin, vcpkg'nin ya da
  Conan'ın libpng'si, yoksa zlib'le birlikte `third_party/`'den derlenen: `scripts/fetch-deps.sh`); dosyanın gaması ve
  renk alanı Android'in ve tarayıcıların yaptığı gibi hesaba katılıyor, gaması hakkında bir şey söylemeyen 16 bitlik
  bir dosya sRGB olarak okunuyor. BMP'yi Kompoz kendisi okuyor (piksel başına 1'den 32 bite, sıkıştırmasız ya da bit
  alanlarıyla); libpng'nin ve zlib'in lisansları da ötekilerle birlikte `assets/licenses`'ta. Masaüstlerinde
  `KOMPOZ_HEADLESS=1` bir uygulamayı SDL'in ekransız sürücüsü (`SDL_VIDEODRIVER=offscreen`) yerine Kompoz'un kendi
  ekransız platformunda çalıştırıyor: penceresi yalnızca bir boyut, hemen gösterilmiş ve odaklı; işaretçisi, klavyesi,
  mikrofonu yok; masaüstünün vurgu rengini de duvar kâğıdınınkini de okumuyor; döngüsü ve zamanlayıcıları Kompoz'un.
  GPU aygıtı pencere olmadan kuruluyor: Vulkan sistemin yükleyicisinden, OpenGL ES 3 EGL üzerinden (Mesa'nın yüzeysiz
  platformu; Windows'ta ANGLE'ınki). GPU testleri aygıtlarını aynı yolla, web'de WebGL 2 bağlamlarını doğrudan
  Emscripten'den alıyor; SDL'siz. **Android artık SDL kullanmıyor**: bir uygulamanın etkinliği Kompoz'un kendi
  `dev.kompoz.KompozActivity`'sini genişletiyor; Kompoz onun `KompozView`'una (bir `SurfaceView`) `ANativeWindow`'u
  üzerindeki bir Vulkan yüzeyi ya da EGL pencere yüzeyiyle çiziyor. Uygulama, Compose'daki gibi etkinliğin UI iş
  parçacığında çalışıyor; kareleri ekranın Choreographer'ından geliyor (vsync zamanı karenin zamanı), dokunmaları,
  faresi, tuşları ve yaşam döngüsü görünümden ve etkinlikten. Başka iş parçacıklarından gelen işler UI iş parçacığının
  looper'ını uyandırıyor (bir eventfd), coroutine gecikmeleri de (bir timerfd); her vuruştan kalan zaman bir tembel
  listenin sıradaki öğelerini önceden kompoze ediyor. Uygulamanın statik durumu süreç başına tek uygulama için: bir
  uygulamayı çalıştırmış bir süreçte yeniden oluşturulan etkinlik süreci bitiriyor ve sistem uygulamayı yeniden
  başlatıyor, SDL'inkinin yaptığı gibi. Bir Xiaomi 14 Ultra'da (120 Hz) ve bir Redmi Note 9 Pro'da (60 Hz) SDL'li
  yapıyla karşılaştırıldığında elle kaydırırken ve fırlatmalarda aynı sayıda yenileme kaçırılıyor; SDL'in Android
  tarafı için tuttuğumuz üç yama da onunla birlikte gitti, vitrinin alt satırı da artık SDL3 demiyor.
  **Bir Wayland oturumu Kompoz'un kendi Wayland platformunda çalışıyor** (`platform/linux/wayland`): bileşikleyicinin
  çerçevesini çizdiği bir `xdg_toplevel` (`xdg-decoration`), çizmediği yerde (GNOME; `KOMPOZ_DECORATIONS=kompoz` ile
  her yerde) Kompoz'un kendisinin: uygulamanın üstüne sistemin renkleriyle (koyu teması, vurgu rengi) oluşturduğu bir
  başlık çubuğu; başlık ortada, düğmeler masaüstünün dizdiği gibi (GNOME'un `button-layout`'u), sürüklenince pencereyi
  taşıyor, çift tıklanınca büyütüyor, ikincil düğmede pencere menüsü; pencerenin çevresinde de işaretçinin ya da
  parmağın onu boyutlandırdığı gölgeler; bileşikleyicinin ölçeğinde (kesirli ölçek ve viewporter, yoksa yüzeyin tam
  sayı ölçeği) Vulkan (`VK_KHR_wayland_surface`) ya da OpenGL ES (bir `wl_egl_window`) ile çiziliyor, kareleri
  bileşikleyicinin kare geri çağrılarında. İşaretçi, tekerlek (bir çentik 1), dokunma, touchpad sıkıştırmaları
  (`pointer-gestures`) ve klavye (xkbcommon; tuşlar bileşikleyicinin hızında yineleniyor), `text-input-v3` üzerinden
  yazma da; data device üzerinden pano ve pencereye bırakılan dosyalar ya da metin, imlecin şekli (`cursor-shape`),
  ekranı açık tutma (`idle-inhibit`), bağlantılar için `xdg-open`, açık ya da koyu tercih portalın
  `color-scheme`'inden. `libwayland-client`, `libwayland-egl`, `libxkbcommon` ve EGL çalışma anında yükleniyor,
  protokollerin kodu önceden üretiliyor (`scripts/wayland-protocols.sh`); derlemek için yalnızca başlıkları gerekiyor.
  Bunlar yoksa bir Wayland oturumunun uygulamaları X11 platformunda (XWayland) çalışıyor. **Bir X11 oturumu Kompoz'un
  kendi X11 platformunda çalışıyor** (`platform/linux/x11`): pencere yöneticisinin çerçevesini çizdiği bir pencere;
  masaüstünün ölçeğinde (`Xft.dpi`, değiştikçe izleniyor) Vulkan (`VK_KHR_xlib_surface`) ya da OpenGL ES (EGL'in X11
  platformu) ile çiziliyor, kareleri ekranın vuruşunda. İşaretçi, tekerleği (XInput 2'nin söylediği gibi yumuşak),
  dokunma ve touchpad sıkıştırmaları XInput 2 üzerinden; klavye bir giriş yöntemiyle (XIM, söylenebildiği yerde alanın
  yeri de söyleniyor); pano (büyükse parça parça) ve dosya ya da metin bırakmalar seçimler olarak (XDND); masaüstü
  temasının imleçleri (libXcursor), XRandR'dan yenileme hızı, `org.freedesktop.ScreenSaver` üzerinden ekranı açık
  tutma. libX11, libXi, libXcursor ve libXrandr çalışma anında yükleniyor; `KOMPOZ_PLATFORM=x11` onu bir Wayland
  oturumunda da XWayland üzerinden çalıştırıyor. İkisinin ortak kullandıkları (tuşlar, bağlantılar, masaüstünün
  hizmetleri) `platform/linux/desktop_services`'ta, EGL üzerinden OpenGL ES `platform/common/egl_window`'da (derlemek
  için EGL başlığı gerekmiyor; Windows'ta ANGLE'ınkiyle de). **Windows, Kompoz'un kendi Win32 platformunda çalışıyor**
  (`platform/windows`): her monitörün DPI'ını bilen bir pencere (monitör başına v2; uygulamanın dp boyutu açıldığı
  monitörde), başlık çubuğu Windows'un koyu moduyla koyulaşıyor; Vulkan (`VK_KHR_win32_surface`) ya da OpenGL ES
  (programın yanındaki ANGLE, yoksa sürücünün WGL üzerinden) ile çiziliyor, kareleri ekranın vuruşunda, Windows
  pencereyi boyutlandırırken ya da taşırken de çiziliyor. Fare (bir düğmesi basılıyken yakalı) ve tekerleği, dokunma
  (`WM_POINTER`; kalem fare gibi), klavye (AltGr Ctrl sayılmıyor) ve giriş yöntemi (IMM32, alanın yerinde), pano,
  dosya ve metin bırakmalar (OLE), sistemin imleçleri, ekranı açık tutma; vurgu rengi, duvar kağıdının renkleri ve UI
  Automation eskisi gibi; `mediaQuery` için mikrofon (waveIn) ve kamera (aygıt arayüzü). **Web, Kompoz'un kendi web
  platformunda çalışıyor** (`platform/web`): sayfanın tuvali; noktaları CSS pikselleri, çizim tamponu aygıtın piksel
  oranında (değiştikçe izleniyor), WebGPU ya da WebGL 2 ile çiziliyor, kareleri tarayıcının canlandırma karelerinde,
  çizecek bir şey yokken döngü duruyor. Fare (bir düğmesi basılıyken tuvalin dışında da izleniyor), tekerlek, dokunma
  ve tuşlar Emscripten'in html5 olaylarıyla; yazma, giriş yöntemleri ve telefon klavyesi (alanın türü `inputmode`
  olarak) gizli bir metin alanından; sistemin panosu kopyala ve yapıştır olaylarıyla (ve `navigator.clipboard` ile),
  SDL kendine ait bir pano tutuyordu; sayfaya bırakılan dosyalar (sanal dosya sistemine okunuyor) ve metin; CSS
  imleçleri; `keepScreenOn` ekranı uyanık tutma (Wake Lock) olarak; açık ya da koyu tercih; bağlantılar yeni sekmede,
  adresin #parçasında yol, dokunsal geri bildirim için titreşim, eskisi gibi. **macOS, Kompoz'un kendi macOS
  platformunda çalışıyor** (`platform/macos`, AppKit üzerinde Objective-C++): görünümünün katmanı Metal aygıtının
  sunduğu `CAMetalLayer` olan bir `NSWindow`; ekranın arka plan ölçeğinde, kareleri ekranın vuruşunda, pencere
  boyutlandırılırken de çiziliyor. Fare, izleme yüzeyinin kaydırması (noktalarının onda biri, tekerleğin çentikleri
  tam, SDL gibi) ve sıkıştırması, tuşlar (Command Kompoz'un Ctrl'ü) ve giriş yöntemleri (`NSTextInputClient`, alanın
  yeri söyleniyor), pano, dosya ve metin bırakmalar, sistemin imleçleri, açık ya da koyu görünüm, IOKit güç beyanı
  olarak `keepScreenOn`, uygulamanın menüsü (Gizle, Çık, pencereninkiler), `NSWorkspace` ile açılan bağlantılar.
  Hepsinde terminaldeki (Windows'ta konsoldaki) Ctrl+C ve SIGTERM, SDL'in platformunun yaptığı gibi uygulamadan
  çıkıyor, durumu çıkarken yazılıyor; böylece `scripts/dev.sh`'nin yeniden başlatmaları ekranı yine koruyor. **SDL3
  tamamen kalktı**: her yerde Kompoz'un kendi platformu olduğundan `platform/main.cpp` hangisinde çalışılacağını
  seçiyor (başsız, Wayland, X11, Windows, macOS, sayfa); SDL koddan, derlemeden, paketlerden ve CI'dan çıktı. Platform
  katmanının hedefi `kompoz::app`, seçeneği `KOMPOZ_BUILD_APP` (`kompoz::sdl3` ve `KOMPOZ_BUILD_SDL3` kalktı:
  `kompoz::app`'e bağlayın); `KOMPOZ_PLATFORM=sdl` kalktı; ne `scripts/fetch-deps.sh`, ne vcpkg port'u, ne Conan
  reçetesi, ne de Homebrew SDL3 istiyor, lisansı da artık bir uygulamanın lisansları arasında değil. Linux'ta derlemek
  için Wayland ile xkbcommon'ın ya da X11'in başlıkları gerekiyor. Dokunmatik yüzeyde iki parmağın dönmesi artık
  Wayland'de, X11'de ve macOS'ta `transformable`'ın tuttuğunu döndürüyor (hareketin açısı `ComposeHost::zoom`'a
  ulaşıyor; SDL'in sıkıştırma olayı bunu hiç taşımıyordu).
- **Bir yolun gölgesi önceden sahip olduğu maskeden çiziliyor** (Compose'un bir `dropShadow`'un ya da
  `innerShadow`'un bulanık bitmap'ini tutması gibi): GPU renderer'ı yuvarlak köşeli dikdörtgen olmayan bir şeklin
  (kalp, yıldız, kurabiye) gölgesini her seferinde baştan, her biri iki geçişli bir katman ve bir bulanıklıkla
  çiziyordu; artık onu bir kez kendi dokusuna beyaz çiziyor, sonra aynı yoldan, aynı ölçekte, aynı bulanıklık ve
  yayılmayla (iç gölgenin kaydırmasıyla da), bir pikselin aynı kesrinde (64'te birine kadar) çizildikçe o dokuyu
  gölgenin rengine boyayarak çiziyor: tam piksellerle kaydırılınca ya da başka renkte yeniden çizilmiyor. 10 s
  çizilmeyen maskeler gidiyor, bellek kısılınca hepsi. Bir Redmi Note 9 Pro'da showcase'in Bileşenler ekranının
  üstünde esneyince (üstünde böyle gölgeler var; liste esnemeye başlarken ve bitirirken yeniden çiziliyor) bir koşuda
  GPU'nun bir tazelemeden uzun sürdüğü kareler 20–21 yerine 11–12, toplamları 484–496 ms yerine 270–298 ms oldu.
  `KOMPOZ_PROFILE` yavaş karelerinde çizilen ve tutulan yol gölgelerini sayıyor.
- **Pencereye yalnızca değişen kopyalanıyor**: saklanan kare her karede takas zincirinin her görüntüsüne bütün
  olarak kopyalanıyordu; artık bir görüntü sunulduğu içeriği tutuyor (Vulkan'ın tuttuğu gibi; takas zinciri artık
  kırpılmıyor) ve birkaç kare önceki bir kareyi gösteren bir görüntüye (`Device::swapchainAge`) yalnızca o zamandan
  beri karelerin değiştirdiği kopyalanıyor (alanları ve taşımalarının gittiği yerler, üst üste binenler bir olarak),
  her parça kendi geçiş alanında (`Renderer::blit`'in bölgeleri), Android'in renderer'ının bir tamponun yalnız
  hasarını çizmesi gibi; bu yarısı ya da daha fazlasıysa ya da görüntü yeniyse hepsi. Bir Redmi Note 9 Pro'da
  showcase'in hareketli çizimlerinde (Bileşenler) dururken GPU kare başına 5,8–7,9 ms yerine 2,9–4,0 ms aldı;
  ekranın çoğunu değiştiren bir liste kaydırması önceki kadar tutuyor.
- **Vulkan'daki bir Android uygulaması arka plandan dönünce penceresini yeniden gösteriyor**: Android o zaman
  pencereye yeni bir yerel pencere veriyor; eskisi için yapılan Vulkan yüzeyine artık sunulamıyordu (her karede
  "dequeueBuffer failed: No such device"): ekran siyah kalıyordu. Yüzey ve takas zinciri yeni bir yerel pencere için
  (`Surface::vulkanNativeWindow`, SDL'in pencere işaretçisi özelliği) ve Vulkan yüzeyin kaybolduğunu söylediğinde
  yeniden yapılıyor; yerel pencere yokken hiçbirine sunulmuyor.
- **Katman dokuları birkaç boyda ve kullanılmadan 10 s tutuluyor** (Android'in renderer'ının tuttuğu gibi): bir
  katman ya da efekt için doku, Skia'nın yaklaşık boyutlarından birinde yapılıyor (1024 px'e kadar bir ikinin kuvvetine,
  ötesinde onun yarısı kadar adımlarla) ve o boyu gereken her şey onu yeniden alıyor; önceden 32 px'e yuvarlanıyor ve
  yalnızca en çok 64 px küçüğünü gerekenlere veriliyordu: ekrana kırpılan, kaydırıldıkça giren bir katman (ekran
  genişliğinde, yüksekliği her karede değişen) her karede 6–9 MB'lık bir doku yapıyordu. Kullanılmayanlar 60 kare
  yerine 10 s tutuluyor, 12 karelik RGBA'yı (Android'in renderer'ının bütçesi) aşınca en uzun süredir kullanılmayan
  önce gidiyor; uygulama arka plana geçince ya da sistemin belleği daraldığında hemen gidiyor
  (`Renderer::trimMemory`). Bir Redmi Note 9 Pro'da showcase'in Bileşenler ekranını gerçek kaydırmalarla kaydırırken
  bir koşu 74 (142 MB) yerine 39–50 doku (80–93 MB) yaptı; ekranın daha azını yeniden çizen koşularda 156–166
  (261–377 MB) yerine 109 (152 MB). Kaçırılan tazelemeler az değişti (26 yerine 22–26; 50–68 yerine 59).
  `KOMPOZ_PROFILE` yavaş karelerinde yapılan dokuları sayıyor.
- **GPU belleği en çok 4 MB'lık bloklarla alınıyor** (Skia'nın aldığı gibi): Vulkan arka ucununkiler 256 MB'a
  kadar büyüyordu; sürücü verdiği belleği temizlediği için bir blok, büyüklüğü kadar sürüyordu: 128 MB'lık biri bir
  Xiaomi 14 Ultra'da ve bir Redmi Note 9 Pro'da 9–18 ms, onu gerektiren dokunun karesinde; bloklar boşalıp yeniden
  gerektikçe de yine (14 Ultra'da bir koşuda altı kez). Bir bloğun yarısından büyük bir doku kendi belleğini alıyor.
  14 Ultra'da showcase'in Bileşenler ekranını gerçek kaydırmalarla kaydırırken yavaş kareler koşu başına 11–19'dan
  8'e, kaçırılan tazelemeler 54–64'ten 45–47'ye indi; en uzun bellek ayırma orada 12,5 ms'den 2,1'e, Redmi'de
  18 ms'den 5,6'ya indi.
- **Tembel listeler ve ızgaralar kareler arasındaki duraklamalarda önceden kompoze eder** (Compose'un prefetch'i
  gibi): kaydırılırken, kompoze ettiklerinin o yöndeki ötesindeki öğe, bir sonraki karenin vaktine kadarki sürede
  kompoze edilir (`ComposeHost::requestIdleWork` ve uygulamanın orada çağırdığı `idle()`); aynı içerik türünden bir
  öğenin daha önce aldığı süre buna sığıyorsa. Böylece onun kaydırılıp girdiği kare onu kompoze etmez. Görünümün
  çevresindekilerin bir öğe ötesi. LazyColumn, LazyRow ve bunlarla yapılan ızgaralar, bir de kademeli ızgaralar. Bir
  Redmi Note 9 Pro'da showcase'in Bileşenler ekranını gerçek kaydırmalarla kaydırırken kompozisyonu 16 ms'yi aşan
  kareler koşu başına 11–22'den 1–5'e, en uzunu 72–109 ms'den 41–61'e indi.
- **Boru hattı önbelleği yapıldığı gölgelendiriciler için tutulur**: dosyası onların adını taşır (scripts/shaders.py
  her üretilen başlığın sürümünü yazar, `Renderer::shadersVersion()` bunları birleştirir) ve bir uygulama açılırken
  arka ucun başka gölgelendiricilere ait dosyalarını siler. Bir sürücü kendisine verileni tutar, bu yüzden eski bir
  derlemenin boru hatları önbellekte kalıcı olarak duruyordu: bir Redmi Note 9 Pro'nunki 6,5 MB'a ulaşmış ve her
  açılışta okunuyordu; kullanılan gölgelendiricilerin boru hatları ise 0,2 MB tutuyordu (masaüstündeki: 12,7 MB ve
  1,2).
- **Boru hattı önbelleği tek seferde okunur**: GPU renderer'ının derlenmiş boru hatlarını tuttuğu dosya karakter
  karakter okunuyor ve bayt bayt denetleniyordu: bir Redmi Note 9 Pro'da 6,5 MB her açılışta, renderer yapılmadan önce
  70 ve 16 ms sürüyordu. Artık tek okumada okunur ve sekizer bayt denetlenir (eski biçimdeki bir dosya bir kez atılır
  ve yeniden yapılır). İlk kare pencere gösterildikten 191–212 yerine 102–127 ms sonra geldi (orada yeniden yapılan
  önbellek 0,2 MB'tı: eskisi çoğunlukla eski gölgelendiricilerin boru hatlarını tutuyordu).
- **Pencerenin ayarları, onları değiştiren her şey için bir kez okunur**: pencerenin boyutu, ölçeği, kenar boşlukları,
  teması ya da dili değişince ayarlar yeniden okunmak üzere işaretlenir ve bunu söyleyen her olayda değil, bir sonraki
  kareden ya da girdiden önce bir kez okunur. Her okuma Android'den ayarlarını, paletlerini ve görünüm
  yapılandırmasını ister (bir Redmi Note 9 Pro'da 4 ms) ve bir pencere bu olaylardan üçüyle başlar: ilk karenin
  girdisi 11–16 yerine 4 ms sürdü.
- **Bir Android uygulamasının ilk karesi yarım saniye erken gelir**: penceresini yaparken SDL onu tam ekran kipinden
  çıkarıyor ve yüzeyi ekran kadar büyükse boyutun değişmesini 500 ms'ye kadar bekliyordu; kenardan kenara (durum ve
  gezinme çubuklarının altına) yerleşen bir pencere hep o kadar büyüktür. Hiçbir şey değişmiyordu, pencereye odak
  gelmesi de bu arada aynı kilidi beklediği için her açılış o yarım saniyeyi sonuna kadar bekliyordu. Artık yalnızca
  tam ekran kipi açıkken bekler (scripts/patches/SDL-android-window-style-wait.patch). Bir Redmi Note 9 Pro'da ilk
  kare pencere gösterildikten 680–710 yerine 180–200 ms sonra geldi.
- **Bir Morph on kat hızlı yapılır**: bir Morph'un iki şeklinden de aldığı `polarRadii`, her yönü her kenarla denemek
  yerine her kenarı yalnızca ortadan bakınca kapsadığı yönlerle (ve her yandan bir fazlasıyla; ortadan geçen ya da
  ortaya değen bir kenarı hepsiyle) dener: aynı yarıçaplar, tam olarak, zamanın onda birinde (masaüstünde bir ana hat
  için 0,106 yerine 0,011 ms). Showcase'in Bileşenler ekranını oluşturmanın üçte biriydi.
- **Bir dolgunun ve bir katmanın içi GPU'ya az maliyetli**: tek renkli yuvarlak bir dikdörtgenin ya da bir katmanın
  iyice içindeki bir piksel (kenarlarından en az yarım piksel, bir eksende köşe yarıçaplarından da içeride), kırpma,
  renk süzgeci ya da perspektif yoksa rengini, ya da katmanın texelini onun alfasıyla, köşe gölgelendiricisinin
  hesapladığı tek bir değerden alır; parça gölgelendiricisinin geri kalanının okuduğu 14 değerden değil (bir pikselin
  komşularına ihtiyaç duyan şey, perspektifteki adımlar ve bir dokununki, ondan önce hesaplanır; ondan sonra her
  mipmap düzeyi bunlardan gelir: resimler, glifler ve resim boyaları kendi eşlemeleriyle). Bir Redmi Note 9 Pro'nun
  Adreno 618'i bir dolgunun çoğunu bunları okumaya harcıyordu: bir ekran dolusu dolgu artık 2,3 yerine 1,5 ms sürüyor.
  Ana sayfa ile Bileşenler sekmeleri arasında gidip gelirken GPU süresi karede ortalama 13,2–13,8 ms'den 11,5–11,7
  ms'ye, Bileşenler ekranını kaydırırken 9,5–9,7'den 8,5 ms'ye düştü.
- **Boru hatları onlara ihtiyaç duyan karelerden önce yapılır**: GPU renderer'ı her boru hattını ilk gerektiğinde
  not eder ve sonraki çalıştırmalarda, aygıt izin veriyorsa (Vulkan) kendi iş parçacığında aynı sırayla yeniden yapar.
  Bir Redmi Note 9 Pro'nun sürücüsü boru hattı önbelleğinde hiçbirini tutmuyor: her biri, onunla ilk çizen karede 8–20
  ms sürüyordu; Bileşenler ekranı kaydırılırken 45 tanesi. Ölçümde artık 137–141 yerine 107–110 tazeleme kaçtı, en uzun
  kare 95 yerine 46–54 ms.
- **Showcase'in Bileşenler ekranı bir LazyColumn**: Compose'da uzun bir listenin olduğu gibi yalnızca görünen
  bölümler (ve biraz ötesi) oluşturulur, yerleştirilir ve çizilir. Bir Redmi Note 9 Pro'da Ana sayfa ile Bileşenler
  sekmeleri arasında beş kez gidip gelmek ana iş parçacığında 4,5 yerine 1,6 s, bunun kompozisyonu ve yerleşimi 3,2
  yerine 0,6 s sürdü; Bileşenler'i yeniden açmak 150–220 yerine 19–35 ms kompozisyon ve yerleşim aldı.
- **Satır kırma her parçayı bir kez dizer**: satırlara bölme, her sözcükte satırın o ana kadarki kısmını yeniden ölçüyordu;
  her seferinde yeni bir metin parçasını diziyordu (ve yazı tipi sisteminin önbelleğini bunlarla doldurup sonra
  boşaltıyordu). Artık her parçayı bir kez dizer ve kümelerinin eklediklerini toplar (`TextMeasurer::clusterAdvances`;
  Android'in satır kırıcısının bir paragrafın ilerlemeleriyle yaptığı gibi). Çizilen ve satırların yerleri eskisi
  gibi ölçülür. Bir Redmi Note 9 Pro'da Bileşenler ekranını kaydırmak kare başına 4,6–4,8 yerine 3,8–4,1 ms
  kompozisyon ve yerleşim sürdü.
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
