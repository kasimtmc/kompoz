// Another source of kompoz_compose_tests: its @Composable functions and data classes are called
// from compose_tests.cpp as Kotlin calls them from another file of the module (named arguments,
// defaults), with no header.

#include <kompoz/kompoz.hpp>

using namespace kompoz;

data class Sticker(val label: String, val count: Int = 0)

@Composable
fun StickerRow(sticker: Sticker, prefix: String = "", suffix: String = "") {
    Text("$prefix${sticker.label}:${sticker.count}$suffix")
}
