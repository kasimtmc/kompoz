#pragma once

// A tree the user opens and closes, its rows composed as they come into view: files and folders, a
// document's outline, settings in groups. Compose has none; this is the API of JetBrains' Jewel
// (IntelliJ's Compose UI) LazyTree:
//
//   val tree = remember {
//       buildTree {
//           addNode("src") {
//               addLeaf("main.cpp")
//               addNode("ui") { addLeaf("list.cpp") }
//           }
//           addLeaf("README.md")
//       }
//   }
//   val treeState = rememberTreeState()
//   LazyTree(tree = tree, treeState = treeState, onElementDoubleClick = { open(it.data) }) { element ->
//       Text(element.data)
//   }
//
// A node's block (its children) runs when it is first opened, so a big tree (a disk) is built as
// it is looked at; an element's id is its data unless given (addNode(data, id = …)), and the ids
// must differ. A row shows its element's content after an arrow that opens and closes it (a
// double click does too), indented by its depth; a click selects it (Ctrl adds or takes one away,
// Shift a range: SelectionMode). The tree takes the keyboard focus as one: Up and Down move the
// selection (Shift extends it), Right opens a node or goes to its first child, Left closes it or
// goes to its parent, Home and End, Enter is a double click, Ctrl+A selects all. To accessibility
// services each row says whether it is selected and open, with actions that open and close it.

#include "lazy.hpp"
#include "layout.hpp"
#include "named.hpp"
#include "runtime.hpp"
#include "saved_value.hpp"
#include "state.hpp"

#include <any>
#include <functional>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <type_traits>
#include <vector>

namespace kompoz {

// What a click selects (Jewel's SelectionMode): nothing, one element, or several (Ctrl and Shift).
enum class SelectionMode { None, Single, Multiple };

namespace detail {

// An element as the tree's layout sees it, its data aside.
struct TreeElementBase : std::enable_shared_from_this<TreeElementBase> {
    size_t key = 0;  // its id, hashed as a lazy item's key
    int depth = 0;
    std::weak_ptr<TreeElementBase> parentElement;
    bool node = false;
    // A node's children, made by its block when first asked.
    std::vector<std::shared_ptr<TreeElementBase>> made;
    bool childrenMade = false;
    std::function<void()> generator;
    std::function<std::shared_ptr<TreeElementBase>(std::any data)> make;  // (its children: of the tree's type)
    const std::vector<std::shared_ptr<TreeElementBase>>& childElements();
    virtual ~TreeElementBase() = default;
};

// The buildTree block's (and a node's block's): where addNode and addLeaf add.
struct TreeBuilder {
    std::function<std::shared_ptr<TreeElementBase>(std::any data)> make;  // an element of the tree's type
    std::vector<std::shared_ptr<TreeElementBase>>* into = nullptr;
    std::weak_ptr<TreeElementBase> parent;
    int depth = 0;
};
TreeBuilder*& treeBuilder();
[[noreturn]] void outsideTree(const char* function);
void addTreeElement(std::any data, size_t key, bool node, std::function<void()> generator);

} // namespace detail

template <class T = std::string>
class Tree {
public:
    class Element : public detail::TreeElementBase {
    public:
        const T& data() const { return data_; }
        int depth() const { return depth_(); }
        // The one it is in (none for a root).
        std::shared_ptr<const Element> parent() const { return std::static_pointer_cast<const Element>(parentElement.lock()); }
        bool isNode() const { return node; }  // (Jewel's Tree.Element.Node)
        bool isLeaf() const { return !node; }
        // A node's children (its block runs the first time they are asked for); a leaf's: none.
        std::vector<std::shared_ptr<const Element>> children() const {
            std::vector<std::shared_ptr<const Element>> out;
            for (const auto& c : const_cast<Element*>(this)->childElements()) out.push_back(std::static_pointer_cast<const Element>(c));
            return out;
        }
        size_t key() const { return detail::TreeElementBase::key; }  // its id, hashed (what TreeState keeps)

        T data_;

    private:
        int depth_() const { return detail::TreeElementBase::depth; }
    };

    const std::vector<std::shared_ptr<detail::TreeElementBase>>& rootElements() const { return data_->roots; }
    std::vector<std::shared_ptr<const Element>> roots() const {
        std::vector<std::shared_ptr<const Element>> out;
        for (const auto& r : data_->roots) out.push_back(std::static_pointer_cast<const Element>(r));
        return out;
    }
    bool isEmpty() const { return data_->roots.empty(); }
    friend bool operator==(const Tree& a, const Tree& b) { return a.data_ == b.data_; }

    struct Data {
        std::vector<std::shared_ptr<detail::TreeElementBase>> roots;
    };
    std::shared_ptr<Data> data_ = std::make_shared<Data>();
};

// buildTree { addNode(…) { … }; addLeaf(…) }: a tree of T (Kotlin's buildTree<T>; strings unless said).
template <class T = std::string, class F>
Tree<T> buildTree(F&& block) {
    Tree<T> tree;
    detail::TreeBuilder builder;
    builder.make = [](std::any data) -> std::shared_ptr<detail::TreeElementBase> {
        auto e = std::make_shared<typename Tree<T>::Element>();
        if (auto* t = std::any_cast<T>(&data)) e->data_ = std::move(*t);
        else if constexpr (std::is_constructible_v<T, const char*>) {
            if (auto* s = std::any_cast<const char*>(&data)) e->data_ = T(*s);
        }
        return e;
    };
    builder.into = &tree.data_->roots;
    detail::TreeBuilder* saved = std::exchange(detail::treeBuilder(), &builder);
    block();
    detail::treeBuilder() = saved;
    return tree;
}

namespace detail {
template <class D>
std::any treeData(D&& data) {
    if constexpr (std::is_convertible_v<D, const char*> && !std::is_same_v<std::decay_t<D>, std::nullptr_t>) return std::any(std::string(data));
    else return std::any(std::decay_t<D>(std::forward<D>(data)));
}
} // namespace detail

// In buildTree's block (and a node's): a node, its children made by `block` when it is first
// opened (addNode(data, id = …) { … }), and a leaf.
template <class D, class... A>
void addNode(D&& data, A&&... args) {
    using P = detail::Params<detail::names::Id, detail::Block>;
    static_assert(P::check<A...>());
    static_assert(P::blockGiven<A...>(), "kompoz: addNode takes a block: its children (addNode, addLeaf)");
    size_t key;
    if constexpr (P::given<detail::names::Id, A...>()) key = LazyListScope::hashedKey(P::arg<detail::names::Id>(args...));
    else key = LazyListScope::hashedKey(data);
    auto&& block = std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
    detail::addTreeElement(detail::treeData(std::forward<D>(data)), key, true, std::function<void()>(block));
}
template <class D, class... A>
void addLeaf(D&& data, A&&... args) {
    using P = detail::Params<detail::names::Id>;
    static_assert(P::check<A...>());
    size_t key;
    if constexpr (P::given<detail::names::Id, A...>()) key = LazyListScope::hashedKey(P::arg<detail::names::Id>(args...));
    else key = LazyListScope::hashedKey(data);
    detail::addTreeElement(detail::treeData(std::forward<D>(data)), key, false, {});
}

// Which nodes are open and which elements selected (Jewel's TreeState), by id; and the rows'
// scroll position. Saved with the app's state.
class TreeState {
public:
    TreeState();
    // Observable: the open nodes' and the selected elements' ids (hashed: an element's key()).
    const std::set<size_t>& openNodes() const;
    const std::set<size_t>& selectedKeys() const;
    template <class K>
    bool isNodeOpen(const K& id) const {
        return openNodes().count(keyOf(id)) > 0;
    }
    template <class K>
    bool isSelected(const K& id) const {
        return selectedKeys().count(keyOf(id)) > 0;
    }
    // Opens a closed node and closes an open one; opens some; closes one; selects.
    template <class K>
    void toggleNode(const K& id) const {
        toggleKey(keyOf(id));
    }
    template <class K>
    void openNodes(const std::vector<K>& ids) const {
        std::set<size_t> keys;
        for (const K& id : ids) keys.insert(keyOf(id));
        openKeys(keys);
    }
    template <class K>
    void closeNode(const K& id) const {
        closeKey(keyOf(id));
    }
    template <class K>
    void selectOnly(const K& id) const {
        setSelected({keyOf(id)});
    }
    void clearSelection() const { setSelected({}); }
    const LazyListState& lazyListState() const;

    void toggleKey(size_t key) const;
    void openKeys(const std::set<size_t>& keys) const;
    void closeKey(size_t key) const;
    void setSelected(std::set<size_t> keys) const;
    void setOpen(std::set<size_t> keys) const;
    friend bool operator==(const TreeState& a, const TreeState& b) { return a.data_ == b.data_; }

    struct Data;
    std::shared_ptr<Data> data_;

private:
    // An element's key as it is (from element.key()), any other id hashed as addNode hashed it.
    template <class K>
    static size_t keyOf(const K& id) {
        if constexpr (std::is_base_of_v<detail::TreeElementBase, std::remove_cvref_t<K>>) return static_cast<const detail::TreeElementBase&>(id).key;
        else if constexpr (requires { static_cast<const detail::TreeElementBase&>(*id); }) return static_cast<const detail::TreeElementBase&>(*id).key;
        else return LazyListScope::hashedKey(id);
    }
};

template <>
struct Saver<TreeState> {
    static SavedValue save(const TreeState& s);
    static std::optional<TreeState> restore(const SavedValue& v);
};

TreeState rememberTreeState(CallSite site = CallSite::current());

// The defaults (Jewel's LazyTree style's metrics).
struct LazyTreeDefaults {
    static constexpr float IndentSize = 20;  // dp a row moves in per level of depth
};

namespace detail {

struct LazyTreeOptions {
    std::optional<TreeState> state;
    SelectionMode selectionMode = SelectionMode::Multiple;
    float indentSize = LazyTreeDefaults::IndentSize;
    std::function<void(const TreeElementBase&)> onElementClick, onElementDoubleClick;
    std::function<void(const std::vector<std::shared_ptr<TreeElementBase>>&)> onSelectionChange;
};
void lazyTree(const std::vector<std::shared_ptr<TreeElementBase>>& roots, const Modifier& modifier, const LazyTreeOptions& options,
              const std::function<void(const TreeElementBase&)>& content, CallSite site);

// LazyTree(tree, modifier, treeState, selectionMode, onElementClick, onElementDoubleClick, onSelectionChange,
// indentSize) { element -> … }
struct LazyTreeImpl {
    template <class... A>
    static void call(CallSite site, A&&... args) {
        using First = Params<names::Tree>;
        using TreeType = std::remove_cvref_t<decltype(First::arg<names::Tree>(args...))>;
        using E = typename TreeType::Element;
        using P = Params<names::Tree, names::Modifier, names::TreeState, names::SelectionMode, names::OnElementClick, names::OnElementDoubleClick,
                         names::OnSelectionChange, names::IndentSize, BlockWith<const E&>>;
        static_assert(P::template check<A...>());
        static_assert(P::template blockGiven<A...>(), "kompoz: LazyTree takes a block: an element's content, { element -> … }");
        const TreeType& tree = First::arg<names::Tree>(args...);
        LazyTreeOptions options;
        if constexpr (P::template given<names::TreeState, A...>()) options.state = P::template arg<names::TreeState>(args...);
        P::template set<names::SelectionMode>(options.selectionMode, args...);
        P::template set<names::IndentSize>(options.indentSize, args...);
        auto typed = [](const TreeElementBase& e) -> const E& { return static_cast<const E&>(e); };
        if constexpr (P::template given<names::OnElementClick, A...>())
            options.onElementClick = [f = P::template arg<names::OnElementClick>(args...), typed](const TreeElementBase& e) mutable { f(typed(e)); };
        if constexpr (P::template given<names::OnElementDoubleClick, A...>())
            options.onElementDoubleClick = [f = P::template arg<names::OnElementDoubleClick>(args...), typed](const TreeElementBase& e) mutable {
                f(typed(e));
            };
        if constexpr (P::template given<names::OnSelectionChange, A...>())
            options.onSelectionChange = [f = P::template arg<names::OnSelectionChange>(args...)](
                                            const std::vector<std::shared_ptr<TreeElementBase>>& elements) mutable {
                std::vector<std::shared_ptr<const E>> out;
                for (const auto& e : elements) out.push_back(std::static_pointer_cast<const E>(e));
                f(out);
            };
        auto&& block = std::get<sizeof...(A) - 1>(std::forward_as_tuple(args...));
        std::function<void(const TreeElementBase&)> content = [block = std::decay_t<decltype(block)>(block), typed](const TreeElementBase& e) mutable {
            block(typed(e));
        };
        lazyTree(tree.rootElements(), P::template get<names::Modifier>(Modifier{}, args...), options, content, site);
    }
};

} // namespace detail

// The tree's rows, composed as they come into view (a LazyColumn of the open nodes' elements).
inline constexpr Composable<detail::LazyTreeImpl> LazyTree;

} // namespace kompoz
