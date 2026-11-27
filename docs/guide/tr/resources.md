# Kaynaklar

Uygulamanızla birlikte dağıtılan dosyalar (fontlar, resimler, vektörler), kütüphanenin kendi metinleri ve bunların sistem dilini nasıl izlediği, bir uygulamanın kullandığı platform hizmetleri: pano, dokunsal geri bildirim ve bağlantı açmak.

## Asset'ler

Kompoz'da Android tarzı bir kaynak derleyicisi yoktur. Bir uygulamanın dosyaları *asset*'lerdir: programın yanında tutulan ve `"images/cover.jpg"` gibi göreli bir yolla okunan dosyalar.

Nereye konacakları platforma bağlıdır:

| Platform | Asset'lerin yeri |
|---|---|
| Linux, Windows | Çalıştırılabilir dosyanın yanı. CMake derlemenizde oraya kopyalayın. |
| Android | APK'nın `assets/` klasörü; Gradle projenizin listelediği klasörlerden gelir. |
| Web | Sayfanın `.data` dosyası; Emscripten'in `--preload-file`'ı ile paketlenir. |

Kompoz'un kendi asset'leri, yani paketlenmiş fontlar ve lisanslar, onun `assets/` klasöründen gelir; `kompoz_copy_assets(myapp)` her derlemede bunları masaüstü ikili dosyanızın yanına kopyalar (bkz. [Kurulum](setup.md)). Uygulamanızın kendi klasörünü de aynı şekilde kopyalayın:

```cmake
add_custom_command(TARGET myapp POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:myapp>)
```

Android'de, vitrinin `app/build.gradle.kts` dosyasının yaptığı gibi iki klasörü de Gradle projesinin asset dizinlerine ekleyin; web için, vitrinin `CMakeLists.txt` dosyasının yaptığı gibi ikisini de `--preload-file` bağlama seçeneklerine ekleyin.

## Resimler

`imageResource("images/cover.jpg")` bir resim asset'ini bir kez yükler ve saklar; `painterResource` aynısını bir `Painter` olarak verir. JPEG, PNG, WebP ve BMP okunur (web'de PNG ve BMP; oradaki diğerleri için `AsyncImage`'ı kullanın).

```kotlin body
Image(imageResource("images/cover.jpg"), contentDescription = "Cover", modifier = Modifier.size(120.dp), contentScale = ContentScale.Crop)
```

`AsyncImage`, asset'leri ve web resimlerini aynı biçimde, gösterildikleri boyutta ve arayüz thread'inin dışında yükler. Bkz. [Grafik](graphics.md#resimler).

## Vektörler

`vectorResource("vectors/logo.xml")` bir Android vector drawable asset'ini okur; böylece Android için dışa aktarılmış vektörler olduğu gibi çalışır:

```kotlin body
Icon(vectorResource("vectors/heart.xml"), contentDescription = "Favorite")                                           // renklendirilmiş
Image(rememberVectorPainter(vectorResource("vectors/logo.xml")), contentDescription = "Logo", modifier = Modifier.size(64.dp))  // kendi renkleriyle
```

Tek bir path için `ImageVector.fromSvgPath("M12,2…")` SVG path verisini doğrudan alır.

## Fontlar

`FontFamily.fromFiles` font asset'lerinden bir aile oluşturur; bkz. [Metin](text.md#fontlar). Paketlenmiş Noto Sans ve lisanslar her uygulamayla birlikte dağıtılmalıdır; bunu `kompoz_copy_assets` ile yukarıdaki Android ve web kurulumları halleder.

## Kütüphanenin kendi metinleri

Bazı bileşenler kendilerine ait metinler gösterir ya da seslendirir: bir metin alanının Kes, Kopyala ve Yapıştır'ı, bir taşma butonundaki "More options", tarih seçicinin ay adları, ekran okuyucuların okuduğu "Selected". Bunlar, Compose'un string kaynakları gibi, `LocalStrings`'in sağladığı `Strings` içindedir.

- Kompoz'da bunlar İngilizce (`Strings.english()`, varsayılan) ve Türkçe (`Strings.turkish()`) olarak bulunur.
- Host bunları sistemin tercih ettiği dile göre seçer: Türkçe bir sistemde Türkçe, aksi halde İngilizce. Bunun yerine `KOMPOZ_LANGUAGE=tr` ya da `en` belirleyici olur.
- Bir uygulama, tamamen kendi yazdığı ya da değiştirdiği metinleri `KompozitionLocalProvider` ile sağlar:

```kotlin
#! @Kompozable fun App() {}
@Kompozable
fun Localized() {
    val mine = Strings.english()
    mine.copy = "Duplicate"
    mine.moreOptions = "More"
    KompozitionLocalProvider(LocalStrings provides mine) { App() }
}
```

Başka bir dil için, `datePicker` adları dahil kendi `Strings`'inizi doldurun ve aynı şekilde sağlayın. `Strings.forLanguage("tr_TR.UTF-8")` herhangi bir Türkçe yerel ayar adı için Türkçe metinleri, geri kalanlar için İngilizceyi döndürür.

`DatePickerOptions.strings` ve `TimePickerOptions`'ın etiketleri, atandıklarında `LocalStrings`'e üstün gelir.

### Uygulamanızın kendi metinleri

Kompoz uygulamanızın metinlerini çevirmez. Aynı modele uyan basit bir yaklaşım, dil başına metinlerinizi içeren bir struct'tır; kendi `KompozitionLocal`'ınızla sağlanır ve `LocalStrings.current().language`'a göre seçilir:

```kotlin
data class AppTexts(val greeting: String = "", val signIn: String = "")

fun textsFor(language: String): AppTexts =
    if (language == "tr") AppTexts("Hoş geldiniz", "Giriş yap") else AppTexts("Welcome", "Sign in")

val LocalAppTexts = kompozitionLocalOf { AppTexts() }

#! @Kompozable fun App() {}
@Kompozable
fun Root() {
    KompozitionLocalProvider(LocalAppTexts provides textsFor(LocalStrings.current.language)) { App() }
}
```

## Platform hizmetleri

Composable'ların bir kompozisyon içinde edindiği ve callback'ler için sakladığı hizmetler; Compose'un `LocalUriHandler`'ı ve benzerleri gibi:

```kotlin
@Kompozable
fun ShareRow(link: String) {
    val links = LocalUriHandler.current
    val clip = LocalClipboardManager.current
    val haptics = LocalHapticFeedback.current
    Row {
        TextButton(onClick = { links.openUri(link) }) { Text("Open") }
        TextButton(onClick = {
            clip.setText(link)
            haptics.performHapticFeedback(HapticFeedbackType.Confirm)
        }) { Text("Copy link") }
    }
}
```

| Hizmet | Ne yapar |
|---|---|
| `uriHandler().openUri(uri)` | Bir bağlantı açar: uygulamanın kendi derin bağlantılarından birini uygulamada, `http(s)`'yi tarayıcıda, diğerlerini (`mailto:`, `tel:`) onları karşılayan uygulamada. |
| `LocalClipboardManager.current.setText(text)`, `getText()` | Sistem panosundaki metin. `clipboard()`, Compose 1.8'in coroutine sürümüdür; panonun tuttuğu her şeyle: metin, HTML, resimler, dosyalar ([İçerik alma](gestures.md#içerik-alma)). |
| `hapticFeedback().performHapticFeedback(type)` | Sistemin yaptığı gibi kısa bir titreşim: Android'de Android'inki, web'de tarayıcınınki, masaüstlerinde hiçbir şey. |
| `LocalSoftwareKeyboardController.current.hide()`, `show()` | Odaklanmış alanın yazılım klavyesini gizler ya da gösterir. |
| `LocalFocusManager.current.clearFocus()` | Odağı kaldırır (bkz. [Hareketler](gestures.md#odak)). |

## Dosyalar

Dosyalar, sistemin kendi diyaloglarıyla açılır ve kaydedilir. API, Compose Multiplatform'un dosya seçicisi FileKit'inkidir; Compose'da böyle bir API yoktur. Bir başlatıcı kompozisyonda hatırlanır. `launch()` diyaloğunu uygulamanın penceresinin üstünde gösterir; bloğu seçilenle, diyalog seçim yapılmadan kapatıldığında `null` ile çağrılır:

```kotlin
@Kompozable
fun Notes() {
    var text by remember { mutableStateOf("") }
    val scope = rememberCoroutineScope()
    val open = rememberFilePickerLauncher(type = FileKitType.File("txt", "md")) { file ->
        if (file != null) scope.launch { text = file.readString() }
    }
    val save = rememberFileSaverLauncher { file ->
        if (file != null) scope.launch { file.writeString(text) }
    }
    Column {
        TextField(value = text, onValueChange = { text = it })
        Row {
            Button(onClick = { open.launch() }) { Text("Aç") }
            Button(onClick = { save.launch(suggestedName = "notes", defaultExtension = "txt") }) { Text("Kaydet") }
        }
    }
}
```

`rememberFilePickerLauncher` sunulacak dosyaların türünü (`type`) ve bir kipi (`mode`) alır. Tür `FileKitType.File("pdf", "docx")` olabilir; uzantı verilmezse her dosya sunulur. `Image`, `Video` ya da `ImageAndVideo` da olabilir; Android'de bunlar fotoğraf seçiciden gelir. Kipler:

- `FileKitMode.Single`: bir `PlatformFile?` verir.
- `Multiple(maxItems = 10)`: bir `List<PlatformFile>?` verir.
- `SingleWithState` / `MultipleWithState`: dosyalar geldikçe bir `FileKitPickerState` verir. Sırasıyla `Started`, `Progress`, ardından `result` ile `Completed`; ya da `Cancelled`, `Failed`.

Diğer parametreler:

- `directory`: diyaloğun açıldığı yer.
- `dialogSettings = FileKitDialogSettings(title = …, macOS = FileKitMacOSSettings(canCreateDirectories = false))`: diyaloğun başlığı ve macOS ayarları.
- `onError = { e -> … }`: diyalog gösterilemediğinde çağrılır.

`rememberDirectoryPickerLauncher` bir klasör seçer. `rememberFileSaverLauncher`'ın `launch(suggestedName, defaultExtension, allowedExtensions, directory)`'si yazılacak dosyayı verir; dosya henüz yazılmamıştır. Bir coroutine'de `FileKit.openFilePicker(…)`, `openFileSaver(…)` ve `openDirectoryPicker()` aynısını geri verir. Diyalog gösterilemediğinde bir `FileKitDialogException` fırlatırlar:

```kotlin body
#! var count by remember { mutableStateOf(0) }
#! val scope = rememberCoroutineScope()
scope.launch {
    val folder = FileKit.openDirectoryPicker()
    if (folder != null) count = withContext(Dispatchers.IO) { folder.list().count { it.isRegularFile() } }
}
```

Bir `PlatformFile` şunları söyler: `name`, `extension`, `nameWithoutExtension`, `path`, `size()`, `exists()`, `isDirectory()`, `mimeType()`, `lastModified()` ve `parent()`. Başka dosyalara `resolve("a/b.txt")` ya da `/` ile ulaşılır. `readBytes()`, `readString()`, `write(bytes)` (ya da başka bir dosya) ve `writeString(text)` suspend fonksiyonlardır; bir işçi iş parçacığında çalışırlar. `list()`, `copyTo`, `createDirectories()` ve `delete()` ise çağrıldıkları yerde çalışır; büyük klasörlerde bunları `withContext(Dispatchers.IO) { }` içine koyun. `bookmarkData()` bir dosyayı sonraki bir çalıştırmada yeniden açmak için saklar (`PlatformFile.fromBookmarkData(bytes)`). Android'de uygulamanın o dosyaya erişimini de korur.

| Platform | Diyaloglar |
|---|---|
| Linux | Masaüstü portalınınkiler (`org.freedesktop.portal.FileChooser`: GNOME'unki, KDE'ninki), uygulamanın penceresinin üstünde. Portal yoksa zenity'ninki ya da kdialog'unki kullanılır; `KOMPOZ_FILE_DIALOG=zenity` birini seçer. |
| Windows | Ortak öğe diyalogları (`IFileOpenDialog`, `IFileSaveDialog`); sahibi pencere. |
| macOS | `NSOpenPanel` ve `NSSavePanel`, pencerenin sayfaları olarak. Başlık, panelin mesajıdır. |
| Android | Sistemin belge seçicisi (`ACTION_OPEN_DOCUMENT`, `ACTION_CREATE_DOCUMENT`, `ACTION_OPEN_DOCUMENT_TREE`); Android 13+'da resimler ve videolar için fotoğraf seçici. Bir dosya bir `content://` URI'sidir ve her dosya gibi okunup yazılır. Uygulamanın manifest'i `dev.kompoz.KompozFiles`'ı bildirmelidir (bkz. [Kurulum](setup.md#android)). |
| Web | Tarayıcının dosya girdisi. Seçilen dosyalar sayfanın sanal dosya sistemine kopyalanır. Kaydetme diyaloğu yoktur: `FileKit.download(bytes, fileName)` tarayıcının indirmeleri üzerinden kaydeder. |

## Ayrıca bakın

- [Grafik](graphics.md): resimler ve vektörler ayrıntılı olarak.
- [Metin](text.md): fontlar ve metin stilleri.
- [Kurulum](setup.md): asset'lerle birlikte derlemek ve paketlemek.
