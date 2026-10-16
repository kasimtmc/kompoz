#pragma once

// What rememberSaveable writes out so that state outlives the process (Android ends apps in
// the background and brings them back with their saved instance state): a small tree of
// values, encoded as bytes.
//
// A type is saveable when Saver<T> has save and restore, as Compose's Saver: numbers, bools,
// enums, strings, and vectors and optionals of saveable types are; so are the library's
// states (MutableState of a saveable value, scroll, lazy list and pager states, TextFieldState,
// NavController). One of your own:
//
//   template <> struct kompoz::Saver<Point> {
//       static SavedValue save(const Point& p) { return SavedValue::List{p.x, p.y}; }
//       static std::optional<Point> restore(const SavedValue& v) { ... }
//   };

#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace kompoz {

class SavedValue {
public:
    using List = std::vector<SavedValue>;
    using Map = std::map<std::string, SavedValue>;

    SavedValue() = default;  // nothing
    SavedValue(bool b) : v_(b) {}
    template <class T>
        requires(std::is_integral_v<T> && !std::is_same_v<T, bool>)
    SavedValue(T n) : v_(int64_t(n)) {}
    template <class T>
        requires std::is_floating_point_v<T>
    SavedValue(T x) : v_(double(x)) {}
    SavedValue(std::string s) : v_(std::move(s)) {}
    SavedValue(const char* s) : v_(std::string(s)) {}
    SavedValue(List list);
    SavedValue(Map map);

    bool isNull() const { return std::holds_alternative<std::monostate>(v_); }
    std::optional<bool> boolean() const;
    std::optional<int64_t> integer() const;
    std::optional<double> number() const;  // an integer too
    const std::string* string() const { return std::get_if<std::string>(&v_); }
    const List* list() const {
        auto* p = std::get_if<std::shared_ptr<const List>>(&v_);
        return p ? p->get() : nullptr;
    }
    const Map* map() const {
        auto* p = std::get_if<std::shared_ptr<const Map>>(&v_);
        return p ? p->get() : nullptr;
    }
    const SavedValue& operator[](const std::string& key) const;  // nothing if it isn't a map with it

    std::vector<uint8_t> encode() const;
    static std::optional<SavedValue> decode(const std::vector<uint8_t>& bytes);

    friend bool operator==(const SavedValue& a, const SavedValue& b);  // by content

private:
    // Lists and maps live in shared boxes (a saved value doesn't change): no container of this
    // type is made inside its own definition, which libc++ gets wrong (it decides how to move
    // the elements before it knows the type).
    std::variant<std::monostate, bool, int64_t, double, std::string, std::shared_ptr<const List>, std::shared_ptr<const Map>> v_;
};

inline SavedValue::SavedValue(List list) : v_(std::make_shared<const List>(std::move(list))) {}
inline SavedValue::SavedValue(Map map) : v_(std::make_shared<const Map>(std::move(map))) {}

// How a type is written out and read back (specialize it for your own).
template <class T>
struct Saver {};

template <class T>
concept HasSaver = requires(const T& value, const SavedValue& saved) {
    { Saver<T>::save(value) } -> std::same_as<SavedValue>;
    { Saver<T>::restore(saved) } -> std::same_as<std::optional<T>>;
};

template <class T>
    requires(std::is_arithmetic_v<T> || std::is_enum_v<T>)
struct Saver<T> {
    static SavedValue save(const T& value) {
        if constexpr (std::is_enum_v<T>) return int64_t(value);
        else return value;
    }
    static std::optional<T> restore(const SavedValue& saved) {
        if constexpr (std::is_same_v<T, bool>) return saved.boolean();
        else if constexpr (std::is_floating_point_v<T>) {
            auto n = saved.number();
            return n ? std::optional<T>(T(*n)) : std::nullopt;
        } else {
            auto n = saved.integer();
            return n ? std::optional<T>(static_cast<T>(*n)) : std::nullopt;
        }
    }
};

template <>
struct Saver<std::string> {
    static SavedValue save(const std::string& value) { return value; }
    static std::optional<std::string> restore(const SavedValue& saved) {
        return saved.string() ? std::optional(*saved.string()) : std::nullopt;
    }
};

template <class T>
    requires HasSaver<T>
struct Saver<std::vector<T>> {
    static SavedValue save(const std::vector<T>& values) {
        SavedValue::List list;
        for (const T& v : values) list.push_back(Saver<T>::save(v));
        return list;
    }
    static std::optional<std::vector<T>> restore(const SavedValue& saved) {
        if (!saved.list()) return std::nullopt;
        std::vector<T> out;
        for (const SavedValue& v : *saved.list()) {
            auto item = Saver<T>::restore(v);
            if (!item) return std::nullopt;
            out.push_back(std::move(*item));
        }
        return out;
    }
};

template <class T>
    requires HasSaver<T>
struct Saver<std::optional<T>> {
    static SavedValue save(const std::optional<T>& value) { return value ? SavedValue::List{Saver<T>::save(*value)} : SavedValue::List{}; }
    static std::optional<std::optional<T>> restore(const SavedValue& saved) {
        if (!saved.list() || saved.list()->size() > 1) return std::nullopt;
        if (saved.list()->empty()) return std::optional<T>{};
        auto v = Saver<T>::restore(saved.list()->front());
        return v ? std::optional<std::optional<T>>(std::move(v)) : std::nullopt;
    }
};

// A saver given to rememberSaveable in place of Saver<T> (Compose's Saver(save, restore)): for
// a type without one, or saved some other way. listSaver and mapSaver make one from a list or a
// map of saved values (restore: std::nullopt if it can't, then init() makes the value).
//   struct City { std::string name, country; };
//   const auto citySaver = mapSaver<City>(
//       [](const City& c) { return SavedValue::Map{{"name", c.name}, {"country", c.country}}; },
//       [](const SavedValue::Map& m) -> std::optional<City> {
//           auto name = m.at("name").string(), country = m.at("country").string();
//           return name && country ? std::optional(City{*name, *country}) : std::nullopt;
//       });
//   auto& city = rememberSaveable(citySaver, [] { return mutableStateOf(City{"Madrid", "Spain"}); });
// (A saver of T also saves a MutableState<T>, as Compose's stateSaver.)
template <class T>
struct CustomSaver {
    std::function<SavedValue(const T&)> save;
    std::function<std::optional<T>(const SavedValue&)> restore;
};

template <class T>
CustomSaver<T> listSaver(std::function<SavedValue::List(const T&)> save, std::function<std::optional<T>(const SavedValue::List&)> restore) {
    return {[save = std::move(save)](const T& value) { return SavedValue(save(value)); },
            [restore = std::move(restore)](const SavedValue& saved) -> std::optional<T> {
                return saved.list() ? restore(*saved.list()) : std::nullopt;
            }};
}

template <class T>
CustomSaver<T> mapSaver(std::function<SavedValue::Map(const T&)> save, std::function<std::optional<T>(const SavedValue::Map&)> restore) {
    return {[save = std::move(save)](const T& value) { return SavedValue(save(value)); },
            [restore = std::move(restore)](const SavedValue& saved) -> std::optional<T> {
                return saved.map() ? restore(*saved.map()) : std::nullopt;
            }};
}

} // namespace kompoz
