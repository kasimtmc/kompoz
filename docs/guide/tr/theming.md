# Tema

`MaterialTheme`, içindeki her şeye Material 3'ün renk şemasını, tipografisini ve hareket şemasını verir. Bu sayfa renk şemalarını, koyu temayı, duvar kâğıdından ya da masaüstünün vurgu renginden gelen dinamik rengi, tipografiyi ve kendi tasarım sisteminizi kurmayı anlatıyor.

## MaterialTheme

Uygulamanızın içeriğini `MaterialTheme` ile sarın, genellikle bir kez, en üstte:

```cpp
#! void App() {}
Application kompoz::createApplication(int, char**) {
    return {.title = "Themed", .content = [] {
                bool dark = isSystemInDarkTheme();
                MaterialTheme(dark ? darkColorScheme() : lightColorScheme(), App);
            }};
}
```

Bileşenler temayı composition local'ler üzerinden okur. Siz de `colorScheme()`, `typography()` ve `motionScheme()` ile okuyabilirsiniz:

```cpp
void Tag(const std::string& text) {
    Text(text, Modifier().background(colorScheme().secondaryContainer, 8).padding(8, 4),
         {.font = typography().labelMedium, .color = colorScheme().onSecondaryContainer});
}
```

`MaterialExpressiveTheme` de aynısıdır, yalnız M3 Expressive'in daha yaylı hareket şemasıyla.

## Renk şemaları

Bir `ColorScheme`, Compose 1.4'teki gibi Material 3'ün bütün renk rollerini taşır: `primary`, `onPrimary`, `primaryContainer`, `secondary`, `tertiary`, `error`, yüzeyler (`surface`, `surfaceContainerLowest` … `surfaceContainerHighest`, `surfaceBright`, `surfaceDim`), `outline`, `inverseSurface`, sabit renkler ve daha fazlası.

- `lightColorScheme()` ve `darkColorScheme()`, Material'ın temel mor şemalarıdır.
- Her `on…` rengi, eşleştiği kabın üzerindeki içeriğin rengidir: `primary` üzerinde `onPrimary`, `surface` üzerinde `onSurface`. `contentColorFor(background)` bunu sizin yerinize bulur.
- Material kapları (`Surface`, `Card`, butonlar) `LocalContentColor`'ı kendi içerik renklerine ayarlar; böylece içlerindeki `Text` ve `Icon` söylenmeden doğru rengi alır.

Kendi şemanız, alanları ayarlanmış bir `ColorScheme`'dir. Temel bir şemadan başlayıp gerekeni değiştirin:

```cpp
ColorScheme brandColors(bool dark) {
    ColorScheme cs = dark ? darkColorScheme() : lightColorScheme();
    cs.primary = dark ? Color::hex(0x9ECAFF) : Color::hex(0x0061A4);
    cs.onPrimary = dark ? Color::hex(0x003258) : colors::White;
    return cs;
}
```

### Tek bir renkten

`TonalPalette::fromSeed(color)`, Material You'nun tek bir renkten türettiği beş ton paletini, Android 13'ün duvar kâğıdından yaptığı gibi ("tonal spot" stiliyle), Material'ın HCT renk uzayında üretir. `dynamicLightColorScheme(palette)` ve `dynamicDarkColorScheme(palette)` bunları Material'ın ton eşlemesiyle şemalara dönüştürür:

```cpp
void BrandTheme(const Content& content) {
    static const TonalPalette palette = TonalPalette::fromSeed(Color::hex(0x00796B));
    bool dark = isSystemInDarkTheme();
    MaterialTheme(dark ? dynamicDarkColorScheme(palette) : dynamicLightColorScheme(palette), content);
}
```

Bir marka renginden eksiksiz ve tutarlı bir şemaya ulaşmanın en kolay yolu budur. `Hct::fromColor(c)` ve `Hct{hue, chroma, tone}.toColor()` doğrudan HCT renk uzayında çalışır.

## Koyu tema

`isSystemInDarkTheme()` sistemin koyu modunu izler ve uygulama çalışırken değişir: Android'de sistem ayarından, Linux'ta XDG masaüstü portalından, web'de tarayıcıdan. Şemayı yukarıdaki gibi buna göre seçin.

Android'de sistem çubuklarının açık mı koyu mu simge çizeceğini `SystemBarsAppearance` ile söyleyin: açık temanın üzerinde koyu simgeler.

```cpp
#! void App() {}
void Root() {
    bool dark = isSystemInDarkTheme();
    SystemBarsAppearance(!dark);
    MaterialTheme(dark ? darkColorScheme() : lightColorScheme(), App);
}
```

Seçimi kullanıcıya bırakmak için seçimi durumda tutun ve sistemin tercihine geri dönün:

```cpp
#! void App() {}
enum class ThemeChoice { System, Light, Dark };

void Root(ThemeChoice choice) {
    bool dark = choice == ThemeChoice::System ? isSystemInDarkTheme() : choice == ThemeChoice::Dark;
    MaterialTheme(dark ? darkColorScheme() : lightColorScheme(), App);
}
```

## Dinamik renk

![Example-1](../../screenshots/dynamic-color-linux/d9.png)
![Example-2](../../screenshots/dynamic-color-linux/d16_light.png)

Bir temanın renkleri üç yerden gelebilir ve Kompoz her birini ayrı bir API olarak sunar. Hangisinin kullanılacağı uygulamanın ya da kullanıcısının kararıdır: Kompoz kendi kendine birinden ötekine geçmez.

| Kaynak | Şemalar | Nerede kullanılabilir |
|---|---|---|
| Uygulamanın kendi renkleri | `lightColorScheme()`, `darkColorScheme()` ya da kendi şemanız (bir marka renginin `TonalPalette::fromSeed`'i) | Her yerde. |
| Sistemin paleti | `dynamicLightColorScheme()`, `dynamicDarkColorScheme()`; `isDynamicColorAvailable()` | Android 12+ (sistemin duvar kâğıdından ya da kullanıcının seçtiği renklerden yaptığı palet); vurgu rengini XDG portalı üzerinden veren Linux masaüstleri (KDE'de renk şemasının vurgu rengi); Windows 10 ve 11 (Ayarlar > Kişiselleştirme > Renkler'deki vurgu rengi). |
| Duvar kâğıdının kendisi | `wallpaperLightColorScheme()`, `wallpaperDarkColorScheme()`; `isWallpaperColorAvailable()` | Linux (KDE Plasma; GNOME, Cinnamon ve MATE; Xfce), Windows, Android 8.1+. |

Sistemin paleti, kullanıcının sistem ayarlarında seçtiğini izler; bu yüzden onu kullanan bir uygulama masaüstünün geri kalanıyla uyumlu görünür. Duvar kâğıdının renkleri, Android'in seçtiği gibi doğrudan resmin kendisinden seçilir (`TonalPalette::fromImage`); kendi renkleri duvar kâğıdını izlemeyen masaüstlerinde de. Uygulamaların renklerin yoğunluklarını göremediği Android'de tohum, sistemin ana duvar kâğıdı renklerinden gri olmayan ilkidir; System UI de bu durumda onu böyle alır. İkisi de uygulama çalışırken duvar kâğıdı, vurgu rengi ya da tema değiştiğinde güncellenir. Bir kaynakta bir şey yoksa şemaları temel şemalardır; böylece uygulama her zaman kendi renklerine geri dönebilir.

Seçimi kullanıcıya bırakın ve saklayın:

```cpp
#! void App() {}
enum class ColorSource { App, System, Wallpaper };

void Root(ColorSource source) {
    bool dark = isSystemInDarkTheme();
    ColorScheme scheme = source == ColorSource::System && isDynamicColorAvailable()
                             ? (dark ? dynamicDarkColorScheme() : dynamicLightColorScheme())
                         : source == ColorSource::Wallpaper && isWallpaperColorAvailable()
                             ? (dark ? wallpaperDarkColorScheme() : wallpaperLightColorScheme())
                             : (dark ? darkColorScheme() : lightColorScheme());
    MaterialTheme(scheme, App);
}
```

Vitrin uygulaması bunu View menüsünde ve Material ekranında böyle soruyor.

### Herhangi bir resimden renkler

`TonalPalette::fromImage(bitmap)`, Material You'nun paletlerini duvar kâğıdınınkiler gibi herhangi bir resimden üretir: bir albüm kapağı, bir profil fotoğrafı. Android'in bir duvar kâğıdıyla yaptığını yapar: resim, `WallpaperColors`'ın küçülttüğü gibi filtrelemeden en fazla 112 × 112'lik alana küçültülür; `quantizeColors(bitmap)` resmin hangi renklerden ve her birinden ne kadar oluştuğunu bulur (Android'in ve Material Color Utilities'in kullandığı Celebi niceleyicisi); `seedColors(colors)` bunlardan iyi tema tohumu olacakları, Android'in System UI'ının bir duvar kâğıdınınkileri sıraladığı gibi, en fazla dört tane olmak üzere sıralar. Böylece kullanıcıya, Android'in duvar kâğıdı seçicisi gibi bir tohum seçeneği sunabilirsiniz:

```cpp
void CoverTheme(const ImageBitmap& cover, const Content& content) {
    TonalPalette palette = remember(cover, [&] { return TonalPalette::fromImage(cover); });   // resim başına bir kez
    MaterialTheme(isSystemInDarkTheme() ? dynamicDarkColorScheme(palette) : dynamicLightColorScheme(palette), content);
}
```

`KOMPOZ_NO_DESKTOP_ACCENT=1` ve `KOMPOZ_NO_WALLPAPER_COLORS=1`, her makinede aynı çıkan ekran görüntüleri için masaüstünün vurgu rengini ve duvar kâğıdının renklerini devre dışı bırakır.

## Tema değişikliğini canlandırmak

`ColorScheme::map` her renge bir fonksiyon uygular. `animateColorAsState` ile birlikte, şema değiştiğinde her renk yeni değerine doğru canlanır:

```cpp
ColorScheme animateColorScheme(const ColorScheme& target, CallSite site = CallSite::current()) {
    ComposableScope scope(site);   // içerideki animasyonlar için kararlı bir kimlik
    return target.map([](Color c) { return animateColorAsState(c, 400); });
}

#! void App() {}
void Root(bool dark) {
    MaterialTheme(animateColorScheme(dark ? darkColorScheme() : lightColorScheme()), App);
}
```

`map` içindeki her `animateColorAsState` çağrısı her seferinde aynı sırayla çalışır; bu yüzden her renk kendi animasyonunu korur.

## Tipografi

`Typography`, Material 3'ün yazı ölçeğini tutar: `displayLarge`, `displayMedium`, `displaySmall`, `headlineLarge` … `headlineSmall`, `titleLarge` … `titleSmall`, `bodyLarge` … `bodySmall` ve `labelLarge` … `labelSmall`. Her biri bir `Font`'tur: sp cinsinden bir boyut, bir kalınlık, bir harf aralığı ve bir satır yüksekliği. Bunları `typography()` üzerinden kullanın:

```cpp body
Text("Settings", {.font = typography().headlineSmall});
Text("Notifications are on", {.font = typography().bodyMedium});
```

Yazı ölçeğini değiştirmek için kendi `Typography`'nizi `LocalTypography` üzerinden sağlayın:

```cpp
#! void App() {}
void BrandTypography(const Content& content) {
    static const FontFamily lora = FontFamily::fromFiles({{"fonts/Lora-Regular.ttf"}, {"fonts/Lora-Bold.ttf", FontWeight::Bold}});
    Typography type;
    for (Font* f : {&type.displayLarge, &type.displayMedium, &type.displaySmall, &type.headlineLarge,
                    &type.headlineMedium, &type.headlineSmall})
        f->family = lora.id;
    CompositionLocalProvider({LocalTypography.provides(type)}, content);
}
```

Font dosyaları varlıklardır (bkz. [Kaynaklar](resources.md)). Genel aileler `FontFamily::Serif` ve `FontFamily::Monospace`, sistemde varsa sistemin fontlarını kullanır. Metin boyutları sistemin font ölçeğini izler.

## Şekiller

Bileşenlerin Material şekilleri yerleşiktir: bir butonun tam yuvarlaklığı, bir kartın 12 dp'lik köşeleri, bir alt sayfanın 28 dp'lik üst köşeleri. Hepsini birden değiştirecek tema çapında bir `Shapes` nesnesi yok; bir bileşen şekil alıyorsa ona şekli verin, kendi şekilleriniz için de `RoundedCornerShape`, `CutCornerShape`, `CircleShape` ya da Material şekil kütüphanesini (`MaterialShapes`) kullanın.

## Kendi tasarım sisteminiz

Uygulamanız Material'ı izlemiyorsa kendi temanızı `MaterialTheme`'in kurulduğu gibi kurun: değerleri bir struct'a koyun, bir `CompositionLocal` üzerinden sağlayın ve küçük bir erişim fonksiyonuyla okuyun:

```cpp
struct BrandColors {
    Color accent, background, text;
    friend bool operator==(const BrandColors&, const BrandColors&) = default;
};

inline const CompositionLocal<BrandColors> LocalBrandColors{
    BrandColors{Color::hex(0xFF5722), colors::White, Color::hex(0x212121)}};

inline const BrandColors& brandColors() { return LocalBrandColors.current(); }

void BrandTheme(BrandColors colors, const Content& content) {
    CompositionLocalProvider({LocalBrandColors.provides(colors), LocalContentColor.provides(colors.text)}, content);
}

void BrandButton(const std::string& label, std::function<void()> onClick) {
    Box(Modifier().clickable(std::move(onClick)).background(brandColors().accent, 8).padding(16, 10), [&] {
        Text(label, {.color = brandColors().background});
    });
}
```

İkisini karıştırabilirsiniz: Material bileşenleri için `MaterialTheme`'i tutun, Material'ın kapsamadığı şeyler için kendi local'lerinizi ekleyin.

## Ayrıca bakın

- [CompositionLocal](composition-local.md): temalar bileşenlere nasıl ulaşır.
- [Material bileşenleri](material.md): temanın stillendirdikleri.
- [Metin](text.md): fontlar ve metin stilleri.
