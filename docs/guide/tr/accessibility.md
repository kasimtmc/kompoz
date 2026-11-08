# Erişilebilirlik

Uygulamanızı ekran okuyucularla, klavyeyle ve büyük metinle kullanılabilir kılmak. Kompoz, Compose'un yaptığı gibi bir semantik ağaç kurar; Android'de TalkBack, Linux'ta AT-SPI, Windows'ta UI Automation üzerinden ekran okuyucular onu okur, testler de her platformda onu sorgular.

## Semantik ağaç

Kompoz, arayüz ağacının yanında bir *semantik* ağaç da tutar: her öğenin onu göremeyen biri için ne anlama geldiği. Material bileşenleri onu sizin yerinize doldurur: bir `Button` etiketiyle birlikte bir butondur, bir `Checkbox` işaretli olup olmadığını, bir `Slider` değerini söyler. Android'de TalkBack bu ağacı okur; [testler](testing.md) içindeki düğümleri bulur.

Anlamın ekranda metin olarak bulunmadığı yerlerde ona ekleme yapmanız gerekir: ikonlar, resimler, özel kontroller.

## Resimleri ve ikonları tanımlamak

Anlam taşıyan her ikona ve resme bir içerik açıklaması verin, süs amaçlı olanları boş bırakın:

```kotlin body
IconButton(onClick = {}) { Icon(Icons.Filled.Share, contentDescription = "Share") }     // "Share, button" olarak seslendirilir
Icon(Icons.Filled.Star, contentDescription = null)                                       // süs: seslendirilmez
Image(imageResource("images/chart.png"), contentDescription = "Sales grew 20% in March", modifier = Modifier.size(200.dp))
```

Metin etiketi de olan bir butonun içindeki ikon onu tekrarlamamalıdır: ikona boş bir açıklama verin.

## Semantik modifier'ı

`Modifier.semantics { … }` bir düğümün ne anlama geldiğini belirler:

| Özellik | Kullanımı |
|---|---|
| `contentDescription` | Metni olmadığında ne olduğu. |
| `role` | `Role.Button`, `Checkbox`, `Switch`, `RadioButton`, `Tab`, `Image`, `Slider`. |
| `checked` | Açık ya da kapalı; anahtarlar ve seçili sekmeler için. |
| `stateDescription` | Durumunun sözcüklerle ifadesi: "Oynatılıyor", "5'te 3". |
| `heading` | Bir başlık; ekran okuyucu kullanıcıları bölümler arasında atlayabilsin diye. |
| `mergeDescendants` | İçindeki her şeyle birlikte tek bir öğe olarak okunur. |
| `liveRegion` | Değişiklikler seslendirilir: `LiveRegionMode.Polite` ya da `Assertive`. |
| `customActions` | TalkBack'in eylemler menüsünde sunulan eylemler. |
| `progressBarRangeInfo`, `setProgress` | Hizmetlerin okuyup atayabileceği, bir aralıktaki değer. |
| `traversalIndex` | Kardeşler arasındaki okuma sırası, küçük olan önce. |

```kotlin body
Text("Settings", modifier = Modifier.semantics { heading() }, style = MaterialTheme.typography.headlineSmall)
```

`Modifier.semantics { contentDescription = "…" }` yalnızca bir içerik açıklaması belirler.

## Birleştirme

Bir ekran okuyucu öğeden öğeye geçer. Bir ikon ve iki metinden oluşan bir satır üç değil, birlikte okunan tek bir öğe olmalıdır. Tıklanabilirler içeriklerini zaten birleştirir; diğer gruplar için `mergeDescendants`'ı ayarlayın:

```kotlin body
Row(modifier = Modifier.semantics(mergeDescendants = true) {}.padding(16.dp)) {
    Icon(Icons.Filled.Person, contentDescription = null)
    Column {
        Text("Ada Lovelace")
        Text("Online")
    }
}   // "Ada Lovelace, Online" olarak okunur
```

`Modifier.clearAndSetSemantics { … }` içindekileri sizin belirlediğinizle değiştirir: beş yıldız olarak çizilen bir puan "Puan: 5'te 4" olur. `Modifier.semantics { hideFromAccessibility() }` bir düğümü ve içindekileri gizler.

```kotlin body
Row(modifier = Modifier.clearAndSetSemantics { contentDescription = "Rating: 4 of 5" }) {
    for (i in 0 until 5) Icon(if (i < 4) Icons.Filled.Star else Icons.Filled.FavoriteBorder, contentDescription = null)
}
```

## Özel kontroller

Kendiniz çizdiğiniz bir kontrol ne olduğunu ve nasıl kullanılacağını söylemelidir. Bunu sizin yerinize yapan modifier'ları tercih edin: `clickable`, `toggleable(value, onChange, role)`, `selectable(selected, onClick, role)` ve `triStateToggleable` rolü, durumu ve eylemi ayarlar:

```kotlin
@Composable
fun StarToggle() {
    var starred by remember { mutableStateOf(false) }
    Icon(if (starred) Icons.Filled.Star else Icons.Filled.FavoriteBorder, contentDescription = "Starred",
         modifier = Modifier.toggleable(value = starred, onValueChange = { starred = it }, role = Role.Switch).padding(12.dp))
}
```

Tek bir kontrol gibi davranan bir satırda modifier'ı satıra koyun ve içindeki kontrole boş bir callback verin; böylece bütün satır tek bir dokunma hedefi ve tek bir öğe olur:

```kotlin
@Composable
fun WifiRow() {
    var on by remember { mutableStateOf(true) }
    Row(modifier = Modifier.fillMaxWidth().toggleable(value = on, onValueChange = { on = it }, role = Role.Switch).padding(16.dp),
        verticalAlignment = Alignment.CenterVertically) {
        Text("Wi-Fi", modifier = Modifier.weight(1f))
        Switch(checked = on, onCheckedChange = null)
    }
}
```

Bir grup radyo butonu satırı `Modifier.selectableGroup()` içine konur; böylece hizmetler bunların bir arada olduğunu bilir.

### Hareket gerektiren eylemler

Kaydırarak silme, uzun basma menüleri ve sürükleme tutamaçlarını bir ekran okuyucuyla kullanmak zordur. Aynı eylemleri özel eylemler olarak da sunun:

```kotlin body
#! fun archive() {}
#! fun deleteItem() {}
Box(modifier = Modifier.semantics {
    customActions = listOf(CustomAccessibilityAction("Archive") { archive(); true },
                           CustomAccessibilityAction("Delete") { deleteItem(); true })
})
```

### Bir aralıktaki değerler

Özel bir slider değerini bildirir ve hizmetlerin onu atamasına izin verir:

```kotlin body
#! val level = mutableStateOf(0.5f)
Box(modifier = Modifier.semantics {
    contentDescription = "Brightness"
    role = Role.Slider
    progressBarRangeInfo = ProgressBarRangeInfo(level.value, 0f, 1f)
    setProgress { level.value = it; true }
})
```

## Değişiklikleri seslendirmek

Odağı taşımadan beliren bir mesaj, örneğin "Kaydedildi" ya da bir alanın altındaki bir hata, seslendirilmelidir. Onu canlı bölge olarak işaretleyin:

```kotlin body
#! val saved = true
if (saved) Text("Saved", modifier = Modifier.semantics { liveRegion = LiveRegionMode.Polite })
```

## Dokunma hedefleri

Material bileşenleri, Compose'dakiler gibi (`LocalMinimumInteractiveComponentSize`), daha küçük görünseler bile en az 48 dp'lik bir dokunma hedefi korur. Kendi küçük kontrolleriniz için `Modifier.minimumInteractiveComponentSize()` ekleyin.

## Metin boyutu ve kontrast

Metin boyutları sp cinsindendir ve sistemin font boyutunu izler; uygulama çalışırken değiştiğinde de. Yerleşimlerin onunla birlikte büyümesine izin verin: metin içeren şeyler için sabit bir yükseklik yerine en az bir yükseklik (`heightIn(56)`) tercih edin. Material'ın renk şemaları her `on…` rengiyle onun kabı arasında yeterli kontrast sağlar; bu çiftlere bağlı kalın.

## Klavye

Her tıklanabilire ve metin alanına Tab ile ulaşılabilir, Enter ya da Space ile kullanılabilir; ok tuşları odaklanabilir öğeler arasında gezinir. Özel kontrolleri odaklanabilir tutun (`clickable` ve `toggleable` öyledir) ve odak sırasının okuma sırasını izlediğinden emin olun. Bkz. [Hareketler](gestures.md#odak). Linux'ta ve Windows'ta ekran okuyucular klavye odağını Tab ile ilerledikçe izler, kendileri de taşıyabilir.

## Platformlar

Android'de TalkBack semantik ağacı okur: etiketler, roller, durumlar, eylemler, özel eylemler ve canlı bölgeler.

Linux'ta Orca gibi ekran okuyucular onu AT-SPI üzerinden okur. Uygulama, masaüstünün erişilebilirliği açıksa erişilebilirlik veriyoluna katılır; Qt uygulamaları da böyle yapar (uygulama çalışırken açılırsa da katılır). `KOMPOZ_ATSPI=1` her durumda bağlar, `KOMPOZ_ATSPI=0` hiç bağlamaz. Ekran okuyucular aynı etiketleri, rolleri, durumları, değerleri ve metni alır; tıklayabilir, odaklayabilir, kaydırabilir, bir kaydırıcının değerini ya da bir metin alanının metnini ayarlayabilir, özel eylemleri çalıştırabilir; değişenler de onlara bildirilir: odak, işaretli ve seçili durumları, yazılan metin, değerler, duyuru olarak canlı bölgeler. Bir parola alanının metni nokta olarak okunur.

Windows'ta Ekran Okuyucusu (Narrator), NVDA ve diğer UI Automation istemcileri onu okur: pencere, istendiğinde düğümlerini onlara verir. Denetim türlerini, adları, durumları, konumları, değerleri (metin alanınınki, kaydırıcınınki) ve onları kullanmak için desenleri (çağırma, açıp kapama, seçme, değer ayarlama, kaydırma, odak) alırlar; odak, ad, durum ve değer değişimleri, gelip giden çocuklar ve canlı bölgeler onlara bildirilir. UI Automation'da özel eylemlere yer yoktur; onlar yalnızca Android'de ve Linux'ta vardır.

macOS'un NSAccessibility'si henüz bağlı değildir.

## Ayrıca bakın

- [Test](testing.md): düğümleri semantiklerine göre bulmak.
- [Material bileşenleri](material.md): kendiliğinden ne seslendirdikleri.
- [Hareketler](gestures.md): odak ve klavye girdisi.
