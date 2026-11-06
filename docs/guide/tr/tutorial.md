# Eğitim

Küçük bir uygulamayı adım adım kurun: metin, yerleşim, durum, kayan bir liste ve Material 3 stili. Her adım bir öncekine biraz daha ekler.

Bu eğitim, Kompoz'u bir kez derlediğinizi ve `kompoz_compose` ile kendi CMake projenize eklediğinizi varsayar ([Kurulum](setup.md) sayfasına bakın). Her kod parçası eksiksizdir: `main.cpp`'nize, `#include <kompoz/kompoz.hpp>` ve `using namespace kompoz;`'dan sonra kopyalayın.

## Adım 1: bir composable fonksiyon

Bir Kompoz uygulaması *composable fonksiyonlardan* oluşur: başka composable'ları çağırarak arayüzün bir parçasını tarif eden, `@Composable` ile işaretlenmiş fonksiyonlar. Kotlin'deki gibi, `.cpp` dosyanızda yazılırlar. En basiti `Text`'tir.

```kotlin
@Composable
fun Greeting(name: String) {
    Text("Merhaba, $name!")
}

kompoz::Application kompoz::createApplication(int, char**) {
    return {.title = "Eğitim", .content = [] { Greeting("Kompoz"); }};
}
```

`createApplication` uygulamanın C++'taki giriş noktasıdır: `main` platform arka ucuna aittir ve o, bu fonksiyonu bir kez çağırır. Döndürdüğü `Application`'da pencerenin başlığı, masaüstündeki boyutu ve pencereyi dolduran composable olan `content` bulunur.

XML yok, view nesneleri yok, `setText` yok: ekrandakini değiştirmek için composable'ları farklı verilerle çağırırsınız, Kompoz da ekranı buna uyacak şekilde günceller.

## Adım 2: yerleşim

Art arda konan composable'lar, nasıl dizileceklerini söylemezseniz üst üste çizilir. `Column` çocuklarını bir sütuna, `Row` bir satıra, `Box` ise üst üste yerleştirir.

```kotlin
data class Message(val author: String, val body: String)

@Composable
fun MessageCard(msg: Message) {
    Row(modifier = Modifier.padding(8.dp)) {
        Icon(Icons.Filled.AccountCircle, contentDescription = "Profil resmi", modifier = Modifier.size(40.dp))
        Spacer(Modifier.width(8.dp))
        Column {
            Text(msg.author, style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(4.dp))
            Text(msg.body, style = MaterialTheme.typography.bodyMedium)
        }
    }
}
```

Çocuklar, Compose'daki gibi sondaki lambdaya gider. Bir lambda daha sonra kendi başına yeniden çalışabilir; bu yüzden kullandığını Kotlin'inkiler gibi değerle yakalar.

`Modifier` bir composable'ı süsler: boyut, iç boşluk, arka plan, tıklamalar ve çok daha fazlası. Modifier'lar zincirlenir ve sıraları önemlidir (önce `padding` sonra `background`, önce `background` sonra `padding` ile aynı şey değildir). [Modifier'lar](modifiers.md) sayfasına bakın.

## Adım 3: durum

Hiç değişmeyen bir arayüz pek uygulama sayılmaz. Kompoz, bir composable'ın okuduğu bir *durum* değişince onu yeniden çizer. `mutableStateOf` bir durum oluşturur; `remember` onu recomposition'lar boyunca saklar, böylece fonksiyon her çalıştığında yeniden oluşturulmaz.

```kotlin
@Composable
fun Counter() {
    var count by remember { mutableStateOf(0) }

    Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
        Text("$count kez tıkladınız")
        Button(onClick = { count++ }) { Text("Tıkla") }
    }
}
```

- `count`'u okumak durumu okur. `Counter` onu okuduğu için, durum değişince `Counter` yeniden çalışır.
- `count++` ona yazar; burada butonun `onClick`'inden.
- `by`, Kotlin'deki gibi `count`'u duruma devreder: durumun değerini okur ve yazar.

Daha fazlası için [Durum](state.md) sayfasına bakın.

## Adım 4: kayan bir liste

`Column`, görünmeyenler dahil her çocuğunu kompoze eder ve yerleştirir. Uzun listeler için `LazyColumn` kullanın: kaydırdıkça yalnızca ekrandaki öğeleri, artı küçük bir payı kompoze eder.

```kotlin
data class Message(val author: String, val body: String)

@Composable
fun MessageCard(msg: Message) {
    Text("${msg.author}: ${msg.body}")
}

@Composable
fun Conversation(messages: List<Message>) {
    LazyColumn(modifier = Modifier.fillMaxSize()) {
        items(messages) { message ->
            MessageCard(message)
        }
    }
}
```

Blok, Compose'da `LazyColumn`'un alıcısının yaptığı gibi öğeleri tarif eder: `item` tek bir öğe, `items` bütün bir liste ekler. Öğe içeriği daha sonra, her öğe görünür oldukça kompoze edilir.

Öğeler eklenip çıkarılabiliyor ya da yer değiştirebiliyorsa, durumu öğeyle birlikte gitsin diye her birine sabit bir anahtar verin:

```kotlin
data class Message(val id: Int, val author: String, val body: String)

@Composable
fun Conversation(messages: List<Message>) {
    LazyColumn(modifier = Modifier.fillMaxSize()) {
        items(messages, key = { it.id }) { message ->
            Text("${message.author}: ${message.body}")
        }
    }
}
```

Izgaralar, pager'lar, yapışkan başlıklar ve koddan kaydırma için [Listeler ve ızgaralar](lists.md) sayfasına bakın.

## Adım 5: Material 3

Kompoz, Compose Material 3 gibi Material 3'ü uygular. `MaterialTheme` içindeki her şeye bir renk şeması, bir tipografi ve şekiller verir; `Scaffold` bir ekranı uygulama çubuğu, yüzen eylem butonu ve snackbar'larıyla birlikte yerleştirir.

```kotlin
data class Message(val author: String, val body: String)

@Composable
fun Conversation(messages: List<Message>) {
    LazyColumn { items(messages) { Text("${it.author}: ${it.body}") } }
}

@Composable
fun ConversationScreen(messages: List<Message>) {
    Scaffold(topBar = { TopAppBar(title = { Text("Mesajlar") }) }) { padding ->
        Box(modifier = Modifier.padding(padding)) { Conversation(messages) }
    }
}

@Composable
fun TutorialApp() {
    val messages = remember { listOf(Message("Ada", "Merhaba!"), Message("Linus", "Selam Ada.")) }
    MaterialTheme(colorScheme = if (isSystemInDarkTheme()) darkColorScheme() else lightColorScheme()) {
        ConversationScreen(messages)
    }
}

kompoz::Application kompoz::createApplication(int, char**) {
    return {.title = "Eğitim", .content = [] { TutorialApp(); }};
}
```

`Scaffold`, içeriğine onu çubuklardan ve sistemin kenar boşluklarından (durum çubuğu, ekran çentiği) uzak tutan iç boşluğu verir. `isSystemInDarkTheme()` sistemin koyu modunu değiştikçe izler; Android 12+'da ve Linux masaüstlerinde `dynamicLightColorScheme()` ile sistemin renklerini de kullanabilirsiniz. [Tema](theming.md) sayfasına bakın.

## Adım 6: açılan bir kart

Her mesajı, tıklanınca metninin tamamını gösteren bir kart yapalım. Açık olma bayrağı tek bir karta ait bir durumdur; bu yüzden her kart kendininkini hatırlar.

```kotlin
data class Message(val author: String, val body: String)

@Composable
fun MessageCard(msg: Message) {
    var expanded by remember { mutableStateOf(false) }

    Card(onClick = { expanded = !expanded }, modifier = Modifier.fillMaxWidth().padding(horizontal = 8.dp, vertical = 4.dp)) {
        Column(modifier = Modifier.padding(16.dp).animateContentSize()) {
            Text(msg.author, color = MaterialTheme.colorScheme.primary, style = MaterialTheme.typography.titleSmall)
            Spacer(Modifier.height(4.dp))
            Text(
                msg.body,
                maxLines = if (expanded) Int.MAX_VALUE else 1,
                overflow = TextOverflow.Ellipsis,
                style = MaterialTheme.typography.bodyMedium,
            )
        }
    }
}
```

- `Card(onClick = …)`, Material'ın dalga (ripple) efektiyle tıklanabilir bir karttır.
- `MaterialTheme.colorScheme` ve `MaterialTheme.typography` geçerli temanın renklerini ve yazı stillerini okur.
- Tek satırda metin üç noktayla biter; `Int.MAX_VALUE` sınırı kaldırır.
- `animateContentSize()`, metin uzayıp kısaldığında sütunun boyutunu canlandırır.

Tembel bir listede `remember` öğe başınadır: her kart kendi `expanded`'ını tutar. Anahtarlar varsa, liste değiştiğinde de mesajla birlikte kalır.

## Sırada ne var

- [Kompoz'da düşünmek](thinking.md): deklaratif arayüzün ve recomposition'ın ardındaki fikirler.
- [Compose'dan geliyorsanız](coming-from-compose.md): kompozc'nin okudukları ve composable'larınızın çevresindeki C++.
- Gerçek ekranlar kurmak için [Yerleşimler](layouts.md), [Material bileşenleri](material.md) ve [Animasyon](animation.md).
- [Araçlar](tools.md): çalışırken önizlemeler ve kaydedince yeniden derleme.
