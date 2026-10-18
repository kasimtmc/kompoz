# Modifier'lar

Bir `Modifier` bir composable'ı süsler ya da ona yeni bir şey katar: boyutunu ve iç boşluğunu, arka planını, tıklanınca ne olacağını, erişilebilirlik servislerine ne söyleyeceğini. Modifier'lar zincirlenir ve zincirin sırası önemlidir.

## Modifier kullanmak

Çoğu composable parametre olarak bir `Modifier` alır. Zincir `Modifier()` ile başlar ve her çağrı bir öğe ekler:

```cpp
void ArtistCard(const std::string& name) {
    Row(Modifier()
            .fillMaxWidth()
            .clickable([] { /* sanatçıyı aç */ })
            .padding(16),
        [&] {
            Icon(Icons::Filled::Person, "", Modifier().size(40));
            Spacer(Modifier().width(12));
            Text(name, {.font = typography().titleMedium});
        });
}
```

Modifier'lar düz değerlerdir: onları değişkenlerde saklayabilir, bir yerden bir yere aktarabilir, koşula göre kurabilirsiniz.

```cpp body
#! bool selected = false;
Modifier base = Modifier().fillMaxWidth().padding(8);
Box(selected ? base.background(colorScheme().secondaryContainer, 12) : base);
```

## Sıra önemlidir

Her öğe, zincirde kendinden sonra gelen her şeyi sarar. İlk öğe en dıştakidir: ilk o ölçülür ve geri kalanın çevresine çizilir. Bu yüzden aynı öğeler farklı sırada farklı sonuç verir:

```cpp body
// 72 dp'lik alanın tamamı tıklanabilir ve iç boşluk tıklama alanının içindedir.
Box(Modifier().clickable([] {}).padding(16).size(40));

// Yalnız 40 dp'lik kutu tıklanabilir; iç boşluk onun dışındadır.
Box(Modifier().padding(16).clickable([] {}).size(40));
```

Arka planlar için de aynısı geçerli:

```cpp body
Box(Modifier().background(colorScheme().primaryContainer).padding(16).size(40));   // iç boşluk boyanır
Box(Modifier().padding(16).background(colorScheme().primaryContainer).size(40));   // iç boşluk boyanmaz
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
| `width(IntrinsicSize::Min)`, `height(IntrinsicSize::Max)` | İçeriğin intrinsic boyutu (bkz. [Özel yerleşimler](custom-layouts.md)). |

Tüm uzunluklar dp cinsindendir.

### İç boşluk ve konum

| Modifier | Ne yapar |
|---|---|
| `padding(all)`, `padding(h, v)`, `padding(start, top, end, bottom)`, `padding(PaddingValues)` | İçeriğin çevresinde boşluk. `start` ve `end` sağdan sola yerleşimlerde yer değiştirir. |
| `absolutePadding(left, top, right, bottom)` | Aynalanmayan iç boşluk. |
| `offset(x, y)` | İçeriği, yerleşimdeki boyutunu değiştirmeden kaydırır. |
| `offset([] { return Offset{…}; })` | Aynısı, ama yerleşim sırasında okunur: sık değişen kaydırmalar için (bkz. [Aşamalar](phases.md)). |
| `windowInsetsPadding(insets)`, `safeDrawingPadding()`, `systemBarsPadding()`, `imePadding()`… | İçeriği sistem çubuklarından, ekran çentiğinden ya da klavyeden uzak tutar. |

### Çizim

| Modifier | Ne yapar |
|---|---|
| `background(color, shape)`, `background(brush, shape)` | İçeriğin arkasını doldurur. |
| `border(width, color, shape)` | Bir çerçeve çizer. |
| `clip(shape)` | İçeriği bir şekle göre kırpar. |
| `shadow(elevation, shape)`, `dropShadow`, `innerShadow` | Gölgeler. |
| `alpha(a)`, `scale(s)`, `rotate(degrees)`, `graphicsLayer(…)`, `blur(r)` | İçeriğin yerleşimini değil, nasıl çizildiğini değiştirir. |
| `drawBehind(…)`, `drawWithContent(…)`, `drawWithCache(…)` | Kendi çiziminiz (bkz. [Grafik](graphics.md)). |

### Etkileşim

| Modifier | Ne yapar |
|---|---|
| `clickable(onClick)`, `combinedClickable(onClick, onLongClick)` | Dalga efekti, klavyeyle etkinleştirme ve erişilebilirlikle birlikte tıklama ve dokunma. |
| `selectable(selected, onClick, role)`, `toggleable(value, onChange, role)` | Radyo düğmesi ya da onay kutusu gibi davranan satırlar için seçme ve açıp kapama. |
| `draggable`, `anchoredDraggable`, `transformable`, `pointerInput` | Hareketler (bkz. [Hareketler](gestures.md)). |
| `verticalScroll(state)`, `horizontalScroll(state)`, `scrollable` | Kaydırma (bkz. [Listeler ve ızgaralar](lists.md)). |
| `focusable()`, `focusRequester(r)`, `onKeyEvent(…)` | Klavye odağı ve tuşlar. |
| `pointerHoverIcon(PointerIcon::Hand)` | Üzerindeyken fare imleci. |

### Erişilebilirlik ve testler

| Modifier | Ne yapar |
|---|---|
| `semantics({…})`, `clearAndSetSemantics({…})` | Erişilebilirlik servislerine ne söylendiği (bkz. [Erişilebilirlik](accessibility.md)). |
| `testTag("name")` | Testlerin düğümü bulabileceği bir isim (bkz. [Test](testing.md)). |

### Yerleşim geri çağrıları

| Modifier | Ne yapar |
|---|---|
| `onSizeChanged([](Size s) { … })` | Düğümün boyutu değişince çağrılır. |
| `onGloballyPositioned([](const LayoutCoordinates& c) { … })` | Penceredeki konumu değişince çağrılır. |
| `onVisibilityChanged([](bool visible) { … })` | Görünür olunca ya da görünmez olunca çağrılır. |
| `zIndex(z)` | Onu kardeşlerinin üstünde ya da altında çizer. |

## Scope modifier'ları

Bazı modifier'lar, Compose'un scope'a bağlı modifier'ları (`RowScope.weight`) gibi, yalnız belirli bir ebeveynin içinde anlam taşır:

| Modifier | Nerede | Ne yapar |
|---|---|---|
| `weight(w)` | `Row`, `Column` | Artan alanı ağırlık verilmiş çocuklar arasında paylaştırır. |
| `align(Alignment::…)` | `Box` | Bu çocuğu kutu içinde hizalar. |
| `align(Alignment::CenterHorizontally)` | `Column` | Bu çocuğu sütunun enine hizalar. |
| `align(Alignment::CenterVertically)`, `alignByBaseline()` | `Row` | Bu çocuğu satırın enine hizalar. |
| `matchParentSize()` | `Box` | Kutuyu büyütmeden kutu kadar büyük. |
| `fillParentMaxWidth()`, `fillParentMaxHeight()` | Tembel liste öğeleri | Listenin görünür alanından bir pay. |
| `animateItem()` | Tembel liste öğeleri | Öğe belirince, yer değiştirince ya da kaybolunca onu canlandırır. |
| `flex(config)` | `FlexBox` | Çocuğun nasıl büyüyüp küçüleceği. |
| `gridItem(row, column, …)` | `Grid` | Çocuğun ızgarada nereye gideceği. |

C++'ta bunlar sıradan `Modifier` metotlarıdır: derleyici `weight`'i bir `Row` ya da `Column` dışında kullanmanıza engel olmaz, yalnızca orada bir etkisi olmaz.

```cpp body
Row(Modifier().fillMaxWidth(), [&] {
    Text("Left", Modifier().weight(1));
    Text("Right");
});
```

## Kendi modifier'ınızı yazmak

### Modifier'ları birleştirmek

C++'ta extension fonksiyonlar yok, bu yüzden kendi modifier'ınız bir zincir döndüren bir fonksiyondur. Onu başka bir zincire `then` ile ekleyin:

```cpp
Modifier cardSurface() {
    return Modifier().shadow(2, RoundedCornerShape(16)).background(colorScheme().surfaceContainerLow, RoundedCornerShape(16)).padding(16);
}

void Note(const std::string& text) {
    Box(Modifier().fillMaxWidth().then(cardSurface()), [&] { Text(text); });
}
```

Genişleteceği zinciri parametre olarak alan bir fonksiyon da doğal okunur: `Modifier card(const Modifier& m = {}) { return m.padding(…); }`.

### Ölçümü değiştirmek

`layout`, içeriği sizin seçtiğiniz kısıtlarla `measure` üzerinden ölçer ve bu düğümün boyutunu döndürür; `placeAt` içeriğin bu düğüm içinde nereye gideceğini söyler:

```cpp
// İçerik, uzun kenarı kadar büyük bir karenin ortasında.
Modifier squareAround() {
    return Modifier().layout([](const MeasureNext& measure, const Constraints& c, Offset& placeAt) {
        Size content = measure(c.loosen());
        float side = std::max(content.width, content.height);
        placeAt = {(side - content.width) / 2, (side - content.height) / 2};
        return c.constrain(Size{side, side});
    });
}
```

### Özel çizim

`drawBehind` içeriğin arkasına, `drawWithContent` çevresine çizer. Bkz. [Grafik](graphics.md).

```cpp
Modifier underline(Color color) {
    return Modifier().drawBehind([color](DrawScope& d) {
        d.drawLine(color, {0, d.size().height}, {d.size().width, d.size().height}, 2);
    });
}
```

### Modifier öğeleri

Perde arkasında bir modifier, `ModifierElement`'lerden oluşan bir listedir. Bir öğe ölçüme, çizime ve girdiye katılabilir; `then(std::make_shared<MyElement>(…))` bir tane ekler. Bu, kütüphanenin kendi modifier'larının kullandığı daha alt düzey bir API'dir; ihtiyaçların çoğunu yukarıdaki fonksiyonlar karşılar. Bir öğe yazarsanız `equals`'ı da yazın ki eşit bir modifier'la çağrılan bir composable yine atlanabilsin.

## Ayrıca bakın

- [Yerleşimler](layouts.md): scope modifier'larının çalıştığı ebeveynler.
- [Aşamalar](phases.md): yerleşim ya da çizim sırasında durum okuyan modifier'lar.
- Etkileşim ve çizim modifier'ları için [Hareketler](gestures.md) ve [Grafik](graphics.md).
