# KompozitionLocal

Bir `KompozitionLocal`, bir değeri aradaki her fonksiyondan geçirmeden kompozisyon ağacında aşağı iletir. Temanın renkleri, metin stili ve yerleşim yönü her bileşene bu yolla ulaşır.

## Bir local'i okumak

Her local'in ağacın her noktasında bir değeri vardır: yukarıdaki en yakın sağlayıcının verdiği, yoksa varsayılanı. `current` onu okur:

```kotlin
@Kompozable
fun Caption(text: String) {
    val tint = LocalContentColor.current   // burada metin ve ikonların kullandığı renk
    Text(text, color = tint, style = MaterialTheme.typography.labelSmall)
}
```

Bir local'i okumak, bir durumu okumak gibi çalışan scope'u abone yapar: yukarıdaki bir sağlayıcı farklı bir değer vermeye başlayınca, aradaki her şey atlanmış olsa bile onu okuyan scope'lar yeniden kompoze edilir.

Tema da Compose'daki gibi local'ler üzerinden okunur: `MaterialTheme.colorScheme`, `LocalColorScheme.current`'tır; `MaterialTheme.typography`, `LocalTypography.current`'tır; `isSystemInDarkTheme()` de `LocalSystemDarkTheme`'i okur.

## Bir değer sağlamak

`KompozitionLocalProvider`, bir ya da daha fazla local'e içeriğinin içindeki her şey için bir değer verir:

```kotlin
@Kompozable
fun Warning(message: String) {
    KompozitionLocalProvider(LocalContentColor provides MaterialTheme.colorScheme.error) {
        Row {
            Icon(Icons.Filled.Info, contentDescription = null)   // hata rengiyle boyanır
            Text(message)                                        // metin de öyle
        }
    }
}
```

Değer yalnız içerik lambdasının içinde geçerlidir; onun dışında local'in önceki değeri vardır. Bir kerede üç local'e kadar değer verilebilir: `KompozitionLocalProvider(LocalA provides a, LocalB provides b) { … }`. Sağlayıcılar iç içe geçer: içteki, kendi içeriği için dıştakini geçersiz kılar.

`LocalX providesDefault value` yalnız yukarıda hiçbir sağlayıcının değer vermediği yerde bir değer verir: bir kütüphanenin, uygulamanın yine de geçersiz kılabileceği bir varsayılan koyma yolu.

## Kendi local'inizi tanımlamak

Bir local hangisi olduğuyla tanınır, bu yüzden onu varsayılanıyla birlikte bir kez, en üst düzeyde bir `val` olarak tanımlayın (kompozc böyle bir `val`'i, onu kullananlardan önce tanımlanan bir isim alanı sabiti yapar):

```kotlin
data class Spacing(val small: Dp = 4.dp, val medium: Dp = 8.dp, val large: Dp = 16.dp)

val LocalSpacing = staticKompozitionLocalOf { Spacing() }

@Kompozable
fun Titled(title: String) {
    Column(modifier = Modifier.padding(LocalSpacing.current.large)) { Text(title) }
}

@Kompozable
fun Compact(content: @Kompozable () -> Unit) {
    KompozitionLocalProvider(LocalSpacing provides Spacing(2.dp, 4.dp, 8.dp), content = content)
}
```

Varsayılan yalnız local bir sağlayıcı olmadan okunduğunda yapılır. Mantıklı bir varsayılan yoksa, Compose'daki gibi bunu söyleyebilir:

```kotlin
data class User(val name: String)

val LocalUser = kompozitionLocalOf<User> { error("LocalUser was not provided") }
```

Compose'daki gibi hem `kompozitionLocalOf` hem `staticKompozitionLocalOf` vardır; Kompoz'da iki tür de yalnız local'i okuyan scope'ları yeniden kompoze eder.

Bir data class (ya da `operator==`'ı olan bir C++ türü) değerle karşılaştırılır: bir sağlayıcı eşit bir değer verdiğinde hiçbir şey yeniden kompoze edilmez.

## Yerleşik local'ler

| Local | Ne tutar |
|---|---|
| `LocalColorScheme`, `LocalTypography`, `LocalMotionScheme` | Material teması (`MaterialTheme.colorScheme`, `.typography`, `.motionScheme` ile okunur). |
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

Başka bir yerde kompoze edilen içerik, örneğin bir popup'taki, çağrıldığı yerin local'lerini zaten alır. Onları elle taşımanız gerekirse, `currentKompozitionLocalContext` bir noktadaki local'leri yakalar, `KompozitionLocalProvider(context) { … }` da onları yeniden sağlar.

## Ayrıca bakın

- [Tema](theming.md): Material'ın teması local'ler üzerine kurulu.
- [Durum](state.md): parametreler ve durumu yukarı taşımayla açık veri akışı.
