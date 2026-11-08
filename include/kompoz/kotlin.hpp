#pragma once

// What kompozc's translations of @Composable functions call for Kotlin's own features: string
// templates, null, ranges, `in`, `?:`, `!!`, a state's value. Code written by hand has no need
// of it.

#include "coroutines.hpp"
#include "named_members.hpp"
#include "runtime.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <coroutine>
#include <cmath>
#include <functional>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <vector>

// a.b in Kotlin: a's property b, which is a member function here (listState.firstVisibleItemIndex is
// firstVisibleItemIndex()) or a field (a data class's). (A macro: a member's name can't be passed.)
#define KOMPOZ_KT_GET(object, name)                                          \
    ::kompoz::kt::detail::get(                                               \
        [](auto&& kompoz_o) -> decltype(auto) {                              \
            if constexpr (requires { kompoz_o.name(); }) return kompoz_o.name(); \
            else return (kompoz_o.name);                                     \
        },                                                                   \
        object)

namespace kompoz::kt {

namespace detail {
template <class F, class T>
decltype(auto) get(F&& f, T&& object) {
    return f(std::forward<T>(object));
}
}  // namespace detail

// ---- Strings: "Count: $count" ----

namespace detail {

inline void appendUtf8(std::string& out, char32_t c) {
    if (c < 0x80) {
        out += char(c);
    } else if (c < 0x800) {
        out += char(0xC0 | (c >> 6));
        out += char(0x80 | (c & 0x3F));
    } else if (c < 0x10000) {
        out += char(0xE0 | (c >> 12));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    } else {
        out += char(0xF0 | (c >> 18));
        out += char(0x80 | ((c >> 12) & 0x3F));
        out += char(0x80 | ((c >> 6) & 0x3F));
        out += char(0x80 | (c & 0x3F));
    }
}

// A Float or Double as Kotlin writes it: the shortest form that reads back, with ".0" on whole
// numbers (1.0, 2.5, 1.0E10).
template <class F>
void appendFloat(std::string& out, F v) {
    if (std::isnan(v)) {
        out += "NaN";
        return;
    }
    if (std::isinf(v)) {
        out += v < 0 ? "-Infinity" : "Infinity";
        return;
    }
    char buf[64];
    auto [end, ec] = std::to_chars(buf, buf + sizeof buf, v);
    std::string s(buf, end);
    if (s.find_first_of(".eEn") == std::string::npos) s += ".0";
    out += s;
}

template <class T>
void append(std::string& out, const T& v) {
    using D = std::decay_t<T>;
    if constexpr (std::is_same_v<D, bool>) {
        out += v ? "true" : "false";
    } else if constexpr (std::is_same_v<D, char32_t>) {
        appendUtf8(out, v);
    } else if constexpr (std::is_same_v<D, char>) {
        out += v;
    } else if constexpr (std::is_floating_point_v<D>) {
        appendFloat(out, v);
    } else if constexpr (std::is_integral_v<D>) {
        out += std::to_string(v);
    } else if constexpr (std::is_convertible_v<const T&, std::string_view>) {
        out += std::string_view(v);
    } else if constexpr (requires { v.has_value(); *v; }) {  // a nullable: its value, or "null"
        if (v.has_value()) append(out, *v);
        else out += "null";
    } else if constexpr (requires { v.first; v.second; }) {  // a Pair: (a, b)
        out += '(';
        append(out, v.first);
        out += ", ";
        append(out, v.second);
        out += ')';
    } else if constexpr (requires { std::begin(v); std::end(v); }) {  // a list: [a, b]
        out += '[';
        bool any = false;
        for (const auto& e : v) {
            if (any) out += ", ";
            any = true;
            append(out, e);
        }
        out += ']';
    } else if constexpr (requires { v.toString(); }) {
        out += v.toString();
    } else {
        static_assert(sizeof(T) == 0, "this value has no text form: give its type a toString()");
    }
}

}  // namespace detail

template <class... T>
std::string str(const T&... parts) {
    std::string out;
    (detail::append(out, parts), ...);
    return out;
}

// ---- null ----

// Kotlin's null: an empty optional, an empty function. (No pointer: a std::string would take it
// for a const char*.)
struct Null {
    template <class T>
    operator std::optional<T>() const { return std::nullopt; }
    template <class F>
    operator std::function<F>() const { return nullptr; }
    static constexpr bool kompozNull = true;  // (contentDescription = null: what takes none says so)
};
inline constexpr Null null{};

// Kotlin's Unit as a value: LaunchedEffect(Unit) { ... }.
struct UnitValue {
    bool operator==(const UnitValue&) const = default;
};
inline constexpr UnitValue Unit{};

template <class T>
bool isNull(const T& v) {
    if constexpr (requires { v.has_value(); }) return !v.has_value();
    else if constexpr (std::is_pointer_v<T>) return v == nullptr;
    else if constexpr (requires { static_cast<bool>(v); }) return !static_cast<bool>(v);
    else return false;
}

// x!!: the value of a nullable.
template <class T>
decltype(auto) notNull(T&& v) {
    if constexpr (requires { v.has_value(); *v; }) return *std::forward<T>(v);
    else return std::forward<T>(v);
}

// a ?: b, b evaluated only when a is null.
template <class T, class F>
auto elvis(const T& a, F&& orElse) {
    if constexpr (requires { a.has_value(); *a; }) {
        using V = std::decay_t<decltype(*a)>;
        if (a.has_value()) return V(*a);
        return V(orElse());
    } else {
        if (!isNull(a)) return a;
        return T(orElse());
    }
}

// ---- A state's value: count (delegated) or count.value ----

template <class S>
decltype(auto) value(const S& s) {
    if constexpr (requires { s.get(); }) return s.get();
    else if constexpr (requires { s.value(); }) return s.value();
    else if constexpr (requires { s.value; }) return (s.value);
    else return (s);  // (produceState's value: delegated to as it is)
}

// What a `by` delegate reads: a state's value (get()), else the value itself (a Transition's animateColor
// gives a Color, whose own value() is its packed bits, not what `by` means).
template <class S>
decltype(auto) delegated(const S& s) {
    if constexpr (requires { s.get(); }) return s.get();
    else if constexpr (requires { s.value; }) return (s.value);
    else return (s);
}

// ---- Ranges: 0 until n, 1..10, 10 downTo 1, r step 2, x in r ----

template <class T>
struct Range {
    T first, last;  // both in, as Kotlin's IntRange
    T step = 1;
    bool empty = false;  // (0 until 0 with an unsigned size: no last below the first to say so)

    struct iterator {
        T v, last, step;
        bool done;
        T operator*() const { return v; }
        iterator& operator++() {
            if (step > 0 ? last - v < step : v - last < -step) done = true;
            else v += step;
            return *this;
        }
        bool operator!=(const iterator& o) const { return done != o.done; }
    };
    iterator begin() const { return {first, last, step, isEmpty()}; }
    iterator end() const { return {first, last, step, true}; }
    bool contains(T x) const { return !isEmpty() && (step > 0 ? first <= x && x <= last : last <= x && x <= first); }
    bool isEmpty() const { return empty || (step > 0 ? first > last : first < last); }
};

template <class A, class B>
auto rangeTo(A a, B b) {
    using T = std::common_type_t<A, B>;
    return Range<T>{T(a), T(b), 1};
}
template <class A, class B>
auto until(A a, B b) {
    using T = std::common_type_t<A, B>;
    if (T(b) <= T(a)) return Range<T>{T(a), T(a), 1, true};
    return Range<T>{T(a), T(b) - 1, 1};
}
template <class A, class B>
auto downTo(A a, B b) {
    using T = std::common_type_t<A, B>;
    return Range<T>{T(a), T(b), -1};
}
template <class T, class S>
Range<T> step(Range<T> r, S s) {
    r.step = r.step > 0 ? T(s) : -T(s);
    return r;
}

// x in c: a range, a container (find, or contains), a string's part.
template <class C, class X>
bool contains(const C& c, const X& x) {
    if constexpr (requires { c.contains(x); }) {
        return c.contains(x);
    } else if constexpr (std::is_convertible_v<const C&, std::string_view>) {
        return std::string_view(c).find(x) != std::string_view::npos;
    } else {
        for (const auto& v : c)
            if (v == x) return true;
        return false;
    }
}

// list.size, text.length (a text's characters, not its UTF-8 bytes), or the size of what has one: a
// count as an int; a size that is something else (a layout result's Size) as it is.
template <class C>
decltype(auto) size(const C& c) {
    if constexpr (std::is_convertible_v<const C&, std::string_view>) {
        int n = 0;
        for (char ch : std::string_view(c)) n += (static_cast<unsigned char>(ch) & 0xC0) != 0x80;
        return n;
    } else if constexpr (requires { c.size(); }) {
        if constexpr (std::is_arithmetic_v<std::remove_cvref_t<decltype(c.size())>>) return int(c.size());
        else return c.size();
    } else if constexpr (requires { c.size; }) {
        if constexpr (std::is_arithmetic_v<std::remove_cvref_t<decltype(c.size)>>) return int(c.size);
        else return (c.size);
    } else {
        return int(c.length);
    }
}

// a?.f(): f(a) when a isn't null; a nullable of what it gives (nothing, for a Unit f).
template <class T, class F>
auto safe(const T& a, F&& f) {
    using R = decltype(f(notNull(a)));
    if constexpr (std::is_void_v<R>) {
        if (!isNull(a)) f(notNull(a));
    } else if constexpr (requires(R r) { r.has_value(); }) {  // (a nullable already: not wrapped twice)
        if (isNull(a)) return R{};
        return R(f(notNull(a)));
    } else {
        using O = std::optional<std::decay_t<R>>;
        if (isNull(a)) return O{};
        return O(f(notNull(a)));
    }
}

// DisposableEffect(key) { …; onDispose { … } }: what the effect gives back, run when it leaves.
template <class F>
std::function<void()> onDispose(F&& f) {
    return std::function<void()>(std::forward<F>(f));
}

// ---- Coroutines: delay(300) in a LaunchedEffect ----

// In a coroutine, each call is written co_await kt::await((call, kt::VoidMark{})): a suspending one
// (a Task, a TaskOf<T>, an awaiter: Kotlin's suspend functions) is awaited, any other gives its
// value at once. (A void call keeps the built-in comma, so it gives the VoidMark.)
struct VoidMark {};
// The call's result, as a reference: a temporary lives to the end of the full expression, the
// co_await too, so an awaiter that can't be moved (a snackbar's) is awaited where it is.
template <class T>
struct Held {
    T&& value;
};
template <class T>
Held<T> operator,(T&& v, VoidMark) {
    return {std::forward<T>(v)};
}
template <class T>
struct Ready {
    T value;
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    T await_resume() { return std::move(value); }
};
template <>
struct Ready<void> {
    bool await_ready() const noexcept { return true; }
    void await_suspend(std::coroutine_handle<>) const noexcept {}
    void await_resume() const noexcept {}
};
template <class T>
concept Awaitable = requires(T& t) { t.await_ready(); } || requires(T&& t) { std::move(t).operator co_await(); } ||
                    requires(T&& t) { operator co_await(std::move(t)); };
template <class T>
decltype(auto) await(Held<T> held) {
    if constexpr (Awaitable<std::remove_cvref_t<T>>) return std::forward<T>(held.value);  // (T&&: awaited in place)
    else return Ready<std::remove_cvref_t<T>>{std::forward<T>(held.value)};
}
inline Ready<void> await(VoidMark) { return {}; }


inline auto delay(long long milliseconds) { return ::kompoz::delay(std::chrono::milliseconds(milliseconds)); }

// ---- Collections: listOf, mapOf, ... ----

template <class T, class... R>
std::vector<std::decay_t<T>> listOf(T&& first, R&&... rest) {
    return {std::forward<T>(first), std::forward<R>(rest)...};
}
// floatArrayOf(8f, 6f), intArrayOf(1, 2): a vector (what takes a span takes it).
template <class... T>
std::vector<float> floatArrayOf(T... values) {
    return {float(values)...};
}
template <class... T>
std::vector<int> intArrayOf(T... values) {
    return {int(values)...};
}
// listOf(), emptyList(), ... without a type: whichever empty collection it goes to.
struct EmptyCollection {
    template <class T, class A>
    operator std::vector<T, A>() const { return {}; }
    template <class T, class C, class A>
    operator std::set<T, C, A>() const { return {}; }
    template <class K, class V, class C, class A>
    operator std::map<K, V, C, A>() const { return {}; }
};
inline EmptyCollection listOf() { return {}; }
template <class T>
std::vector<T> listOf() {
    return {};
}
template <class... T>
auto mutableListOf(T&&... v) {
    return listOf(std::forward<T>(v)...);
}
template <class T>
std::vector<T> mutableListOf() {
    return {};
}
template <class... T>
auto arrayListOf(T&&... v) {
    return listOf(std::forward<T>(v)...);
}
inline EmptyCollection emptyList() { return {}; }
template <class T>
std::vector<T> emptyList() {
    return {};
}
template <class T, class... R>
std::set<std::decay_t<T>> setOf(T&& first, R&&... rest) {
    return {std::forward<T>(first), std::forward<R>(rest)...};
}
template <class... T>
auto mutableSetOf(T&&... v) {
    return setOf(std::forward<T>(v)...);
}
inline EmptyCollection setOf() { return {}; }
inline EmptyCollection emptySet() { return {}; }
template <class T>
std::set<T> emptySet() {
    return {};
}
template <class K, class V, class... R>
std::map<K, V> mapOf(std::pair<K, V> first, R&&... rest) {
    return {std::move(first), std::forward<R>(rest)...};
}
template <class... T>
auto mutableMapOf(T&&... v) {
    return mapOf(std::forward<T>(v)...);
}
inline EmptyCollection mapOf() { return {}; }
inline EmptyCollection emptyMap() { return {}; }
template <class K, class V>
std::map<K, V> emptyMap() {
    return {};
}

template <class F>
void repeat(int times, F&& action) {
    for (int i = 0; i < times; ++i) action(i);
}
template <class... T>
void println(const T&... v) {
    std::fputs((str(v...) + "\n").c_str(), stdout);
}
template <class A, class B>
    requires requires(const A& a, const B& b) { a < b; }
auto maxOf(const A& a, const B& b) {
    return a < b ? b : a;
}
template <class A, class B>
    requires requires(const A& a, const B& b) { b < a; }
auto minOf(const A& a, const B& b) {
    return b < a ? b : a;
}
template <class F = int>
void require(bool condition, F&& message = 0) {
    if (!condition) throw std::invalid_argument(str(message));
}
template <class F = int>
void check(bool condition, F&& message = 0) {
    if (!condition) throw std::logic_error(str(message));
}
// Kotlin's Nothing: what error() and TODO() give, as they never return. It goes for a value of any
// type: compositionLocalOf<User> { error("…") }, x ?: error("…").
struct Nothing {
    template <class T>
    [[noreturn]] operator T() const {
        throw std::logic_error("kompoz: Nothing has no value");
    }
};
[[noreturn]] inline Nothing error(const std::string& message) { throw std::logic_error(message); }
[[noreturn]] inline Nothing TODO(const std::string& reason = "") { throw std::logic_error("not implemented: " + reason); }

// ---- Members of Kotlin's standard types: kt::trim(text) for text.trim() ----
//
// Each uses the value's own member of the same name where it has one (a Kompoz type's).

#define KOMPOZ_KT_OWN(...)                   \
    if constexpr (requires { c.__VA_ARGS__; }) \
        return c.__VA_ARGS__;                  \
    else

namespace detail {
inline bool space(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
template <class C>
using Element = std::decay_t<decltype(*std::begin(std::declval<const C&>()))>;
}  // namespace detail

template <class C, class F, class... A>
void forEach(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(forEach(f)) for (const auto& v : c) f(v);
}
template <class C, class F, class... A>
void forEachIndexed(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(forEachIndexed(f)) {
        int i = 0;
        for (const auto& v : c) f(i++, v);
    }
}
template <class C, class F, class... A>
auto map(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(map(f)) {
        std::vector<std::decay_t<decltype(f(*std::begin(c)))>> out;
        for (const auto& v : c) out.push_back(f(v));
        return out;
    }
}
template <class C, class F, class... A>
auto mapIndexed(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(mapIndexed(f)) {
        std::vector<std::decay_t<decltype(f(0, *std::begin(c)))>> out;
        int i = 0;
        for (const auto& v : c) out.push_back(f(i++, v));
        return out;
    }
}
template <class C, class F, class... A>
auto filter(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(filter(f)) {
        std::vector<detail::Element<C>> out;
        for (const auto& v : c)
            if (f(v)) out.push_back(v);
        return out;
    }
}
template <class C, class F, class... A>
auto filterNot(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(filterNot(f)) return filter(c, [&](const auto& v) { return !f(v); });
}
template <class C, class... A>
bool isEmpty(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(isEmpty()) return kt::size(c) == 0;
}
template <class C, class... A>
bool isNotEmpty(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(isNotEmpty()) return kt::size(c) != 0;
}
template <class C, class... A>
bool isBlank(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(isBlank()) return std::all_of(std::string_view(c).begin(), std::string_view(c).end(), detail::space);
}
template <class C, class... A>
bool isNotBlank(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(isNotBlank()) return !isBlank(c);
}
template <class C, class... A>
std::string trim(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(trim()) {
        std::string_view v(c);
        while (!v.empty() && detail::space(v.front())) v.remove_prefix(1);
        while (!v.empty() && detail::space(v.back())) v.remove_suffix(1);
        return std::string(v);
    }
}
// """...""".trimIndent(): the first and last lines dropped if blank, the indent all lines share removed.
template <class C, class... A>
std::string trimIndent(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(trimIndent()) {
        std::vector<std::string_view> lines;
        std::string_view v(c);
        for (size_t at = 0;;) {
            size_t nl = v.find('\n', at);
            lines.push_back(v.substr(at, nl == std::string_view::npos ? std::string_view::npos : nl - at));
            if (nl == std::string_view::npos) break;
            at = nl + 1;
        }
        auto blank = [](std::string_view l) { return std::all_of(l.begin(), l.end(), detail::space); };
        if (!lines.empty() && blank(lines.front())) lines.erase(lines.begin());
        if (!lines.empty() && blank(lines.back())) lines.pop_back();
        size_t indent = std::string_view::npos;
        for (auto l : lines)
            if (!blank(l)) indent = std::min(indent, l.find_first_not_of(" \t"));
        std::string out;
        for (size_t i = 0; i < lines.size(); ++i) {
            if (i) out += '\n';
            if (!blank(lines[i])) out += lines[i].substr(indent);
        }
        return out;
    }
}
template <class C, class... A>
std::string uppercase(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(uppercase()) {
        std::string s(c);
        for (char& ch : s)
            if (ch >= 'a' && ch <= 'z') ch = char(ch - 'a' + 'A');
        return s;
    }
}
template <class C, class... A>
std::string lowercase(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(lowercase()) {
        std::string s(c);
        for (char& ch : s)
            if (ch >= 'A' && ch <= 'Z') ch = char(ch - 'A' + 'a');
        return s;
    }
}
template <class C, class... A>
auto toInt(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(toInt()) if constexpr (std::is_arithmetic_v<C>) return int(c);
    else return std::stoi(std::string(c));
}
template <class C, class... A>
auto toLong(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(toLong()) if constexpr (std::is_arithmetic_v<C>) return (long long)(c);
    else return std::stoll(std::string(c));
}
template <class C, class... A>
auto toFloat(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(toFloat()) if constexpr (std::is_arithmetic_v<C>) return float(c);
    else return std::stof(std::string(c));
}
template <class C, class... A>
auto toDouble(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(toDouble()) if constexpr (std::is_arithmetic_v<C>) return double(c);
    else return std::stod(std::string(c));
}
template <class C, class... A>
std::string toString(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(toString()) return str(c);
}
template <class C, class... A>
decltype(auto) first(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(first()) {
        if (std::begin(c) == std::end(c)) throw std::out_of_range("the list is empty");
        return *std::begin(c);
    }
}
template <class C, class... A>
decltype(auto) last(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(last()) {
        if (std::begin(c) == std::end(c)) throw std::out_of_range("the list is empty");
        return *std::prev(std::end(c));
    }
}
template <class C, class... A>
auto firstOrNull(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(firstOrNull()) {
        if (std::begin(c) == std::end(c)) return std::optional<detail::Element<C>>{};
        return std::optional<detail::Element<C>>(*std::begin(c));
    }
}
template <class C, class... A>
auto lastOrNull(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(lastOrNull()) {
        if (std::begin(c) == std::end(c)) return std::optional<detail::Element<C>>{};
        return std::optional<detail::Element<C>>(*std::prev(std::end(c)));
    }
}
template <class C, class... A>
std::string joinToString(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(joinToString(args...)) {
        std::string separator = ", ";
        if constexpr (sizeof...(A) > 0) separator = std::string(std::get<0>(std::forward_as_tuple(args...)));
        std::string out;
        bool any = false;
        for (const auto& v : c) {
            if (any) out += separator;
            any = true;
            out += str(v);
        }
        return out;
    }
}
template <class C, class X, class... A>
bool startsWith(const C& c, const X& x, A&&... args) {
    KOMPOZ_KT_OWN(startsWith(x)) return std::string_view(c).starts_with(std::string_view(x));
}
template <class C, class X, class... A>
bool endsWith(const C& c, const X& x, A&&... args) {
    KOMPOZ_KT_OWN(endsWith(x)) return std::string_view(c).ends_with(std::string_view(x));
}
template <class C, class... A>
std::string substring(const C& c, int from, A&&... args) {
    KOMPOZ_KT_OWN(substring(from, args...)) {
        std::string_view v(c);
        if constexpr (sizeof...(A) > 0) return std::string(v.substr(size_t(from), size_t(int(std::get<0>(std::forward_as_tuple(args...))) - from)));
        else return std::string(v.substr(size_t(from)));
    }
}
template <class C, class... A>
auto reversed(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(reversed()) {
        std::vector<detail::Element<C>> out(std::begin(c), std::end(c));
        std::reverse(out.begin(), out.end());
        return out;
    }
}
template <class C, class... A>
auto sorted(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(sorted()) {
        std::vector<detail::Element<C>> out(std::begin(c), std::end(c));
        std::stable_sort(out.begin(), out.end());
        return out;
    }
}
template <class C, class F, class... A>
auto sortedBy(const C& c, F&& key, A&&... args) {
    KOMPOZ_KT_OWN(sortedBy(key)) {
        std::vector<detail::Element<C>> out(std::begin(c), std::end(c));
        std::stable_sort(out.begin(), out.end(), [&](const auto& a, const auto& b) { return key(a) < key(b); });
        return out;
    }
}
template <class C, class F, class... A>
bool any(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(any(f)) return std::any_of(std::begin(c), std::end(c), f);
}
template <class C, class F, class... A>
bool all(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(all(f)) return std::all_of(std::begin(c), std::end(c), f);
}
template <class C, class F, class... A>
bool none(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(none(f)) return std::none_of(std::begin(c), std::end(c), f);
}
template <class C, class... A>
int count(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(count(args...)) if constexpr (sizeof...(A) == 0) return kt::size(c);
    else return int(std::count_if(std::begin(c), std::end(c), args...));
}
template <class C, class... A>
int indexOf(const C& c, const A&... args) {
    if constexpr (requires { c.indexOf(args...); }) {
        return c.indexOf(args...);
    } else if constexpr (std::is_convertible_v<const C&, std::string_view>) {
        size_t at = std::string_view(c).find(args...);
        return at == std::string_view::npos ? -1 : int(at);
    } else {
        int i = 0;
        for (const auto& v : c) {
            if (((v == args) && ...)) return i;
            ++i;
        }
        return -1;
    }
}
template <class C, class... A>
auto getOrNull(const C& c, int i, A&&... args) {
    KOMPOZ_KT_OWN(getOrNull(i)) {
        if (i < 0 || i >= kt::size(c)) return std::optional<detail::Element<C>>{};
        return std::optional<detail::Element<C>>(*std::next(std::begin(c), i));
    }
}
template <class C, class... A>
auto withIndex(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(withIndex()) {
        std::vector<std::pair<int, detail::Element<C>>> out;
        int i = 0;
        for (const auto& v : c) out.emplace_back(i++, v);
        return out;
    }
}
template <class C, class... A>
auto toList(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(toList()) return std::vector<detail::Element<C>>(std::begin(c), std::end(c));
}
template <class C, class... A>
auto toMutableList(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(toMutableList()) return std::vector<detail::Element<C>>(std::begin(c), std::end(c));
}
template <class T, class F, class... A>
auto let(const T& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(let(f)) return f(c);
}
template <class T, class F, class... A>
T also(const T& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(also(f)) {
        f(c);
        return c;
    }
}
template <class T, class F, class... A>
std::optional<T> takeIf(const T& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(takeIf(f)) return f(c) ? std::optional<T>(c) : std::nullopt;
}
template <class T, class L, class H, class... A>
T coerceIn(T c, L low, H high, A&&... args) {
    KOMPOZ_KT_OWN(coerceIn(low, high)) return c < T(low) ? T(low) : T(high) < c ? T(high) : c;
}
template <class T, class L, class... A>
T coerceAtLeast(T c, L low, A&&... args) {
    KOMPOZ_KT_OWN(coerceAtLeast(low)) return c < T(low) ? T(low) : c;
}
template <class T, class H, class... A>
T coerceAtMost(T c, H high, A&&... args) {
    KOMPOZ_KT_OWN(coerceAtMost(high)) return T(high) < c ? T(high) : c;
}
template <class T, class... A>
int roundToInt(T c, A&&... args) {
    KOMPOZ_KT_OWN(roundToInt()) return int(std::lround(c));
}
// Density's conversions (in a scope that is a Density: a DrawScope, a FlexBox's config): Kompoz's
// lengths are dp already, so a dp is its own "px" (16.dp.toPx() is 16), as are sp's.
template <class T, class... A>
float toPx(T c, A&&... args) {
    KOMPOZ_KT_OWN(toPx()) return float(c);
}
template <class T, class... A>
int roundToPx(T c, A&&... args) {
    KOMPOZ_KT_OWN(roundToPx()) return int(std::lround(float(c)));
}
template <class T, class... A>
float toDp(T c, A&&... args) {
    KOMPOZ_KT_OWN(toDp()) return float(c);
}

template <class C, class... A>
auto take(const C& c, int n, A&&... args) {
    KOMPOZ_KT_OWN(take(n)) {
        if constexpr (std::is_convertible_v<const C&, std::string_view>) return std::string(std::string_view(c).substr(0, size_t(std::max(n, 0))));
        else return std::vector<detail::Element<C>>(std::begin(c), std::next(std::begin(c), std::clamp(n, 0, kt::size(c))));
    }
}
template <class C, class... A>
auto drop(const C& c, int n, A&&... args) {
    KOMPOZ_KT_OWN(drop(n)) {
        if constexpr (std::is_convertible_v<const C&, std::string_view>) {
            std::string_view v(c);
            return std::string(v.substr(std::min(size_t(std::max(n, 0)), v.size())));
        } else {
            return std::vector<detail::Element<C>>(std::next(std::begin(c), std::clamp(n, 0, kt::size(c))), std::end(c));
        }
    }
}
template <class C, class... A>
auto sum(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(sum()) {
        detail::Element<C> total{};
        for (const auto& v : c) total += v;
        return total;
    }
}
template <class C, class F, class... A>
auto sumOf(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(sumOf(f)) {
        std::decay_t<decltype(f(*std::begin(c)))> total{};
        for (const auto& v : c) total += f(v);
        return total;
    }
}
// list.maxOfOrNull { it.width }: the largest of what f gives, null for an empty list.
template <class C, class F, class... A>
    requires std::invocable<F&, detail::Element<C>>
auto maxOfOrNull(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(maxOfOrNull(f)) {
        std::optional<std::decay_t<std::invoke_result_t<F&, detail::Element<C>>>> best;
        for (const auto& e : c)
            if (auto v = f(e); !best || *best < v) best = v;
        return best;
    }
}
template <class C, class F, class... A>
    requires std::invocable<F&, detail::Element<C>>
auto minOfOrNull(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(minOfOrNull(f)) {
        std::optional<std::decay_t<std::invoke_result_t<F&, detail::Element<C>>>> best;
        for (const auto& e : c)
            if (auto v = f(e); !best || v < *best) best = v;
        return best;
    }
}
// list.maxOf { it.width }: the largest of what f gives (an empty list has none: it throws, as Kotlin's).
template <class C, class F, class... A>
    requires std::invocable<F&, detail::Element<C>>
auto maxOf(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(maxOf(f)) {
        auto it = std::begin(c);
        if (it == std::end(c)) throw std::out_of_range("maxOf of an empty collection");
        auto best = f(*it);
        for (++it; it != std::end(c); ++it)
            if (auto v = f(*it); best < v) best = v;
        return best;
    }
}
template <class C, class F, class... A>
    requires std::invocable<F&, detail::Element<C>>
auto minOf(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(minOf(f)) {
        auto it = std::begin(c);
        if (it == std::end(c)) throw std::out_of_range("minOf of an empty collection");
        auto best = f(*it);
        for (++it; it != std::end(c); ++it)
            if (auto v = f(*it); v < best) best = v;
        return best;
    }
}
template <class C, class F, class... A>
auto find(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(find(f)) {
        for (const auto& v : c)
            if (f(v)) return std::optional<detail::Element<C>>(v);
        return std::optional<detail::Element<C>>{};
    }
}
template <class C, class... A>
auto distinct(const C& c, A&&... args) {
    KOMPOZ_KT_OWN(distinct()) {
        std::vector<detail::Element<C>> out;
        for (const auto& v : c)
            if (std::find(out.begin(), out.end(), v) == out.end()) out.push_back(v);
        return out;
    }
}
template <class C, class S, class... A>
std::vector<std::string> split(const C& c, const S& separator, A&&... args) {
    KOMPOZ_KT_OWN(split(separator)) {
        std::string_view v(c), sep(separator);
        std::vector<std::string> out;
        for (size_t at = 0;;) {
            size_t next = sep.empty() ? std::string_view::npos : v.find(sep, at);
            out.emplace_back(v.substr(at, next == std::string_view::npos ? std::string_view::npos : next - at));
            if (next == std::string_view::npos) return out;
            at = next + sep.size();
        }
    }
}
template <class C, class O, class N, class... A>
std::string replace(const C& c, const O& oldValue, const N& newValue, A&&... args) {
    KOMPOZ_KT_OWN(replace(oldValue, newValue)) {
        std::string s(c);
        std::string_view o(oldValue), n(newValue);
        if (o.empty()) return s;
        for (size_t at = 0; (at = s.find(o, at)) != std::string::npos; at += n.size()) s.replace(at, o.size(), n);
        return s;
    }
}

// MutableList's: a std::vector's push_back, insert, erase.
template <class C, class X, class... A>
decltype(auto) add(C& c, X&& x, A&&... args) {
    KOMPOZ_KT_OWN(add(std::forward<X>(x))) {
        c.push_back(std::forward<X>(x));
        return bool(true);
    }
}
template <class C, class X, class... A>
decltype(auto) addAll(C& c, const X& more, A&&... args) {
    KOMPOZ_KT_OWN(addAll(more)) {
        c.insert(c.end(), std::begin(more), std::end(more));
        return std::begin(more) != std::end(more);
    }
}
template <class C, class... A>
decltype(auto) removeAt(C& c, int i, A&&... args) {
    KOMPOZ_KT_OWN(removeAt(i)) {
        auto v = c.at(size_t(i));
        c.erase(c.begin() + i);
        return v;
    }
}
template <class C, class X, class... A>
decltype(auto) remove(C& c, const X& x, A&&... args) {
    KOMPOZ_KT_OWN(remove(x)) {
        auto at = std::find(c.begin(), c.end(), x);
        if (at == c.end()) return false;
        c.erase(at);
        return true;
    }
}

#undef KOMPOZ_KT_OWN

// x.copy(b = 2) on a data class: a copy, changed by `change`.
template <class T, class F>
T copy(const T& value, F&& change) {
    T c = value;
    change(c);
    return c;
}

template <class A, class B>
std::pair<A, B> to(A a, B b) {
    return {std::move(a), std::move(b)};
}

}  // namespace kompoz::kt
