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

```kotlin
@Composable
fun ProfileHeader(name: String, handle: String) {
    Row(modifier = Modifier.padding(16.dp), verticalAlignment = Alignment.CenterVertically) {
        Box {
            Icon(Icons.Filled.AccountCircle, contentDescription = null, modifier = Modifier.size(56.dp))
            Box(modifier = Modifier.size(14.dp).align(Alignment.BottomEnd).background(Color(0xFF2E7D32), CircleShape))
        }
        Spacer(Modifier.width(12.dp))
        Column {
            Text(name, style = MaterialTheme.typography.titleMedium)
            Text(handle, color = MaterialTheme.colorScheme.onSurfaceVariant, style = MaterialTheme.typography.bodyMedium)
        }
    }
}
```

### Dizilim ve hizalama

Bir `Column`'un stili çocukları yüksekliği boyunca dizer ve genişliği boyunca hizalar; bir `Row`'unki bunun tersini yapar:

```kotlin body
Column(
    modifier = Modifier.fillMaxSize(),
    verticalArrangement = Arrangement.SpaceBetween,
    horizontalAlignment = Alignment.CenterHorizontally,
) {
    Text("Top")
    Text("Middle")
    Text("Bottom")
}
```

| Dizilim | Çocuklar |
|---|---|
| `Arrangement.Start`, `Top` | Başa toplanır. |
| `Arrangement.End`, `Bottom` | Sona toplanır. |
| `Arrangement.Center` | Ortada toplanır. |
| `Arrangement.SpaceBetween` | Uçlarda boşluk bırakmadan yayılır. |
| `Arrangement.SpaceAround` | Uçlarda yarım boşlukla yayılır. |
| `Arrangement.SpaceEvenly` | Her yerde eşit boşlukla yayılır. |
| `Arrangement.spacedBy(8.dp)` | Aralarında 8 dp ile toplanır; `spacedBy(8.dp, Arrangement.Kind.Center)` onları ortada toplar. |

Tek bir çocuk kendi hizasını değiştirebilir: bir `Column`'da `Modifier.align(Alignment.End)`, bir `Row`'da `Modifier.align(Alignment.CenterVertically)`, bir `Box`'ta `Modifier.align(Alignment.BottomEnd)`. Bir `Box` tüm çocuklarını `contentAlignment`'ıyla hizalar: `Box(contentAlignment = Alignment.Center) { … }`.

### Ağırlık

`weight`, bir `Row`'un ya da `Column`'un ağırlıksız çocuklar ölçüldükten sonra kalan alanını ağırlıklı çocuklar arasında orantılı olarak paylaştırır:

```kotlin body
Row(modifier = Modifier.fillMaxWidth().height(48.dp)) {
    Box(modifier = Modifier.weight(2f).fillMaxHeight().background(MaterialTheme.colorScheme.primary))    // üçte iki
    Box(modifier = Modifier.weight(1f).fillMaxHeight().background(MaterialTheme.colorScheme.secondary))  // üçte bir
}
```

`weight(1f, false)` (`fill` false) çocuğun kendi payından küçük olmasına izin verir.

### Spacer

`Spacer(Modifier.width(8.dp))` boş bir alandır; `Spacer(Modifier.weight(1f))` kendinden sonra geleni sona iter.

## Alt satıra geçen diziler

`FlowRow` çocukları bir satıra dizer ve satır dolunca yeni bir satıra geçer, chip'lerin ya da etiketlerin istediği gibi; `FlowColumn` aynısını sütunlarla yapar. Düzenlemelerinin (arrangement) aralığı öğeler arasına ve satırlar arasına girer.

```kotlin body
#! val tags = listOf("C++", "UI", "Compose")
FlowRow(
    modifier = Modifier.fillMaxWidth(),
    horizontalArrangement = Arrangement.spacedBy(8.dp),
    verticalArrangement = Arrangement.spacedBy(8.dp),
) {
    for (tag in tags) {
        Box(modifier = Modifier.background(MaterialTheme.colorScheme.secondaryContainer, RoundedCornerShape(8.dp)).padding(horizontal = 12.dp, vertical = 6.dp)) {
            Text(tag)
        }
    }
}
```

`maxItemsInEachLine` bir satıra kaç öğe gireceğini sınırlar.

## FlexBox

`FlexBox`, Compose 1.12'deki gibi CSS'in flexbox'ıdır: `Row`'u, `Column`'u ve flow yerleşimlerini kapsayan bir üst küme. Nasıl dizileceğini `config`'i söyler: çocuklar ana eksen boyunca dizilir (`direction`), alt satıra geçer ya da geçmez (`wrap`), bu eksen boyunca yayılır (`justifyContent`) ve kendi satırlarının enine hizalanır (`alignItems`). Her çocuk payını `Modifier.flex { grow(1f) }` ile (ve `shrink`, `basis`, `alignSelf`, `order` ile) alır:

```kotlin body
var query by remember { mutableStateOf("") }
FlexBox(
    modifier = Modifier.fillMaxWidth(),
    config = {
        wrap(FlexWrap.Wrap)
        justifyContent(FlexJustifyContent.SpaceBetween)
        alignItems(FlexAlignItems.Center)
        gap(8.dp)
    },
) {
    OutlinedTextField(value = query, onValueChange = { query = it }, modifier = Modifier.flex { grow(1f) })
    Button(onClick = { }) { Text("Go") }
}
```

Yapılandırma kutu yerleştirilirken, kutunun `constraints`'iyle çalışır: `direction(if (constraints.maxWidth < 400.dp.roundToPx()) FlexDirection.Column else FlexDirection.Row)` yönü eldeki alana göre seçer; içinde okunan bir durum kutuyu yeniden kompoze etmeden yeniden yerleştirir. `basis(80.dp)` bir çocuğun başlangıç boyutunu verir; kutudan bir pay `basis(FlexBasis.Percent(0.5f))`'tir (Compose'un `basis(0.5f)`'i: burada dp bir float'tır).

## Grid

`Grid`, CSS grid ve Compose 1.12'nin `Grid`'i gibi çocukları satırlara ve sütunlara yerleştirir. Sütun ve satır izlerini `config`'i verir; her çocuk bir sonraki boş hücreye ya da `Modifier.gridItem(row = …, column = …, rowSpan = …, columnSpan = …)`'in koyduğu yere gider:

```kotlin body
Grid(
    modifier = Modifier.fillMaxWidth(),
    config = {
        column(96.dp)
        column(1.fr)
        gap(row = 8.dp, column = 16.dp)
    },
) {
    Text("Name")
    Text("Ada Lovelace")
    Text("Born")
    Text("10 December 1815")
    Text("Known for", modifier = Modifier.gridItem(row = 3, column = 1))
    Text("The first published algorithm", modifier = Modifier.gridItem(row = 3, column = 2))
}
```

İzler: `column(96.dp)` sabittir, `column(1.fr)` kalan alandan bir pay, `column(minmax(0.dp, 1.fr))` bir en azdan başlayan bir pay; `GridTrackSize.Percentage(0.5f)`, `GridTrackSize.MinContent`, `MaxContent` ve `Auto` da `column(…)` ve `row(…)` içine girer. `area("header", row = 1, columnSpan = 2)` hücrelere bir ad verir, bir çocuk onları `Modifier.gridItem("header")` ile alır. Bir `FlexBox`'ınki gibi yapılandırma ızgara yerleştirilirken çalışır ve onun `constraints`'ini görür.

## Kaydırma

Bir yerleşim kendiliğinden kaymaz. `verticalScroll` ve `horizontalScroll`, içeriği elindeki alandan büyük olduğunda bir `Column`'u ya da `Row`'u kaydırılabilir yapar:

```kotlin body
Column(modifier = Modifier.fillMaxSize().verticalScroll(rememberScrollState())) {
    for (i in 0 until 50) Text("Line $i")
}
```

Bu, her çocuğu kompoze eder. Uzun ya da sonu olmayan içerik için onun yerine tembel bir liste kullanın: bkz. [Listeler ve ızgaralar](lists.md).

## Pencereye uyum sağlamak

### Boyut sınıfları

Kesin boyutlara göre değil, pencerenin boyut sınıfına göre yerleşim yapın. `currentWindowAdaptiveInfo()`, Compose'un adaptive kütüphanesinin yaptığı gibi pencerenin genişlik ve yükseklik sınıflarını verir: Compact (600 dp'den dar), Medium (840 dp'den dar) ya da Expanded.

```kotlin
#! @Composable fun ListPane() {}
#! @Composable fun DetailPane() {}
@Composable
fun MailScreen() {
    val info = currentWindowAdaptiveInfo()
    if (info.windowSizeClass.windowWidthSizeClass == WindowWidthSizeClass.Expanded) {
        Row(modifier = Modifier.fillMaxSize()) {
            Box(modifier = Modifier.weight(1f)) { ListPane() }
            Box(modifier = Modifier.weight(2f)) { DetailPane() }
        }
    } else {
        ListPane()
    }
}
```

Pencere sınıf değiştirince yeniden kompoze eder. `NavigationSuiteScaffold` bunu bir gezinme çubuğuyla bir ray arasında geçiş yapmak için kullanır (bkz. [Material bileşenleri](material.md)).

### Media query'ler

`mediaQuery { … }`, Compose'unki gibi, uygulamanın üzerinde çalıştığı ortamı sorar: pencerenin boyutunu, en hassas işaretçiyi, klavye olup olmadığını. `derivedMediaQuery { … }` yalnızca cevap değişince değişen bir durum verir; pencerenin boyutu için istediğiniz de budur, çünkü o, yeniden boyutlandırmanın her adımında değişir:

```kotlin
#! @Composable fun TwoPanes() {}
#! @Composable fun OnePane() {}
@Composable
fun Adaptive() {
    val wide by derivedMediaQuery { windowWidth >= 600.dp }
    if (wide) TwoPanes() else OnePane()

    if (mediaQuery { pointerPrecision == UiMediaScope.PointerPrecision.Fine }) {
        Text("Hover for details")
    }
}
```

### BoxWithConstraints

`BoxWithConstraints`, içeriğine kutunun yerleştirildiği kısıtları verir (`maxWidth`, `minHeight` ve ötekiler, bir de `constraints`); böylece içerik yerleşimini pencereninkine göre değil, gerçekten elindeki alana göre seçebilir:

```kotlin body
BoxWithConstraints(modifier = Modifier.fillMaxWidth()) {
    if (maxWidth > 400.dp) Text("A wide layout")
    else Text("A narrow layout")
}
```

Compose'unki gibi bir `SubcomposeLayout`'tur: içeriği kutu ölçülürken, aldığı kısıtlarla (ilk seferde de) kompoze edilir; kısıtlar değişince yeniden kompoze edilir.

### Uyarlanır bölmeler

`ListDetailPaneScaffold` ve `SupportingPaneScaffold`, Compose Material 3 Adaptive'inkilerdir: dar bir pencerede bir seferde bir bölme (geri tuşu geri döner), geniş bir pencerede yan yana. Navigator'ları hangi bölmenin ve neyin gösterildiğini söyler: `navigateTo(role, key)`, `navigateBack()`. Bir `paneExpansionState` ile `paneExpansionDragHandle = { state -> VerticalDragHandle(modifier = Modifier.paneExpansionDraggable(state)) }` ayrımın sürüklenmesini sağlar.

```kotlin
@Composable
fun Mail() {
    val navigator = rememberListDetailPaneScaffoldNavigator<Int>()
    val mails = listOf("Hoş geldiniz", "Fatura", "Bülten")
    ListDetailPaneScaffold(
        navigator = navigator,
        listPane = {
            Column {
                for (i in 0 until mails.size)
                    Text(mails[i], modifier = Modifier.clickable { navigator.navigateTo(ListDetailPaneScaffoldRole.Detail, i) }.padding(16.dp))
            }
        },
        detailPane = { mail -> Text(if (mail == null) "Bir posta seçin" else mails[mail], modifier = Modifier.padding(16.dp)) },
    )
}
```

## Pencere kenar boşlukları

Telefonlarda sistem çubukları, ekran çentiği ve klavye pencerenin bazı kısımlarını örter. *Kenar boşlukları* (insets) bunun ne kadar olduğunu söyler, böylece içerik onlardan uzak durabilir:

- `Scaffold` içeriğine, çubuklardan ve sistem kenar boşluklarından zaten uzak tutan bir iç boşluk verir.
- `Modifier.safeDrawingPadding()`, `systemBarsPadding()`, `statusBarsPadding()`, `navigationBarsPadding()` ve `imePadding()` tek bir tür kenar boşluğu kadar iç boşluk ekler.
- `windowInsetsPadding(WindowInsets.systemBars)` herhangi bir `WindowInsets` alır.

Insets iç boşlukları toplanmaz: içteki, yalnızca dıştakilerin henüz almadığı kadar boşluk ekler. Metin alanı olan bir `Scaffold`'da içeriğe Compose'daki gibi iç boşluk verin:

```kotlin body
var text by remember { mutableStateOf("") }
Scaffold { padding ->
    Column(modifier = Modifier.padding(padding).consumeWindowInsets(padding).imePadding()) {
        OutlinedTextField(value = text, onValueChange = { text = it })
    }
}
```

## Sağdan sola

Sistem dili sağdan sola yazılıyorsa (Arapça, İbranice, Farsça…) yerleşim yönü sağdan soladır: `Row`'lar sağdan başlar, `start` ve `end` iç boşlukları yer değiştirir ve yatay listeler sağdan kayar. `CompositionLocalProvider(LocalLayoutDirection provides LayoutDirection.Rtl) { … }` bunu ağacın bir kısmı için, `KOMPOZ_LAYOUT_DIRECTION=rtl` ise bütün uygulama için değiştirir. Çizim, `absoluteOffset` ve `absolutePadding` aynalanmaz.

## Ayrıca bakın

- [Modifier'lar](modifiers.md): boyut, iç boşluk ve scope modifier'ları.
- [Özel yerleşimler](custom-layouts.md): `Layout`, intrinsic'ler ve `SubcomposeLayout`.
- [Listeler ve ızgaralar](lists.md): uzun içerik için tembel yerleşimler.
