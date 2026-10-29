# Araçlar

Önizlemeler, kaydedince yeniden derleme, betikle ekran görüntüleri, profil çıkarma ve çalışırken işe yarayan ortam değişkenleri.

## Önizlemeler

Compose'un `@Preview`'ı gibi, bir önizleme tek bir composable'ı uygulamanın geri kalanı olmadan, seçtiğiniz boyutta bir çerçevede tek başına gösterir. Onu composable'ın yanına, uygulamanızın kaynaklarına yazın:

```cpp
#! void CounterCard() {}
KOMPOZ_PREVIEW(CounterCardPreview, .width = 400, .height = 220) {
    MaterialTheme(lightColorScheme(), [] { CounterCard(); });
}

KOMPOZ_PREVIEW(CounterCardDarkPreview, .width = 400, .height = 220, .darkTheme = true) {
    MaterialTheme(darkColorScheme(), [] { CounterCard(); });
}
```

Uygulamayı `KOMPOZ_PREVIEW`'ı bir önizlemenin adına ayarlayarak başlatın; penceresi yalnızca o önizlemeyi, o boyutta gösterir:

```bash
KOMPOZ_PREVIEW=CounterCardPreview ./build/myapp
```

`KOMPOZ_PREVIEW='*'` ya da hiçbir önizlemenin taşımadığı bir ad, hepsini adı ve boyutuyla birlikte alt alta gösterir.

Addan sonraki seçenekler bir `PreviewSpec`'tir:

| Seçenek | Anlamı |
|---|---|
| `.width`, `.height` | Çerçevenin dp cinsinden boyutu (varsayılan 360 × 640). |
| `.darkTheme` | Önizleme içinde `isSystemInDarkTheme()`'in döndürdüğü değer; verilmezse sistemin. |
| `.background` | Önizlemenin arkasındaki renk; verilmezse yok. |

Yazı ölçeği önizlemenin kendisinin değil, pencerenindir (`KOMPOZ_FONT_SCALE`). Bir önizleme program başlarken kendini kaydeder; bu yüzden uygulamaya bağlanan bir kaynak dosyada olmalıdır.

## Kaydedince yeniden derleme ve başlatma

```bash
scripts/dev.sh
```

`dev.sh` vitrini derler, başlatır ve bir kaynak dosya her değiştiğinde yeniden derleyip yeniden başlatır. Uygulama kaldığı yere döner: bulunduğu ekran, kaydırma konumları ve `rememberSaveable` değerleri, Android'in süreci sonlanan bir uygulamayı geri getirdiği gibi, `KOMPOZ_STATE_FILE` aracılığıyla kapanırken yazılır ve açılırken okunur. Başarısız bir derleme çalışan uygulamaya dokunmaz. Pencereyi kapatmak ya da Ctrl+C'ye basmak onu sonlandırır.

Argümanları; hedef, derleme dizini ve uygulamanın neyle başlatılacağıdır:

```bash
scripts/dev.sh showcase build kompoz://components
```

Bir önizlemeyle birlikte, her kaydedişten birkaç saniye sonra geri gelen, tek başına bir bileşen elde edersiniz:

```bash
KOMPOZ_PREVIEW=CounterCardPreview scripts/dev.sh
```

`dev.sh` Kompoz'un kendi klasörlerini (`include`, `src`, `platform`, `examples`) izler. Kendi uygulamanız için onu kopyalayın; izlediği klasörleri ve derlediği hedefi değiştirin.

## Betikli çalıştırmalar ve ekran görüntüleri

`KOMPOZ_SCRIPT` uygulamayı bir kullanıcı olmadan sürer: tıklamalar, yazma, kaydırmalar ve ekran görüntüleri, koordinatlar dp cinsinden. Betik çalışırken zaman sanaldır, böylece animasyonlar her çalıştırmada aynıdır; betik bitince gerçek zaman devralır ve uygulamayı kaldığı yerden kullanmaya devam edebilirsiniz. `realtime`'dan sonra betiğin geri kalanı gerçek zamanda çalışır, kareleri bir insanın göreceği gibi ekranın hızına göre bekletilir: `KOMPOZ_PROFILE` ile ölçmek için.

```bash
SDL_VIDEODRIVER=offscreen KOMPOZ_SCRIPT="click 160 263; wait 30; shot out.png; quit" ./build/showcase
```

`SDL_VIDEODRIVER=offscreen` pencere olmadan çalıştırır; bu, bir derleme sunucusunda işe yarar: GPU renderer'ı yalnızca sakladığı kareye çizer (GPU'su olmayan bir bilgisayarın yazılımla çalışan bir GPU'su vardır: Linux'ta lavapipe ya da llvmpipe, Windows'ta WARP). Bir önizleme de aynı yolla resme dönüşür:

```bash
SDL_VIDEODRIVER=offscreen KOMPOZ_PREVIEW=CounterCardPreview KOMPOZ_SCRIPT="wait 10; shot preview.png; quit" ./build/myapp
```

`;` ile ayrılan komutlar:

| Komut | Ne yapar |
|---|---|
| `click x y`, `rclick x y` | Bir dokunma ya da sağ tık. |
| `move x y` | Fareyi hareket ettirir (üzerine gelme). |
| `down x y`, `up x y` | Basılı tutmak için basar ve bırakır. |
| `drag x1 y1 x2 y2`, `mdrag x1 y1 x2 y2` | Parmakla ya da fareyle bir sürükleme. |
| `swipe x1 y1 x2 y2 frames` | Fling yaptıracak kadar hızlı, o kadar karede bir sürükleme. |
| `fdown id x y`, `fmove id x y`, `fup id x y` | Çoklu dokunma hareketinin parmakları. |
| `wheel x y dy`, `zoom x y factor` | Fare tekerleği; Ctrl+tekerlek ya da dokunmatik yüzeyde iki parmakla yakınlaştırma. |
| `type text` | Odaktaki alana metin yazar. |
| `key name` | `enter`, `backspace`, `escape`, `tab`, `shift+tab`, `space`, `up`, `down`, `left`, `right`. |
| `resize w h` | Pencereyi yeniden boyutlandırır. |
| `wait frames` | Karelerin geçmesine izin verir (sanal zamanda). |
| `realtime` | Geri kalanı gerçek zamanda, kareler ekranın hızında. |
| `sleep ms` | Başka thread'lerdeki işler için gerçek zamanın geçmesine izin verir: yüklenen bir resim, okunan duvar kağıdı renkleri. |
| `shot file.png` | Bir ekran görüntüsü kaydeder. |
| `quit` | Uygulamayı sonlandırır. |

Arayüzü hiç pencere açmadan kontrol eden testler için [Test](testing.md) sayfasına bakın.

## Recomposition'ı görmek

`Modifier().recomposeHighlighter()`, Compose snippet'lerindeki highlighter gibi, bir düğümün çevresine onu oluşturan composable'ın kaç kez çalıştığını gösteren bir çerçeve çizer: olması gerekenden sık yeniden kompoze olan şeyi bulmak için kullanın.

```cpp
void Row1(const std::string& title) {
    Text(title, Modifier().recomposeHighlighter());
}
```

`KOMPOZ_TRACE=1`, her recomposition için kaç scope'un çalıştığını ve kaçının atlandığını günlüğe yazar.

## Karelerin profilini çıkarmak

`KOMPOZ_PROFILE=1` kare sürelerini günlüğe yazar. İşi ekranın yenileme süresinden uzun süren ya da bir öncekinden iki yenileme süresinden fazla sonra gelen kare yavaştır; her yavaş kare için girdiye, kompozisyon ve yerleşime, GPU'nun daha önceki bir kareyi bitirmesini ve pencerenin içine çizilecek bir görüntüyü geri vermesini beklemeye (`wait`), çizime ve sunuma harcanan süreyi, çizdiği düğümlerle olduğu gibi tuttuklarını (içlerindekilerle, bkz. [Aşamalar](phases.md#yalnız-değişeni-çizmek)), çizdiği ekran dışı katmanlarla (ve bunlardan kaçının, içerikleri öncekiyle aynı olduğundan sakladıkları piksellerden birleştirildiğini), çizdiği yol gölgelerini (ve bunlardan kaçının önceden sahip oldukları maskeden çizildiğini), katmanlar ve efektler için yaptığı dokuları (kullanılmadan tuttuğu biri uyduysa hiç) ve rasterleştirdiği glifleri yazar. Her saniye bir özet gelir: kareler, kaçının gösterildiği ve arka arkaya gösterilen iki kare arasında ekranın kaç yenilemesinin karesiz geçtiği, işin ortalaması ve parçaları (kompozisyon ve yerleşim, çizim ve çizimin içinde kayıt: ağacın görüntü listesine çizilip önceki kareyle karşılaştırılması), beklemenin ortalaması, Kompoz'un GPU renderer'ında da bir karenin GPU'daki kendi süresi. GPU'da bir yenileme süresinden uzun süren kare, en pahalı geçişleriyle yazılır; her biri çizdiği yerle (`window`, `1080x2400`, `2048x2048 R16F`) ya da `copy WxH` olarak adlandırılır:

```
kompoz: GPU frame 12.2 ms: 1080x2400 10.5, window 1.6, 2048x2402 R16F 0.1
```

GPU süresi Vulkan'ın ve WebGPU'nun zaman damgalarıyla (WebGPU'da adaptörde `timestamp-query` varsa) ve OpenGL ES'in zamanlayıcı sorgularıyla (`EXT_disjoint_timer_query`, WebGL 2'de `_webgl2` olanı) ölçülür; Metal'de yalnızca karenin tamamınınki, komut tamponlarından. Hiçbirinin olmadığı yerde profil bunu söyler.

Android'de ortam değişkeni yoktur; orada profil aracı uygulamanın depolamasında bir dosya arar ve çıktısı `adb logcat -s SDL/APP`'a gider:

```bash
adb shell run-as dev.kompoz.showcase touch files/kompoz-profile
```

`run-as`'in reddedildiği yerde (SELinux: örneğin bir LineageOS derlemesi) bu dosyalar (`kompoz-profile`, `kompoz-damage-check`, `kompoz-keep-check`, `kompoz-full-redraw`, `kompoz-script`, `kompoz-gpu`) uygulamanın harici dosyalarına da konabilir: `adb shell touch /sdcard/Android/data/dev.kompoz.showcase/files/kompoz-profile`.

Bir betik de oradaki bir dosyadan gelir (aynı komutlar; bir kare ölçümü her cihazda aynı tekrarlanır):

```bash
adb shell run-as dev.kompoz.showcase sh -c "'echo \"realtime; wait 60; swipe 200 600 200 200 12; wait 120\" > files/kompoz-script'"
```

Kompoz her kareyi önce kaydeder ve yalnızca bir önceki kareden beri değişeni yeniden çizer; ekranda hiçbir şeyin değişmediği bir kare ne çizilir ne sunulur. Bunu görmek ya da kontrol etmek için:

- `KOMPOZ_DAMAGE_SHOW=1` her karede yeniden çizilenin (her bir alanının) çevresini çizer.
- `KOMPOZ_DAMAGE_CHECK=1` her kareyi ayrıca sıfırdan çizer ve farklı çıkan her pikseli günlüğe yazar (Android'de, yukarıdaki `kompoz-profile` gibi uygulamanın dosyalarında `kompoz-damage-check` adlı bir dosya).
- `KOMPOZ_FULL_REDRAW=1` her kareyi sıfırdan çizer (Android'de `kompoz-full-redraw` adlı bir dosya): `KOMPOZ_PROFILE` ile birlikte, ekranın tamamını çizmenin GPU'ya maliyetini gösterir.
- `KOMPOZ_KEEP_CHECK=1` her kareyi ayrıca her düğümü çizerek kaydeder; karenin değişmeyen düğümlerden tuttuğu şeyin bundan farklı çıktığı yeri, çıkışta da böyle karelerin sayısını günlüğe yazar (Android'de `kompoz-keep-check` adlı bir dosya).

## Ortam değişkenleri

Web'de onları sayfanın sorgusu belirler: `showcase.html?KOMPOZ_GPU=gles&KOMPOZ_PROFILE=1`.

| Değişken | Ne yapar |
|---|---|
| `KOMPOZ_PREVIEW=name` | Tek bir önizlemeyi, `*` ile de hepsini gösterir. |
| `KOMPOZ_SCRIPT="…"` | Uygulamayı yukarıdaki komutlarla sürer. |
| `KOMPOZ_STATE_FILE=file` | Kaydedilen durumu (`rememberSaveable`, gezinmenin geri yığını) çıkışta yazar, açılışta okur. |
| `KOMPOZ_FONT_SCALE=1.3` | Masaüstünde sistemin yazı ölçeğinin yerini tutar. |
| `KOMPOZ_SCALE=2.625` | Bir telefonun yerleşimini masaüstünde görmek için ekran yoğunluğunun yerini tutar. |
| `KOMPOZ_LANGUAGE=tr` | Sistemin dili yerine kütüphanenin kendi metinlerinin dili (`en` ya da `tr`). |
| `KOMPOZ_LAYOUT_DIRECTION=rtl` | Sistem dilininki yerine sağdan sola ya da soldan sağa yerleşim. |
| `KOMPOZ_NO_DESKTOP_ACCENT=1` | Ekran görüntüleri her makinede aynı olsun diye masaüstünün vurgu rengini (Linux, Windows) yok sayar. |
| `KOMPOZ_NO_WALLPAPER_COLORS=1` | Aynı nedenle duvar kağıdının renklerini okumaz. |
| `KOMPOZ_ATSPI=1` | Masaüstünün erişilebilirliği kapalıyken de semantik ağacı AT-SPI üzerinden ekran okuyuculara sunar (Linux); `0` hiç sunmaz. |
| `KOMPOZ_TRACE=1` | Her recomposition'da kaç scope'un çalışıp atlandığını günlüğe yazar. |
| `KOMPOZ_PROFILE=1` | Kare sürelerini günlüğe yazar. |
| `KOMPOZ_GPU=vulkan`, `KOMPOZ_GPU=metal`, `KOMPOZ_GPU=webgpu`, `KOMPOZ_GPU=gles` | GPU renderer'ının arka ucu: Vulkan (Linux, Windows, Android), Metal (macOS, iOS), WebGPU (web) ya da OpenGL ES 3 üzerinde (sistemde olduğu yerde: Linux, Android; web'de WebGL 2; Windows'ta ANGLE'ınki, bkz. [Kurulum](setup.md#windows)). Verilmezse (ya da `KOMPOZ_GPU=auto` ile) platformun en iyisi, olmazsa sıradaki: Apple'ın sistemlerinde Metal; web'de WebGPU, sonra WebGL 2; diğer yerlerde Vulkan (Android'de Android 10'dan itibaren), sonra OpenGL ES 3. Tek başına adı verilen ve başlamayan bir arka uç uygulamayı bitirir, hiçbirinin başlamaması da. Derlediği shader'lar uygulamanın önbellek dizininde tutulur (`AsyncImage`'ın tuttuğu resimlerin yanında `pipelines-vulkan-<sürüm>`, `pipelines-gles-<sürüm>`; sürüm shader'ların kendi sürümüdür, başka sürümlerin dosyaları silinir), böylece onları yalnızca shader'lar değiştikten sonraki ilk çalışma derler. Bir uygulamanın ortam değişkeni olmayan Android'de, uygulamanın dosyalarındaki `kompoz-gpu` adlı dosyanın ilk sözcüğü: `adb shell run-as <paket> sh -c 'echo gles > files/kompoz-gpu'`. |
| `KOMPOZ_GPU_DEVICE=name`, `KOMPOZ_GPU_VALIDATION=1` | Onunla birlikte: adında `name` geçen GPU (Vulkan, macOS'ta Metal); kuruluysa Vulkan'ın validation katmanları, Metal'in API doğrulaması, OpenGL ES'in hata ayıklama çıktısı (debug context): ikisinin bildirdiği her hata sayılır (`kompoz_gpu_tests` birinde başarısız olur). |
| `KOMPOZ_DAMAGE_SHOW=1`, `KOMPOZ_DAMAGE_CHECK=1`, `KOMPOZ_FULL_REDRAW=1` | Yalnızca değişeni yeniden çizmeyi gösterir, kontrol eder ya da kapatır. |
| `KOMPOZ_LAYOUT_CHECK=1` | Her artımlı yerleşimi tam bir yerleşimle karşılaştırır (testler bunu hep yapar). |
| `KOMPOZ_KEEP_CHECK=1` | Her karenin değişmeyen düğümlerden tuttuğunu her düğümü çizmekle karşılaştırır (testler bunu hep yapar). |
| `KOMPOZ_IMAGE_CACHE=dir` | `AsyncImage`'ın indirdiği resimleri tuttuğu yer. |
| `KOMPOZ_IMAGE_LOG=1` | Her resmin nereden geldiğini ve ne kadar büyük olduğunu günlüğe yazar. |

## Paketleme

`scripts/package.sh` vitrini araç zincirini bulduğu her platform için paketler; [Kurulum](setup.md#paketleme) sayfasına bakın.

## Ayrıca bakın

- [Test](testing.md): pencere olmadan çalışan arayüz testleri.
- [Aşamalar](phases.md): kompozisyonun, yerleşimin ve çizimin maliyeti ve her birini nasıl ucuz tutacağınız.
