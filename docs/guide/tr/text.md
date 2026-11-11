# Metin

Metni göstermek, stillendirmek, kullanıcının seçmesine izin vermek ve metin girdisi almak. Kompoz metni HarfBuzz ile şekillendirir, sağdan sola yazılan dilleri yerleştirir ve paketlenen fontta olmayan karakterler için sistemin fontlarına başvurur.

## Metin göstermek

`Text`, bir dizeyi geçerli stille (`LocalTextStyle`) ve geçerli içerik rengiyle gösterir:

```kotlin body
Text("Hello, world")
Text("A title", style = MaterialTheme.typography.titleLarge)
Text("A note", modifier = Modifier.padding(8.dp), color = MaterialTheme.colorScheme.onSurfaceVariant)
```

Önce metni, sonra Compose'un parametrelerini, yerine göre ya da adıyla alır: `modifier`, `color`, `fontSize`, `fontStyle`, `fontWeight`, `fontFamily`, `letterSpacing`, `textDecoration`, `textAlign`, `lineHeight`, `overflow`, `softWrap`, `maxLines`, `minLines`, `onTextLayout`, `style`, `autoSize`. Metin boyutları sp cinsindendir ve sistemin font boyutuna göre ölçeklenir.

## Metni stillendirmek

Bir `TextStyle` bir metnin görünüşünü tutar: `style = …` bütün bir stil verir (bir temanın yazı stili, `typography().titleLarge` de olur), adıyla verilenler onun üstüne gelir. Alanları; aksi söylenmedikçe Text bunları adıyla da alır:

| Alan | Ne yapar |
|---|---|
| `font` | Bütün bir `Font`: Material'ın yazı stilleri (`typography().bodyLarge`) birer fonttur (yalnızca bir `TextStyle`'da). |
| `fontSize` | Boyut: sp cinsinden bir sayı ya da geçerli boyuta göre `1.2.em`. |
| `fontWeight` | `FontWeight.Normal`, `Medium`, `Bold` ya da `W100` … `W900`. |
| `color` | Renk; verilmezse `LocalContentColor`. |
| `textAlign` | `TextAlign.Start`, `Center`, `End`, `Justify`. |
| `textDecoration` | `TextDecoration.Underline`, `LineThrough`. |
| `maxLines`, `minLines` | En fazla kaç satır kaplayabileceği (0: sınır yok) ve en az kaç satır. |
| `fontStyle` | `FontStyle.Italic`. |
| `fontFamily` | `FontFamily.Serif`, `Monospace` ya da dosyalardan yapılmış bir aile. |
| `letterSpacing` | Her karakterden sonraki ek boşluk. |
| `fontFeatureSettings` | CSS'in yazdığı gibi OpenType özellikleri: `"tnum"`, `"smcp"` (yalnızca bir `TextStyle`'da). |
| `overflow` | Kesildiğinde son satırın ne gösterdiği: `TextOverflow.Clip`, `Ellipsis`, `StartEllipsis`, `MiddleEllipsis`. |
| `softWrap` | `false`: her paragraf tek satır. |
| `autoSize` | Alana sığan font boyutu: `TextAutoSize.StepBased(12.sp, 48.sp)`. |
| `lineHeight` | Bir taban çizgisinden ötekine sp. |

Yalnızca gerekenleri, istediğiniz sırayla verin:

```kotlin body
#! val article = ""
Text(article, modifier = Modifier.fillMaxWidth(), fontWeight = FontWeight.Medium, textAlign = TextAlign.Justify, maxLines = 3, overflow = TextOverflow.Ellipsis)
```

## Tek metinde birkaç stil

Bir `AnnotatedString`, Compose'daki gibi metninin parçaları üzerinde stiller taşır. `buildAnnotatedString` ile oluşturun:

```kotlin
#! fun openTerms() {}
@Kompozable
fun Terms() {
    Text(buildAnnotatedString {
        append("By continuing you accept the ")
        withLink(LinkAnnotation.Clickable("terms") { openTerms() }) { append("terms") }
        append(". ")
        withStyle(SpanStyle(fontWeight = FontWeight.Bold)) { append("No refunds.") }
    })
}
```

- `withStyle(SpanStyle, content)`, `content`'in eklediklerini stillendirir; aralıklar iç içe geçer, içteki kazanır. `SpanStyle`'da `fontSize`, `fontWeight`, `fontStyle`, `fontFamily`, `color`, `textDecoration`, `background`, `letterSpacing`, `brush`… vardır.
- `withStyle(ParagraphStyle, content)`, kendi hizalaması, girintisi ve satır yüksekliği olan bir paragraf yapar.
- `withLink(LinkAnnotation.Clickable(tag) { … }) { … }`, bloğun eklediklerini tıklanabilir ve birincil renkte altı çizili yapar; `LinkAnnotation.Url(url)` bir web adresi açar. Kompoz'un `withLink({ … }) { … }`'i yalnız tıklamayı alır.
- `pushStyle`/`pop`, `addStyle(style, start, end)` ve dize açıklamaları Compose'daki gibi çalışır.

`AnnotatedString.fromHtml`, basit HTML'i (`<b>`, `<i>`, `<u>`, `<a href>`, `<br>`, `<p>`, listeler) açıklamalı bir dizeye dönüştürür.

## Fontlar

Kompoz, Noto Sans'ı üç kalınlıkta (normal, orta ve kalın) paketler ve onda olmayan her şey için sistemin fontlarını kullanır: simgeler, emoji, CJK, diğer yazı sistemleri. `FontFamily.Serif` ve `FontFamily.Monospace`, sistemde varsa sistemin fontlarını kullanır. `FontFamily.fromFiles`, font dosyalarından bir aile yapar (Kompoz'unki; Compose'un `FontFamily(Font(…))`'ı kaynakları adlandırır).

Kendi fontlarınız varlık dosyalarıdır:

```kotlin
val Lora = FontFamily.fromFiles(listOf(FontFile("fonts/Lora-Regular.ttf"), FontFile("fonts/Lora-Bold.ttf", FontWeight.Bold)))

@Kompozable
fun Quote(text: String) {
    Text(text, fontSize = 20.sp, fontStyle = FontStyle.Italic, fontFamily = Lora)
}
```

Kendi dosyası olmayan bir kalınlık en yakınından üretilir (kalınlar kalınlaştırılarak); eksik bir italik eğiltilerek elde edilir. Değişken bir font, her kalınlık için `FontVariation` ayarlarıyla birkaç kez listelenebilir. Varlık dosyalarının nereye konacağı için bkz. [Kaynaklar](resources.md).

## Metin seçmek

Metin varsayılan olarak seçilemez. Bir `SelectionContainer`'ın içinde seçilebilir: bütün metinlerine yayılan tek bir seçim, dokunmatik ekranlarda tutamaçlar, bir Kopyala eylemi, fareyle sürükleme ve Ctrl+C:

```kotlin body
SelectionContainer {
    Column {
        Text("Order number")
        Text("KMP-2026-0042", fontWeight = FontWeight.Bold)
        DisableSelection { Text("(not selectable)") }
    }
}
```

## Metin alanları

`TextField` (dolgulu) ve `OutlinedTextField`, Material'ın metin alanlarıdır. Compose'daki gibi metinleri ya bir değer ve bir callback ile yukarı taşınır ya da bir `TextFieldState`'te tutulur.

### Bir değer ve bir callback ile

```kotlin
@Kompozable
fun EmailField() {
    var email by rememberSaveable { mutableStateOf("") }
    OutlinedTextField(
        value = email,
        onValueChange = { email = it },
        modifier = Modifier.fillMaxWidth(),
        singleLine = true,
        label = { Text("Email") },
        supportingText = { Text("We never share it") },
        keyboardOptions = KeyboardOptions(keyboardType = KeyboardType.Email, imeAction = ImeAction.Next),
    )
}
```

### Bir TextFieldState ile

Bir `TextFieldState`, metni ve seçimini, bir de geri alma geçmişini tutar:

```kotlin
#! fun send(text: String) {}
@Kompozable
fun Kompozer() {
    val message = rememberTextFieldState()
    Row(modifier = Modifier.fillMaxWidth(), verticalAlignment = Alignment.CenterVertically) {
        OutlinedTextField(
            state = message,
            modifier = Modifier.weight(1f),
            lineLimits = TextFieldLineLimits.MultiLine(maxHeightInLines = 4),
            placeholder = { Text("Message") },
        )
        IconButton(onClick = { send(message.text.toString()); message.clearText() }) {
            Icon(Icons.AutoMirrored.Filled.Send, contentDescription = "Send")
        }
    }
}
```

`text` ve `selection` gözlemlenebilirdir. `edit { … }` ikisini birden değiştirir (bloğu bir `TextFieldBuffer` üzerinde); `undoState.undo()` geri alır, alan Ctrl+Z ile de geri alır. `InputTransformation.maxLength(10)` yazılabilecekleri sınırlar; bir `OutputTransformation` ise metni değiştirmeden nasıl gösterildiğini değiştirir.

### Seçenekler

| Seçenek | Ne yapar |
|---|---|
| `singleLine`, `minLines`, `maxLines`; `lineLimits` | Tek satır (Enter klavye eylemini çalıştırır) ya da kaydırmadan önce kaç satır gösterdiği; bir durumla `TextFieldLineLimits.SingleLine` ya da `MultiLine(min, max)`. |
| `label`, `placeholder` | Metnin üzerinde yüzen bir etiket ve alan boşken gösterilen bir ipucu: Compose'daki gibi composable'lar, `label = { Text("…") }` (bir dize de olur). |
| `prefix`, `suffix` | Girdinin önünde ve arkasında, para birimi ya da birim gibi metin (burada dize; Compose composable alır). |
| `leadingIcon`, `trailingIcon` | Uçlardaki composable'lar. |
| `supportingText`, `isError` | Alanın altında bir satır (bir composable ya da bir dize) ve hata renkleri. |
| `keyboardOptions` | Klavyenin türü (`Email`, `Number`, `Password`…), eylem tuşu (`Next`, `Done`, `Search`…), büyük harf kullanımı ve otomatik düzeltme. |
| `keyboardActions`, `onSubmit` | Eylem tuşunun ne yaptığı. |
| `visualTransformation` | `VisualTransformation.Password` noktalar gösterir. |
| `enabled`, `readOnly` | Devre dışı ya da odaklanabilir ve seçilebilir ama düzenlenemez. |

`BasicTextField`, kendi tasarımlarınız için süslemesiz bir alandır; `decorator` seçeneği metnin çevresini çizer.

Android'de yazılım klavyesi öneriler gösterir ve otomatik düzeltme yapar; türü ve eylem tuşu `keyboardOptions`'ı izler. Masaüstlerinde alanlar alışılmış klavye düzenlemesine sahiptir: Ctrl+oklar ile kelime kelime gezinme, Home ve End, seçmek için Shift, bir kelime için çift tıklama.

### Odak

Bir `FocusRequester` odağı koddan bir alana taşır, `focusManager()` ise odağı temizler:

```kotlin
@Kompozable
fun Search() {
    val query = rememberTextFieldState()
    val focus = remember { FocusRequester() }
    LaunchedEffect(Unit) {
        focus.requestFocus()   // ekran açıldığında alana odaklan
    }
    OutlinedTextField(state = query, modifier = Modifier.fillMaxWidth().focusRequester(focus), lineLimits = TextFieldLineLimits.SingleLine, label = { Text("Search") })
}
```

## Metni ölçmek

Bir `Canvas`'a metin çizmek ya da bir metnin nasıl yerleştirildiğini öğrenmek için bir metin ölçer kullanın:

```kotlin
@Kompozable
fun Gauge(percent: Int) {
    val measurer = rememberTextMeasurer()
    val primary = MaterialTheme.colorScheme.primary   // tema çizerken değil, kompozisyonda okunur
    Canvas(modifier = Modifier.size(120.dp)) {
        drawCircle(primary.copy(alpha = 0.2f))
        val label = measurer.measure("$percent%", style = TextStyle(fontSize = 24.sp))
        drawText(label, topLeft = Offset(center.x - label.size.width / 2, center.y - label.size.height / 2))
    }
}
```

`Text` de nasıl yerleştirildiğini bir `onTextLayout` callback'i üzerinden söyleyebilir: kaç satır kapladığı, kesilip kesilmediği.

```kotlin
@Kompozable
fun Excerpt(article: String) {
    var cut by remember { mutableStateOf(false) }
    Column {
        Text(article, maxLines = 3, overflow = TextOverflow.Ellipsis, onTextLayout = { cut = it.hasVisualOverflow })
        if (cut) TextButton(onClick = { }) { Text("Read more") }
    }
}
```

Kompoz'un her yerinde metin içindeki konumlar UTF-8 bayt konumlarıdır.

## Sağdan sola ve karışık metin

Arapça, İbranice ya da Farsça metin sağdan sola yerleştirilir; karışık metin Unicode çift yönlü algoritmasıyla sıralanır. Bir paragrafın yönü, yönü olan ilk harfinden gelir; yönü olmayan metin (rakamlar, noktalama) yerleşimin yönünü izler.

## Ayrıca bakın

- [Kaynaklar](resources.md): font dosyaları ve kütüphanenin kendi metinleri.
- [Tema](theming.md): yazı ölçeği.
- [Erişilebilirlik](accessibility.md): metnin nasıl sesli okunduğu.
