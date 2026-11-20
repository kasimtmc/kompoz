# Kompoz for VS Code

For C++ sources with `@Kompozable` functions written in Kotlin's syntax ([Kompoz](https://github.com/kasimtmc/kompoz)'s `kompozable`):

- **Kompoz C++**: a `.cpp` file with Kotlin declarations in it (`@Kompozable`, `fun`, `val`, `data class`, ...) is shown
  as Kompoz C++. Its Kotlin is highlighted as Kotlin and its C++ as C++, and the C/C++ and clangd extensions, which
  would read the Kotlin as broken C++, leave the file alone. Plain C++ files stay C++.
- **kompozc's language server**: `kompozc --lsp` stands in front of clangd. kompozc's errors in the Kotlin, and the
  C++ compiler's on what it translates to, show on the Kotlin's own lines; clangd's answers (completion, hover, going
  to definitions) work in the C++ around it.

## Setting up

Build Kompoz's project with CMake (`kompozable` targets write the `compile_commands.json` and `kompozc_commands.json`
the server reads). The extension finds `kompozc` in this order:

1. the `kompoz.kompozcPath` setting;
2. `KOMPOZC` in the environment;
3. `build/compiler/kompozc` or `cmake-build-*/compiler/kompozc` in a workspace folder (the newest);
4. `kompozc` on `PATH`.

`kompoz.clangdPath` names the clangd it starts (else `clangd` on `PATH`), and `kompoz.clangdArguments` are given to it.
**Kompoz: Restart kompozc --lsp** starts the server again (after a rebuild of `kompozc`).

The setup guide is at <https://kasimtmc.github.io/kompoz/en/tools.html>.
