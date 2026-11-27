# Resources

Files your app ships (fonts, images, vectors), the library's own texts and how they follow the system language, and the platform services an app uses: the clipboard, haptics and opening links.

## Assets

Kompoz has no Android-style resource compiler. An app's files are *assets*: files kept next to the program and read by a relative path such as `"images/cover.jpg"`.

Where they go depends on the platform:

| Platform | Where assets are |
|---|---|
| Linux, Windows | Next to the executable. Copy them there in your CMake build. |
| Android | In the APK's `assets/`, from the folders your Gradle project lists. |
| Web | In the page's `.data` file, packed with Emscripten's `--preload-file`. |

Kompoz's own assets, the bundled fonts and the licenses, come from its `assets/` folder; `kompoz_copy_assets(myapp)` copies them next to your desktop binary on every build (see [Setup](setup.md)). Copy your app's own folder the same way:

```cmake
add_custom_command(TARGET myapp POST_BUILD
                   COMMAND ${CMAKE_COMMAND} -E copy_directory ${CMAKE_CURRENT_SOURCE_DIR}/assets $<TARGET_FILE_DIR:myapp>)
```

On Android, add both folders to the Gradle project's asset directories, as the showcase's `app/build.gradle.kts` does; for the web, add both to the `--preload-file` link options, as the showcase's `CMakeLists.txt` does.

## Images

`imageResource("images/cover.jpg")` loads an image asset once and keeps it; `painterResource` gives the same as a `Painter`. JPEG, PNG, WebP and BMP are read (on the web, PNG and BMP; use `AsyncImage` for the others there).

```kotlin body
Image(imageResource("images/cover.jpg"), contentDescription = "Cover", modifier = Modifier.size(120.dp), contentScale = ContentScale.Crop)
```

`AsyncImage` loads assets and web images alike, at the size they are shown, off the UI thread. See [Graphics](graphics.md#images).

## Vectors

`vectorResource("vectors/logo.xml")` reads an Android vector drawable asset, so vectors exported for Android work as they are:

```kotlin body
Icon(vectorResource("vectors/heart.xml"), contentDescription = "Favorite")                                           // tinted
Image(rememberVectorPainter(vectorResource("vectors/logo.xml")), contentDescription = "Logo", modifier = Modifier.size(64.dp))  // in its own colors
```

For a single path, `ImageVector.fromSvgPath("M12,2…")` takes SVG path data directly.

## Fonts

`FontFamily.fromFiles` makes a family from font assets; see [Text](text.md#fonts). The bundled Noto Sans and the licenses must ship with every app, which `kompoz_copy_assets` and the Android and web setups above take care of.

## The library's own texts

Some components show or announce texts of their own: a text field's Cut, Copy and Paste, "More options" on an overflow button, the date picker's month names, "Selected" read out by screen readers. These are in `Strings`, which `LocalStrings` provides, like Compose's string resources.

- Kompoz has them in English (`Strings.english()`, the default) and Turkish (`Strings.turkish()`).
- The host picks them by the system's preferred language: Turkish on a Turkish system, English otherwise. `KOMPOZ_LANGUAGE=tr` or `en` decides instead.
- An app provides its own, whole or changed, with `KompozitionLocalProvider`:

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

For another language, fill in a `Strings` of your own, including its `datePicker` names, and provide it the same way. `Strings.forLanguage("tr_TR.UTF-8")` returns the Turkish strings for any Turkish locale name and English for the rest.

`DatePickerOptions.strings` and `TimePickerOptions`' labels, when set, win over `LocalStrings`.

### Your app's own texts

Kompoz doesn't translate your app's texts. A simple approach that fits the same model is a struct of your texts per language, provided through a `KompozitionLocal` of your own and picked by `LocalStrings.current().language`:

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

## Platform services

Services that composables get inside a composition and keep for callbacks, as Compose's `LocalUriHandler` and friends:

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

| Service | What it does |
|---|---|
| `uriHandler().openUri(uri)` | Opens a link: one of the app's own deep links in the app, `http(s)` in the browser, others (`mailto:`, `tel:`) in the app that takes them. |
| `LocalClipboardManager.current.setText(text)`, `getText()` | The system clipboard's text. `clipboard()` is Compose 1.8's coroutine version, with all the clipboard holds: text, HTML, images, files ([Receiving content](gestures.md#receiving-content)). |
| `hapticFeedback().performHapticFeedback(type)` | A short vibration, as the system does it: Android's, the browser's on the web, nothing on desktops. |
| `LocalSoftwareKeyboardController.current.hide()`, `show()` | Hides or shows the focused field's soft keyboard. |
| `LocalFocusManager.current.clearFocus()` | Takes focus away (see [Gestures](gestures.md#focus)). |

## Files

Files are opened and saved through the system's own dialogs with the API of FileKit, Compose Multiplatform's file picker (Compose has none). A launcher is remembered in the composition; `launch()` shows its dialog over the app's window, and its block is called with what was chosen, or `null` when the dialog was closed without a choice:

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
            Button(onClick = { open.launch() }) { Text("Open") }
            Button(onClick = { save.launch(suggestedName = "notes", defaultExtension = "txt") }) { Text("Save") }
        }
    }
}
```

`rememberFilePickerLauncher` takes the `type` of files offered and a `mode`. The type is `FileKitType.File("pdf", "docx")` (any file with no extensions), or `Image`, `Video` or `ImageAndVideo`, which come from the photo picker on Android. The modes:

- `FileKitMode.Single` gives a `PlatformFile?`.
- `Multiple(maxItems = 10)` gives a `List<PlatformFile>?`.
- `SingleWithState` / `MultipleWithState` give a `FileKitPickerState` as the files come: `Started`, `Progress`, then `Completed` with the `result`; or `Cancelled`, `Failed`.

The other parameters:

- `directory`: where the dialog opens.
- `dialogSettings = FileKitDialogSettings(title = …, macOS = FileKitMacOSSettings(canCreateDirectories = false))`: the dialog's title and macOS's settings.
- `onError = { e -> … }`: called where the dialog can't be shown.

`rememberDirectoryPickerLauncher` picks a folder. `rememberFileSaverLauncher`'s `launch(suggestedName, defaultExtension, allowedExtensions, directory)` gives the file to write, not written yet. In a coroutine, `FileKit.openFilePicker(…)`, `openFileSaver(…)` and `openDirectoryPicker()` give the same back, and throw a `FileKitDialogException` where the dialog can't be shown:

```kotlin body
#! var count by remember { mutableStateOf(0) }
#! val scope = rememberCoroutineScope()
scope.launch {
    val folder = FileKit.openDirectoryPicker()
    if (folder != null) count = withContext(Dispatchers.IO) { folder.list().count { it.isRegularFile() } }
}
```

A `PlatformFile` tells its `name`, `extension`, `nameWithoutExtension`, `path`, `size()`, `exists()`, `isDirectory()`, `mimeType()`, `lastModified()` and `parent()`, and reaches others with `resolve("a/b.txt")` or `/`. `readBytes()`, `readString()`, `write(bytes)` (or another file), and `writeString(text)` are suspend functions that run on a worker thread. `list()`, `copyTo`, `createDirectories()` and `delete()` run where they are called; put them in `withContext(Dispatchers.IO) { }` for big folders. `bookmarkData()` keeps a file to open again in a later run (`PlatformFile.fromBookmarkData(bytes)`); on Android, it also keeps the app's access to it.

| Platform | The dialogs |
|---|---|
| Linux | The desktop portal's (`org.freedesktop.portal.FileChooser`: GNOME's, KDE's), over the app's window; zenity's or kdialog's where there is no portal (`KOMPOZ_FILE_DIALOG=zenity` picks one). |
| Windows | The common item dialogs (`IFileOpenDialog`, `IFileSaveDialog`), owned by the window. |
| macOS | `NSOpenPanel` and `NSSavePanel`, as sheets of the window; the title is the panel's message. |
| Android | The system's document picker (`ACTION_OPEN_DOCUMENT`, `ACTION_CREATE_DOCUMENT`, `ACTION_OPEN_DOCUMENT_TREE`), and the photo picker for images and videos on Android 13+. A file is a `content://` URI, read and written like any other file. The app's manifest declares `dev.kompoz.KompozFiles` (see [Setup](setup.md#android)). |
| Web | The browser's file input. The files chosen are copied into the page's virtual file system. There is no save dialog: `FileKit.download(bytes, fileName)` saves through the browser's downloads. |

## See also

- [Graphics](graphics.md): images and vectors in depth.
- [Text](text.md): fonts and text styles.
- [Setup](setup.md): building and packaging with the assets.
