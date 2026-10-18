#include "uia_bridge.hpp"

#include <SDL3/SDL.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <unordered_map>
#include <utility>

namespace kompoz::sdl {

namespace {

constexpr int kRoot = -1;  // the window: the top nodes' parent
constexpr long kHeadingLevel1 = 80051;
constexpr const wchar_t* kProperty = L"KompozUiaBridge";  // the window's bridge (SetPropW)
constexpr double kNoScroll = -1;                          // UIA_ScrollPatternNoScroll
// (MinGW's are unsigned)
constexpr HRESULT kNotAvailable = HRESULT(UIA_E_ELEMENTNOTAVAILABLE), kNotEnabled = HRESULT(UIA_E_ELEMENTNOTENABLED),
                  kInvalidOperation = HRESULT(UIA_E_INVALIDOPERATION);

std::wstring wide(const std::string& s) {
    int n = MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), nullptr, 0);
    std::wstring out(size_t(n), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, s.data(), int(s.size()), out.data(), n);
    return out;
}
std::string narrow(const wchar_t* s) {
    if (!s) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s, -1, nullptr, 0, nullptr, nullptr);
    std::string out(size_t(std::max(n, 1)), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, -1, out.data(), n, nullptr, nullptr);
    out.resize(out.size() - 1);
    return out;
}
BSTR bstr(const std::string& s) {
    std::wstring w = wide(s);
    return SysAllocStringLen(w.data(), UINT(w.size()));
}

VARIANT variantString(const std::string& s) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BSTR;
    v.bstrVal = bstr(s);
    return v;
}
VARIANT variantInt(long i) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_I4;
    v.lVal = i;
    return v;
}
VARIANT variantBool(bool b) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_BOOL;
    v.boolVal = b ? VARIANT_TRUE : VARIANT_FALSE;
    return v;
}
VARIANT variantDouble(double d) {
    VARIANT v;
    VariantInit(&v);
    v.vt = VT_R8;
    v.dblVal = d;
    return v;
}

long controlTypeOf(const SemanticsNode& n, bool hasChildren) {
    switch (n.role) {
    case SemanticsRole::Button: return UIA_ButtonControlTypeId;
    case SemanticsRole::Checkbox: return UIA_CheckBoxControlTypeId;
    case SemanticsRole::Switch: return UIA_ButtonControlTypeId;  // a toggle switch: a button that toggles
    case SemanticsRole::RadioButton: return UIA_RadioButtonControlTypeId;
    case SemanticsRole::Tab: return UIA_TabItemControlTypeId;
    case SemanticsRole::Image: return UIA_ImageControlTypeId;
    case SemanticsRole::Slider: return n.settable ? UIA_SliderControlTypeId : UIA_ProgressBarControlTypeId;
    case SemanticsRole::None: break;
    }
    if (n.editable) return UIA_EditControlTypeId;
    if (n.heading) return UIA_TextControlTypeId;
    if (n.clickable) return UIA_ButtonControlTypeId;
    if (n.range) return n.settable ? UIA_SliderControlTypeId : UIA_ProgressBarControlTypeId;
    if (n.scrollable) return UIA_PaneControlTypeId;
    if (!n.label.empty() && !hasChildren) return UIA_TextControlTypeId;
    return UIA_GroupControlTypeId;
}

bool isSelectionItem(const SemanticsNode& n) {
    return n.checked && (n.role == SemanticsRole::Tab || n.role == SemanticsRole::RadioButton);
}
bool isToggle(const SemanticsNode& n) { return n.checked && !isSelectionItem(n); }
bool focusable(const SemanticsNode& n) { return n.editable || (n.clickable && n.enabled); }  // what Tab goes to

// A text field's text as it is told: a password's hidden.
std::string textOf(const SemanticsNode& n) {
    if (!n.password) return n.value;
    std::string dots;
    for (unsigned char c : n.value)
        if ((c & 0xC0) != 0x80) dots += "\u2022";
    return dots;
}

class Provider;

} // namespace

struct UiaBridge::State : std::enable_shared_from_this<State> {
    HWND hwnd = nullptr;
    WNDPROC previous = nullptr;
    uint32_t eventType = 0;
    std::atomic<bool> asked{false}, treeRequest{false};
    std::mutex mutex;  // what follows, for UI Automation's threads
    std::vector<SemanticsNode> nodes;
    std::unordered_map<int, size_t> at;                  // id -> index in nodes
    std::unordered_map<int, std::vector<int>> children;  // parent id (kRoot: the top) -> ids
    float dpToPixels = 1;
    bool published = false, gone = false;
    std::vector<Request> requests;
    std::unordered_map<int, Provider*> providers;  // one per object, each holding a reference

    const SemanticsNode* node(int id) const {
        auto it = at.find(id);
        return it == at.end() ? nullptr : &nodes[it->second];
    }
    const std::vector<int>& kidsOf(int id) const {
        static const std::vector<int> none;
        auto it = children.find(id);
        return it == children.end() ? none : it->second;
    }
    bool exists(int id) const { return !gone && (id == kRoot || at.contains(id)); }
    Provider* provider(int id);  // (under the mutex) a new reference

    void notify() const {
        SDL_Event e{};
        e.type = eventType;
        SDL_PushEvent(&e);
    }
    void request(int id, SemanticsAction action, float argument = 0, std::string text = {}) {
        requests.push_back({id, action, argument, std::move(text)});
        notify();
    }

    // Where a node is on the screen, in pixels.
    UiaRect rectOf(const SemanticsNode& n) const {
        POINT origin{0, 0};
        ClientToScreen(hwnd, &origin);
        float k = dpToPixels;
        return {origin.x + double(n.bounds.x * k), origin.y + double(n.bounds.y * k), double(n.bounds.width * k),
                double(n.bounds.height * k)};
    }
};

namespace {

using State = UiaBridge::State;

class Provider final : public IRawElementProviderSimple,
                       public IRawElementProviderFragment,
                       public IRawElementProviderFragmentRoot,
                       public IInvokeProvider,
                       public IToggleProvider,
                       public ISelectionItemProvider,
                       public IValueProvider,
                       public IRangeValueProvider,
                       public IScrollProvider {
public:
    Provider(std::shared_ptr<State> state, int id) : state_(std::move(state)), id_(id) {}

    // ---- IUnknown ----
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IRawElementProviderSimple)) *out = static_cast<IRawElementProviderSimple*>(this);
        else if (riid == __uuidof(IRawElementProviderFragment)) *out = static_cast<IRawElementProviderFragment*>(this);
        else if (riid == __uuidof(IRawElementProviderFragmentRoot) && id_ == kRoot) *out = static_cast<IRawElementProviderFragmentRoot*>(this);
        else if (riid == __uuidof(IInvokeProvider)) *out = static_cast<IInvokeProvider*>(this);
        else if (riid == __uuidof(IToggleProvider)) *out = static_cast<IToggleProvider*>(this);
        else if (riid == __uuidof(ISelectionItemProvider)) *out = static_cast<ISelectionItemProvider*>(this);
        else if (riid == __uuidof(IValueProvider)) *out = static_cast<IValueProvider*>(this);
        else if (riid == __uuidof(IRangeValueProvider)) *out = static_cast<IRangeValueProvider*>(this);
        else if (riid == __uuidof(IScrollProvider)) *out = static_cast<IScrollProvider*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }

    // ---- IRawElementProviderSimple ----
    HRESULT STDMETHODCALLTYPE get_ProviderOptions(ProviderOptions* out) override {
        if (!out) return E_POINTER;
        *out = ProviderOptions_ServerSideProvider;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetPatternProvider(PATTERNID pattern, IUnknown** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return withNode([&](const SemanticsNode& n) {
            bool has = (pattern == UIA_InvokePatternId && n.clickable && !n.checked) ||
                       (pattern == UIA_TogglePatternId && isToggle(n)) ||
                       (pattern == UIA_SelectionItemPatternId && isSelectionItem(n)) ||
                       (pattern == UIA_ValuePatternId && n.editable) || (pattern == UIA_RangeValuePatternId && n.range) ||
                       (pattern == UIA_ScrollPatternId && n.scrollable);
            if (has) {
                *out = static_cast<IRawElementProviderSimple*>(this);
                AddRef();
            }
            return S_OK;
        }, S_OK);
    }
    HRESULT STDMETHODCALLTYPE GetPropertyValue(PROPERTYID property, VARIANT* out) override {
        if (!out) return E_POINTER;
        VariantInit(out);
        if (id_ == kRoot) {  // the window's own (its title, its type) come from its HWND's provider
            if (property == UIA_FrameworkIdPropertyId) *out = variantString("Kompoz");
            return S_OK;
        }
        std::lock_guard lock(state_->mutex);
        const SemanticsNode* n = state_->exists(id_) ? state_->node(id_) : nullptr;
        if (!n) return kNotAvailable;
        switch (property) {
        case UIA_ControlTypePropertyId: *out = variantInt(controlTypeOf(*n, !state_->kidsOf(id_).empty())); break;
        case UIA_NamePropertyId:
            if (!n->label.empty()) *out = variantString(n->label);
            break;
        case UIA_AutomationIdPropertyId: *out = variantString(std::to_string(id_)); break;
        case UIA_FrameworkIdPropertyId: *out = variantString("Kompoz"); break;
        case UIA_IsEnabledPropertyId: *out = variantBool(n->enabled); break;
        case UIA_HasKeyboardFocusPropertyId: *out = variantBool(n->focused); break;
        case UIA_IsKeyboardFocusablePropertyId: *out = variantBool(focusable(*n)); break;
        case UIA_IsPasswordPropertyId: *out = variantBool(n->password); break;
        case UIA_ItemStatusPropertyId:
            if (!n->stateDescription.empty()) *out = variantString(n->stateDescription);
            break;
        case UIA_LiveSettingPropertyId:
            if (n->liveRegion != LiveRegionMode::None) *out = variantInt(n->liveRegion == LiveRegionMode::Assertive ? Assertive : Polite);
            break;
        case UIA_HeadingLevelPropertyId:
            if (n->heading) *out = variantInt(kHeadingLevel1);
            break;
        case UIA_LocalizedControlTypePropertyId:
            if (n->role == SemanticsRole::Switch) *out = variantString("toggle switch");
            break;
        default: break;
        }
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_HostRawElementProvider(IRawElementProviderSimple** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return id_ == kRoot ? UiaHostProviderFromHwnd(state_->hwnd, out) : S_OK;
    }

    // ---- IRawElementProviderFragment ----
    HRESULT STDMETHODCALLTYPE Navigate(NavigateDirection direction, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        std::lock_guard lock(state_->mutex);
        if (!state_->exists(id_)) return kNotAvailable;
        int target = kRoot - 1;  // none
        if (direction == NavigateDirection_Parent) {
            if (id_ != kRoot) target = state_->node(id_)->parent;
        } else if (direction == NavigateDirection_FirstChild || direction == NavigateDirection_LastChild) {
            const auto& kids = state_->kidsOf(id_);
            if (!kids.empty()) target = direction == NavigateDirection_FirstChild ? kids.front() : kids.back();
        } else if (id_ != kRoot) {
            const auto& siblings = state_->kidsOf(state_->node(id_)->parent);
            auto it = std::find(siblings.begin(), siblings.end(), id_);
            if (direction == NavigateDirection_NextSibling && it + 1 < siblings.end()) target = *(it + 1);
            if (direction == NavigateDirection_PreviousSibling && it != siblings.begin()) target = *(it - 1);
        }
        if (target >= kRoot) *out = fragment(target);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetRuntimeId(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        if (id_ == kRoot) return S_OK;  // the HWND's
        SAFEARRAY* ids = SafeArrayCreateVector(VT_I4, 0, 2);
        if (!ids) return E_OUTOFMEMORY;
        LONG i = 0, values[2] = {UiaAppendRuntimeId, id_};
        for (; i < 2; ++i) SafeArrayPutElement(ids, &i, &values[i]);
        *out = ids;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE get_BoundingRectangle(UiaRect* out) override {
        if (!out) return E_POINTER;
        *out = {};
        if (id_ == kRoot) return S_OK;  // the HWND's
        return withNode([&](const SemanticsNode& n) {
            *out = state_->rectOf(n);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE GetEmbeddedFragmentRoots(SAFEARRAY** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetFocus() override {
        return withNode([&](const SemanticsNode& n) {
            if (focusable(n)) state_->request(id_, SemanticsAction::Focus);
            return S_OK;
        }, S_OK);
    }
    HRESULT STDMETHODCALLTYPE get_FragmentRoot(IRawElementProviderFragmentRoot** out) override {
        if (!out) return E_POINTER;
        std::lock_guard lock(state_->mutex);
        *out = static_cast<IRawElementProviderFragmentRoot*>(state_->provider(kRoot));
        return S_OK;
    }

    // ---- IRawElementProviderFragmentRoot (the window) ----
    HRESULT STDMETHODCALLTYPE ElementProviderFromPoint(double x, double y, IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        std::lock_guard lock(state_->mutex);
        int found = kRoot;  // the last laid out there: the innermost, drawn on top
        for (const SemanticsNode& n : state_->nodes) {
            UiaRect r = state_->rectOf(n);
            if (x >= r.left && y >= r.top && x < r.left + r.width && y < r.top + r.height) found = n.id;
        }
        *out = fragment(found);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE GetFocus(IRawElementProviderFragment** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        std::lock_guard lock(state_->mutex);
        for (const SemanticsNode& n : state_->nodes)
            if (n.focused) *out = fragment(n.id);
        return S_OK;
    }

    // ---- patterns ----
    HRESULT STDMETHODCALLTYPE Invoke() override { return act(SemanticsAction::Click); }
    HRESULT STDMETHODCALLTYPE Toggle() override { return act(SemanticsAction::Click); }
    HRESULT STDMETHODCALLTYPE get_ToggleState(ToggleState* out) override {
        if (!out) return E_POINTER;
        return withNode([&](const SemanticsNode& n) {
            *out = n.checked.value_or(false) ? ToggleState_On : ToggleState_Off;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE Select() override {
        return withNode([&](const SemanticsNode& n) {
            if (!n.enabled) return kNotEnabled;
            if (!n.checked.value_or(false)) state_->request(id_, SemanticsAction::Click);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE AddToSelection() override { return Select(); }
    HRESULT STDMETHODCALLTYPE RemoveFromSelection() override { return kInvalidOperation; }
    HRESULT STDMETHODCALLTYPE get_IsSelected(BOOL* out) override {
        if (!out) return E_POINTER;
        return withNode([&](const SemanticsNode& n) {
            *out = n.checked.value_or(false);
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_SelectionContainer(IRawElementProviderSimple** out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetValue(LPCWSTR value) override {  // a text field's
        return withNode([&](const SemanticsNode& n) {
            if (!n.editable) return kInvalidOperation;
            state_->request(id_, SemanticsAction::SetText, 0, narrow(value));
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_Value(BSTR* out) override {
        if (!out) return E_POINTER;
        *out = nullptr;
        return withNode([&](const SemanticsNode& n) {
            *out = bstr(textOf(n));
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_IsReadOnly(BOOL* out) override {  // (Value's and RangeValue's)
        if (!out) return E_POINTER;
        return withNode([&](const SemanticsNode& n) {
            *out = !n.editable && !n.settable;
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE SetValue(double value) override {  // a slider's
        return withNode([&](const SemanticsNode& n) {
            if (!n.settable) return kInvalidOperation;
            if (!n.range || value < n.range->rangeStart || value > n.range->rangeEnd) return E_INVALIDARG;
            state_->request(id_, SemanticsAction::SetProgress, float(value));
            return S_OK;
        });
    }
    HRESULT STDMETHODCALLTYPE get_Value(double* out) override { return range(out, [](const ProgressBarRangeInfo& r) { return r.current; }); }
    HRESULT STDMETHODCALLTYPE get_Maximum(double* out) override { return range(out, [](const ProgressBarRangeInfo& r) { return r.rangeEnd; }); }
    HRESULT STDMETHODCALLTYPE get_Minimum(double* out) override { return range(out, [](const ProgressBarRangeInfo& r) { return r.rangeStart; }); }
    HRESULT STDMETHODCALLTYPE get_SmallChange(double* out) override {
        return range(out, [](const ProgressBarRangeInfo& r) { return r.steps > 0 ? (r.rangeEnd - r.rangeStart) / float(r.steps + 1) : 0.f; });
    }
    HRESULT STDMETHODCALLTYPE get_LargeChange(double* out) override {
        return range(out, [](const ProgressBarRangeInfo& r) { return (r.rangeEnd - r.rangeStart) / 10; });
    }
    HRESULT STDMETHODCALLTYPE Scroll(ScrollAmount horizontal, ScrollAmount vertical) override {
        auto forward = [](ScrollAmount a) { return a == ScrollAmount_LargeIncrement || a == ScrollAmount_SmallIncrement; };
        auto backward = [](ScrollAmount a) { return a == ScrollAmount_LargeDecrement || a == ScrollAmount_SmallDecrement; };
        if (forward(horizontal) || forward(vertical)) return act(SemanticsAction::ScrollForward);
        if (backward(horizontal) || backward(vertical)) return act(SemanticsAction::ScrollBackward);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE SetScrollPercent(double, double) override { return kInvalidOperation; }
    HRESULT STDMETHODCALLTYPE get_HorizontalScrollPercent(double* out) override { return number(out, kNoScroll); }
    HRESULT STDMETHODCALLTYPE get_VerticalScrollPercent(double* out) override { return number(out, kNoScroll); }
    HRESULT STDMETHODCALLTYPE get_HorizontalViewSize(double* out) override { return number(out, 100); }
    HRESULT STDMETHODCALLTYPE get_VerticalViewSize(double* out) override { return number(out, 100); }
    HRESULT STDMETHODCALLTYPE get_HorizontallyScrollable(BOOL* out) override { return flag(out); }
    HRESULT STDMETHODCALLTYPE get_VerticallyScrollable(BOOL* out) override { return flag(out); }

    // A new reference to the object's fragment (under the mutex).
    IRawElementProviderFragment* fragment(int id) { return static_cast<IRawElementProviderFragment*>(state_->provider(id)); }

private:
    template <class F>
    HRESULT withNode(F&& f, HRESULT forRoot = kInvalidOperation) {
        std::lock_guard lock(state_->mutex);
        if (!state_->exists(id_)) return kNotAvailable;
        if (id_ == kRoot) return forRoot;
        return f(*state_->node(id_));
    }
    HRESULT act(SemanticsAction action) {
        return withNode([&](const SemanticsNode& n) {
            if (!n.enabled) return kNotEnabled;
            state_->request(id_, action);
            return S_OK;
        });
    }
    template <class F>
    HRESULT range(double* out, F&& value) {
        if (!out) return E_POINTER;
        return withNode([&](const SemanticsNode& n) {
            if (!n.range) return kInvalidOperation;
            *out = value(*n.range);
            return S_OK;
        });
    }
    HRESULT number(double* out, double value) {
        if (!out) return E_POINTER;
        *out = value;
        return S_OK;
    }
    HRESULT flag(BOOL* out) {
        if (!out) return E_POINTER;
        return withNode([&](const SemanticsNode& n) {
            *out = n.scrollable;
            return S_OK;
        });
    }

    std::shared_ptr<State> state_;
    int id_;
    std::atomic<ULONG> refs_{1};
};

// The window's own messages: the root when UI Automation asks for it, the rest to SDL's.
LRESULT CALLBACK windowProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    auto* s = static_cast<State*>(GetPropW(hwnd, kProperty));
    if (!s) return DefWindowProcW(hwnd, message, wParam, lParam);
    if (message == WM_GETOBJECT && LONG(DWORD(lParam)) == UiaRootObjectId) {
        IRawElementProviderSimple* root;
        {
            std::lock_guard lock(s->mutex);
            root = static_cast<IRawElementProviderSimple*>(s->provider(kRoot));
        }
        if (!s->asked.exchange(true)) {
            s->treeRequest = true;
            s->notify();  // to publish the tree
        }
        LRESULT result = UiaReturnRawElementProvider(hwnd, wParam, lParam, root);
        root->Release();
        return result;
    }
    return CallWindowProcW(s->previous, hwnd, message, wParam, lParam);
}

// An event to raise once the mutex is let go (UI Automation may call back while raising it).
struct Raise {
    Provider* provider;
    enum class Kind { Property, Event, Structure } kind;
    int id;  // the property's or the event's
    VARIANT before, after;
};

} // namespace

Provider* State::provider(int id) {
    auto [it, inserted] = providers.try_emplace(id, nullptr);
    if (inserted) it->second = new Provider(shared_from_this(), id);
    it->second->AddRef();
    return it->second;
}

UiaBridge::UiaBridge(void* hwnd) : state_(std::make_shared<State>()) {
    State& s = *state_;
    s.hwnd = static_cast<HWND>(hwnd);
    if (!s.hwnd) return;
    s.eventType = SDL_RegisterEvents(1);
    if (s.eventType == 0) return;
    SetPropW(s.hwnd, kProperty, &s);
    s.previous = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(s.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(&windowProc)));
}

UiaBridge::~UiaBridge() {
    State& s = *state_;
    if (!s.previous) return;
    if (reinterpret_cast<WNDPROC>(GetWindowLongPtrW(s.hwnd, GWLP_WNDPROC)) == &windowProc)
        SetWindowLongPtrW(s.hwnd, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s.previous));
    RemovePropW(s.hwnd, kProperty);
    std::unordered_map<int, Provider*> providers;
    {
        std::lock_guard lock(s.mutex);
        s.gone = true;  // what clients still hold says so
        providers.swap(s.providers);
    }
    UiaReturnRawElementProvider(s.hwnd, 0, 0, nullptr);
    for (auto& [id, p] : providers) {
        UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(p));
        p->Release();
    }
}

bool UiaBridge::active() const { return state_->asked.load(); }

bool UiaBridge::takeTreeRequest() { return state_->treeRequest.exchange(false); }

void UiaBridge::publish(std::vector<SemanticsNode> nodes, float dpToPixels) {
    State& s = *state_;
    std::unordered_map<int, size_t> at;
    std::unordered_map<int, std::vector<int>> children;
    for (size_t i = 0; i < nodes.size(); ++i) {
        at[nodes[i].id] = i;
        children[nodes[i].parent].push_back(nodes[i].id);
    }
    std::vector<Raise> raises;
    std::vector<Provider*> removed;
    {
        std::lock_guard lock(s.mutex);
        auto property = [&](int id, int property, VARIANT before, VARIANT after) {
            raises.push_back({s.provider(id), Raise::Kind::Property, property, before, after});
        };
        auto event = [&](int id, int event) { raises.push_back({s.provider(id), Raise::Kind::Event, event, {}, {}}); };
        if (s.published && UiaClientsAreListening()) {
            // Whose children changed: told on them.
            for (const auto& [parent, kids] : children)
                if ((parent == kRoot || s.at.contains(parent)) && s.kidsOf(parent) != kids)
                    raises.push_back({s.provider(parent), Raise::Kind::Structure, 0, {}, {}});
            for (const auto& [parent, kids] : s.children)
                if (!children.contains(parent) && (parent == kRoot || at.contains(parent)))
                    raises.push_back({s.provider(parent), Raise::Kind::Structure, 0, {}, {}});
            int focusedBefore = kRoot - 1, focusedNow = kRoot - 1;
            for (const SemanticsNode& old : s.nodes)
                if (old.focused) focusedBefore = old.id;
            for (const SemanticsNode& n : nodes) {
                if (n.focused) focusedNow = n.id;
                const SemanticsNode* old = s.node(n.id);
                if (!old) continue;
                if (n.label != old->label) property(n.id, UIA_NamePropertyId, variantString(old->label), variantString(n.label));
                if (n.stateDescription != old->stateDescription)
                    property(n.id, UIA_ItemStatusPropertyId, variantString(old->stateDescription), variantString(n.stateDescription));
                if (n.enabled != old->enabled) property(n.id, UIA_IsEnabledPropertyId, variantBool(old->enabled), variantBool(n.enabled));
                bool was = old->checked.value_or(false), is = n.checked.value_or(false);
                if (isToggle(n) && was != is)
                    property(n.id, UIA_ToggleToggleStatePropertyId, variantInt(was ? ToggleState_On : ToggleState_Off),
                             variantInt(is ? ToggleState_On : ToggleState_Off));
                if (isSelectionItem(n) && was != is) {
                    property(n.id, UIA_SelectionItemIsSelectedPropertyId, variantBool(was), variantBool(is));
                    if (is) event(n.id, UIA_SelectionItem_ElementSelectedEventId);
                }
                if (n.editable && old->editable && n.value != old->value)
                    property(n.id, UIA_ValueValuePropertyId, variantString(textOf(*old)), variantString(textOf(n)));
                if (n.range && old->range && n.range->current != old->range->current)
                    property(n.id, UIA_RangeValueValuePropertyId, variantDouble(old->range->current), variantDouble(n.range->current));
                if (n.liveRegion != LiveRegionMode::None && (n.label != old->label || n.stateDescription != old->stateDescription))
                    event(n.id, UIA_LiveRegionChangedEventId);
            }
            if (focusedNow != focusedBefore && focusedNow > kRoot - 1) event(focusedNow, UIA_AutomationFocusChangedEventId);
        }
        // Those gone: disconnected, so that what clients hold of them says so.
        for (auto it = s.providers.begin(); it != s.providers.end();)
            if (it->first != kRoot && !at.contains(it->first)) {
                removed.push_back(it->second);
                it = s.providers.erase(it);
            } else
                ++it;
        s.nodes = std::move(nodes);
        s.at = std::move(at);
        s.children = std::move(children);
        s.dpToPixels = dpToPixels;
        s.published = true;
    }
    for (Raise& r : raises) {
        auto* simple = static_cast<IRawElementProviderSimple*>(r.provider);
        switch (r.kind) {
        case Raise::Kind::Property:
            UiaRaiseAutomationPropertyChangedEvent(simple, r.id, r.before, r.after);
            VariantClear(&r.before);
            VariantClear(&r.after);
            break;
        case Raise::Kind::Event: UiaRaiseAutomationEvent(simple, r.id); break;
        case Raise::Kind::Structure: {
            SAFEARRAY* id = nullptr;
            r.provider->GetRuntimeId(&id);
            int ids[2] = {UiaAppendRuntimeId, 0};
            LONG* values = nullptr;
            if (id) SafeArrayAccessData(id, reinterpret_cast<void**>(&values)), ids[1] = values[1], SafeArrayUnaccessData(id);
            UiaRaiseStructureChangedEvent(simple, StructureChangeType_ChildrenInvalidated, ids, id ? 2 : 0);
            if (id) SafeArrayDestroy(id);
            break;
        }
        }
        r.provider->Release();
    }
    for (Provider* p : removed) {
        UiaDisconnectProvider(static_cast<IRawElementProviderSimple*>(p));
        p->Release();
    }
}

uint32_t UiaBridge::eventType() const { return state_->eventType; }

void* UiaBridge::rootProvider() {
    std::lock_guard lock(state_->mutex);
    return static_cast<IRawElementProviderSimple*>(state_->provider(kRoot));
}

std::vector<UiaBridge::Request> UiaBridge::takeRequests() {
    std::lock_guard lock(state_->mutex);
    return std::exchange(state_->requests, {});
}

} // namespace kompoz::sdl
