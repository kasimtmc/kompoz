// Tests of the Windows platform code that runs without an SDL window: the UI Automation bridge,
// on a window of the tests' own, asked through UI Automation's own client (IUIAutomation), as
// Narrator asks.

#include "uia_bridge.hpp"

#include <SDL3/SDL.h>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <ole2.h>
#include <uiautomation.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace kompoz;

namespace {

int checks = 0, failures = 0;
#define CHECK(c)                                                       \
    do {                                                               \
        ++checks;                                                      \
        if (!(c)) {                                                    \
            ++failures;                                                \
            std::printf("  %s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #c); \
        }                                                              \
    } while (0)

std::string narrow(BSTR s) {
    if (!s) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, s, int(SysStringLen(s)), nullptr, 0, nullptr, nullptr);
    std::string out(size_t(n), '\0');
    WideCharToMultiByte(CP_UTF8, 0, s, int(SysStringLen(s)), out.data(), n, nullptr, nullptr);
    SysFreeString(s);
    return out;
}

template <class T>
struct Ref {  // a COM reference, released when it goes
    T* p = nullptr;
    Ref() = default;
    Ref(const Ref&) = delete;
    Ref& operator=(const Ref&) = delete;
    ~Ref() {
        if (p) p->Release();
    }
    T** out() { return &p; }
    T* operator->() const { return p; }
    explicit operator bool() const { return p != nullptr; }
};

// Messages for `ms` milliseconds, or until `done` (UI Automation sends the window some).
bool pump(int ms, const std::function<bool()>& done = {}) {
    auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < end) {
        if (done && done()) return true;
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return done && done();
}

std::vector<SemanticsNode> testTree() {
    std::vector<SemanticsNode> nodes(13);
    nodes[0] = {.id = 1, .bounds = {10, 20, 30, 40}, .label = "OK", .clickable = true};
    nodes[1] = {.id = 2, .bounds = {0, 70, 24, 24}, .label = "Agree", .role = SemanticsRole::Checkbox, .clickable = true, .checked = false};
    nodes[2] = {.id = 3, .bounds = {0, 100, 200, 56}, .label = "Name", .value = "héllo", .editable = true};
    nodes[3] = {.id = 4, .bounds = {0, 160, 200, 40}, .label = "Volume", .stateDescription = "3", .role = SemanticsRole::Slider,
                .range = ProgressBarRangeInfo{3, 0, 10, 4}, .settable = true};
    nodes[4] = {.id = 5, .bounds = {0, 200, 200, 100}, .label = "List", .scrollable = true};
    nodes[5] = {.id = 6, .parent = 5, .bounds = {0, 210, 200, 20}, .label = "Item"};
    nodes[6] = {.id = 7, .bounds = {0, 300, 200, 56}, .label = "Password", .value = "abc", .editable = true, .password = true};
    nodes[7] = {.id = 8, .bounds = {0, 0, 100, 20}, .label = "Tab", .role = SemanticsRole::Tab, .clickable = true, .checked = false};
    nodes[8] = {.id = 9, .bounds = {0, 360, 100, 20}, .label = "Title", .heading = true};
    nodes[9] = {.id = 10, .parent = 9, .bounds = {0, 380, 100, 20}, .label = "Subtitle"};
    nodes[10] = {.id = 11, .bounds = {200, 0, 100, 100}, .label = "Card"};
    nodes[11] = {.id = 12, .parent = 11, .bounds = {200, 0, 100, 20}, .label = "Inside"};
    nodes[12] = {.id = 13, .parent = 5, .bounds = {0, 230, 200, 20}, .label = "Item 2"};
    return nodes;
}

// What an event handler heard, as "kind id what".
struct Heard {
    std::mutex mutex;
    std::vector<std::string> lines;
    void add(std::string line) {
        std::lock_guard lock(mutex);
        lines.push_back(std::move(line));
    }
    bool has(const std::string& line) {
        std::lock_guard lock(mutex);
        return std::find(lines.begin(), lines.end(), line) != lines.end();
    }
};
std::string idOf(IUIAutomationElement* e) {  // its name (Wine doesn't give clients the automation id)
    BSTR name = nullptr;
    e->get_CurrentName(&name);
    return narrow(name);
}

class Handler final : public IUIAutomationEventHandler,
                      public IUIAutomationPropertyChangedEventHandler,
                      public IUIAutomationStructureChangedEventHandler,
                      public IUIAutomationFocusChangedEventHandler {
public:
    explicit Handler(Heard& heard) : heard_(heard) {}
    ULONG STDMETHODCALLTYPE AddRef() override { return ++refs_; }
    ULONG STDMETHODCALLTYPE Release() override {
        ULONG n = --refs_;
        if (n == 0) delete this;
        return n;
    }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** out) override {
        *out = nullptr;
        if (riid == __uuidof(IUnknown) || riid == __uuidof(IUIAutomationEventHandler)) *out = static_cast<IUIAutomationEventHandler*>(this);
        else if (riid == __uuidof(IUIAutomationPropertyChangedEventHandler))
            *out = static_cast<IUIAutomationPropertyChangedEventHandler*>(this);
        else if (riid == __uuidof(IUIAutomationStructureChangedEventHandler))
            *out = static_cast<IUIAutomationStructureChangedEventHandler*>(this);
        else if (riid == __uuidof(IUIAutomationFocusChangedEventHandler)) *out = static_cast<IUIAutomationFocusChangedEventHandler*>(this);
        else return E_NOINTERFACE;
        AddRef();
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HandleAutomationEvent(IUIAutomationElement* sender, EVENTID event) override {
        heard_.add("event " + idOf(sender) + " " + std::to_string(event));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HandlePropertyChangedEvent(IUIAutomationElement* sender, PROPERTYID property, VARIANT value) override {
        std::string v = value.vt == VT_BSTR ? narrow(SysAllocString(value.bstrVal))
                      : value.vt == VT_BOOL ? (value.boolVal ? "true" : "false")
                      : value.vt == VT_I4   ? std::to_string(value.lVal)
                      : value.vt == VT_R8   ? std::to_string(int(value.dblVal))
                                            : "?";
        heard_.add("property " + idOf(sender) + " " + std::to_string(property) + " " + v);
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HandleStructureChangedEvent(IUIAutomationElement* sender, StructureChangeType type, SAFEARRAY*) override {
        heard_.add("structure " + idOf(sender) + " " + std::to_string(int(type)));
        return S_OK;
    }
    HRESULT STDMETHODCALLTYPE HandleFocusChangedEvent(IUIAutomationElement* sender) override {
        heard_.add("focus " + idOf(sender));
        return S_OK;
    }

private:
    Heard& heard_;
    std::atomic<ULONG> refs_{1};
};

void uiaBridgeServesTheTree() {
    WNDCLASSW wc{};
    wc.lpfnWndProc = DefWindowProcW;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"KompozPlatformTests";
    RegisterClassW(&wc);
    HWND hwnd = CreateWindowExW(0, wc.lpszClassName, L"Main", WS_OVERLAPPEDWINDOW, 100, 50, 400, 300, nullptr, nullptr, wc.hInstance, nullptr);
    CHECK(hwnd != nullptr);
    sdl::UiaBridge bridge(hwnd);
    CHECK(bridge.eventType() != 0 && !bridge.active());

    Ref<IUIAutomation> uia;
    CHECK(SUCCEEDED(CoCreateInstance(CLSID_CUIAutomation, nullptr, CLSCTX_INPROC_SERVER, IID_IUIAutomation,
                                     reinterpret_cast<void**>(uia.out()))));
    if (!uia) return;
    bridge.publish(testTree(), 2);
    Ref<IUIAutomationElement> window;
    CHECK(SUCCEEDED(uia->ElementFromHandle(hwnd, window.out())) && window);
    if (!window) return;
    CHECK(bridge.active() && bridge.takeTreeRequest() && !bridge.takeTreeRequest());  // asked for: the tree wanted
    SDL_PumpEvents();
    CHECK(SDL_PeepEvents(nullptr, 0, SDL_PEEKEVENT, bridge.eventType(), bridge.eventType()) > 0);  // the main loop woken to publish it
    SDL_FlushEvent(bridge.eventType());
    CHECK(narrow([&] { BSTR b = nullptr; window->get_CurrentName(&b); return b; }()) == "Main");  // the HWND's own

    Ref<IUIAutomationCondition> all;
    uia->CreateTrueCondition(all.out());
    Ref<IUIAutomationTreeWalker> walker;  // the raw view (Wine has no RawViewWalker)
    CHECK(all && SUCCEEDED(uia->CreateTreeWalker(all.p, walker.out())) && walker);
    if (!walker) return;
    auto childIds = [&](IUIAutomationElement* parent) {
        std::vector<std::string> ids;
        if (!parent) return ids;
        IUIAutomationElement* child = nullptr;
        walker->GetFirstChildElement(parent, &child);
        while (child) {
            ids.push_back(idOf(child));
            IUIAutomationElement* next = nullptr;
            walker->GetNextSiblingElement(child, &next);
            child->Release();
            child = next;
        }
        return ids;
    };
    CHECK((childIds(window.p) == std::vector<std::string>{"OK", "Agree", "Name", "Volume", "List", "Password", "Tab", "Title", "Card"}));
    // Each node by its name, walked to (Wine has no FindFirst).
    std::function<IUIAutomationElement*(IUIAutomationElement*, const std::string&)> search = [&](IUIAutomationElement* parent,
                                                                                                 const std::string& wanted) {
        IUIAutomationElement* child = nullptr;
        walker->GetFirstChildElement(parent, &child);
        while (child) {
            if (idOf(child) == wanted) return child;
            if (IUIAutomationElement* inside = search(child, wanted)) {
                child->Release();
                return inside;
            }
            IUIAutomationElement* next = nullptr;
            walker->GetNextSiblingElement(child, &next);
            child->Release();
            child = next;
        }
        return static_cast<IUIAutomationElement*>(nullptr);
    };
    auto find = [&](const char* name) { return search(window.p, name); };
    auto withElement = [&](const char* id, const std::function<void(IUIAutomationElement*)>& f) {
        IUIAutomationElement* e = find(id);
        CHECK(e != nullptr);
        if (!e) return;
        f(e);
        e->Release();
    };
    auto controlType = [](IUIAutomationElement* e) {
        CONTROLTYPEID type = 0;
        e->get_CurrentControlType(&type);
        return type;
    };
    auto name = [](IUIAutomationElement* e) {
        BSTR b = nullptr;
        e->get_CurrentName(&b);
        return narrow(b);
    };
    auto property = [](IUIAutomationElement* e, PROPERTYID id) {
        VARIANT v;
        VariantInit(&v);
        e->GetCurrentPropertyValue(id, &v);
        long out = v.vt == VT_I4 ? v.lVal : v.vt == VT_BOOL ? (v.boolVal ? 1 : 0) : -1;
        VariantClear(&v);
        return out;
    };
    auto requests = [&] { return bridge.takeRequests(); };
    auto requested = [&](int id, SemanticsAction action) {
        auto all = requests();
        return all.size() == 1 && all[0].id == id && all[0].action == action;
    };

    withElement("Item", [&](IUIAutomationElement* e) { CHECK(name(e) == "Item" && controlType(e) == UIA_TextControlTypeId); });
    {
        IUIAutomationElement* list = find("List");
        CHECK((childIds(list) == std::vector<std::string>{"Item", "Item 2"}));
        if (list) list->Release();
    }
    // A button: where it is (dp to pixels, on the screen).
    withElement("OK", [&](IUIAutomationElement* e) {
        CHECK(name(e) == "OK" && controlType(e) == UIA_ButtonControlTypeId);
        RECT r{};
        e->get_CurrentBoundingRectangle(&r);
        POINT origin{0, 0};
        ClientToScreen(hwnd, &origin);
        CHECK(r.left == origin.x + 20 && r.top == origin.y + 40 && r.right - r.left == 60 && r.bottom - r.top == 80);
    });
    withElement("Agree", [&](IUIAutomationElement* e) { CHECK(controlType(e) == UIA_CheckBoxControlTypeId); });
    withElement("Tab", [&](IUIAutomationElement* e) { CHECK(controlType(e) == UIA_TabItemControlTypeId); });
    withElement("Name", [&](IUIAutomationElement* e) { CHECK(controlType(e) == UIA_EditControlTypeId); });
    withElement("Volume", [&](IUIAutomationElement* e) { CHECK(controlType(e) == UIA_SliderControlTypeId); });
    withElement("List", [&](IUIAutomationElement* e) { CHECK(controlType(e) == UIA_PaneControlTypeId); });

    // The providers themselves, as UI Automation gets them from the window: their patterns and
    // properties (Wine's client has none of those but names and types).
    Ref<IRawElementProviderSimple> root;
    *root.out() = static_cast<IRawElementProviderSimple*>(bridge.rootProvider());
    auto stringOf = [](IRawElementProviderSimple* p, PROPERTYID id) {
        VARIANT v;
        VariantInit(&v);
        p->GetPropertyValue(id, &v);
        std::string out = v.vt == VT_BSTR ? narrow(SysAllocString(v.bstrVal)) : std::string{};
        VariantClear(&v);
        return out;
    };
    auto numberOf = [](IRawElementProviderSimple* p, PROPERTYID id) {
        VARIANT v;
        VariantInit(&v);
        p->GetPropertyValue(id, &v);
        long out = v.vt == VT_I4 ? v.lVal : v.vt == VT_BOOL ? (v.boolVal ? 1 : 0) : -1;
        VariantClear(&v);
        return out;
    };
    // A provider by its name, walked to with Navigate.
    std::function<IRawElementProviderSimple*(IRawElementProviderFragment*, const std::string&)> walk =
        [&](IRawElementProviderFragment* parent, const std::string& wanted) -> IRawElementProviderSimple* {
        IRawElementProviderFragment* child = nullptr;
        parent->Navigate(NavigateDirection_FirstChild, &child);
        while (child) {
            IRawElementProviderSimple* simple = nullptr;
            child->QueryInterface(IID_IRawElementProviderSimple, reinterpret_cast<void**>(&simple));
            if (simple && stringOf(simple, UIA_NamePropertyId) == wanted) {
                child->Release();
                return simple;
            }
            if (simple) simple->Release();
            if (IRawElementProviderSimple* inside = walk(child, wanted)) {
                child->Release();
                return inside;
            }
            IRawElementProviderFragment* next = nullptr;
            child->Navigate(NavigateDirection_NextSibling, &next);
            child->Release();
            child = next;
        }
        return nullptr;
    };
    Ref<IRawElementProviderFragment> rootFragment;
    root->QueryInterface(IID_IRawElementProviderFragment, reinterpret_cast<void**>(rootFragment.out()));
    CHECK(rootFragment);
    if (!rootFragment) return;
    auto withProvider = [&](const char* name, const std::function<void(IRawElementProviderSimple*)>& f) {
        IRawElementProviderSimple* p = walk(rootFragment.p, name);
        CHECK(p != nullptr);
        if (!p) return;
        f(p);
        p->Release();
    };
    auto pattern = [](IRawElementProviderSimple* p, PATTERNID id, REFIID iid) {
        IUnknown* provider = nullptr;
        void* out = nullptr;
        p->GetPatternProvider(id, &provider);
        if (provider) provider->QueryInterface(iid, &out), provider->Release();
        return out;
    };
    // A button: invoked; found from a point in it.
    withProvider("OK", [&](IRawElementProviderSimple* p) {
        CHECK(numberOf(p, UIA_IsEnabledPropertyId) == 1 && numberOf(p, UIA_IsKeyboardFocusablePropertyId) == 1);  // Tab goes to it
        CHECK(stringOf(p, UIA_AutomationIdPropertyId) == "1" && stringOf(p, UIA_FrameworkIdPropertyId) == "Kompoz");
        auto* invoke = static_cast<IInvokeProvider*>(pattern(p, UIA_InvokePatternId, IID_IInvokeProvider));
        CHECK(invoke && SUCCEEDED(invoke->Invoke()));
        if (invoke) invoke->Release();
        CHECK(pattern(p, UIA_TogglePatternId, IID_IToggleProvider) == nullptr);  // nothing to toggle
        Ref<IRawElementProviderFragmentRoot> fragmentRoot;
        root->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(fragmentRoot.out()));
        POINT origin{0, 0};
        ClientToScreen(hwnd, &origin);
        IRawElementProviderFragment* at = nullptr;
        if (fragmentRoot) fragmentRoot->ElementProviderFromPoint(origin.x + 30, origin.y + 50, &at);
        IRawElementProviderSimple* atSimple = nullptr;
        if (at) at->QueryInterface(IID_IRawElementProviderSimple, reinterpret_cast<void**>(&atSimple)), at->Release();
        CHECK(atSimple && stringOf(atSimple, UIA_NamePropertyId) == "OK");
        if (atSimple) atSimple->Release();
        at = nullptr;  // in the list, on its item: the item
        if (fragmentRoot) fragmentRoot->ElementProviderFromPoint(origin.x + 20, origin.y + 425, &at);
        atSimple = nullptr;
        if (at) at->QueryInterface(IID_IRawElementProviderSimple, reinterpret_cast<void**>(&atSimple)), at->Release();
        CHECK(atSimple && stringOf(atSimple, UIA_NamePropertyId) == "Item");
        if (atSimple) atSimple->Release();
    });
    CHECK(requested(1, SemanticsAction::Click));
    // Focus: on a button, which Tab goes to; not on a label.
    auto setFocus = [&](const char* name) {
        withProvider(name, [&](IRawElementProviderSimple* p) {
            Ref<IRawElementProviderFragment> fragment;
            p->QueryInterface(IID_IRawElementProviderFragment, reinterpret_cast<void**>(fragment.out()));
            CHECK(fragment && SUCCEEDED(fragment->SetFocus()));
        });
    };
    setFocus("OK");
    CHECK(requested(1, SemanticsAction::Focus));
    setFocus("Item");
    CHECK(requests().empty());
    withProvider("Item", [&](IRawElementProviderSimple* p) { CHECK(numberOf(p, UIA_IsKeyboardFocusablePropertyId) == 0); });
    SDL_PumpEvents();
    CHECK(SDL_PeepEvents(nullptr, 0, SDL_PEEKEVENT, bridge.eventType(), bridge.eventType()) > 0);  // the main loop woken
    // A checkbox: toggled.
    withProvider("Agree", [&](IRawElementProviderSimple* p) {
        auto* toggle = static_cast<IToggleProvider*>(pattern(p, UIA_TogglePatternId, IID_IToggleProvider));
        CHECK(toggle);
        if (!toggle) return;
        ToggleState state = ToggleState_On;
        toggle->get_ToggleState(&state);
        CHECK(state == ToggleState_Off && SUCCEEDED(toggle->Toggle()));
        toggle->Release();
        CHECK(pattern(p, UIA_InvokePatternId, IID_IInvokeProvider) == nullptr);
    });
    CHECK(requested(2, SemanticsAction::Click));
    // A tab: selected.
    withProvider("Tab", [&](IRawElementProviderSimple* p) {
        auto* item = static_cast<ISelectionItemProvider*>(pattern(p, UIA_SelectionItemPatternId, IID_ISelectionItemProvider));
        CHECK(item);
        if (!item) return;
        BOOL selected = TRUE;
        item->get_IsSelected(&selected);
        CHECK(!selected && SUCCEEDED(item->Select()));
        item->Release();
    });
    CHECK(requested(8, SemanticsAction::Click));
    // A text field: its value, set; focused.
    withProvider("Name", [&](IRawElementProviderSimple* p) {
        CHECK(numberOf(p, UIA_IsKeyboardFocusablePropertyId) == 1 && numberOf(p, UIA_IsPasswordPropertyId) == 0);
        auto* value = static_cast<IValueProvider*>(pattern(p, UIA_ValuePatternId, IID_IValueProvider));
        CHECK(value);
        if (!value) return;
        BSTR text = nullptr;
        value->get_Value(&text);
        CHECK(narrow(text) == "héllo");
        BOOL readOnly = TRUE;
        value->get_IsReadOnly(&readOnly);
        CHECK(!readOnly && SUCCEEDED(value->SetValue(L"süt")));
        value->Release();
        auto all = requests();
        CHECK(all.size() == 1 && all[0].id == 3 && all[0].action == SemanticsAction::SetText && all[0].text == "süt");
        Ref<IRawElementProviderFragment> fragment;
        p->QueryInterface(IID_IRawElementProviderFragment, reinterpret_cast<void**>(fragment.out()));
        CHECK(fragment && SUCCEEDED(fragment->SetFocus()));
        CHECK(requested(3, SemanticsAction::Focus));
    });
    // A password field: what is typed isn't told.
    withProvider("Password", [&](IRawElementProviderSimple* p) {
        CHECK(numberOf(p, UIA_IsPasswordPropertyId) == 1);
        auto* value = static_cast<IValueProvider*>(pattern(p, UIA_ValuePatternId, IID_IValueProvider));
        BSTR text = nullptr;
        if (value) value->get_Value(&text), value->Release();
        CHECK(narrow(text) == "•••");
    });
    // A slider: its range, set.
    withProvider("Volume", [&](IRawElementProviderSimple* p) {
        CHECK(stringOf(p, UIA_ItemStatusPropertyId) == "3");
        auto* range = static_cast<IRangeValueProvider*>(pattern(p, UIA_RangeValuePatternId, IID_IRangeValueProvider));
        CHECK(range);
        if (!range) return;
        double value = 0, maximum = 0, small = 0;
        BOOL readOnly = TRUE;
        range->get_Value(&value), range->get_Maximum(&maximum), range->get_SmallChange(&small), range->get_IsReadOnly(&readOnly);
        CHECK(value == 3 && maximum == 10 && small == 2 && !readOnly);
        CHECK(SUCCEEDED(range->SetValue(7.0)));
        CHECK(FAILED(range->SetValue(11.0)));  // out of its range
        range->Release();
    });
    {
        auto all = requests();
        CHECK(all.size() == 1 && all[0].id == 4 && all[0].action == SemanticsAction::SetProgress && all[0].argument == 7);
    }
    // A list: scrolled.
    withProvider("List", [&](IRawElementProviderSimple* p) {
        auto* scroll = static_cast<IScrollProvider*>(pattern(p, UIA_ScrollPatternId, IID_IScrollProvider));
        CHECK(scroll && SUCCEEDED(scroll->Scroll(ScrollAmount_NoAmount, ScrollAmount_LargeDecrement)));
        if (scroll) scroll->Release();
    });
    CHECK(requested(5, SemanticsAction::ScrollBackward));
    // Up, and to the one before.
    auto neighbour = [&](const char* name, NavigateDirection direction) {
        std::string out;
        withProvider(name, [&](IRawElementProviderSimple* p) {
            Ref<IRawElementProviderFragment> fragment, other;
            p->QueryInterface(IID_IRawElementProviderFragment, reinterpret_cast<void**>(fragment.out()));
            if (fragment) fragment->Navigate(direction, other.out());
            Ref<IRawElementProviderSimple> simple;
            if (other) other->QueryInterface(IID_IRawElementProviderSimple, reinterpret_cast<void**>(simple.out()));
            out = simple ? stringOf(simple.p, UIA_NamePropertyId) : "(none)";
        });
        return out;
    };
    CHECK(neighbour("Item", NavigateDirection_Parent) == "List");
    CHECK(neighbour("Agree", NavigateDirection_PreviousSibling) == "OK");
    CHECK(neighbour("OK", NavigateDirection_PreviousSibling) == "(none)");
    CHECK(neighbour("Title", NavigateDirection_NextSibling) == "Card" && neighbour("Card", NavigateDirection_NextSibling) == "(none)");
    CHECK(neighbour("List", NavigateDirection_LastChild) == "Item 2");
    CHECK(neighbour("List", NavigateDirection_FirstChild) == "Item");
    withProvider("Title", [&](IRawElementProviderSimple* p) {
        CHECK(numberOf(p, UIA_HeadingLevelPropertyId) == 80051 && numberOf(p, UIA_ControlTypePropertyId) == UIA_TextControlTypeId);
    });
    // A label with something in it: a group.
    withProvider("Card", [&](IRawElementProviderSimple* p) { CHECK(numberOf(p, UIA_ControlTypePropertyId) == UIA_GroupControlTypeId); });

    // What changed is raised.
    Heard heard;
    auto* handler = new Handler(heard);
    PROPERTYID properties[] = {UIA_NamePropertyId, UIA_ToggleToggleStatePropertyId, UIA_ValueValuePropertyId,
                               UIA_RangeValueValuePropertyId, UIA_IsEnabledPropertyId, UIA_SelectionItemIsSelectedPropertyId};
    SAFEARRAY* propertyArray = SafeArrayCreateVector(VT_I4, 0, 6);
    for (LONG i = 0; i < 6; ++i) SafeArrayPutElement(propertyArray, &i, &properties[i]);
    HRESULT registered[] = {
        uia->AddPropertyChangedEventHandler(window.p, TreeScope_Subtree, nullptr, handler, propertyArray),
        uia->AddAutomationEventHandler(UIA_LiveRegionChangedEventId, window.p, TreeScope_Subtree, nullptr, handler),
        uia->AddFocusChangedEventHandler(nullptr, handler),
        uia->AddStructureChangedEventHandler(window.p, TreeScope_Subtree, nullptr, handler),
        uia->AddAutomationEventHandler(UIA_SelectionItem_ElementSelectedEventId, window.p, TreeScope_Subtree, nullptr, handler),
    };
    SafeArrayDestroy(propertyArray);
    // Wine has no property-changed or structure-changed handlers: those events are heard on Windows.
    bool propertyEvents = registered[0] != E_NOTIMPL, structureEvents = registered[3] != E_NOTIMPL;
    if (!propertyEvents || !structureEvents) std::printf("  (no property or structure handlers here: those events aren't heard)\n");
    for (HRESULT r : registered) CHECK(SUCCEEDED(r) || r == E_NOTIMPL);
    IRawElementProviderSimple* item = walk(rootFragment.p, "Item");
    auto next = testTree();
    next[0].label = "Done", next[0].liveRegion = LiveRegionMode::Polite;
    next[1].checked = true;
    next[2].value = "süt", next[2].focused = true;
    next[3].range->current = 5;
    next[7].checked = true;
    next[1].enabled = false;
    next.erase(next.begin() + 5);  // the list's item
    bridge.publish(next, 2);
    std::vector<std::string> expected = {"property Done 30005 Done", "event Done 20024", "property Agree 30086 1",
                                         "property Name 30045 süt", "property Volume 30047 5", "property Tab 30079 true",
                                         "event Tab 20012", "focus Name", "structure List 2", "property Agree 30010 false"};
    pump(2000, [&] {
        for (const auto& line : expected)
            if (!heard.has(line)) return false;
        return true;
    });
    std::erase_if(expected, [&](const std::string& line) {
        return (!propertyEvents && line.starts_with("property")) || (!structureEvents && line.starts_with("structure"));
    });
    for (const auto& line : expected) {
        CHECK(heard.has(line));
        if (!heard.has(line)) std::printf("    not heard: %s\n", line.c_str());
    }
    // Focus where the tree has it; a tab already selected, a checkbox disabled: nothing asked.
    {
        Ref<IRawElementProviderFragmentRoot> fragmentRoot;
        root->QueryInterface(IID_IRawElementProviderFragmentRoot, reinterpret_cast<void**>(fragmentRoot.out()));
        Ref<IRawElementProviderFragment> focused;
        if (fragmentRoot) fragmentRoot->GetFocus(focused.out());
        Ref<IRawElementProviderSimple> simple;
        if (focused) focused->QueryInterface(IID_IRawElementProviderSimple, reinterpret_cast<void**>(simple.out()));
        CHECK(simple && stringOf(simple.p, UIA_NamePropertyId) == "Name");
    }
    withProvider("Tab", [&](IRawElementProviderSimple* p) {
        auto* item = static_cast<ISelectionItemProvider*>(pattern(p, UIA_SelectionItemPatternId, IID_ISelectionItemProvider));
        CHECK(item && SUCCEEDED(item->Select()));
        if (item) item->Release();
    });
    withProvider("Agree", [&](IRawElementProviderSimple* p) {
        CHECK(numberOf(p, UIA_IsEnabledPropertyId) == 0 && numberOf(p, UIA_IsKeyboardFocusablePropertyId) == 0);
        auto* toggle = static_cast<IToggleProvider*>(pattern(p, UIA_TogglePatternId, IID_IToggleProvider));
        CHECK(toggle && toggle->Toggle() == HRESULT(UIA_E_ELEMENTNOTENABLED));
        if (toggle) toggle->Release();
    });
    CHECK(requests().empty());
    // A node gone: what was held of it says so.
    CHECK(item != nullptr);
    if (item) {
        VARIANT gone;
        VariantInit(&gone);
        CHECK(item->GetPropertyValue(UIA_NamePropertyId, &gone) == HRESULT(UIA_E_ELEMENTNOTAVAILABLE));
        item->Release();
    }
    uia->RemoveAllEventHandlers();
    handler->Release();
}

} // namespace

int main() {
    setvbuf(stdout, nullptr, _IOLBF, 0);  // what failed seen before a crash
    SDL_Init(SDL_INIT_EVENTS);
    CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    uiaBridgeServesTheTree();
    CoUninitialize();
    SDL_Quit();
    std::printf("%d checks, %d failed\n", checks, failures);
    return failures == 0 ? 0 : 1;
}
