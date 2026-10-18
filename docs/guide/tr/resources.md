# Kaynaklar

Uygulamanızla birlikte dağıtılan dosyalar (fontlar, resimler, vektörler), kütüphanenin kendi metinleri ve bunların sistem dilini nasıl izlediği, bir uygulamanın kullandığı platform hizmetleri: pano, dokunsal geri bildirim ve bağlantı açmak.

## Asset'ler

Kompoz'da Android tarzı bir kaynak derleyicisi yoktur. Bir uygulamanın dosyaları *asset*'lerdir: programın yanında tutulan ve `"images/cover.jpg"` gibi göreli bir yolla okunan dosyalar.

Nereye konacakları platforma bağlıdır:

| Platform | Asset'lerin yeri |
|---|---|
| Linux, Windows | Çalıştırılabilir dosyanın yanı. CMake derlemenizde oraya kopyalayın. |
| Android | APK'nın `assets/` klasörü; Gradle projenizin listelediği klasörlerden gelir. |
| Web | Sayfanın `.data` dosyası; Emscripten'in `--preload-file`'ı ile paketlenir. |

Kompoz'un kendi asset'leri, yani paketlenmiş fontlar ve lisanslar, onun `assets/` klasöründen gelir; `kompoz_copy_assets(myapp)` her derlemede bunları masaüstü ikili dosyanızın yanına kopyalar (bkz. [Kurulum](setup.md)). Uygulamanızın kendi klasörünü de aynı şekilde kopyalayın:

```cmake
add_custom_command(TARGET myapp POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:myapp>)
```

Android'de, vitrinin `app/build.gradle.kts` dosyasının yaptığı gibi iki klasörü de Gradle projesinin asset dizinlerine ekleyin; web için, vitrinin `CMakeLists.txt` dosyasının yaptığı gibi ikisini de `--preload-file` bağlama seçeneklerine ekleyin.

## Resimler

`imageResource("images/cover.jpg")` bir resim asset'ini bir kez yükler ve saklar; `painterResource` aynısını bir `Painter` olarak verir. JPEG, PNG, WebP ve BMP okunur (web'de PNG ve BMP; oradaki diğerleri için `AsyncImage`'ı kullanın).

```cpp body
Image(imageResource("images/cover.jpg"), "Cover", Modifier().size(120), ContentScale::Crop);
```

`AsyncImage`, asset'leri ve web resimlerini aynı biçimde, gösterildikleri boyutta ve arayüz thread'inin dışında yükler. Bkz. [Grafik](graphics.md#resimler).

## Vektörler

`vectorResource("vectors/logo.xml")` bir Android vector drawable asset'ini okur; böylece Android için dışa aktarılmış vektörler olduğu gibi çalışır:

```cpp body
Icon(vectorResource("vectors/heart.xml"), "Favorite");                                         // renklendirilmiş
Image(rememberVectorPainter(vectorResource("vectors/logo.xml")), "Logo", Modifier().size(64));  // kendi renkleriyle
```

Tek bir path için `ImageVector::fromSvgPath("M12,2…")` SVG path verisini doğrudan alır.

## Fontlar

`FontFamily::fromFiles` font asset'lerinden bir aile oluşturur; bkz. [Metin](text.md#fontlar). Paketlenmiş Noto Sans ve lisanslar her uygulamayla birlikte dağıtılmalıdır; bunu `kompoz_copy_assets` ile yukarıdaki Android ve web kurulumları halleder.

## Kütüphanenin kendi metinleri

Bazı bileşenler kendilerine ait metinler gösterir ya da seslendirir: bir metin alanının Kes, Kopyala ve Yapıştır'ı, bir taşma butonundaki "More options", tarih seçicinin ay adları, ekran okuyucuların okuduğu "Selected". Bunlar, Compose'un string kaynakları gibi, `LocalStrings`'in sağladığı `Strings` içindedir.

- Kompoz'da bunlar İngilizce (`Strings::english()`, varsayılan) ve Türkçe (`Strings::turkish()`) olarak bulunur.
- Host bunları sistemin tercih ettiği dile göre seçer: Türkçe bir sistemde Türkçe, aksi halde İngilizce. Bunun yerine `KOMPOZ_LANGUAGE=tr` ya da `en` belirleyici olur.
- Bir uygulama, tamamen kendi yazdığı ya da değiştirdiği metinleri `CompositionLocalProvider` ile sağlar:

```cpp
#! void App() {}
void Localized() {
    Strings mine = Strings::english();
    mine.copy = "Duplicate";
    mine.moreOptions = "More";
    CompositionLocalProvider({LocalStrings.provides(mine)}, App);
}
```

Başka bir dil için, `datePicker` adları dahil kendi `Strings`'inizi doldurun ve aynı şekilde sağlayın. `Strings::forLanguage("tr_TR.UTF-8")` herhangi bir Türkçe yerel ayar adı için Türkçe metinleri, geri kalanlar için İngilizceyi döndürür.

`DatePickerOptions::strings` ve `TimePickerOptions`'ın etiketleri, atandıklarında `LocalStrings`'e üstün gelir.

### Uygulamanızın kendi metinleri

Kompoz uygulamanızın metinlerini çevirmez. Aynı modele uyan basit bir yaklaşım, dil başına metinlerinizi içeren bir struct'tır; kendi `CompositionLocal`'ınızla sağlanır ve `LocalStrings.current().language`'a göre seçilir:

```cpp
struct AppTexts {
    std::string greeting, signIn;
    friend bool operator==(const AppTexts&, const AppTexts&) = default;
};

const AppTexts& textsFor(const std::string& language) {
    static const AppTexts en{"Welcome", "Sign in"}, tr{"Hoş geldiniz", "Giriş yap"};
    return language == "tr" ? tr : en;
}

inline const CompositionLocal<AppTexts> LocalAppTexts{AppTexts{}};

#! void App() {}
void Root() {
    CompositionLocalProvider({LocalAppTexts.provides(textsFor(LocalStrings.current().language))}, App);
}
```

## Platform hizmetleri

Composable'ların bir kompozisyon içinde edindiği ve callback'ler için sakladığı hizmetler; Compose'un `LocalUriHandler`'ı ve benzerleri gibi:

```cpp
void ShareRow(const std::string& link) {
    UriHandler links = uriHandler();
    ClipboardManager clip = clipboardManager();
    HapticFeedback haptics = hapticFeedback();
    Row([&] {
        TextButton([links, link] { links.openUri(link); }, [] { Text("Open"); });
        TextButton([clip, haptics, link] {
            clip.setText(link);
            haptics.performHapticFeedback(HapticFeedbackType::Confirm);
        }, [] { Text("Copy link"); });
    });
}
```

| Hizmet | Ne yapar |
|---|---|
| `uriHandler().openUri(uri)` | Bir bağlantı açar: uygulamanın kendi derin bağlantılarından birini uygulamada, `http(s)`'yi tarayıcıda, diğerlerini (`mailto:`, `tel:`) onları karşılayan uygulamada. |
| `clipboardManager().setText(text)`, `getText()` | Sistem panosundaki metin. `clipboard()`, Compose 1.8'in coroutine sürümüdür. |
| `hapticFeedback().performHapticFeedback(type)` | Sistemin yaptığı gibi kısa bir titreşim: Android'de Android'inki, web'de tarayıcınınki, masaüstlerinde hiçbir şey. |
| `softwareKeyboardController().hide()`, `show()` | Odaklanmış alanın yazılım klavyesini gizler ya da gösterir. |
| `focusManager().clearFocus()` | Odağı kaldırır (bkz. [Hareketler](gestures.md#odak)). |

## Ayrıca bakın

- [Grafik](graphics.md): resimler ve vektörler ayrıntılı olarak.
- [Metin](text.md): fontlar ve metin stilleri.
- [Kurulum](setup.md): asset'lerle birlikte derlemek ve paketlemek.
