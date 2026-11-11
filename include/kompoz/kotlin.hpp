#pragma once

// What kompozc's translations of @Kompozable functions call for Kotlin's own features: string
// templates, null, ranges, `in`, `?:`, `!!`, a state's value. Code written by hand has no need
// of it.

#include "coroutines.hpp"
#include "named_members.hpp"
#include "runtime.hpp"

#include <algorithm>
#include <random>
#include <charconv>
#include <chrono>
#include <coroutine>
#include <cmath>
#include <format>
#include <functional>
#include <limits>
#include <map>
#include <cstdlib>
#include <cstdint>
#include <memory>
#include <tuple>
#include <typeinfo>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

// a.b in Kotlin: a's property b, which is a member function here (listState.firstVisibleItemIndex is
// firstVisibleItemIndex()) or a field (a data class's); `a` may be a pointer to the object (a
// viewModel<T>()'s shared_ptr): Kotlin has no pointers. (A macro: a member's name can't be passed.)
#define KOMPOZ_KT_GET(object, name)                                          \
    ::kompoz::kt::detail::get(                                               \
        [](auto&& kompoz_o) -> decltype(auto) {                              \
            if constexpr (requires { kompoz_o.name(); }) return kompoz_o.name(); \
            else if constexpr (requires { kompoz_o.name; }) return (kompoz_o.name); \
            else if constexpr (requires { kompoz_o.properties().name; })      \
                return (kompoz_o.properties().name); /* a GraphicsLayer's: Compose's layer.alpha */ \
            else                                                             \
                static_assert(::kompoz::kt::detail::noMember<decltype(kompoz_o)>, \
                              "kompoz: no property, member or field named " #name); \
        },                                                                   \
        ::kompoz::kt::deref(object))

// object.name = value (Kotlin's property set: state.targetState = false, paint.alpha = 0.6f): the setter
// where there is one (setName), else the field, a reference a function of the name gives, or a
// GraphicsLayer's properties().name.
#define KOMPOZ_KT_SET(object, name, setter, value)                                                      \
    ::kompoz::kt::detail::set(                                                                           \
        [](auto&& kompoz_o, auto&& kompoz_v) {                                                           \
            if constexpr (requires { kompoz_o.setter(kompoz_v); }) kompoz_o.setter(kompoz_v);            \
            else if constexpr (requires { kompoz_o.name = kompoz_v; }) kompoz_o.name = kompoz_v;         \
            else if constexpr (requires { kompoz_o.name() = kompoz_v; }) kompoz_o.name() = kompoz_v;     \
            else kompoz_o.properties().name = kompoz_v;                                                   \
        },                                                                                               \
        ::kompoz::kt::deref(object), (value))

namespace kompoz::kt {

// A pointer to an object: a raw one, or a smart one (a shared_ptr: get() gives the raw pointer; a
// state's get() gives its value, so a state is not one).
template <class T>
concept PointerLike = std::is_pointer_v<std::remove_cvref_t<T>> ||
                      (requires(const std::remove_cvref_t<T>& x) { *x; x.get(); } &&
                       std::is_pointer_v<std::remove_cvref_t<decltype(std::declval<const std::remove_cvref_t<T>&>().get())>>);

// The object behind a pointer (a viewModel<T>()'s shared_ptr, a raw pointer), or the value itself:
// Kotlin's `a.b` knows no pointers (an optional is not one: a nullable's ?. unwraps it).
template <class T>
decltype(auto) deref(T&& x) {
    if constexpr (requires { x.kompoz_object(); }) return (x.kompoz_object());  // a kompozc class's handle: its object
    else if constexpr (PointerLike<T>) return (*x);
    else if constexpr (requires { x.has_value(); *x; }) return (*x);  // a nullable after Kotlin's smart cast (if (x == null) return): its value
    else return std::forward<T>(x);
}

namespace detail {
template <class T>
inline constexpr bool noMember = false;  // (KOMPOZ_KT_GET's: false, but only once the type is known)
template <class F, class T>
decltype(auto) get(F&& f, T&& object) {
    return f(std::forward<T>(object));
}
template <class F, class T, class V>
void set(F&& f, T&& object, V&& value) {
    f(std::forward<T>(object), std::forward<V>(value));
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
    template <class T>
    operator std::shared_ptr<T>() const { return nullptr; }  // (a handle: OverscrollEffect? = null)
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
    else if constexpr (PointerLike<T>) return (*v);  // (a pointer, a shared_ptr: the object)
    else return std::forward<T>(v);
}

// if (c) { { … } } else null: a lambda or none, as the function type it goes to (an empty one for none).
template <class F>
struct MaybeLambda {
    bool has;
    F f;
    template <class R, class... A>
    operator std::function<R(A...)>() const {
        if (!has) return {};
        return std::function<R(A...)>(f);
    }
};
template <class F>
MaybeLambda<std::decay_t<F>> nullableLambda(bool has, F&& f) {
    return {has, std::forward<F>(f)};
}
// if (c) x else null: x, evaluated only then, or none.
template <class F>
auto nullable(bool has, F&& value) -> std::optional<std::decay_t<std::invoke_result_t<F&>>> {
    if (has) return value();
    return std::nullopt;
}

// A lambda's read-only receiver kept past its block (a graphicsLayer lambda reading a scope's
// progress as it draws, after the scope is gone): a copy where the type can be copied, the pointer
// otherwise; *held is the receiver either way.
template <class T>
struct ReceiverCopy {
    T value;
    const T& operator*() const { return value; }
};
template <class T>
auto heldReceiver(T* p) {
    if constexpr (std::is_copy_constructible_v<T>) return ReceiverCopy<std::remove_const_t<T>>{*p};
    else return p;
}
template <class T>
ReceiverCopy<T> heldReceiver(const ReceiverCopy<T>& held) {  // (a lambda inside one that already holds it: the same copy)
    return held;
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

// x++ / ++x / x-- / --x on a state read through `by`, as a value: the state set, the old or the new value.
template <class S>
auto postIncrement(const S& s) { auto v = s.get(); s.set(v + 1); return v; }
template <class S>
auto preIncrement(const S& s) { auto v = s.get() + 1; s.set(v); return v; }
template <class S>
auto postDecrement(const S& s) { auto v = s.get(); s.set(v - 1); return v; }
template <class S>
auto preDecrement(const S& s) { auto v = s.get() - 1; s.set(v); return v; }

// What a `by` delegate reads: a state's value (get()), else the value itself (a Transition's animateColor
// gives a Color, whose own value() is its packed bits, not what `by` means).
template <class S>
decltype(auto) delegated(const S& s) {
    if constexpr (requires { s.get(); } && !PointerLike<S>) return s.get();  // (not a shared_ptr's)
    else if constexpr (requires { s.value; }) return (s.value);
    else return (s);
}

// ---- Ranges: 0 until n, 1..10, 10 downTo 1, r step 2, x in r ----

template <class T>
struct Range {
    T first, last;  // both in, as Kotlin's IntRange
    T step = 1;
    bool empty = false;  // (0 until 0 with an unsigned size: no last below the first to say so)
    T start() const { return first; }         // Kotlin's ClosedRange: start, endInclusive
    T endInclusive() const { return last; }

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
    } else if constexpr (requires { c.length(); }) {
        return c.length();  // a PathMeasure's: as it is
    } else {
        return int(c.length);
    }
}

// An enum's ordinal: its place among the values.
template <class E>
    requires std::is_enum_v<E>
int ordinal(E e) {
    return static_cast<int>(e);
}

// A map's keys and values (map.keys, map.values), as lists; of anything else, its member of the name.
template <class C>
decltype(auto) keys(const C& c) {
    if constexpr (requires { typename C::key_type; typename C::mapped_type; }) {
        std::vector<typename C::key_type> out;
        for (const auto& [k, v] : c) out.push_back(k);
        return out;
    } else if constexpr (requires { c.keys(); }) {
        return c.keys();
    } else {
        return (c.keys);
    }
}
template <class C>
decltype(auto) values(const C& c) {
    if constexpr (requires { typename C::key_type; typename C::mapped_type; }) {
        std::vector<typename C::mapped_type> out;
        for (const auto& [k, v] : c) out.push_back(v);
        return out;
    } else if constexpr (requires { c.values(); }) {
        return c.values();
    } else {
        return (c.values);
    }
}

// a[k]: a map's element is nullable (Kotlin's Map.get), a list's or a text's the element itself.
template <class C, class K>
decltype(auto) at(C&& c, K&& k) {
    using M = std::remove_cvref_t<C>;
    if constexpr (requires { k.has_value(); *k; } && !requires { typename M::key_type; }) {  // a nullable key after Kotlin's smart cast
        return at(std::forward<C>(c), *k);
    } else if constexpr (requires { typename M::key_type; typename M::mapped_type; c.find(k); }) {
        auto it = c.find(k);
        return it == c.end() ? std::optional<typename M::mapped_type>() : std::optional<typename M::mapped_type>(it->second);
    } else {
        return std::forward<C>(c)[std::forward<K>(k)];
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
// a[k] = v: a's set(k, v) where it has one (a SavedStateHandle's, a state list's), else its element.
template <class C, class K, class V>
void setAt(C&& c, K&& k, V&& v) {
    if constexpr (requires { c.set(k, v); }) c.set(std::forward<K>(k), std::forward<V>(v));
    else c[std::forward<K>(k)] = std::forward<V>(v);
}

// LifecycleResumeEffect(key) { …; onPauseOrDispose { … } } and LifecycleStartEffect's onStopOrDispose: the same.
template <class F>
std::function<void()> onPauseOrDispose(F&& f) {
    return std::function<void()>(std::forward<F>(f));
}
template <class F>
std::function<void()> onStopOrDispose(F&& f) {
    return std::function<void()>(std::forward<F>(f));
}

// "%.1f km · %d dk".format(x, n): Kotlin's String.format, Java's specifiers as std::format's — %d %s %f %x
// %c %%, with a width, a precision, '0' and '-' flags ("%5.1f", "%-8s", "%03d").
namespace detail {
inline std::string javaFormatToStd(std::string_view fmt) {
    std::string out;
    for (size_t i = 0; i < fmt.size(); ++i) {
        char c = fmt[i];
        if (c == '{' || c == '}') {
            out += c;
            out += c;
            continue;
        }
        if (c != '%') {
            out += c;
            continue;
        }
        if (i + 1 < fmt.size() && fmt[i + 1] == '%') {
            out += '%';
            ++i;
            continue;
        }
        size_t j = i + 1;
        std::string flags, width, precision;
        while (j < fmt.size() && (fmt[j] == '-' || fmt[j] == '0' || fmt[j] == '+' || fmt[j] == ' ')) flags += fmt[j++];
        while (j < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[j]))) width += fmt[j++];
        if (j < fmt.size() && fmt[j] == '.') {
            ++j;
            while (j < fmt.size() && std::isdigit(static_cast<unsigned char>(fmt[j]))) precision += fmt[j++];
        }
        char conv = j < fmt.size() ? fmt[j] : 's';
        std::string spec;
        if (flags.find('-') != std::string::npos) spec += '<';
        if (flags.find('+') != std::string::npos) spec += '+';
        if (flags.find('0') != std::string::npos && flags.find('-') == std::string::npos) spec += '0';
        spec += width;
        if (!precision.empty()) spec += "." + precision;
        switch (conv) {
            case 'f': spec += 'f'; break;
            case 'e': spec += 'e'; break;
            case 'x': spec += 'x'; break;
            case 'X': spec += 'X'; break;
            case 'd': case 's': case 'c': case 'b': break;
            default: break;
        }
        out += spec.empty() ? std::string("{}") : "{:" + spec + "}";
        i = j;
    }
    return out;
}
}  // namespace detail
template <class... A>
std::string format(std::string_view fmt, A&&... args) {
    return std::vformat(detail::javaFormatToStd(fmt), std::make_format_args(args...));
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

// What a call gives once awaited, as a type (declared only: for decltype). A suspend fun whose
// result is its expression's, `suspend fun twice(x: Int) = load(x) + load(x)`, is a
// TaskFor<decltype(that expression, each call's awaitedType)>.
template <class A>
auto resumed(A&& awaiter) -> decltype(awaiter.await_resume());
template <class T>
auto awaitedType(Held<T> held) -> decltype(resumed(await(std::move(held))));
void awaitedType(VoidMark);
// A suspend fun's coroutine: Task when it gives nothing, else TaskOf<T>.
template <class T>
using TaskFor = std::conditional_t<std::is_void_v<T>, Task, TaskOf<std::remove_cvref_t<T>>>;


inline auto delay(long long milliseconds) { return ::kompoz::delay(std::chrono::milliseconds(milliseconds)); }

// ---- by lazy { … }: the value made the first time it is read ----

template <class T>
class Lazy {
public:
    explicit Lazy(std::function<T()> make) : state_(std::make_shared<State>(State{std::move(make), std::nullopt})) {}
    const T& get() const {
        if (!state_->value) state_->value.emplace(state_->make());
        return *state_->value;
    }
    bool isInitialized() const { return state_->value.has_value(); }
    bool operator==(const Lazy& o) const { return state_ == o.state_; }

private:
    struct State {
        std::function<T()> make;
        std::optional<T> value;
    };
    std::shared_ptr<State> state_;  // (copies share it: one value)
};
template <class F>
auto lazy(F make) -> Lazy<std::decay_t<std::invoke_result_t<F&>>> {
    return Lazy<std::decay_t<std::invoke_result_t<F&>>>(std::move(make));
}

// A function giving a T, as a type (declared only): what a class's delegate's lambda stands for where its
// type is written (`val doubled by derivedStateOf { count * 2 }`: derivedStateOf(returning<int>())).
template <class T>
std::function<T()> returning();

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
// List(n) { i -> … } and MutableList(n) { i -> … }: n elements, each from its index.
template <class F>
auto List(int n, F&& init) {
    std::vector<std::decay_t<std::invoke_result_t<F&, int>>> out;
    out.reserve(size_t(std::max(n, 0)));
    for (int i = 0; i < n; ++i) out.push_back(init(i));
    return out;
}
template <class F>
auto MutableList(int n, F&& init) {
    return List(n, std::forward<F>(init));
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
// type: kompozitionLocalOf<User> { error("…") }, x ?: error("…").
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
        if constexpr (std::is_convertible_v<const C&, std::string_view>) {  // text.filter { it.isDigit() }: a text (its bytes)
            std::string out;
            for (char ch : std::string_view(c))
                if (f(ch)) out += ch;
            return out;
        } else {
            std::vector<detail::Element<C>> out;
            for (const auto& v : c)
                if (f(v)) out.push_back(v);
            return out;
        }
    }
}
// A character's class, as Kotlin's Char asks (ASCII; a byte of a longer UTF-8 character counts as a letter).
inline bool isDigit(char c) { return c >= '0' && c <= '9'; }
inline bool isLetter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || static_cast<unsigned char>(c) >= 0x80; }
inline bool isLetterOrDigit(char c) { return isLetter(c) || isDigit(c); }
inline bool isWhitespace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f' || c == '\v'; }
inline bool isUpperCase(char c) { return c >= 'A' && c <= 'Z'; }
inline bool isLowerCase(char c) { return c >= 'a' && c <= 'z'; }
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
decltype(auto) first(C&& c, A&&... args) {
    KOMPOZ_KT_OWN(first()) {
        if (std::begin(c) == std::end(c)) throw std::out_of_range("the list is empty");
        // The element itself from a list that stays (event.changes.first().consume()); a copy from one that goes.
        if constexpr (std::is_lvalue_reference_v<C>) return (*std::begin(c));
        else return std::remove_cvref_t<decltype(*std::begin(c))>(*std::begin(c));
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
// Kotlin's x.apply { … }: the block with x as its receiver (kompozc names its type from the constructor,
// Matrix().apply { scale(2f, 2f) }), x given back.
template <class T, class F, class... A>
T apply(T c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(apply(f)) {
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
// More of Kotlin's collection functions (a type's own member of the name wins, as above).
template <class C, class F, class... A>
int indexOfFirst(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(indexOfFirst(f)) {
        int i = 0;
        for (const auto& v : c) {
            if (f(v)) return i;
            ++i;
        }
        return -1;
    }
}
template <class C, class F, class... A>
int indexOfLast(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(indexOfLast(f)) {
        int i = 0, found = -1;
        for (const auto& v : c) {
            if (f(v)) found = i;
            ++i;
        }
        return found;
    }
}
template <class C, class F, class... A>
bool removeIf(C& c, F&& f, A&&... args) {
    if constexpr (requires { c.removeIf(f); }) {  // (a state list's, which says nothing back)
        if constexpr (std::is_void_v<decltype(c.removeIf(f))>) {
            c.removeIf(f);
            return true;
        } else {
            return c.removeIf(f);
        }
    } else {
        auto it = std::remove_if(c.begin(), c.end(), [&](const auto& v) { return bool(f(v)); });
        bool any = it != c.end();
        c.erase(it, c.end());
        return any;
    }
}
template <class C, class F, class... A>
    requires std::is_invocable_v<F&, const detail::Element<C>&>
bool removeAll(C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(removeAll(f)) return removeIf(c, std::forward<F>(f));
}
template <class C, class F, class... A>
auto flatMap(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(flatMap(f)) {
        std::vector<detail::Element<std::decay_t<decltype(f(*std::begin(c)))>>> out;
        for (const auto& v : c)
            for (const auto& w : f(v)) out.push_back(w);
        return out;
    }
}
template <class C, class F, class... A>
auto sortedByDescending(const C& c, F&& key, A&&... args) {
    KOMPOZ_KT_OWN(sortedByDescending(key)) {
        std::vector<detail::Element<C>> out(std::begin(c), std::end(c));
        std::stable_sort(out.begin(), out.end(), [&](const auto& a, const auto& b) { return key(b) < key(a); });
        return out;
    }
}
template <class C, class F, class... A>
auto maxByOrNull(const C& c, F&& key, A&&... args) {
    KOMPOZ_KT_OWN(maxByOrNull(key)) {
        std::optional<detail::Element<C>> best;
        for (const auto& v : c)
            if (!best || key(*best) < key(v)) best = v;
        return best;
    }
}
template <class C, class F, class... A>
auto minByOrNull(const C& c, F&& key, A&&... args) {
    KOMPOZ_KT_OWN(minByOrNull(key)) {
        std::optional<detail::Element<C>> best;
        for (const auto& v : c)
            if (!best || key(v) < key(*best)) best = v;
        return best;
    }
}
template <class C, class F, class... A>
auto mapNotNull(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(mapNotNull(f)) {
        std::vector<std::decay_t<decltype(*f(*std::begin(c)))>> out;
        for (const auto& v : c)
            if (auto r = f(v); !isNull(r)) out.push_back(*r);
        return out;
    }
}
template <class C, class F, class... A>
auto filterIndexed(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(filterIndexed(f)) {
        std::vector<detail::Element<C>> out;
        int i = 0;
        for (const auto& v : c)
            if (f(i++, v)) out.push_back(v);
        return out;
    }
}
template <class C, class F, class... A>
auto distinctBy(const C& c, F&& key, A&&... args) {
    KOMPOZ_KT_OWN(distinctBy(key)) {
        std::vector<detail::Element<C>> out;
        std::vector<std::decay_t<decltype(key(*std::begin(c)))>> keys;
        for (const auto& v : c) {
            auto k = key(v);
            if (std::find(keys.begin(), keys.end(), k) == keys.end()) {
                keys.push_back(k);
                out.push_back(v);
            }
        }
        return out;
    }
}
template <class T, class F, class... A>
std::optional<T> takeUnless(const T& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(takeUnless(f)) return f(c) ? std::nullopt : std::optional<T>(c);
}
template <class C, class F, class... A>
auto groupBy(const C& c, F&& key, A&&... args) {
    KOMPOZ_KT_OWN(groupBy(key)) {
        std::map<std::decay_t<decltype(key(*std::begin(c)))>, std::vector<detail::Element<C>>> out;
        for (const auto& v : c) out[key(v)].push_back(v);
        return out;
    }
}
template <class C, class F, class... A>
auto associateBy(const C& c, F&& key, A&&... args) {
    KOMPOZ_KT_OWN(associateBy(key)) {
        std::map<std::decay_t<decltype(key(*std::begin(c)))>, detail::Element<C>> out;
        for (const auto& v : c) out.insert_or_assign(key(v), v);
        return out;
    }
}
template <class C, class F, class... A>
auto associateWith(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(associateWith(f)) {
        std::map<detail::Element<C>, std::decay_t<decltype(f(*std::begin(c)))>> out;
        for (const auto& v : c) out.insert_or_assign(v, f(v));
        return out;
    }
}
template <class C, class... A>
auto chunked(const C& c, int size, A&&... args) {
    KOMPOZ_KT_OWN(chunked(size)) {
        std::vector<std::vector<detail::Element<C>>> out;
        for (const auto& v : c) {
            if (out.empty() || int(out.back().size()) >= size) out.emplace_back();
            out.back().push_back(v);
        }
        return out;
    }
}
template <class C, class F, class... A>
auto partition(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(partition(f)) {
        std::pair<std::vector<detail::Element<C>>, std::vector<detail::Element<C>>> out;
        for (const auto& v : c) (f(v) ? out.first : out.second).push_back(v);
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
        if constexpr (requires { c.insert(c.end(), std::begin(more), std::end(more)); }) {
            c.insert(c.end(), std::begin(more), std::end(more));
        } else {  // a SnapshotStateList: one by one
            for (const auto& x : more) c.add(x);
        }
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
        if constexpr (requires { c.erase(c.begin()); }) {
            auto at = std::find(c.begin(), c.end(), x);
            if (at == c.end()) return false;
            c.erase(at);
            return true;
        } else {  // a SnapshotStateList: by its index
            size_t i = 0;
            for (auto it = c.begin(); it != c.end(); ++it, ++i)
                if (*it == x) {
                    c.removeAt(i);
                    return true;
                }
            return false;
        }
    }
}
// Pair(a, b) and Triple(a, b, c) as values (a to b is kt::to).
template <class A, class B>
std::pair<std::decay_t<A>, std::decay_t<B>> Pair(A&& a, B&& b) {
    return {std::forward<A>(a), std::forward<B>(b)};
}
template <class A, class B, class C>
std::tuple<std::decay_t<A>, std::decay_t<B>, std::decay_t<C>> Triple(A&& a, B&& b, C&& c) {
    return {std::forward<A>(a), std::forward<B>(b), std::forward<C>(c)};
}
// Kotlin's Random(seed): a source for shuffle(random) and nextInt(n).
struct KtRandom {
    std::mt19937 engine;
    int nextInt(int until) { return int(engine() % unsigned(until)); }
    int nextInt(int from, int until) { return from + nextInt(until - from); }
    float nextFloat() { return std::uniform_real_distribution<float>(0, 1)(engine); }
    bool nextBoolean() { return engine() & 1u; }
};
inline KtRandom Random(unsigned seed) { return {std::mt19937(seed)}; }
// list.shuffle(), list.shuffle(random): in place (a SnapshotStateList through its update).
template <class C, class R>
void shuffleWith(C& c, R& random) {
    if constexpr (requires { c.update([](auto&) {}); }) c.update([&](auto& v) { std::shuffle(v.begin(), v.end(), random); });
    else std::shuffle(c.begin(), c.end(), random);
}
template <class C>
void shuffle(C& c) {
    std::mt19937 random{std::random_device{}()};
    shuffleWith(c, random);
}
template <class C>
void shuffle(C& c, KtRandom random) {
    shuffleWith(c, random.engine);
}
// text.ifEmpty { "none" }, list.ifEmpty { … }, text.ifBlank { … }: the value, or the block's when it is empty (blank).
template <class C, class F, class... A>
auto ifEmpty(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(ifEmpty(f)) {
        using T = std::decay_t<C>;
        if (isEmpty(c)) return T(f());
        return T(c);
    }
}
template <class C, class F, class... A>
auto ifBlank(const C& c, F&& f, A&&... args) {
    KOMPOZ_KT_OWN(ifBlank(f)) {
        using T = std::decay_t<C>;
        if (isBlank(c)) return T(f());
        return T(c);
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

// A destructuring's parts, val (a, b) = x and for ((k, v) in m): a tuple's or a pair's by std::get, a data
// class's (any struct's) fields in their order, up to eight.
namespace detail {
struct AnyField {
    template <class T>
    operator T() const;
};
template <class U, class... I>
constexpr bool bracesWith = requires { U{std::declval<I>()...}; };
template <class U>
constexpr size_t fieldCount() {
    using F = AnyField;
    if constexpr (bracesWith<U, F, F, F, F, F, F, F, F>) return 8;
    else if constexpr (bracesWith<U, F, F, F, F, F, F, F>) return 7;
    else if constexpr (bracesWith<U, F, F, F, F, F, F>) return 6;
    else if constexpr (bracesWith<U, F, F, F, F, F>) return 5;
    else if constexpr (bracesWith<U, F, F, F, F>) return 4;
    else if constexpr (bracesWith<U, F, F, F>) return 3;
    else if constexpr (bracesWith<U, F, F>) return 2;
    else if constexpr (bracesWith<U, F>) return 1;
    else return 0;
}
}  // namespace detail
template <size_t I, class T>
decltype(auto) component(T& t) {
    using U = std::remove_cvref_t<T>;
    if constexpr (requires { std::tuple_size<U>::value; }) {
        return std::get<I>(t);
    } else {
        constexpr size_t n = detail::fieldCount<U>();
        static_assert(n > 0 && n <= 8, "kompoz: a destructuring takes a tuple, a pair or a class of up to eight fields");
        if constexpr (n == 1) { auto&& [a] = t; return std::get<I>(std::forward_as_tuple(a)); }
        else if constexpr (n == 2) { auto&& [a, b] = t; return std::get<I>(std::forward_as_tuple(a, b)); }
        else if constexpr (n == 3) { auto&& [a, b, c] = t; return std::get<I>(std::forward_as_tuple(a, b, c)); }
        else if constexpr (n == 4) { auto&& [a, b, c, d] = t; return std::get<I>(std::forward_as_tuple(a, b, c, d)); }
        else if constexpr (n == 5) { auto&& [a, b, c, d, e] = t; return std::get<I>(std::forward_as_tuple(a, b, c, d, e)); }
        else if constexpr (n == 6) { auto&& [a, b, c, d, e, f] = t; return std::get<I>(std::forward_as_tuple(a, b, c, d, e, f)); }
        else if constexpr (n == 7) { auto&& [a, b, c, d, e, f, g] = t; return std::get<I>(std::forward_as_tuple(a, b, c, d, e, f, g)); }
        else { auto&& [a, b, c, d, e, f, g, h] = t; return std::get<I>(std::forward_as_tuple(a, b, c, d, e, f, g, h)); }
    }
}

// try { … } finally { … }: the block runs when the scope ends, however it ends (a coroutine's cancellation too).
struct Finally {
    explicit Finally(std::function<void()> block) : block_(std::move(block)) {}
    Finally(const Finally&) = delete;
    Finally& operator=(const Finally&) = delete;
    ~Finally() {
        if (block_) block_();
    }

private:
    std::function<void()> block_;
};

// kotlin.math: sin(x), sqrt(x), abs(x), max(a, b), PI… (a Float in, a Float out; an Int in, a Double).
inline constexpr double PI = 3.14159265358979323846;
#define KOMPOZ_KT_MATH(name, fn) \
    template <class T> \
    auto name(T x) { \
        return fn(x); \
    }
KOMPOZ_KT_MATH(sin, std::sin)
KOMPOZ_KT_MATH(cos, std::cos)
KOMPOZ_KT_MATH(tan, std::tan)
KOMPOZ_KT_MATH(asin, std::asin)
KOMPOZ_KT_MATH(acos, std::acos)
KOMPOZ_KT_MATH(atan, std::atan)
KOMPOZ_KT_MATH(sinh, std::sinh)
KOMPOZ_KT_MATH(cosh, std::cosh)
KOMPOZ_KT_MATH(tanh, std::tanh)
KOMPOZ_KT_MATH(sqrt, std::sqrt)
KOMPOZ_KT_MATH(abs, std::abs)
KOMPOZ_KT_MATH(floor, std::floor)
KOMPOZ_KT_MATH(ceil, std::ceil)
KOMPOZ_KT_MATH(round, std::nearbyint)  // (Kotlin's round: halves to the even neighbour)
KOMPOZ_KT_MATH(truncate, std::trunc)
KOMPOZ_KT_MATH(exp, std::exp)
KOMPOZ_KT_MATH(ln, std::log)
KOMPOZ_KT_MATH(log2, std::log2)
KOMPOZ_KT_MATH(log10, std::log10)
#undef KOMPOZ_KT_MATH
template <class T>
T sign(T x) {
    return x > T(0) ? T(1) : x < T(0) ? T(-1) : T(0);
}
template <class A, class B>
auto pow(A x, B y) {
    return std::pow(x, y);
}
template <class A, class B>
auto hypot(A x, B y) {
    return std::hypot(x, y);
}
template <class A, class B>
auto atan2(A y, B x) {
    return std::atan2(y, x);
}
template <class A, class B>
auto max(A a, B b) {
    using C = std::common_type_t<A, B>;
    return C(a) < C(b) ? C(b) : C(a);
}
template <class A, class B>
auto min(A a, B b) {
    using C = std::common_type_t<A, B>;
    return C(b) < C(a) ? C(b) : C(a);
}

// "ab".repeat(3): the string that many times.
inline std::string repeat(const std::string& s, int times) {
    std::string out;
    for (int i = 0; i < times; ++i) out += s;
    return out;
}

// Compose's Rect(left, top, right, bottom) and Rect(topLeft, size): Kompoz's Rect holds x, y, width, height.
constexpr ::kompoz::Rect Rect(float left, float top, float right, float bottom) { return {left, top, right - left, bottom - top}; }
constexpr ::kompoz::Rect Rect(Offset topLeft, Size size) { return {topLeft.x, topLeft.y, size.width, size.height}; }
// Compose's CornerSize(percent: Int) and CornerSize(size: Dp).
constexpr ::kompoz::CornerSize CornerSize(int percent) { return {0, float(percent)}; }
constexpr ::kompoz::CornerSize CornerSize(float dp) { return {dp, 0}; }
// Compose's PaddingValues(all), PaddingValues(horizontal, vertical), PaddingValues(start, top, end, bottom) and the
// named forms (start = …, horizontal = …): Kompoz's PaddingValues holds start, top, end, bottom.
inline ::kompoz::PaddingValues PaddingValues(float all = 0) { return {all, all, all, all}; }
inline ::kompoz::PaddingValues PaddingValues(float horizontal, float vertical) { return {horizontal, vertical, horizontal, vertical}; }
inline ::kompoz::PaddingValues PaddingValues(float start, float top, float end, float bottom) { return {start, top, end, bottom}; }
template <class... A>
    requires(::kompoz::detail::anyNamedArgument<A...>)
::kompoz::PaddingValues PaddingValues(A&&... args) {
    namespace n = ::kompoz::detail::names;
    using P = ::kompoz::detail::Params<n::Start, n::Top, n::End, n::Bottom, n::Horizontal, n::Vertical, n::All>;
    static_assert(P::check<A...>());
    float all = P::get<n::All>(0.f, args...), horizontal = P::get<n::Horizontal>(all, args...), vertical = P::get<n::Vertical>(all, args...);
    return {P::get<n::Start>(horizontal, args...), P::get<n::Top>(vertical, args...), P::get<n::End>(horizontal, args...),
            P::get<n::Bottom>(vertical, args...)};
}

}  // namespace kompoz::kt

namespace kompoz {
// Kotlin's list + list and list + element (a new list), for the lists kotlin.hpp makes (std::vector).
template <class T>
std::vector<T> operator+(const std::vector<T>& a, const std::vector<T>& b) {
    std::vector<T> out = a;
    out.insert(out.end(), b.begin(), b.end());
    return out;
}
template <class T>
std::vector<T> operator+(const std::vector<T>& a, const std::type_identity_t<T>& b) {
    std::vector<T> out = a;
    out.push_back(b);
    return out;
}
namespace kt {
namespace detail {
template <class T, class V>
inline constexpr bool alternativeOf = false;
template <class T, class... A>
inline constexpr bool alternativeOf<T, std::variant<A...>> = (std::is_same_v<T, A> || ...);
template <class V>
concept Sealed = requires(const V& v) { v.kompoz_value; };

// A type with an object that interfaces reach (a kompozc class's handle, an interface's, a data class or an
// object implementing one): its Object type, and it made again from such an object (T::kompoz_from).
template <class T>
concept HasObject = requires { typename T::Object; } && requires(const std::shared_ptr<typename T::Object>& o) { T::kompoz_from(o); };
template <class T>
inline constexpr bool sharedPointer = false;
template <class T>
inline constexpr bool sharedPointer<std::shared_ptr<T>> = true;
// The object behind a value, as a shared pointer (a handle's, a ViewModel's own pointer), or none; a
// nullable's when it has one.
template <class V>
auto sharedOf(const V& v) {
    if constexpr (requires { v.kompoz_shared(); }) return v.kompoz_shared();
    else if constexpr (sharedPointer<V>) return v;
    else if constexpr (requires { v.has_value(); *v; }) return v.has_value() ? sharedOf(*v) : decltype(sharedOf(*v))();
    else return std::shared_ptr<void>();
}
template <class V>
inline constexpr bool sharesObject = !std::is_same_v<decltype(sharedOf(std::declval<const V&>())), std::shared_ptr<void>>;
}  // namespace detail

// x is T: a sealed class's value holding a T (kompozc's sealed classes are variants), an object implementing
// T or of T's class (behind an interface: its object's type asked), or a T itself.
template <class T, class V>
bool is(const V& value) {
    if constexpr (detail::sharesObject<V> && detail::HasObject<T>) {
        auto shared = detail::sharedOf(value);
        return shared && dynamic_cast<const typename T::Object*>(shared.get()) != nullptr;
    } else if constexpr (detail::sharesObject<V> && detail::sharedPointer<T>) {  // a ViewModel's type: its pointer
        auto shared = detail::sharedOf(value);
        return shared && dynamic_cast<const typename T::element_type*>(shared.get()) != nullptr;
    } else if constexpr (!detail::sharesObject<V> && std::is_convertible_v<const V&, T> && !std::is_arithmetic_v<V>) {
        return true;  // (a data class implementing T)
    } else {
    const auto& v = deref(value);
    using U = std::remove_cvref_t<decltype(v)>;
    if constexpr (detail::Sealed<U>) {
        if constexpr (detail::alternativeOf<T, decltype(v.kompoz_value)>) return std::holds_alternative<T>(v.kompoz_value);
        else return std::is_same_v<T, U>;
    } else if constexpr (std::is_same_v<T, U> || std::is_base_of_v<T, U>) {
        return true;
    } else if constexpr (std::is_polymorphic_v<U> && std::is_polymorphic_v<T>) {
        return dynamic_cast<const T*>(&v) != nullptr;
    } else {
        return false;
    }
    }
}
// x as T: the T a sealed class's value holds (it must), the T an interface's object is (made again from
// it), else x cast to T.
template <class T, class V>
decltype(auto) as(const V& value) {
    if constexpr (std::is_same_v<T, V>) {
        return T(value);
    } else if constexpr (detail::sharesObject<V> && detail::HasObject<T>) {
        auto object = std::dynamic_pointer_cast<typename T::Object>(detail::sharedOf(value));
        if (!object) throw std::bad_cast();  // (Kotlin's ClassCastException)
        return T(T::kompoz_from(object));
    } else if constexpr (detail::sharesObject<V> && detail::sharedPointer<T>) {
        auto object = std::dynamic_pointer_cast<typename T::element_type>(detail::sharedOf(value));
        if (!object) throw std::bad_cast();
        return T(object);
    } else if constexpr (!detail::sharesObject<V> && std::is_convertible_v<const V&, T>) {
        return T(value);
    } else {
    const auto& v = deref(value);
    using U = std::remove_cvref_t<decltype(v)>;
    if constexpr (detail::Sealed<U> && detail::alternativeOf<T, decltype(v.kompoz_value)>) return std::get<T>(v.kompoz_value);
    else return static_cast<T>(v);
    }
}
// x as? T: the T, or none when x isn't one.
template <class T, class V>
std::optional<T> asOrNull(const V& value) {
    if (is<T>(value)) return T(as<T>(value));
    return std::nullopt;
}
// A when giving a value that no branch took: Kotlin's compiler rules it out (a sealed class's subclasses all covered).
[[noreturn]] inline void noBranch() { std::abort(); }

// ---- Interfaces: interface Repository { … } ----

// What every interface's object is (kompozc's interface Name: a handle to a Name::Object, an abstract class
// deriving from this, virtually: a class may implement several): its text, and whether it equals
// another (the same object; a data class's: an equal value).
struct InterfaceObject {
    virtual ~InterfaceObject() = default;
    virtual std::string toString() const { return identity("Object", this); }
    virtual bool kompoz_equals(const InterfaceObject& other) const { return this == &other; }
    // Kotlin's Any.toString(): Name@ and where it is.
    static std::string identity(std::string_view name, const void* object) {
        return std::format("{}@{:x}", name, reinterpret_cast<std::uintptr_t>(object) & 0xffffffff);
    }
};
inline bool sameObject(const InterfaceObject* a, const InterfaceObject* b) {
    return a == b || (a && b && a->kompoz_equals(*b));
}
// A handle made from an object (kompoz_from).
struct FromObject {};
// T, named where a template's parameter D is in scope: what it says is looked up when the template is used,
// not where it is written (an interface is declared first and defined later).
template <class D, class T>
struct LaterType {
    using type = T;
};
template <class D, class T>
using Later = typename LaterType<D, T>::type;
}  // namespace kt

// Kotlin's String + anything: "Tıklama: " + count, "x" + 1.5f, the value written as a string template
// writes it (Int.plus(String) has no such form in Kotlin either: only the string on the left).
template <class T>
    requires(!std::is_convertible_v<const T&, std::string_view> && !std::is_same_v<std::remove_cvref_t<T>, char> &&
             requires(const T& v) { ::kompoz::kt::str(v); })
std::string operator+(const std::string& s, const T& value) {
    return s + ::kompoz::kt::str(value);
}

// Kotlin's list - element (the first equal one left out) and list - list (every element of the second).
template <class T>
std::vector<T> operator-(const std::vector<T>& a, const std::type_identity_t<T>& b) {
    std::vector<T> out = a;
    if (auto it = std::find(out.begin(), out.end(), b); it != out.end()) out.erase(it);
    return out;
}
template <class T>
std::vector<T> operator-(const std::vector<T>& a, const std::vector<T>& b) {
    std::vector<T> out;
    for (const T& x : a)
        if (std::find(b.begin(), b.end(), x) == b.end()) out.push_back(x);
    return out;
}

namespace kt {
// Kotlin's `using` infix: a ContentTransform's SizeTransform ((enter togetherWith exit) using SizeTransform()), or a
// keyframe's easing (keyframes { 0.4f at 75 using FastOutSlowInEasing }).
inline ContentTransform infixUsing(const ContentTransform& transform, const std::optional<SizeTransform>& sizeTransform) {
    return transform.withSizeTransform(sizeTransform);
}
inline Keyframe& infixUsing(Keyframe& frame, const Easing& easing) {
    frame.easing = easing;
    return frame;
}
}  // namespace kt
}  // namespace kompoz

// Unit as a key (LaunchedEffect(Unit), LifecycleResumeEffect(Unit)): one value, one hash.
template <>
struct std::hash<kompoz::kt::UnitValue> {
    size_t operator()(const kompoz::kt::UnitValue&) const noexcept { return 0; }
};
