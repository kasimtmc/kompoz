#include <kompoz/host.hpp>
#include <kompoz/viewmodel.hpp>

namespace kompoz {

CoroutineScope ViewModel::viewModelScope() {
    if (!scope_) scope_.emplace(dispatcher ? *dispatcher : detail::currentDispatcher());
    return *scope_;
}

void ViewModel::clear() {
    if (scope_) scope_->cancel();
    onCleared();
}

void ViewModelStore::clear() {
    auto entries = std::move(entries_);  // onCleared may make or read others
    entries_.clear();
    for (auto& [key, entry] : entries)
        if (entry.viewModel) entry.viewModel->clear();
    handles_.clear();
    restored_.clear();
}

SavedStateHandle ViewModelStore::savedStateHandle(const std::string& key) {
    if (auto it = handles_.find(key); it != handles_.end()) return it->second;
    SavedStateHandle handle;
    if (auto it = restored_.find(key); it != restored_.end()) {
        handle = SavedStateHandle::restore(it->second);
        restored_.erase(it);
    }
    handles_.emplace(key, handle);
    return handle;
}

SavedValue ViewModelStore::saveState() const {
    SavedValue::Map out(restored_.begin(), restored_.end());  // not claimed yet: still theirs
    for (const auto& [key, handle] : handles_)
        if (!handle.keys().empty()) out[key] = handle.save();
    return out;
}

void ViewModelStore::restoreState(const SavedValue& saved) {
    if (const SavedValue::Map* m = saved.map())
        for (const auto& [key, value] : *m) restored_[key] = value;
}

struct SavedStateHandle::Data {
    std::map<std::string, SavedValue> values;
    struct Watcher {
        std::shared_ptr<void> flow;  // a MutableStateFlow<T>
        std::function<void(const SavedValue&)> update;
    };
    std::map<std::string, Watcher> flows;
};

SavedStateHandle::SavedStateHandle() : data_(std::make_shared<Data>()) {}
bool SavedStateHandle::contains(const std::string& key) const { return data_->values.contains(key); }
std::vector<std::string> SavedStateHandle::keys() const {
    std::vector<std::string> out;
    for (const auto& [key, value] : data_->values) out.push_back(key);
    return out;
}
const SavedValue* SavedStateHandle::find(const std::string& key) const {
    auto it = data_->values.find(key);
    return it == data_->values.end() ? nullptr : &it->second;
}
void SavedStateHandle::put(const std::string& key, SavedValue value) {
    data_->values[key] = value;
    if (auto it = data_->flows.find(key); it != data_->flows.end()) {
        auto update = it->second.update;  // it may set others
        update(value);
    }
}
void SavedStateHandle::remove(const std::string& key) {
    data_->values.erase(key);
    data_->flows.erase(key);
}
std::shared_ptr<void> SavedStateHandle::flow(const std::string& key) const {
    auto it = data_->flows.find(key);
    return it == data_->flows.end() ? nullptr : it->second.flow;
}
void SavedStateHandle::watch(const std::string& key, std::shared_ptr<void> flow, std::function<void(const SavedValue&)> update) {
    data_->flows[key] = {std::move(flow), std::move(update)};
}
SavedValue SavedStateHandle::save() const { return SavedValue::Map(data_->values.begin(), data_->values.end()); }
SavedStateHandle SavedStateHandle::restore(const SavedValue& saved) {
    SavedStateHandle handle;
    if (const SavedValue::Map* m = saved.map()) handle.data_->values = *m;
    return handle;
}

std::shared_ptr<void> ViewModelStore::get(const std::string& key) const {
    auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : it->second.value;
}

void ViewModelStore::put(const std::string& key, std::shared_ptr<void> value, ViewModel* viewModel) {
    entries_[key] = {std::move(value), viewModel};
}

const CompositionLocal<std::shared_ptr<ViewModelStore>> detail::LocalViewModelStore{nullptr};

std::shared_ptr<ViewModelStore> detail::currentViewModelStore() {
    if (auto store = LocalViewModelStore.current()) return store;
    return Composer::require().host().viewModelStore();
}

} // namespace kompoz
