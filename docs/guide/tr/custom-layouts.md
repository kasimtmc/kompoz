# Özel yerleşimler

`Row`, `Column`, `Box` ve diğer yerleşimler işinizi görmediğinde çocukları `Layout` ile kendiniz ölçüp yerleştirin. Bu sayfa ayrıca layout modifier'ını, intrinsic boyutları, `SubcomposeLayout`'u ve yerleşim değişikliklerini canlandırmayı anlatıyor.

## Kısıtlar

Ebeveyn her çocuğu `Constraints` ile ölçer: dp cinsinden en küçük ve en büyük genişlik ve yükseklik. Çocuk bunların içinde bir boyut seçer. Bazı yardımcılar:

| | |
|---|---|
| `Constraints.fixed(w, h)` | Tam olarak bu boyut. |
| `constraints.copy(minWidth = 0, minHeight = 0)` | Aynı en büyük değerler, en küçükler sıfır: çocuk daha küçük olabilir. |
| `constraints.constrainWidth(w)`, `constrainHeight(h)` | Kısıtların içine sıkıştırılmış bir genişlik ya da yükseklik. |
| `constraints.hasBoundedWidth`, `hasBoundedHeight` | En büyük değerin sonlu olup olmadığı: dikey bir kaydırmanın içinde yükseklik sonlu değildir. |
| `constraints.offset(-h, -v)` | İç boşluk için `h` ve `v` dp düşülmüş kısıtlar. |

Bunlar dp cinsindendir ve float'tır: Compose'unkilerin tam piksel olduğu yerde (`indent.roundToPx()`) burada dp'nin kendisidir.

## Layout

`Layout(content, modifier) { measurables, constraints -> … }`, `content`'i kompoze eder ve her çocuğunu bir `Measurable` olarak ölçüm politikasına, lambdasına verir. Politika onları ölçer; `layout(width, height) { … }` de yerleşimin kendi boyutunu verir ve onları yerleştirir:

```kotlin
// Çocuklar alt alta, her biri bir öncekinden `indent` kadar daha sağa itilmiş.
@Composable
fun Staircase(indent: Dp, modifier: Modifier = Modifier, content: @Composable () -> Unit) {
    Layout(content = content, modifier = modifier) { measurables, constraints ->
        val placeables = measurables.map { it.measure(constraints.copy(minWidth = 0, minHeight = 0)) }
        var width = 0f
        var height = 0f
        placeables.forEachIndexed { i, placeable ->
            width = maxOf(width, i * indent + placeable.width)
            height += placeable.height
        }
        layout(constraints.constrainWidth(width), constraints.constrainHeight(height)) {
            var y = 0f
            placeables.forEachIndexed { i, placeable ->
                placeable.placeRelative(x = i * indent, y = y)
                y += placeable.height
            }
        }
    }
}

@Composable
fun Steps() {
    Staircase(24.dp) {
        Text("Wake up")
        Text("Make coffee")
        Text("Write code")
    }
}
```

- `measure(constraints)` bir çocuğu ölçer ve `width` ve `height` değerleriyle bir `Placeable` döndürür. Her çocuğu bir geçişte bir kez ölçün.
- `layout`'un bloğunda `placeRelative(x, y)` bir çocuğu yerleşimin başından ve üstünden o uzaklığa koyar ve sağdan sola bir yerleşimde aynalar: politikayı yerleşim soldan sağaymış gibi yazın. `place(x, y)` yön ne olursa olsun soldan yerleştirir; ikisi de bir `zIndex` alır.
- Size verilen kısıtların içinde bir boyut verin; `constrainWidth` ve `constrainHeight` bunu garanti eder.
- `forEach { }`, `map { }` ve Kotlin'in öteki inline fonksiyonları lambdalarını hemen çalıştırır, bu yüzden lambda çevresindeki `var`'ları (buradaki `y`) Kotlin'deki gibi değiştirir.

Policy kompozisyon sırasında değil, yerleşim sırasında çalışır. Okuduğu bir durum değişince yerleşim, yeniden kompoze edilmeden yeniden yerleştirilir.

### Çocukları ayırt etmek

Policy çocuklarını sırayla görür. Onları rollerine göre ayırt etmek için `Modifier.layoutId("name")` ile etiketleyin ve `layoutId`'yi okuyun:

```kotlin
@Composable
fun LabeledValue(label: String, value: String) {
    Layout(
        content = {
            Text(label, modifier = Modifier.layoutId("label"))
            Text(value, modifier = Modifier.layoutId("value"), fontWeight = FontWeight.Bold)
        },
        modifier = Modifier.fillMaxWidth(),
    ) { measurables, constraints ->
        val loose = constraints.copy(minWidth = 0, minHeight = 0)
        val labelPlaceable = measurables.first { it.layoutId == "label" }.measure(loose)
        val valuePlaceable = measurables.first { it.layoutId == "value" }.measure(loose)
        layout(constraints.maxWidth, maxOf(labelPlaceable.height, valuePlaceable.height)) {
            // Etiket başta, değer sonda.
            labelPlaceable.placeRelative(0, 0)
            valuePlaceable.placeRelative(constraints.maxWidth - valuePlaceable.width, 0)
        }
    }
}
```

### Hizalama çizgileri

Bir `Placeable` metin taban çizgilerinin nerede olduğunu bilir: `placeable[FirstBaseline]` ilk taban çizgisinin üstten uzaklığıdır, hiç yoksa NaN'dir (Compose'un `AlignmentLine.Unspecified`'ı). Farklı boyutlardaki metinleri hizalamak için kullanın. `alignByBaseline()` ve `paddingFromBaseline(top, bottom)` modifier'ları sık karşılaşılan durumları özel bir yerleşim yazmadan karşılar.

## Layout modifier'ı

Tek bir composable'ın nasıl ölçüldüğünü ya da yerleştirildiğini değiştirmek için onu bir `Layout`'a sarmak yerine `Modifier.layout { measurable, constraints -> … }` kullanın: içeriği ölçer, `layout(width, height) { … }` de düğümün boyutunu verir ve içeriği onun içine yerleştirir:

```kotlin
// En az `min` yüksekliğinde, içerik bunun içinde dikey olarak ortalanmış.
fun Modifier.atLeastHeight(min: Dp) = layout { measurable, constraints ->
    val placeable = measurable.measure(constraints)
    val height = maxOf(placeable.height, minOf(min, constraints.maxHeight))
    layout(placeable.width, height) {
        placeable.placeRelative(0f, (height - placeable.height) / 2)
    }
}
```

## Intrinsic boyutlar

Yerleşim her çocuğu bir kez ölçer, bu yüzden bir ebeveyn bir çocuğun hangi boyutta olmak isteyeceğini öğrenmek için onu iki kez ölçemez. *Intrinsic* boyutlar bu soruyu gerçekten ölçmeden cevaplar: bu içerik doğal boyutunda ne kadar uzun olurdu?

Tipik kullanımı kardeşleri en uzunları kadar uzun yapmaktır:

```kotlin body
Row(modifier = Modifier.height(IntrinsicSize.Min)) {
    Text("A short label", modifier = Modifier.weight(1f))
    VerticalDivider(modifier = Modifier.fillMaxHeight())   // uzun olan metin kadar uzun
    Text("A label that wraps onto more than one line", modifier = Modifier.weight(1f))
}
```

`height(IntrinsicSize.Min)` ve `width(IntrinsicSize.Min)` (ya da `Max`) düğümü içeriğinin intrinsic boyutuna göre boyutlandırır; onu dolduran çocuklar da bu boyutu doldurur.

> **Not:** Kompoz'un intrinsic'leri Compose'unkilerin bir yaklaşığıdır: hem `Min` hem `Max` içeriği o eksende sınırsız olarak ölçer ve bir `Layout` intrinsic boyutlarının nasıl hesaplanacağını değiştiremez. En küçük intrinsic genişlikte metin her boşlukta satır kırar, bu yüzden en geniş kelimesi kadar geniş çıkar.

## SubcomposeLayout

Bazen neyin kompoze edileceği bir ölçüme bağlıdır: gövdeye göre boyutlanan bir başlık, dar kısıtlar için farklı bir yerleşim, süslediği metne göre boyutlanan bir rozet. `SubcomposeLayout` içeriğinin parçalarını ölçüm *sırasında* kompoze eder:

```kotlin
// İçerik ve altında, içeriğin çıktığı genişlikte bir çizgi.
@Composable
fun Underlined(content: @Composable () -> Unit) {
    SubcomposeLayout { constraints ->
        val loose = constraints.copy(minWidth = 0, minHeight = 0)
        val main = subcompose("main", content).map { it.measure(loose) }
        val width = main.maxOf { it.width }
        val height = main.sumOf { it.height }
        // Eşleşmesi gereken genişlikle kompoze edilir; o değişince yeniden kompoze edilir.
        val line = subcompose("line", width) {
            Box(modifier = Modifier.size(width, 2.dp).background(MaterialTheme.colorScheme.primary))
        }.map { it.measure(loose) }
        layout(width, height + line.sumOf { it.height }) {
            var y = 0f
            main.forEach { it.placeRelative(0, y); y += it.height }
            line.forEach { it.placeRelative(0, y) }
        }
    }
}
```

- `subcompose(slotId, content)` bir slot'u kompoze eder ve measurable'larını döndürür: ilk istendiğinde, Compose'unki gibi, orada hemen kompoze edilir.
- Kompoz'un `subcompose(slotId, dependency, content)`'i, `dependency` değişince slot'u yeniden kompoze eder (Compose'unki bir slot'u her yeni içerik verildiğinde yeniden kompoze eder).
- İçerik daha sonra, kompozisyon sırasında çalışır; buradaki her lambda gibi kullandığını değerle yakalar.

`SubcomposeLayout`, `Layout`'tan daha pahalıdır; yalnızca kompozisyon gerçekten ölçüme bağlıysa kullanın. `BoxWithConstraints` onun en yaygın kullanımının hazır hâlidir.

## Yerleşim değişikliklerini canlandırmak

`Modifier.animateContentSize()`, içeriği büyüyüp küçüldüğünde bir düğümün boyutunu canlandırır.

Çevresindeki yerleşim değiştiği için yer değiştiren ya da yeniden boyutlanan bir düğüm için `Modifier.animateBounds()`, Compose'un `animateBounds`'u gibi onu çizildiği yerden şimdiki yerine süzülerek taşır. Konumu en yakın `LookaheadScope`'a göre ölçülür (Compose'unki onu adlandırır: `animateBounds(this@LookaheadScope)`):

```kotlin
@Composable
fun Chips(wide: Boolean) {
    LookaheadScope(modifier = Modifier.fillMaxWidth()) {
        FlowRow(
            modifier = Modifier.fillMaxWidth(),
            horizontalArrangement = Arrangement.spacedBy(8.dp),
            maxItemsInEachLine = if (wide) 4 else 2,
        ) {
            for (name in listOf("One", "Two", "Three", "Four")) {
                Text(name, modifier = Modifier.animateBounds().padding(8.dp))
            }
        }
    }
}
```

Yalnızca çizim hareket eder; dokunmalar düğümün yerleştiği yere düşer. `skipToLookaheadSize()` içeriği aradaki boyutlarda değil, varacağı boyutta yerleştirir; `approachLayout` ise aradaki adımları kendiniz yazmanızı sağlar.

## Ayrıca bakın

- [Yerleşimler](layouts.md): yerleşik yerleşimler.
- [Aşamalar](phases.md): yerleşim sırasında durum okumanın maliyeti.
- [Modifier'lar](modifiers.md): her yerleşim düğümünü saran modifier zinciri.
