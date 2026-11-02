#pragma once

// The block form Compose writes: the block after the call, in braces.
//
//   #include <kompoz/kompoz.hpp>
//   #include <kompoz/blocks.hpp>  // last: it makes macros of the composables' names
//
//   Column(Modifier().padding(16), verticalArrangement = Arrangement::spacedBy(8)) {
//       Text("İlk");
//       Row() { Text("Yan yana"); };
//       Box(Modifier().size(24).background(colors::Red)) {};  // (no content: an empty block)
//   };
//
// Each composable taking a block becomes a macro: Column(…) { … }; is Column(…) with the braces as
// its block, a lambda capturing by value ([=]). Mind the parentheses, even empty, and the
// semicolon after the closing brace. A block that has to capture something by reference is given
// as the composable's last argument instead, with the name in parentheses so the macro leaves it
// alone: (Column)(modifier, [=, &log] { … }).
//
// Macros know no namespaces: in a file including this header, Column( always means the
// composable, so it isn't for a file that has other things called so.

#include "kompoz.hpp"

#define KOMPOZ_BLOCK(composable, ...) ::kompoz::composable.withBlock(__VA_ARGS__) + [=]()

#define Box(...) KOMPOZ_BLOCK(Box __VA_OPT__(, ) __VA_ARGS__)
#define Column(...) KOMPOZ_BLOCK(Column __VA_OPT__(, ) __VA_ARGS__)
#define Row(...) KOMPOZ_BLOCK(Row __VA_OPT__(, ) __VA_ARGS__)
