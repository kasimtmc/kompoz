# Kurulum

Kompoz'u derleyin, kendi CMake projenize ekleyin ve uygulamanızı Linux, Windows, Android ve web'de çalıştırın.

## Gereksinimler

- Bir C++20 derleyicisi: GCC 13 ya da daha yenisi, veya Clang 17 ya da daha yenisi (Kompoz GCC 16 ve Clang 22 ile test ediliyor). GCC 13 bazı coroutine lambda'larında dahili derleyici hatasıyla çöküyor; GCC 14'te bu yok.
- CMake 3.24 ya da daha yenisi.
- SDL3, FreeType ve libpng.
- İsteğe bağlı: metin şekillendirme için HarfBuzz (Arapça harf bağlama, Hint yazıları, bitişik harfler, emoji dizileri). Olmadan metin karakter karakter çizilir.
- Linux'ta isteğe bağlı: `AsyncImage`'ın resim indirmek ve WebP ile JPEG çözmek için kullandığı libcurl, libwebp ve libturbojpeg (3.0 ya da sonrası). libdbus, varsa, masaüstünün vurgu rengini ve açık ya da koyu tercihini okumak için çalışma anında yüklenir.
- Linux'ta xkbcommon'ın başlıkları (`libxkbcommon-dev`; Arch'ta `libxkbcommon` ile gelir), bunlarla birlikte bir Wayland oturumunun üzerinde çalıştığı Kompoz'un kendi Wayland platformu için Wayland'inkiler (`libwayland-dev`; `wayland`), bir X11 oturumunun üzerinde çalıştığı X11 platformu için de X11'in, XInput 2'nin, Xcursor'ın ve XRandR'ın başlıkları (`libx11-dev`, `libxi-dev`, `libxcursor-dev`, `libxrandr-dev`; `libx11`, `libxi`, `libxcursor`, `libxrandr`); kitaplıkları çalışma anında yüklenir. Bunlar yoksa uygulamalar SDL'in platformunda çalışır; `KOMPOZ_PLATFORM=sdl` onu her zaman seçer.

Linux'ta bunları dağıtımınızın paketlerinden kurun. Android, web ve Windows için Kompoz FreeType, HarfBuzz, zlib ve libpng'yi, Android dışında SDL3'ü de kaynaktan derler; `scripts/fetch-deps.sh` onları indirir (aşağıya bakın).

## Kütüphaneyi ve vitrini derlemek

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/showcase
```

Bu; çekirdek kütüphaneyi (`kompoz_core`), SDL3 arka ucunu (`kompoz_sdl3`), vitrin uygulamasını ve pencere açmadan çalışan testleri (`./build/kompoz_tests`) derler.

İşe yarayan seçenekler:

| Seçenek | Varsayılan | Ne yapar |
|---|---|---|
| `KOMPOZ_BUILD_SDL3` | `ON` | SDL3 arka ucunu derler. `OFF` yalnızca çekirdeği ve testleri, pencere sistemi olmadan derler. |
| `KOMPOZ_BUILD_EXAMPLES` | `ON` | Vitrini derler. |
| `KOMPOZ_BUILD_TESTS` | `ON` (Android, iOS ve web'de `OFF`) | Pencere açmadan çalışan testleri derler. |
| `KOMPOZ_VENDORED_DEPS` | Android, iOS, web ve Windows'ta `ON` | SDL3'ü (Android'de değil), FreeType, HarfBuzz, zlib ve libpng'yi sistemdekileri kullanmak yerine `third_party/`'den derler. |

lld bulunan masaüstlerinde, siz başka bir bağlayıcı seçmediyseniz Kompoz onunla bağlar: vitrin için GNU ld'nin yirmi beş saniyesi yerine yaklaşık bir saniye.

## Kompoz'u projenizde kullanmak

Kompoz'u bir alt dizin olarak (bir klon ya da git submodule) ekleyin ve SDL3 arka ucuna bağlayın. Programın giriş noktasını arka uç getirir.

```cmake
cmake_minimum_required(VERSION 3.24)
project(myapp LANGUAGES C CXX)
set(CMAKE_CXX_STANDARD 20)

set(KOMPOZ_BUILD_EXAMPLES OFF)
set(KOMPOZ_BUILD_TESTS OFF)
add_subdirectory(kompoz)        # a clone or a git submodule

add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE kompoz::sdl3)
kompoz_copy_assets(myapp)       # the fonts and the licenses next to the binary
```

`kompoz_copy_assets`, her uygulamanın birlikte dağıttığı şeyleri, yani gömülü Noto Sans fontlarını ve lisansları, her derlemede programınızın yanına kopyalar.

Ya da Kompoz'u bir kez kurup paket olarak bulun; Linux'ta, macOS'ta ya da Windows'ta. Sistemin SDL3, FreeType,
HarfBuzz ve libpng'siyle derlendiyse onları yine orada bulur; kendi derlediğiyle derlendiyse (`scripts/fetch-deps.sh`: Windows ya
da `-DKOMPOZ_VENDORED_DEPS=ON`) onları yanına kurar:

```bash
cmake -S kompoz -B kompoz/build -DCMAKE_BUILD_TYPE=Release -DKOMPOZ_BUILD_EXAMPLES=OFF -DKOMPOZ_BUILD_TESTS=OFF
cmake --build kompoz/build -j
cmake --install kompoz/build --prefix ~/.local
```

```cmake
find_package(kompoz 0.1 REQUIRED)  # -DCMAKE_PREFIX_PATH=~/.local, orası zaten aranmıyorsa
add_executable(myapp main.cpp)
target_link_libraries(myapp PRIVATE kompoz::sdl3)
kompoz_copy_assets(myapp)
```

vcpkg ya da Conan ile, deponun bir klonundan (overlay port'u ve reçetesi o klonu, paket yöneticisinin SDL3, FreeType,
HarfBuzz ve libpng'siyle derler), ardından yukarıdaki gibi `find_package(kompoz)`:

```bash
vcpkg install kompoz --overlay-ports=kompoz/packaging/vcpkg/ports
conan create kompoz/packaging/conan --build=missing -s compiler.cppstd=20
```

Linux'ta ve macOS'ta `AsyncImage` JPEG'i libjpeg-turbo ile (2.x ya da 3.x), WebP'yi libwebp ile çözer ve libcurl ile
indirir (Windows, Android ve web kendilerininkini kullanır). Her biri bulunursa kullanılır; `-DKOMPOZ_WITH_JPEG=ON` (ya
da `WEBP`, `CURL`) onu zorunlu kılar, `OFF` dışarıda bırakır. vcpkg'nin `jpeg`, `webp`, `curl` özellikleri ve Conan'ın
`with_jpeg`, `with_webp`, `with_curl` seçenekleri (hepsi varsayılan olarak açık) aynı şeydir.

`main.cpp`'niz `main` yerine `kompoz::createApplication`'ı tanımlar:

```cpp
void Counter() {
    auto count = remember([] { return mutableStateOf(0); });

    Column(Modifier().padding(16), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
        Text("Count: " + std::to_string(count.get()), {.font = typography().titleLarge});
        Button([count] { count.set(count.get() + 1); }, [] { Text("Add"); });
    });
}

Application kompoz::createApplication(int, char**) {
    return {.title = "Demo", .content = [] { MaterialTheme(lightColorScheme(), Counter); }};
}
```

Kompoz'un tamamı tek bir başlık dosyasında, `#include <kompoz/kompoz.hpp>`, ve `kompoz` isim alanındadır.

### Application

`createApplication` komut satırını alır ve bir `Application` döndürür:

| Alan | Anlamı |
|---|---|
| `title` | Pencerenin başlığı. |
| `width`, `height` | Masaüstünde pencerenin dp cinsinden ilk boyutu. |
| `content` | Pencereyi dolduran composable. |
| `fontPath`, `boldFontPath`, `mediumFontPath` | Gömülü fontlar yerine kullanılacak fontlar. |
| `imageDiskCacheBytes` | `AsyncImage`'ın indirdiği resimlerin ne kadarının diskte tutulacağı (varsayılan 64 MB; 0 hiçbirini tutmaz). |

Masaüstünde komut satırı, uygulamanın açılışta açacağı bir derin bağlantı taşıyabilir, örneğin `./myapp kompoz://item/42` ([Gezinme](navigation.md) sayfasına bakın).

## Platformlar

### Linux

Yukarıdaki derleme yeterlidir. Koyu mod ve masaüstünün vurgu rengi XDG masaüstü portalından gelir ve değiştikçe onu izler: `isSystemInDarkTheme()` ve `dynamicLightColorScheme()` bunları görür. `wallpaperLightColorScheme()` için duvar kağıdı masaüstünün ayarlarından okunur (KDE Plasma, GNOME, Cinnamon, MATE, Xfce). Bir Wayland oturumunda pencerenin çerçevesini, çizdiği yerde (KDE Plasma, Sway) bileşikleyici çizer; çizmediği yerde (GNOME) başlık çubuğunu sistemin renkleriyle ve masaüstünün dizdiği düğmelerle Kompoz kendisi çizer, pencerenin çevresine de onu boyutlandıran gölgeler koyar.

### Windows

Windows derlemeleri Linux'tan MinGW-w64 ile çapraz derlenir (Arch'ta: `pacman -S mingw-w64-gcc`). Bağımlılıklar kaynaktan derlenip statik bağlanır; böylece program yalnızca Windows'un kendi DLL'lerine ihtiyaç duyar ve Windows 10 ve sonrasında Kompoz'un kendi Win32 platformunda çalışır: her monitörün DPI'ını bilir, başlık çubuğu Windows'un koyu moduyla koyulaşır.

```bash
scripts/fetch-deps.sh
cmake -S . -B build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows -j
```

Çalıştırılabilir dosyayı yanında `fonts/` ve `licenses/` klasörleriyle dağıtın. Testler Wine altında da çalışır: `WINEPATH=/usr/x86_64-w64-mingw32/bin wine build-windows/kompoz_tests.exe`.

Kompoz'un OpenGL ES üzerindeki GPU renderer'ı (`KOMPOZ_GPU=gles`), uygulamanın yanında ANGLE'ın `libEGL.dll` ve `libGLESv2.dll` dosyaları varsa ANGLE'ı kullanır: Direct3D 11 üzerinde OpenGL ES, her Windows GPU'sunda aynı. vcpkg onları Google'ın kaynağından derler (`vcpkg install angle:x64-windows-release`: `bin/` klasöründeki DLL'leri dağıtın, kullandıkları `zlib1.dll` de onların arasındadır); onlar yoksa GPU sürücüsünün kendi OpenGL ES'i kullanılır. Vulkan sürücüsü olmayan bir Windows bilgisayarında (bir sanal makine, Uzak Masaüstü, bir CI makinesi) bunların hiçbiri yoktur: uygulamayla ANGLE'ı da dağıtın; orada Direct3D 11'in WARP'ıyla işlemcide çizer. ANGLE, vcpkg'ninki ve `third_party/`'deki gibi EGL ile derlenmiş bir SDL ister; Conan Center'ın SDL'inde Windows'ta OpenGL ES yoktur, bu yüzden Conan ile derlenen bir uygulama orada yalnızca Vulkan ile çizer.

### Android

Android SDK, NDK 29.0.14206865, SDK'nın CMake 4.1.2'si (Android Studio'nun SDK Manager'ından ya da `sdkmanager "ndk;29.0.14206865" "cmake;4.1.2"` ile) ve JDK 17 ya da daha yenisi gerekir (Android Studio'yla gelen JBR iş görür).

```bash
scripts/fetch-deps.sh
cd examples/showcase/android && ./gradlew installDebug
```

`fetch-deps.sh`, SDL3, FreeType, HarfBuzz, zlib ve libpng'yi sabitlenmiş sürümlerde `third_party/`'ye indirir ve SHA-256 özetlerini doğrular (Android'de SDL kullanılmaz). Gradle, Kompoz'un CMake projesini NDK ile uygulamanızın `libmain.so`'suna derler.

Kendi uygulamanızı yapmak için vitrinin `android/` klasöründen başlayın: içinde yalnızca manifest, bir tema ve Kompoz'un `KompozActivity`'sini genişleten bir `MainActivity` var (`KompozActivity` `libmain.so`'yu yükler, uygulamanızı kendi UI iş parçacığında çalıştırır ve her kareyi ekranın ritminde çizer). Fontlar APK'ya Kompoz'un `assets/` klasöründen girer. Android'de Kompoz; önerili yazılım klavyesini, kenardan kenara (edge-to-edge) kenar boşluklarını, geri hareketini ve tahmine dayalı geriyi, sistemin koyu modunu ve yazı boyutunu, Android 12+'da dinamik rengi ve TalkBack'i destekler.

### Web

Kompoz tarayıcı için Emscripten ile derlenir (Arch'ta: `pacman -S emscripten`). SDL3, FreeType, HarfBuzz, zlib ve libpng kaynaktan derlenir; fontlar ve resimler sayfanın yanındaki bir `.data` dosyasına paketlenir. Uygulama Kompoz'un kendi web platformunda çalışır: yazma ve giriş yöntemleri (ve telefonun klavyesi) gizli bir metin alanından geçer, pano sistemin panosudur (Ctrl+C ile kopyalanır, Ctrl+V ile yapıştırılır), sayfaya dosya ve metin bırakılabilir, `keepScreenOn` ekranı uyanık tutar (Wake Lock).

```bash
scripts/fetch-deps.sh
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DKOMPOZ_BUILD_TESTS=OFF
cmake --build build-web -j
python3 -m http.server 8765 --directory build-web
```

Ardından `http://localhost:8765/showcase.html` adresini açın. Web'de thread yoktur; bu yüzden bir thread'e koyacağınız iş onun yerine bir coroutine'de çalışır. Gömülü fontta olmayan karakterler (emoji, CJK, Arapça) tarayıcının kendi fontlarıyla çizilir. Sayfanın sorgusu uygulamanın ortamıdır (`showcase.html?KOMPOZ_GPU=gles`, bkz. [Araçlar](tools.md#ortam-değişkenleri)): Kompoz'un GPU renderer'ı orada WebGL 2 (`gles`) ya da WebGPU (`webgpu`) üzerinde çizer. Sayfa ASYNCIFY ile bağlanır; bir okuma (içine çizilen bir bitmap, bir ekran görüntüsü) WebGPU'yu onunla bekler.

Kendi uygulamanızın web derlemesi için Emscripten seçeneklerini `examples/showcase/CMakeLists.txt`'den kopyalayın.

### macOS ve iOS

macOS'ta Kompoz, Linux'taki gibi Homebrew'un SDL3, FreeType, HarfBuzz ve libpng'siyle derlenir (`brew install sdl3 freetype harfbuzz libpng`) ve çalışır: CI onu orada derliyor, testleri ve bir uygulamayı gerçek bir pencerede, Kompoz'un kendi macOS platformunda (AppKit, Metal) çalıştırıyor. Henüz bir Mac'te elle kullanılmadı, oradaki ekran okuyuculara da (NSAccessibility) bağlı değil. iOS derlenmedi: Android'inki gibi bir Xcode projesi ya da CMake kurulumu gerekiyor.

## Paketleme

```bash
scripts/package.sh
```

`package.sh`, vitrini araç zincirini bulduğu her platform için `dist/` içine paketler: bir Linux `.tar.gz`'si, bir Windows `.zip`'i, hata ayıklama anahtarıyla imzalanmış bir Android `.apk`'sı ve bir web `.zip`'i. Yalnızca bazılarını derlemek için platformları adlarıyla verin: `scripts/package.sh linux web`. Paketler `CMakeLists.txt`'deki sürümü taşır; sürümün etiketinden derlenmediklerinde ayrıca commit'i de. Kendi uygulamanızı paketlerken betiği örnek alın.

## Ayrıca bakın

- [Eğitim](tutorial.md): ilk uygulamanız, adım adım.
- [Araçlar](tools.md): kaydedince yeniden derleme, önizlemeler, ekran görüntüleri ve profil çıkarma.
