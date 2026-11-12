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
