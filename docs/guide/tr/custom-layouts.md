# Özel yerleşimler

`Row`, `Column`, `Box` ve diğer yerleşimler işinizi görmediğinde çocukları `Layout` ile kendiniz ölçüp yerleştirin. Bu sayfa ayrıca layout modifier'ını, intrinsic boyutları, `SubcomposeLayout`'u ve yerleşim değişikliklerini canlandırmayı anlatıyor.

## Kısıtlar

Ebeveyn her çocuğu `Constraints` ile ölçer: dp cinsinden en küçük ve en büyük genişlik ve yükseklik. Çocuk bunların içinde bir boyut seçer. Bazı yardımcılar:

| | |
|---|---|
| `Constraints::fixed(w, h)` | Tam olarak bu boyut. |
| `c.loosen()` | Aynı en büyük değerler, en küçükler sıfır: çocuk daha küçük olabilir. |
| `c.constrain(size)` | Kısıtların içine sıkıştırılmış `size`. |
| `c.hasBoundedWidth()`, `c.hasBoundedHeight()` | En büyük değerin sonlu olup olmadığı: dikey bir kaydırmanın içinde yükseklik sonlu değildir. |
| `c.deflate(h, v)` | İç boşluk için `h` ve `v` dp düşülmüş kısıtlar. |

## Layout

`Layout(modifier, content, measurePolicy)`, `content`'i kompoze eder ve her çocuğunu bir `Measurable` olarak `measurePolicy`'ye verir. Policy onları ölçer, yerleştirir ve yerleşimin kendi boyutunu döndürür:

```cpp
// Çocuklar alt alta, her biri bir öncekinden `indent` dp daha sağa itilmiş.
void Staircase(float indent, const Content& content) {
    Layout(Modifier(), content, [indent](const std::vector<Measurable>& children, const Constraints& c) {
        float x = 0, y = 0, width = 0;
        for (const Measurable& child : children) {
            Placeable p = child.measure(c.loosen());
            p.place(x, y);
            width = std::max(width, x + p.width);
            x += indent;
            y += p.height;
        }
        return c.constrain(Size{width, y});
    });
}

void Steps() {
    Staircase(24, [] {
        Text("Wake up");
        Text("Make coffee");
        Text("Write code");
    });
}
```

- `measure(constraints)` bir çocuğu ölçer ve `width` ve `height` değerleriyle bir `Placeable` döndürür. Her çocuğu bir geçişte bir kez ölçün.
- `place(x, y)` onu yerleşimin başından ve üstünden o uzaklığa koyar; isteğe bağlı üçüncü argüman z-index'idir. Sağdan sola bir yerleşimde Kompoz, Compose'un `placeRelative`'inin yaptığı gibi yerleştirdiklerinizi aynalar: policy'yi yerleşim soldan sağaymış gibi yazın.
- Size verilen kısıtların içinde bir boyut döndürün; `c.constrain` bunu garanti eder.

Policy kompozisyon sırasında değil, yerleşim sırasında çalışır. Okuduğu bir durum değişince yerleşim, yeniden kompoze edilmeden yeniden yerleştirilir.

### Çocukları ayırt etmek

Policy çocuklarını sırayla görür. Onları rollerine göre ayırt etmek için `Modifier().layoutId("name")` ile etiketleyin ve `child.layoutId()`'yi okuyun:

```cpp
void LabeledValue(const std::string& label, const std::string& value) {
    Layout(Modifier().fillMaxWidth(), [&] {
        Text(label, Modifier().layoutId("label"));
        Text(value, Modifier().layoutId("value"), {.fontWeight = FontWeight::Bold});
    }, [](const std::vector<Measurable>& children, const Constraints& c) {
        float height = 0;
        for (const Measurable& child : children) {
            Placeable p = child.measure(c.loosen());
            // Etiket başta, değer sonda.
            p.place(child.layoutId() == "value" ? c.maxWidth - p.width : 0, 0);
            height = std::max(height, p.height);
        }
        return c.constrain(Size{c.maxWidth, height});
    });
}
```

### Hizalama çizgileri

Bir `Placeable` metin taban çizgilerinin nerede olduğunu bilir: `p[AlignmentLine::FirstBaseline]` ilk taban çizgisinin üstten uzaklığıdır, hiç yoksa NaN'dir. Farklı boyutlardaki metinleri hizalamak için kullanın. `alignByBaseline()` ve `paddingFromBaseline(top, bottom)` modifier'ları sık karşılaşılan durumları özel bir yerleşim yazmadan karşılar.

## Layout modifier'ı

Tek bir composable'ın nasıl ölçüldüğünü ya da yerleştirildiğini değiştirmek için onu bir `Layout`'a sarmak yerine `Modifier().layout` kullanın. İçeriği `measure` üzerinden ölçer, düğümün boyutunu döndürür ve `placeAt`'e içeriğin düğüm içinde nereye gideceğini yazar:

```cpp
// En az `min` dp yüksekliğinde, içerik bunun içinde dikey olarak ortalanmış.
Modifier atLeastHeight(float min) {
    return Modifier().layout([min](const MeasureNext& measure, const Constraints& c, Offset& placeAt) {
        Size content = measure(c);
        float height = std::max(content.height, std::min(min, c.maxHeight));
        placeAt = {0, (height - content.height) / 2};
        return Size{content.width, height};
    });
}
```

## Intrinsic boyutlar

Yerleşim her çocuğu bir kez ölçer, bu yüzden bir ebeveyn bir çocuğun hangi boyutta olmak isteyeceğini öğrenmek için onu iki kez ölçemez. *Intrinsic* boyutlar bu soruyu gerçekten ölçmeden cevaplar: bu içerik doğal boyutunda ne kadar uzun olurdu?

Tipik kullanımı kardeşleri en uzunları kadar uzun yapmaktır:

```cpp body
Row(Modifier().height(IntrinsicSize::Min), [&] {
    Text("A short label", Modifier().weight(1));
    VerticalDivider(Modifier().fillMaxHeight());   // uzun olan metin kadar uzun
    Text("A label that wraps onto more than one line", Modifier().weight(1));
});
```

`height(IntrinsicSize::Min)` ve `width(IntrinsicSize::Min)` (ya da `Max`) düğümü içeriğinin intrinsic boyutuna göre boyutlandırır; onu dolduran çocuklar da bu boyutu doldurur.

> **Not:** Kompoz'un intrinsic'leri Compose'unkilerin bir yaklaşığıdır: hem `Min` hem `Max` içeriği o eksende sınırsız olarak ölçer ve bir `Layout` intrinsic boyutlarının nasıl hesaplanacağını değiştiremez. En küçük intrinsic genişlikte metin her boşlukta satır kırar, bu yüzden en geniş kelimesi kadar geniş çıkar.

## SubcomposeLayout

Bazen neyin kompoze edileceği bir ölçüme bağlıdır: gövdeye göre boyutlanan bir başlık, dar kısıtlar için farklı bir yerleşim, süslediği metne göre boyutlanan bir rozet. `SubcomposeLayout` içeriğinin parçalarını ölçüm *sırasında* kompoze eder:

```cpp
// İçerik ve altında, içeriğin çıktığı genişlikte bir çizgi.
void Underlined(const Content& content) {
    SubcomposeLayout(Modifier(), [content](SubcomposeMeasureScope& s, const Constraints& c) {
        float width = 0, height = 0;
        for (const Measurable& m : s.subcompose(std::string("main"), content)) {
            Placeable p = m.measure(c.loosen());
            p.place(0, height);
            width = std::max(width, p.width);
            height += p.height;
        }
        // Eşleşmesi gereken genişlikle kompoze edilir; o değişince yeniden kompoze edilir.
        for (const Measurable& m : s.subcompose(std::string("line"), width, [width] {
                 Box(Modifier().size(width, 2).background(colorScheme().primary));
             })) {
            Placeable line = m.measure(c.loosen());
            line.place(0, height);
            height += line.height;
        }
        return c.constrain(Size{width, height});
    });
}
```

- `subcompose(slotId, content)` bir slot'u kompoze eder ve measurable'larını döndürür. Slot ilk istendiğinde kompoze edilir ve yerleşim aynı karede yeniden çalışır.
- `subcompose(slotId, dependency, content)`, `dependency` değişince slot'u yeniden kompoze eder.
- İçerik daha sonra, kompozisyon sırasında çalışır, bu yüzden değerle yakalayın.

`SubcomposeLayout`, `Layout`'tan daha pahalıdır; yalnızca kompozisyon gerçekten ölçüme bağlıysa kullanın. `BoxWithConstraints` onun en yaygın kullanımının hazır hâlidir.

## Yerleşim değişikliklerini canlandırmak

`Modifier().animateContentSize()`, içeriği büyüyüp küçüldüğünde bir düğümün boyutunu canlandırır.

Çevresindeki yerleşim değiştiği için yer değiştiren ya da yeniden boyutlanan bir düğüm için `Modifier().animateBounds()`, Compose'un `animateBounds`'u gibi onu çizildiği yerden şimdiki yerine süzülerek taşır. Konumu en yakın `LookaheadScope`'a göre ölçülür:

```cpp
void Chips(bool wide) {
    LookaheadScope(Modifier().fillMaxWidth(), [&] {
        FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .maxItemsInEachLine = wide ? 4 : 2}, [&] {
            for (const char* name : {"One", "Two", "Three", "Four"})
                AssistChip([] {}, [&] { Text(name); }, {}, Modifier().animateBounds());
        });
    });
}
```

Yalnızca çizim hareket eder; dokunmalar düğümün yerleştiği yere düşer. `skipToLookaheadSize()` içeriği aradaki boyutlarda değil, varacağı boyutta yerleştirir; `approachLayout` ise aradaki adımları kendiniz yazmanızı sağlar.

## Ayrıca bakın

- [Yerleşimler](layouts.md): yerleşik yerleşimler.
- [Aşamalar](phases.md): yerleşim sırasında durum okumanın maliyeti.
- [Modifier'lar](modifiers.md): her yerleşim düğümünü saran modifier zinciri.
