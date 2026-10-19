# Kurulum

Kompoz'u derleyin, kendi CMake projenize ekleyin ve uygulamanızı Linux, Windows, Android ve web'de çalıştırın.

## Gereksinimler

- Bir C++20 derleyicisi: GCC 13 ya da daha yenisi, veya Clang 17 ya da daha yenisi (Kompoz GCC 16 ve Clang 22 ile test ediliyor). GCC 13 bazı coroutine lambda'larında dahili derleyici hatasıyla çöküyor; GCC 14'te bu yok.
- CMake 3.24 ya da daha yenisi.
- SDL3 ve FreeType.
- İsteğe bağlı: metin şekillendirme için HarfBuzz (Arapça harf bağlama, Hint yazıları, bitişik harfler, emoji dizileri). Olmadan metin karakter karakter çizilir.
- Linux'ta isteğe bağlı: `AsyncImage`'ın resim indirmek ve WebP ile JPEG çözmek için kullandığı libcurl, libwebp ve libturbojpeg (3.0 ya da sonrası). libdbus, varsa, masaüstünün vurgu rengini okumak için çalışma anında yüklenir.

Linux'ta bunları dağıtımınızın paketlerinden kurun. Android, web ve Windows için Kompoz SDL3, FreeType ve HarfBuzz'ı kaynaktan derler; `scripts/fetch-deps.sh` onları indirir (aşağıya bakın).

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
| `KOMPOZ_VENDORED_DEPS` | Android, iOS, web ve Windows'ta `ON` | SDL3, FreeType ve HarfBuzz'ı sistemdekileri kullanmak yerine `third_party/`'den derler. |

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
target_link_libraries(myapp PRIVATE kompoz_sdl3)
kompoz_copy_assets(myapp)       # the fonts and the licenses next to the binary
```

`kompoz_copy_assets`, her uygulamanın birlikte dağıttığı şeyleri, yani gömülü Noto Sans fontlarını ve lisansları, her derlemede programınızın yanına kopyalar.

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

Yukarıdaki derleme yeterlidir. Koyu mod ve masaüstünün vurgu rengi XDG masaüstü portalından gelir ve değiştikçe onu izler: `isSystemInDarkTheme()` ve `dynamicLightColorScheme()` bunları görür. `wallpaperLightColorScheme()` için duvar kağıdı masaüstünün ayarlarından okunur (KDE Plasma, GNOME, Cinnamon, MATE, Xfce).

### Windows

Windows derlemeleri Linux'tan MinGW-w64 ile çapraz derlenir (Arch'ta: `pacman -S mingw-w64-gcc`). Bağımlılıklar kaynaktan derlenip statik bağlanır; böylece program yalnızca Windows'un kendi DLL'lerine ihtiyaç duyar ve Windows 10 ve sonrasında çalışır.

```bash
scripts/fetch-deps.sh
cmake -S . -B build-windows -DCMAKE_TOOLCHAIN_FILE=cmake/mingw-w64-x86_64.cmake -DCMAKE_BUILD_TYPE=Release
cmake --build build-windows -j
```

Çalıştırılabilir dosyayı yanında `fonts/` ve `licenses/` klasörleriyle dağıtın. Testler Wine altında da çalışır: `WINEPATH=/usr/x86_64-w64-mingw32/bin wine build-windows/kompoz_tests.exe`.

### Android

Android SDK, NDK 29.0.14206865, SDK'nın CMake 4.1.2'si (Android Studio'nun SDK Manager'ından ya da `sdkmanager "ndk;29.0.14206865" "cmake;4.1.2"` ile) ve JDK 17 ya da daha yenisi gerekir (Android Studio'yla gelen JBR iş görür).

```bash
scripts/fetch-deps.sh
cd examples/showcase/android && ./gradlew installDebug
```

`fetch-deps.sh`, SDL3, FreeType ve HarfBuzz'ı sabitlenmiş sürümlerde `third_party/`'ye indirir, SHA-256 özetlerini doğrular ve `scripts/patches/` içindeki yamaları uygular. Gradle, Kompoz'un CMake projesini NDK ile `libSDL3.so`'ya ve uygulamanızın `libmain.so`'suna derler.

Kendi uygulamanızı yapmak için vitrinin `android/` klasöründen başlayın: içinde yalnızca manifest, bir tema ve SDL'in `SDLActivity`'si üzerine kurulu kısa bir `MainActivity` var. Fontlar APK'ya Kompoz'un `assets/` klasöründen girer. Android'de Kompoz; önerili yazılım klavyesini, kenardan kenara (edge-to-edge) kenar boşluklarını, geri hareketini ve tahmine dayalı geriyi, sistemin koyu modunu ve yazı boyutunu, Android 12+'da dinamik rengi ve TalkBack'i destekler.

### Web

Kompoz tarayıcı için Emscripten ile derlenir (Arch'ta: `pacman -S emscripten`). SDL3, FreeType ve HarfBuzz kaynaktan derlenir; fontlar ve resimler sayfanın yanındaki bir `.data` dosyasına paketlenir.

```bash
scripts/fetch-deps.sh
emcmake cmake -S . -B build-web -DCMAKE_BUILD_TYPE=Release -DKOMPOZ_BUILD_TESTS=OFF
cmake --build build-web -j
python3 -m http.server 8765 --directory build-web
```

Ardından `http://localhost:8765/showcase.html` adresini açın. Web'de thread yoktur; bu yüzden bir thread'e koyacağınız iş onun yerine bir coroutine'de çalışır. Gömülü fontta olmayan karakterler (emoji, CJK, Arapça) tarayıcının kendi fontlarıyla çizilir.

Kendi uygulamanızın web derlemesi için Emscripten seçeneklerini `examples/showcase/CMakeLists.txt`'den kopyalayın.

### macOS ve iOS

Kod taşınabilir, ama ikisi de henüz derlenmedi: ikisi de bir Mac gerektiriyor.

## Paketleme

```bash
scripts/package.sh
```

`package.sh`, vitrini araç zincirini bulduğu her platform için `dist/` içine paketler: bir Linux `.tar.gz`'si, bir Windows `.zip`'i, hata ayıklama anahtarıyla imzalanmış bir Android `.apk`'sı ve bir web `.zip`'i. Yalnızca bazılarını derlemek için platformları adlarıyla verin: `scripts/package.sh linux web`. Paketler `CMakeLists.txt`'deki sürümü taşır; sürümün etiketinden derlenmediklerinde ayrıca commit'i de. Kendi uygulamanızı paketlerken betiği örnek alın.

## Ayrıca bakın

- [Eğitim](tutorial.md): ilk uygulamanız, adım adım.
- [Araçlar](tools.md): kaydedince yeniden derleme, önizlemeler, ekran görüntüleri ve profil çıkarma.
