# Kompoz'a katkı

[English](CONTRIBUTING.md) · **Türkçe**

Yardımınız için teşekkürler. Hata bildirimleri, düzeltmeler, yeni bileşenler ve başka platformlara uyarlamalar hepsi
memnuniyetle karşılanır. Bakımcıların üzerinde çalışamadığı tek platform Apple'ınki: Mac'iniz varsa
[macOS ve iOS](#macos-ve-ios) bölümü tam size göre.

## Başlamadan önce

- Bir hata için ne yaptığınızı, ne beklediğinizi ve ne olduğunu, platformu ve mümkünse hatayı gösteren birkaç satır
  kodu yazan bir issue açın.
- Daha büyük bir değişiklik için (yeni bir bileşen, bir platform, Compose'dan ayrılan bir API) önce bir issue açın;
  biçiminde anlaşalım. Kompoz, Jetpack Compose'un API'sini ve davranışını izler: Compose'a uyan bir değişiklik,
  ondan ayrılan birinden daha kolay kabul edilir.

## Derleme ve test

README'deki [Derleme](README.tr.md#derleme) bölümüne bakın. Her değişiklikten sonra bunlar geçmeli:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/kompoz_tests                 # pencere gerektirmeyen testler (her platform)
./build/kompoz_platform_tests        # Linux ve Windows: pencere gerektirmeyen platform kodu
python3 scripts/docs.py --check      # rehberi değiştirdiyseniz: içindeki C++ örnekleri derleniyor mu
```

CI her pull request'te bütün platformları derler (Linux'ta GCC, Clang ve sanitizer'larla, Windows, macOS, Android,
web); bütün araç zincirlerinin sizde kurulu olması gerekmez.

## Değişiklik yaparken

- **Değişiklikle birlikte test.** Bir düzeltme, onsuz başarısız olan bir testle gelir; bir özellik, yaptığını
  sınayan testlerle. `kompoz::testing::ComposeTestRule` kompozisyonları pencere olmadan çalıştırır (bkz.
  [Test](docs/guide/tr/testing.md)).
- **Çevresindeki koda uyun**: adlandırmasına, yorumlarına, biçimine. Değiştirmediğiniz kodu yeniden biçimlendirmeyin.
- **Her commit'te bir değişiklik**, neyin neden değiştiğini söyleyen bir mesajla: kısa bir ilk satır, sonra ayrıntılar.
- **Belgeler iki dilde.** Rehber (`docs/guide/en`, `docs/guide/tr`), README'ler ve değişiklik günlükleri İngilizce ve
  Türkçedir; birini yazamıyorsanız pull request'te belirtin, eklenir.
- **API durum sayfasını güncel tutun** (`docs/components.md` ve `components.tr.md`): Compose'da olan bir API'yi
  eklediğinizde ya da değiştirdiğinizde.
- **Shader'lar** (`platform/gpu/shaders/*.slang`) bir kez Slang'de yazılır ve her backend için derlenmiş hâlleriyle
  commit'lenir (`platform/gpu/shaders/generated/`). Birini değiştirdikten sonra `python3 scripts/shaders.py`
  çalıştırın (Linux x86-64: derleyicilerini sabitlenmiş sürümlerle indirir) ve başlıkları onunla birlikte commit'leyin;
  CI eşleştiklerini denetler.

## macOS ve iOS

CI, Kompoz'u macOS'ta derliyor, testlerini çalıştırıyor ve orada bir uygulamayı gerçek bir pencerede (Cocoa, Metal)
çalıştırıyor; ama henüz kimse onu bir Mac'te elle kullanmadı ve iOS için derlenmedi. Gerekenler:

- Showcase'i bir Mac'te kullanıp çalışmayanları düzeltmek (girdi, metin girişi, fontlar, Retina ölçeği, koyu mod).
- Bir iOS derlemesi: `examples/showcase/android`'deki Android kurulumu gibi bir Xcode projesi ya da CMake kurulumu.
- Ekran okuyucular: semantik ağaçtan (`ComposeHost::semanticsTree`) NSAccessibility'ye (macOS) ve UIAccessibility'ye
  (iOS) bir köprü; `platform/sdl3/atspi_bridge.cpp` (Linux) ve `uia_bridge.cpp` (Windows) gibi, onlarınki gibi
  `tests/platform_tests*.cpp` testleriyle.

## Lisans

Kompoz [Apache Lisansı 2.0](LICENSE) ile lisanslanmıştır. Katkıda bulunarak katkınızın da bu lisansla
lisanslanmasını kabul etmiş olursunuz.
