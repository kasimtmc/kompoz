#pragma once

// What kompozc's translations of @Composable functions call for Kotlin's own features: string
// templates, null, ranges, `in`, `?:`, `!!`, a state's value. Code written by hand has no need
// of it.

#include "runtime.hpp"

#include <charconv>
#include <cmath>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace kompoz::kt {

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

// Kotlin's null: an empty optional, an empty function, a null pointer.
struct Null {
    template <class T>
    operator std::optional<T>() const { return std::nullopt; }
    template <class F>
    operator std::function<F>() const { return nullptr; }
    template <class T>
    operator T*() const { return nullptr; }
};
inline constexpr Null null{};

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
    else return (s.value);
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

// list.size, text.length (a text's characters, not its UTF-8 bytes), or the size of what has one.
template <class C>
int size(const C& c) {
    if constexpr (std::is_convertible_v<const C&, std::string_view>) {
        int n = 0;
        for (char ch : std::string_view(c)) n += (static_cast<unsigned char>(ch) & 0xC0) != 0x80;
        return n;
    } else if constexpr (requires { c.size(); }) {
        return int(c.size());
    } else if constexpr (requires { c.size; }) {
        return int(c.size);
    } else {
        return int(c.length);
    }
}

template <class A, class B>
std::pair<A, B> to(A a, B b) {
    return {std::move(a), std::move(b)};
}

}  // namespace kompoz::kt
