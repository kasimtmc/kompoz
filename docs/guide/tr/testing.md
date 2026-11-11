# Test

Compose'un `createComposeRule()`'u gibi pencere olmadan çalışan arayüz testleri: `KompozTestRule { … }` bir composable'ı kompoze eder, düğümleri bulun, onlar üzerinde işlem yapın ve sonucu kontrol edin; animasyonları ve coroutine'leri deterministik kılan sanal bir saatle.

## İlk test

`KompozTestRule`, içeriğini ekransız (headless) olarak, sabit boyutlu bir metin ölçücüyle kompoze eder, yerleştirir ve çizer. Düğümleri metne, içerik açıklamasına ya da test etiketine göre bulun, onlar üzerinde işlem yapın ve doğrulayın:

```kotlin
@Kompozable
fun Counter() {
    var count by remember { mutableStateOf(0) }
    Column {
        Text("Count: $count")
        Button(onClick = { count++ }) { Text("Add") }
    }
}

@Test
fun counterIncrements() {
    val rule = KompozTestRule { Counter() }
    rule.onNodeWithText("Count: 0").assertIsDisplayed()
    rule.onNodeWithText("Add").performClick()
    rule.onNodeWithText("Count: 1").assertIsDisplayed()
}
```

Başarısız bir doğrulama, neyi aradığını ve ne bulduğunu söyleyen bir `AssertionError` (`kompoz::testing::AssertionError`) fırlatır. Herhangi bir test çatısını kullanın ya da hiçbirini: rule sade bir fonksiyon içinde de çalışır; `@Test` sizinki için birini işaretler.

## Bir test hedefi kurmak

Testlerin yalnızca çekirdek kütüphaneye ihtiyacı vardır, pencere sistemine değil:

```cmake
add_executable(myapp_tests tests/main.cpp src/screens.cpp)
target_link_libraries(myapp_tests PRIVATE kompoz_core)
add_test(NAME myapp_tests COMMAND myapp_tests)
```

Composable'larınızı uygulamanın ve testlerin paylaştığı kaynak dosyalarda tutun. Pencere sistemlerinin başlıkları olmayan bir derleme sunucusunda Kompoz'u `-DKOMPOZ_BUILD_APP=OFF` ile derleyin.

## Düğümleri bulmak

| Bulucu | Bulduğu |
|---|---|
| `onNodeWithText(text)` | Metni `text` olan bir düğüm; alt dize için `onNodeWithText(text, true)`. |
| `onNodeWithContentDescription(d)` | O içerik açıklamasına sahip bir düğüm: ikonlar, resimler. |
| `onNodeWithTag(tag)` | `Modifier.testTag(tag)` taşıyan bir düğüm. |
| `onAllNodesWithText(text)`, `onAllNodesWithContentDescription(d)`, `onAllNodesWithTag(tag)` | Hepsini, bir koleksiyon olarak: `assertCountEquals(n)`, `[i]`, `size()`. |

Compose'da olduğu gibi bulucular birleştirilmiş semantiği kullanır: bir tıklanabilirin içindeki metin o tıklanabilir adına yanıt verir; böylece `onNodeWithText("Add").performClick()` butona tıklar ve `assertIsEnabled()` onu kontrol eder.

Metni değişen ya da çevrilen bir şeyi bulmanın en sağlam yolu bir test etiketidir:

```kotlin
@Kompozable
fun Search() {
    val query = rememberTextFieldState()
    OutlinedTextField(state = query, modifier = Modifier.testTag("search"), label = { Text("Search") })
}

@Test
fun typingFillsTheField() {
    val rule = KompozTestRule { Search() }
    rule.onNodeWithTag("search").performTextInput("kompoz")
    rule.onNodeWithText("kompoz").assertExists()
}
```

## İşlemler

| İşlem | Ne yapar |
|---|---|
| `performClick()` | Düğümün ortasına dokunur. |
| `performTextInput(text)` | Düğüme odaklanır ve yazar. |
| `performKey(Key.Enter)` | Bir tuşa basar. |
| `performScrollTo()` | Düğümü göstermek için çevresindeki kaydırılabilirleri kaydırır. |
| `rule.swipe(from, to, frames)` | Pencere boyunca, adım başına bir kare süren bir sürükleme: fling için yeterince hızlı. |

Her işlem ardından arayüzün durulmasını bekler.

## Doğrulamalar

`assertExists`, `assertDoesNotExist`, `assertIsDisplayed` (pencerede, kırpılıp gizlenmemiş), `assertIsNotDisplayed`, `assertTextEquals`, `assertTextContains`, `assertContentDescriptionEquals`, `assertHasClickAction`, `assertIsEnabled`, `assertIsNotEnabled`, `assertIsSelected` (seçili bir sekme, işaretli bir checkbox, açık bir anahtar), `assertIsOn`, `assertIsOff`. Düğümü döndürdükleri için zincirlenebilirler:

```kotlin
#! @Kompozable fun Form() {}
@Test
fun saves() {
    val rule = KompozTestRule { Form() }
    rule.onNodeWithText("Save").assertIsDisplayed().assertIsEnabled().performClick()
}
```

Diğer her şey için bir düğüm `bounds()`'unu (dp cinsinden, pencere içinde) ve `text()`'ini verir, bir de `exists()` vardır. `rule.drawnTexts()` gerçekten çizilen metinleri listeler; ekrana neyin ulaştığını kontrol etmek için.

## Zaman

Rule'un saati sanaldır: zaman yalnızca test öyle dediğinde ilerler; böylece animasyonlar, gecikmeler ve coroutine'ler her çalıştırmada aynı sonucu verir.

- `rule.waitForIdle()` bekleyen hiçbir şey kalmayana kadar kare çalıştırır: recomposition, yerleşim, yoldaki animasyonlar (çizilirken okunanlar da) ve birkaç saniye içinde vakti gelen zamanlayıcılar.
- `rule.advanceTimeBy(ms)` saati ileri alır ve aradaki kareleri çalıştırır.
- `rule.advanceFrame()` bir kare çalıştırır; `rule.mainClock.advanceTimeBy(ms)`, `rule.advanceTimeBy(ms)`'nin Compose'daki adıdır.

```kotlin
@Kompozable
fun Toast(show: Boolean) {
    var visible by remember { mutableStateOf(false) }
    LaunchedEffect(show) {
        if (show) {
            visible = true
            delay(2000)
            visible = false
        }
    }
    if (visible) Text("Saved")
}

@Test
fun toastGoesAwayAfterTwoSeconds() {
    val rule = KompozTestRule { Toast(true) }
    rule.onNodeWithText("Saved").assertExists()
    rule.mainClock.advanceTimeBy(2100)
    rule.onNodeWithText("Saved").assertDoesNotExist()
}
```

`withBackground`'a verilen işler gerçek thread'lerde çalışır; `waitForIdle` bitmelerini bekler.

## Testin gösterdiğini değiştirmek

İçerik lambda'sı testin sahip olduğu durumu okuyabilir; böylece test onu adımlar arasında değiştirebilir:

```kotlin
@Test
fun showsTheNewName() {
    val name = mutableStateOf("Ada")
    val rule = KompozTestRule { Text("Hello, ${name.value}") }
    rule.onNodeWithText("Hello, Ada").assertExists()
    name.value = "Grace"
    rule.waitForIdle()
    rule.onNodeWithText("Hello, Grace").assertExists()
}
```

`rule.host()` host'u verir; platformun değiştireceği şeyleri değiştirmek için: `setSystemDarkTheme(true)`, `setLayoutDirection(LayoutDirection.Rtl)`, `setStrings(Strings.turkish())`. Rule kurucusunun ikinci argümanı pencerenin dp cinsinden boyutudur, varsayılanı `{400, 800}`.

## Neyi test etmeli

- **Durumsuz composable'lar** en kolayıdır: veriyi verin, gösterileni kontrol edin, olayların geri çağrıldığını kontrol edin. Durumu yukarı taşımak (bkz. [Durum](state.md#durumu-yukarı-taşıma)) arayüzünüzün çoğunu bu şekilde test edilebilir kılar.
- **ViewModel'lar** ve diğer mantık sade C++ nesneleridir: onları hiç rule olmadan test edin.
- `ViewModel`'larıyla birlikte **ekranlar**: rule onları uygulamanın yapacağı gibi kompoze eder.

## Ekran görüntüleri

Bir ekranın nasıl göründüğünü kontrol etmek için gerçek uygulamayı bir betikle ekransız çalıştırın ve kaydettiği resimleri karşılaştırın; bkz. [Araçlar](tools.md#betikli-çalıştırmalar-ve-ekran-görüntüleri).

## Ayrıca bakın

- [Erişilebilirlik](accessibility.md): bulucuların kullandığı semantik.
- [Mimari](architecture.md): kodu test etmesi kolay olacak biçimde yapılandırmak.
- [Araçlar](tools.md): gerçek uygulamanın betikli çalıştırmaları.
