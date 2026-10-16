#pragma once

// More state holders: derivedStateOf, mutableStateListOf, mutableStateMapOf and rememberUpdatedState.

#include "runtime.hpp"

#include <algorithm>
#include <concepts>
#include <functional>
#include <initializer_list>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace kompoz {

// A value computed from other states. Readers recompose only when the computed value itself
// changes, not on every change of its inputs:
//   auto showButton = remember([&] { return derivedStateOf([list] { return list.firstVisibleItemIndex() > 0; }); });
//   if (showButton.get()) ...
// Create it inside a composition (in remember) so it can observe its inputs.
template <class T>
class DerivedState {
public:
    explicit DerivedState(std::function<T()> calc) : d_(std::make_shared<Data>()) {
        d_->calc = std::move(calc);
        if (auto* c = detail::Composer::current()) {
            d_->composer = c;
            d_->observer = std::make_unique<detail::Scope>(*c, nullptr);
            Data* raw = d_.get();
            d_->observer->onInvalidated = [raw] {
                if (raw->recompute()) raw->composer->invalidateReaders(raw);
            };
        }
    }

    const T& get() const {
        if (!d_->valid || !d_->observer) d_->recompute();  // unobserved: always fresh
        detail::recordRead(d_.get());
        return *d_->value;
    }

    friend bool operator==(const DerivedState& a, const DerivedState& b) { return a.d_ == b.d_; }

private:
    struct Data {
        std::function<T()> calc;
        std::optional<T> value;
        bool valid = false;
        detail::Composer* composer = nullptr;
        std::unique_ptr<detail::Scope> observer;

        bool recompute() {  // returns whether the value changed
            std::optional<T> next;
            if (observer)
                composer->observeReads(*observer, [&] { next.emplace(calc()); });
            else
                next.emplace(calc());
            bool changed = true;
            if constexpr (std::equality_comparable<T>) changed = !value || !(*value == *next);
            value = std::move(next);
            valid = true;
            return changed;
        }
    };
    std::shared_ptr<Data> d_;
};

template <class F>
auto derivedStateOf(F calc) -> DerivedState<std::decay_t<std::invoke_result_t<F&>>> {
    using T = std::decay_t<std::invoke_result_t<F&>>;
    return DerivedState<T>(std::function<T()>(std::move(calc)));
}

// An observable list (Compose's SnapshotStateList): reading it subscribes, changing it
// recomposes the readers.
template <class T>
class SnapshotStateList {
public:
    SnapshotStateList() : state_(std::vector<T>{}) {}
    SnapshotStateList(std::initializer_list<T> items) : state_(std::vector<T>(items)) {}

    const std::vector<T>& items() const { return state_.get(); }
    size_t size() const { return items().size(); }
    bool empty() const { return items().empty(); }
    const T& operator[](size_t i) const { return items()[i]; }
    auto begin() const { return items().begin(); }
    auto end() const { return items().end(); }

    void add(T item) const {
        state_.update([&](auto& v) { v.push_back(std::move(item)); });
    }
    void add(size_t index, T item) const {
        state_.update([&](auto& v) { v.insert(v.begin() + std::ptrdiff_t(std::min(index, v.size())), std::move(item)); });
    }
    void set(size_t index, T item) const {
        state_.update([&](auto& v) { v.at(index) = std::move(item); });
    }
    void removeAt(size_t index) const {
        state_.update([&](auto& v) { v.erase(v.begin() + std::ptrdiff_t(index)); });
    }
    template <class Pred>
    void removeIf(Pred pred) const {
        state_.update([&](auto& v) { std::erase_if(v, pred); });
    }
    void clear() const {
        state_.update([](auto& v) { v.clear(); });
    }
    // Changes items in place: list.update([](auto& v) { ... });
    template <class F>
    void update(F&& mutate) const {
        state_.update(std::forward<F>(mutate));
    }

    friend bool operator==(const SnapshotStateList& a, const SnapshotStateList& b) { return a.state_ == b.state_; }

private:
    MutableState<std::vector<T>> state_;
};

template <class T>
SnapshotStateList<T> mutableStateListOf(std::initializer_list<T> items = {}) {
    return SnapshotStateList<T>(items);
}

// An observable map (Compose's SnapshotStateMap): reading it subscribes, changing it recomposes
// the readers. Its entries go in key order.
//   auto likes = remember([] { return mutableStateMapOf<std::string, int>(); });
//   Text(std::to_string(likes.getOrDefault(id, 0)));
//   Button([likes, id] { likes.put(id, likes.getOrDefault(id, 0) + 1); }, ...);
template <class K, class V>
class SnapshotStateMap {
public:
    SnapshotStateMap() : state_(std::map<K, V>{}) {}
    SnapshotStateMap(std::initializer_list<std::pair<const K, V>> entries) : state_(std::map<K, V>(entries)) {}

    const std::map<K, V>& entries() const { return state_.get(); }
    size_t size() const { return entries().size(); }
    bool empty() const { return entries().empty(); }
    bool containsKey(const K& key) const { return entries().contains(key); }
    // The value for `key`, or nothing (Compose's map[key]).
    std::optional<V> get(const K& key) const {
        auto it = entries().find(key);
        return it == entries().end() ? std::nullopt : std::optional<V>(it->second);
    }
    V getOrDefault(const K& key, V otherwise) const { return get(key).value_or(std::move(otherwise)); }
    auto begin() const { return entries().begin(); }
    auto end() const { return entries().end(); }

    // Sets `key` to `value` (Compose's map[key] = value); the value it had before, if any. The
    // same value again: nobody recomposes.
    std::optional<V> put(const K& key, V value) const {
        if constexpr (std::equality_comparable<V>) {
            auto it = state_.peek().find(key);
            if (it != state_.peek().end() && it->second == value) return it->second;
        }
        std::optional<V> before;
        state_.update([&](auto& m) {
            auto [it, added] = m.try_emplace(key, std::move(value));
            if (!added) before = std::exchange(it->second, std::move(value));
        });
        return before;
    }
    void putAll(const std::map<K, V>& more) const {
        state_.update([&](auto& m) {
            for (const auto& [k, v] : more) m.insert_or_assign(k, v);
        });
    }
    // Takes `key` out; the value it had, if any. Nothing there: nobody recomposes.
    std::optional<V> remove(const K& key) const {
        if (!state_.peek().contains(key)) return std::nullopt;
        std::optional<V> was;
        state_.update([&](auto& m) {
            auto it = m.find(key);
            was = std::move(it->second);
            m.erase(it);
        });
        return was;
    }
    void clear() const {
        state_.update([](auto& m) { m.clear(); });
    }
    // Changes entries in place: map.update([](auto& m) { ... });
    template <class F>
    void update(F&& mutate) const {
        state_.update(std::forward<F>(mutate));
    }

    friend bool operator==(const SnapshotStateMap& a, const SnapshotStateMap& b) { return a.state_ == b.state_; }

private:
    MutableState<std::map<K, V>> state_;
};

template <class K, class V>
SnapshotStateMap<K, V> mutableStateMapOf(std::initializer_list<std::pair<const K, V>> entries = {}) {
    return SnapshotStateMap<K, V>(entries);
}

// The latest value passed to rememberUpdatedState. Not observable: meant for long-running
// effects that must not restart but should use the newest callback or value.
template <class T>
class UpdatedState {
public:
    const T& get() const { return *value_; }
    std::shared_ptr<T> value_;
};

template <class T>
UpdatedState<T> rememberUpdatedState(const T& value, CallSite site = CallSite::current()) {
    auto& holder = remember([&] { return std::make_shared<T>(value); }, site);
    *holder = value;
    return UpdatedState<T>{holder};
}

} // namespace kompoz
