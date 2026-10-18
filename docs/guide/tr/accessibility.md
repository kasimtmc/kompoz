# Erişilebilirlik

Uygulamanızı ekran okuyucularla, klavyeyle ve büyük metinle kullanılabilir kılmak. Kompoz, Compose'un yaptığı gibi bir semantik ağaç kurar; Android'de TalkBack, Linux'ta AT-SPI, Windows'ta UI Automation üzerinden ekran okuyucular onu okur, testler de her platformda onu sorgular.

## Semantik ağaç

Kompoz, arayüz ağacının yanında bir *semantik* ağaç da tutar: her öğenin onu göremeyen biri için ne anlama geldiği. Material bileşenleri onu sizin yerinize doldurur: bir `Button` etiketiyle birlikte bir butondur, bir `Checkbox` işaretli olup olmadığını, bir `Slider` değerini söyler. Android'de TalkBack bu ağacı okur; [testler](testing.md) içindeki düğümleri bulur.

Anlamın ekranda metin olarak bulunmadığı yerlerde ona ekleme yapmanız gerekir: ikonlar, resimler, özel kontroller.

## Resimleri ve ikonları tanımlamak

Anlam taşıyan her ikona ve resme bir içerik açıklaması verin, süs amaçlı olanları boş bırakın:

```cpp body
IconButton([] {}, [] { Icon(Icons::Filled::Share, "Share"); });     // "Share, button" olarak seslendirilir
Icon(Icons::Filled::Star, "");                                      // süs: seslendirilmez
Image(imageResource("images/chart.png"), "Sales grew 20% in March", Modifier().size(200));
```

Metin etiketi de olan bir butonun içindeki ikon onu tekrarlamamalıdır: ikona boş bir açıklama verin.

## Semantik modifier'ı

`Modifier().semantics({…})` bir düğümün ne anlama geldiğini belirler:

| Özellik | Kullanımı |
|---|---|
| `contentDescription` | Metni olmadığında ne olduğu. |
| `role` | `SemanticsRole::Button`, `Checkbox`, `Switch`, `RadioButton`, `Tab`, `Image`, `Slider`. |
| `checked` | Açık ya da kapalı; anahtarlar ve seçili sekmeler için. |
| `stateDescription` | Durumunun sözcüklerle ifadesi: "Oynatılıyor", "5'te 3". |
| `heading` | Bir başlık; ekran okuyucu kullanıcıları bölümler arasında atlayabilsin diye. |
| `mergeDescendants` | İçindeki her şeyle birlikte tek bir öğe olarak okunur. |
| `liveRegion` | Değişiklikler seslendirilir: `LiveRegionMode::Polite` ya da `Assertive`. |
| `customActions` | TalkBack'in eylemler menüsünde sunulan eylemler. |
| `progressBarRangeInfo`, `setProgress` | Hizmetlerin okuyup atayabileceği, bir aralıktaki değer. |
| `traversalIndex` | Kardeşler arasındaki okuma sırası, küçük olan önce. |

```cpp body
Text("Settings", Modifier().semantics({.heading = true}), {.font = typography().headlineSmall});
```

`Modifier().semantics("description")`, yalnızca bir içerik açıklaması için kısa yoldur.

## Birleştirme

Bir ekran okuyucu öğeden öğeye geçer. Bir ikon ve iki metinden oluşan bir satır üç değil, birlikte okunan tek bir öğe olmalıdır. Tıklanabilirler içeriklerini zaten birleştirir; diğer gruplar için `mergeDescendants`'ı ayarlayın:

```cpp body
Row(Modifier().semantics({.mergeDescendants = true}).padding(16), [&] {
    Icon(Icons::Filled::Person, "");
    Column([&] {
        Text("Ada Lovelace");
        Text("Online");
    });
});   // "Ada Lovelace, Online" olarak okunur
```

`Modifier().clearAndSetSemantics({…})` içindekileri sizin belirlediğinizle değiştirir: beş yıldız olarak çizilen bir puan "Puan: 5'te 4" olur. `Modifier().hideFromAccessibility()` bir düğümü ve içindekileri gizler.

```cpp body
Row(Modifier().clearAndSetSemantics({.contentDescription = "Rating: 4 of 5"}), [&] {
    for (int i = 0; i < 5; ++i) Icon(i < 4 ? Icons::Filled::Star : Icons::Filled::FavoriteBorder, "");
});
```

## Özel kontroller

Kendiniz çizdiğiniz bir kontrol ne olduğunu ve nasıl kullanılacağını söylemelidir. Bunu sizin yerinize yapan modifier'ları tercih edin: `clickable`, `toggleable(value, onChange, role)`, `selectable(selected, onClick, role)` ve `triStateToggleable` rolü, durumu ve eylemi ayarlar:

```cpp
void StarToggle() {
    auto starred = remember([] { return mutableStateOf(false); });
    Icon(starred.get() ? Icons::Filled::Star : Icons::Filled::FavoriteBorder, "Starred",
         Modifier().toggleable(starred.get(), [starred](bool v) { starred.set(v); }, SemanticsRole::Switch).padding(12));
}
```

Tek bir kontrol gibi davranan bir satırda modifier'ı satıra koyun ve içindeki kontrole boş bir callback verin; böylece bütün satır tek bir dokunma hedefi ve tek bir öğe olur:

```cpp
void WifiRow() {
    auto on = remember([] { return mutableStateOf(true); });
    Row(Modifier().fillMaxWidth().toggleable(on.get(), [on](bool v) { on.set(v); }, SemanticsRole::Switch).padding(16),
        {.verticalAlignment = Alignment::CenterVertically}, [&] {
            Text("Wi-Fi", Modifier().weight(1));
            Switch(on.get(), {});
        });
}
```

Bir grup radyo butonu satırı `Modifier().selectableGroup()` içine konur; böylece hizmetler bunların bir arada olduğunu bilir.

### Hareket gerektiren eylemler

Kaydırarak silme, uzun basma menüleri ve sürükleme tutamaçlarını bir ekran okuyucuyla kullanmak zordur. Aynı eylemleri özel eylemler olarak da sunun:

```cpp body
#! std::function<void()> archive, deleteItem;
Box(Modifier().semantics({.customActions = {{"Archive", [archive] { archive(); return true; }},
                                            {"Delete", [deleteItem] { deleteItem(); return true; }}}}));
```

### Bir aralıktaki değerler

Özel bir slider değerini bildirir ve hizmetlerin onu atamasına izin verir:

```cpp body
#! auto level = mutableStateOf(0.5f);
Box(Modifier().semantics({.contentDescription = "Brightness", .role = SemanticsRole::Slider,
                          .progressBarRangeInfo = ProgressBarRangeInfo{level.get(), 0, 1},
                          .setProgress = [level](float v) { level.set(v); return true; }}));
```

## Değişiklikleri seslendirmek

Odağı taşımadan beliren bir mesaj, örneğin "Kaydedildi" ya da bir alanın altındaki bir hata, seslendirilmelidir. Onu canlı bölge olarak işaretleyin:

```cpp body
#! bool saved = true;
if (saved) Text("Saved", Modifier().semantics({.liveRegion = LiveRegionMode::Polite}));
```

## Dokunma hedefleri

Material bileşenleri, Compose'dakiler gibi (`LocalMinimumInteractiveComponentSize`), daha küçük görünseler bile en az 48 dp'lik bir dokunma hedefi korur. Kendi küçük kontrolleriniz için `Modifier().minimumInteractiveComponentSize()` ekleyin.

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
