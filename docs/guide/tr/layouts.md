# Yerleşimler

Kompoz'un yerleşimleri çocukları sütunlara, satırlara, üst üste yığınlara, kaydırılarak alt satıra geçen dizilere ve ızgaralara yerleştirir ve pencerenin boyutuna uyum sağlar. Bu sayfa standart yerleşimleri anlatıyor; kendinizinkini nasıl yazacağınızı [Özel yerleşimler](custom-layouts.md) gösteriyor.

## Yerleşim nasıl çalışır

Yerleşim, ağaçta bir kez aşağı inip yukarı çıkan tek bir geçişte olur:

1. Ebeveyn her çocuğa *kısıtlar* verir: en küçük ve en büyük genişlik ve yükseklik.
2. Her çocuk, önce kendi çocuklarını ölçerek bu kısıtlar içinde kendini ölçer ve boyutunu bildirir.
3. Ebeveyn çocuklarını yerleştirir ve kendi boyutunu bildirir.

Her düğüm bir geçişte bir kez ölçülür, bu yüzden ağaç ne kadar derin olursa olsun yerleşim hızlı kalır. Tüm boyutlar dp cinsindendir ve Compose'daki gibi tam piksellere yerleşir: boyutlar, padding, aralıklar ve konumlar Compose'un yuvarladığı yerlerde en yakın piksele (yarım yukarı) yuvarlanır, bir metin de onu çevreleyen tam pikseller kadar büyüktür.

## Column, Row ve Box

Üç temel yerleşim:

- `Column` çocuklarını alt alta dizer.
- `Row` onları yan yana, baştan başlayarak dizer (soldan, sağdan sola bir yerleşimde sağdan).
- `Box` onları üst üste yığar.

```cpp
void ProfileHeader(const std::string& name, const std::string& handle) {
    Row(Modifier().padding(16), {.verticalAlignment = Alignment::CenterVertically}, [&] {
        Box([&] {
            Icon(Icons::Filled::AccountCircle, "", Modifier().size(56));
            Box(Modifier().size(14).align(Alignment::BottomEnd).background(Color::hex(0x2E7D32), CircleShape));
        });
        Spacer(Modifier().width(12));
        Column([&] {
            Text(name, {.font = typography().titleMedium});
            Text(handle, {.font = typography().bodyMedium, .color = colorScheme().onSurfaceVariant});
        });
    });
}
```

### Dizilim ve hizalama

Bir `Column`'un stili çocukları yüksekliği boyunca dizer ve genişliği boyunca hizalar; bir `Row`'unki bunun tersini yapar:

```cpp body
Column(Modifier().fillMaxSize(), {.verticalArrangement = Arrangement::SpaceBetween,
                                  .horizontalAlignment = Alignment::CenterHorizontally}, [&] {
    Text("Top");
    Text("Middle");
    Text("Bottom");
});
```

| Dizilim | Çocuklar |
|---|---|
| `Arrangement::Start`, `Top` | Başa toplanır. |
| `Arrangement::End`, `Bottom` | Sona toplanır. |
| `Arrangement::Center` | Ortada toplanır. |
| `Arrangement::SpaceBetween` | Uçlarda boşluk bırakmadan yayılır. |
| `Arrangement::SpaceAround` | Uçlarda yarım boşlukla yayılır. |
| `Arrangement::SpaceEvenly` | Her yerde eşit boşlukla yayılır. |
| `Arrangement::spacedBy(8)` | Aralarında 8 dp ile toplanır; `spacedBy(8, Arrangement::Kind::Center)` onları ortada toplar. |

Tek bir çocuk kendi hizasını değiştirebilir: bir `Column`'da `Modifier().align(Alignment::End)`, bir `Row`'da `Modifier().align(Alignment::CenterVertically)`, bir `Box`'ta `Modifier().align(Alignment::BottomEnd)`. Bir `Box` tüm çocuklarını `contentAlignment` argümanıyla hizalar: `Box(modifier, Alignment::Center, content)`.

### Ağırlık

`weight`, bir `Row`'un ya da `Column`'un ağırlıksız çocuklar ölçüldükten sonra kalan alanını ağırlıklı çocuklar arasında orantılı olarak paylaştırır:

```cpp body
Row(Modifier().fillMaxWidth().height(48), [&] {
    Box(Modifier().weight(2).fillMaxHeight().background(colorScheme().primary));    // üçte iki
    Box(Modifier().weight(1).fillMaxHeight().background(colorScheme().secondary));  // üçte bir
});
```

`weight(w, false)` çocuğun kendi payından küçük olmasına izin verir.

### Spacer

`Spacer(Modifier().width(8))` boş bir alandır; `Spacer(Modifier().weight(1))` kendinden sonra geleni sona iter.

## Alt satıra geçen diziler

`FlowRow` çocukları bir satıra dizer ve satır dolunca yeni bir satıra geçer, chip'lerin ya da etiketlerin istediği gibi; `FlowColumn` aynısını sütunlarla yapar.

```cpp body
#! std::vector<std::string> tags{"C++", "UI", "Compose"};
FlowRow(Modifier().fillMaxWidth(), {.horizontalSpacing = 8, .verticalSpacing = 8}, [&] {
    for (const std::string& tag : tags) AssistChip([] {}, [&] { Text(tag); });
});
```

`maxItemsInEachLine` bir satıra kaç öğe gireceğini sınırlar.

## FlexBox

`FlexBox`, Compose 1.12'deki gibi CSS'in flexbox'ıdır: `Row`'u, `Column`'u ve flow yerleşimlerini kapsayan bir üst küme. Çocuklar ana eksen boyunca dizilir (`direction`), alt satıra geçer ya da geçmez (`wrap`), bu eksen boyunca yayılır (`justifyContent`) ve kendi satırlarının enine hizalanır (`alignItems`). Her çocuk payını `Modifier().flex({.grow, .shrink, .basis})` ile alır:

```cpp body
FlexBox(Modifier().fillMaxWidth(), {.wrap = FlexWrap::Wrap, .justifyContent = FlexJustifyContent::SpaceBetween,
                                    .alignItems = FlexAlignItems::Center, .rowGap = 8, .columnGap = 8}, [&] {
    OutlinedTextField("", [](std::string) {}, Modifier().flex({.grow = 1}), {.label = "Search"});
    Button([] {}, [] { Text("Go"); });
});
```

## Grid

`Grid`, CSS grid ve Compose 1.12'nin `Grid`'i gibi çocukları satırlara ve sütunlara yerleştirir. Sütun ve satır izlerini siz verirsiniz; her çocuk bir sonraki boş hücreye ya da `Modifier().gridItem(row, column, rowSpan, columnSpan)`'in koyduğu yere gider:

```cpp body
Grid(Modifier().fillMaxWidth(), {.columns = {GridTrackSize::Fixed(96), GridTrackSize::Flex(1)}, .rowGap = 8, .columnGap = 16}, [&] {
    Text("Name");
    Text("Ada Lovelace");
    Text("Born");
    Text("10 December 1815");
    Text("Known for", Modifier().gridItem(3, 1));
    Text("The first published algorithm", Modifier().gridItem(3, 2));
});
```

İz boyutları: `Fixed(dp)`, `Percentage(fraction)`, `Flex(weight)` (kalan alandan bir pay), `MinMax(minDp, weight)`, `MinContent()`, `MaxContent()` ve `Auto()`. Yapılandırmadaki isimli alanlar sayesinde çocuklar `Modifier().gridItem("header")` diyebilir.

## Kaydırma

Bir yerleşim kendiliğinden kaymaz. `verticalScroll` ve `horizontalScroll`, içeriği elindeki alandan büyük olduğunda bir `Column`'u ya da `Row`'u kaydırılabilir yapar:

```cpp body
Column(Modifier().fillMaxSize().verticalScroll(rememberScrollState()), [&] {
    for (int i = 0; i < 50; ++i) Text("Line " + std::to_string(i));
});
```

Bu, her çocuğu kompoze eder. Uzun ya da sonu olmayan içerik için onun yerine tembel bir liste kullanın: bkz. [Listeler ve ızgaralar](lists.md).

## Pencereye uyum sağlamak

### Boyut sınıfları

Kesin boyutlara göre değil, pencerenin boyut sınıfına göre yerleşim yapın. `currentWindowAdaptiveInfo()`, Compose'un adaptive kütüphanesinin yaptığı gibi pencerenin genişlik ve yükseklik sınıflarını verir: Compact (600 dp'den dar), Medium (840 dp'den dar) ya da Expanded.

```cpp
#! void ListPane() {}
#! void DetailPane() {}
void MailScreen() {
    WindowAdaptiveInfo info = currentWindowAdaptiveInfo();
    if (info.windowSizeClass.windowWidthSizeClass == WindowWidthSizeClass::Expanded) {
        Row(Modifier().fillMaxSize(), [&] {
            Box(Modifier().weight(1), [] { ListPane(); });
            Box(Modifier().weight(2), [] { DetailPane(); });
        });
    } else {
        ListPane();
    }
}
```

Pencere sınıf değiştirince yeniden kompoze eder. `NavigationSuiteScaffold` bunu bir gezinme çubuğuyla bir ray arasında geçiş yapmak için kullanır (bkz. [Material bileşenleri](material.md)).

### Media query'ler

`mediaQuery` uygulamanın üzerinde çalıştığı ortamı sorar: pencerenin boyutunu, en hassas işaretçiyi, klavye olup olmadığını. `derivedMediaQuery` yalnızca cevap değişince değişen bir durum verir; pencerenin boyutu için istediğiniz de budur, çünkü o, yeniden boyutlandırmanın her adımında değişir:

```cpp
#! void TwoPanes() {}
#! void OnePane() {}
void Adaptive() {
    auto wide = derivedMediaQuery([](const UiMediaScope& m) { return m.windowWidth() >= 600; });
    if (wide.get()) TwoPanes(); else OnePane();

    if (mediaQuery([](const UiMediaScope& m) { return m.pointerPrecision() == PointerPrecision::Fine; }))
        Text("Hover for details");
}
```

### BoxWithConstraints

`BoxWithConstraints`, içeriğine kutunun yerleştirildiği kısıtları verir; böylece içerik yerleşimini pencereninkine göre değil, gerçekten elindeki alana göre seçebilir:

```cpp body
BoxWithConstraints(Modifier().fillMaxWidth(), [&](const Constraints& c) {
    if (c.maxWidth > 400) Text("A wide layout");
    else Text("A narrow layout");
});
```

En ilk kompozisyon sınırsız kısıtlar görür; kısıtlar belli olunca içerik aynı karede yeniden kompoze edilir.

## Pencere kenar boşlukları

Telefonlarda sistem çubukları, ekran çentiği ve klavye pencerenin bazı kısımlarını örter. *Kenar boşlukları* (insets) bunun ne kadar olduğunu söyler, böylece içerik onlardan uzak durabilir:

- `Scaffold` içeriğine, çubuklardan ve sistem kenar boşluklarından zaten uzak tutan bir iç boşluk verir.
- `Modifier().safeDrawingPadding()`, `systemBarsPadding()`, `statusBarsPadding()`, `navigationBarsPadding()` ve `imePadding()` tek bir tür kenar boşluğu kadar iç boşluk ekler.
- `windowInsetsPadding(WindowInsets::systemBars())` herhangi bir `WindowInsets` alır.

Insets iç boşlukları toplanmaz: içteki, yalnızca dıştakilerin henüz almadığı kadar boşluk ekler. Metin alanı olan bir `Scaffold`'da içeriğe Compose'daki gibi iç boşluk verin:

```cpp body
Scaffold(Modifier(), {}, [&](PaddingValues padding) {
    Column(Modifier().padding(padding).consumeWindowInsets(padding).imePadding(), [&] {
        OutlinedTextField("", [](std::string) {});
    });
});
```

## Sağdan sola

Sistem dili sağdan sola yazılıyorsa (Arapça, İbranice, Farsça…) yerleşim yönü sağdan soladır: `Row`'lar sağdan başlar, `start` ve `end` iç boşlukları yer değiştirir ve yatay listeler sağdan kayar. `CompositionLocalProvider({LocalLayoutDirection.provides(LayoutDirection::Rtl)}, …)` bunu ağacın bir kısmı için, `KOMPOZ_LAYOUT_DIRECTION=rtl` ise bütün uygulama için değiştirir. Çizim, `absoluteOffset` ve `absolutePadding` aynalanmaz.

## Ayrıca bakın

- [Modifier'lar](modifiers.md): boyut, iç boşluk ve scope modifier'ları.
- [Özel yerleşimler](custom-layouts.md): `Layout`, intrinsic'ler ve `SubcomposeLayout`.
- [Listeler ve ızgaralar](lists.md): uzun içerik için tembel yerleşimler.
