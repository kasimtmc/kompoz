# Eğitim

Küçük bir uygulamayı adım adım kurun: metin, yerleşim, durum, kayan bir liste ve Material 3 stili. Her adım bir öncekine biraz daha ekler.

Bu eğitim, Kompoz'u bir kez derlediğinizi ([Kurulum](setup.md) sayfasına bakın) ve onunla kendi uygulamanızı çalıştırabildiğinizi varsayar. Her kod parçası eksiksiz C++'tır: `main.cpp`'nize kopyalayın.

## Adım 1: bir composable fonksiyon

Bir Kompoz uygulaması *composable fonksiyonlardan* oluşur: başka composable'ları çağırarak arayüzün bir parçasını tarif eden sıradan C++ fonksiyonları. En basiti `Text`'tir.

```cpp
void Greeting(const std::string& name) {
    Text("Hello, " + name + "!");
}

Application kompoz::createApplication(int, char**) {
    return {.title = "Tutorial", .content = [] { Greeting("Kompoz"); }};
}
```

`createApplication` uygulamanın giriş noktasıdır: `main` platform arka ucuna aittir ve o, bu fonksiyonu bir kez çağırır. Döndürdüğü `Application`'da pencerenin başlığı, masaüstündeki boyutu ve pencereyi dolduran composable olan `content` bulunur.

XML yok, view nesneleri yok, `setText` yok: ekrandakini değiştirmek için composable'ları farklı verilerle çağırırsınız, Kompoz da ekranı buna uyacak şekilde günceller.

## Adım 2: yerleşim

Art arda konan composable'lar, nasıl dizileceklerini söylemezseniz üst üste çizilir. `Column` çocuklarını bir sütuna, `Row` bir satıra, `Box` ise üst üste yerleştirir.

```cpp
struct Message {
    std::string author, body;
};

void MessageCard(const Message& msg) {
    Row(Modifier().padding(8), [&] {
        Icon(Icons::Filled::AccountCircle, "Profile picture", Modifier().size(40));
        Spacer(Modifier().width(8));
        Column([&] {
            Text(msg.author, {.font = typography().titleSmall});
            Spacer(Modifier().height(4));
            Text(msg.body, {.font = typography().bodyMedium});
        });
    });
}
```

Çocuklar, Compose'un sondaki lambdası gibi, son argüman olan bir lambdaya girer. İçerik lambdaları ebeveyn kompoze edilirken hemen çalışır; bu yüzden orada `[&]` ile yakalamak güvenlidir.

`Modifier` bir composable'ı süsler: boyut, iç boşluk, arka plan, tıklamalar ve çok daha fazlası. Modifier'lar zincirlenir ve sıraları önemlidir (önce `padding` sonra `background`, önce `background` sonra `padding` ile aynı şey değildir). [Modifier'lar](modifiers.md) sayfasına bakın.

## Adım 3: durum

Hiç değişmeyen bir arayüz pek uygulama sayılmaz. Kompoz, bir composable'ın okuduğu bir *durum* değişince onu yeniden çizer. `mutableStateOf` bir durum oluşturur; `remember` onu recomposition'lar boyunca saklar, böylece fonksiyon her çalıştığında yeniden oluşturulmaz.

```cpp
void Counter() {
    auto count = remember([] { return mutableStateOf(0); });

    Column(Modifier().padding(16), {.verticalArrangement = Arrangement::spacedBy(8)}, [&] {
        Text("You clicked " + std::to_string(count.get()) + " times");
        Button([count] { count.set(count.get() + 1); }, [] { Text("Click me"); });
    });
}
```

- `count.get()` durumu okur. `Counter` onu okuduğu için, durum değişince `Counter` yeniden çalışır.
- `count.set(...)` ona yazar; burada butonun `onClick`'inden.
- `MutableState<T>`, kopyalayabileceğiniz küçük bir handle'dır. Tıklama callback'i daha sonra, kompozisyondan sonra çalışır; bu yüzden `count`'u referansla değil, **değerle** (`[count]`) yakalar.

`Column`'un ikinci argümanı, designated initializer'larla doldurulan bir stil struct'ıdır: C++'ın Compose'un isimli parametrelerine cevabı. Fazlası için [Durum](state.md) sayfasına bakın.

## Adım 4: kayan bir liste

`Column`, görünmeyenler dahil her çocuğunu kompoze eder ve yerleştirir. Uzun listeler için `LazyColumn` kullanın: kaydırdıkça yalnızca ekrandaki öğeleri, artı küçük bir payı kompoze eder.

```cpp
#! struct Message { std::string author, body; };
#! void MessageCard(const Message&) {}
void Conversation(const std::vector<Message>& messages) {
    LazyColumn(Modifier().fillMaxSize(), [&](LazyListScope& list) {
        list.items(messages, [](const Message& msg) { MessageCard(msg); });
    });
}
```

Lambda, öğeleri tarif eden bir `LazyListScope` alır: `item` tek bir öğe, `items` bütün bir liste ekler. Öğe içeriği daha sonra, her öğe görünür oldukça kompoze edilir; bu yüzden öğe lambdası o zamana kadar ortadan kalkmış olabilecek hiçbir şeyi referansla yakalamaz. Burada yalnızca kendi argümanını kullanıyor.

Öğeler eklenip çıkarılabiliyor ya da yer değiştirebiliyorsa, durumu öğeyle birlikte gitsin diye her birine sabit bir anahtar verin:

```cpp body
#! struct Message { int id; std::string author, body; };
#! void MessageCard(const Message&) {}
#! std::vector<Message> messages;
LazyColumn(Modifier().fillMaxSize(), [&](LazyListScope& list) {
    list.items(messages, [](const Message& msg) { return msg.id; }, [](const Message& msg) { MessageCard(msg); });
});
```

Izgaralar, pager'lar, yapışkan başlıklar ve koddan kaydırma için [Listeler ve ızgaralar](lists.md) sayfasına bakın.

## Adım 5: Material 3

Kompoz, Compose Material 3 gibi Material 3'ü uygular. `MaterialTheme` içindeki her şeye bir renk şeması, bir tipografi ve şekiller verir; `Scaffold` bir ekranı uygulama çubuğu, yüzen eylem butonu ve snackbar'larıyla birlikte yerleştirir.

```cpp
#! struct Message { std::string author, body; };
#! void Conversation(const std::vector<Message>&) {}
void ConversationScreen(const std::vector<Message>& messages) {
    Scaffold(Modifier(), {.topBar = [] { TopAppBar({.title = [] { Text("Messages"); }}); }},
             [&](PaddingValues padding) {
                 Box(Modifier().padding(padding), [&] { Conversation(messages); });
             });
}

Application kompoz::createApplication(int, char**) {
    return {.title = "Tutorial", .content = [] {
                static const std::vector<Message> messages = {{"Ada", "Hello!"}, {"Linus", "Hi Ada."}};
                bool dark = isSystemInDarkTheme();
                MaterialTheme(dark ? darkColorScheme() : lightColorScheme(), [] { ConversationScreen(messages); });
            }};
}
```

`Scaffold`, içeriğine onu çubuklardan ve sistemin kenar boşluklarından (durum çubuğu, ekran çentiği) uzak tutan iç boşluğu verir. `isSystemInDarkTheme()` sistemin koyu modunu değiştikçe izler; Android 12+'da ve Linux masaüstlerinde `dynamicLightColorScheme()` ile sistemin renklerini de kullanabilirsiniz. [Tema](theming.md) sayfasına bakın.

## Adım 6: açılan bir kart

Her mesajı, tıklanınca metninin tamamını gösteren bir kart yapalım. Açık olma bayrağı tek bir karta ait bir durumdur; bu yüzden her kart kendininkini hatırlar.

```cpp
#! struct Message { std::string author, body; };
void MessageCard(const Message& msg) {
    auto expanded = remember([] { return mutableStateOf(false); });

    Card([expanded] { expanded.set(!expanded.get()); }, Modifier().fillMaxWidth().padding(8, 4), [&] {
        Column(Modifier().padding(16).animateContentSize(), [&] {
            Text(msg.author, {.font = typography().titleSmall, .color = colorScheme().primary});
            Spacer(Modifier().height(4));
            Text(msg.body, {.font = typography().bodyMedium, .maxLines = expanded.get() ? 0 : 1,
                            .overflow = TextOverflow::Ellipsis});
        });
    });
}
```

- `Card(onClick, modifier, content)`, Material'ın dalga (ripple) efektiyle tıklanabilir bir karttır.
- `colorScheme()` ve `typography()` geçerli temanın renklerini ve yazı stillerini okur.
- `maxLines = 0` sınır yok demektir; tek satırda metin üç noktayla biter.
- `animateContentSize()`, metin uzayıp kısaldığında sütunun boyutunu canlandırır.

Tembel bir listede `remember` öğe başınadır: her kart kendi `expanded`'ını tutar. Anahtarlar varsa, liste değiştiğinde de mesajla birlikte kalır.

## Sırada ne var

- [Kompoz'da düşünmek](thinking.md): deklaratif arayüzün ve recomposition'ın ardındaki fikirler.
- [Compose'dan gelenler için](coming-from-compose.md): Kotlin'deki Compose'un C++'a nasıl karşılık geldiği.
- Gerçek ekranlar kurmak için [Yerleşimler](layouts.md), [Material bileşenleri](material.md) ve [Animasyon](animation.md).
- [Araçlar](tools.md): çalışırken önizlemeler ve kaydedince yeniden derleme.
