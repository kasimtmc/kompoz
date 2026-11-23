// A third source of kompozable_tests: Kotlin and C++ in C++ namespaces. kompozc's #include of
// kotlin.hpp goes after the file's own, outside its namespaces (in one, the standard library's
// headers it brings would open there), and a C++ enum class, ended by its ;, stays C++.

#include <kompoz/kompoz.hpp>

#include <string>

namespace shop {
namespace {

// Whether kompozc made an enum's Kotlin helpers (its entries' names): Kotlin's enum class has them.
template <class E>
constexpr bool kotlinEnum = requires(E e) { kompoz_kt_property_name(e); };

enum class Aisle { Fruit, Bread, Milk };
static_assert(!kotlinEnum<Aisle>, "C++'s enum class, ended by ;, stays C++");

enum class Rack { Top, Bottom }

data class Basket(val items: Int, val rack: Rack = Rack.Top)

fun basketLabel(basket: Basket): String = "${basket.items} ürün, ${basket.rack}"

static_assert(kotlinEnum<Rack>, "Kotlin's enum class has its entries' names");

}  // namespace

std::string shopLabel() { return basketLabel(Basket{2, Rack::Bottom}) + " / " + std::to_string(int(Aisle::Milk)); }

}  // namespace shop
