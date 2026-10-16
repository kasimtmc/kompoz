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

```cpp body
Image(imageResource("images/cover.jpg"), "Cover", Modifier().size(120), ContentScale::Crop);
```

`AsyncImage` loads assets and web images alike, at the size they are shown, off the UI thread. See [Graphics](graphics.md#images).

## Vectors

`vectorResource("vectors/logo.xml")` reads an Android vector drawable asset, so vectors exported for Android work as they are:

```cpp body
Icon(vectorResource("vectors/heart.xml"), "Favorite");                                         // tinted
Image(rememberVectorPainter(vectorResource("vectors/logo.xml")), "Logo", Modifier().size(64));  // in its own colors
```

For a single path, `ImageVector::fromSvgPath("M12,2…")` takes SVG path data directly.

## Fonts

`FontFamily::fromFiles` makes a family from font assets; see [Text](text.md#fonts). The bundled Noto Sans and the licenses must ship with every app, which `kompoz_copy_assets` and the Android and web setups above take care of.

## The library's own texts

Some components show or announce texts of their own: a text field's Cut, Copy and Paste, "More options" on an overflow button, the date picker's month names, "Selected" read out by screen readers. These are in `Strings`, which `LocalStrings` provides, like Compose's string resources.

- Kompoz has them in English (`Strings::english()`, the default) and Turkish (`Strings::turkish()`).
- The host picks them by the system's preferred language: Turkish on a Turkish system, English otherwise. `KOMPOZ_LANGUAGE=tr` or `en` decides instead.
- An app provides its own, whole or changed, with `CompositionLocalProvider`:

```cpp
#! void App() {}
void Localized() {
    Strings mine = Strings::english();
    mine.copy = "Duplicate";
    mine.moreOptions = "More";
    CompositionLocalProvider({LocalStrings.provides(mine)}, App);
}
```

For another language, fill in a `Strings` of your own, including its `datePicker` names, and provide it the same way. `Strings::forLanguage("tr_TR.UTF-8")` returns the Turkish strings for any Turkish locale name and English for the rest.

`DatePickerOptions::strings` and `TimePickerOptions`' labels, when set, win over `LocalStrings`.

### Your app's own texts

Kompoz doesn't translate your app's texts. A simple approach that fits the same model is a struct of your texts per language, provided through a `CompositionLocal` of your own and picked by `LocalStrings.current().language`:

```cpp
struct AppTexts {
    std::string greeting, signIn;
    friend bool operator==(const AppTexts&, const AppTexts&) = default;
};

const AppTexts& textsFor(const std::string& language) {
    static const AppTexts en{"Welcome", "Sign in"}, tr{"Hoş geldiniz", "Giriş yap"};
    return language == "tr" ? tr : en;
}

inline const CompositionLocal<AppTexts> LocalAppTexts{AppTexts{}};

#! void App() {}
void Root() {
    CompositionLocalProvider({LocalAppTexts.provides(textsFor(LocalStrings.current().language))}, App);
}
```

## Platform services

Services that composables get inside a composition and keep for callbacks, as Compose's `LocalUriHandler` and friends:

```cpp
void ShareRow(const std::string& link) {
    UriHandler links = uriHandler();
    ClipboardManager clip = clipboardManager();
    HapticFeedback haptics = hapticFeedback();
    Row([&] {
        TextButton([links, link] { links.openUri(link); }, [] { Text("Open"); });
        TextButton([clip, haptics, link] {
            clip.setText(link);
            haptics.performHapticFeedback(HapticFeedbackType::Confirm);
        }, [] { Text("Copy link"); });
    });
}
```

| Service | What it does |
|---|---|
| `uriHandler().openUri(uri)` | Opens a link: one of the app's own deep links in the app, `http(s)` in the browser, others (`mailto:`, `tel:`) in the app that takes them. |
| `clipboardManager().setText(text)`, `getText()` | The system clipboard's text. `clipboard()` is Compose 1.8's coroutine version. |
| `hapticFeedback().performHapticFeedback(type)` | A short vibration, as the system does it: Android's, the browser's on the web, nothing on desktops. |
| `softwareKeyboardController().hide()`, `show()` | Hides or shows the focused field's soft keyboard. |
| `focusManager().clearFocus()` | Takes focus away (see [Gestures](gestures.md#focus)). |

## See also

- [Graphics](graphics.md): images and vectors in depth.
- [Text](text.md): fonts and text styles.
- [Setup](setup.md): building and packaging with the assets.
