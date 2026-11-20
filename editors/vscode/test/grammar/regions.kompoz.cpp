// SYNTAX TEST "source.kompoz-cpp" "Kotlin regions in a C++ source, and the C++ around them"
#include "showcase.hpp"
// <- meta.preprocessor.include.cpp

@Kompozable
// <- meta.embedded.block.kotlin storage.type.annotation.kotlin
fun Greeting(name: String, count: Int = 2) {
// <--- storage.type.function.kotlin
//  ^^^^^^^^ entity.name.function.kotlin
//                                      ^ constant.numeric.decimal.kotlin
    var clicks by remember { mutableStateOf(0) }
//  ^^^ storage.type.variable.kotlin
//      ^^^^^^ variable.other.declaration.kotlin
//                ^^^^^^^^ entity.name.function.call.kotlin
    Text("Hello $name, ${clicks + 1}!", modifier = Modifier.padding(16.dp))
//  ^^^^ entity.name.function.call.kotlin
//       ^^^^^^^ string.quoted.double.kotlin
//              ^ punctuation.definition.template-expression.kotlin
//               ^^^^ variable.other.template.kotlin
//                     ^^ meta.template.expression.kotlin punctuation.definition.template-expression.begin.kotlin
//                                                                  ^^ constant.numeric.decimal.kotlin
    if (clicks > 3) return@Greeting
//  ^^ keyword.control.kotlin
//                  ^^^^^^ keyword.control.kotlin
//                         ^^^^^^^^ entity.name.label.kotlin
    val f = 1.5f; val big = 0xFF_00L; val c = 'x'
//          ^^^^ constant.numeric.decimal.kotlin
//                          ^^^^^^^^ constant.numeric.hex.kotlin
//                                            ^^^ string.quoted.single.kotlin
}
// <- meta.embedded.block.kotlin punctuation.section.block.end.kotlin

class ShrinkIndicationNode final : public IndicationNode {
// <- keyword.other.cpp
// <- - meta.embedded.block.kotlin
public:
// <- keyword.other.cpp
};

data class Point(val x: Int, val y: Int)
// <- meta.embedded.block.kotlin storage.modifier.kotlin
//         ^^^^^ entity.name.type.class.kotlin
val origin = Point(0, 0)
// <- meta.embedded.block.kotlin storage.type.variable.kotlin
class Box<T>(val item: T)
// <- meta.embedded.block.kotlin storage.type.class.kotlin

TEST(greeting_says_hello) {
// <- - meta.embedded.block.kotlin
    int answer = 42;
//  ^^^ keyword.other.cpp
}
