# Modifier'lar

Bir `Modifier` bir composable'ı süsler ya da ona yeni bir şey katar: boyutunu ve iç boşluğunu, arka planını, tıklanınca ne olacağını, erişilebilirlik servislerine ne söyleyeceğini. Modifier'lar zincirlenir ve zincirin sırası önemlidir.

## Modifier kullanmak

Çoğu composable bir `modifier` parametresi alır. Zincir `Modifier` ile başlar ve her çağrı bir öğe ekler:

```kotlin
@Kompozable
fun ArtistCard(name: String) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { /* sanatçıyı aç */ }
            .padding(16.dp)
    ) {
        Icon(Icons.Filled.Person, contentDescription = null, modifier = Modifier.size(40.dp))
        Spacer(Modifier.width(12.dp))
        Text(name, style = MaterialTheme.typography.titleMedium)
    }
}
```

Modifier'lar düz değerlerdir: onları değişkenlerde saklayabilir, bir yerden bir yere aktarabilir, koşula göre kurabilirsiniz.

```kotlin body
#! val picked = false
val base = Modifier.fillMaxWidth().padding(8.dp)
Box(modifier = if (picked) base.background(MaterialTheme.colorScheme.secondaryContainer, RoundedCornerShape(12.dp)) else base)
```

## Sıra önemlidir

Her öğe, zincirde kendinden sonra gelen her şeyi sarar. İlk öğe en dıştakidir: ilk o ölçülür ve geri kalanın çevresine çizilir. Bu yüzden aynı öğeler farklı sırada farklı sonuç verir:

```kotlin body
// 72 dp'lik alanın tamamı tıklanabilir ve iç boşluk tıklama alanının içindedir.
Box(modifier = Modifier.clickable { }.padding(16.dp).size(40.dp))

// Yalnız 40 dp'lik kutu tıklanabilir; iç boşluk onun dışındadır.
Box(modifier = Modifier.padding(16.dp).clickable { }.size(40.dp))
```

Arka planlar için de aynısı geçerli:

```kotlin body
Box(modifier = Modifier.background(MaterialTheme.colorScheme.primaryContainer).padding(16.dp).size(40.dp))   // iç boşluk boyanır
Box(modifier = Modifier.padding(16.dp).background(MaterialTheme.colorScheme.primaryContainer).size(40.dp))   // iç boşluk boyanmaz
```

Bir zinciri soldan sağa, "dıştan içe" diye okuyun.

## Sık kullanılan modifier'lar

### Boyut

| Modifier | Ne yapar |
|---|---|
| `size(s)`, `size(w, h)`, `width(w)`, `height(h)` | Ebeveynin kısıtları içinde tercih edilen bir boyut. |
| `requiredSize(…)`, `requiredWidth(w)`, `requiredHeight(h)` | Ebeveynin kısıtları aksini söylese bile bu boyut. |
| `widthIn(min, max)`, `heightIn(min, max)`, `sizeIn(…)` | Boyuta sınırlar. |
| `defaultMinSize(w, h)` | İçeriğin aşabileceği bir en küçük boyut. |
| `fillMaxWidth(f)`, `fillMaxHeight(f)`, `fillMaxSize(f)` | Ebeveynin sunduğu alanın tamamı (ya da bir kesri). |
| `wrapContentSize(align)`, `wrapContentWidth`, `wrapContentHeight` | İçeriğin kendi boyutu, ebeveynin dayattığı alan içinde hizalanmış. |
| `aspectRatio(r)` | Kısıtlar içinde bir genişlik/yükseklik oranı. |
| `width(IntrinsicSize.Min)`, `height(IntrinsicSize.Max)` | İçeriğin intrinsic boyutu (bkz. [Özel yerleşimler](custom-layouts.md)). |

Tüm uzunluklar dp cinsindendir (`16.dp`).

### İç boşluk ve konum

| Modifier | Ne yapar |
|---|---|
| `padding(all)`, `padding(h, v)`, `padding(start, top, end, bottom)`, `padding(PaddingValues)` | İçeriğin çevresinde boşluk. `start` ve `end` sağdan sola yerleşimlerde yer değiştirir. |
| `absolutePadding(left, top, right, bottom)` | Aynalanmayan iç boşluk. |
| `offset(x, y)` | İçeriği, yerleşimdeki boyutunu değiştirmeden kaydırır. |
| `offset { IntOffset(…) }` | Aynısı, ama yerleşim sırasında okunur: sık değişen kaydırmalar için (bkz. [Aşamalar](phases.md)). |
| `windowInsetsPadding(insets)`, `safeDrawingPadding()`, `systemBarsPadding()`, `imePadding()`… | İçeriği sistem çubuklarından, ekran çentiğinden ya da klavyeden uzak tutar. |

### Çizim

| Modifier | Ne yapar |
|---|---|
| `background(color, shape)`, `background(brush, shape)` | İçeriğin arkasını doldurur. |
| `border(width, color, shape)` | Bir çerçeve çizer. |
| `clip(shape)` | İçeriği bir şekle göre kırpar. |
| `shadow(elevation, shape)`, `dropShadow`, `innerShadow` | Gölgeler. |
| `alpha(a)`, `scale(s)`, `rotate(degrees)`, `graphicsLayer(…)`, `graphicsLayer { … }`, `blur(r)` | İçeriğin yerleşimini değil, nasıl çizildiğini değiştirir. |
| `drawBehind { … }`, `drawWithContent { … }`, `drawWithCache { … }` | Kendi çiziminiz (bkz. [Grafik](graphics.md)). |

### Etkileşim

| Modifier | Ne yapar |
|---|---|
| `clickable { … }`, `combinedClickable(onClick, onLongClick)` | Dalga efekti, klavyeyle etkinleştirme ve erişilebilirlikle birlikte tıklama ve dokunma. |
| `selectable(selected, onClick, role)`, `toggleable(value, onChange, role)` | Radyo düğmesi ya da onay kutusu gibi davranan satırlar için seçme ve açıp kapama. |
| `draggable`, `anchoredDraggable`, `transformable`, `pointerInput` | Hareketler (bkz. [Hareketler](gestures.md)). |
| `verticalScroll(state)`, `horizontalScroll(state)`, `scrollable` | Kaydırma (bkz. [Listeler ve ızgaralar](lists.md)). |
| `focusable()`, `focusRequester(r)`, `onKeyEvent { … }` | Klavye odağı ve tuşlar. |
| `pointerHoverIcon(PointerIcon.Hand)` | Üzerindeyken fare imleci. |

### Erişilebilirlik ve testler

| Modifier | Ne yapar |
|---|---|
| `semantics { … }`, `clearAndSetSemantics { … }` | Erişilebilirlik servislerine ne söylendiği (bkz. [Erişilebilirlik](accessibility.md)). |
| `testTag("name")` | Testlerin düğümü bulabileceği bir isim (bkz. [Test](testing.md)). |

### Yerleşim geri çağrıları

| Modifier | Ne yapar |
|---|---|
| `onSizeChanged { size -> … }` | Düğümün boyutu değişince çağrılır. |
| `onGloballyPositioned { coordinates -> … }` | Penceredeki konumu değişince çağrılır. |
| `onVisibilityChanged { visible -> … }` | Görünür olunca ya da görünmez olunca çağrılır. |
| `zIndex(z)` | Onu kardeşlerinin üstünde ya da altında çizer. |

## Scope modifier'ları

Bazı modifier'lar, Compose'un scope'a bağlı modifier'ları (`RowScope.weight`) gibi, yalnız belirli bir ebeveynin içinde anlam taşır:

| Modifier | Nerede | Ne yapar |
|---|---|---|
| `weight(w)` | `Row`, `Column` | Artan alanı ağırlık verilmiş çocuklar arasında paylaştırır. |
| `align(Alignment.…)` | `Box` | Bu çocuğu kutu içinde hizalar. |
| `align(Alignment.CenterHorizontally)` | `Column` | Bu çocuğu sütunun enine hizalar. |
| `align(Alignment.CenterVertically)`, `alignByBaseline()` | `Row` | Bu çocuğu satırın enine hizalar. |
| `matchParentSize()` | `Box` | Kutuyu büyütmeden kutu kadar büyük. |
| `fillParentMaxWidth()`, `fillParentMaxHeight()` | Tembel liste öğeleri | Listenin görünür alanından bir pay. |
| `animateItem()` | Tembel liste öğeleri | Öğe belirince, yer değiştirince ya da kaybolunca onu canlandırır. |
| `flex(config)` | `FlexBox` | Çocuğun nasıl büyüyüp küçüleceği. |
| `gridItem(row, column, …)` | `Grid` | Çocuğun ızgarada nereye gideceği. |

Burada bunlar bir scope'un değil, `Modifier`'ın sıradan üyeleridir: Compose'un scope'larının yapacağı gibi hiçbir şey `weight`'i bir `Row` ya da `Column` dışında kullanmanıza engel olmaz; yalnızca orada bir etkisi olmaz.

```kotlin body
Row(modifier = Modifier.fillMaxWidth()) {
    Text("Sol", modifier = Modifier.weight(1f))
    Text("Sağ")
}
```

## Kendi modifier'ınızı yazmak

### Modifier'ları birleştirmek

Kendi modifier'ınız, Compose'daki gibi, çağrıldığı zincire ekleyen bir `Modifier` uzantı fonksiyonudur:

```kotlin
fun Modifier.cardSurface(color: Color) =
    shadow(2.dp, RoundedCornerShape(16.dp))
        .background(color, RoundedCornerShape(16.dp))
        .padding(16.dp)

@Kompozable
fun Note(text: String) {
    Box(modifier = Modifier.fillMaxWidth().cardSurface(MaterialTheme.colorScheme.surfaceContainerLow)) { Text(text) }
}
```

kompozc onu zinciri ilk parametre olarak alan bir fonksiyon yapar: `Modifier.fillMaxWidth().cardSurface(c)`, `cardSurface(Modifier.fillMaxWidth(), c)`'dir. Gövdesinde `shadow` ve ötekiler, `this` gibi zincirin kendisinindir. `then` iki zinciri birleştirir: `Modifier.fillMaxWidth().then(other)`.

### Ölçümü değiştirmek

`layout { measurable, constraints -> }` içeriği sizin seçtiğiniz kısıtlarla ölçer; `layout(width, height) { }` bu düğümün boyutunu verir ve içeriği onun içine yerleştirir, Compose'daki gibi:

```kotlin
// İçerik, uzun kenarı kadar büyük bir karenin ortasında.
fun Modifier.squareAround() = layout { measurable, constraints ->
    val placeable = measurable.measure(constraints)
    val side = maxOf(placeable.width, placeable.height)
    layout(side, side) {
        placeable.placeRelative((side - placeable.width) / 2, (side - placeable.height) / 2)
    }
}
```

`placeRelative` baştan yerleştirir, bu yüzden içerik sağdan sola düzende aynalanır; `place` yön ne olursa olsun soldan yerleştirir.

### Özel çizim

`drawBehind` içeriğin arkasına, `drawWithContent` çevresine çizer. Bkz. [Grafik](graphics.md).

```kotlin
fun Modifier.underline(color: Color) = drawBehind {
    drawLine(color, Offset(0f, size.height), Offset(size.width, size.height), 2f)
}
```

### Modifier öğeleri

Perde arkasında bir modifier, `ModifierElement`'lerden oluşan bir listedir. Bir öğe ölçüme, çizime ve girdiye katılabilir; `then(std::make_shared<MyElement>(…))` bir tane ekler. Bu, kütüphanenin kendi modifier'larının kullandığı, C++'ta daha alt düzey bir API'dir (bkz. [C++ ile yazmak](cpp.md)); ihtiyaçların çoğunu yukarıdaki fonksiyonlar karşılar. Bir öğe yazarsanız `equals`'ı da yazın ki eşit bir modifier'la çağrılan bir composable yine atlanabilsin.

## Ayrıca bakın

- [Yerleşimler](layouts.md): scope modifier'larının çalıştığı ebeveynler.
- [Aşamalar](phases.md): yerleşim ya da çizim sırasında durum okuyan modifier'lar.
- Etkileşim ve çizim modifier'ları için [Hareketler](gestures.md) ve [Grafik](graphics.md).
