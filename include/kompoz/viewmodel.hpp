#pragma once

// ViewModels, as in Jetpack: state and work that outlive the composables showing them. One
// lives in the nearest owner, a navigation entry (cleared when the entry is popped for good)
// or else the app (cleared when the host goes away). Screens of a nested navigation graph
// share one through the graph's entry: viewModel<T>(*entry.parent()).
//
//   class CounterViewModel : public ViewModel {
//   public:
//       MutableStateFlow<int> count{0};
//       void start() { viewModelScope().launch([this]() -> Task { ... }); }
//   protected:
//       void onCleared() override { ... }
//   };
//   auto vm = viewModel<CounterViewModel>();  // the same one while its owner lives
//
// What a ViewModel keeps in its SavedStateHandle outlives the process too (Android ending the
// app in the background): a ViewModel whose constructor takes one gets it, with the values its
// owner and key had, when it is made again.
//
//   class SearchViewModel : public ViewModel {
//   public:
//       explicit SearchViewModel(SavedStateHandle state)
//           : state_(state), query(state.getStateFlow<std::string>("query", "")) {}
//       void setQuery(std::string q) { state_.set("query", q); }
//       StateFlow<std::string> query;
//   private:
//       SavedStateHandle state_;
//   };

#include "coroutines.hpp"
#include "flow.hpp"
#include "runtime.hpp"

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <typeinfo>

namespace kompoz {

class ViewModel {
public:
    virtual ~ViewModel() = default;
    // Coroutines that stop when the ViewModel is cleared (on the UI thread).
    CoroutineScope viewModelScope();

    detail::Dispatcher* dispatcher = nullptr;  // set by viewModel() when it is made

protected:
    virtual void onCleared() {}

private:
    friend class ViewModelStore;
    void clear();
    std::optional<CoroutineScope> scope_;
};

// A ViewModel's values that are written out with the app's saved state (Jetpack's
// SavedStateHandle): values of saveable types (see saved_value.hpp), by key. On the UI thread.
class SavedStateHandle {
public:
    SavedStateHandle();
    bool contains(const std::string& key) const;
    std::vector<std::string> keys() const;
    template <class T>
        requires HasSaver<T>
    std::optional<T> get(const std::string& key) const {
        const SavedValue* v = find(key);
        return v ? Saver<T>::restore(*v) : std::nullopt;
    }
    // Also what its getStateFlow gives from now on.
    template <class T>
        requires HasSaver<T>
    void set(const std::string& key, const T& value) {
        put(key, Saver<T>::save(value));
    }
    void remove(const std::string& key);
    // The value as it is set, as an observable StateFlow: initialValue (also set) if there is
    // none yet (Jetpack's getStateFlow).
    template <class T>
        requires HasSaver<T>
    StateFlow<T> getStateFlow(const std::string& key, T initialValue) {
        if (auto flow = this->flow(key)) return *std::static_pointer_cast<MutableStateFlow<T>>(flow);
        if (!contains(key)) set(key, initialValue);
        auto flow = std::make_shared<MutableStateFlow<T>>(get<T>(key).value_or(std::move(initialValue)));
        std::weak_ptr<MutableStateFlow<T>> weak = flow;
        watch(key, flow, [weak](const SavedValue& v) {
            if (auto f = weak.lock())
                if (auto value = Saver<T>::restore(v)) f->setValue(std::move(*value));
        });
        return *flow;
    }

    SavedValue save() const;  // the values
    static SavedStateHandle restore(const SavedValue& saved);
    friend bool operator==(const SavedStateHandle& a, const SavedStateHandle& b) { return a.data_ == b.data_; }

private:
    const SavedValue* find(const std::string& key) const;
    void put(const std::string& key, SavedValue value);
    std::shared_ptr<void> flow(const std::string& key) const;
    void watch(const std::string& key, std::shared_ptr<void> flow, std::function<void(const SavedValue&)> update);
    struct Data;
    std::shared_ptr<Data> data_;
};

// The ViewModels of one owner, by type and key, and their SavedStateHandles.
class ViewModelStore {
public:
    ViewModelStore() = default;
    ViewModelStore(const ViewModelStore&) = delete;
    ViewModelStore& operator=(const ViewModelStore&) = delete;
    ~ViewModelStore() { clear(); }
    void clear();  // onCleared for each, their scopes cancelled; their saved state dropped
    std::shared_ptr<void> get(const std::string& key) const;
    void put(const std::string& key, std::shared_ptr<void> value, ViewModel* viewModel);
    size_t size() const { return entries_.size(); }
    // The ViewModel `key`'s handle: made the first time, with what was restored for it.
    SavedStateHandle savedStateHandle(const std::string& key);
    // The handles' values (and those restored but not claimed yet), to write out; and back.
    SavedValue saveState() const;
    void restoreState(const SavedValue& saved);

private:
    struct Entry {
        std::shared_ptr<void> value;
        ViewModel* viewModel = nullptr;
    };
    std::map<std::string, Entry> entries_;
    std::map<std::string, SavedStateHandle> handles_;
    std::map<std::string, SavedValue> restored_;
};

// Whatever keeps ViewModels: a navigation entry (Jetpack's ViewModelStoreOwner).
class ViewModelStoreOwner {
public:
    virtual ~ViewModelStoreOwner() = default;
    virtual const std::shared_ptr<ViewModelStore>& viewModelStore() const = 0;
};

namespace detail {
extern const CompositionLocal<std::shared_ptr<ViewModelStore>> LocalViewModelStore;
std::shared_ptr<ViewModelStore> currentViewModelStore();  // the provided one, else the host's

template <class T, class F>
std::shared_ptr<T> viewModelIn(ViewModelStore& store, const std::string& key, F&& factory) {
    std::string id = std::string(typeid(T).name()) + ':' + key;
    if (auto existing = store.get(id)) return std::static_pointer_cast<T>(existing);
    std::shared_ptr<T> value;
    if constexpr (std::is_invocable_v<F&, SavedStateHandle>)
        value = factory(store.savedStateHandle(id));  // the one it had, after the process ended
    else
        value = factory();
    ViewModel* vm = nullptr;
    if constexpr (std::is_base_of_v<ViewModel, T>) {
        vm = value.get();
        vm->dispatcher = &currentDispatcher();
    }
    store.put(id, value, vm);
    return value;
}
} // namespace detail

// The T of the nearest owner (for `key`), made by `factory` the first time.
template <class T, class F>
std::shared_ptr<T> viewModel(const std::string& key, F&& factory) {
    return detail::viewModelIn<T>(*detail::currentViewModelStore(), key, std::forward<F>(factory));
}
namespace detail {
// The default factory: a constructor that takes a SavedStateHandle gets one.
template <class T>
auto defaultViewModelFactory() {
    return [](SavedStateHandle state) {
        if constexpr (std::is_constructible_v<T, SavedStateHandle>) return std::make_shared<T>(std::move(state));
        else return std::make_shared<T>();
    };
}
} // namespace detail
template <class T>
std::shared_ptr<T> viewModel(const std::string& key = {}) {
    return viewModel<T>(key, detail::defaultViewModelFactory<T>());
}
// The T of a given owner, e.g. a navigation graph's entry, whose screens then share it.
template <class T, class F>
std::shared_ptr<T> viewModel(const ViewModelStoreOwner& owner, const std::string& key, F&& factory) {
    return detail::viewModelIn<T>(*owner.viewModelStore(), key, std::forward<F>(factory));
}
template <class T>
std::shared_ptr<T> viewModel(const ViewModelStoreOwner& owner, const std::string& key = {}) {
    return viewModel<T>(owner, key, detail::defaultViewModelFactory<T>());
}

} // namespace kompoz
