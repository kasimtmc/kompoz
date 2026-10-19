# Contributing to Kompoz

**English** · [Türkçe](CONTRIBUTING.tr.md)

Thank you for helping. Bug reports, fixes, new components and ports are all welcome. The one platform no maintainer
can work on is Apple's: if you have a Mac, the [macOS and iOS](#macos-and-ios) section is for you.

## Before you start

- For a bug, open an issue with what you did, what you expected and what happened, the platform and, if you can, a
  few lines of code that show it.
- For a larger change (a new component, a platform, an API that differs from Compose), open an issue first to agree
  on the shape. Kompoz follows Jetpack Compose's API and behavior: a change that keeps to Compose is easier to
  accept than one that departs from it.

## Building and testing

See [Building](README.md#building) in the README. Every change should keep these passing:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/kompoz_tests                 # the headless tests (all platforms)
./build/kompoz_platform_tests        # Linux and Windows: the platform code that needs no window
python3 scripts/docs.py --check      # when you change the guide: its C++ examples compile
```

CI builds every platform (Linux with GCC and Clang and sanitizers, Windows, macOS, Android, the web) on each pull
request, so you don't need all the toolchains yourself.

## Making a change

- **Tests with the change.** A fix comes with a test that fails without it; a feature with tests of what it does.
  `kompoz::testing::ComposeTestRule` runs compositions without a window (see [Testing](docs/guide/en/testing.md)).
- **Match the code around it**: its naming, its comments, its formatting. Don't reformat code you aren't changing.
- **One change per commit**, with a message that says what changed and why: a short first line, then the details.
- **Documentation in both languages.** The guide (`docs/guide/en`, `docs/guide/tr`), the READMEs and the changelogs
  are in English and Turkish; if you can't write one of them, say so in the pull request and it will be added.
- **Keep the API status page current** (`docs/components.md` and `components.tr.md`) when you add or change an API
  that Compose has.

## macOS and iOS

CI builds Kompoz on macOS, runs its tests and runs an app there in a real window (Cocoa, Metal), but nobody has used
it by hand on a Mac yet, and it hasn't been built for iOS. What is needed:

- Using the showcase on a Mac and fixing what doesn't work (input, text input, fonts, the Retina scale, dark mode).
- An iOS build: an Xcode project or CMake setup, like the Android one in `examples/showcase/android`.
- Screen readers: a bridge from the semantics tree (`ComposeHost::semanticsTree`) to NSAccessibility (macOS) and
  UIAccessibility (iOS), as `platform/sdl3/atspi_bridge.cpp` (Linux) and `uia_bridge.cpp` (Windows) do, with tests
  like theirs in `tests/platform_tests*.cpp`.

## License

Kompoz is licensed under the [Apache License 2.0](LICENSE). By contributing you agree that your contribution is
licensed under it too.
