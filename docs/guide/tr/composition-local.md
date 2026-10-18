# CompositionLocal

Bir `CompositionLocal`, bir değeri aradaki her fonksiyondan geçirmeden kompozisyon ağacında aşağı iletir. Temanın renkleri, metin stili ve yerleşim yönü her bileşene bu yolla ulaşır.

## Bir local'i okumak

Her local'in ağacın her noktasında bir değeri vardır: yukarıdaki en yakın sağlayıcının verdiği, yoksa varsayılanı. `current()` onu okur:

```cpp
void Caption(const std::string& text) {
    Color color = LocalContentColor.current();          // burada metin ve ikonların kullandığı renk
    Text(text, {.font = typography().labelSmall, .color = color});
}
```

Bir local'i okumak, bir durumu okumak gibi çalışan scope'u abone yapar: yukarıdaki bir sağlayıcı farklı bir değer vermeye başlayınca, aradaki her şey atlanmış olsa bile onu okuyan scope'lar yeniden kompoze edilir.

Tema erişimcilerinin altında da local'ler vardır: `colorScheme()`, `LocalColorScheme.current()`'tır; `typography()`, `LocalTypography.current()`'tır; `isSystemInDarkTheme()` `LocalSystemDarkTheme`'i okur.

## Bir değer sağlamak

`CompositionLocalProvider`, bir ya da daha fazla local'e içeriğinin içindeki her şey için bir değer verir:

```cpp
void Warning(const std::string& message) {
    CompositionLocalProvider({LocalContentColor.provides(colorScheme().error)}, [&] {
        Row([&] {
            Icon(Icons::Filled::Info, "");   // hata rengiyle boyanır
            Text(message);                        // metin de öyle
        });
    });
}
```

Değer yalnız içerik lambdasının içinde geçerlidir; onun dışında local'in önceki değeri vardır. Sağlayıcılar iç içe geçer: içteki, kendi içeriği için dıştakini geçersiz kılar.

`providesDefault(value)` yalnız yukarıda hiçbir sağlayıcının değer vermediği yerde bir değer verir: bir kütüphanenin, uygulamanın yine de geçersiz kılabileceği bir varsayılan koyma yolu.

## Kendi local'inizi tanımlamak

Bir local adresiyle tanınır, bu yüzden onu varsayılanıyla birlikte bir kez, global olarak tanımlayın:

```cpp
struct Spacing {
    float small = 4, medium = 8, large = 16;
    friend bool operator==(const Spacing&, const Spacing&) = default;
};

inline const CompositionLocal<Spacing> LocalSpacing{Spacing{}};

void Card1(const std::string& title) {
    Column(Modifier().padding(LocalSpacing.current().large), [&] { Text(title); });
}

void Compact(const Content& content) {
    CompositionLocalProvider({LocalSpacing.provides(Spacing{2, 4, 8})}, content);
}
```

Mantıklı bir varsayılan yoksa local'i `compositionLocalOf` ile yapın; varsayılanı yalnız bir sağlayıcı olmadan okunduğunda yapılır ve local'in sağlanması gerektiğini söylemek için bir istisna fırlatabilir:

```cpp
struct User {
    std::string name;
    friend bool operator==(const User&, const User&) = default;
};

inline const auto LocalUser = compositionLocalOf<User>([]() -> User { throw std::logic_error("LocalUser was not provided"); });
```

`staticCompositionLocalOf` Compose'dan gelen kod için vardır; Kompoz'da iki tür de yalnız local'i okuyan scope'ları yeniden kompoze eder.

Bir local'in türüne `operator==` verin: bir sağlayıcı eşit bir değer verdiğinde hiçbir şey yeniden kompoze edilmez.

## Yerleşik local'ler

| Local | Ne tutar |
|---|---|
| `LocalColorScheme`, `LocalTypography`, `LocalMotionScheme` | Material teması (`colorScheme()`, `typography()`, `motionScheme()` ile okunur). |
| `LocalContentColor` | Burada metin ve ikonlar için renk; Material kapsayıcıları onu kendi içerik renklerine ayarlar. |
| `LocalTextStyle` | Hiçbiri verilmediğinde `Text`'in kullandığı font. |
| `LocalLayoutDirection` | Soldan sağa ya da sağdan sola; kökte sistem dilinden gelir. |
| `LocalDensity` | Ekranın yoğunluğu ve sistemin font ölçeği. |
| `LocalWindowSize`, `LocalWindowInfo` | Pencerenin dp cinsinden boyutu ve odakta olup olmadığı. |
| `LocalSystemDarkTheme` | Sistemin koyu modda olup olmadığı (`isSystemInDarkTheme()` ile okunur). |
| `LocalWindowInsets`, `LocalSystemInsets` | Sistem çubuklarının, kesiğin ve klavyenin kenar boşlukları. |
| `LocalViewConfiguration` | Dokunma eşiği (touch slop), uzun basma süresi ve platformun diğer hareket eşikleri. |
| `LocalIndication` | Tıklanabilir öğelerin basılınca çizdiği şey: varsayılan olarak Material'ın dalgalanması (ripple). |
| `LocalMinimumInteractiveComponentSize` | Bileşenlerin koruduğu en küçük dokunma hedefi (48 dp). |
| `LocalTextSelectionColors` | Seçili metnin ve tutamaçlarının renkleri. |
| `LocalScrollbarStyle` | Kaydırma çubuklarının nasıl göründüğü. |
| `LocalStrings` | Kütüphanenin kendi metinleri, İngilizce ya da Türkçe (bkz. [Kaynaklar](resources.md)). |
| `LocalUiMediaScope` | `mediaQuery` için pencerenin boyutu, işaretçisi ve klavyesi. |

## Ne zaman kullanılmalı

Bir local örtüktür: birini okuyan bir fonksiyon, imzasının göstermediği bir şeye bağlıdır. Onları ölçülü kullanın:

- **İyi kullanımlar**, ağacın büyük kısmının ihtiyaç duyduğu ve nadiren değişen değerlerdir: bir tema, bir tasarım sisteminin token'ları, bir metin yönü, mevcut kullanıcının ayarları.
- Yalnız birkaç composable'ın ihtiyaç duyduğu veri ve sık değişen durum için **onlardan kaçının**. Bunları parametre olarak geçirin; böylece bağımlılık görünür olur, composable'ı önizlemek ve test etmek kolaylaşır.

Bir local, durumu yukarı taşımanın yerini tutmaz: değerleri aşağı iletir, olayları yukarı değil.

## Local'leri başka bir kompozisyona geçirmek

Başka bir yerde kompoze edilen içerik, örneğin bir popup'taki, çağrıldığı yerin local'lerini zaten alır. Onları elle taşımanız gerekirse, `currentCompositionLocalContext()` bir noktadaki local'leri yakalar, `CompositionLocalProvider` da onları yeniden sağlayabilir.

## Ayrıca bakın

- [Tema](theming.md): Material'ın teması local'ler üzerine kurulu.
- [Durum](state.md): parametreler ve durumu yukarı taşımayla açık veri akışı.
