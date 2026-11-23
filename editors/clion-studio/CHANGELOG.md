# Changes

## 0.3.0

- Android Studio too: the plugin takes the IDEs from the 2026.2 line on (Android Studio and CLion 2026.2 and later),
  is checked with JetBrains' Plugin Verifier against Android Studio 2026.2.2 as against CLion 2026.3, and was tried in
  Android Studio 2026.2.2. Its package is `kompoz-clion-studio-<version>.zip`.
- Its version is the VS Code extension's: the two are released together, from one tag.

## 0.2.4

- The Kotlin is colored as Kotlin: CLion asks kompozc --lsp for its colors (semantic tokens) in files with Kotlin code,
  which it asks for only in files it doesn't color itself, and colors `@Kompozable` as an annotation.

## 0.2.3

- The description speaks of every JetBrains IDE with language server support, as the plugin is offered for all of
  them (C++ is written in CLion, Rider, Android Studio, ...), not of CLion only.

## 0.2.2

- Kompoz's icon, and plyGround as its vendor: released on GitHub and JetBrains Marketplace.

## 0.2.1

- A file's highlighting is turned off at a write-safe moment (CLion reported an IDE error as such a file opened).

## 0.1.0

- kompozc's language server (`kompozc --lsp`, in front of clangd) for C++ sources with Kotlin code, CLion's own
  highlighting turned off in them; kompozc found in Settings, Tools, Kompoz, in `KOMPOZC`, built in the project or on
  `PATH`.
