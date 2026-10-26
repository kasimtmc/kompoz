# Aşamalar

Her kare üç aşamadan geçer: kompozisyon *neyin* gösterileceğine, yerleşim *nereye* konacağına, çizim *nasıl görüneceğine* karar verir. Bir durumu hangi aşamanın okuduğunu bilmek, bir değişikliğin ne kadar işe mal olacağını söyler.

## Üç aşama

1. **Kompozisyon** composable fonksiyonlarınızı çalıştırır ve onları, her biri kendi modifier'larıyla birlikte bir yerleşim düğümleri ağacına çevirir.
2. **Yerleşim** her düğümü ebeveyninin verdiği kısıtlarla ölçer ve çocuklarını yerleştirir. Bir düğüm her geçişte bir kez ölçülür.
3. **Çizim** her düğümü, modifier'larının arka planlarını, çerçevelerini ve özel çizimlerini sırayla çizer.

Ardından kare ekrana sunulur. Görünür hiçbir şeyin değişmediği bir kare ne çizilir ne sunulur.

Aşamalar normalde bu sırayla gider, ama her biri atlanabilir. Bir durum değişince Kompoz yalnız ona bağlı aşamaları yapar:

| Durumun okunduğu yer | Yeniden çalışan |
|---|---|
| Bir composable'ın gövdesinde | Onu okuyan scope'ların kompozisyonu, ardından değişen şeyin yerleşimi ve çizimi. |
| Ölçerken ya da yerleştirirken (`Modifier().offset([] { … })`, bir `Layout`'un ölçüm politikası) | Yerleşim ve çizim; kompozisyon yok. |
| Çizerken (`graphicsLayer([] (GraphicsLayerScope&) { … })`, `drawBehind`, bir `Canvas`) | Onu okuyan düğümlerin çizimi; bir yerleşim geçişi bakar ama hiçbir şey ölçmez. |

## Durum okumalarını ertelemek

Değişen bir değeri okuyan aşama ne kadar geçse, her değişiklik o kadar az işe mal olur. Bu, her karede değişen değerler için önemlidir: animasyonlar, kaydırma konumları, sürüklemeler.

Animasyonlu bir değeri kompozisyonda okumak her karede yeniden kompoze eder:

```cpp
void Pulse() {
    auto pulse = rememberInfiniteTransition().animateFloatState(0.8f, 1.f, infiniteRepeatable(tween(600), RepeatMode::Reverse));
    // çizerken okunur: her karede yalnız yeniden çizilir, hiç yeniden kompoze edilmez ya da ölçülmez
    Box(Modifier().size(48).graphicsLayer([pulse](GraphicsLayerScope& l) { l.scaleX = l.scaleY = pulse.get(); })
            .background(colorScheme().primary, CircleShape));
}
```

`graphicsLayer`'ın lambda biçimi çizerken değerlendirilir, bu yüzden ölçek kompozisyon ya da yerleşim olmadan değişir. Değer biçimi, `graphicsLayer({.alpha = 0.5f})`, kompozisyonda okunur; sık değişmeyen değerler için bu gayet iyidir.

Bir şeyi hareket ettirmek için de aynısı geçerli. `offset(x, y)` kompozisyonda okunan değerler alır; `offset([] { … })` yerleşim sırasında okunan bir lambda alır:

```cpp
void DraggableBall() {
    auto x = remember([] { return mutableStateOf(0.f); });
    Box(Modifier().offset([x] { return Offset{x.get(), 0}; })        // yerleşim sırasında okunur
            .draggable(rememberDraggableState([x](float delta) { x.set(x.get() + delta); }), Orientation::Horizontal)
            .size(56)
            .background(colorScheme().tertiary, CircleShape));
}
```

Topu sürüklemek her işaretçi hareketinde `x`'i değiştirir. Onu yalnız yerleşim aşaması okuduğu için hiçbir şey yeniden kompoze edilmez: top yeniden yerleştirilir ve çizilir.

## Artımlı yerleşim

Kompoz'da yerleşim, Compose'daki gibi artımlıdır. Geçen seferkiyle aynı kısıtlarla yeniden ölçülen ve kendisinde ya da altında hiçbir şey değişmemiş bir düğüm, çocuklarını ölçmeden boyutunu ve çocuklarının konumlarını korur. Yalnız yeniden kompoze edilen ya da ölçülürken okuduğu bir durum değişen düğümler, boyutu onlara bağlı ebeveynlerle birlikte yeniden ölçülür.

Bunun için bir şey yapmanız gerekmez. Ama şu anlama gelir: büyük bir ağacın derinliklerindeki bir değişiklik, etrafındakilerin boyutunu değiştirmediği sürece az şeye mal olur.

`KOMPOZ_LAYOUT_CHECK=1` her artımlı yerleşimi tam bir yerleşimle karşılaştırır ve her farkı kaydeder; testler her zaman onunla çalışır.

## Yalnız değişeni çizmek

Kompoz her kareyi bir çizim komutları listesi olarak kaydeder ve önceki kareyle karşılaştırır. Ekranın yalnız değişen bölümleri yeniden çizilir; piksellerin geri kalanı korunur. Kompoz'un GPU renderer'ında kaydırılan bir liste de yeniden çizilmez: pikselleri kaydırılır, yalnızca görünüme giren şerit ve onunla birlikte kaymayanlar (üstündeki yüzen bir düğme) çizilir. Bu, listenin altı tek renkken işler: kendi zemini ya da ekranınki; bir gradyanın ya da resmin üstünde liste kaydıkça yeniden çizilir. Bunu, yeniden çizileni çerçeveleyen `KOMPOZ_DAMAGE_SHOW=1` ile görebilirsiniz. Bkz. [Araçlar](tools.md#karelerin-profilini-çıkarmak).

Compose'daki gibi, çizdiği şeylerden hiçbiri değişmeyen bir düğümün çizimi de yeniden çalışmaz: kare, düğümün ve içindeki her şeyin geçen sefer çizdiğini, düğüm ne kadar kaydıysa o kadar kaydırarak (kaydırılan bir liste) yeniden kaydeder; böylece üstünde küçük bir animasyon olan büyük bir ekranı kaydetmek aşağı yukarı animasyon kadar tutar. Bir düğüm şu durumlarda yeniden çizilir: yeniden ölçüldüğünde, çiziminin okuduğu bir durum değiştiğinde, çizerken yeniden çizim istediğinde (karenin zamanıyla çizilenler: bir `Canvas`'ta okunan sonsuz bir geçişin durumu, bir ilerleme göstergesi) ve basıldığında, üzerine gelindiğinde ya da odaklandığında veya dalgası (ripple) ilerlediğinde. Bu yüzden çizim kodu değişen şeyi Compose'daki gibi bir durumdan alır: başka yerde değiştirilip çizerken okunan düz bir değer, düğümü başka bir şey yeniden çizene kadar görülmez. `KOMPOZ_KEEP_CHECK=1` her kareyi her düğümü çizen bir kareyle karşılaştırır; testler bunu hep yapar.

## Performans ipuçları

### Pahalı işi bir kez yapın

Bir composable'ın hesapladığı her şey her recomposition'da yeniden çalışır. Onu girdilerine bağlı bir anahtarla, `remember` ile bir kez hesaplayın:

```cpp
#! struct Contact { std::string name; friend bool operator==(const Contact&, const Contact&) = default; };
void Contacts(const std::vector<Contact>& contacts) {
    auto sorted = remember(contacts, [&] {   // yalnız kişiler değişince yeniden sıralanır
        std::vector<Contact> s = contacts;
        std::ranges::sort(s, {}, &Contact::name);
        return s;
    });
    LazyColumn(Modifier().fillMaxSize(), [&](LazyListScope& list) {
        list.items(sorted, [](const Contact& c) { Text(c.name); });
    });
}
```

Daha da iyisi, sıralı listeyi durumda ya da bir `ViewModel`'da tutun; böylece arayüz yeniden kompoze edildiğinde değil, liste değiştiğinde sıralanır.

### Tembel listelerde anahtar kullanın

Anahtar olmadan bir tembel liste öğe durumunu konumlara bağlar ve en üste eklemek her öğeyi yeniden kompoze eder. Anahtarlarla, yer değiştiren öğeler kompozisyonlarını korur. Bkz. [Listeler ve ızgaralar](lists.md).

### Girdilerinden daha seyrek değişen değerler için derivedStateOf kullanın

Bir kaydırma konumu kaydırmanın her karesinde değişir, ama "liste hiç kaydırıldı mı" bir kez değişir. Konumu okumak her karede yeniden kompoze eder; bir `derivedStateOf` okumak bir kez. Bkz. [Durum](state.md#derivedstateof).

### Atlamanın işlemesine izin verin

Bir scope yalnız argümanları eşit olduğunda atlanır. Ucuz ve doğru karşılaştırılan değerler geçirin: `operator==`'ı olan struct'lar, büyük kapsayıcıların kopyaları yerine durumlar (kimlikle karşılaştırılırlar). `std::function` alan bir composable hiçbir zaman atlanmaz.

### Az önce okuduğunuz durumu yazmayın

Kompozisyon sırasında, bu kompozisyonun zaten okuduğu bir durumu yazmak Kompoz'un hemen yeniden kompoze etmesine yol açar; bunu her geçişte yapmak hiç durulmaz. Durumu callback'lerde ve efektlerde güncelleyin ya da değeri saklamak yerine hesaplayın.

## Ayrıca bakın

- [Yaşam döngüsü ve kimlik](lifecycle.md): scope'lar ve atlama.
- [Özel yerleşimler](custom-layouts.md): ölçmeyi ve yerleştirmeyi kendiniz yapmak.
- [Grafik](graphics.md): çizim ve `graphicsLayer`.
- [Araçlar](tools.md): profilleyici ve recomposition vurgulayıcısı.
