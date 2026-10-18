# Metin

Metni göstermek, stillendirmek, kullanıcının seçmesine izin vermek ve metin girdisi almak. Kompoz metni HarfBuzz ile şekillendirir, sağdan sola yazılan dilleri yerleştirir ve paketlenen fontta olmayan karakterler için sistemin fontlarına başvurur.

## Metin göstermek

`Text`, bir dizeyi geçerli stille (`LocalTextStyle`) ve geçerli içerik rengiyle gösterir:

```cpp body
Text("Hello, world");
Text("A title", {.font = typography().titleLarge});
Text("A note", Modifier().padding(8), {.color = colorScheme().onSurfaceVariant});
```

Aşırı yüklemeleri önce metni, sonra isteğe bağlı olarak bir `Modifier`'ı, ardından bir `TextStyle`'ı alır. Metin boyutları sp cinsindendir ve sistemin font boyutuna göre ölçeklenir.

## Metni stillendirmek

`TextStyle`'ın alanları, yazılma sırasıyla:

| Alan | Ne yapar |
|---|---|
| `font` | Bütün bir `Font`: Material'ın yazı stilleri (`typography().bodyLarge`) birer fonttur. |
| `fontSize` | Boyut: sp cinsinden bir sayı ya da geçerli boyuta göre `TextUnit::em(1.2f)`. |
| `fontWeight` | `FontWeight::Normal`, `Medium`, `Bold` ya da `W100` … `W900`. |
| `color` | Renk; verilmezse `LocalContentColor`. |
| `textAlign` | `TextAlign::Start`, `Center`, `End`, `Justify`. |
| `textDecoration` | `TextDecoration::Underline`, `LineThrough`. |
| `maxLines`, `minLines` | En fazla kaç satır kaplayabileceği (0: sınır yok) ve en az kaç satır. |
| `fontStyle` | `FontStyle::Italic`. |
| `fontFamily` | `FontFamily::Serif`, `Monospace` ya da dosyalardan yapılmış bir aile. |
| `letterSpacing` | Her karakterden sonraki ek boşluk. |
| `fontFeatureSettings` | CSS'in yazdığı gibi OpenType özellikleri: `"tnum"`, `"smcp"`. |
| `overflow` | Kesildiğinde son satırın ne gösterdiği: `TextOverflow::Clip`, `Ellipsis`, `StartEllipsis`, `MiddleEllipsis`. |
| `softWrap` | `false`: her paragraf tek satır. |
| `autoSize` | Alana sığan font boyutu: `TextAutoSize::StepBased(12, 48)`. |
| `lineHeight` | Bir taban çizgisinden ötekine sp. |

Designated initializer'lar bu sırayla yazılır; ihtiyacınız olmayanları atlayın.

```cpp body
#! std::string article;
Text(article, Modifier().fillMaxWidth(), {.fontWeight = FontWeight::Medium, .textAlign = TextAlign::Justify,
                                          .maxLines = 3, .overflow = TextOverflow::Ellipsis});
```

## Tek metinde birkaç stil

Bir `AnnotatedString`, Compose'daki gibi metninin parçaları üzerinde stiller taşır. `buildAnnotatedString` ile oluşturun:

```cpp
#! void openTerms() {}
void Terms() {
    Text(buildAnnotatedString([](AnnotatedString::Builder& b) {
        b.append("By continuing you accept the ");
        b.withLink([] { openTerms(); }, [&] { b.append("terms"); });
        b.append(". ");
        b.withStyle({.fontWeight = FontWeight::Bold}, [&] { b.append("No refunds."); });
    }));
}
```

- `withStyle(SpanStyle, content)`, `content`'in eklediklerini stillendirir; aralıklar iç içe geçer, içteki kazanır. `SpanStyle`'da `fontSize`, `fontWeight`, `fontStyle`, `fontFamily`, `color`, `textDecoration`, `background`, `letterSpacing`, `brush`… vardır.
- `withStyle(ParagraphStyle, content)`, kendi hizalaması, girintisi ve satır yüksekliği olan bir paragraf yapar.
- `withLink(onClick, content)`, `content`'in eklediklerini tıklanabilir ve birincil renkte altı çizili yapar; `LinkAnnotation::Url(url)` bir web adresi açar.
- `pushStyle`/`pop`, `addStyle(style, start, end)` ve dize açıklamaları Compose'daki gibi çalışır.

`AnnotatedString::fromHtml`, basit HTML'i (`<b>`, `<i>`, `<u>`, `<a href>`, `<br>`, `<p>`, listeler) açıklamalı bir dizeye dönüştürür.

## Fontlar

Kompoz, Noto Sans'ı üç kalınlıkta (normal, orta ve kalın) paketler ve onda olmayan her şey için sistemin fontlarını kullanır: simgeler, emoji, CJK, diğer yazı sistemleri. `FontFamily::Serif` ve `FontFamily::Monospace`, sistemde varsa sistemin fontlarını kullanır.

Kendi fontlarınız varlık dosyalarıdır:

```cpp
void Quote(const std::string& text) {
    static const FontFamily lora = FontFamily::fromFiles({{"fonts/Lora-Regular.ttf"},
                                                          {"fonts/Lora-Bold.ttf", FontWeight::Bold}});
    Text(text, {.fontSize = 20, .fontStyle = FontStyle::Italic, .fontFamily = lora});
}
```

Kendi dosyası olmayan bir kalınlık en yakınından üretilir (kalınlar kalınlaştırılarak); eksik bir italik eğiltilerek elde edilir. Değişken bir font, her kalınlık için `FontVariation` ayarlarıyla birkaç kez listelenebilir. Varlık dosyalarının nereye konacağı için bkz. [Kaynaklar](resources.md).

## Metin seçmek

Metin varsayılan olarak seçilemez. Bir `SelectionContainer`'ın içinde seçilebilir: bütün metinlerine yayılan tek bir seçim, dokunmatik ekranlarda tutamaçlar, bir Kopyala eylemi, fareyle sürükleme ve Ctrl+C:

```cpp body
SelectionContainer(Modifier(), [&] {
    Column([&] {
        Text("Order number");
        Text("KMP-2026-0042", {.fontWeight = FontWeight::Bold});
        DisableSelection([] { Text("(not selectable)"); });
    });
});
```

## Metin alanları

`TextField` (dolgulu) ve `OutlinedTextField`, Material'ın metin alanlarıdır. Compose'daki gibi metinleri ya bir değer ve bir callback ile yukarı taşınır ya da bir `TextFieldState`'te tutulur.

### Bir değer ve bir callback ile

```cpp
void EmailField() {
    auto email = rememberSaveable([] { return mutableStateOf(std::string()); });
    OutlinedTextField(email.get(), [email](std::string v) { email.set(std::move(v)); }, Modifier().fillMaxWidth(),
                      {.singleLine = true,
                       .label = "Email",
                       .supportingText = "We never share it",
                       .keyboardOptions = {.keyboardType = KeyboardType::Email, .imeAction = ImeAction::Next}});
}
```

### Bir TextFieldState ile

Bir `TextFieldState`, metni ve seçimini, bir de geri alma geçmişini tutar:

```cpp
#! void send(const std::string&) {}
void Composer() {
    auto message = rememberTextFieldState();
    Row(Modifier().fillMaxWidth(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
        OutlinedTextField(message, Modifier().weight(1), {.maxLines = 4, .placeholder = "Message"});
        IconButton([message] { send(message.text()); message.clearText(); }, [] { Icon(Icons::AutoMirrored::Filled::Send, "Send"); });
    });
}
```

`text()` ve `selection()` gözlemlenebilirdir. `edit([](TextFieldBuffer& b) { … })` ikisini birden değiştirir; `undoState().undo()` geri alır, alan Ctrl+Z ile de geri alır. `InputTransformation::maxLength(10)` yazılabilecekleri sınırlar; bir `OutputTransformation` ise metni değiştirmeden nasıl gösterildiğini değiştirir.

### Seçenekler

| Seçenek | Ne yapar |
|---|---|
| `singleLine`, `minLines`, `maxLines` | Tek satır (Enter klavye eylemini çalıştırır) ya da kaydırmadan önce kaç satır gösterdiği. |
| `label`, `placeholder` | Metnin üzerinde yüzen bir etiket ve alan boşken gösterilen bir ipucu. |
| `prefix`, `suffix` | Girdinin önünde ve arkasında, para birimi ya da birim gibi metin. |
| `leadingIcon`, `trailingIcon` | Uçlardaki composable'lar. |
| `supportingText`, `isError` | Alanın altında bir satır ve hata renkleri. |
| `keyboardOptions` | Klavyenin türü (`Email`, `Number`, `Password`…), eylem tuşu (`Next`, `Done`, `Search`…), büyük harf kullanımı ve otomatik düzeltme. |
| `keyboardActions`, `onSubmit` | Eylem tuşunun ne yaptığı. |
| `visualTransformation` | `VisualTransformation::Password` noktalar gösterir. |
| `enabled`, `readOnly` | Devre dışı ya da odaklanabilir ve seçilebilir ama düzenlenemez. |

`BasicTextField`, kendi tasarımlarınız için süslemesiz bir alandır; `decorator` seçeneği metnin çevresini çizer.

Android'de yazılım klavyesi öneriler gösterir ve otomatik düzeltme yapar; türü ve eylem tuşu `keyboardOptions`'ı izler. Masaüstlerinde alanlar alışılmış klavye düzenlemesine sahiptir: Ctrl+oklar ile kelime kelime gezinme, Home ve End, seçmek için Shift, bir kelime için çift tıklama.

### Odak

Bir `FocusRequester` odağı koddan bir alana taşır, `focusManager()` ise odağı temizler:

```cpp
void Search() {
    auto query = rememberTextFieldState();
    auto focus = remember([] { return FocusRequester(); });
    LaunchedEffect(0, [focus]() -> Task {
        focus.requestFocus();   // ekran açıldığında alana odaklan
        co_return;
    });
    OutlinedTextField(query, Modifier().fillMaxWidth().focusRequester(focus), {.singleLine = true, .label = "Search"});
}
```

## Metni ölçmek

Bir `Canvas`'a metin çizmek ya da bir metnin nasıl yerleştirildiğini öğrenmek için bir metin ölçer kullanın:

```cpp
void Gauge(int percent) {
    auto measurer = rememberTextMeasurer();
    Canvas(Modifier().size(120), [=](DrawScope& d) {
        d.drawCircle(colorScheme().primary.copy(0.2f));
        TextLayoutResult label = measurer.measure(std::to_string(percent) + "%", {.fontSize = 24});
        d.drawText(label, {d.center().x - label.size().width / 2, d.center().y - label.size().height / 2});
    });
}
```

`Text` de nasıl yerleştirildiğini bir `onTextLayout` callback'i üzerinden söyleyebilir: kaç satır kapladığı, kesilip kesilmediği.

```cpp
void Excerpt(const std::string& article) {
    auto cut = remember([] { return mutableStateOf(false); });
    Column([&] {
        Text(article, Modifier(), {.maxLines = 3, .overflow = TextOverflow::Ellipsis},
             [cut](const TextLayoutResult& r) { cut.set(r.hasVisualOverflow()); });
        if (cut.get()) TextButton([] {}, [] { Text("Read more"); });
    });
}
```

Kompoz'un her yerinde metin içindeki konumlar UTF-8 bayt konumlarıdır.

## Sağdan sola ve karışık metin

Arapça, İbranice ya da Farsça metin sağdan sola yerleştirilir; karışık metin Unicode çift yönlü algoritmasıyla sıralanır. Bir paragrafın yönü, yönü olan ilk harfinden gelir; yönü olmayan metin (rakamlar, noktalama) yerleşimin yönünü izler.

## Ayrıca bakın

- [Kaynaklar](resources.md): font dosyaları ve kütüphanenin kendi metinleri.
- [Tema](theming.md): yazı ölçeği.
- [Erişilebilirlik](accessibility.md): metnin nasıl sesli okunduğu.
