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
- **Kompoz'un kendi GPU renderer'ı**, yapım aşamasında (`KOMPOZ_GPU=vulkan`): her native grafik API'si için ince bir
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
