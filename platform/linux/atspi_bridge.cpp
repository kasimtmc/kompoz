#include "atspi_bridge.hpp"

#include "libdbus.hpp"

#include <poll.h>
#include <sys/eventfd.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <functional>
#include <mutex>
#include <optional>
#include <ranges>
#include <string_view>
#include <thread>
#include <unordered_map>
#include <utility>

namespace kompoz::platform {

namespace {

using namespace dbus;

constexpr const char* kRootPath = "/org/a11y/atspi/accessible/root";
constexpr const char* kWindowPath = "/org/a11y/atspi/accessible/window";
constexpr std::string_view kNodePrefix = "/org/a11y/atspi/accessible/";
constexpr const char* kNullPath = "/org/a11y/atspi/null";
constexpr const char* kCachePath = "/org/a11y/atspi/cache";
constexpr int kRoot = -2, kWindow = -1;  // the application, and its window: the top nodes' parent (-1)

constexpr const char* kAccessible = "org.a11y.atspi.Accessible";
constexpr const char* kApplication = "org.a11y.atspi.Application";
constexpr const char* kComponent = "org.a11y.atspi.Component";
constexpr const char* kAction = "org.a11y.atspi.Action";
constexpr const char* kValue = "org.a11y.atspi.Value";
constexpr const char* kText = "org.a11y.atspi.Text";
constexpr const char* kEditableText = "org.a11y.atspi.EditableText";
constexpr const char* kProperties = "org.freedesktop.DBus.Properties";
constexpr const char* kIntrospectable = "org.freedesktop.DBus.Introspectable";
constexpr const char* kEventObject = "org.a11y.atspi.Event.Object";
constexpr const char* kEventWindow = "org.a11y.atspi.Event.Window";
constexpr const char* kEventFocus = "org.a11y.atspi.Event.Focus";

// AtspiRole
enum : uint32_t {
    RoleCheckBox = 7, RoleFrame = 23, RoleImage = 27, RoleLabel = 29, RolePageTab = 37, RolePanel = 39, RolePasswordText = 40,
    RoleProgressBar = 42, RoleButton = 43, RoleRadioButton = 44, RoleScrollPane = 49, RoleSlider = 51, RoleApplication = 75,
    RoleEntry = 79, RoleHeading = 83, RoleSwitch = 130,
};
// AtspiStateType
enum : int {
    StateActive = 1, StateChecked = 4, StateEditable = 7, StateEnabled = 8, StateFocusable = 11, StateFocused = 12,
    StateResizable = 21, StateSelectable = 22, StateSelected = 23, StateSensitive = 24, StateShowing = 25, StateVisible = 30,
    StateIndeterminate = 32, StateCheckable = 41,
};
constexpr uint32_t kLayerWidget = 3, kLayerWindow = 7;  // AtspiComponentLayer
constexpr uint32_t kCoordWindow = 1, kCoordParent = 2;  // AtspiCoordType (0: the screen)

uint32_t roleOf(const SemanticsNode& n) {
    switch (n.role) {
    case SemanticsRole::Button: return RoleButton;
    case SemanticsRole::Checkbox: return RoleCheckBox;
    case SemanticsRole::Switch: return RoleSwitch;
    case SemanticsRole::RadioButton: return RoleRadioButton;
    case SemanticsRole::Tab: return RolePageTab;
    case SemanticsRole::Image: return RoleImage;
    case SemanticsRole::Slider: return n.settable ? RoleSlider : RoleProgressBar;
    case SemanticsRole::None: break;
    }
    if (n.editable) return n.password ? RolePasswordText : RoleEntry;
    if (n.heading) return RoleHeading;
    if (n.clickable) return RoleButton;
    if (n.range) return n.settable ? RoleSlider : RoleProgressBar;
    if (n.scrollable) return RoleScrollPane;
    return RolePanel;
}

const char* roleName(uint32_t role) {
    switch (role) {
    case RoleCheckBox: return "check box";
    case RoleFrame: return "frame";
    case RoleImage: return "image";
    case RoleLabel: return "label";
    case RolePageTab: return "page tab";
    case RolePasswordText: return "password text";
    case RoleProgressBar: return "progress bar";
    case RoleButton: return "button";
    case RoleRadioButton: return "radio button";
    case RoleScrollPane: return "scroll pane";
    case RoleSlider: return "slider";
    case RoleApplication: return "application";
    case RoleEntry: return "entry";
    case RoleHeading: return "heading";
    case RoleSwitch: return "switch";
    default: return "panel";
    }
}

// A UTF-8 string's code points (AT-SPI's text offsets): where each starts, and its end.
std::vector<size_t> codePoints(const std::string& s) {
    std::vector<size_t> at;
    for (size_t i = 0; i < s.size(); ++i)
        if ((uint8_t(s[i]) & 0xC0) != 0x80) at.push_back(i);
    at.push_back(s.size());
    return at;
}
std::string slice(const std::string& s, const std::vector<size_t>& at, int from, int to) {
    int n = int(at.size()) - 1;
    from = std::clamp(from, 0, n), to = std::clamp(to < 0 ? n : to, from, n);
    return s.substr(at[size_t(from)], at[size_t(to)] - at[size_t(from)]);
}
uint32_t codePointAt(const std::string& s, size_t i) {
    auto b = [&](size_t k) { return k < s.size() ? uint32_t(uint8_t(s[k])) : 0u; };
    uint32_t c = b(i);
    if (c < 0x80) return c;
    if (c < 0xE0) return (c & 0x1F) << 6 | (b(i + 1) & 0x3F);
    if (c < 0xF0) return (c & 0x0F) << 12 | (b(i + 1) & 0x3F) << 6 | (b(i + 2) & 0x3F);
    return (c & 0x07) << 18 | (b(i + 1) & 0x3F) << 12 | (b(i + 2) & 0x3F) << 6 | (b(i + 3) & 0x3F);
}

// The piece of a text an offset is in: a character, a word (with the spaces after it) or the whole
// (a text field's one line). Code points [start, end).
enum class Unit { Char, Word, Line };
std::pair<int, int> segment(const std::string& s, const std::vector<size_t>& at, int offset, Unit unit) {
    int n = int(at.size()) - 1;
    if (offset < 0 || offset >= n) return {std::clamp(offset, 0, n), std::clamp(offset, 0, n)};
    if (unit == Unit::Char) return {offset, offset + 1};
    if (unit == Unit::Line) return {0, n};
    auto space = [&](int i) { return std::isspace(int(uint8_t(s[at[size_t(i)]]))) != 0; };
    int start = offset, end = offset;
    while (start > 0 && space(start)) --start;  // in the spaces after a word: that word's
    while (start > 0 && !space(start - 1)) --start;
    while (end < n && !space(end)) ++end;
    while (end < n && space(end)) ++end;
    return {start, end};
}

struct Rect4 {
    int x = 0, y = 0, width = 0, height = 0;
};

// An AT-SPI event, sent by the bus's thread.
struct Event {
    int object = 0;
    const char* interface = kEventObject;
    const char* member = "";
    std::string detail;
    int detail1 = 0, detail2 = 0;
    enum class Data { None, Text, Number, Ref } data = Data::None;
    std::string text;
    double number = 0;
    int ref = 0;
};

// What a node does (Action's actions, in order).
struct NodeAction {
    std::string name;
    SemanticsAction action;
    float argument = 0;
};
std::vector<NodeAction> actionsOf(const SemanticsNode& n) {
    std::vector<NodeAction> out;
    if (n.clickable || n.editable) out.push_back({"click", SemanticsAction::Click});
    if (n.scrollable) {
        out.push_back({"scroll forward", SemanticsAction::ScrollForward});
        out.push_back({"scroll backward", SemanticsAction::ScrollBackward});
    }
    for (size_t i = 0; i < n.customActions.size(); ++i) out.push_back({n.customActions[i], SemanticsAction::CustomAction, float(i)});
    return out;
}

// A text field's text as it is said: a password's hidden.
std::string textOf(const SemanticsNode& n) {
    if (!n.password) return n.value;
    std::string dots;
    for (size_t i = 1; i < codePoints(n.value).size(); ++i) dots += "\u2022";
    return dots;
}

} // namespace

struct AtspiBridge::State {
    Lib d;
    std::string appName;
    platform::Notifier notifier;
    int wakeFd = -1;
    std::atomic<bool> running{true}, connected{false}, treeRequest{false};
    std::thread thread;
    // The bus's thread's own.
    Connection* session = nullptr;
    Connection* bus = nullptr;  // the accessibility bus
    std::string uniqueName;
    std::string desktopName, desktopPath = kNullPath;  // the registry's desktop: the application's parent
    int32_t appId = 0;
    // The UI thread's, for the bus's thread: under the mutex.
    std::mutex mutex;
    std::vector<SemanticsNode> nodes;
    std::unordered_map<int, size_t> at;                  // id -> index in nodes
    std::unordered_map<int, std::vector<int>> children;  // parent id (kWindow: the top) -> ids
    Window window;
    bool published = false;
    std::vector<Event> events;
    std::vector<Request> requests;

    void wake() const {
        if (wakeFd < 0) return;
        uint64_t one = 1;
        [[maybe_unused]] ssize_t n = write(wakeFd, &one, sizeof one);
    }

    // ---- objects ----

    static std::string pathOf(int object) {
        if (object == kRoot) return kRootPath;
        if (object == kWindow) return kWindowPath;
        return std::string(kNodePrefix) + std::to_string(object);
    }
    static std::optional<int> objectOf(std::string_view path) {
        if (path == kRootPath) return kRoot;
        if (path == kWindowPath) return kWindow;
        if (!path.starts_with(kNodePrefix) || path.size() == kNodePrefix.size()) return std::nullopt;
        int id = 0;
        for (char c : path.substr(kNodePrefix.size())) {
            if (c < '0' || c > '9' || id > 100'000'000) return std::nullopt;
            id = id * 10 + (c - '0');
        }
        return id;
    }
    bool exists(int object) const { return object == kRoot || object == kWindow || at.contains(object); }
    const SemanticsNode& node(int id) const { return nodes[at.at(id)]; }
    const std::vector<int>& kidsOf(int object) const {
        static const std::vector<int> none;
        static const std::vector<int> window{kWindow};
        if (object == kRoot) return window;
        auto it = children.find(object);
        return it == children.end() ? none : it->second;
    }
    int parentOf(int object) const {
        if (object == kWindow) return kRoot;
        return object >= 0 ? node(object).parent : kRoot;
    }

    void ref(Writer& w, int object) {
        w.open(kTypeStruct, nullptr, [&](Writer& s) {
            s.str(uniqueName);
            s.path(pathOf(object));
        });
    }
    void nullRef(Writer& w) {
        w.open(kTypeStruct, nullptr, [&](Writer& s) {
            s.str("");
            s.path(kNullPath);
        });
    }

    std::vector<const char*> interfacesOf(int object) const {
        if (object == kRoot) return {kAccessible, kApplication};
        if (object == kWindow) return {kAccessible, kComponent};
        const SemanticsNode& n = node(object);
        std::vector<const char*> out{kAccessible, kComponent};
        if (!actionsOf(n).empty()) out.push_back(kAction);
        if (n.range) out.push_back(kValue);
        if (n.editable) out.insert(out.end(), {kText, kEditableText});
        return out;
    }
    bool implements(int object, std::string_view interface) const {
        auto all = interfacesOf(object);
        return std::any_of(all.begin(), all.end(), [&](const char* i) { return interface == i; });
    }

    uint32_t roleOfObject(int object) const {
        if (object == kRoot) return RoleApplication;
        if (object == kWindow) return RoleFrame;
        const SemanticsNode& n = node(object);
        uint32_t role = roleOf(n);
        return role == RolePanel && !n.label.empty() && kidsOf(object).empty() ? RoleLabel : role;
    }

    std::string nameOf(int object) const {
        if (object == kRoot) return appName;
        if (object == kWindow) return window.title;
        return node(object).label;
    }

    std::array<uint32_t, 2> statesOf(int object) const {
        std::array<uint32_t, 2> bits{};
        auto set = [&](int state) { bits[size_t(state / 32)] |= 1u << (state % 32); };
        if (object == kRoot) return bits;
        set(StateVisible), set(StateShowing);
        if (object == kWindow) {
            set(StateEnabled), set(StateSensitive), set(StateResizable);
            if (window.focused) set(StateActive);
            return bits;
        }
        const SemanticsNode& n = node(object);
        if (n.enabled) set(StateEnabled), set(StateSensitive);
        if (n.clickable || n.editable || n.settable) set(StateFocusable);
        if (n.focused) set(StateFocused);
        if (n.editable) set(StateEditable);
        if (n.checked) {
            if (n.role == SemanticsRole::Tab) {
                set(StateSelectable);
                if (*n.checked) set(StateSelected);
            } else {
                set(StateCheckable);
                if (*n.checked) set(StateChecked);
            }
        }
        if (n.range && *n.range == ProgressBarRangeInfo::Indeterminate) set(StateIndeterminate);
        return bits;
    }

    // Where it is, in points: on the screen, in the window, or from its parent's corner.
    Rect4 extentsOf(int object, uint32_t coordType) const {
        auto onScreen = [&](int o) -> std::array<float, 4> {
            if (o == kWindow || o == kRoot) return {window.x, window.y, window.width, window.height};
            Rect b = node(o).bounds;
            float k = window.dpToPoints;
            return {window.x + b.x * k, window.y + b.y * k, b.width * k, b.height * k};
        };
        auto r = onScreen(object);
        if (coordType == kCoordWindow) r[0] -= window.x, r[1] -= window.y;
        else if (coordType == kCoordParent && object >= 0) {
            auto p = onScreen(parentOf(object));
            r[0] -= p[0], r[1] -= p[1];
        }
        return {int(std::lround(r[0])), int(std::lround(r[1])), int(std::lround(r[2])), int(std::lround(r[3]))};
    }

    void notify() const {  // the main loop woken
        if (notifier.notify) notifier.notify();
    }
    void request(int id, SemanticsAction action, float argument = 0, std::string text = {}) {
        requests.push_back({id, action, argument, std::move(text)});
        notify();
    }

    // ---- properties ----

    // Writes the property as a variant; false: it has none of that name.
    bool property(Writer& w, int object, std::string_view interface, std::string_view name) {
        auto variant = [&](const char* signature, auto&& fill) {
            w.open(kTypeVariant, signature, fill);
            return true;
        };
        auto str = [&](const std::string& v) { return variant("s", [&](Writer& x) { x.str(v); }); };
        auto i32 = [&](int32_t v) { return variant("i", [&](Writer& x) { x.i32(v); }); };
        auto dbl = [&](double v) { return variant("d", [&](Writer& x) { x.dbl(v); }); };
        if (interface == kAccessible) {
            if (name == "Name") return str(nameOf(object));
            if (name == "Description") return str(object >= 0 ? node(object).stateDescription : std::string{});
            if (name == "Locale" || name == "AccessibleId" || name == "HelpText") return str({});
            if (name == "ChildCount") return i32(int32_t(kidsOf(object).size()));
            if (name == "Parent")
                return variant("(so)", [&](Writer& x) {
                    if (object != kRoot) ref(x, parentOf(object));
                    else
                        x.open(kTypeStruct, nullptr, [&](Writer& s) {
                            s.str(desktopName);
                            s.path(desktopPath);
                        });
                });
        } else if (interface == kApplication) {
            if (name == "ToolkitName") return str("Kompoz");
            if (name == "Version") return str({});
            if (name == "AtspiVersion") return str("2.1");
            if (name == "Id") return i32(appId);
        } else if (interface == kAction) {
            if (name == "NActions") return i32(int32_t(actionsOf(node(object)).size()));
        } else if (interface == kValue) {
            const ProgressBarRangeInfo& r = *node(object).range;
            if (name == "MinimumValue") return dbl(r.rangeStart);
            if (name == "MaximumValue") return dbl(r.rangeEnd);
            if (name == "CurrentValue") return dbl(r.current);
            if (name == "MinimumIncrement") return dbl(r.steps > 0 ? (r.rangeEnd - r.rangeStart) / float(r.steps + 1) : 0.0);
            if (name == "Text") return str(node(object).stateDescription);
        } else if (interface == kText) {
            if (name == "CharacterCount" || name == "CaretOffset") return i32(int32_t(codePoints(textOf(node(object))).size() - 1));
        }
        return false;
    }
    static std::vector<const char*> propertyNames(std::string_view interface) {
        if (interface == kAccessible) return {"Name", "Description", "Parent", "ChildCount", "Locale", "AccessibleId", "HelpText"};
        if (interface == kApplication) return {"ToolkitName", "Version", "AtspiVersion", "Id"};
        if (interface == kAction) return {"NActions"};
        if (interface == kValue) return {"MinimumValue", "MaximumValue", "MinimumIncrement", "CurrentValue", "Text"};
        if (interface == kText) return {"CharacterCount", "CaretOffset"};
        return {};
    }

    // ---- methods ----

    // The reply to a call on an object that exists; nullptr: no such method.
    Message* answer(Message* m, int object, std::string_view interface, std::string_view member) {
        Reader in(d, m);
        Message* reply = d.message_new_method_return(m);
        Writer out(d, reply);
        auto fail = [&] {
            d.message_unref(reply);
            return nullptr;
        };
        if (interface == kIntrospectable && member == "Introspect") {
            std::string xml = "<node>";
            for (const char* i : interfacesOf(object)) xml += std::string("<interface name=\"") + i + "\"/>";
            out.str(xml + "</node>");
            return reply;
        }
        if (interface == kProperties) {
            std::string iface = in.str();
            if (!implements(object, iface)) return fail();
            if (member == "Get") {
                std::string name = in.str();
                return in.ok && property(out, object, iface, name) ? reply : fail();
            }
            if (member == "GetAll") {
                out.open(kTypeArray, "{sv}", [&](Writer& dict) {
                    for (const char* name : propertyNames(iface))
                        dict.open(kTypeDictEntry, nullptr, [&](Writer& entry) {
                            entry.str(name);
                            property(entry, object, iface, name);
                        });
                });
                return reply;
            }
            if (member == "Set") {
                std::string name = in.str();
                Reader value = in.enter();
                if (iface == kApplication && name == "Id" && value.type() == kTypeInt32) appId = value.i32();
                else if (iface == kValue && name == "CurrentValue" && value.type() == kTypeDouble) {
                    double v = value.dbl();
                    if (node(object).settable) request(object, SemanticsAction::SetProgress, float(v));
                } else
                    return fail();
                return reply;
            }
            return fail();
        }
        if (!implements(object, interface)) return fail();
        if (interface == kAccessible) {
            const auto& kids = kidsOf(object);
            if (member == "GetChildren") {
                out.open(kTypeArray, "(so)", [&](Writer& a) {
                    for (int kid : kids) ref(a, kid);
                });
            } else if (member == "GetChildAtIndex") {
                int32_t i = in.i32();
                if (i >= 0 && size_t(i) < kids.size()) ref(out, kids[size_t(i)]);
                else nullRef(out);
            } else if (member == "GetIndexInParent") {
                const auto& siblings = kidsOf(parentOf(object));
                auto it = std::find(siblings.begin(), siblings.end(), object);
                out.i32(object == kRoot || it == siblings.end() ? -1 : int32_t(it - siblings.begin()));
            } else if (member == "GetRelationSet") {
                out.open(kTypeArray, "(ua(so))", [](Writer&) {});
            } else if (member == "GetRole") {
                out.u32(roleOfObject(object));
            } else if (member == "GetRoleName" || member == "GetLocalizedRoleName") {
                out.str(roleName(roleOfObject(object)));
            } else if (member == "GetState") {
                auto bits = statesOf(object);
                out.open(kTypeArray, "u", [&](Writer& a) {
                    a.u32(bits[0]);
                    a.u32(bits[1]);
                });
            } else if (member == "GetAttributes") {
                out.open(kTypeArray, "{ss}", [](Writer&) {});
            } else if (member == "GetApplication") {
                ref(out, kRoot);
            } else if (member == "GetInterfaces") {
                out.open(kTypeArray, "s", [&](Writer& a) {
                    for (const char* i : interfacesOf(object)) a.str(i);
                });
            } else
                return fail();
            return reply;
        }
        if (interface == kApplication) {
            if (member != "GetLocale") return fail();
            out.str({});
            return reply;
        }
        if (interface == kComponent) {
            if (member == "GetExtents") {
                Rect4 r = extentsOf(object, in.u32());
                out.open(kTypeStruct, nullptr, [&](Writer& s) {
                    s.i32(r.x), s.i32(r.y), s.i32(r.width), s.i32(r.height);
                });
            } else if (member == "GetPosition") {
                Rect4 r = extentsOf(object, in.u32());
                out.i32(r.x), out.i32(r.y);
            } else if (member == "GetSize") {
                Rect4 r = extentsOf(object, 0);
                out.i32(r.width), out.i32(r.height);
            } else if (member == "Contains" || member == "GetAccessibleAtPoint") {
                int32_t x = in.i32(), y = in.i32();
                uint32_t coordType = in.u32();
                auto inside = [&](int o) {
                    Rect4 r = extentsOf(o, coordType);
                    return x >= r.x && y >= r.y && x < r.x + r.width && y < r.y + r.height;
                };
                if (member == "Contains") out.boolean(inside(object));
                else {
                    const auto& kids = kidsOf(object);  // the last drawn on top
                    auto it = std::find_if(kids.rbegin(), kids.rend(), inside);
                    if (it != kids.rend()) ref(out, *it);
                    else nullRef(out);
                }
            } else if (member == "GetLayer") {
                out.u32(object == kWindow ? kLayerWindow : kLayerWidget);
            } else if (member == "GetMDIZOrder") {
                out.i16(0);
            } else if (member == "GrabFocus") {
                bool focusable = object >= 0 && (node(object).editable || (node(object).clickable && node(object).enabled));
                if (focusable) request(object, SemanticsAction::Focus);
                out.boolean(focusable);
            } else if (member == "GetAlpha") {
                out.dbl(1);
            } else if (member == "ScrollTo" || member == "ScrollToPoint" || member == "SetExtents" || member == "SetPosition" ||
                       member == "SetSize") {
                out.boolean(false);
            } else
                return fail();
            return reply;
        }
        if (interface == kAction) {
            auto actions = actionsOf(node(object));
            if (member == "GetActions") {
                out.open(kTypeArray, "(sss)", [&](Writer& a) {
                    for (const auto& action : actions)
                        a.open(kTypeStruct, nullptr, [&](Writer& s) {
                            s.str(action.name), s.str({}), s.str({});
                        });
                });
                return reply;
            }
            int32_t i = in.i32();
            bool valid = in.ok && i >= 0 && size_t(i) < actions.size();
            if (member == "GetName" || member == "GetLocalizedName") out.str(valid ? actions[size_t(i)].name : std::string{});
            else if (member == "GetDescription" || member == "GetKeyBinding") out.str({});
            else if (member == "DoAction") {
                if (valid) request(object, actions[size_t(i)].action, actions[size_t(i)].argument);
                out.boolean(valid);
            } else
                return fail();
            return reply;
        }
        if (interface == kText) {
            std::string text = textOf(node(object));
            auto cps = codePoints(text);
            int count = int(cps.size()) - 1;
            auto range = [&](std::pair<int, int> r) {
                out.str(slice(text, cps, r.first, r.second)), out.i32(r.first), out.i32(r.second);
            };
            if (member == "GetText") {
                int32_t from = in.i32(), to = in.i32();
                out.str(slice(text, cps, from, to));
            } else if (member == "GetCharacterAtOffset") {
                int32_t i = in.i32();
                out.i32(i >= 0 && i < count ? int32_t(codePointAt(text, cps[size_t(i)])) : 0);
            } else if (member == "GetStringAtOffset") {
                int32_t offset = in.i32();
                uint32_t granularity = in.u32();  // AtspiTextGranularity: char, word, sentence, line, paragraph
                range(segment(text, cps, offset, granularity == 0 ? Unit::Char : granularity == 1 ? Unit::Word : Unit::Line));
            } else if (member == "GetTextAtOffset" || member == "GetTextBeforeOffset" || member == "GetTextAfterOffset") {
                int32_t offset = in.i32();
                uint32_t boundary = in.u32();  // AtspiTextBoundaryType: char, word start/end, sentence start/end, line start/end
                Unit unit = boundary == 0 ? Unit::Char : boundary <= 2 ? Unit::Word : Unit::Line;
                auto r = segment(text, cps, offset, unit);
                if (member == "GetTextBeforeOffset") r = r.first > 0 ? segment(text, cps, r.first - 1, unit) : std::pair{0, 0};
                if (member == "GetTextAfterOffset") r = segment(text, cps, r.second, unit);
                range(r);
            } else if (member == "GetNSelections") {
                out.i32(0);
            } else if (member == "GetSelection") {
                out.i32(0), out.i32(0);
            } else if (member == "SetCaretOffset" || member == "AddSelection" || member == "RemoveSelection" ||
                       member == "SetSelection") {
                out.boolean(false);
            } else if (member == "GetAttributes" || member == "GetAttributeRun") {
                out.open(kTypeArray, "{ss}", [](Writer&) {});
                out.i32(0), out.i32(count);
            } else if (member == "GetDefaultAttributes" || member == "GetDefaultAttributeSet") {
                out.open(kTypeArray, "{ss}", [](Writer&) {});
            } else if (member == "GetCharacterExtents" || member == "GetRangeExtents") {
                in.i32();
                if (member == "GetRangeExtents") in.i32();
                Rect4 r = extentsOf(object, in.u32());
                out.i32(r.x), out.i32(r.y), out.i32(r.width), out.i32(r.height);
            } else if (member == "GetOffsetAtPoint") {
                out.i32(-1);
            } else
                return fail();
            return reply;
        }
        if (interface == kEditableText) {
            const std::string& value = node(object).value;
            auto cps = codePoints(value);
            if (member == "SetTextContents") {
                std::string text = in.str();
                request(object, SemanticsAction::SetText, 0, text);
            } else if (member == "InsertText") {
                int32_t position = in.i32();
                std::string text = in.str();
                int32_t length = in.i32();
                text = slice(text, codePoints(text), 0, length);
                request(object, SemanticsAction::SetText, 0, slice(value, cps, 0, position) + text + slice(value, cps, position, -1));
            } else if (member == "DeleteText") {
                int32_t from = in.i32(), to = in.i32();
                request(object, SemanticsAction::SetText, 0, slice(value, cps, 0, from) + slice(value, cps, to, -1));
            } else if (member == "CopyText") {
                return reply;  // (nothing to say)
            } else if (member == "CutText" || member == "PasteText") {
                out.boolean(false);
                return reply;
            } else
                return fail();
            if (!in.ok) return fail();
            out.boolean(true);
            return reply;
        }
        return fail();
    }

    void handle(Message* m) {
        const char* p = d.message_get_path(m);
        const char* i = d.message_get_interface(m);
        const char* n = d.message_get_member(m);
        std::optional<int> object = objectOf(p ? p : "");
        Message* reply = nullptr;
        if (p && std::string_view(p) == kCachePath && n && std::string_view(n) == "GetItems") {
            // Nothing cached up front: what is asked for is read as it is then.
            reply = d.message_new_method_return(m);
            Writer(d, reply).open(kTypeArray, "((so)(so)(so)iiassusau)", [](Writer&) {});
        } else {
            std::lock_guard lock(mutex);
            if (!object || !exists(*object)) reply = d.message_new_error(m, "org.freedesktop.DBus.Error.UnknownObject", "No such object");
            else if (!(reply = answer(m, *object, i ? i : "", n ? n : "")))
                reply = d.message_new_error(m, "org.freedesktop.DBus.Error.UnknownMethod", "No such method");
        }
        if (!reply) return;
        if (!d.message_get_no_reply(m)) d.connection_send(bus, reply, nullptr);
        d.message_unref(reply);
    }

    void send(const Event& e) {
        Message* s = d.message_new_signal(pathOf(e.object).c_str(), e.interface, e.member);
        if (!s) return;
        Writer w(d, s);
        w.str(e.detail), w.i32(e.detail1), w.i32(e.detail2);
        switch (e.data) {
        case Event::Data::None: w.open(kTypeVariant, "i", [](Writer& v) { v.i32(0); }); break;
        case Event::Data::Text: w.open(kTypeVariant, "s", [&](Writer& v) { v.str(e.text); }); break;
        case Event::Data::Number: w.open(kTypeVariant, "d", [&](Writer& v) { v.dbl(e.number); }); break;
        case Event::Data::Ref: w.open(kTypeVariant, "(so)", [&](Writer& v) { ref(v, e.ref); }); break;
        }
        w.open(kTypeArray, "{sv}", [](Writer&) {});
        d.connection_send(bus, s, nullptr);
        d.message_unref(s);
    }

    // ---- what changed ----

    void event(int object, const char* interface, const char* member, std::string detail = {}, int detail1 = 0, int detail2 = 0) {
        events.push_back({object, interface, member, std::move(detail), detail1, detail2});
    }
    void textEvent(int object, const char* member, std::string detail, int detail1, int detail2, std::string text) {
        Event e{object, kEventObject, member, std::move(detail), detail1, detail2, Event::Data::Text};
        e.text = std::move(text);
        events.push_back(std::move(e));
    }

    // The new tree against the one before (under the mutex): events for what changed.
    void compare(const std::vector<SemanticsNode>& next, const std::unordered_map<int, size_t>& nextAt,
                 const std::unordered_map<int, std::vector<int>>& nextChildren, const Window& nextWindow) {
        auto indexIn = [](const std::unordered_map<int, std::vector<int>>& kids, int parent, int id) {
            auto it = kids.find(parent);
            if (it == kids.end()) return -1;
            auto at = std::find(it->second.begin(), it->second.end(), id);
            return at == it->second.end() ? -1 : int(at - it->second.begin());
        };
        auto childEvent = [&](int parent, const char* what, int index, int child) {
            Event e{parent, kEventObject, "ChildrenChanged", what, index, 0, Event::Data::Ref};
            e.ref = child;
            events.push_back(std::move(e));
        };
        if (published) {
            // Whole subtrees come and go: told on the parent that stays (the last first, so that
            // the indexes of those before still hold).
            for (const SemanticsNode& old : std::views::reverse(nodes))
                if (!nextAt.contains(old.id) && (old.parent == kWindow || nextAt.contains(old.parent)))
                    childEvent(old.parent, "remove", indexIn(children, old.parent, old.id), old.id);
            for (const SemanticsNode& n : next)
                if (!at.contains(n.id) && (n.parent == kWindow || at.contains(n.parent)))
                    childEvent(n.parent, "add", indexIn(nextChildren, n.parent, n.id), n.id);
        }
        if (!published ? nextWindow.focused : nextWindow.focused != window.focused) {
            textEvent(kWindow, nextWindow.focused ? "Activate" : "Deactivate", {}, 0, 0, nextWindow.title);
            events.back().interface = kEventWindow;
            event(kWindow, kEventObject, "StateChanged", "active", nextWindow.focused);
        }
        if (published && nextWindow.title != window.title)
            textEvent(kWindow, "PropertyChange", "accessible-name", 0, 0, nextWindow.title);
        int focusedBefore = -1, focusedNow = -1;
        for (const SemanticsNode& old : nodes)
            if (old.focused) focusedBefore = old.id;
        for (const SemanticsNode& n : next) {
            if (n.focused) focusedNow = n.id;
            auto it = at.find(n.id);
            if (!published || it == at.end()) continue;
            const SemanticsNode& old = nodes[it->second];
            if (n.label != old.label) textEvent(n.id, "PropertyChange", "accessible-name", 0, 0, n.label);
            if (n.stateDescription != old.stateDescription)
                textEvent(n.id, "PropertyChange", "accessible-description", 0, 0, n.stateDescription);
            if (n.liveRegion != LiveRegionMode::None && (n.label != old.label || n.stateDescription != old.stateDescription)) {
                std::string said = n.label;
                if (!n.stateDescription.empty()) said += (said.empty() ? "" : ", ") + n.stateDescription;
                textEvent(n.id, "Announcement", {}, n.liveRegion == LiveRegionMode::Assertive ? 2 : 1, 0, said);  // AtspiLive
            }
            if (n.checked.value_or(false) != old.checked.value_or(false))
                event(n.id, kEventObject, "StateChanged", n.role == SemanticsRole::Tab ? "selected" : "checked", n.checked.value_or(false));
            if (n.enabled != old.enabled) {
                event(n.id, kEventObject, "StateChanged", "enabled", n.enabled);
                event(n.id, kEventObject, "StateChanged", "sensitive", n.enabled);
            }
            if (n.range && old.range && n.range->current != old.range->current) {
                Event e{n.id, kEventObject, "PropertyChange", "accessible-value", 0, 0, Event::Data::Number};
                e.number = n.range->current;
                events.push_back(std::move(e));
            }
            if (n.editable && old.editable && n.value != old.value) {
                // What was typed or taken away: what lies between the same start and the same end.
                std::string before = textOf(old), after = textOf(n);
                auto a = codePoints(before), b = codePoints(after);
                int na = int(a.size()) - 1, nb = int(b.size()) - 1, head = 0, tail = 0;
                auto same = [&](int i, int j) { return slice(before, a, i, i + 1) == slice(after, b, j, j + 1); };
                while (head < na && head < nb && same(head, head)) ++head;
                while (tail < na - head && tail < nb - head && same(na - 1 - tail, nb - 1 - tail)) ++tail;
                if (na - head - tail > 0) textEvent(n.id, "TextChanged", "delete", head, na - head - tail, slice(before, a, head, na - tail));
                if (nb - head - tail > 0) textEvent(n.id, "TextChanged", "insert", head, nb - head - tail, slice(after, b, head, nb - tail));
            }
        }
        if (focusedNow != focusedBefore || (!published && focusedNow >= 0)) {
            if (published && focusedBefore >= 0 && nextAt.contains(focusedBefore))
                event(focusedBefore, kEventObject, "StateChanged", "focused", 0);
            if (focusedNow >= 0) {
                event(focusedNow, kEventObject, "StateChanged", "focused", 1);
                event(focusedNow, kEventFocus, "Focus");
            }
        }
    }

    // ---- the bus ----

    Message* call(Connection* c, const char* destination, const char* path, const char* interface, const char* method,
                  const std::function<void(Writer&)>& args = {}) {
        Message* m = d.message_new_method_call(destination, path, interface, method);
        if (!m) return nullptr;
        if (args) {
            Writer w(d, m);
            args(w);
        }
        Error error;
        d.error_init(&error);
        Message* reply = d.connection_send_with_reply_and_block(c, m, 2000, &error);
        d.error_free(&error);
        d.message_unref(m);
        return reply;
    }

    // Whether the desktop's accessibility is on (what Qt asks too).
    bool statusEnabled() {
        for (const char* name : {"IsEnabled", "ScreenReaderEnabled"}) {
            Message* reply = call(session, "org.a11y.Bus", "/org/a11y/bus", kProperties, "Get", [&](Writer& w) {
                w.str("org.a11y.Status"), w.str(name);
            });
            if (!reply) return false;  // no accessibility bus here
            Reader r(d, reply);
            Reader v = r.enter();
            bool on = v.type() == kTypeBoolean && v.boolean();
            d.message_unref(reply);
            if (on) return true;
        }
        return false;
    }

    // Until something is there to read (or the UI thread wakes it); false: stopping.
    bool wait(Connection* c) {
        int fd = -1;
        d.connection_get_unix_fd(c, &fd);
        pollfd fds[2] = {{fd, POLLIN, 0}, {wakeFd, POLLIN, 0}};
        poll(fds, 2, -1);
        uint64_t count;
        if (fds[1].revents & POLLIN) [[maybe_unused]] ssize_t n = read(wakeFd, &count, sizeof count);
        return running.load();
    }

    // A connection of its own to a bus, registered on it; nullptr: none.
    Connection* open(const char* address) {
        Error error;
        d.error_init(&error);
        Connection* c = d.connection_open_private(address, &error);
        d.error_free(&error);
        if (!c) return nullptr;
        d.connection_set_exit_on_disconnect(c, 0);
        d.error_init(&error);
        bool registered = d.bus_register(c, &error);
        d.error_free(&error);
        if (registered) return c;
        d.connection_close(c);
        d.connection_unref(c);
        return nullptr;
    }

    bool connect() {
        std::string address;
        if (const char* env = std::getenv("AT_SPI_BUS_ADDRESS"); env && *env) address = env;
        else if (session) {
            if (Message* reply = call(session, "org.a11y.Bus", "/org/a11y/bus", "org.a11y.Bus", "GetAddress")) {
                address = Reader(d, reply).str();
                d.message_unref(reply);
            }
        }
        if (address.empty() || !(bus = open(address.c_str()))) return false;
        uniqueName = d.bus_get_unique_name(bus);
        // Into the registry's desktop, which tells assistive technologies the app is there.
        Message* reply = call(bus, "org.a11y.atspi.Registry", kRootPath, "org.a11y.atspi.Socket", "Embed", [&](Writer& w) {
            w.open(kTypeStruct, nullptr, [&](Writer& s) {
                s.str(uniqueName);
                s.path(kRootPath);
            });
        });
        if (reply) {
            Reader r(d, reply);
            Reader desktop = r.enter();
            std::string name = desktop.str(), path = desktop.path();
            if (desktop.ok) desktopName = name, desktopPath = path;
            d.message_unref(reply);
        }
        return true;
    }

    void run() {
        const char* forced = std::getenv("KOMPOZ_ATSPI");
        bool always = forced && std::string_view(forced) == "1";
        Error error;
        // The session bus as it is now set (libdbus keeps the address it first saw: SDL's).
        if (const char* address = std::getenv("DBUS_SESSION_BUS_ADDRESS"); address && *address) session = open(address);
        else {
            d.error_init(&error);
            session = d.bus_get_private(kSessionBus, &error);
            d.error_free(&error);
            if (session) d.connection_set_exit_on_disconnect(session, 0);
        }
        if (!always) {
            if (!session) return;
            d.error_init(&error);
            d.bus_add_match(session, "type='signal',interface='org.freedesktop.DBus.Properties',member='PropertiesChanged',path='/org/a11y/bus'",
                            &error);
            d.error_free(&error);
            bool enabled = statusEnabled();
            while (!enabled) {  // until it is turned on
                if (!wait(session) || !d.connection_read_write(session, 0)) return;
                while (Message* m = d.connection_pop_message(session)) {
                    if (d.message_is_signal(m, kProperties, "PropertiesChanged")) enabled = enabled || statusEnabled();
                    d.message_unref(m);
                }
            }
        }
        if (!running.load() || !connect()) return;
        if (session) {  // not needed any more
            d.connection_close(session);
            d.connection_unref(std::exchange(session, nullptr));
        }
        connected = true;
        treeRequest = true;
        notify();  // to publish the tree
        while (running.load()) {
            if (!d.connection_read_write(bus, 0)) break;  // the bus went away
            while (Message* m = d.connection_pop_message(bus)) {
                if (d.message_get_type(m) == kMessageMethodCall) handle(m);
                d.message_unref(m);
            }
            std::vector<Event> pending;
            {
                std::lock_guard lock(mutex);
                pending.swap(events);
            }
            for (const Event& e : pending) send(e);
            d.connection_flush(bus);
            if (!wait(bus)) break;
        }
        connected = false;
    }
};

AtspiBridge::AtspiBridge(std::string appName, platform::Notifier notifier) : state_(std::make_shared<State>()) {
    State& s = *state_;
    s.appName = std::move(appName);
    s.notifier = std::move(notifier);
    const char* env = std::getenv("KOMPOZ_ATSPI");
    if ((env && std::string_view(env) == "0") || !s.d.load()) return;
    s.wakeFd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (s.wakeFd < 0) return;
    s.thread = std::thread([&s] {
        s.run();
        if (s.notifier.threadDone) s.notifier.threadDone();
    });
}

AtspiBridge::~AtspiBridge() {
    State& s = *state_;
    s.running = false;
    s.wake();
    if (s.thread.joinable()) s.thread.join();
    for (Connection* c : {s.bus, s.session})
        if (c) {
            s.d.connection_close(c);
            s.d.connection_unref(c);
        }
    if (s.wakeFd >= 0) close(s.wakeFd);
}

bool AtspiBridge::active() const { return state_->connected.load(); }

bool AtspiBridge::takeTreeRequest() { return state_->treeRequest.exchange(false); }

void AtspiBridge::publish(std::vector<SemanticsNode> nodes, const Window& window) {
    State& s = *state_;
    std::unordered_map<int, size_t> at;
    std::unordered_map<int, std::vector<int>> children;
    for (size_t i = 0; i < nodes.size(); ++i) {
        at[nodes[i].id] = i;
        children[nodes[i].parent].push_back(nodes[i].id);
    }
    {
        std::lock_guard lock(s.mutex);
        if (s.connected) s.compare(nodes, at, children, window);
        s.nodes = std::move(nodes);
        s.at = std::move(at);
        s.children = std::move(children);
        s.window = window;
        s.published = true;
    }
    s.wake();
}

bool AtspiBridge::listening() const { return state_->thread.joinable(); }

std::vector<AtspiBridge::Request> AtspiBridge::takeRequests() {
    std::lock_guard lock(state_->mutex);
    return std::exchange(state_->requests, {});
}

} // namespace kompoz::platform
