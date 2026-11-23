# Changes

## 0.3.0

- Its version is the plugin for CLion and Android Studio's (from 0.1.1 to 0.3.0 for that): the two are released
  together, from one tag.

## 0.1.1

- Kompoz's icon.

## 0.1.0

- C++ sources with Kotlin declarations (`@Kompozable`, `fun`, `val`, `data class`, ...) shown as Kompoz C++: the
  Kotlin highlighted as Kotlin (a grammar of its own, and kompozc's semantic tokens), the C++ as C++; the C/C++ and
  clangd extensions leave such files alone. Plain C++ stays C++.
- `kompozc --lsp` started for them, found in the `kompoz.kompozcPath` setting, `KOMPOZC`, a build in the workspace or
  on `PATH`; `kompoz.clangdPath` and `kompoz.clangdArguments` for the clangd behind it; a command to restart it.
