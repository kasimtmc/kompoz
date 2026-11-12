#include "codegen.hpp"

#include <algorithm>
#include <cctype>
#include <functional>
#include <set>
#include <utility>
#include <vector>

namespace kompozc {

// ---- Output -------------------------------------------------------------------------------------

void Output::at(Pos pos) {
    if (pos.line == current_) return;
    if (current_ != 0 && pos.line > current_ && pos.line - current_ <= 4) {
        out_.append(size_t(pos.line - current_), '\n');
    } else {
        if (!out_.empty() && out_.back() != '\n') out_ += '\n';
        out_ += "#line " + std::to_string(pos.line) + " \"" + file_ + "\"\n";
    }
    current_ = pos.line;
}

void Output::text(std::string_view s) {
    for (char c : s)
        if (c == '\n' && current_ != 0) ++current_;
    out_ += s;
}

void Output::line(int n) {
    if (!out_.empty() && out_.back() != '\n') out_ += '\n';
    out_ += "#line " + std::to_string(n) + " \"" + file_ + "\"\n";
    current_ = n;
}

namespace {

// Kotlin functions whose lambda gives a value (its last expression); others' lambdas give none.
const std::set<std::string, std::less<>> kValueLambdas = {
    "remember", "rememberSaveable", "derivedStateOf", "map", "mapNotNull", "mapIndexed", "filter", "filterNot",
    "filterIndexed", "sortedBy", "sortedByDescending", "any", "all", "none", "count", "first", "firstOrNull",
    "last", "lastOrNull", "find", "groupBy", "associateBy", "associateWith", "flatMap", "sumOf", "maxOf", "minOf",
    "maxOfOrNull", "minOfOrNull",
    "maxByOrNull", "minByOrNull", "let", "run", "with", "takeIf", "takeUnless", "getOrElse", "lazy", "snapshotFlow",
    "List", "buildString", "buildList", "indexOfFirst", "indexOfLast", "partition", "distinctBy", "chunked",
    "DisposableEffect", "async", "withBackground", "offset", "absoluteOffset", "drawWithCache", "shader", "kompozitionLocalOf",
    "slideInVertically", "slideInHorizontally", "slideOutVertically", "slideOutHorizontally", "animateFloat", "animateDp", "animateColor",
    "dragAndDropSource", "update", "LifecycleResumeEffect", "LifecycleStartEffect", "flatMapLatest", "mapLatest", "flatMapConcat",
    "removeIf", "removeAll", "ifEmpty", "ifBlank", "byValue", "rememberScrollableState", "rememberScrollable2DState",
    "staticKompozitionLocalOf", "CustomAccessibilityAction", "setProgress", "onKeyEvent", "onPreviewKeyEvent", "mediaQuery",
    "derivedMediaQuery", "SubkompozLayout", "rememberPagerState", "rememberCarouselState"};

// Kotlin types the C++ way.
const std::map<std::string, std::string, std::less<>> kTypes = {
    {"String", "std::string"},   {"Int", "int"},           {"Long", "long long"},     {"Short", "short"},
    {"Byte", "signed char"},     {"Float", "float"},       {"Double", "double"},      {"Boolean", "bool"},
    {"Char", "char32_t"},        {"Unit", "void"},         {"UInt", "unsigned"},      {"ULong", "unsigned long long"},
    {"Exception", "std::exception"}, {"Throwable", "std::exception"}, {"RuntimeException", "std::runtime_error"},
    {"IllegalStateException", "std::logic_error"}, {"IllegalArgumentException", "std::invalid_argument"},
    {"KompozTestRule", "::kompoz::testing::KompozTestRule"}, {"AssertionError", "::kompoz::testing::AssertionError"},
    {"Dp", "float"},             {"List", "std::vector"},  {"MutableList", "std::vector"}, {"ArrayList", "std::vector"},
    {"Set", "std::set"},         {"MutableSet", "std::set"}, {"Map", "std::map"},    {"MutableMap", "std::map"},
    {"Pair", "std::pair"},       {"LocalDate", "std::chrono::year_month_day"}, {"Any", "std::any"}};

// Objects whose Kotlin properties are calls here: MaterialTheme.colorScheme -> colorScheme().
const std::map<std::string, std::string, std::less<>> kObjectProperties = {
    {"MaterialTheme.colorScheme", "::kompoz::colorScheme()"},
    {"MaterialTheme.typography", "::kompoz::typography()"},
    {"MaterialTheme.shapes", "::kompoz::LocalShapes.current()"},
    // Compose's platform locals: what the host gives (platform.hpp)
    {"LocalUriHandler.current", "::kompoz::uriHandler()"}, {"LocalClipboardManager.current", "::kompoz::clipboardManager()"},
    {"LocalHapticFeedback.current", "::kompoz::hapticFeedback()"}, {"LocalSoftwareKeyboardController.current", "::kompoz::softwareKeyboardController()"},
    {"LocalLifecycleOwner.current", "::kompoz::currentLifecycleOwner()"}, {"LocalClipboard.current", "::kompoz::clipboard()"},
    {"LocalAutofillManager.current", "::kompoz::autofillManager()"},
    {"LocalFocusManager.current", "::kompoz::focusManager()"},
    {"MaterialTheme.motionScheme", "::kompoz::motionScheme()"},
    // (Compose's insets are properties: WindowInsets.systemBars)
    {"WindowInsets.statusBars", "::kompoz::WindowInsets::statusBars()"},
    {"WindowInsets.navigationBars", "::kompoz::WindowInsets::navigationBars()"},
    {"WindowInsets.systemBars", "::kompoz::WindowInsets::systemBars()"},
    {"WindowInsets.captionBar", "::kompoz::WindowInsets::captionBar()"},
    {"WindowInsets.mandatorySystemGestures", "::kompoz::WindowInsets::mandatorySystemGestures()"},
    {"WindowInsets.tappableElement", "::kompoz::WindowInsets::tappableElement()"},
    {"WindowInsets.waterfall", "::kompoz::WindowInsets::waterfall()"},
    {"WindowInsets.ime", "::kompoz::WindowInsets::ime()"},
    {"WindowInsets.imeAnimationSource", "::kompoz::WindowInsets::imeAnimationSource()"},
    {"WindowInsets.imeAnimationTarget", "::kompoz::WindowInsets::imeAnimationTarget()"},
    {"WindowInsets.displayCutout", "::kompoz::WindowInsets::displayCutout()"},
    {"WindowInsets.systemGestures", "::kompoz::WindowInsets::systemGestures()"},
    {"WindowInsets.safeGestures", "::kompoz::WindowInsets::safeGestures()"},
    {"WindowInsets.safeDrawing", "::kompoz::WindowInsets::safeDrawing()"},
    {"WindowInsets.safeContent", "::kompoz::WindowInsets::safeContent()"},
    {"Int.MAX_VALUE", "std::numeric_limits<int>::max()"},
    {"Int.MIN_VALUE", "std::numeric_limits<int>::min()"},
    {"Long.MAX_VALUE", "std::numeric_limits<long long>::max()"},
    {"Float.MAX_VALUE", "std::numeric_limits<float>::max()"},
    {"Float.POSITIVE_INFINITY", "std::numeric_limits<float>::infinity()"},
    {"Float.NaN", "std::numeric_limits<float>::quiet_NaN()"},
};

// Compose's @Composable properties (Kompoz's @Kompozable ones) read by a name alone: calls here.
const std::map<std::string, std::string, std::less<>> kComposableProperties = {
    {"currentKompozitionLocalContext", "::kompoz::currentKompozitionLocalContext()"}};

// Named arguments whose lambda gives a value: items(list, key = { it.id }, contentType = { … }).
const std::set<std::string, std::less<>> kValueArguments = {"key", "contentType", "selector", "comparator", "span", "pageCount", "confirmValueChange",
                                                            "items", "keyboardModifiers", "canPan", "isMeasurementApproachInProgress", "approachMeasure",
                                                            "itemCount", "progress", "transitionSpec", "contentKey", "shouldStartDragAndDrop",
                                                            "enterTransition", "exitTransition", "popEnterTransition", "popExitTransition",
                                                            "initialOffsetX", "initialOffsetY", "targetOffsetX", "targetOffsetY",
                                                            "sourceCenter", "magnifierCenter", "amplitude"};

// Properties of a class's own that are functions here, read by their bare name in its body (Kotlin's
// viewModelScope in a ViewModel).
const std::set<std::string, std::less<>> kBareProperties = {"viewModelScope"};

// Kotlin's standard functions, from kotlin.hpp: listOf(1, 2) -> kt::listOf(1, 2).
const std::set<std::string, std::less<>> kKtFunctions = {
    "listOf", "mutableListOf", "emptyList", "arrayListOf", "setOf", "mutableSetOf", "emptySet", "mapOf", "mutableMapOf",
    "emptyMap", "repeat", "println", "maxOf", "minOf", "require", "check", "error", "TODO", "onDispose", "onPauseOrDispose",
    "onStopOrDispose", "floatArrayOf", "intArrayOf", "List", "MutableList", "Random", "Pair", "Triple",
    // kotlin.math's (a Float in, a Float out), and Compose's Rect(left, top, right, bottom) / Rect(offset, size) and
    // CornerSize(percent) / CornerSize(dp): Kotlin's meanings over the C++ structs' fields.
    "sin", "cos", "tan", "asin", "acos", "atan", "atan2", "sinh", "cosh", "tanh", "sqrt", "abs", "min", "max", "floor", "ceil",
    "round", "truncate", "sign", "pow", "hypot", "exp", "ln", "log2", "log10", "Rect", "CornerSize", "PaddingValues", "lazy"};
// Compose's extension functions on a flow that Kompoz has as free functions with a call site of their own:
// vm.items.collectAsStateWithLifecycle() -> ::kompoz::collectAsStateWithLifecycle(vm.items).
const std::set<std::string, std::less<>> kFreeMembers = {"collectAsStateWithLifecycle"};
// Interfaces one implements (an object expression, a class) and hands around: Kompoz holds them behind shared
// pointers, as it does ViewModels; a nullable one is an empty pointer.
const std::set<std::string, std::less<>> kHandleTypes = {"OverscrollEffect", "NestedScrollConnection", "PopupPositionProvider",
                                                         "IndicationNodeFactory", "TextToolbar", "LifecycleOwner"};
// kotlin.math's constants: PI.
const std::map<std::string, std::string, std::less<>> kBareConstants = {{"PI", "::kompoz::kt::PI"}};

// Members of Kotlin's standard types, from kotlin.hpp: text.trim() -> kt::trim(text). Each uses the
// value's own member of that name where it has one.
const std::set<std::string, std::less<>> kKtMembers = {
    "forEach", "forEachIndexed", "map", "mapIndexed", "filter", "filterNot", "isEmpty", "isNotEmpty", "isBlank",
    "isNotBlank", "trim", "trimIndent", "uppercase", "lowercase", "toInt", "toFloat", "toDouble", "toLong", "toString",
    "first", "last", "firstOrNull", "lastOrNull", "joinToString", "startsWith", "endsWith", "contains", "substring",
    "take", "drop", "reversed", "sorted", "sortedBy", "any", "all", "none", "count", "sum", "sumOf", "maxOf", "minOf", "maxOfOrNull", "minOfOrNull", "indexOf",
    "getOrNull", "let", "also", "takeIf", "withIndex", "toList", "toMutableList", "distinct", "split", "replace",
    "coerceIn", "coerceAtLeast", "coerceAtMost", "roundToInt", "toPx", "roundToPx", "toDp", "find", "add", "addAll", "removeAt", "remove", "repeat",
    "format", "indexOfFirst", "indexOfLast", "removeIf", "removeAll", "flatMap", "sortedByDescending", "maxByOrNull", "minByOrNull",
    "mapNotNull", "filterIndexed", "distinctBy", "takeUnless", "groupBy", "associateBy", "associateWith", "chunked", "partition",
    "ifEmpty", "ifBlank", "shuffle", "isDigit", "isLetter", "isLetterOrDigit", "isWhitespace", "isUpperCase", "isLowerCase"};

// Calls whose lambda has a receiver, Kotlin's T.() -> R: the C++ lambda takes it
// first, and the names the body doesn't declare that are the receiver's members are the receiver's
// (Modifier.graphicsLayer { scaleX = 2f }, Canvas(m) { drawCircle(Color.Red) }).
const std::map<std::string, std::string, std::less<>> kReceiverLambdas = {
    {"graphicsLayer", "GraphicsLayerScope"}, {"drawBehind", "DrawScope"},       {"Canvas", "DrawScope"},
    {"drawWithContent", "ContentDrawScope"}, {"drawWithCache", "CacheDrawScope"},
    {"onDrawBehind", "DrawScope"},           {"onDrawWithContent", "ContentDrawScope"},
    {"record", "DrawScope"},  // (record(layer, size = …) { … }: the recording's drawing)
    // DrawScope's own: what they draw is drawn moved, clipped, ... (rotate(45f) { drawRect(…) })
    {"translate", "DrawScope"},              {"inset", "DrawScope"},            {"clipRect", "DrawScope"},
    {"clipPath", "DrawScope"},               {"rotate", "DrawScope"},           {"rotateRad", "DrawScope"},
    {"scale", "DrawScope"},                  {"withTransform", "DrawScope"},
    {"semantics", "SemanticsPropertyReceiver"}, {"clearAndSetSemantics", "SemanticsPropertyReceiver"},
    {"flex", "FlexConfigScope"},             {"BoxWithConstraints", "BoxWithConstraintsScope"},
    // ("const ": a receiver only read)
    {"mediaQuery", "const UiMediaScope"},    {"derivedMediaQuery", "const UiMediaScope"},
    {"SubkompozLayout", "SubkompozMeasureScope"},
    {"ButtonGroup", "ButtonGroupScope"},  // (its items: clickableItem(…); its buttons' Modifier.animateWidth(source))
    {"FloatingActionButtonMenu", "const FloatingActionButtonMenuScope"}, {"ToggleFloatingActionButton", "const ToggleFloatingActionButtonScope"},
    {"buildAnnotatedString", "AnnotatedStringBuilder"}, {"edit", "TextFieldBuffer"}, {"OutputTransformation", "TextFieldBuffer"},
    {"focusProperties", "FocusProperties"},
    {"GenericShape", "Path"},                {"blur", "BlurScope"},
    {"AnimatedVisibility", "const AnimatedVisibilityScope"}, {"AnimatedContent", "const AnimatedContentScope"},
    {"SharedTransitionLayout", "const SharedTransitionScope"},
    {"pointerInput", "PointerInputScope"},   {"awaitEachGesture", "PointerInputScope"},
    {"produce", "auto Channel"},  // (scope.produce<Int> { send(i) }: the channel it sends to)
    {"AppBarRow", "AppBarScope"},            {"AppBarColumn", "AppBarScope"},
    {"DraggableAnchors", "auto DraggableAnchors"},  // (the builder's anchors: DragValue.Start at 0f)
    {"keyframes", "KeyframesConfig"},  // (keyframes { durationMillis = 420; 1.35f at 110 }: the spec's frames)
    {"navOptions", "NavOptionsBuilder"},
    {"MenuBar", "MenuBarScope"},             {"Menu", "MenuScope"},  // (Compose Multiplatform's menu builder)
    {"NavHost", "NavGraphBuilder"},          {"navigation", "NavGraphBuilder"},
    {"navigate", "NavOptionsBuilder"},       {"popUpTo", "PopUpToBuilder"},     {"navDeepLink", "NavDeepLink"},
    // (an ImageVector's builder: group(…) { path(…) { moveTo(…) } })
    {"group", "ImageVector::Builder"},       {"path", "PathBuilder"},
    {"HorizontalMultiBrowseCarousel", "const CarouselItemScope"}, {"HorizontalUncontainedCarousel", "const CarouselItemScope"},
    {"HorizontalCenteredHeroCarousel", "const CarouselItemScope"}};

// Calls whose arguments by name are the fields of one C++ struct they take (Compose's parameters are
// its fields): addPath(pathData = …, fill = …) is addPath(VectorPath{.pathData = …, .fill = …}).
const std::map<std::string, std::string, std::less<>> kStructParameters = {
    {"addPath", "VectorPath"}, {"path", "VectorPath"}, {"Path", "VectorPath"}, {"group", "VectorGroup"},
    {"detectTapGestures", "TapGestureHandlers"}, {"detectDragGestures", "DragGestureHandlers"},
    {"detectDragGesturesAfterLongPress", "DragGestureHandlers"}, {"detectHorizontalDragGestures", "AxisDragGestureHandlers"},
    {"detectVerticalDragGestures", "AxisDragGestureHandlers"}};

// Kotlin's inline functions: their lambda runs before they return, so it reaches the locals around
// it by reference ([&]), and `x += 1` in forEach { } changes the x outside, as in Kotlin.
const std::set<std::string, std::less<>> kInlineLambdas = {
    "forEach", "forEachIndexed", "map", "mapIndexed", "mapNotNull", "filter", "filterNot", "filterIndexed", "any", "all",
    "none", "count", "first", "firstOrNull", "last", "lastOrNull", "find", "sumOf", "maxOf", "minOf", "maxOfOrNull",
    "minOfOrNull", "maxByOrNull", "minByOrNull", "sortedBy", "sortedByDescending", "groupBy", "associateBy", "associateWith", "flatMap", "partition",
    "distinctBy", "indexOfFirst", "indexOfLast", "takeIf", "takeUnless", "let", "also", "run", "apply", "with",
    "repeat", "getOrElse", "buildString", "buildList", "chunked"};

// Named arguments whose lambda has a receiver: FlexBox(config = { gap(8.dp) }).
const std::map<std::string, std::string, std::less<>> kReceiverArguments = {
    {"FlexBox.config", "FlexBoxConfigScope"}, {"Grid.config", "GridConfigurationScope"},
    {"NavigationSuiteScaffold.navigationSuiteItems", "NavigationSuiteScope"},
    {"item.span", "LazyGridItemSpanScope"},   {"items.span", "LazyGridItemSpanScope"},
    {"withTransform.", "DrawTransform"},  // (its positional lambda: withTransform({ translate(…); rotate(…) }) { … })
    // ("auto X": a template's scope, typed auto&&, its members X's: AnimatedContentTransitionScope<S>)
    {"AnimatedContent.transitionSpec", "auto AnimatedContentTransitionScope"},
    {"animateFloat.transitionSpec", "auto TransitionSegment"}, {"animateDp.transitionSpec", "auto TransitionSegment"},
    {"animateColor.transitionSpec", "auto TransitionSegment"},
    {"NavHost.enterTransition", "auto AnimatedContentTransitionScope"}, {"NavHost.exitTransition", "auto AnimatedContentTransitionScope"},
    {"NavHost.popEnterTransition", "auto AnimatedContentTransitionScope"}, {"NavHost.popExitTransition", "auto AnimatedContentTransitionScope"}};

// Calls whose lambda is a coroutine (Kotlin's suspend lambdas): [=]() -> Task { ...; co_return; }.
const std::set<std::string, std::less<>> kSuspendLambdas = {"LaunchedEffect", "launch", "produceState", "pointerInput", "awaitEachGesture", "flow",
                                                            "produce", "consumeEach", "collect", "PredictiveBackHandler"};
// Of those, the calls that suspend themselves until their block is done with every element (co_await on the call too);
// the others start something (a job, a channel, a flow) and give it back.
const std::set<std::string, std::less<>> kAwaitedBlocks = {"consumeEach", "collect"};

// Functions of a PointerInputScope's (Compose's extensions on it), called in pointerInput { } with the
// scope first here: detectTapGestures(onTap = { }) is detectTapGestures(scope, TapGestureHandlers{…}).
const std::set<std::string, std::less<>> kScopeFunctions = {"detectTapGestures", "detectDragGestures", "detectDragGesturesAfterLongPress",
                                                            "detectHorizontalDragGestures", "detectVerticalDragGestures",
                                                            "detectTransformGestures", "awaitEachGesture"};


const std::set<std::string, std::less<>> kCppKeywords = {
    "alignas", "alignof", "and", "asm", "auto", "bitand", "bitor", "bool", "case", "catch", "char", "compl", "concept", "const",
    "consteval", "constexpr", "constinit", "const_cast", "co_await", "co_return", "co_yield", "decltype", "default", "delete",
    "double", "dynamic_cast", "enum", "explicit", "export", "extern", "float", "friend", "goto", "inline", "int", "long",
    "mutable", "namespace", "new", "noexcept", "not", "operator", "or", "private", "protected", "public", "register",
    "reinterpret_cast", "requires", "short", "signed", "sizeof", "static", "static_assert", "static_cast", "struct",
    "switch", "template", "thread_local", "throw", "typedef", "typeid", "typename", "union", "unsigned", "using",
    "virtual", "void", "volatile", "wchar_t", "xor", "site"};

bool capitalized(std::string_view s) { return !s.empty() && std::isupper(static_cast<unsigned char>(s[0])); }

std::string cppName(const std::string& name) { return kCppKeywords.contains(name) ? name + "_" : name; }

std::string qualified(std::string dotted) {
    std::string out;
    for (char c : dotted) {
        if (c == '.') out += "::";
        else out += c;
    }
    return out;
}

// Whether a lambda's body says `it` for itself (not inside a lambda of its own that has `it`).
bool usesIt(const Node* n) {
    if (!n) return false;
    if (n->kind == Node::Name && n->text == "it") return true;
    if (n->kind == Node::Lambda && !n->flag) return false;  // its own `it`
    for (auto& k : n->kids)
        if (usesIt(k.get())) return true;
    if (n->lambda && usesIt(n->lambda.get())) return true;
    for (auto& b : n->branches) {
        for (auto& c : b.conds)
            if (usesIt(c.second.get())) return true;
        if (usesIt(b.body.get())) return true;
    }
    return false;
}

class Generator {
public:
    Generator(Output& out, const Module& module) : out_(out), module_(module) {}

    std::string type(const Type& t) {
        std::string s;
        if (t.function) {
            // RowScope.() -> Unit: what the receiver gives (weight, items, ...) Kompoz has on its own.
            if (t.composable && t.params.empty() && t.result && t.result->name == "Unit") return "::kompoz::Content";
            s = "std::function<" + (t.result ? type(*t.result) : std::string("void")) + "(";
            for (size_t i = 0; i < t.params.size(); ++i) s += (i ? ", " : "") + type(t.params[i]);
            return s + ")>";  // (a null one is an empty std::function: no optional around it)
        }
        auto known = kTypes.find(t.name);
        s = known != kTypes.end() ? known->second : qualified(t.name);
        // A ViewModel (a class deriving from it, here in Kotlin or in a header): its owner hands it out
        // as a shared pointer (viewModel<T>()), and a parameter or property of the type is one too.
        if (known == kTypes.end() && t.args.empty() && !plainTypes_ && (isViewModel(t.name) || kHandleTypes.contains(t.name))) {
            s = "std::shared_ptr<" + s + ">";
            if (t.nullable) return s;  // (a nullable handle: an empty pointer is null)
        }
        if (!t.args.empty()) {
            size_t count = t.name == "Animatable" ? 1 : t.args.size();  // Animatable<Float, AnimationVector1D>: the vector is Compose's own
            s += "<";
            for (size_t i = 0; i < count; ++i) s += (i ? ", " : "") + type(t.args[i]);
            s += ">";
        }
        if (t.nullable) s = "std::optional<" + s + ">";
        return s;
    }

    // The type as named: a base class, a call's type argument (viewModel<T>()), not what a value of it is.
    std::string plainType(const Type& t) {
        bool was = std::exchange(plainTypes_, true);
        std::string s = type(t);
        plainTypes_ = was;
        return s;
    }
    bool plainTypes_ = false;
    bool isViewModel(std::string_view name, int depth = 0) const {
        if (name == "ViewModel") return true;
        if (depth > 16) return false;
        if (auto k = module_.classDecls.find(name); k != module_.classDecls.end())
            return k->second->base && isViewModel(k->second->base->name, depth + 1);
        return module_.structs && module_.structs->derivesFrom(name, "ViewModel");
    }

    // How a parameter is passed: small values by value, the rest by const reference.
    std::string paramType(const Type& t) {
        std::string s = type(t);
        static const std::set<std::string, std::less<>> byValue = {"int", "long long", "short", "signed char", "float",
                                                                     "double", "bool", "char32_t", "unsigned", "unsigned long long"};
        return byValue.contains(s) ? s : "const " + s + "&";
    }

    void signature(const Function& f, bool defaults) {
        bool unit = !f.result || f.result->name == "Unit";
        out_.at(f.pos);
        if (!f.composable) {
            // A plain function: defined in each file that uses it (inline), its type the body's when
            // it is an expression and none is written; an extension's receiver comes first.
            // A suspend fun is a coroutine: Task, or TaskOf<T> (its expression's when no type is written), its
            // parameters by value (its frame outlives the call).
            std::string result = f.result ? type(*f.result) : f.expressionBody ? "auto" : "void";
            if (f.isSuspend && (f.result || !f.expressionBody)) result = result == "void" ? "::kompoz::Task" : "::kompoz::TaskOf<" + result + ">";
            out_.text(std::string(f.isSuspend ? "[[nodiscard]] " : "") + (f.isPrivate ? "static " : "inline ") + result + " " + cppName(f.name) + "(");
            if (f.receiver)
                out_.text((f.isSuspend ? type(*f.receiver) : "const " + type(*f.receiver) + "&") + " kompoz_receiver_1" + (f.params.empty() ? "" : ", "));
        } else {
            out_.text((unit ? std::string("void") : type(*f.result)) + " " + f.name + "(");
        }
        // Defaults: those at the end (C++ takes no others), where kompozc's calls leave them out.
        size_t firstDefault = f.params.size();
        while (firstDefault > 0 && f.params[firstDefault - 1].defaultValue) --firstDefault;
        for (size_t i = 0; i < f.params.size(); ++i) {
            const Param& p = f.params[i];
            out_.text((i ? ", " : "") + paramTypeOf(p, f.isSuspend) + " " + cppName(p.name));
            if (defaults && i >= firstDefault) {
                out_.text(" = ");
                scopes_.emplace_back();
                expr(*p.defaultValue);
                scopes_.pop_back();
            }
        }
        if (f.composable)  // (a value-returning one too: a group of its own, as Compose gives every call)
            out_.text(std::string(f.params.empty() ? "" : ", ") + "::kompoz::CallSite site" + (defaults ? " = ::kompoz::CallSite::current()" : ""));
        out_.text(")");
        if (f.isSuspend && !f.result && f.expressionBody) {
            out_.text(" -> ::kompoz::kt::TaskFor<decltype(");
            typing(*f.body->kids[0]->kids[0], true);
            out_.text(")>");
        }
    }

    // ---- Interfaces, accessors, suspend functions ----

    const InterfaceDecl* interfaceNamed(const std::string& name) const {
        auto i = module_.interfaces.find(name);
        return i == module_.interfaces.end() ? nullptr : i->second;
    }
    // The interfaces among `types` (the rest left: the sealed class a data class is a case of) and those they
    // extend, each once, nearest first.
    std::vector<const InterfaceDecl*> allInterfaces(const std::vector<Type>& types) const {
        std::vector<const InterfaceDecl*> out;
        std::vector<const Type*> todo;
        for (const Type& t : types) todo.push_back(&t);
        for (size_t i = 0; i < todo.size() && i < 256; ++i)
            if (const InterfaceDecl* d = interfaceNamed(todo[i]->name); d && std::find(out.begin(), out.end(), d) == out.end()) {
                out.push_back(d);
                for (const Type& b : d->bases) todo.push_back(&b);
            }
        return out;
    }
    // A member of interfaces: the property or function of the name, the interface that declares it first
    // (nearest first), and whether one of them gives it a body (a getter, a function's).
    struct InterfaceMember {
        const InterfaceDecl* in = nullptr;
        const ClassProperty* property = nullptr;
        const Function* method = nullptr;
        bool hasBody = false;
    };
    using InterfaceMembers = std::vector<std::pair<std::string, InterfaceMember>>;
    InterfaceMembers interfaceMembers(const std::vector<Type>& types) const {
        InterfaceMembers out;
        auto find = [&](const std::string& n) -> InterfaceMember* {
            for (auto& [k, v] : out)
                if (k == n) return &v;
            return nullptr;
        };
        for (const InterfaceDecl* d : allInterfaces(types)) {
            for (const ClassProperty& p : d->properties) {
                InterfaceMember* m = find(p.name);
                if (!m) m = &out.emplace_back(p.name, InterfaceMember{d, &p, nullptr, false}).second;
                m->hasBody = m->hasBody || p.getter;
            }
            for (const Function& f : d->methods) {
                InterfaceMember* m = find(f.name);
                if (!m) m = &out.emplace_back(f.name, InterfaceMember{d, nullptr, &f, false}).second;
                m->hasBody = m->hasBody || !f.isAbstract;
            }
        }
        return out;
    }
    static const InterfaceMember* memberNamed(const InterfaceMembers& members, const std::string& name) {
        for (auto& [n, m] : members)
            if (n == name) return &m;
        return nullptr;
    }
    std::string objectOf(const InterfaceDecl& d) const { return qualified(d.name) + "::Object"; }
    // A parameter's type: by value in a coroutine (its frame outlives the call), else as paramType says.
    std::string paramTypeOf(const Param& p, bool byValue) { return byValue ? type(p.type) : paramType(p.type); }
    // The type an interface's member gives, for an override's declaration: decltype of the call on the
    // interface's object (`object`: that type as it is spelled where the override is).
    std::string memberType(const std::string& object, const InterfaceMember& m) {
        std::string call = "std::declval<const " + object + "&>()." + cppName(m.property ? m.property->name : m.method->name) + "(";
        if (m.method)
            for (size_t i = 0; i < m.method->params.size(); ++i)
                call += (i ? ", " : "") + std::string("std::declval<") + paramTypeOf(m.method->params[i], m.method->isSuspend) + ">()";
        return "decltype(" + call + "))";
    }

    // A function's head, `prefix` before it (" virtual ", " static "): R name(params) [const] [-> T]`suffix`, its
    // parameters declared by the caller. A suspend fun's result is its coroutine (Task, TaskOf<T>), its parameters
    // by value; `inherited`: what an override gives (an interface's function's); a result written nowhere is
    // deduced (auto, `deduce`) or, where it can't be (a virtual one, a coroutine), its expression's (decltype).
    void functionHead(const Function& m, const std::string& prefix, bool isConst, const std::string& suffix, const std::string& inherited,
                      bool defaults, bool deduce) {
        bool unitResult = m.result ? m.result->name == "Unit" : !m.expressionBody;
        std::string result = "auto";
        int trailing = 0;  // 1: inherited, 2: the expression's
        if (m.result || !m.expressionBody) {
            std::string t = unitResult ? "void" : type(*m.result);
            result = !m.isSuspend ? t : unitResult ? "::kompoz::Task" : "::kompoz::TaskOf<" + t + ">";
        } else if (!inherited.empty()) {
            trailing = 1;
        } else if (!deduce || m.isSuspend) {
            trailing = 2;
        }
        out_.text((m.isSuspend ? " [[nodiscard]]" : "") + prefix + result + " " + cppName(m.name) + "(");
        for (size_t i = 0; i < m.params.size(); ++i) {
            const Param& p = m.params[i];
            out_.text((i ? ", " : "") + paramTypeOf(p, m.isSuspend) + " " + cppName(p.name));
            if (defaults && p.defaultValue) {
                out_.text(" = ");
                expr(*p.defaultValue, &p.type);
            }
        }
        out_.text(std::string(")") + (isConst ? " const" : ""));
        if (trailing == 1) out_.text(" -> " + inherited);
        if (trailing == 2) {
            out_.text(std::string(" -> ") + (m.isSuspend ? "::kompoz::kt::TaskFor<decltype(" : "std::remove_cvref_t<decltype("));
            typing(*m.body->kids[0]->kids[0], m.isSuspend);  // (an expression body: a Return)
            out_.text(")>");
        }
        out_.text(suffix);
    }
    // An expression written for its type (decltype): in a suspend fun's, each call as what it gives once awaited.
    // In an interface's object, its own members are reached through a declval of it (not `this`, which a
    // declaration's type doesn't have).
    void typing(const Node& e, bool awaited) {
        bool wasSuspend = std::exchange(suspend_, false), wasTyping = std::exchange(awaitedTypes_, awaited);
        std::string wasSelf = std::exchange(selfPrefix_, interfaceSelf_.empty() ? std::string() : "std::declval<const " + interfaceSelf_ + "&>().");
        expr(e);
        suspend_ = wasSuspend, awaitedTypes_ = wasTyping, selfPrefix_ = wasSelf;
    }
    // A function's body, a coroutine's for a suspend fun (a Unit one ends with co_return).
    void functionBody(const Function& m) {
        const Type* wasResult = std::exchange(currentResult_, m.result ? &*m.result : nullptr);
        bool wasSuspend = std::exchange(suspend_, m.isSuspend);
        out_.text(" {");
        statements(*m.body, false);
        if (m.isSuspend && !m.expressionBody && (!m.result || m.result->name == "Unit")) out_.text(" co_return;");
        out_.text(" }");
        suspend_ = wasSuspend;
        currentResult_ = wasResult;
    }

    // Whether a property's accessors read `field` (the value it keeps).
    static bool usesField(const ClassProperty& p) {
        std::set<std::string, std::less<>> used;
        usedNames(p.getter.get(), used);
        usedNames(p.setter.get(), used);
        return used.contains("field");
    }
    // Whether a property read through accessors keeps a value (kompoz_value_<name>): one given a value, one
    // without a getter (the default one reads it), one whose accessors read `field`. (A delegated one keeps
    // its delegate, kompoz_by_<name>.)
    static bool stores(const ClassProperty& p) { return !p.delegated && (p.init || !p.getter || usesField(p)); }
    static std::string storageOf(const ClassProperty& p) { return (p.delegated ? "kompoz_by_" : "kompoz_value_") + p.name; }

    // The members of a class, an object or an interface read through functions, known by name in its body:
    // the delegated ones (their delegate), those with accessors (an object's: get, set, private set; a class's:
    // an interface's too), and the interfaces' properties it doesn't declare (`own`: its fields of other names).
    void declareMembers(const std::vector<ClassProperty>& properties, const std::vector<Type>& interfaces, bool isObject,
                        const std::vector<std::string>& accessedParams = {}, const std::set<std::string, std::less<>>& own = {}) {
        for (const std::string& n : accessedParams) declare(n, Accessor);
        for (const ClassProperty& p : properties)
            if (p.delegated) declare(p.name, MemberDelegated);
            else if (isObject ? p.getter || p.setter || p.privateSetter : p.accessed()) declare(p.name, Accessor);
        for (auto& [name, m] : interfaceMembers(interfaces)) {
            bool declared = own.contains(name) || scopes_.back().contains(name) ||
                            std::any_of(properties.begin(), properties.end(), [&](const ClassProperty& p) { return p.name == name; });
            if (m.property && !declared && (!isObject || m.hasBody)) declare(name, Accessor);
        }
    }

    // A property's functions: its getter, name(), and a var's setter, setName(v) (private with `private set`);
    // `isStatic`: an object's. An override of an interface's property gives what it does; the getter reads
    // the delegate, the value kept, or runs get() (its `field` the value kept), the setter likewise.
    template <class Access>
    void accessorFunctions(const ClassProperty& p, const InterfaceMember* base, bool isStatic, Access& access) {
        if (p.overrides && (!base || !base->property)) throw CompileError(p.pos, p.name + " overrides no property of the interfaces");
        std::string storage = storageOf(p);
        std::string q = isStatic ? " static " : " ", c = isStatic ? "" : " const";
        bool isOverride = !isStatic && p.overrides;
        const Type* baseType = base && base->property && base->property->type ? &*base->property->type : nullptr;
        std::string t = p.type ? type(*p.type) : baseType ? type(*baseType) : "";
        access(!p.isPrivate);
        out_.at(p.pos);
        if (!t.empty()) {
            out_.text(q + t + " " + cppName(p.name) + "()" + c);
        } else if (isOverride) {
            out_.text(q + "auto " + cppName(p.name) + "()" + c + " -> " + memberType(objectOf(*base->in), *base));
        } else if (p.delegated) {
            out_.text(q + "decltype(auto) " + cppName(p.name) + "()" + c);
        } else if (!p.getter) {
            out_.text(q + "std::remove_cvref_t<decltype(" + storage + ")> " + cppName(p.name) + "()" + c);
        } else {
            out_.text(q + "auto " + cppName(p.name) + "()" + c);
        }
        out_.text(std::string(isOverride ? " override" : "") + " {");
        if (p.delegated) {
            out_.text(" return ::kompoz::kt::delegated(" + storage + ");");
        } else if (p.getter) {
            std::string was = std::exchange(fieldOf_, storage);
            scopes_.emplace_back();
            declare("field", BackingField);
            statements(*p.getter, false);
            scopes_.pop_back();
            fieldOf_ = was;
        } else {
            out_.text(" return " + storage + ";");
        }
        out_.text(" }");
        if (!p.isVar) return;
        access(!(p.isPrivate || p.privateSetter));
        std::string pt = p.type ? paramType(*p.type) : baseType ? paramType(*baseType)
                         : p.delegated ? "const std::remove_cvref_t<decltype(::kompoz::kt::delegated(" + storage + "))>&"
                                       : "const std::remove_cvref_t<decltype(" + storage + ")>&";
        std::string v = p.setter ? cppName(p.setterParam) : "kompoz_v";
        out_.text(q + "void " + setterOf(p.name) + "(" + pt + " " + v + ")" + c + (isOverride && base->property->isVar ? " override" : "") + " {");
        if (p.delegated) {
            out_.text(" " + storage + ".set(" + v + ");");
        } else if (p.setter) {
            std::string was = std::exchange(fieldOf_, storage);
            scopes_.emplace_back();
            declare(p.setterParam, Local);
            declare("field", BackingField);
            statements(*p.setter, false);
            scopes_.pop_back();
            fieldOf_ = was;
        } else {
            out_.text(" " + storage + " = " + v + ";");
        }
        out_.text(" }");
    }

    // What a data class's value or an object is where one of its interfaces is wanted: a class template
    // (kompoz_Object, made only where it is converted, after the interfaces' definitions: they are only
    // declared before it), its abstract members the value's or the object's; kompoz_shared() makes one (an
    // object's: its one), kompoz_from(o) gives the value back (`x as Item`).
    void interfaceAdapter(const std::string& name, const std::vector<Type>& supers, bool isObject, const std::function<bool(const std::string&)>& has,
                          Pos pos) {
        std::vector<const InterfaceDecl*> direct;
        for (const Type& t : supers)
            if (const InterfaceDecl* d = interfaceNamed(t.name)) direct.push_back(d);
        if (direct.empty()) return;
        auto later = [](const std::string& t) { return "::kompoz::kt::Later<kompoz_D, " + t + ">"; };
        std::string bases;
        for (const InterfaceDecl* d : direct) bases += std::string(bases.empty() ? " " : ", ") + "public virtual " + later(qualified(d->name)) + "::Object";
        std::string value = isObject ? later(name) + "{}" : "value";
        out_.text(" template <class kompoz_D = void> struct kompoz_Object :" + bases + " {");
        if (!isObject) out_.text(" mutable " + later(name) + " value; explicit kompoz_Object(const " + later(name) + "& v) : value(v) {}");
        for (auto& [n, m] : interfaceMembers(supers)) {
            if (!has(n)) {
                if (m.hasBody) continue;  // (the interface's own)
                throw CompileError(pos, name + " doesn't implement " + m.in->name + "." + n);
            }
            std::string object = "typename " + later(qualified(m.in->name)) + "::Object";
            if (m.property) {
                out_.text(" auto " + cppName(n) + "() const -> " + memberType(object, m) + " override { return KOMPOZ_KT_GET((" + value + "), " +
                          cppName(n) + "); }");
                if (m.property->isVar) {
                    if (!m.property->type) throw CompileError(m.property->pos, n + " needs a type");
                    out_.text(" void " + setterOf(n) + "(" + paramType(*m.property->type) + " kompoz_v) const override { KOMPOZ_KT_SET((" + value + "), " +
                              cppName(n) + ", " + setterOf(n) + ", (kompoz_v)); }");
                }
            } else {
                if (!isObject) throw CompileError(pos, name + ": a data class has no functions here, so it can't implement " + m.in->name + "." + n);
                out_.text(" auto " + cppName(n) + "(");
                std::string args;
                for (size_t i = 0; i < m.method->params.size(); ++i) {
                    const Param& p = m.method->params[i];
                    out_.text((i ? ", " : "") + paramTypeOf(p, m.method->isSuspend) + " " + cppName(p.name));
                    args += (i ? ", " : "") + cppName(p.name);
                }
                out_.text(") const -> " + memberType(object, m) + " override { return " + later(name) + "::" + cppName(n) + "(" + args + "); }");
            }
        }
        out_.text(" std::string toString() const override { return ::kompoz::kt::str(" + value + "); }");
        if (!isObject)
            out_.text(" bool kompoz_equals(const ::kompoz::kt::InterfaceObject& o) const override { auto* other = dynamic_cast<const kompoz_Object*>(&o); "
                      "return other && other->value == value; }");
        out_.text(" }; using Object = kompoz_Object<>;");
        if (isObject) {
            out_.text(" template <class kompoz_D = void> static std::shared_ptr<kompoz_Object<kompoz_D>> kompoz_shared() { static const auto object = "
                      "std::make_shared<kompoz_Object<kompoz_D>>(); return object; }");
            out_.text(" template <class kompoz_O> static " + name + " kompoz_from(const std::shared_ptr<kompoz_O>&) { return {}; }");
        } else {
            out_.text(" template <class kompoz_D = void> std::shared_ptr<kompoz_Object<kompoz_D>> kompoz_shared() const { return "
                      "std::make_shared<kompoz_Object<kompoz_D>>(*this); }");
            out_.text(" template <class kompoz_O> static " + name + " kompoz_from(const std::shared_ptr<kompoz_O>& o) { return o->value; }");
        }
        // The interfaces' members with bodies that it doesn't declare, reached as its own (Item("a").shown(), Name.shown()).
        std::string q = isObject ? " static " : " ", c = isObject ? "" : " const";
        for (auto& [n, m] : interfaceMembers(supers)) {
            if (!m.hasBody || has(n)) continue;
            if (m.method)
                out_.text(" template <class... kompoz_A>" + q + "decltype(auto) " + cppName(n) + "(kompoz_A&&... a)" + c + " { return kompoz_shared<::kompoz::kt::Later<std::tuple<kompoz_A...>, void>>()->" +
                          cppName(n) + "(std::forward<kompoz_A>(a)...); }");
            else
                out_.text(" template <class kompoz_D = void>" + q + "decltype(auto) " + cppName(n) + "()" + c + " { return kompoz_shared<kompoz_D>()->" + cppName(n) +
                          "(); }");
        }
    }

    // interface Name : A { val a: T; fun f(): R; fun g() = … }: a handle (as a class's: copies are the same object,
    // == too, a data class's equal values) to an object deriving from Name::Object, an abstract class: its
    // properties getters (a var's a setter too), abstract or the interface's own get(), its functions virtual,
    // abstract or with their bodies; deriving from InterfaceObject and the interfaces' it extends, virtually.
    // Made from what implements it: a class's handle, a ViewModel's pointer, a data class or an object (their
    // kompoz_shared()); a fun interface's from a lambda too (kompoz_lambda). C++ calls its functions on it.
    void interfaceDefinition(const InterfaceDecl& i) {
        std::string bases = "public virtual ::kompoz::kt::InterfaceObject";
        for (const Type& b : i.bases) {
            const InterfaceDecl* d = interfaceNamed(b.name);
            if (!d) throw CompileError(b.pos, b.name + " isn't an interface written in Kotlin here");
            bases += ", public virtual " + objectOf(*d);
        }
        InterfaceMembers inherited = interfaceMembers(i.bases);
        out_.at(i.pos);
        out_.text("class " + i.name + " { public: struct Object : " + bases + " {");
        bool wasInClass = std::exchange(inClass_, true);
        interfaceSelf_ = "Object";
        for (const Function& m : i.methods) interfaceMethods_.insert(m.name);
        for (auto& [n, m] : inherited)
            if (m.method) interfaceMethods_.insert(n);
        scopes_.emplace_back();
        for (const ClassProperty& p : i.properties) declare(p.name, Accessor);
        for (auto& [n, m] : inherited)
            if (m.property && !scopes_.back().contains(n)) declare(n, Accessor);
        for (size_t index : i.order) {
            if (index % 2 == 0) {
                const ClassProperty& p = i.properties[index / 2];
                const InterfaceMember* base = memberNamed(inherited, p.name);
                if (p.overrides && (!base || !base->property)) throw CompileError(p.pos, p.name + " overrides no property of " + i.name + "'s interfaces");
                const Type* typed = p.type ? &*p.type : base && base->property->type ? &*base->property->type : nullptr;
                out_.at(p.pos);
                if (!p.getter) {
                    if (!typed) throw CompileError(p.pos, p.name + " needs a type");
                    out_.text(" virtual " + type(*typed) + " " + cppName(p.name) + "() const = 0;");
                } else {
                    out_.text(" virtual " + (typed ? type(*typed) : std::string("auto")) + " " + cppName(p.name) + "() const");
                    if (!typed) {
                        if (!(p.getter->kids.size() == 1 && p.getter->kids[0]->kind == Node::Return && !p.getter->kids[0]->kids.empty()))
                            throw CompileError(p.pos, p.name + " needs a type");
                        out_.text(" -> std::remove_cvref_t<decltype(");
                        typing(*p.getter->kids[0]->kids[0], false);
                        out_.text(")>");
                    }
                    out_.text(std::string(p.overrides ? " override" : "") + " {");
                    statements(*p.getter, false);
                    out_.text(" }");
                }
                if (p.isVar) {
                    if (!typed) throw CompileError(p.pos, p.name + " needs a type");
                    out_.text(" virtual void " + setterOf(p.name) + "(" + paramType(*typed) + ") const = 0;");
                }
            } else {
                const Function& m = i.methods[index / 2];
                const InterfaceMember* base = memberNamed(inherited, m.name);
                if (base && !base->method) base = nullptr;
                if (m.overrides && !base) throw CompileError(m.pos, m.name + " overrides no function of " + i.name + "'s interfaces");
                out_.at(m.pos);
                scopes_.emplace_back();
                for (const Param& p : m.params) declare(p.name, Local);
                functionHead(m, " virtual ", true, m.isAbstract ? " = 0" : base ? " override" : "", base ? memberType(objectOf(*base->in), *base) : "", true,
                             false);
                if (m.isAbstract) out_.text(";");
                else functionBody(m);
                scopes_.pop_back();
            }
        }
        scopes_.pop_back();
        inClass_ = wasInClass;
        interfaceSelf_.clear();
        interfaceMethods_.clear();
        const std::string& n = i.name;
        out_.text(" }; " + n + "() = default;");
        out_.text(" template <class T> requires requires(const T& t) { { t.kompoz_shared() } -> std::convertible_to<std::shared_ptr<Object>>; } " + n +
                  "(const T& t) : kompoz_object_(t.kompoz_shared()) {}");
        out_.text(" template <class T> requires std::convertible_to<std::shared_ptr<T>, std::shared_ptr<Object>> " + n +
                  "(std::shared_ptr<T> p) : kompoz_object_(std::move(p)) {}");
        out_.text(" " + n + "(::kompoz::kt::FromObject, std::shared_ptr<Object> o) : kompoz_object_(std::move(o)) {}");
        out_.text(" static " + n + " kompoz_from(std::shared_ptr<Object> o) { return " + n + "(::kompoz::kt::FromObject{}, std::move(o)); }");
        out_.text(" Object& kompoz_object() const { return *kompoz_object_; }");
        out_.text(" const std::shared_ptr<Object>& kompoz_shared() const { return kompoz_object_; }");
        out_.text(" bool operator==(const " + n + "& o) const { return ::kompoz::kt::sameObject(kompoz_object_.get(), o.kompoz_object_.get()); }");
        out_.text(" std::string toString() const { return kompoz_object_ ? kompoz_object_->toString() : std::string(\"null\"); }");
        // C++ calls its functions on the handle (Kotlin's code reaches the object: kt::deref).
        for (const Function& m : i.methods) {
            if (m.name == "toString" || m.name == "equals" || m.name == "hashCode") continue;
            out_.text(" decltype(auto) " + cppName(m.name) + "(");
            std::string args;
            for (size_t k = 0; k < m.params.size(); ++k) {
                out_.text((k ? ", " : "") + paramTypeOf(m.params[k], m.isSuspend) + " " + cppName(m.params[k].name));
                args += (k ? ", " : "") + cppName(m.params[k].name);
            }
            out_.text(") const { return kompoz_object_->" + cppName(m.name) + "(" + args + "); }");
        }
        if (i.isFun) {  // Name { … }: an object whose one abstract function runs the lambda
            const Function* m = nullptr;
            for (const Function& f : i.methods)
                if (f.isAbstract) m = &f;
            if (m->isSuspend) throw CompileError(m->pos, "a fun interface's suspend fun isn't supported");
            std::string params, args;
            for (size_t k = 0; k < m->params.size(); ++k) {
                params += (k ? ", " : "") + paramType(m->params[k].type) + " " + cppName(m->params[k].name);
                args += (k ? ", " : "") + cppName(m->params[k].name);
            }
            InterfaceMember im{&i, nullptr, m, false};
            out_.text(" template <class F> static " + n + " kompoz_lambda(F f) { struct Lambda : Object { F f; explicit Lambda(F g) : f(std::move(g)) {} auto " +
                      cppName(m->name) + "(" + params + ") const -> " + memberType("Object", im) + " override { return f(" + args + "); } }; return " + n +
                      "(::kompoz::kt::FromObject{}, std::make_shared<Lambda>(std::move(f))); }");
        }
        out_.text(" private: std::shared_ptr<Object> kompoz_object_; };\n");
    }

    void classDefinition(const DataClass& k) {
        out_.at(k.pos);
        out_.text("struct " + k.name + " {");
        scopes_.emplace_back();
        for (const Param& f : k.fields) {
            out_.at(f.pos);
            out_.text(" " + type(f.type) + " " + cppName(f.name));
            if (f.defaultValue) {
                out_.text(" = ");
                expr(*f.defaultValue, &f.type);
            } else {
                out_.text("{}");
            }
            out_.text(";");
        }
        scopes_.pop_back();
        if (std::find(k.annotations.begin(), k.annotations.end(), "Serializable") != k.annotations.end()) {  // a route (nav_host.hpp)
            out_.text(" KOMPOZ_ROUTE(" + k.name);
            for (const Param& f : k.fields) out_.text(", " + cppName(f.name));
            out_.text(")");
        }
        out_.text(" bool operator==(const " + k.name + "&) const = default;");
        // (a template: built only where it is used, so a field without a text form doesn't stop the rest)
        out_.text(" template <class = void> std::string toString() const { return ::kompoz::kt::str(\"" + k.name + "(\"");
        for (size_t i = 0; i < k.fields.size(); ++i)
            out_.text(", \"" + std::string(i ? ", " : "") + k.fields[i].name + "=\", " + cppName(k.fields[i].name));
        out_.text(", \")\"); }");
        interfaceAdapter(k.name, k.supers, false, [&](const std::string& n) {
            return std::any_of(k.fields.begin(), k.fields.end(), [&](const Param& f) { return f.name == n; });
        }, k.pos);
        out_.text(" };\n");
    }

    // Name(a = 1, b = "x") for a class: its constructor's parameters in their order (defaults left to it).
    void constructClass(const Node& c, const ClassDecl& k) {
        if (c.lambda) throw CompileError(c.lambda->pos, k.name + " takes no lambda");
        std::vector<const Node*> given(k.ctorParams.size(), nullptr);
        size_t next = 0;
        for (size_t i = 0; i + 1 < c.kids.size(); ++i) {
            const std::string& n = c.names[i];
            size_t at = next;
            if (!n.empty()) {
                at = k.ctorParams.size();
                for (size_t f = 0; f < k.ctorParams.size(); ++f)
                    if (k.ctorParams[f].name == n) at = f;
                if (at == k.ctorParams.size()) throw CompileError(c.kids[i + 1]->pos, k.name + " has no parameter named " + n);
            } else {
                next = at + 1;
            }
            if (at >= k.ctorParams.size()) throw CompileError(c.kids[i + 1]->pos, "too many arguments for " + k.name);
            if (given[at]) throw CompileError(c.kids[i + 1]->pos, k.ctorParams[at].name + " is given twice");
            given[at] = c.kids[i + 1].get();
        }
        size_t last = k.ctorParams.size();
        while (last > 0 && !given[last - 1]) --last;  // (trailing ones left out take their defaults)
        out_.text(qualifierOf(k.name) + k.name + "(");
        for (size_t f = 0; f < last; ++f) {
            if (f) out_.text(", ");
            const Param& p = k.ctorParams[f];
            const Node* arg = given[f] ? given[f] : p.defaultValue.get();
            if (!arg) throw CompileError(c.pos, k.name + " needs " + p.name);
            if (arg->kind == Node::Lambda) lambda(*arg, p.type.function && p.type.result && p.type.result->name != "Unit");
            else expr(*arg, &p.type);
        }
        out_.text(")");
    }

    // class Name(val a: T, b: U) : Base(b), A { val c = …; fun f() { … } }: a C++ class. One with a base is that
    // class itself (the base's owner hands it out: viewModel<T>()); one without is a handle to its object
    // (Name::Object, shared by its copies), as a Kotlin object is reached through references: a copy is
    // the same object (== too), a lambda capturing it keeps it, a `const Name&` parameter changes it.
    // Either way the constructor's val/var parameters and the properties are members (typed as written,
    // else as their value is; initialized in the constructor in their order), init blocks the
    // constructor's body, the functions inline, const, their members mutable (Kotlin's references never
    // make an object const), an override of a C++ base's as it is there. Its interfaces' objects are bases
    // (virtual); a property by a delegate, with get() or set(), or an interface's, is read through functions
    // (name(), setName(v)) keeping its delegate or value aside. Kotlin code reaches a handle's object
    // through kt::deref (kompoz_object()); C++ calls the handle's functions, which forward.
    void classDeclDefinition(const ClassDecl& k) {
        bool handle = !k.base;
        std::string bases;  // its interfaces' objects
        for (const Type& t : k.interfaces) {
            const InterfaceDecl* d = interfaceNamed(t.name);
            if (!d) throw CompileError(t.pos, t.name + " isn't an interface written in Kotlin here; a base class is constructed: " + t.name + "()");
            bases += std::string(bases.empty() ? "" : ", ") + "public virtual " + objectOf(*d);
        }
        InterfaceMembers inherited = interfaceMembers(k.interfaces);
        out_.at(k.pos);
        out_.text("class " + k.name + (k.base ? " : public " + plainType(*k.base) + (bases.empty() ? std::string() : ", " + bases) : std::string()) +
                  " {");
        bool wasInClass = std::exchange(inClass_, true);
        // Its members read through functions, by their names in all of it.
        scopes_.emplace_back();
        {
            std::vector<std::string> accessedParams;
            std::set<std::string, std::less<>> fields;
            for (size_t i = 0; i < k.ctorParams.size(); ++i)
                if (k.ctorKinds[i] != ' ') {
                    if (k.ctorOverride[i]) accessedParams.push_back(k.ctorParams[i].name);
                    else fields.insert(k.ctorParams[i].name);
                }
            declareMembers(k.properties, k.interfaces, false, accessedParams, fields);
        }
        scopes_.emplace_back();
        // Plain constructor parameters: only the constructor and the initializers see them.
        std::set<std::string, std::less<>> plainParams;
        for (size_t i = 0; i < k.ctorParams.size(); ++i)
            if (k.ctorKinds[i] == ' ') plainParams.insert(k.ctorParams[i].name);
        bool isPublic = true;
        auto access = [&](bool wantPublic) {
            if (wantPublic != isPublic) out_.text(wantPublic ? " public:" : " private:");
            isPublic = wantPublic;
        };
        out_.text(" public:");
        if (handle) out_.text(" struct Object" + (bases.empty() ? std::string() : " : " + bases) + " {");
        // The members: the constructor's val/var parameters, then the properties (one read through functions
        // keeps its value or delegate aside, private: kompoz_value_<name>, kompoz_by_<name>).
        auto paramStorage = [&](size_t i) { return k.ctorOverride[i] ? "kompoz_value_" + k.ctorParams[i].name : cppName(k.ctorParams[i].name); };
        for (size_t i = 0; i < k.ctorParams.size(); ++i) {
            if (k.ctorKinds[i] == ' ') continue;
            access(!k.ctorPrivate[i] && !k.ctorOverride[i]);
            out_.at(k.ctorParams[i].pos);
            out_.text(" mutable " + type(k.ctorParams[i].type) + " " + paramStorage(i) + ";");
        }
        auto propertyStorage = [&](const ClassProperty& p) { return p.accessed() ? storageOf(p) : cppName(p.name); };
        for (const ClassProperty& p : k.properties) {
            if (p.accessed() && !p.delegated && !stores(p)) continue;  // (a getter's alone)
            access(!p.accessed() && !p.isPrivate);
            out_.at(p.pos);
            const InterfaceMember* base = p.overrides ? memberNamed(inherited, p.name) : nullptr;
            const Type* baseType = base && base->property && base->property->type ? &*base->property->type : nullptr;
            if (p.type && !p.delegated) {
                out_.text(" mutable " + type(*p.type) + " " + propertyStorage(p) + ";");
            } else if (baseType && !p.delegated) {
                out_.text(" mutable " + type(*baseType) + " " + propertyStorage(p) + ";");
            } else {
                if (!p.init) throw CompileError(p.pos, "give " + p.name + " a type");
                // Its value's type (the value is written again, unevaluated): one that reads a plain
                // constructor parameter can't be, the parameter isn't here yet.
                std::set<std::string, std::less<>> used;
                usedNames(p.init.get(), used);
                for (const std::string& n : used)
                    if (plainParams.contains(n))
                        throw CompileError(p.pos, "give " + p.name + " a type: its value reads the constructor's parameter " + n);
                // (a delegate's lambda, which C++ can't write here, stands for what it gives: derivedStateOf { count * 2 })
                out_.text(" mutable std::remove_cvref_t<decltype(");
                bool wasTyping = std::exchange(classTyping_, p.delegated);
                const Type* wasValue = std::exchange(delegateValue_, p.delegated && p.type ? &*p.type : nullptr);
                expr(*p.init);
                classTyping_ = wasTyping, delegateValue_ = wasValue;
                out_.text(")> " + propertyStorage(p) + ";");
            }
        }
        // The constructor: Base(args), the members in order, the init blocks. (A val/var parameter is
        // named for its member: the initializers and init blocks reading the Kotlin name reach the
        // member, which the parameter has filled by then.)
        access(true);
        out_.at(k.pos);
        size_t firstDefault = k.ctorParams.size();
        while (firstDefault > 0 && k.ctorParams[firstDefault - 1].defaultValue) --firstDefault;
        auto parameters = [&](bool defaults, bool forMembers) {
            for (size_t i = 0; i < k.ctorParams.size(); ++i) {
                const Param& p = k.ctorParams[i];
                bool renamed = forMembers && k.ctorKinds[i] != ' ';
                out_.text((i ? ", " : "") + paramType(p.type) + " " + (renamed ? "kompoz_" + p.name : cppName(p.name)));
                if (defaults && i >= firstDefault) {
                    out_.text(" = ");
                    expr(*p.defaultValue);
                }
            }
        };
        if (handle) out_.text(" Object(");
        else out_.text(std::string(k.ctorParams.size() == 1 ? " explicit " : " ") + k.name + "(");
        parameters(!handle, true);
        out_.text(")");
        for (size_t i = 0; i < k.ctorParams.size(); ++i)
            if (k.ctorKinds[i] == ' ') declare(k.ctorParams[i].name, Local);
        bool any = false;
        if (k.base && !k.baseArgs.empty()) {
            out_.text(" : " + plainType(*k.base) + "(");
            for (size_t i = 0; i < k.baseArgs.size(); ++i) {
                if (i) out_.text(", ");
                expr(*k.baseArgs[i]);
            }
            out_.text(")");
            any = true;
        }
        for (size_t i = 0; i < k.ctorParams.size(); ++i) {
            if (k.ctorKinds[i] == ' ') continue;
            out_.text(std::string(any ? ", " : " : ") + paramStorage(i) + "(std::move(kompoz_" + k.ctorParams[i].name + "))");
            any = true;
        }
        for (const ClassProperty& p : k.properties) {
            if (!p.init) continue;
            out_.text(std::string(any ? ", " : " : ") + propertyStorage(p) + "(");
            expr(*p.init, p.type && !p.delegated ? &*p.type : nullptr);
            out_.text(")");
            any = true;
        }
        out_.text(" {");
        for (const NodeP& b : k.initBlocks) statements(*b, false);
        out_.text(" }");
        scopes_.pop_back();
        // The functions of the properties read through them: an override parameter's, then the properties'.
        for (size_t i = 0; i < k.ctorParams.size(); ++i) {
            if (!k.ctorOverride[i]) continue;
            const Param& p = k.ctorParams[i];
            const InterfaceMember* base = memberNamed(inherited, p.name);
            if (!base || !base->property) throw CompileError(p.pos, p.name + " overrides no property of " + k.name + "'s interfaces");
            access(!k.ctorPrivate[i]);
            out_.at(p.pos);
            out_.text(" " + type(p.type) + " " + cppName(p.name) + "() const override { return kompoz_value_" + p.name + "; }");
            if (k.ctorKinds[i] == 'm')
                out_.text(" void " + setterOf(p.name) + "(" + paramType(p.type) + " kompoz_v) const" + (base->property->isVar ? " override" : "") +
                          " { kompoz_value_" + p.name + " = kompoz_v; }");
        }
        for (const ClassProperty& p : k.properties)
            if (p.accessed()) accessorFunctions(p, memberNamed(inherited, p.name), false, access);
        // Its functions, each seeing its parameters and, by their names, the class's members.
        bool ownToString = false;
        for (const Function& m : k.methods) {
            if (m.isAbstract) throw CompileError(m.pos, m.name + " needs a body (an abstract class isn't supported: an interface is)");
            access(!m.isPrivate);
            out_.at(m.pos);
            const InterfaceMember* base = memberNamed(inherited, m.name);
            if (base && !base->method) base = nullptr;
            bool any = m.name == "toString" || m.name == "equals" || m.name == "hashCode";  // (Kotlin's Any's)
            ownToString = ownToString || m.name == "toString";
            bool isConst = true;
            std::string suffix, inheritedType;
            if (base) {
                suffix = " override", inheritedType = memberType(objectOf(*base->in), *base);
            } else if (any) {
                if (m.name == "toString" && !bases.empty()) suffix = " override", inheritedType = "std::string";
            } else if (m.overrides) {  // a C++ base's (a ViewModel's onCleared)
                isConst = false, suffix = " override";
            }
            scopes_.emplace_back();
            for (const Param& p : m.params) declare(p.name, Local);
            functionHead(m, " ", isConst, suffix, inheritedType, !handle, true);
            functionBody(m);
            scopes_.pop_back();
        }
        if (!bases.empty() && !ownToString) {
            access(true);
            out_.text(" std::string toString() const override { return ::kompoz::kt::InterfaceObject::identity(\"" + k.name + "\", this); }");
        }
        scopes_.pop_back();
        if (handle) {
            access(true);
            out_.text(" };");  // (Object)
            // The handle: made with the object, the same object in every copy, its public functions forwarded.
            out_.at(k.pos);
            out_.text(std::string(k.ctorParams.size() == 1 ? " explicit " : " ") + k.name + "(");
            parameters(true, false);
            out_.text(") : kompoz_object_(std::make_shared<Object>(");
            for (size_t i = 0; i < k.ctorParams.size(); ++i) out_.text((i ? ", " : "") + cppName(k.ctorParams[i].name));
            out_.text(")) {}");
            out_.text(" " + k.name + "(::kompoz::kt::FromObject, std::shared_ptr<Object> o) : kompoz_object_(std::move(o)) {}");
            out_.text(" static " + k.name + " kompoz_from(std::shared_ptr<Object> o) { return " + k.name + "(::kompoz::kt::FromObject{}, std::move(o)); }");
            out_.text(" Object& kompoz_object() const { return *kompoz_object_; }");
            out_.text(" const std::shared_ptr<Object>& kompoz_shared() const { return kompoz_object_; }");
            out_.text(" bool operator==(const " + k.name + "& o) const { return kompoz_object_ == o.kompoz_object_; }");
            if (!bases.empty() || ownToString) out_.text(" std::string toString() const { return kompoz_object_->toString(); }");
            for (const Function& m : k.methods) {
                if (m.isPrivate || m.name == "toString") continue;
                out_.at(m.pos);
                bool unitResult = m.result ? m.result->name == "Unit" : !m.expressionBody;
                std::string result = m.result || !m.expressionBody ? (unitResult ? (m.isSuspend ? "::kompoz::Task" : "void")
                                                                                 : m.isSuspend ? "::kompoz::TaskOf<" + type(*m.result) + ">" : type(*m.result))
                                                                   : "decltype(auto)";
                out_.text(" " + result + " " + cppName(m.name) + "(");
                for (size_t i = 0; i < m.params.size(); ++i) {
                    const Param& p = m.params[i];
                    out_.text((i ? ", " : "") + paramTypeOf(p, m.isSuspend) + " " + cppName(p.name));
                    if (p.defaultValue) {  // (the defaults where the call arrives)
                        out_.text(" = ");
                        expr(*p.defaultValue);
                    }
                }
                out_.text(") const { return kompoz_object_->" + cppName(m.name) + "(");
                for (size_t i = 0; i < m.params.size(); ++i) out_.text((i ? ", " : "") + cppName(m.params[i].name));
                out_.text("); }");
            }
            out_.text(" private: std::shared_ptr<Object> kompoz_object_;");
        }
        out_.text(" };\n");
        inClass_ = wasInClass;
    }

    // sealed class Name { … }: a struct holding one of its subclasses (std::variant), made from any of them,
    // equal when they are, written as the one it holds; its nested subclasses inside it (Name.Success is
    // Name::Success), the file's top-level ones before it.
    void sealedDefinition(const SealedDecl& s) {
        out_.at(s.pos);
        out_.text("struct " + s.name + " {");
        std::vector<std::string> alternatives;
        for (auto [kind, i] : s.order) {
            if (kind == 'd') {
                classDefinition(s.dataClasses[i]);
                alternatives.push_back(s.name + "::" + s.dataClasses[i].name);
            } else {
                generateObject(out_, s.objects[i], module_);
                alternatives.push_back(s.name + "::" + s.objects[i].name);
            }
        }
        for (const std::string& t : s.topLevel) alternatives.push_back(qualifierOf(t) + t);
        if (alternatives.empty()) throw CompileError(s.pos, "a sealed class needs a subclass (a data class or an object of its own)");
        std::string list, any;
        for (const std::string& a : alternatives) {
            list += (list.empty() ? "" : ", ") + a;
            any += (any.empty() ? "" : " || ") + std::string("std::is_same_v<std::remove_cvref_t<T>, ") + a + ">";
        }
        out_.text(" std::variant<" + list + "> kompoz_value;");
        out_.text(" template <class T> requires(" + any + ") " + s.name + "(T&& value) : kompoz_value(std::forward<T>(value)) {}");
        out_.text(" bool operator==(const " + s.name + "&) const = default;");
        out_.text(" template <class = void> std::string toString() const { return std::visit([](const auto& v) { return ::kompoz::kt::str(v); }, "
                  "kompoz_value); }");
        out_.text(" };\n");
    }

    // object Name { … }: a struct of its own, its members static (Kotlin's object is one value: Name.a is
    // Name::a, Name.f() is Name::f()), its init blocks run once, as the program starts; a property by a
    // delegate or with accessors read through static functions (Name.a is Name::a()). One implementing
    // interfaces is converted to them through its kompoz_Object (interfaceAdapter), the interfaces' functions
    // it doesn't declare reached as its own (static, forwarded to that object).
    void objectDefinition(const ObjectDecl& o) {
        bool route = std::find(o.annotations.begin(), o.annotations.end(), "Serializable") != o.annotations.end();
        bool ownToString = std::any_of(o.methods.begin(), o.methods.end(), [](const Function& m) { return m.name == "toString"; });
        out_.at(o.pos);
        out_.text("struct " + o.name + " {" + (route ? " KOMPOZ_ROUTE(" + o.name + ")" : std::string()) + " bool operator==(const " +
                  o.name + "&) const = default;");
        if (!ownToString) out_.text(" template <class = void> std::string toString() const { return \"" + o.name + "\"; }");
        bool isPublic = true;
        auto access = [&](bool wantPublic) {
            if (wantPublic != isPublic) out_.text(wantPublic ? " public:" : " private:");
            isPublic = wantPublic;
        };
        InterfaceMembers inherited = interfaceMembers(o.supers);
        scopes_.emplace_back();
        declareMembers(o.properties, o.supers, true);
        for (size_t index : o.order) {
            if (index % 2 == 0) {  // val / var: a static member, its type the value's unless one is written
                const ClassProperty& p = o.properties[index / 2];
                bool accessed = p.delegated || p.getter || p.setter || p.privateSetter;
                if (accessed && !p.delegated && !stores(p)) {
                    accessorFunctions(p, memberNamed(inherited, p.name), true, access);
                    continue;
                }
                access(!accessed && !p.isPrivate);
                out_.at(p.pos);
                std::string t = p.type && !p.delegated ? type(*p.type) : std::string("auto");
                out_.text(std::string(" static inline ") + (p.isVar || accessed ? "" : "const ") + t + " " + (accessed ? storageOf(p) : cppName(p.name)) + " = ");
                global_ = true;
                scopes_.emplace_back();
                if (p.init) expr(*p.init, p.type && !p.delegated ? &*p.type : nullptr);
                else out_.text(t + "{}");
                scopes_.pop_back();
                global_ = false;
                out_.text(";");
                if (accessed) accessorFunctions(p, memberNamed(inherited, p.name), true, access);
            } else {  // fun: a static function
                const Function& m = o.methods[index / 2];
                access(!m.isPrivate);
                out_.at(m.pos);
                scopes_.emplace_back();
                for (const Param& p : m.params) declare(p.name, Local);
                functionHead(m, " static ", false, "", "", true, true);
                functionBody(m);
                scopes_.pop_back();
            }
        }
        for (size_t i = 0; i < o.initBlocks.size(); ++i) {
            access(false);
            out_.text(" static inline const bool kompoz_init_" + std::to_string(i) + " = [] {");
            scopes_.emplace_back();
            statements(*o.initBlocks[i], false);
            scopes_.pop_back();
            out_.text(" return true; }();");
        }
        scopes_.pop_back();
        access(true);
        auto own = [&](const std::string& n) {
            return std::any_of(o.properties.begin(), o.properties.end(), [&](const ClassProperty& p) { return p.name == n; }) ||
                   std::any_of(o.methods.begin(), o.methods.end(), [&](const Function& m) { return m.name == n; });
        };
        interfaceAdapter(o.name, o.supers, true, own, o.pos);
        out_.text(" };\n");
    }

    // Whether Name.member of an object is read through a static function (Name::member()): a property by a
    // delegate or with accessors, or an interface's with a getter that it doesn't declare.
    bool objectAccessor(const std::string& object, const std::string& member) const {
        auto o = module_.objectDecls.find(object);
        if (o == module_.objectDecls.end()) return false;
        for (const ClassProperty& p : o->second->properties)
            if (p.name == member) return p.delegated || p.getter || p.setter || p.privateSetter;
        for (const Function& f : o->second->methods)
            if (f.name == member) return false;
        const InterfaceMember* m = memberNamed(interfaceMembers(o->second->supers), member);
        return m && m->property && m->hasBody;
    }

    // val name = value at the top: a constant of the namespace (private: of the file), its type the
    // value's unless one is written.
    void property(const Property& p) {
        const Node& d = *p.decl;
        out_.at(p.pos);
        out_.text(std::string(p.isPrivate ? "static" : "inline") + " const " + (d.type ? type(*d.type) : std::string("auto")) + " " +
                  cppName(p.name) + " = ");
        global_ = true;
        scopes_.emplace_back();
        expr(*d.kids[0], d.type ? &*d.type : nullptr);
        scopes_.pop_back();
        global_ = false;
        out_.text(";\n");
    }

    void definition(const Function& f) {
        bool unit = !f.result || f.result->name == "Unit";
        currentResult_ = f.result ? &*f.result : nullptr;
        if (!f.composable) {  // (no declaration before it: its defaults are here)
            scopes_.emplace_back();
            for (const Param& p : f.params) declare(p.name, Local);  // (a suspend fun's result type may read them)
            signature(f, true);
            out_.text(" {");
            if (f.receiver) {  // its members reached by their names, as a lambda's receiver's
                receivers_.push_back(qualified(f.receiver->name));
                receiverConst_.push_back(true);
                receiverLabels_.push_back({});
                out_.text(" [[maybe_unused]] auto* kompoz_this_1 = &kompoz_receiver_1;");
            }
            bool wasSuspend = std::exchange(suspend_, f.isSuspend);
            statements(*f.body, false);
            if (f.isSuspend && !f.expressionBody && (!f.result || f.result->name == "Unit")) out_.text(" co_return;");
            suspend_ = wasSuspend;
            receivers_.clear();
            receiverConst_.clear();
            receiverLabels_.clear();
            scopes_.pop_back();
            out_.text(" }\n");
            return;
        }
        signature(f, false);
        out_.text(" {");
        scopes_.emplace_back();
        for (const Param& p : f.params) declare(p.name, Local);
        if (unit) {
            // Restartable and skippable, as Compose's compiler makes a Unit @Kompozable function.
            out_.text(" ::kompoz::kompozable(site, ");
            for (const Param& p : f.params) out_.text(cppName(p.name) + ", ");
            out_.text("[=] {");
            statements(*f.body, false);
            out_.text(" });");
        } else {
            // Not restartable (what it reads is its caller's), but a group of its own: what it
            // remembers is its, however many times its caller calls it from one place.
            out_.text(" ::kompoz::KompozableScope kompoz_scope(site);");
            statements(*f.body, true);
        }
        scopes_.pop_back();
        out_.text(" }\n");
        if (unit && f.params.empty() && std::find(f.annotations.begin(), f.annotations.end(), "Preview") != f.annotations.end()) {
            // @Preview(widthDp = 400, heightDp = 220, showBackground = true, uiMode = UI_MODE_NIGHT_YES): a preview
            // registered under the function's name (preview.hpp's KOMPOZ_PREVIEW), shown with KOMPOZ_PREVIEW=Name.
            std::string spec;
            auto arg = [&](const char* key) { auto it = f.annotationArgs.find(key); return it == f.annotationArgs.end() ? std::string() : it->second; };
            if (!arg("widthDp").empty()) spec += ".width = " + arg("widthDp") + ", ";
            if (!arg("heightDp").empty()) spec += ".height = " + arg("heightDp") + ", ";
            if (arg("uiMode").find("NIGHT_YES") != std::string::npos) spec += ".darkTheme = true, ";
            if (arg("showBackground") == "true") spec += ".background = ::kompoz::colors::White, ";
            if (!spec.empty()) spec.resize(spec.size() - 2);
            out_.text("static const ::kompoz::detail::PreviewRegistration kompoz_preview_" + f.name + "{{\"" + f.name + "\", ::kompoz::PreviewSpec{" + spec +
                      "}, [] { " + f.name + "(); }}};\n");
        }
    }

private:
    // Local: a local (Callable: one of a function type, val f: (Int) -> Unit = { … }); Delegated: a local by a delegate
    // (var x by remember { … }: reading it reads the state); MemberDelegated: a class's or object's property by a
    // delegate, held in kompoz_by_<name>; Accessor: a member read and set through its functions (name(), setName(v));
    // BackingField: `field` in an accessor (fieldOf_).
    enum VarKind { Local, Delegated, Callable, MemberDelegated, Accessor, BackingField };

    void declare(const std::string& name, VarKind kind) { scopes_.back()[name] = kind; }
    const VarKind* lookup(const std::string& name) const {
        for (auto s = scopes_.rbegin(); s != scopes_.rend(); ++s)
            if (auto f = s->find(name); f != s->end()) return &f->second;
        return nullptr;
    }

    // Which receiver (1 for the outermost) has `name` as a member, nearest first; 0 for none. (Kotlin
    // looks at local names first, then at the implicit receivers, then at what is declared at the top.)
    size_t receiverOf(const std::string& name) const {
        if (!module_.structs) return 0;
        for (size_t r = receivers_.size(); r > 0; --r) {
            std::string_view type = receivers_[r - 1];
            if (size_t colons = type.rfind("::"); colons != std::string_view::npos) type = type.substr(colons + 2);  // (ImageVector::Builder)
            if (module_.structs->member(type, name)) return r;
        }
        return 0;
    }

    // A lambda's capture: none at namespace scope (a top-level val's value), where there is nothing to
    // capture and C++ takes no default. What is inside it is in a block scope (Scoped).
    std::string capture(const char* captureDefault) const {
        if (global_) return "[]";
        if (inClass_ && std::string_view(captureDefault) == "[=]") return "[=, this]";  // (C++20 deprecates [=]'s this)
        return captureDefault;
    }
    struct Scoped {
        explicit Scoped(bool& global) : global_(global), was_(std::exchange(global, false)) {}
        ~Scoped() { global_ = was_; }
        bool& global_;
        bool was_;
    };

    std::string qualifierOf(const std::string& name) const {
        auto q = module_.qualifiers.find(name);
        return q == module_.qualifiers.end() ? std::string() : q->second;
    }

    // ---- Statements ----

    // The statements of a block; with `tail`, the last one gives the block's value (return).
    void statements(const Node& block, bool tail) {
        scopes_.emplace_back();
        for (size_t i = 0; i < block.kids.size(); ++i) statement(*block.kids[i], tail && i + 1 == block.kids.size());
        scopes_.pop_back();
    }

    void braced(const Node& block, bool tail) {
        out_.text(" {");
        statements(block, tail);
        out_.text(" }");
    }

    void statement(const Node& s, bool tail) {
        out_.at(s.pos);
        out_.text(" ");
        switch (s.kind) {
            case Node::ExprStmt: {
                const Node& e = *s.kids[0];
                if (e.kind == Node::If) return ifStatement(e, tail);
                if (e.kind == Node::When) return whenStatement(e, tail);
                if (e.kind == Node::Unary && (e.text == "++" || e.text == "--")) {
                    if (const Node* target = e.kids[0].get(); delegatedName(*target)) {
                        out_.text(held(target->text) + ".set(::kompoz::kt::value(" + held(target->text) + ") " + e.text[0] + " 1);");
                        return;
                    }
                    if (const Node* target = e.kids[0].get(); target->kind == Node::Member && !target->flag && target->text != "value" &&
                                                              target->text != "size" && target->text != "length") {
                        // shelf.picks++: the property set to its value plus one (through its setter, where it has one)
                        out_.text("{ auto&& kompoz_o = ");
                        expr(*target->kids[0]);
                        out_.text("; KOMPOZ_KT_SET((kompoz_o), " + cppName(target->text) + ", " + setterOf(target->text) + ", (KOMPOZ_KT_GET((kompoz_o), " +
                                  cppName(target->text) + ") " + e.text[0] + " 1)); }");
                        return;
                    }
                    if (const Node* target = e.kids[0].get(); accessorName(*target)) {  // count++ on a property with a setter
                        out_.text(setterOf(target->text) + "(" + cppName(target->text) + "() " + e.text[0] + " 1);");
                        return;
                    }
                }
                if (tail) out_.text("return ");
                expr(e);
                out_.text(";");
                return;
            }
            case Node::Decl: return declaration(s);
            case Node::Assign: return assignment(s);
            case Node::Return:
                // return@Label: from the lambda given to Label, which must be the one just around it (C++ returns from that one only)
                if (!s.text.empty() && (lambdaLabels_.empty() || lambdaLabels_.back() != s.text))
                    throw CompileError(s.pos, "return@" + s.text + ": only from the lambda just around it" +
                                                  (lambdaLabels_.empty() || lambdaLabels_.back().empty() ? std::string() : " (" + lambdaLabels_.back() + "'s)"));
                out_.text(suspend_ ? "co_return" : "return");
                if (!s.kids.empty()) {
                    out_.text(" ");
                    expr(*s.kids[0]);
                }
                out_.text(";");
                return;
            case Node::Break: out_.text("break;"); return;
            case Node::Continue: out_.text("continue;"); return;
            case Node::For: return forStatement(s);
            case Node::While:
                out_.text("while (");
                expr(*s.kids[0]);
                out_.text(")");
                braced(*s.kids[1], false);
                return;
            case Node::Try: return tryStatement(s);
            case Node::DoWhile:
                out_.text("do");
                braced(*s.kids[0], false);
                out_.text(" while (");
                expr(*s.kids[1]);
                out_.text(");");
                return;
            default: throw CompileError(s.pos, "unexpected statement");
        }
    }

    bool delegatedName(const Node& n) const {
        if (n.kind != Node::Name) return false;
        const VarKind* k = lookup(n.text);
        return k && (*k == Delegated || *k == MemberDelegated);
    }
    bool accessorName(const Node& n) const {
        if (n.kind != Node::Name) return false;
        const VarKind* k = lookup(n.text);
        return k && *k == Accessor;
    }
    // What holds a delegated name's delegate: the local, or a member's kompoz_by_<name>.
    std::string held(const std::string& name) const {
        const VarKind* k = lookup(name);
        return k && *k == MemberDelegated ? "kompoz_by_" + name : cppName(name);
    }
    static std::string setterOf(const std::string& name) {
        std::string set = "set" + name;
        set[3] = char(std::toupper(static_cast<unsigned char>(set[3])));
        return set;
    }

    void declaration(const Node& d) {
        if (!d.names.empty()) {  // val (a, b) = pair
            std::string held = destructured();
            out_.text("auto&& " + held + " = ");
            expr(*d.kids[0]);
            out_.text(";");
            destructuredNames(d.names, held);
            for (auto& n : d.names) declare(n, Local);
            return;
        }
        std::string name = cppName(d.text);
        if (d.delegated) {
            // var x by remember { mutableStateOf(0) }: x holds the state; reading x reads its value.
            out_.text("auto " + name + " = ");
            expr(*d.kids[0]);
            out_.text(";");
            declare(d.text, Delegated);
            return;
        }
        out_.text((d.type ? type(*d.type) : std::string("auto")) + " " + name);
        if (!d.kids.empty()) {
            out_.text(" = ");
            expr(*d.kids[0], d.type ? &*d.type : nullptr);
        } else if (d.type) {
            out_.text("{}");
        } else {
            throw CompileError(d.pos, "a variable without a value needs a type");
        }
        out_.text(";");
        declare(d.text, d.type && d.type->function ? Callable : Local);
    }

    void assignment(const Node& a) {
        const Node& target = *a.kids[0];
        std::string op = a.text;
        auto rhs = [&] {
            if (op == "=") {
                expr(*a.kids[1]);
            } else {  // x += v: x's value (a state's read through) and v
                expr(target);
                out_.text(" " + op.substr(0, 1) + " (");
                expr(*a.kids[1]);
                out_.text(")");
            }
        };
        if (delegatedName(target)) {  // x = v on a delegated x: the state set
            out_.text(held(target.text) + ".set(");
            rhs();
            out_.text(");");
            return;
        }
        if (accessorName(target)) {  // x = v on a member with a setter (a class's own or an interface's property)
            out_.text(setterOf(target.text) + "(");
            rhs();
            out_.text(");");
            return;
        }
        if (target.kind == Node::Member && target.text == "value") {  // state.value = v
            expr(*target.kids[0]);
            out_.text(".set(");
            rhs();
            out_.text(");");
            return;
        }
        // obj.name = v, or name = v on a receiver's member: the property set (its setter where it has one: setName)
        auto setter = setterOf;
        if (op != "=" && target.kind == Node::Member && !target.flag && target.text != "value") {  // shelf.picks += 2: through its setter
            out_.text("{ auto&& kompoz_o = ");
            expr(*target.kids[0]);
            out_.text("; KOMPOZ_KT_SET((kompoz_o), " + cppName(target.text) + ", " + setterOf(target.text) + ", (KOMPOZ_KT_GET((kompoz_o), " +
                      cppName(target.text) + ") " + op.substr(0, op.size() - 1) + " (");
            expr(*a.kids[1]);
            out_.text("))); }");
            return;
        }
        if (op == "=" && target.kind == Node::Member && !target.flag && target.text != "value") {
            out_.text("KOMPOZ_KT_SET((");
            expr(*target.kids[0]);
            out_.text("), " + cppName(target.text) + ", " + setter(target.text) + ", (");
            rhs();
            out_.text("));");
            return;
        }
        if (op == "=" && target.kind == Node::Name && !lookup(target.text)) {
            if (size_t r = receiverOf(target.text)) {
                out_.text("KOMPOZ_KT_SET((*kompoz_this_" + std::to_string(r) + "), " + cppName(target.text) + ", " + setter(target.text) + ", (");
                rhs();
                out_.text("));");
                return;
            }
        }
        if (target.kind == Node::Index && target.kids.size() == 2 && op == "=") {  // a[k] = v: set(k, v) where there is one (a SavedStateHandle's)
            out_.text("::kompoz::kt::setAt(");
            expr(*target.kids[0]);
            out_.text(", ");
            expr(*target.kids[1]);
            out_.text(", ");
            expr(*a.kids[1]);
            out_.text(");");
            return;
        }
        if (target.kind == Node::Index) {  // a[k] += v: the element itself
            indexed(target);
            out_.text(" " + op + " ");
            expr(*a.kids[1]);
            out_.text(";");
            return;
        }
        expr(target);
        out_.text(" " + op + " ");
        expr(*a.kids[1]);
        out_.text(";");
    }

    void ifStatement(const Node& n, bool tail) {
        out_.text("if (");
        std::vector<const Node*> casts;
        castCondition(*n.kids[0], casts);
        out_.text(") {");
        scopes_.emplace_back();
        smartCasts(casts);
        statements(*n.kids[1], tail);
        scopes_.pop_back();
        out_.text(" }");
        if (n.kids.size() > 2) {
            out_.text(" else");
            braced(*n.kids[2], tail);
        }
    }

    // A condition, its `x is T` tests on locals (and a && chain's after them) giving Kotlin's smart casts:
    // what follows a test sees x as a T (`x is T && x.y > 0`), and `casts` gets the tests, for the block after.
    void castCondition(const Node& c, std::vector<const Node*>& casts) {
        if (c.kind == Node::Binary && c.text == "&&") {
            out_.text("(");
            castCondition(*c.kids[0], casts);
            out_.text(" && ");
            if (casts.empty()) {
                castCondition(*c.kids[1], casts);
            } else {
                out_.text(capture("[&]") + "() -> bool {");
                scopes_.emplace_back();
                smartCasts(casts);
                out_.text(" return ");
                castCondition(*c.kids[1], casts);
                out_.text("; }()");
                scopes_.pop_back();
            }
            out_.text(")");
            return;
        }
        if (c.kind == Node::Paren && c.kids[0]->kind == Node::Binary && c.kids[0]->text == "&&") return castCondition(*c.kids[0], casts);
        expr(c);
        if (c.kind == Node::Is && !c.flag && c.kids[0]->kind == Node::Name)
            if (const VarKind* k = lookup(c.kids[0]->text); k && *k == Local) casts.push_back(&c);
    }
    // Each test's view, under the local's own name (Kotlin's smart cast): the T inside its value.
    void smartCasts(const std::vector<const Node*>& casts) {
        for (const Node* is : casts) {
            const std::string& n = is->kids[0]->text;
            std::string held = "kompoz_smart_" + std::to_string(++smart_);
            out_.text(" const auto& " + held + " = ::kompoz::kt::as<" + type(*is->type) + ">(" + cppName(n) + "); [[maybe_unused]] const auto& " +
                      cppName(n) + " = " + held + ";");
            declare(n, Local);
        }
    }

    void whenCondition(const Node::WhenBranch& b, const std::string& subject) {
        for (size_t i = 0; i < b.conds.size(); ++i) {
            if (i) out_.text(" || ");
            auto& [kind, e] = b.conds[i];
            out_.text("(");
            switch (kind) {
                case Node::WhenBranch::Expr:
                    if (subject.empty()) {
                        expr(*e);
                    } else {
                        out_.text(subject + " == ");
                        expr(*e);
                    }
                    break;
                case Node::WhenBranch::In:
                case Node::WhenBranch::NotIn:
                    out_.text(std::string(kind == Node::WhenBranch::NotIn ? "!" : "") + "::kompoz::kt::contains(");
                    expr(*e);
                    out_.text(", " + subject + ")");
                    break;
                default:  // is T, !is T
                    out_.text(std::string(kind == Node::WhenBranch::NotIs ? "!" : "") + "::kompoz::kt::is<" + type(b.types[i]) + ">(" + subject + ")");
                    break;
            }
            out_.text(")");
        }
    }

    void whenStatement(const Node& n, bool tail, bool asValue = false) {
        std::string subject, smartName;  // (smartName: the subject's, a local's, seen as the T of an `is T` branch)
        out_.text("{");
        if (const Node* s = n.kids[0].get()) {
            if (s->kind == Node::Decl) smartName = s->text;
            else if (const VarKind* k = s->kind == Node::Name ? lookup(s->text) : nullptr; k && *k == Local) smartName = s->text;
            subject = "kompoz_when_" + std::to_string(n.pos.line) + "_" + std::to_string(n.pos.column);
            out_.text(" const auto& " + subject + " = ");
            expr(s->kind == Node::Decl ? *s->kids[0] : *s);
            out_.text(";");
            if (s->kind == Node::Decl) out_.text(" const auto& " + cppName(s->text) + " = " + subject + ";");
            scopes_.emplace_back();
            if (s->kind == Node::Decl) declare(s->text, Local);
        } else {
            scopes_.emplace_back();
        }
        bool first = true, otherwise = false;
        for (const auto& b : n.branches) {
            out_.at(b.pos);
            std::vector<const Node*> casts;
            std::string cast;  // (an `is T` branch of a local's subject: the local as the T)
            if (b.conds.empty()) {
                out_.text(first ? " {" : " else {");
                otherwise = true;
            } else if (subject.empty() && b.conds.size() == 1) {  // when { x is T -> … }
                out_.text(first ? " if (" : " else if (");
                castCondition(*b.conds[0].second, casts);
                out_.text(") {");
            } else {
                out_.text(first ? " if (" : " else if (");
                whenCondition(b, subject);
                out_.text(") {");
                if (!smartName.empty() && b.conds.size() == 1 && b.conds[0].first == Node::WhenBranch::Is)
                    cast = " [[maybe_unused]] const auto& " + cppName(smartName) + " = ::kompoz::kt::as<" + type(b.types[0]) + ">(" + subject + ");";
            }
            scopes_.emplace_back();
            smartCasts(casts);
            if (!cast.empty()) {
                out_.text(cast);
                declare(smartName, Local);
            }
            statements(*b.body, tail);
            scopes_.pop_back();
            out_.text(" }");
            first = false;
        }
        // A when giving a value covers every case (Kotlin checks it: a sealed class's subclasses): no way out.
        if (asValue && !otherwise && !first) out_.text(" else { ::kompoz::kt::noBranch(); }");
        scopes_.pop_back();
        out_.text(" }");
    }

    // try { } catch (e: T) { } finally { }: C++'s try / catch; the finally block runs from a guard's destructor,
    // so a coroutine cancelled inside the try runs it too (its frame is destroyed), as Kotlin's does.
    void tryStatement(const Node& t) {
        out_.text("{");
        if (t.flag) {
            out_.text(" ::kompoz::kt::Finally kompoz_finally_" + std::to_string(++finally_) + "(" + capture("[&]") + "() {");
            scopes_.emplace_back();
            statements(*t.kids.back(), false);
            scopes_.pop_back();
            out_.text(" });");
        }
        if (t.names.empty()) {
            scopes_.emplace_back();
            braced(*t.kids[0], false);
            scopes_.pop_back();
        } else {
            out_.text(" try");
            scopes_.emplace_back();
            braced(*t.kids[0], false);
            scopes_.pop_back();
            for (size_t i = 0; i < t.names.size(); ++i) {
                scopes_.emplace_back();
                declare(t.names[i], Local);
                out_.text(" catch (const " + type(t.typeArgs[i]) + "& " + cppName(t.names[i]) + ")");
                braced(*t.kids[1 + i], false);
                scopes_.pop_back();
            }
        }
        out_.text(" }");
    }
    int finally_ = 0;

    void forStatement(const Node& f) {
        const Node& it = *f.kids[0];
        scopes_.emplace_back();
        for (auto& n : f.names) declare(n, Local);
        if (f.names.size() == 1) {
            out_.text("for (auto&& " + cppName(f.names[0]) + " : ");
            expr(it);
            out_.text(")");
            braced(*f.kids[1], false);
        } else {
            std::string held = destructured();
            out_.text("for (auto&& " + held + " : ");
            expr(it);
            out_.text(") {");
            destructuredNames(f.names, held);
            statements(*f.kids[1], false);
            out_.text(" }");
        }
        scopes_.pop_back();
    }

    // A destructuring (val (a, b) = x; for ((k, v) in m)) holds the whole and takes each part with
    // kt::component<i>: a copy, as Kotlin's component vals are. (Not a structured binding: GCC 16 loses
    // one inside a generic lambda nested in another, which every composable's block is.) `_` declares nothing.
    std::string destructured() { return "kompoz_d" + std::to_string(destructured_++); }
    void destructuredNames(const std::vector<std::string>& names, const std::string& held) {
        for (size_t i = 0; i < names.size(); ++i)
            if (names[i] != "_")
                out_.text(" [[maybe_unused]] auto " + cppName(names[i]) + " = ::kompoz::kt::component<" + std::to_string(i) + ">(" + held + ");");
    }
    int destructured_ = 0;

    // ---- Expressions ----

    // `expected`: the type the value goes to, where it is known (a declaration's).
    void expr(const Node& e, const Type* expected = nullptr) {
        switch (e.kind) {
            case Node::Name: return name(e);
            case Node::Number: out_.text(number(e.text)); return;
            case Node::String: return string(e);
            case Node::Char: out_.text("U'" + e.text + "'"); return;
            case Node::Bool: out_.text(e.text); return;
            case Node::Null: out_.text("::kompoz::kt::null"); return;
            case Node::This: {  // in a lambda with a receiver: the receiver; this@Label: the lambda Label(…) { }'s
                size_t r = receivers_.size();
                if (!e.text.empty()) {
                    while (r > 0 && receiverLabels_[r - 1] != e.text) --r;
                    if (r == 0) throw CompileError(e.pos, "this@" + e.text + ": no lambda of " + e.text + " around it");
                }
                out_.text(r == 0 ? std::string("(*this)") : "(*kompoz_this_" + std::to_string(r) + ")");
                return;
            }
            case Node::Paren:
                out_.text("(");
                expr(*e.kids[0]);
                out_.text(")");
                return;
            case Node::Call:
                if (suspend_ && awaitable(e)) {  // in a coroutine: awaited if it suspends (kotlin.hpp's kt::await)
                    out_.text("(co_await ::kompoz::kt::await((");
                    call(e);
                    out_.text(", ::kompoz::kt::VoidMark{})))");
                    return;
                }
                if (awaitedTypes_ && awaitable(e)) {  // a suspend fun's result type, taken from its expression
                    out_.text("::kompoz::kt::awaitedType((");
                    call(e);
                    out_.text(", ::kompoz::kt::VoidMark{}))");
                    return;
                }
                expectedCall_ = expected;
                return call(e);
            case Node::Member: return member(e);
            case Node::Index:
                if (e.kids.size() == 2) {  // a[k]: a map's element is nullable (kt::at), a list's is the element
                    out_.text("::kompoz::kt::at(");
                    expr(*e.kids[0]);
                    out_.text(", ");
                    expr(*e.kids[1]);
                    out_.text(")");
                    return;
                }
                indexed(e);
                return;
            case Node::Lambda: return lambda(e, expected && expected->function && expected->result && expected->result->name != "Unit");
            case Node::Unary: return unary(e);
            case Node::Binary: return binary(e);
            case Node::As:  // x as T (a sealed class's T inside it, else a cast); x as? T: none when it isn't one
                out_.text(std::string(e.flag ? "::kompoz::kt::asOrNull<" : "::kompoz::kt::as<") + type(*e.type) + ">(");
                expr(*e.kids[0]);
                out_.text(")");
                return;
            case Node::Is:
                out_.text(std::string(e.flag ? "!" : "") + "::kompoz::kt::is<" + type(*e.type) + ">(");
                expr(*e.kids[0]);
                out_.text(")");
                return;
            case Node::If: return ifExpression(e, expected);
            case Node::Object: return objectExpression(e);
            case Node::When:
                // A when as a value: run where it stands, each branch giving its value (a declared type the lambda's).
                out_.text(capture("[&]") + "()" + (expected ? " -> " + type(*expected) : std::string()) + " {");
                {
                    Scoped inside(global_);
                    whenStatement(e, true, true);
                }
                out_.text(" }()");
                return;
            default: throw CompileError(e.pos, "unexpected expression");
        }
    }

    void name(const Node& e) {
        if (const VarKind* k = lookup(e.text)) {
            if (*k == Delegated || *k == MemberDelegated) out_.text("::kompoz::kt::delegated(" + held(e.text) + ")");
            else if (*k == Accessor) out_.text(selfPrefix_ + cppName(e.text) + "()");
            else if (*k == BackingField) out_.text(fieldOf_);
            else out_.text(cppName(e.text));
            return;
        }
        if (size_t r = receiverOf(e.text)) {  // a receiver's member: its getter, or the field
            out_.text("KOMPOZ_KT_GET((*kompoz_this_" + std::to_string(r) + "), " + cppName(e.text) + ")");
            return;
        }
        if (module_.properties.contains(e.text)) {  // a top-level val (after a receiver's members, as in Kotlin)
            out_.text(qualifierOf(e.text) + cppName(e.text));
            return;
        }
        if (module_.objects.contains(e.text)) {  // an object: its one value
            out_.text(qualifierOf(e.text) + e.text + "{}");
            return;
        }
        if (e.text == "Modifier") {
            out_.text("::kompoz::Modifier()");
            return;
        }
        if (inClass_ && kBareProperties.contains(e.text)) {  // a class's own property that is a function here
            out_.text(cppName(e.text) + "()");
            return;
        }
        if (auto p = kComposableProperties.find(e.text); p != kComposableProperties.end()) {
            out_.text(p->second);
            return;
        }
        if (e.text == "Unit") {
            out_.text("::kompoz::kt::Unit");
            return;
        }
        if (auto k = kBareConstants.find(e.text); k != kBareConstants.end()) {  // PI
            out_.text(k->second);
            return;
        }
        out_.text(cppName(e.text));
    }

    static std::string number(std::string n) {
        bool hex = n.size() > 1 && (n[1] == 'x' || n[1] == 'X' || n[1] == 'b' || n[1] == 'B');
        std::string suffix;
        while (!n.empty() && (n.back() == 'L' || n.back() == 'u' || n.back() == 'U' || (!hex && (n.back() == 'f' || n.back() == 'F')))) {
            suffix.insert(suffix.begin(), n.back());
            n.pop_back();
        }
        bool fl = suffix.find_first_of("fF") != std::string::npos;
        if (fl && n.find_first_of(".eE") == std::string::npos) n += ".0";  // 1f -> 1.0f
        std::string out = n;
        for (char c : suffix) {
            if (c == 'L') out += "LL";
            else if (c == 'f' || c == 'F') out += "f";
            else out += "u";
        }
        return out;
    }

    void string(const Node& e) {
        if (e.kids.empty()) {
            std::string text;
            for (auto& p : e.parts) text += p.text;
            out_.text("std::string(\"" + text + "\")");
            return;
        }
        out_.text("::kompoz::kt::str(");
        size_t k = 0;
        for (size_t i = 0; i < e.parts.size(); ++i) {
            if (i) out_.text(", ");
            if (e.parts[i].expr) expr(*e.kids[k++]);
            else out_.text("\"" + e.parts[i].text + "\"");
        }
        out_.text(")");
    }

    // A name that stands for a type or a namespace here (Arrangement, Icons.Filled): what comes
    // after it is reached with ::, not with a dot.
    bool qualifier(const Node& e) const {
        if (e.kind == Node::Name)
            return (capitalized(e.text) || (module_.structs && module_.structs->namespaces.contains(e.text))) && !lookup(e.text) && e.text != "Modifier" &&
                   !kBareConstants.contains(e.text) && !module_.properties.contains(e.text);  // (BadgeIcon.root: a top-level val's)
        if (e.kind == Node::Member)
            return !e.flag && capitalized(e.text) && qualifier(*e.kids[0]) && !staticValue(e) &&
                   !(e.kids[0]->kind == Node::Name && e.kids[0]->text == "MaterialShapes");  // (MaterialShapes.Heart: a shape, a value)
        return false;
    }
    // Color.White: a type's member of its own (a static value), so Color.White.copy(…) calls on it.
    bool staticValue(const Node& e) const {
        if (!module_.structs || e.kind != Node::Member) return false;
        const Node& type = companionless(*e.kids[0]);
        return module_.structs->member(type.text, e.text);
    }

    std::string dotted(const Node& e) const {
        if (e.kind == Node::Name) return e.text;
        return dotted(*e.kids[0]) + "." + e.text;
    }

    void member(const Node& e) {
        if (e.text == "Companion") return expr(companionless(e));
        const Node& obj = companionless(*e.kids[0]);
        if (e.flag) {  // a?.b
            out_.text("::kompoz::kt::safe(");
            expr(obj);
            out_.text(", " + capture("[&]") + (e.text == "size" || e.text == "length" ? "(auto&& kompoz_safe) { return ::kompoz::kt::size(kompoz_safe); })"
                                                                                    : "(auto&& kompoz_safe) { return KOMPOZ_KT_GET((kompoz_safe), " + cppName(e.text) + "); })"));
            return;
        }
        if (obj.kind == Node::Member || obj.kind == Node::Name) {
            if (qualifier(obj)) {
                auto special = kObjectProperties.find(dotted(obj) + "." + e.text);
                if (special != kObjectProperties.end()) {
                    out_.text(special->second);
                    return;
                }
                if (dotted(obj) == "MaterialShapes" && capitalized(e.text)) {  // Compose's MaterialShapes.Cookie9Sided (a property): a function here
                    out_.text("::kompoz::MaterialShapes::" + e.text + "()");
                    return;
                }
            }
        }
        if (e.text == "dp") {
            out_.text("float(");
            expr(obj);
            out_.text(")");
            return;
        }
        if (e.text == "fr") {  // a grid track's share (1.fr)
            out_.text("::kompoz::Fr{float(");
            expr(obj);
            out_.text(")}");
            return;
        }
        if (e.text == "sp" || e.text == "em") {
            out_.text("::kompoz::TextUnit::" + e.text + "(float(");
            expr(obj);
            out_.text("))");
            return;
        }
        if (obj.kind == Node::Name && e.text == "current" && obj.text.starts_with("Local") && !lookup(obj.text)) {
            out_.text(obj.text + ".current()");  // LocalDensity.current
            return;
        }
        if (e.text == "size" || e.text == "length") {  // list.size, text.length: Kotlin's properties
            out_.text("::kompoz::kt::size(");
            expr(obj);
            out_.text(")");
            return;
        }
        if (e.text == "ordinal" || e.text == "keys" || e.text == "values") {  // an enum's place; a map's keys and values
            out_.text("::kompoz::kt::" + e.text + "(");
            expr(obj);
            out_.text(")");
            return;
        }
        if (e.text == "value") {  // state.value
            out_.text("::kompoz::kt::value(");
            expr(obj);
            out_.text(")");
            return;
        }
        if (qualifier(obj)) {
            out_.text(qualified(dotted(obj)) + "::" + cppName(e.text));
            if (&e != calleeNode_ && module_.objects.contains(dotted(obj) + "." + e.text)) {  // UiState.Loading: a sealed class's own object, its value
                out_.text("{}");
                return;
            }
            if (&e != calleeNode_ && objectAccessor(dotted(obj), e.text)) {  // Settings.dark (by mutableStateOf): its getter
                out_.text("()");
                return;
            }
            // WindowInsets.statusBars, a class's static function read as Compose's property: called.
            std::string type = dotted(obj);
            if (size_t dot = type.rfind('.'); dot != std::string::npos) type = type.substr(dot + 1);
            if (&e != calleeNode_ && module_.structs && module_.structs->staticFunction(type, e.text)) out_.text("()");
            return;
        }
        // A property: a getter of the same name where the type has one (listState.firstVisibleItemIndex is
        // firstVisibleItemIndex() here), else the field.
        out_.text("KOMPOZ_KT_GET((");
        expr(obj);
        out_.text("), " + cppName(e.text) + ")");
    }

    // Whether a call in a coroutine may suspend: not a type's construction (Point(…), a data class,
    // a capitalized name), not a function given its coroutine (launch { } is the coroutine's).
    bool awaitable(const Node& c) const {
        const Node& callee = companionless(*c.kids[0]);
        std::string name = callee.kind == Node::Name ? callee.text : callee.kind == Node::Member ? callee.text : "";
        if (callee.kind == Node::Name && capitalized(callee.text) && !lookup(callee.text)) return false;
        if (kSuspendLambdas.contains(name) && !kAwaitedBlocks.contains(name)) return false;
        return true;
    }

    // X.Companion is X.
    static const Node& companionless(const Node& n) {
        return n.kind == Node::Member && n.text == "Companion" ? companionless(*n.kids[0]) : n;
    }

    void call(const Node& c) {
        const Type* expected = std::exchange(expectedCall_, nullptr);
        const Node& callee = companionless(*c.kids[0]);
        std::string calleeName = callee.kind == Node::Name ? callee.text : callee.kind == Node::Member ? callee.text : "";
        if (!selfPrefix_.empty() && callee.kind == Node::Name && !lookup(callee.text) && interfaceMethods_.contains(callee.text))
            out_.text(selfPrefix_);  // (an interface's own function, in a declaration's type)
        if (callee.kind == Node::Name && !lookup(callee.text) && c.lambda && c.kids.size() == 1)
            if (const InterfaceDecl* i = interfaceNamed(callee.text); i && i->isFun) {  // Validator { it.isNotEmpty() }: a fun interface's object
                const Function* m = nullptr;
                for (const Function& f : i->methods)
                    if (f.isAbstract) m = &f;
                out_.text(qualified(i->name) + "::kompoz_lambda(");
                lambda(*c.lambda, m->result && m->result->name != "Unit");
                out_.text(")");
                return;
            }
        // A local of the callee's name is not what is called when the call has a trailing lambda or named
        // arguments (a value can't take them): items(items, key = { }) { } calls the function, as Kotlin does.
        if (const VarKind* k = callee.kind == Node::Name ? lookup(callee.text) : nullptr; k && *k == Callable && c.lambda) {
            // strip("x") { t -> … }: a local of a function type called with its trailing lambda (a value: what the type returns)
            out_.text(cppName(callee.text) + "(");
            for (size_t i = 1; i < c.kids.size(); ++i) {
                if (i > 1) out_.text(", ");
                expr(*c.kids[i]);
            }
            if (c.kids.size() > 1) out_.text(", ");
            lambda(*c.lambda, true);
            out_.text(")");
            return;
        }
        if (callee.kind == Node::Name && lookup(callee.text) && (c.lambda || named(c))) {
            if (auto f = module_.functions.find(callee.text); f != module_.functions.end()) return moduleCall(c, *f->second);
            out_.text("::kompoz::" + cppName(callee.text));  // (qualified: C++ would take the local)
            return arguments(c, calleeName, false);
        }
        // A receiver's member (drawCircle(…) in a Canvas's lambda): before the file's and Kotlin's own.
        if (callee.kind == Node::Name && !lookup(callee.text)) {
            if (size_t r = receiverOf(callee.text)) {
                out_.text("(*kompoz_this_" + std::to_string(r) + ")." + cppName(callee.text));
                if (!c.typeArgs.empty()) {  // kompozable<Profile> { } in a NavHost
                    out_.text("<");
                    for (size_t i = 0; i < c.typeArgs.size(); ++i) out_.text((i ? ", " : "") + plainType(c.typeArgs[i]));
                    out_.text(">");
                }
                return arguments(c, calleeName, false);
            }
            if (kScopeFunctions.contains(callee.text) && !receivers_.empty()) {  // the scope first (the PointerInputScope around it)
                size_t r = receivers_.size();
                while (r > 1 && receivers_[r - 1] != "PointerInputScope") --r;
                out_.text(callee.text + "((*kompoz_this_" + std::to_string(r) + ")");
                return arguments(c, calleeName, false, true);
            }
            if (callee.text == "flow" && c.lambda && c.kids.size() == 1 && c.typeArgs.empty()) {
                // flow { emit(…) } in a function returning Flow<T>: flow<T>, its lambda the collector's (emit)
                if (!currentResult_ || currentResult_->name != "Flow" || currentResult_->args.size() != 1)
                    throw CompileError(c.pos, "flow { } needs the function's type: fun f(): Flow<T> = flow { … }");
                out_.text("::kompoz::flow<" + type(currentResult_->args[0]) + ">");
                return arguments(c, calleeName, false);
            }
            if (callee.text == "DraggableAnchors" && c.lambda && c.kids.size() == 1) {  // Compose's builder: DraggableAnchors { A at 0f }
                out_.text("::kompoz::draggableAnchors");
                return arguments(c, calleeName, false);
            }
        }
        // A @Kompozable kompozc translates: arguments by position, defaults filled in.
        if (callee.kind == Node::Name && !lookup(callee.text)) {
            if (auto f = module_.functions.find(callee.text); f != module_.functions.end()) return moduleCall(c, *f->second);
            if (auto k = module_.classes.find(callee.text); k != module_.classes.end()) return construct(c, *k->second);
            if (auto k = module_.classDecls.find(callee.text); k != module_.classDecls.end() && named(c)) return constructClass(c, *k->second);
            if (auto t = kTypes.find(callee.text); t != kTypes.end() && t->second.starts_with("::")) {  // a type named in full (KompozTestRule { })
                out_.text(t->second);
                return arguments(c, calleeName, false);
            }
            if (callee.text == "Canvas" && c.kids.size() == 2 && c.names[0].empty() && !c.lambda) {
                // Compose's Canvas(image): drawing into an ImageBitmap (graphics::Canvas), not the composable, which takes its block
                out_.text("::kompoz::graphics::Canvas");
                return arguments(c, calleeName, false);
            }
            if (callee.text == "Color" && c.kids.size() == 2 && c.names[0].empty() && !c.lambda) {
                // Compose's Color(0xFF6750A4), an Int or a Long: AARRGGBB (its packed ULong form is Color(value = …), not this)
                out_.text("::kompoz::Color::argb(");
                expr(*c.kids[1]);
                out_.text(")");
                return;
            }
            if (callee.text == "Path" && named(c)) {  // a vector's path (Compose's Path(pathData = …) in rememberVectorPainter { })
                out_.text("::kompoz::vector::Path");
                return arguments(c, calleeName, false);
            }
            if (module_.structs && !c.lambda && c.kids.size() > 1 && capitalized(callee.text) && !kKtFunctions.contains(callee.text))
                if (const auto* fields = module_.structs->find(callee.text); fields && structCall(c, callee.text, *fields)) return;
            // topAppBarColors(containerColor = …): a colors object called without its XDefaults (Kotlin's
            // import of TopAppBarDefaults.topAppBarColors), when the name is one struct's alone. (Only a
            // colors object's: C++'s own aliases, value_type, aren't Kotlin's.)
            if (module_.structs && !c.lambda && !capitalized(callee.text) && callee.text.ends_with("Colors"))
                if (auto a = module_.structs->aliases.find(callee.text); a != module_.structs->aliases.end() && a->second.size() == 1)
                    if (const auto* fields = module_.structs->find(*a->second.begin()); fields && structCall(c, *a->second.begin(), *fields)) return;
            if (kKtFunctions.contains(callee.text) || ((suspend_ || awaitedTypes_) && callee.text == "delay")) {
                out_.text("::kompoz::kt::" + callee.text);
                static const std::set<std::string, std::less<>> collections = {"listOf", "mutableListOf", "arrayListOf", "setOf", "mutableSetOf"};
                if (c.typeArgs.empty() && c.kids.size() > 1 && expected && expected->args.size() == 1 && collections.contains(callee.text))
                    out_.text("<" + type(expected->args[0]) + ">");  // (val tags: List<Titled> = listOf(Tag("a")): of the type wanted)
                typeArguments(c);  // listOf<String>()
                return arguments(c, calleeName, false);
            }
        }
        if (callee.kind == Node::Member) {
            const Node& obj = companionless(*callee.kids[0]);
            if (callee.flag) {  // a?.f(x): f called only when a isn't null
                out_.text("::kompoz::kt::safe(");
                expr(obj);
                out_.text(", " + capture("[&]") + "(auto&& kompoz_safe) { return ");
                Scoped inside(global_);
                if (kKtMembers.contains(callee.text)) {
                    out_.text("::kompoz::kt::" + callee.text + "(kompoz_safe");
                    arguments(c, calleeName, true, true);
                } else {  // (a?.hasRoute<T>(): the template's arguments, on a dependent object)
                    out_.text("kompoz_safe." + std::string(c.typeArgs.empty() ? "" : "template ") + cppName(callee.text));
                    typeArguments(c);
                    arguments(c, calleeName, false);
                }
                out_.text("; })");
                return;
            }
            if (auto f = module_.functions.find(callee.text); f != module_.functions.end() && f->second->receiver && !qualifier(obj))
                return moduleCall(c, *f->second, &obj);  // an extension: x.card() is card(x)
            if (obj.kind == Node::Name && obj.text == "Modifier" && !lookup("Modifier")) {
                // Modifier.animateWidth(source) in a ButtonGroup's block: the scope's, as Compose's extension is, where
                // Modifier has no member of the name.
                if (module_.structs && !module_.structs->member("Modifier", callee.text))
                    if (size_t r = receiverOf(callee.text)) {
                        out_.text("(*kompoz_this_" + std::to_string(r) + ")." + cppName(callee.text));
                        return arguments(c, calleeName, false);
                    }
                out_.text("::kompoz::Modifier()." + cppName(callee.text));
                return arguments(c, calleeName, false);
            }
            if (callee.text == "copy" && dataClassCopy(c, obj)) return;
            if (qualifier(obj))  // UiState.Ready(items): a sealed class's nested data class, made as one is
                if (auto k = module_.classes.find(dotted(callee)); k != module_.classes.end())
                    return construct(c, *k->second, qualified(dotted(obj)) + "::" + k->second->name);
            if (module_.structs && !c.lambda && c.kids.size() > 1 && qualifier(obj)) {  // TextFieldDefaults.colors(…)
                std::string name = qualified(dotted(callee));
                const auto* fields = module_.structs->find(name);  // (its own namespace's alias first: ListItemDefaults::colors)
                if (!fields) fields = module_.structs->find(callee.text);
                if (fields && structCall(c, name, *fields)) return;
            }
            if (callee.text == "apply" && c.lambda && c.kids.size() == 1 && !qualifier(obj)) {
                // X(…).apply { … }: the block with the object as its receiver (Kotlin's this), the object given back; the
                // receiver's type is the constructor's (what the block's names are looked up in).
                const Node* made = &obj;
                while (made->kind == Node::Paren) made = made->kids[0].get();
                std::string type;
                if (made->kind == Node::Call && made->kids[0]->kind == Node::Name && capitalized(made->kids[0]->text)) type = "auto " + made->kids[0]->text;
                if (type.empty()) throw CompileError(c.pos, "apply { }: the receiver's type must show, as X(…).apply { … }");
                out_.text("::kompoz::kt::apply(");
                expr(obj);
                out_.text(", ");
                lambda(*c.lambda, false, false, false, &type);
                out_.text(")");
                return;
            }
            if (kKtMembers.contains(callee.text) && !qualifier(obj)) {
                out_.text("::kompoz::kt::" + callee.text + "(");
                expr(obj);
                return arguments(c, calleeName, true, true);
            }
            if (kFreeMembers.contains(callee.text) && !qualifier(obj)) {
                out_.text("::kompoz::" + callee.text + "(");
                expr(obj);
                return arguments(c, calleeName, true, true);
            }
        }
        if (callee.kind == Node::Member && !callee.flag && !qualifier(companionless(*callee.kids[0])) && callee.text != "Companion") {
            // A member called: obj.f(…), not a property (obj may be a pointer to the object: a viewModel's).
            // In a coroutine, Compose's suspending forms where Kompoz's C++ has a separate one.
            std::string member = suspend_ && callee.text == "showSnackbar" ? "showSnackbarAsync" : cppName(callee.text);
            if (!c.typeArgs.empty()) {
                // e.toRoute<Profile>(): the object's type may depend on a lambda's parameter, so the call is
                // made where it is dependent for sure (.template is right there, wrong elsewhere).
                out_.text(capture("[&]") + "(auto&& kompoz_o) -> decltype(auto) { return kompoz_o.template " + member);
                typeArguments(c);
                arguments(c, calleeName, false);
                out_.text("; }(::kompoz::kt::deref(");
                expr(companionless(*callee.kids[0]));
                out_.text("))");
                return;
            }
            out_.text("::kompoz::kt::deref(");
            expr(companionless(*callee.kids[0]));
            out_.text(")." + member);
        } else {
            const Node* wasCallee = std::exchange(calleeNode_, &callee);  // (Arrangement.spacedBy(8.dp): a static function called, not read)
            expr(callee);
            calleeNode_ = wasCallee;
        }
        typeArguments(c);
        arguments(c, calleeName, false);
    }

    void typeArguments(const Node& c) {
        if (c.typeArgs.empty()) return;
        out_.text("<");
        for (size_t i = 0; i < c.typeArgs.size(); ++i) out_.text((i ? ", " : "") + plainType(c.typeArgs[i]));
        out_.text(">");
    }

    // (args): as written, named ones as kompoz's named arguments, the trailing lambda last.
    // `positional`: names dropped (kotlin.hpp's functions take their arguments by position);
    // `after`: the parenthesis is open and the first argument written.
    static bool named(const Node& c) {
        return std::any_of(c.names.begin(), c.names.end(), [](const std::string& n) { return !n.empty(); });
    }

    void arguments(const Node& c, const std::string& calleeName, bool positional, bool after = false) {
        if (!after) out_.text("(");
        bool any = after;
        auto sp = kStructParameters.find(calleeName);
        const std::vector<std::string>* fields =
            sp != kStructParameters.end() && module_.structs && !positional && named(c) ? module_.structs->find(sp->second) : nullptr;
        if (fields && structCall(c, "::kompoz::" + sp->second, *fields, any ? ", " : "")) {
            any = true;
        } else {
            for (size_t i = 0; i < c.kids.size() - 1; ++i) {
                if (any) out_.text(", ");
                any = true;
                const std::string& n = c.names[i];
                if (!n.empty() && !positional) out_.text("::kompoz::arg::" + n + " = ");
                auto receiver = kReceiverArguments.find(calleeName + "." + n);
                if (c.kids[i + 1]->kind == Node::Lambda && kValueArguments.contains(n) && receiver == kReceiverArguments.end()) lambda(*c.kids[i + 1], true);
                else if (c.kids[i + 1]->kind == Node::Lambda && receiver != kReceiverArguments.end())
                    lambda(*c.kids[i + 1], kValueArguments.contains(n), false, false, &receiver->second, calleeName);
                else expr(*c.kids[i + 1]);
            }
        }
        if (c.lambda) {
            if (any) out_.text(", ");
            auto receiver = kReceiverLambdas.find(calleeName);
            const std::string* type = receiver != kReceiverLambdas.end() ? &receiver->second : nullptr;
            // layout: Modifier.layout { measurable, constraints -> … } measures (its value the size),
            // layout(width, height) { … } in it places.
            static const std::string measureScope = "MeasureScope", placementScope = "PlacementScope";
            // (Layout(content) { measurables, constraints -> } measures as Compose's measure policy does)
            bool measures = (calleeName == "layout" || calleeName == "Layout") && c.lambda->flag && !c.lambda->names.empty();
            if (calleeName == "layout" || measures) type = measures ? &measureScope : &placementScope;
            inline_ = kInlineLambdas.contains(calleeName);
            taskBlock_ = calleeName == "async";  // (its block gives the task a suspend fun's call is: scope.async { load(id) })
            lambda(*c.lambda, kValueLambdas.contains(calleeName) || measures, kSuspendLambdas.contains(calleeName), calleeName == "produceState",
                   type, calleeName);
        }
        out_.text(")");
    }

    // Name(a = 1, b = "x"): the struct, its fields in their order (designated, defaults left out).
    // (`spelled`: a nested one's name, UiState::Ready)
    void construct(const Node& c, const DataClass& k, const std::string& spelled = {}) {
        if (c.lambda) throw CompileError(c.lambda->pos, k.name + " takes no lambda");
        std::vector<const Node*> given(k.fields.size(), nullptr);
        size_t next = 0;
        for (size_t i = 0; i + 1 < c.kids.size(); ++i) {
            const std::string& n = c.names[i];
            size_t at = next;
            if (!n.empty()) {
                at = k.fields.size();
                for (size_t f = 0; f < k.fields.size(); ++f)
                    if (k.fields[f].name == n) at = f;
                if (at == k.fields.size()) throw CompileError(c.kids[i + 1]->pos, k.name + " has no field named " + n);
            } else {
                next = at + 1;
            }
            if (at >= k.fields.size()) throw CompileError(c.kids[i + 1]->pos, "too many arguments for " + k.name);
            if (given[at]) throw CompileError(c.kids[i + 1]->pos, k.fields[at].name + " is given twice");
            given[at] = c.kids[i + 1].get();
        }
        out_.text((spelled.empty() ? k.name : spelled) + "{");
        bool any = false;
        for (size_t f = 0; f < k.fields.size(); ++f) {
            if (!given[f]) {
                if (!k.fields[f].defaultValue) throw CompileError(c.pos, k.name + " needs " + k.fields[f].name);
                continue;
            }
            out_.text(std::string(any ? ", " : "") + "." + cppName(k.fields[f].name) + " = ");
            any = true;
            expr(*given[f], &k.fields[f].type);
        }
        out_.text("}");
    }

    // DialogProperties(dismissOnClickOutside = false): a C++ struct's designated initializers, in
    // its fields' order. False (nothing written) if a name isn't one of its fields.
    bool structCall(const Node& c, const std::string& name, const std::vector<std::string>& fields, const std::string& prefix = {}) {
        std::vector<const Node*> given(fields.size(), nullptr);
        size_t next = 0;
        for (size_t i = 0; i + 1 < c.kids.size(); ++i) {
            const std::string& n = c.names[i];
            size_t at = next;
            if (!n.empty()) {
                at = std::find(fields.begin(), fields.end(), n) - fields.begin();
                if (at == fields.size()) return false;
            } else {
                next = at + 1;
            }
            if (at >= fields.size() || given[at]) return false;
            given[at] = c.kids[i + 1].get();
        }
        out_.text(prefix + name + "{");
        bool any = false;
        for (size_t f = 0; f < fields.size(); ++f) {
            if (!given[f]) continue;
            out_.text(std::string(any ? ", " : "") + "." + fields[f] + " = ");
            any = true;
            // (a field's lambda returns its last expression: a std::function<bool(…)> wants it, a void one takes it)
            if (given[f]->kind == Node::Lambda) lambda(*given[f], true);
            else expr(*given[f]);
        }
        out_.text("}");
        return true;
    }

    // x.copy(b = 2): a copy with those fields changed, when a data class has them all.
    bool dataClassCopy(const Node& c, const Node& obj) {
        if (c.lambda || c.kids.size() < 2) return false;
        for (size_t i = 1; i < c.kids.size(); ++i)
            if (c.names[i - 1].empty()) return false;
        bool known = std::any_of(module_.classes.begin(), module_.classes.end(), [&](const auto& k) {
            for (size_t i = 1; i < c.kids.size(); ++i)
                if (std::none_of(k.second->fields.begin(), k.second->fields.end(), [&](const Param& f) { return f.name == c.names[i - 1]; }))
                    return false;
            return true;
        });
        if (!known) return false;
        out_.text("::kompoz::kt::copy(");
        expr(obj);
        out_.text(", " + capture("[&]") + "(auto& kompoz_copy) {");
        Scoped inside(global_);
        for (size_t i = 1; i < c.kids.size(); ++i) {
            out_.text(" kompoz_copy." + cppName(c.names[i - 1]) + " = ");
            expr(*c.kids[i]);
            out_.text(";");
        }
        out_.text(" })");
        return true;
    }

    // `receiver`: an extension's, written before it (x.card()); else an extension called by its
    // name alone has the receiver it is called in (this).
    void moduleCall(const Node& c, const Function& f, const Node* receiver = nullptr) {
        if (f.isSuspend && !suspend_ && !awaitedTypes_ && !inTaskBlock_)
            throw CompileError(c.pos, f.name + " is a suspend fun: call it in a coroutine (LaunchedEffect { }, scope.launch { }, another suspend fun)");
        std::vector<const Node*> given(f.params.size(), nullptr);
        size_t next = 0;
        for (size_t i = 0; i + 1 < c.kids.size(); ++i) {
            const std::string& n = c.names[i];
            size_t at = next;
            if (!n.empty()) {
                at = f.params.size();
                for (size_t p = 0; p < f.params.size(); ++p)
                    if (f.params[p].name == n) at = p;
                if (at == f.params.size()) throw CompileError(c.kids[i + 1]->pos, f.name + " has no parameter named " + n);
            } else {
                next = at + 1;
            }
            if (at >= f.params.size()) throw CompileError(c.kids[i + 1]->pos, "too many arguments for " + f.name);
            if (given[at]) throw CompileError(c.kids[i + 1]->pos, n.empty() ? "too many arguments for " + f.name : n + " is given twice");
            given[at] = c.kids[i + 1].get();
        }
        if (c.lambda) {
            if (f.params.empty() || given.back()) throw CompileError(c.lambda->pos, f.name + " takes no trailing lambda here");
            given.back() = c.lambda.get();
        }
        size_t last = given.size();  // the defaults left at the end: C++'s
        while (last > 0 && !given[last - 1] && f.params[last - 1].defaultValue) --last;
        out_.text(qualifierOf(f.name) + cppName(f.name) + "(");
        if (f.receiver) {
            if (receiver) expr(*receiver);
            else if (!receivers_.empty()) out_.text("(*kompoz_this_" + std::to_string(receivers_.size()) + ")");
            else throw CompileError(c.pos, f.name + " is an extension: it needs a receiver");
            if (last > 0) out_.text(", ");
        }
        for (size_t i = 0; i < last; ++i) {
            if (i) out_.text(", ");
            const Param& p = f.params[i];
            const Node* arg = given[i] ? given[i] : p.defaultValue.get();
            if (!arg) throw CompileError(c.pos, f.name + " needs " + p.name);
            if (arg->kind == Node::Lambda) lambda(*arg, p.type.function && p.type.result && p.type.result->name != "Unit");
            else expr(*arg, &p.type);
        }
        out_.text(")");
    }

    // `receiver`: the type of its receiver (Kotlin's T.() -> R, one of Kompoz's), taken first; kompoz_this_N points
    // to it (a pointer, so a lambda inside that keeps it sees the same one, as Kotlin's would).
    void lambda(const Node& l, bool value, bool suspend = false, bool produceState = false, const std::string* receiver = nullptr,
                const std::string& label = {}) {
        if (classTyping_) {
            taskBlock_ = false;
            // A class's delegate's type, at the class's scope: its lambda as a function giving what the lambda
            // does (the property's type, or its one expression's: derivedStateOf { count * 2 }).
            out_.text("::kompoz::kt::returning<");
            if (delegateValue_) {
                out_.text(type(*delegateValue_));
            } else {
                const Node* last = l.kids.size() == 1 ? l.kids[0].get() : nullptr;
                const Node* e = !last ? nullptr : last->kind == Node::ExprStmt ? last->kids[0].get()
                                               : last->kind == Node::Return && !last->kids.empty() ? last->kids[0].get() : nullptr;
                if (!e || l.flag || usesIt(e)) throw CompileError(l.pos, "give the property a type: its delegate's lambda isn't one expression");
                bool wasGlobal = std::exchange(global_, true);
                classTyping_ = false;
                out_.text("std::remove_cvref_t<decltype(");
                expr(*e);
                out_.text(")>");
                classTyping_ = true, global_ = wasGlobal;
            }
            out_.text(">()");
            return;
        }
        bool wasSuspend = std::exchange(suspend_, suspend), wasTyping = std::exchange(awaitedTypes_, false);
        bool wasTaskBlock = std::exchange(inTaskBlock_, std::exchange(taskBlock_, false));
        // A read-only receiver around (a "const X" one: an AnimatedContentScope, a ToggleFloatingActionButtonScope) is
        // kept by a copy where the lambda may outlive the block (a graphicsLayer's reads checkedProgress as it draws):
        // kt::heldReceiver copies what can be copied, the pointer stays for the rest.
        std::string capture = this->capture(std::exchange(inline_, false) ? "[&]" : "[=]");
        if (capture.starts_with("[=")) {
            std::string held;
            for (size_t r = 0; r < receivers_.size(); ++r)
                if (receiverConst_[r]) held += ", kompoz_this_" + std::to_string(r + 1) + " = ::kompoz::kt::heldReceiver(kompoz_this_" + std::to_string(r + 1) + ")";
            capture = capture.substr(0, capture.size() - 1) + held + "]";
        }
        out_.text(capture + "(");
        Scoped inside(global_);
        scopes_.emplace_back();
        std::string self;
        if (receiver) {
            bool isConst = receiver->starts_with("const "), isAuto = receiver->starts_with("auto ");
            std::string bare = isConst ? receiver->substr(6) : isAuto ? receiver->substr(5) : *receiver;
            receivers_.push_back(bare);
            receiverConst_.push_back(isConst);
            receiverLabels_.push_back(label);
            self = std::to_string(receivers_.size());
            // (typed: an overload taking other parameters is never tried with it; "auto": a template's scope)
            std::string type = isAuto ? "auto&&" : (isConst ? "const ::kompoz::" + bare : "::kompoz::" + bare) + "&";
            out_.text(type + " kompoz_receiver_" + self + std::string(l.flag && !l.names.empty() ? ", " : ""));
        }
        auto named = [&] {  // the parameters it names
            for (size_t i = 0; i < l.names.size(); ++i) {
                if (l.names[i] == "_") {  // (unused, as Kotlin's)
                    out_.text((i ? ", " : "") + std::string("auto&&"));
                    continue;
                }
                out_.text((i ? ", " : "") + std::string("auto&& ") + cppName(l.names[i]));
                declare(l.names[i], Local);
            }
        };
        bool itUsed = std::any_of(l.kids.begin(), l.kids.end(), [](const NodeP& k) { return usesIt(k.get()); });
        if (suspend) {
            // A coroutine (Kotlin's suspend lambda): its CoroutineScope receiver has no name here;
            // produceState's gives it the state, `value` (a copy: a coroutine's parameters outlive
            // their call); flow's gives it the collector, `emit`; consumeEach's and collect's take
            // the element they name (or `it`).
            if (produceState) {
                out_.text("auto value");
                declare("value", Delegated);
            } else if (label == "flow") {
                out_.text("auto emit");
                declare("emit", Local);
            } else if (l.flag) {
                named();
            } else if (!receiver && itUsed) {
                out_.text("auto&& it");
                declare("it", Local);
            } else if (!receiver) {
                out_.text("auto&&...");  // (collect { } given the element it doesn't use; LaunchedEffect { } given nothing)
            }
        } else if (l.flag) {
            named();
        } else if (receiver) {
            // (its `it` is a lambda's around it)
        } else if (itUsed) {
            out_.text("auto&& it");  // (update { it.add(x) } changes what it is given)
            declare("it", Local);
        } else {
            out_.text("auto&&...");  // { ... } where the type has parameters it doesn't use
        }
        out_.text(suspend ? ") -> ::kompoz::Task {" : ") {");
        if (receiver) out_.text(" [[maybe_unused]] auto* kompoz_this_" + self + " = &kompoz_receiver_" + self + ";");
        lambdaLabels_.push_back(label);
        for (size_t i = 0; i < l.kids.size(); ++i) statement(*l.kids[i], value && i + 1 == l.kids.size());
        lambdaLabels_.pop_back();
        if (suspend) out_.text(" co_return;");
        scopes_.pop_back();
        if (receiver) {
            receivers_.pop_back();
            receiverConst_.pop_back();
            receiverLabels_.pop_back();
        }
        out_.text(" }");
        suspend_ = wasSuspend;
        awaitedTypes_ = wasTyping;
        inTaskBlock_ = wasTaskBlock;
    }

    // The names a node uses (to find the locals an object's functions reach).
    static void usedNames(const Node* n, std::set<std::string, std::less<>>& out) {
        if (!n) return;
        if (n->kind == Node::Name) out.insert(n->text);
        for (auto& k : n->kids) usedNames(k.get(), out);
        usedNames(n->lambda.get(), out);
        for (auto& b : n->branches) {
            for (auto& c : b.conds) usedNames(c.second.get(), out);
            usedNames(b.body.get(), out);
        }
    }

    // object : NestedScrollConnection { override fun … }: a class of its own deriving from the type,
    // made where it stands, its functions the overrides; the locals they use are copied into it,
    // as a lambda's captures are. A shared_ptr to it, as Kompoz takes such objects.
    // object : Painter() { override val intrinsicSize = Size(…); override fun DrawScope.onDraw() { … } }: Kompoz's Painter
    // isn't derived from; it is Painter::custom(onDraw, intrinsicSize), the drawing a lambda in the DrawScope.
    void painterObject(const Node& o) {
        const Node* size = nullptr;
        const Node* draw = nullptr;
        for (auto& m : o.kids) {
            if (m->flag && m->text == "intrinsicSize") size = m.get();
            else if (!m->flag && m->text == "onDraw" && m->names.empty() && m->kids.size() == 2 && m->kids[1]->text == "DrawScope") draw = m.get();
            else throw CompileError(m->pos, "a Painter object has override val intrinsicSize and override fun DrawScope.onDraw() only");
        }
        if (!draw) throw CompileError(o.pos, "a Painter object needs override fun DrawScope.onDraw()");
        out_.text("::kompoz::Painter::custom(" + capture("[=]") + "(");
        {
            Scoped inside(global_);
            scopes_.emplace_back();
            receivers_.push_back("DrawScope");
            receiverConst_.push_back(false);
            receiverLabels_.push_back({});
            std::string self = std::to_string(receivers_.size());
            bool wasSuspend = std::exchange(suspend_, false);
            out_.text("::kompoz::DrawScope& kompoz_receiver_" + self + ") { [[maybe_unused]] auto* kompoz_this_" + self + " = &kompoz_receiver_" + self + ";");
            auto lambdaLabels = std::exchange(lambdaLabels_, {});
            statements(*draw->kids[0], false);
            lambdaLabels_ = std::move(lambdaLabels);
            out_.text(" }");
            suspend_ = wasSuspend;
            scopes_.pop_back();
            receivers_.pop_back();
            receiverConst_.pop_back();
            receiverLabels_.pop_back();
        }
        if (size) {
            out_.text(", ");
            const Node& body = *size->kids[0];
            if (body.kids.size() == 1 && body.kids[0]->kind == Node::Return && !body.kids[0]->kids.empty()) {
                expr(*body.kids[0]->kids[0]);
            } else {
                out_.text(capture("[&]") + "() {");
                Scoped inside(global_);
                statements(body, false);
                out_.text(" }()");
            }
        }
        out_.text(")");
    }

    void objectExpression(const Node& o) {
        if (std::string base = plainType(*o.type); base == "::kompoz::Painter" || base == "Painter") return painterObject(o);
        // object : Named { override val name = …; override fun f() = … }: one of a Kotlin interface's, its handle given
        const InterfaceDecl* iface = interfaceNamed(o.type->name);
        InterfaceMembers members = iface ? interfaceMembers({*o.type}) : InterfaceMembers();
        for (auto& m : o.kids) {
            if (m->flag && !iface) throw CompileError(m->pos, "an object's property (override val) here: a Painter's intrinsicSize only; the rest are functions");
            if (m->kids.size() > 1) throw CompileError(m->pos, "an object's function with a receiver here: a Painter's DrawScope.onDraw() only");
            if (iface) {
                const InterfaceMember* im = memberNamed(members, m->text);
                if (!im || (m->flag ? !im->property : !im->method)) throw CompileError(m->pos, m->text + " overrides nothing of " + iface->name);
            }
        }
        for (auto& [n, im] : members)
            if (!im.hasBody && std::none_of(o.kids.begin(), o.kids.end(), [&](const NodeP& m) { return m->text == n; }))
                throw CompileError(o.pos, "the object doesn't implement " + im.in->name + "." + n);
        std::set<std::string, std::less<>> used;
        for (auto& m : o.kids) usedNames(m.get(), used);
        std::vector<std::pair<std::string, VarKind>> captured;
        for (const std::string& n : used)
            if (const VarKind* k = lookup(n); k && (*k == Local || *k == Delegated || *k == Callable)) captured.emplace_back(n, *k);
        out_.text(capture("[=]") + "() {");
        // (each held one's type named outside the class: inside, the name is the member's)
        for (auto& [n, k] : captured) out_.text(" using kompoz_type_" + n + " = std::remove_cvref_t<decltype(" + cppName(n) + ")>;");
        out_.text(" struct kompoz_object : " + (iface ? "public virtual " + objectOf(*iface) : plainType(*o.type)) + " {");
        for (auto& [n, k] : captured) out_.text(" kompoz_type_" + n + " " + cppName(n) + ";");
        if (!captured.empty()) {
            out_.text(" kompoz_object(");
            for (size_t i = 0; i < captured.size(); ++i)
                out_.text((i ? ", " : "") + std::string("kompoz_type_") + captured[i].first + " kompoz_" + captured[i].first);
            out_.text(") : ");
            for (size_t i = 0; i < captured.size(); ++i)
                out_.text((i ? ", " : "") + cppName(captured[i].first) + "(std::move(kompoz_" + captured[i].first + "))");
            out_.text(" {}");
        }
        // Its functions see only what it holds and their parameters (and `this` is the object).
        auto scopes = std::exchange(scopes_, {});
        auto receivers = std::exchange(receivers_, {});
        auto receiverConst = std::exchange(receiverConst_, {});
        auto receiverLabels = std::exchange(receiverLabels_, {});
        auto lambdaLabels = std::exchange(lambdaLabels_, {});
        bool wasSuspend = std::exchange(suspend_, false), wasGlobal = std::exchange(global_, false);
        for (auto& m : o.kids) {
            out_.at(m->pos);
            out_.text(" " + (iface ? std::string("auto") : m->type ? type(*m->type) : std::string("void")) + " " + cppName(m->text) + "(");
            scopes_.assign(1, {});
            for (auto& [n, im] : members)  // (the interface's properties, read through its getters)
                if (im.property) declare(n, Accessor);
            for (auto& [n, k] : captured) declare(n, k);
            for (size_t i = 0; i < m->names.size(); ++i) {
                out_.text((i ? ", " : "") + (iface ? paramType(m->typeArgs[i]) : type(m->typeArgs[i])) + " " + cppName(m->names[i]));
                declare(m->names[i], Local);
            }
            if (iface) out_.text(") const -> " + memberType(objectOf(*memberNamed(members, m->text)->in), *memberNamed(members, m->text)) + " override {");
            else out_.text(") override {");
            statements(*m->kids[0], false);
            out_.text(" }");
        }
        scopes_ = std::move(scopes);
        receivers_ = std::move(receivers);
        receiverConst_ = std::move(receiverConst);
        receiverLabels_ = std::move(receiverLabels);
        lambdaLabels_ = std::move(lambdaLabels);
        suspend_ = wasSuspend, global_ = wasGlobal;
        out_.text(" }; return " + (iface ? qualified(iface->name) + "(" : std::string()) + "std::make_shared<kompoz_object>(");
        for (size_t i = 0; i < captured.size(); ++i) out_.text((i ? ", " : "") + cppName(captured[i].first));
        out_.text(std::string(iface ? ")" : "") + "); }()");
    }

    // a[i] (a[i, j]) as C++ writes it: the element itself, to assign to or count up.
    void indexed(const Node& e) {
        expr(*e.kids[0]);
        out_.text("[");
        for (size_t i = 1; i < e.kids.size(); ++i) {
            if (i > 1) out_.text(", ");
            expr(*e.kids[i]);
        }
        out_.text("]");
    }

    void unary(const Node& u) {
        const Node& x = *u.kids[0];
        if (u.text == "!!") {
            out_.text("::kompoz::kt::notNull(");
            expr(x);
            out_.text(")");
        } else if ((u.text == "++" || u.text == "--") && delegatedName(x)) {
            // nextId++ as a value on a state: the state set, the old (postfix) or new (prefix) value given.
            out_.text(std::string("::kompoz::kt::") + (u.flag ? "post" : "pre") + (u.text == "++" ? "Increment(" : "Decrement(") + held(x.text) + ")");
        } else if ((u.text == "++" || u.text == "--") && accessorName(x)) {  // the same through a property's setter
            std::string get = cppName(x.text) + "()", set = setterOf(x.text);
            out_.text(capture("[&]") + "() { auto kompoz_old = " + get + "; " + set + "(kompoz_old " + u.text[0] + " 1); return " +
                      (u.flag ? std::string("kompoz_old") : get) + "; }()");
        } else if (u.flag) {
            if (x.kind == Node::Index) indexed(x);  // counts[k]++: the element itself
            else expr(x);
            out_.text(u.text);
        } else {
            out_.text(u.text);
            if ((u.text == "++" || u.text == "--") && x.kind == Node::Index) indexed(x);
            else expr(x);
        }
    }

    void binary(const Node& b) {
        const std::string& op = b.text;
        const Node& l = *b.kids[0];
        const Node& r = *b.kids[1];
        auto helper = [&](const char* fn, const Node& first, const Node& second) {
            out_.text(std::string("::kompoz::kt::") + fn + "(");
            expr(first);
            out_.text(", ");
            expr(second);
            out_.text(")");
        };
        if (op == "..") return helper("rangeTo", l, r);
        if (op == "..<" || op == "until") return helper("until", l, r);
        if (op == "downTo") return helper("downTo", l, r);
        if (op == "step") return helper("step", l, r);
        if (op == "in") return helper("contains", r, l);
        if (op == "!in") {
            out_.text("!");
            return helper("contains", r, l);
        }
        if (op == "to") return helper("to", l, r);
        // (enter togetherWith exit) using SizeTransform(clip = false); 0.4f at 75 using FastOutSlowInEasing
        if (op == "using") return helper("infixUsing", l, r);
        static const std::map<std::string, std::string, std::less<>> bitwise = {{"and", "&"}, {"or", "|"}, {"xor", "^"}, {"shl", "<<"}, {"shr", ">>"}};
        if (auto bit = bitwise.find(op); bit != bitwise.end()) {  // Kotlin's bitwise infix functions on integers
            out_.text("(");
            expr(l);
            out_.text(" " + bit->second + " ");
            expr(r);
            out_.text(")");
            return;
        }
        if (op == "?:") {
            out_.text("::kompoz::kt::elvis(");
            expr(l);
            out_.text(", " + capture("[&]") + " { return ");
            Scoped inside(global_);
            expr(r);
            out_.text("; })");
            return;
        }
        if ((op == "==" || op == "!=" || op == "===" || op == "!==") && (l.kind == Node::Null || r.kind == Node::Null)) {
            out_.text(op[0] == '!' ? "!::kompoz::kt::isNull(" : "::kompoz::kt::isNull(");
            expr(l.kind == Node::Null ? r : l);
            out_.text(")");
            return;
        }
        static const std::set<std::string, std::less<>> plain = {"+", "-", "*", "/", "%", "<", ">", "<=", ">=", "==", "!=", "&&", "||"};
        if (plain.contains(op) || op == "===" || op == "!==") {
            expr(l);
            out_.text(" " + (op == "===" ? std::string("==") : op == "!==" ? std::string("!=") : op) + " ");
            expr(r);
            return;
        }
        // An infix function of the receiver's, with both sides (DragValue.Start at 0f in DraggableAnchors { }).
        if (size_t rcv = std::isalpha(static_cast<unsigned char>(op[0])) ? receiverOf(op) : 0) {
            out_.text("(*kompoz_this_" + std::to_string(rcv) + ")." + cppName(op) + "(");
            expr(l);
            out_.text(", ");
            expr(r);
            out_.text(")");
            return;
        }
        // An infix function: a.name(b).
        expr(l);
        out_.text("." + cppName(op) + "(");
        expr(r);
        out_.text(")");
    }

    // if as a value: a ? b : c where both branches are one expression, else run in place.
    void ifExpression(const Node& n, const Type* expected = nullptr) {
        auto single = [](const Node* b) { return b && b->kids.size() == 1 && b->kids[0]->kind == Node::ExprStmt && b->kids[0]->kids[0]->kind != Node::If && b->kids[0]->kids[0]->kind != Node::When; };
        if (n.kids.size() > 2 && single(n.kids[1].get()) && single(n.kids[2].get())) {
            // if (c) { { … } } else null: a lambda or none (kt::nullableLambda: the function type it goes to, empty for none).
            const Node& then = *n.kids[1]->kids[0]->kids[0];
            const Node& otherwise = *n.kids[2]->kids[0]->kids[0];
            if ((then.kind == Node::Lambda && otherwise.kind == Node::Null) || (then.kind == Node::Null && otherwise.kind == Node::Lambda)) {
                out_.text(std::string("::kompoz::kt::nullableLambda(") + (then.kind == Node::Null ? "!(" : "("));
                expr(*n.kids[0]);
                out_.text("), ");
                lambda(then.kind == Node::Lambda ? then : otherwise, false);
                out_.text(")");
                return;
            }
            if (then.kind == Node::Null || otherwise.kind == Node::Null) {  // if (c) x else null: a nullable of x (kt::nullable, x only then)
                out_.text(std::string("::kompoz::kt::nullable(") + (then.kind == Node::Null ? "!(" : "("));
                expr(*n.kids[0]);
                out_.text("), " + capture("[&]") + "() { return ");
                expr(then.kind == Node::Null ? otherwise : then);
                out_.text("; })");
                return;
            }
            out_.text("(");
            std::vector<const Node*> casts;
            castCondition(*n.kids[0], casts);
            out_.text(" ? ");
            if (casts.empty()) {
                expr(*n.kids[1]->kids[0]->kids[0]);
            } else {  // (the smart casts' view: the value made where they are)
                out_.text(capture("[&]") + "() {");
                scopes_.emplace_back();
                smartCasts(casts);
                out_.text(" return ");
                expr(*n.kids[1]->kids[0]->kids[0]);
                out_.text("; }()");
                scopes_.pop_back();
            }
            out_.text(" : ");
            expr(*n.kids[2]->kids[0]->kids[0]);
            out_.text(")");
            return;
        }
        if (n.kids.size() < 3) throw CompileError(n.pos, "an if used as a value needs an else");
        // (a declared type is the lambda's: `val x: T? = if (…) a else if (…) b else null` returns a T? in every branch)
        out_.text(capture("[&]") + "()" + (expected ? " -> " + type(*expected) : std::string()) + " {");
        Scoped inside(global_);
        ifStatement(n, true);
        out_.text(" }()");
    }

    Output& out_;
    const Module& module_;
    bool suspend_ = false;  // in a coroutine
    std::vector<std::string> receivers_;  // the receivers' types of the lambdas it is in, innermost last
    std::vector<bool> receiverConst_;     // and whether each is read-only (a "const X" receiver: copied into lambdas that keep it)
    const Node* calleeNode_ = nullptr;    // the callee being written, when a member: called, not read as a property
    int smart_ = 0;                            // smart casts' held views, numbered
    std::vector<std::string> lambdaLabels_;    // the lambdas being written, each its call's name (return@LaunchedEffect)
    std::vector<std::string> receiverLabels_;  // and the calls they were given to (this@AnimatedContent)
    bool global_ = false;  // at namespace scope: a top-level val's value
    bool inClass_ = false;  // in a class's body: its members are reached by their names
    const Type* currentResult_ = nullptr;  // the function being defined's result type (flow { } takes its T from Flow<T>)
    bool inline_ = false;  // the lambda written next is an inline function's (kInlineLambdas)
    bool awaitedTypes_ = false;  // in a suspend fun's result type (decltype): calls give what they give once awaited
    bool taskBlock_ = false, inTaskBlock_ = false;  // the lambda written next is async's (it gives a task), and in it
    bool classTyping_ = false;   // in a class's delegated property's type (decltype at the class's scope)
    const Type* delegateValue_ = nullptr;  // and that property's type, written
    const Type* expectedCall_ = nullptr;  // what the call written next is expected to give (val tags: List<Titled> = listOf(…))
    std::string fieldOf_;        // in a property's accessor: what `field` is
    std::string interfaceSelf_;  // in an interface's object: its name (Object)
    std::set<std::string, std::less<>> interfaceMethods_;  // and its functions, its interfaces' too
    std::string selfPrefix_;     // in a type written there: how its members are reached
    std::vector<std::map<std::string, VarKind, std::less<>>> scopes_;
};

}  // namespace

void generateEnum(Output& out, const EnumClass& e) {
    out.at(e.pos);
    out.text("enum class " + e.name + " {");
    for (size_t i = 0; i < e.entries.size(); ++i) out.text(std::string(i ? ", " : " ") + e.entries[i]);
    out.text(" };\n");
}

void generateObject(Output& out, const ObjectDecl& o, const Module& module) {
    if (o.hasMembers() || std::any_of(o.supers.begin(), o.supers.end(), [&](const Type& t) { return module.interfaces.contains(t.name); })) {
        Generator g(out, module);
        g.objectDefinition(o);
        return;
    }
    out.at(o.pos);
    bool route = std::find(o.annotations.begin(), o.annotations.end(), "Serializable") != o.annotations.end();
    out.text("struct " + o.name + " {" + (route ? " KOMPOZ_ROUTE(" + o.name + ")" : std::string()) + " bool operator==(const " + o.name +
             "&) const = default; template <class = void> std::string toString() const { return \"" + o.name + "\"; } };\n");
}

void generateSealed(Output& out, const SealedDecl& s, const Module& module) {
    Generator g(out, module);
    g.sealedDefinition(s);
}

void generateClass(Output& out, const DataClass& c, const Module& module) {
    Generator g(out, module);
    g.classDefinition(c);
}

void generatePrototype(Output& out, const Function& f, const Module& module) {
    Generator g(out, module);
    g.signature(f, true);
    out.text(";\n");
}

void generateProperty(Output& out, const Property& p, const Module& module) {
    Generator g(out, module);
    g.property(p);
}

void generateInterface(Output& out, const InterfaceDecl& i, const Module& module) {
    Generator g(out, module);
    g.interfaceDefinition(i);
}

void declareInterface(Output& out, const InterfaceDecl& i) {
    out.at(i.pos);
    out.text("class " + i.name + ";\n");
}

void generateClassDecl(Output& out, const ClassDecl& c, const Module& module) {
    Generator g(out, module);
    g.classDeclDefinition(c);
}

void generateDefinition(Output& out, const Function& f, const Module& module) {
    Generator g(out, module);
    g.definition(f);
}

}  // namespace kompozc
