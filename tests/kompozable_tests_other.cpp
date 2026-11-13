// Another source of kompozable_tests: its @Kompozable functions, data classes and vals are used
// from kompozable_tests.cpp as Kotlin calls them from another file of the module (named arguments,
// defaults), with no header.

#include <kompoz/kompoz.hpp>

using namespace kompoz;

data class Sticker(val label: String, val count: Int = 0)

@Kompozable
fun StickerRow(sticker: Sticker, prefix: String = "", suffix: String = "") {
    Text("$prefix${sticker.label}:${sticker.count}$suffix")
}

val StickerGreeting = "selam " + Shout

private val Shout = "!"

// An overload of kompozable_tests.cpp's describe: one name's functions may be in several files.
fun describe(d: Double): String = "ondalık $d"

// An interface of this file, implemented in kompozable_tests.cpp (an object, a data class).
interface Emblem {
    val text: String
    fun shown() = "<$text>"
}

// Its C++ names kompozable_tests.cpp's Kotlin too (a data class): its translation declares it here, and so does the
// language server's view of it (tests/kompozc_lsp_test.py: the file as it is has no diagnostics).
[[maybe_unused]] static std::string padLabel() { return Pad{3, "p"}.label; }
