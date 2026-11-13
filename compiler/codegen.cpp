#include "codegen.hpp"

#include <algorithm>
#include <cctype>
#include <deque>
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
    "maxOfOrNull", "minOfOrNull", "check", "require", "joinToString",
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
    // (catch (e: Exception) catches C++'s exceptions too; the others are Kotlin's, kt's: what kt's functions throw)
    {"Exception", "std::exception"}, {"Throwable", "std::exception"}, {"Error", "::kompoz::kt::Error"},
    {"NotImplementedError", "::kompoz::kt::NotImplementedError"}, {"RuntimeException", "::kompoz::kt::RuntimeException"},
    {"IllegalStateException", "::kompoz::kt::IllegalStateException"}, {"IllegalArgumentException", "::kompoz::kt::IllegalArgumentException"},
    {"NumberFormatException", "::kompoz::kt::NumberFormatException"}, {"IndexOutOfBoundsException", "::kompoz::kt::IndexOutOfBoundsException"},
    {"NoSuchElementException", "::kompoz::kt::NoSuchElementException"}, {"ArithmeticException", "::kompoz::kt::ArithmeticException"},
    {"UnsupportedOperationException", "::kompoz::kt::UnsupportedOperationException"},
    {"NullPointerException", "::kompoz::kt::NullPointerException"}, {"ClassCastException", "::kompoz::kt::ClassCastException"},
    {"UninitializedPropertyAccessException", "::kompoz::kt::UninitializedPropertyAccessException"},
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
    "substringBefore", "substringAfter", "substringBeforeLast", "substringAfterLast",
    "take", "drop", "reversed", "sorted", "sortedBy", "any", "all", "none", "count", "sum", "sumOf", "maxOf", "minOf", "maxOfOrNull", "minOfOrNull", "indexOf",
    "getOrNull", "let", "also", "takeIf", "withIndex", "toList", "toMutableList", "distinct", "split", "replace",
    "coerceIn", "coerceAtLeast", "coerceAtMost", "roundToInt", "toPx", "roundToPx", "toDp", "find", "add", "addAll", "removeAt", "remove", "repeat",
    "format", "indexOfFirst", "indexOfLast", "removeIf", "removeAll", "flatMap", "sortedByDescending", "maxByOrNull", "minByOrNull",
    "mapNotNull", "filterIndexed", "distinctBy", "takeUnless", "groupBy", "associateBy", "associateWith", "chunked", "partition",
    "ifEmpty", "ifBlank", "shuffle", "isDigit", "isLetter", "isLetterOrDigit", "isWhitespace", "isUpperCase", "isLowerCase"};

// Kotlin's members that change nothing (what else a value's member call does may be anything: next(), add(x)).
const std::set<std::string, std::less<>> kPureMembers = {
    "map", "mapIndexed", "filter", "filterNot", "isEmpty", "isNotEmpty", "isBlank", "isNotBlank", "trim", "trimIndent",
    "uppercase", "lowercase", "toInt", "toFloat", "toDouble", "toLong", "toString", "first", "last", "firstOrNull",
    "lastOrNull", "joinToString", "startsWith", "endsWith", "contains", "substring", "substringBefore", "substringAfter",
    "substringBeforeLast", "substringAfterLast", "take", "drop", "reversed", "sorted",
    "sortedBy", "any", "all", "none", "count", "sum", "sumOf", "maxOf", "minOf", "maxOfOrNull", "minOfOrNull", "indexOf",
    "getOrNull", "getOrElse", "takeIf", "takeUnless", "withIndex", "toList", "toSet", "distinct", "split", "replace", "coerceIn",
    "coerceAtLeast", "coerceAtMost", "roundToInt", "toPx", "roundToPx", "toDp", "find", "format", "indexOfFirst", "indexOfLast",
    "flatMap", "sortedByDescending", "maxByOrNull", "minByOrNull", "mapNotNull", "filterIndexed", "distinctBy", "groupBy",
    "associateBy", "associateWith", "chunked", "partition", "ifEmpty", "ifBlank", "isDigit", "isLetter", "isLetterOrDigit",
    "isWhitespace", "isUpperCase", "isLowerCase", "copy", "padStart", "padEnd", "plus", "minus", "times", "div", "rem", "toChar",
    "code", "equals", "hashCode", "compareTo", "copyOf", "get", "keys", "values", "containsKey", "lerp", "compositeOver"};

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

// The module as a body sees it: its nested declarations also by their own names (Entry inside Catalog is
// Catalog.Entry), over any other of the name.
Module withNested(const Module& module, const std::string& outer, const NestedDecls& n) {
    Module m = module;
    for (const auto& c : n.classes) m.classDecls[c->name] = c.get();
    for (const auto& d : n.dataClasses) m.classes[d->name] = d.get();
    for (const auto& e : n.enums) m.enums[e->name] = e.get();
    for (const auto& o : n.objects) {
        m.objects.insert(o->name);
        m.objectDecls[o->name] = o.get();
    }
    for (const auto& i : n.interfaces) m.interfaces[i->name] = i.get();
    for (const auto& s : n.sealeds) {  // (and its cases: S.Ready, S.Loading)
        m.sealeds[s->name] = s.get();
        for (const DataClass& d : s->dataClasses) m.classes[s->name + "." + d.name] = &d;
        for (const ObjectDecl& o : s->objects) m.objects.insert(s->name + "." + o.name);
    }
    (void)outer;
    return m;
}

class Generator {
public:
    Generator(Output& out, const Module& module) : out_(&out), module_(module) {}

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
        GenericSig generic = genericSig(f);
        out_->at(f.pos);
        out_->text(generic.head);
        if (!f.composable) {
            // A plain function: defined in each file that uses it (inline), its type the body's when
            // it is an expression and none is written; an extension's receiver comes first.
            // A suspend fun is a coroutine: Task, or TaskOf<T> (its expression's when no type is written), its
            // parameters by value (its frame outlives the call).
            std::string result = f.result ? type(*f.result) : f.expressionBody ? "auto" : "void";
            if (f.isSuspend && (f.result || !f.expressionBody)) result = result == "void" ? "::kompoz::Task" : "::kompoz::TaskOf<" + result + ">";
            std::string fnName = f.isExtensionProperty ? "kompoz_get_" + f.name : cppName(f.name);  // (an extension property's getter: a name of its own)
            out_->text(std::string(f.isSuspend ? "[[nodiscard]] " : "") + (f.isPrivate ? "static " : "inline ") + result + " " + fnName + "(");
            if (f.receiver)
                out_->text((f.isSuspend ? type(*f.receiver) : "const " + type(*f.receiver) + "&") + " kompoz_receiver_1" + (f.params.empty() ? "" : ", "));
        } else {
            out_->text((unit ? std::string("void") : type(*f.result)) + " " + f.name + "(");
        }
        // Defaults: those at the end (C++ takes no others), where kompozc's calls leave them out; none of overloads
        // (their calls give each: C++ would take show(x) for show(x, n = 1) too).
        size_t firstDefault = f.params.size();
        while (firstDefault > 0 && f.params[firstDefault - 1].defaultValue && !overloadedName(f)) --firstDefault;
        for (size_t i = 0; i < f.params.size(); ++i) {
            const Param& p = f.params[i];
            out_->text((i ? ", " : "") + genericParamType(generic, i, p, f.isSuspend) + " " + cppName(p.name));
            if (defaults && i >= firstDefault) {  // (at namespace scope: a lambda there captures nothing)
                out_->text(" = ");
                scopes_.emplace_back();
                bool wasGlobal = std::exchange(global_, true);
                expr(*p.defaultValue, &p.type);
                global_ = wasGlobal;
                scopes_.pop_back();
            }
        }
        if (f.composable)  // (a value-returning one too: a group of its own, as Compose gives every call)
            out_->text(std::string(f.params.empty() ? "" : ", ") + "::kompoz::CallSite site" + (defaults ? " = ::kompoz::CallSite::current()" : ""));
        out_->text(")");
        if (f.isSuspend && !f.result && f.expressionBody) {
            out_->text(" -> ::kompoz::kt::TaskFor<decltype(");
            typing(*f.body->kids[0]->kids[0], true);
            out_->text(")>");
        }
    }

    // ---- Interfaces, accessors, suspend functions ----

    const InterfaceDecl* interfaceNamed(const std::string& name) const {
        auto i = module_.interfaces.find(name);
        return i == module_.interfaces.end() ? nullptr : i->second;
    }
    // The interfaces among `types` (the rest left: the sealed class a data class is a case of) and those they
    // extend, each once, nearest first.
    using Bindings = std::map<std::string, Type, std::less<>>;
    // An interface reached from a type: what its type parameters are there (Source<Int>: T is Int), and the object
    // type an override names its members by (the interface listed, through which a base's are reached too).
    struct InterfaceUse {
        const InterfaceDecl* decl;
        Bindings bind;
        std::string owner;
    };
    std::vector<InterfaceUse> allInterfaces(const std::vector<Type>& types) {
        std::vector<InterfaceUse> out;
        struct Todo {
            const Type* type;
            Bindings bind;
            std::string owner;
        };
        std::vector<Todo> todo;
        for (const Type& t : types) todo.push_back({&t, {}, ""});
        for (size_t i = 0; i < todo.size() && i < 256; ++i) {
            const InterfaceDecl* d = interfaceNamed(todo[i].type->name);
            if (!d || std::any_of(out.begin(), out.end(), [&](const InterfaceUse& u) { return u.decl == d; })) continue;
            Type actual = substitute(*todo[i].type, todo[i].bind);
            Bindings bind;
            for (size_t j = 0; j < d->typeParams.size() && j < actual.args.size(); ++j) bind.emplace(d->typeParams[j], actual.args[j]);
            std::string owner = todo[i].owner.empty() ? plainType(actual) + "::Object" : todo[i].owner;
            for (const Type& b : d->bases) todo.push_back({&b, bind, owner});
            out.push_back({d, std::move(bind), std::move(owner)});
        }
        return out;
    }
    // A type with its type parameters given (T in Source<T>'s fun put(x: T), as an implementer of Source<Int> has it).
    static Type substitute(const Type& t, const Bindings& bind) {
        if (bind.empty()) return t;
        if (!t.function && t.args.empty())
            if (auto b = bind.find(t.name); b != bind.end()) {
                Type r = b->second;
                r.nullable = r.nullable || t.nullable;
                return r;
            }
        Type r = t;
        for (Type& a : r.args) a = substitute(a, bind);
        for (Type& p : r.params) p = substitute(p, bind);
        if (r.result) *r.result = substitute(*r.result, bind);
        if (r.receiver) *r.receiver = substitute(*r.receiver, bind);
        return r;
    }
    // A member of interfaces: the property or function of the name, the interface that declares it first
    // (nearest first), and whether one of them gives it a body (a getter, a function's).
    struct InterfaceMember {
        const InterfaceDecl* in = nullptr;  // (none: a base class's)
        const ClassProperty* property = nullptr;
        const Function* method = nullptr;
        bool hasBody = false;
        std::string owner;  // the C++ type that declares it, as an override's type names it (Name::Object, a class)
        std::string from;   // its interface's or class's name
        std::map<std::string, Type, std::less<>> bind;  // its type parameters, as they are given where it is inherited
        bool overloaded = false;  // a function with others of its name (its type is that of a call taking its parameters)
    };
    // A type of an inherited member, its type parameters given.
    std::string typeIn(const InterfaceMember& m, const Type& t) { return type(substitute(t, m.bind)); }
    std::string paramTypeIn(const InterfaceMember& m, const Type& t) { return paramType(substitute(t, m.bind)); }
    using InterfaceMembers = std::vector<std::pair<std::string, InterfaceMember>>;
    InterfaceMembers interfaceMembers(const std::vector<Type>& types) {
        InterfaceMembers out;
        auto find = [&](const std::string& n) -> InterfaceMember* {
            for (auto& [k, v] : out)
                if (k == n) return &v;
            return nullptr;
        };
        for (const InterfaceUse& u : allInterfaces(types)) {
            const InterfaceDecl* d = u.decl;
            for (const ClassProperty& p : d->properties) {
                InterfaceMember* m = find(p.name);
                if (!m) m = &out.emplace_back(p.name, InterfaceMember{d, &p, nullptr, false, u.owner, d->name, u.bind}).second;
                m->hasBody = m->hasBody || p.getter;
            }
            for (const Function& f : d->methods) {
                InterfaceMember* m = nullptr;
                for (auto& [k, v] : out)
                    if (k == f.name && sameMethod(v, f, u.bind)) m = &v;
                if (!m) m = &out.emplace_back(f.name, InterfaceMember{d, nullptr, &f, false, u.owner, d->name, u.bind}).second;
                m->hasBody = m->hasBody || !f.isAbstract;
            }
        }
        markOverloads(out);
        return out;
    }
    // Whether an inherited function and another (its type parameters given as `bind`) take the same parameters.
    bool sameMethod(const InterfaceMember& m, const Function& f, const Bindings& bind) const {
        if (!m.method || m.method->params.size() != f.params.size()) return false;
        for (size_t i = 0; i < f.params.size(); ++i)
            if (!sameType(substitute(m.method->params[i].type, m.bind), substitute(f.params[i].type, bind))) return false;
        return true;
    }
    static void markOverloads(InterfaceMembers& members) {
        for (auto& [n, m] : members)
            for (auto& [n2, m2] : members)
                if (&m != &m2 && n == n2 && m.method && m2.method) m.overloaded = true;
    }
    // The inherited member an object expression's member overrides: a property by its name, a function by its
    // parameters' types too (its names / typeArgs).
    const InterfaceMember* objectMember(const InterfaceMembers& members, const Node& m) const {
        const InterfaceMember* only = nullptr;  // (the one function of the name: it, as its types may be spelled otherwise)
        int named = 0;
        for (auto& [n, im] : members)
            if (n == m.text && (m.flag ? im.property != nullptr : im.method != nullptr)) only = &im, ++named;
        if (named == 1) return only;
        for (auto& [n, im] : members) {
            if (n != m.text || (m.flag ? !im.property : !im.method)) continue;
            if (m.flag) return &im;
            bool same = im.method->params.size() == m.typeArgs.size();
            for (size_t i = 0; same && i < m.typeArgs.size(); ++i) same = sameType(substitute(im.method->params[i].type, im.bind), m.typeArgs[i]);
            if (same) return &im;
        }
        return nullptr;
    }
    // The inherited function `f` overrides (its name, its parameters), if any.
    const InterfaceMember* overridden(const InterfaceMembers& inherited, const Function& f) const {
        const InterfaceMember* only = nullptr;  // (the one function of the name: it, as its types may be spelled otherwise)
        int named = 0;
        for (auto& [n, m] : inherited)
            if (n == f.name && m.method) only = &m, ++named;
        if (named == 1) return only;
        for (auto& [n, m] : inherited)
            if (n == f.name && overrides(f, m)) return &m;
        return nullptr;
    }
    static const InterfaceMember* memberNamed(const InterfaceMembers& members, const std::string& name) {
        for (auto& [n, m] : members)
            if (n == name) return &m;
        return nullptr;
    }
    std::string objectOf(const InterfaceDecl& d) const { return qualified(d.name) + "::Object"; }
    // A generic function's template (fun <T, R> map(items: List<T>, f: (T) -> R): List<R>): its type parameters that
    // its parameters give (or a call writes) first, in their order; a parameter of a function type naming one taken
    // as any callable (kompoz_F<i>: a lambda's type is its own, C++ takes no T from it into a std::function); then a
    // type parameter that only such a parameter's result is, its default what the callable gives.
    struct GenericSig {
        std::string head;
        std::map<size_t, std::string> callables;  // a parameter's index -> its template parameter
        size_t written = 0;                       // the type parameters a call may write (the first ones)
    };
    static bool mentions(const Type& t, const std::string& name) {
        if (!t.function && t.name == name) return true;
        for (const Type& a : t.args) if (mentions(a, name)) return true;
        for (const Type& p : t.params) if (mentions(p, name)) return true;
        return (t.result && mentions(*t.result, name)) || (t.receiver && mentions(*t.receiver, name));
    }
    GenericSig genericSig(const Function& f) {
        GenericSig g;
        if (f.typeParams.empty()) return g;
        std::vector<std::string> front;
        std::vector<std::pair<std::string, size_t>> back;  // (a type parameter, the parameter whose result it is)
        for (const std::string& tp : f.typeParams) {
            bool given = f.receiver && mentions(*f.receiver, tp);
            for (const Param& p : f.params) given = given || (!p.type.function && mentions(p.type, tp));
            size_t fn = f.params.size();
            for (size_t i = 0; i < f.params.size() && !given; ++i) {
                const Type& t = f.params[i].type;
                if (t.function && t.result && !t.result->function && t.result->args.empty() && t.result->name == tp) fn = i;
            }
            if (given || fn == f.params.size()) front.push_back(tp);
            else back.emplace_back(tp, fn);
        }
        for (size_t i = 0; i < f.params.size(); ++i)
            if (f.params[i].type.function && std::any_of(f.typeParams.begin(), f.typeParams.end(), [&](const std::string& tp) { return mentions(f.params[i].type, tp); }))
                g.callables[i] = "kompoz_F" + std::to_string(i);
        g.written = front.size();
        std::string head;
        for (const std::string& tp : front) head += (head.empty() ? "" : ", ") + std::string("class ") + tp;
        for (auto& [i, name] : g.callables) head += (head.empty() ? "" : ", ") + std::string("class ") + name;
        for (auto& [tp, i] : back) {
            std::string args;
            for (const Type& a : f.params[i].type.params) args += ", " + type(a);
            head += ", class " + tp + " = std::remove_cvref_t<std::invoke_result_t<const " + g.callables[i] + "&" + args + ">>";
        }
        g.head = "template <" + head + "> ";
        return g;
    }
    std::string genericParamType(const GenericSig& g, size_t i, const Param& p, bool byValue) {
        auto c = g.callables.find(i);
        if (c == g.callables.end()) return paramTypeOf(p, byValue);
        return byValue ? c->second : "const " + c->second + "&";
    }

    static std::string templateHead(const std::vector<std::string>& params) {
        if (params.empty()) return {};
        std::string s = "template <";
        for (size_t i = 0; i < params.size(); ++i) s += (i ? ", class " : "class ") + params[i];
        return s + "> ";
    }

    // Whether a function of the module's classes, interfaces or objects of the name takes, last, a function that gives
    // a value (crate.convert { "#$it" }: its lambda's last expression is what it gives).
    bool methodLambdaGivesValue(const std::string& name) const {
        auto gives = [&](const std::vector<Function>& methods) {
            for (const Function& m : methods)
                if (m.name == name && !m.params.empty()) {
                    const Type& t = m.params.back().type;
                    if (t.function && t.result && t.result->name != "Unit") return true;
                }
            return false;
        };
        for (auto& [n, k] : module_.classDecls) if (gives(k->methods)) return true;
        for (auto& [n, i] : module_.interfaces) if (gives(i->methods)) return true;
        for (auto& [n, o] : module_.objectDecls) if (gives(o->methods)) return true;
        for (auto& [n, d] : module_.classes) if (gives(d->methods)) return true;
        return false;
    }

    // The functions a body calls by their names: its own, then those it inherits (but those its own override).
    using Overloads = std::map<std::string, std::vector<const Function*>, std::less<>>;
    Overloads methodsOf(const std::vector<Function>& own, const InterfaceMembers& inherited) const {
        Overloads out;
        for (const Function& f : own) out[f.name].push_back(&f);
        for (auto& [n, m] : inherited)
            if (m.method && std::none_of(own.begin(), own.end(), [&](const Function& f) { return overrides(f, m); })) out[n].push_back(m.method);
        return out;
    }

    // ---- Kotlin's order of evaluation: left to right ----

    // Whether evaluating a node may change what another one reads: an increment, an assignment, a call of the
    // module's (a function, a class made), a member call on a value that isn't one of Kotlin's pure ones (next(),
    // add(x)), a local function called; and the lambda an inline function runs at once (map { }). Kompoz's own
    // calls (Modifier chains, colors, sizes) don't.
    bool effectful(const Node* n) {
        if (!n) return false;
        switch (n->kind) {
            case Node::Lambda:
            case Node::Object: return false;
            case Node::Assign: return true;
            case Node::Unary:
                if (n->text == "++" || n->text == "--") return true;
                break;
            case Node::Call: {
                const Node& callee = companionless(*n->kids[0]);
                bool effect = false;
                if (callee.kind == Node::Name) {
                    const VarKind* k = lookup(callee.text);
                    effect = (k && *k == Callable) || (!k && (module_.functions.contains(callee.text) || module_.classDecls.contains(callee.text) ||
                                                              classMethods_.contains(callee.text) || callee.text == "println"));
                } else if (callee.kind == Node::Member) {
                    const Node* root = callee.kids[0].get();  // (a Modifier chain's start)
                    while ((root->kind == Node::Call || root->kind == Node::Member) && !root->kids.empty()) root = root->kids[0].get();
                    bool modifier = root->kind == Node::Name && root->text == "Modifier" && !lookup("Modifier");
                    bool staticCall = qualifier(*callee.kids[0]) && !module_.objectDecls.contains(dotted(*callee.kids[0]));
                    effect = !modifier && !staticCall && !kPureMembers.contains(callee.text);
                } else {
                    effect = true;
                }
                if (effect) return true;
                if (n->lambda && kInlineLambdas.contains(callee.text) && effectful(n->lambda.get()->kids.empty() ? nullptr : n->lambda.get())) return true;
                for (const NodeP& k : n->kids)
                    if (effectful(k.get())) return true;
                return callee.kind == Node::Member && effectful(callee.kids[0].get());
            }
            default: break;
        }
        for (const NodeP& k : n->kids)
            if (effectful(k.get())) return true;
        for (const auto& b : n->branches) {
            for (const auto& c : b.conds)
                if (effectful(c.second.get())) return true;
            if (effectful(b.body.get())) return true;
        }
        return false;
    }
    // Whether when a node is evaluated may change its value: anything but a constant, a lambda, or a val's or a
    // parameter's name (nothing changes those).
    bool orderMatters(const Node* n) const {
        switch (n->kind) {
            case Node::Name:
                if (const VarKind* k = lookup(n->text); k && *k == Local) return varLocals_.contains(n->text);
                if (auto p = module_.properties.find(n->text); p != module_.properties.end() && !lookup(n->text) && !memberFields_.contains(n->text))
                    return p->second->decl->flag || p->second->decl->delegated;  // (a top-level var may change)
                return true;
            case Node::Number:
            case Node::Bool:
            case Node::Null:
            case Node::Char:
            case Node::Lambda:
            case Node::Object: return false;
            case Node::String: return !n->kids.empty();
            default: return true;
        }
    }
    // An expression whose operands C++ evaluates in no fixed order (a call's arguments, a + b, a string template's
    // parts), written so that they are evaluated as Kotlin does, left to right, where it can matter (two of them
    // whose order counts, one changing something): ::kompoz::kt::inOrder([&](auto&& a0, …) { the expression on
    // a0, … }, ::kompoz::kt::Args{operand0, …}) (a braced list's are in order). `emit` writes the expression; the
    // operands (`nodes`, in their order) are written in it as the lambda's parameters. False: not needed, nothing
    // written.
    bool ordered(const Node& owner, const std::vector<const Node*>& nodes, const std::function<void()>& emit) {
        if (orderingNow_.contains(&owner) || awaitedTypes_ || classTyping_) return false;
        std::vector<const Node*> held;
        bool effects = false;
        for (const Node* n : nodes)
            if (n && orderMatters(n)) {
                held.push_back(n);
                effects = effects || effectful(n);
            }
        if (held.size() < 2 || !effects) return false;
        Output::Mark mark = out_->mark();
        std::string id = std::to_string(ordered_++), params;
        for (size_t i = 0; i < held.size(); ++i) params += (i ? ", auto&& kompoz_arg" : "auto&& kompoz_arg") + id + "_" + std::to_string(i);
        out_->text("::kompoz::kt::inOrder(" + capture("[&]") + "(" + params + ") -> decltype(auto) { return ");
        for (size_t i = 0; i < held.size(); ++i) {
            std::string a = "kompoz_arg" + id + "_" + std::to_string(i);
            substitutes_[held[i]] = {"std::forward<decltype(" + a + ")>(" + a + ")", nullptr, false};
        }
        orderingNow_.insert(&owner);
        bool wasSuspend = std::exchange(suspend_, false);  // (the operands are awaited where they are: in the Args)
        emit();
        suspend_ = wasSuspend;
        orderingNow_.erase(&owner);
        std::vector<Substitute> written;
        bool all = true;
        for (const Node* n : held) {
            written.push_back(substitutes_[n]);
            all = all && substitutes_[n].used;
            substitutes_.erase(n);
        }
        if (!all) {  // (an operand the expression's form writes otherwise: as it is, then)
            out_->rewind(mark);
            return false;
        }
        out_->text("; }, ::kompoz::kt::Args{");
        for (size_t i = 0; i < held.size(); ++i) {
            if (i) out_->text(", ");
            expr(*held[i], written[i].expected);
        }
        out_->text("})");
        return true;
    }

    // A class's base when it is a class written in Kotlin here (open or abstract), else none.
    const ClassDecl* kotlinBase(const ClassDecl& k) const {
        if (!k.base) return nullptr;
        auto b = module_.classDecls.find(k.base->name);
        return b == module_.classDecls.end() || b->second == &k ? nullptr : b->second;
    }
    // Whether a class is a handle to its object (Name::Object): one with no base, or deriving from such a class
    // (its object from that one's); one deriving from a C++ class (a ViewModel) is that class itself.
    bool isHandle(const ClassDecl& k, int depth = 0) const {
        if (!k.base || sealedNamed(k.base->name)) return true;  // (a sealed class's case: a handle, as one with no base)
        const ClassDecl* b = kotlinBase(k);
        return b && depth < 32 && isHandle(*b, depth + 1);
    }
    // The members a class inherits: its Kotlin base classes' (read through functions: accessed properties,
    // functions), nearest first, then the interfaces of all of them.
    InterfaceMembers inheritedMembers(const ClassDecl& k) {
        InterfaceMembers out;
        std::vector<Type> interfaces = k.interfaces;
        int depth = 0;
        Bindings bind;
        const Type* baseType = k.base ? &*k.base : nullptr;
        for (const ClassDecl* b = kotlinBase(k); b && depth < 32; baseType = b->base ? &*b->base : nullptr, b = kotlinBase(*b), ++depth) {
            Type actual = substitute(*baseType, bind);  // (Box<Int>: its T is Int, and its base's parameters as it gives them)
            Bindings next;
            for (size_t j = 0; j < b->typeParams.size() && j < actual.args.size(); ++j) next.emplace(b->typeParams[j], actual.args[j]);
            bind = std::move(next);
            std::string owner = isHandle(*b) ? plainType(actual) + "::Object" : plainType(actual);
            auto add = [&](const std::string& n, const ClassProperty* p, const Function* f, bool body) {
                for (auto& [k2, v] : out)
                    if (k2 == n && (!f || sameMethod(v, *f, bind))) {
                        v.hasBody = v.hasBody || body;
                        return;
                    }
                out.emplace_back(n, InterfaceMember{nullptr, p, f, body, owner, b->name, bind});
            };
            for (const ClassProperty& p : b->properties)
                if (p.accessed()) add(p.name, &p, nullptr, !p.isAbstract);
            for (const Function& f : b->methods) add(f.name, nullptr, &f, !f.isAbstract);
            for (const Type& t : b->interfaces) interfaces.push_back(substitute(t, bind));
        }
        for (auto& [n, m] : interfaceMembers(interfaces))
            if (std::none_of(out.begin(), out.end(), [&](const auto& o) { return o.first == n && (!m.method || sameMethod(o.second, *m.method, m.bind)); }))
                out.emplace_back(n, m);
        markOverloads(out);
        return out;
    }
    // A parameter's type: by value in a coroutine (its frame outlives the call), else as paramType says.
    std::string paramTypeOf(const Param& p, bool byValue) { return byValue ? type(p.type) : paramType(p.type); }
    // The type an interface's member gives, for an override's declaration: decltype of the call on the
    // interface's object (`object`: that type as it is spelled where the override is).
    std::string memberType(const std::string& object, const InterfaceMember& m) {
        // (from the member function's own type: its parameters' types are the interface's, named as it names them)
        std::string owner = object.starts_with("typename ") ? object.substr(9) : object;
        if (m.overloaded) {  // (of several of its name: a call taking its parameters)
            std::string call = "decltype(std::declval<const " + owner + "&>()." + cppName(m.method->name) + "(";
            for (size_t i = 0; i < m.method->params.size(); ++i)
                call += (i ? ", " : "") + std::string("std::declval<") + paramTypeIn(m, m.method->params[i].type) + ">()";
            return call + "))";
        }
        return "::kompoz::kt::ResultOf<decltype(&" + owner + "::" + cppName(m.property ? m.property->name : m.method->name) + ")>";
    }

    // A function's head, `prefix` before it (" virtual ", " static "): R name(params) [const] [-> T]`suffix`, its
    // parameters declared by the caller. A suspend fun's result is its coroutine (Task, TaskOf<T>), its parameters
    // by value; `inherited`: what an override gives (an interface's function's); a result written nowhere is
    // deduced (auto, `deduce`) or, where it can't be (a virtual one, a coroutine), its expression's (decltype).
    void functionHead(const Function& m, const std::string& prefix, bool isConst, const std::string& suffix, const std::string& inherited,
                      bool defaults, bool deduce) {
        GenericSig generic = genericSig(m);
        if (!generic.head.empty() && (prefix.find("virtual") != std::string::npos || !suffix.empty()))
            throw CompileError(m.pos, m.name + ": a generic function can't be open, abstract or an override (C++ has no virtual templates)");
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
        out_->text(" " + generic.head + (m.isSuspend ? "[[nodiscard]]" : "") + prefix + result + " " + cppName(m.name) + "(");
        for (size_t i = 0; i < m.params.size(); ++i) {
            const Param& p = m.params[i];
            out_->text((i ? ", " : "") + genericParamType(generic, i, p, m.isSuspend) + " " + cppName(p.name));
            if (defaults && p.defaultValue && i >= defaultsFrom(m)) {
                out_->text(" = ");
                expr(*p.defaultValue, &p.type);
            }
        }
        out_->text(std::string(")") + (isConst ? " const" : ""));
        if (trailing == 1) out_->text(" -> " + inherited);
        if (trailing == 2) {
            out_->text(std::string(" -> ") + (m.isSuspend ? "::kompoz::kt::TaskFor<decltype(" : "std::remove_cvref_t<decltype("));
            typing(*m.body->kids[0]->kids[0], m.isSuspend);  // (an expression body: a Return)
            out_->text(")>");
        }
        out_->text(suffix);
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
        out_->text(" {");
        statements(*m.body, false);
        if (m.isSuspend && !m.expressionBody && (!m.result || m.result->name == "Unit")) out_->text(" co_return;");
        out_->text(" }");
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
    static bool stores(const ClassProperty& p) { return !p.delegated && !p.isAbstract && (p.init || !p.getter || usesField(p)); }
    static std::string storageOf(const ClassProperty& p) { return (p.delegated ? "kompoz_by_" : "kompoz_value_") + p.name; }

    // The members of a class, an object or an interface read through functions, known by name in its body:
    // the delegated ones (their delegate), those with accessors (an object's: get, set, private set; a class's:
    // an interface's too), and the interfaces' properties it doesn't declare (`own`: its fields of other names).
    void declareMembers(const std::vector<ClassProperty>& properties, const std::vector<Type>& interfaces, bool isObject,
                        const std::vector<std::string>& accessedParams = {}, const std::set<std::string, std::less<>>& own = {}) {
        declareMembers(properties, interfaceMembers(interfaces), isObject, accessedParams, own);
    }
    void declareMembers(const std::vector<ClassProperty>& properties, const InterfaceMembers& inherited, bool isObject,
                        const std::vector<std::string>& accessedParams = {}, const std::set<std::string, std::less<>>& own = {}) {
        for (const std::string& n : accessedParams) declare(n, Accessor);
        for (const ClassProperty& p : properties)
            if (p.delegated) declare(p.name, MemberDelegated);
            else if (isObject ? p.getter || p.setter || p.privateSetter : p.accessed()) declare(p.name, Accessor);
        for (auto& [name, m] : inherited) {
            bool declared = own.contains(name) || scopes_.back().contains(name) ||
                            std::any_of(properties.begin(), properties.end(), [&](const ClassProperty& p) { return p.name == name; });
            if (m.property && !declared && (!isObject || m.hasBody)) declare(name, Accessor);
        }
    }

    // A property's functions: its getter, name(), and a var's setter, setName(v) (private with `private set`);
    // `isStatic`: an object's. An override of an interface's property gives what it does; the getter reads
    // the delegate, the value kept, or runs get() (its `field` the value kept), the setter likewise.
    template <class Access>
    void accessorFunctions(const ClassProperty& p, const InterfaceMember* base, bool isStatic, Access& access, bool derives = true) {
        if (p.overrides && (!base || !base->property)) throw CompileError(p.pos, p.name + " overrides no property of the interfaces");
        std::string storage = storageOf(p);
        std::string q = isStatic ? " static " : p.isOpen || p.isAbstract ? " virtual " : " ", c = isStatic ? "" : " const";
        bool isOverride = !isStatic && p.overrides && derives;  // (a data class's: its interfaces' object forwards to it)
        const Type* baseType = base && base->property && base->property->type ? &*base->property->type : nullptr;
        std::string t = p.type ? type(*p.type) : baseType ? typeIn(*base, *baseType) : "";
        if (p.isAbstract) {  // an abstract class's: its subclasses give it
            access(!p.isPrivate);
            out_->at(p.pos);
            out_->text(" virtual " + t + " " + cppName(p.name) + "() const = 0;");
            if (p.isVar) out_->text(" virtual void " + setterOf(p.name) + "(" + paramType(*p.type) + ") const = 0;");
            return;
        }
        access(!p.isPrivate);
        out_->at(p.pos);
        if (!t.empty()) {
            out_->text(q + t + " " + cppName(p.name) + "()" + c);
        } else if (isOverride) {
            out_->text(q + "auto " + cppName(p.name) + "()" + c + " -> " + memberType(base->owner, *base));
        } else if (p.delegated) {
            out_->text(q + "decltype(auto) " + cppName(p.name) + "()" + c);
        } else if (!p.getter) {
            out_->text(q + "std::remove_cvref_t<decltype(" + storage + ")> " + cppName(p.name) + "()" + c);
        } else {
            out_->text(q + "auto " + cppName(p.name) + "()" + c);
        }
        out_->text(std::string(isOverride ? " override" : "") + " {");
        if (p.isLateinit) {
            out_->text(" return ::kompoz::kt::initialized(" + storage + ", \"" + p.name + "\");");
        } else if (p.delegated) {
            out_->text(" return ::kompoz::kt::delegated(" + storage + ");");
        } else if (p.getter) {
            std::string was = std::exchange(fieldOf_, storage);
            scopes_.emplace_back();
            declare("field", BackingField);
            statements(*p.getter, false);
            scopes_.pop_back();
            fieldOf_ = was;
        } else {
            out_->text(" return " + storage + ";");
        }
        out_->text(" }");
        if (!p.isVar) return;
        access(!(p.isPrivate || p.privateSetter));
        std::string pt = p.type ? paramType(*p.type) : baseType ? paramTypeIn(*base, *baseType)
                         : p.delegated ? "const std::remove_cvref_t<decltype(::kompoz::kt::delegated(" + storage + "))>&"
                                       : "const std::remove_cvref_t<decltype(" + storage + ")>&";
        std::string v = p.setter ? cppName(p.setterParam) : "kompoz_v";
        out_->text(q + "void " + setterOf(p.name) + "(" + pt + " " + v + ")" + c + (isOverride && base->property->isVar ? " override" : "") + " {");
        if (p.delegated) {
            out_->text(" " + storage + ".set(" + v + ");");
        } else if (p.setter) {
            std::string was = std::exchange(fieldOf_, storage);
            scopes_.emplace_back();
            declare(p.setterParam, Local);
            declare("field", BackingField);
            statements(*p.setter, false);
            scopes_.pop_back();
            fieldOf_ = was;
        } else {
            out_->text(" " + storage + " = " + v + ";");
        }
        out_->text(" }");
    }

    // What a data class's value or an object is where one of its interfaces is wanted: a class template
    // (kompoz_Object, made only where it is converted, after the interfaces' definitions: they are only
    // declared before it), its abstract members the value's or the object's; kompoz_shared() makes one (an
    // object's: its one), kompoz_from(o) gives the value back (`x as Item`).
    void interfaceAdapter(const std::string& name, const std::vector<Type>& supers, bool isObject, const std::function<bool(const std::string&)>& has,
                          Pos pos) {
        std::vector<const Type*> direct;
        for (const Type& t : supers)
            if (interfaceNamed(t.name)) direct.push_back(&t);
        if (direct.empty()) return;
        auto later = [](const std::string& t) { return "::kompoz::kt::Later<kompoz_D, " + t + ">"; };
        std::string bases;
        for (const Type* t : direct) bases += std::string(bases.empty() ? " " : ", ") + "public virtual " + later(plainType(*t)) + "::Object";
        std::string value = isObject ? later(name) + "{}" : "value";
        out_->text(" template <class kompoz_D = void> struct kompoz_Object :" + bases + " {");
        if (!isObject) out_->text(" mutable " + later(name) + " value; explicit kompoz_Object(const " + later(name) + "& v) : value(v) {}");
        for (auto& [n, m] : interfaceMembers(supers)) {
            if (!has(n)) {
                if (m.hasBody) continue;  // (the interface's own)
                throw CompileError(pos, name + " doesn't implement " + m.from + "." + n);
            }
            std::string object = "typename " + later(m.owner.substr(0, m.owner.size() - std::string("::Object").size())) + "::Object";
            if (m.property) {
                out_->text(" auto " + cppName(n) + "() const -> " + memberType(object, m) + " override { return KOMPOZ_KT_GET((" + value + "), " +
                          cppName(n) + "); }");
                if (m.property->isVar) {
                    if (!m.property->type) throw CompileError(m.property->pos, n + " needs a type");
                    out_->text(" void " + setterOf(n) + "(" + paramTypeIn(m, *m.property->type) + " kompoz_v) const override { KOMPOZ_KT_SET((" + value + "), " +
                              cppName(n) + ", " + setterOf(n) + ", (kompoz_v)); }");
                }
            } else {
                out_->text(" auto " + cppName(n) + "(");
                std::string args;
                for (size_t i = 0; i < m.method->params.size(); ++i) {
                    const Param& p = m.method->params[i];
                    out_->text((i ? ", " : "") + (m.method->isSuspend ? typeIn(m, p.type) : paramTypeIn(m, p.type)) + " " + cppName(p.name));
                    args += (i ? ", " : "") + cppName(p.name);
                }
                out_->text(") const -> " + memberType(object, m) + " override { return " + (isObject ? later(name) + "::" : std::string("value.")) + cppName(n) +
                          "(" + args + "); }");
            }
        }
        out_->text(" std::string toString() const override { return ::kompoz::kt::str(" + value + "); }");
        if (!isObject)
            out_->text(" bool kompoz_equals(const ::kompoz::kt::InterfaceObject& o) const override { auto* other = dynamic_cast<const kompoz_Object*>(&o); "
                      "return other && other->value == value; }");
        out_->text(" }; using Object = kompoz_Object<>;");
        if (isObject) {
            out_->text(" template <class kompoz_D = void> static std::shared_ptr<kompoz_Object<kompoz_D>> kompoz_shared() { static const auto object = "
                      "std::make_shared<kompoz_Object<kompoz_D>>(); return object; }");
            out_->text(" template <class kompoz_O> static " + name + " kompoz_from(const std::shared_ptr<kompoz_O>&) { return {}; }");
        } else {
            out_->text(" template <class kompoz_D = void> std::shared_ptr<kompoz_Object<kompoz_D>> kompoz_shared() const { return "
                      "std::make_shared<kompoz_Object<kompoz_D>>(*this); }");
            out_->text(" template <class kompoz_O> static " + name + " kompoz_from(const std::shared_ptr<kompoz_O>& o) { return o->value; }");
        }
        // The interfaces' members with bodies that it doesn't declare, reached as its own (Item("a").shown(), Name.shown()).
        std::string q = isObject ? " static " : " ", c = isObject ? "" : " const";
        for (auto& [n, m] : interfaceMembers(supers)) {
            if (!m.hasBody || has(n)) continue;
            if (m.method)
                out_->text(" template <class... kompoz_A>" + q + "decltype(auto) " + cppName(n) + "(kompoz_A&&... a)" + c + " { return kompoz_shared<::kompoz::kt::Later<std::tuple<kompoz_A...>, void>>()->" +
                          cppName(n) + "(std::forward<kompoz_A>(a)...); }");
            else
                out_->text(" template <class kompoz_D = void>" + q + "decltype(auto) " + cppName(n) + "()" + c + " { return kompoz_shared<kompoz_D>()->" + cppName(n) +
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
            bases += ", public virtual " + plainType(b) + "::Object";
        }
        InterfaceMembers inherited = interfaceMembers(i.bases);
        std::string wasMemberPrefix = std::exchange(memberPrefix_, i.typeParams.empty() ? std::string() : "this->");
        out_->at(i.pos);
        Outermost outermost(*this);
        nestedDefinitions(i.nested, i.name);
        out_->text(templateHead(i.typeParams) + "class " + i.name + " {");
        companionMembers(i.companion, i.name);
        nestedAliases(i.nested, i.name);
        out_->text(" public: struct Object : " + bases + " {");
        bool wasInClass = std::exchange(inClass_, true);
        interfaceSelf_ = "Object";
        auto wasMethods = std::exchange(classMethods_, methodsOf(i.methods, inherited));
        for (const Function& m : i.methods) interfaceMethods_.insert(m.name);
        for (auto& [n, m] : inherited)
            if (m.method) interfaceMethods_.insert(n);
        scopes_.emplace_back();
        knowCompanion(i.companion);
        for (const ClassProperty& p : i.properties) declare(p.name, Accessor);
        for (auto& [n, m] : inherited)
            if (m.property && !scopes_.back().contains(n)) declare(n, Accessor);
        for (size_t index : i.order) {
            if (index % 2 == 0) {
                const ClassProperty& p = i.properties[index / 2];
                const InterfaceMember* base = memberNamed(inherited, p.name);
                if (p.overrides && (!base || !base->property)) throw CompileError(p.pos, p.name + " overrides no property of " + i.name + "'s interfaces");
                const Type* typed = p.type ? &*p.type : base && base->property->type ? &*base->property->type : nullptr;
                out_->at(p.pos);
                if (!p.getter) {
                    if (!typed) throw CompileError(p.pos, p.name + " needs a type");
                    out_->text(" virtual " + type(*typed) + " " + cppName(p.name) + "() const = 0;");
                } else {
                    out_->text(" virtual " + (typed ? type(*typed) : std::string("auto")) + " " + cppName(p.name) + "() const");
                    if (!typed) {
                        if (!(p.getter->kids.size() == 1 && p.getter->kids[0]->kind == Node::Return && !p.getter->kids[0]->kids.empty()))
                            throw CompileError(p.pos, p.name + " needs a type");
                        out_->text(" -> std::remove_cvref_t<decltype(");
                        typing(*p.getter->kids[0]->kids[0], false);
                        out_->text(")>");
                    }
                    out_->text(std::string(p.overrides ? " override" : "") + " {");
                    statements(*p.getter, false);
                    out_->text(" }");
                }
                if (p.isVar) {
                    if (!typed) throw CompileError(p.pos, p.name + " needs a type");
                    out_->text(" virtual void " + setterOf(p.name) + "(" + paramType(*typed) + ") const = 0;");
                }
            } else {
                const Function& m = i.methods[index / 2];
                const InterfaceMember* base = overridden(inherited, m);
                if (m.overrides && !base) throw CompileError(m.pos, m.name + " overrides no function of " + i.name + "'s interfaces");
                out_->at(m.pos);
                scopes_.emplace_back();
                for (const Param& p : m.params) declare(p.name, Local);
                functionHead(m, " virtual ", true, m.isAbstract ? " = 0" : base ? " override" : "", base ? memberType(base->owner, *base) : "", true,
                             false);
                if (m.isAbstract) out_->text(";");
                else functionBody(m);
                scopes_.pop_back();
            }
        }
        scopes_.pop_back();
        inClass_ = wasInClass;
        interfaceSelf_.clear();
        interfaceMethods_.clear();
        classMethods_ = std::move(wasMethods);
        memberPrefix_ = std::move(wasMemberPrefix);
        const std::string& n = i.name;
        out_->text(" }; " + n + "() = default;");
        out_->text(" template <class kompoz_T> requires requires(const kompoz_T& t) { { t.kompoz_shared() } -> std::convertible_to<std::shared_ptr<Object>>; } " + n +
                  "(const kompoz_T& t) : kompoz_object_(t.kompoz_shared()) {}");
        out_->text(" template <class kompoz_T> requires std::convertible_to<std::shared_ptr<kompoz_T>, std::shared_ptr<Object>> " + n +
                  "(std::shared_ptr<kompoz_T> p) : kompoz_object_(std::move(p)) {}");
        out_->text(" " + n + "(::kompoz::kt::FromObject, std::shared_ptr<Object> o) : kompoz_object_(std::move(o)) {}");
        out_->text(" static " + n + " kompoz_from(std::shared_ptr<Object> o) { return " + n + "(::kompoz::kt::FromObject{}, std::move(o)); }");
        out_->text(" Object& kompoz_object() const { return *kompoz_object_; }");
        out_->text(" const std::shared_ptr<Object>& kompoz_shared() const { return kompoz_object_; }");
        out_->text(" bool operator==(const " + n + "& o) const { return ::kompoz::kt::sameObject(kompoz_object_.get(), o.kompoz_object_.get()); }");
        out_->text(" std::string toString() const { return kompoz_object_ ? kompoz_object_->toString() : std::string(\"null\"); }");
        // C++ calls its functions on the handle (Kotlin's code reaches the object: kt::deref).
        for (const Function& m : i.methods) {
            if (m.name == "toString" || m.name == "equals" || m.name == "hashCode") continue;
            out_->text(" decltype(auto) " + cppName(m.name) + "(");
            std::string args;
            for (size_t k = 0; k < m.params.size(); ++k) {
                out_->text((k ? ", " : "") + paramTypeOf(m.params[k], m.isSuspend) + " " + cppName(m.params[k].name));
                args += (k ? ", " : "") + cppName(m.params[k].name);
            }
            out_->text(") const { return kompoz_object_->" + cppName(m.name) + "(" + args + "); }");
        }
        if (i.isFun) {  // Name { … }: an object whose one abstract function runs the lambda
            const Function* m = nullptr;
            for (const Function& f : i.methods)
                if (f.isAbstract) m = &f;
            std::string params, args;  // (a suspend fun's: its parameters by value, its result the lambda's coroutine)
            for (size_t k = 0; k < m->params.size(); ++k) {
                params += (k ? ", " : "") + paramTypeOf(m->params[k], m->isSuspend) + " " + cppName(m->params[k].name);
                args += (k ? ", " : "") + cppName(m->params[k].name);
            }
            InterfaceMember im{&i, nullptr, m, false, "Object", i.name, {}};
            out_->text(" template <class kompoz_F> static " + n + " kompoz_lambda(kompoz_F f) { struct Lambda : Object { kompoz_F f; explicit Lambda(kompoz_F g) : f(std::move(g)) {} auto " +
                      cppName(m->name) + "(" + params + ") const -> " + memberType("Object", im) + " override { return f(" + args + "); } }; return " + n +
                      "(::kompoz::kt::FromObject{}, std::make_shared<Lambda>(std::move(f))); }");
        }
        operators(i.methods, "kompoz_object_->", i.name);
        out_->text(" private: std::shared_ptr<Object> kompoz_object_; };\n");
        outermost.end();
    }

    // data class Name(val a: T, var b: U = d) { … }: a struct of its fields (made with their names, in any order),
    // == and toString() over them; its body's properties after them (a value given is computed as it is made, a
    // getter's each time, read through name()), its functions const, its init blocks run as it is made.
    void classDefinition(const DataClass& k) {
        out_->at(k.pos);
        // A generic one is made by kompoz_make_Name(fields…) where its type arguments aren't written: C++ takes them
        // from a function's arguments (not from designated initializers everywhere). Declared first: its own
        // functions make it too.
        std::string made, factoryParams, factoryFields;
        if (!k.typeParams.empty()) {
            made = k.name + "<";
            for (size_t i = 0; i < k.typeParams.size(); ++i) made += (i ? ", " : "") + k.typeParams[i];
            made += ">";
            for (size_t i = 0; i < k.fields.size(); ++i) {
                factoryParams += (i ? ", " : "") + type(k.fields[i].type) + " " + cppName(k.fields[i].name);
                factoryFields += (i ? ", ." : ".") + cppName(k.fields[i].name) + " = std::move(" + cppName(k.fields[i].name) + ")";
            }
            // (nested: a static function of the struct it is in, its outer type forwarding to it)
            out_->text(templateHead(k.typeParams) + "struct " + k.name + "; ");
            if (outerQualifier_.empty()) out_->text(templateHead(k.typeParams) + made + " kompoz_make_" + k.name + "(" + factoryParams + "); ");
        }
        Outermost outermost(*this);
        nestedDefinitions(k.nested, k.name);
        auto own = fieldNames(k.properties);
        for (const Param& f : k.fields) own.insert(f.name);
        if (const SealedDecl* s = sealedOf(k.supers)) {
            for (const Param& p : s->ctorParams) own.insert(p.name);
            for (const ClassProperty& p : s->properties) own.insert(p.name);
        }
        OwnFields ownFields(*this, std::move(own));
        out_->text(templateHead(k.typeParams) + "struct " + k.name + " {");
        companionMembers(k.companion, k.name);
        nestedAliases(k.nested, k.name);
        // Its inner classes: made with a copy of it (a data class is a value here), defined at its end.
        if (!k.nested.innerClasses.empty() && !k.typeParams.empty())
            throw CompileError(k.nested.innerClasses[0]->pos, "an inner class of a generic data class isn't supported: of a generic class it is");
        for (const auto& c : k.nested.innerClasses) out_->text(" class " + c->name + ";");
        auto wasInnerMade = innerMade_;
        for (const auto& c : k.nested.innerClasses) innerMade_[c->name] = {c.get(), "std::make_shared<" + k.name + ">(*this)"};
        scopes_.emplace_back();
        const SealedDecl* sealed = sealedOf(k.supers);
        bool body = k.hasBody() || sealed;
        for (size_t i = 0; i < k.fields.size(); ++i) {
            const Param& f = k.fields[i];
            out_->at(f.pos);
            out_->text(std::string(body && k.mutableFields[i] ? " mutable " : " ") + type(f.type) + " " + cppName(f.name));
            if (f.defaultValue) {
                out_->text(" = ");
                expr(*f.defaultValue, &f.type);
            } else {
                out_->text("{}");
            }
            out_->text(";");
        }
        if (sealed) sealedFields(*sealed, k.superCall, k.name, k.pos, &k);
        scopes_.pop_back();
        if (std::find(k.annotations.begin(), k.annotations.end(), "Serializable") != k.annotations.end()) {  // a route (nav_host.hpp)
            out_->text(" KOMPOZ_ROUTE(" + k.name);
            for (const Param& f : k.fields) out_->text(", " + cppName(f.name));
            out_->text(")");
        }
        if (body) dataClassBody(k);
        if (!body) {
            out_->text(" bool operator==(const " + k.name + "&) const = default;");
        } else {  // (its fields', as Kotlin's equals: not its body's)
            out_->text(" bool operator==(const " + k.name + "& o) const { return true");
            for (const Param& f : k.fields) out_->text(" && " + cppName(f.name) + " == o." + cppName(f.name));
            out_->text("; }");
        }
        // (a template: built only where it is used, so a field without a text form doesn't stop the rest)
        if (std::none_of(k.methods.begin(), k.methods.end(), [](const Function& m) { return m.name == "toString"; })) {
            out_->text(" template <class = void> std::string toString() const { return ::kompoz::kt::str(\"" + k.name + "(\"");
            for (size_t i = 0; i < k.fields.size(); ++i)
                out_->text(", \"" + std::string(i ? ", " : "") + k.fields[i].name + "=\", " + cppName(k.fields[i].name));
            out_->text(", \")\"); }");
        }
        if (!k.nested.innerClasses.empty()) {
            Module module = withNested(module_, k.name, k.nested);
            module.classes[k.name] = &k;
            innerDefinitions(k.name, k.nested, module, "std::shared_ptr<" + k.name + ">", "");
        }
        innerMade_ = std::move(wasInnerMade);
        interfaceAdapter(k.name, caseSupers(k.supers), false, [&](const std::string& n) {
            return std::any_of(k.fields.begin(), k.fields.end(), [&](const Param& f) { return f.name == n; }) ||
                   std::any_of(k.properties.begin(), k.properties.end(), [&](const ClassProperty& p) { return p.name == n; }) ||
                   std::any_of(k.methods.begin(), k.methods.end(), [&](const Function& m) { return m.name == n; }) ||
                   (sealed && sealedHas(*sealed, n));
        }, k.pos);
        out_->text(" };");
        if (!made.empty())
            out_->text(" " + templateHead(k.typeParams) + (outerQualifier_.empty() ? "" : "static ") + made + " kompoz_make_" + k.name + "(" + factoryParams +
                       ") { return " + made + "{" + factoryFields +
                      "}; }");
        out_->text("\n");
        outermost.end();
    }

    // A data class's body, after its fields (all public: it is still an aggregate, made with designated
    // initializers): a property with a value its member (computed from the fields as the value is made), one by a
    // delegate or with accessors read through name(); the functions const; an init block a member whose
    // initializer runs it. copy() makes it again (kompoz_rebuild: the values and init blocks run anew).
    void dataClassBody(const DataClass& k) {
        bool wasInClass = std::exchange(inClass_, true);
        scopes_.emplace_back();
        // A sealed class's case: its members first (those it doesn't override, not the abstract ones), as Kotlin
        // makes the base first; then its own.
        const SealedDecl* sealed = sealedOf(k.supers);
        std::vector<Type> supers = caseSupers(k.supers);
        if (sealed) declareMembers(sealed->properties, std::vector<Type>{}, false);
        declareMembers(k.properties, supers, false);
        auto access = [](bool) {};  // (an aggregate's members are public)
        InterfaceMembers inherited = interfaceMembers(supers);
        auto wasMethods = std::exchange(classMethods_, methodsOf(k.methods, inherited));
        std::vector<std::pair<const ClassProperty*, const Function*>> members;
        if (sealed)
            for (size_t index : sealed->memberOrder) {
                if (index % 2 == 0) {
                    const ClassProperty& p = sealed->properties[index / 2];
                    if (!p.isAbstract && std::none_of(k.properties.begin(), k.properties.end(), [&](const ClassProperty& q) { return q.name == p.name; }))
                        members.emplace_back(&p, nullptr);
                } else {
                    const Function& m = sealed->methods[index / 2];
                    if (!m.isAbstract && std::none_of(k.methods.begin(), k.methods.end(), [&](const Function& f) { return f.name == m.name && f.params.size() == m.params.size(); })) {
                        members.emplace_back(nullptr, &m);
                        classMethods_[m.name].push_back(&m);
                    }
                }
            }
        for (size_t index : k.order) members.emplace_back(index % 2 == 0 ? &k.properties[index / 2] : nullptr, index % 2 == 0 ? nullptr : &k.methods[index / 2]);
        if (sealed) calleesFirst(members);
        knowCompanion(k.companion);
        bool rebuilds = !k.initBlocks.empty() || sealed;
        for (auto [property, method] : members) {
            if (property) {
                const ClassProperty& p = *property;
                if (p.accessed() && !p.delegated && !stores(p)) {
                    accessorFunctions(p, memberNamed(inherited, p.name), false, access, false);
                    continue;
                }
                rebuilds = rebuilds || p.init;
                out_->at(p.pos);
                std::string storage = p.accessed() ? storageOf(p) : cppName(p.name);
                if (p.type && !p.delegated) {
                    out_->text(" mutable " + type(*p.type) + " " + storage);
                } else {
                    if (!p.init) throw CompileError(p.pos, "give " + p.name + " a type");
                    out_->text(" mutable std::remove_cvref_t<decltype(");
                    bool wasTyping = std::exchange(classTyping_, p.delegated);
                    const Type* wasValue = std::exchange(delegateValue_, p.delegated && p.type ? &*p.type : nullptr);
                    expr(*p.init);
                    classTyping_ = wasTyping, delegateValue_ = wasValue;
                    out_->text(")> " + storage);
                }
                if (p.init) {
                    out_->text(" = ");
                    const DataClass* was = std::exchange(dataInit_, &k);
                    expr(*p.init, p.type && !p.delegated ? &*p.type : nullptr);
                    dataInit_ = was;
                } else {
                    out_->text("{}");
                }
                out_->text(";");
                if (p.accessed()) accessorFunctions(p, memberNamed(inherited, p.name), false, access, false);
            } else {
                const Function& m = *method;
                if (m.isAbstract) throw CompileError(m.pos, m.name + " needs a body");
                out_->at(m.pos);
                scopes_.emplace_back();
                for (const Param& p : m.params) declare(p.name, Local);
                bool toString = m.name == "toString";
                functionHead(m, toString ? " template <class = void> " : " ", true, "", "", true, true);
                functionBody(m);
                scopes_.pop_back();
            }
        }
        for (size_t i = 0; sealed && i < sealed->initBlocks.size(); ++i) {  // (its sealed class's first)
            out_->text(" [[no_unique_address]] ::kompoz::kt::InitBlock kompoz_sealed_init_" + std::to_string(i) + " = " + capture("[=]") + "() {");
            statements(*sealed->initBlocks[i], false);
            out_->text(" };");
        }
        for (size_t i = 0; i < k.initBlocks.size(); ++i) {  // (run as it is made, after the values above)
            out_->text(" [[no_unique_address]] ::kompoz::kt::InitBlock kompoz_init_" + std::to_string(i) + " = " + capture("[=]") + "() {");
            statements(*k.initBlocks[i], false);
            out_->text(" };");
        }
        operators(k.methods, "", k.name);
        if (rebuilds) {
            out_->text(" void kompoz_rebuild() { *this = " + k.name + "{");
            for (size_t i = 0; i < k.fields.size(); ++i) out_->text((i ? ", ." : ".") + cppName(k.fields[i].name) + " = std::move(" + cppName(k.fields[i].name) + ")");
            out_->text("}; }");
        }
        scopes_.pop_back();
        inClass_ = wasInClass;
        classMethods_ = std::move(wasMethods);
    }

    // Name(a = 1, b = "x") for a class: its constructor's parameters in their order (defaults left to it).
    // (an inner class's: `outer` writes the outer object it is made with, first; `spelled`: its type as written here)
    void constructClass(const Node& c, const ClassDecl& k, const std::function<void()>& outer = {}, const std::string& spelled = {}) {
        if (c.lambda) throw CompileError(c.lambda->pos, k.name + " takes no lambda");
        if (k.isAbstract) throw CompileError(c.pos, k.name + " is abstract: make one of its subclasses");
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
        out_->text(spelled.empty() ? qualifierOf(k.name) + k.name : spelled);
        typeArguments(c);  // (Box<String>("x"); without them C++ takes them from the arguments, as Kotlin does)
        out_->text("(");
        if (outer) {
            outer();
            if (last > 0) out_->text(", ");
        }
        for (size_t f = 0; f < last; ++f) {
            if (f) out_->text(", ");
            const Param& p = k.ctorParams[f];
            const Node* arg = given[f] ? given[f] : p.defaultValue.get();
            if (!arg) throw CompileError(c.pos, k.name + " needs " + p.name);
            if (const Type* t = resolved(&p.type); arg->kind == Node::Lambda) lambda(*arg, t->function && t->result && t->result->name != "Unit");
            else expr(*arg, k.typeParams.empty() ? &p.type : nullptr);
        }
        out_->text(")");
    }

    // A class's C++ base as written: Kotlin's Exception and Throwable kt's (C++'s takes no message; a catch's is C++'s).
    std::string baseClass(const Type& t) {
        return t.name == "Exception" || t.name == "Throwable" ? "::kompoz::kt::" + t.name : plainType(t);
    }
    // Whether a class derives from one of Kotlin's exceptions (or from a class that does): its objects are thrown.
    bool thrown(const Type& base, int depth = 0) const {
        if (auto t = kTypes.find(base.name); t != kTypes.end())
            return base.name == "Exception" || base.name == "Throwable" || t->second.starts_with("::kompoz::kt::");
        auto b = module_.classDecls.find(base.name);
        return b != module_.classDecls.end() && b->second->base && depth < 32 && thrown(*b->second->base, depth + 1);
    }

    // A class's inner classes, in it (after its object, complete there): each by a generator of its own, whose body's
    // receivers are the outer objects (this one's and those it is inner to), their members reached by their names.
    void innerDefinitions(const ClassDecl& k, bool handle) {
        Module module = withNested(module_, k.name, k.nested);
        module.classDecls[k.name] = &k;  // (its outer one's members: looked up by its name)
        innerDefinitions(k.name, k.nested, module, handle ? "std::shared_ptr<" + k.name + "::Object>" : k.name + "*", handle ? k.name : std::string());
    }
    // (a data class's or an enum's: a value, the inner object holding a copy of it, shared: std::shared_ptr<Name>)
    void innerDefinitions(const std::string& name, const NestedDecls& nested, const Module& module, const std::string& holder, const std::string& handle) {
        for (const auto& c : nested.innerClasses) {
            Generator g(*out_, module);
            g.deferred_ = deferred_;
            g.innerClass_ = c.get();
            g.outers_ = outers_;
            g.outers_.push_back({name, holder, handle});
            for (const OuterLink& o : g.outers_) {
                g.receivers_.push_back(o.receiver);
                g.receiverConst_.push_back(false);
                g.receiverLabels_.push_back(o.receiver);
            }
            // (its sibling inner classes, made by their names: with the same outer object)
            for (const auto& s : nested.innerClasses) g.innerMade_[s->name] = {s.get(), "kompoz_this_" + std::to_string(g.outers_.size())};
            g.classDeclDefinition(*c);
        }
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
        Outermost outermost(*this);
        nestedDefinitions(k.nested, k.name);
        // A sealed class's case (class Failure(val e: Exception) : Result()): a handle, as a class without a base is,
        // with its sealed class's interfaces, constructor's values (from its base's arguments) and members, first.
        const SealedDecl* sealedBase = k.base ? sealedNamed(k.base->name) : nullptr;
        std::vector<Type> interfaces;
        for (const Type& t : k.interfaces) {
            if (const SealedDecl* si = sealedNamed(t.name)) sealedBase = si;  // (a sealed interface's case)
            else interfaces.push_back(t);
        }
        if (sealedBase) interfaces.insert(interfaces.end(), sealedBase->supers.begin(), sealedBase->supers.end());
        const ClassDecl* kbase = kotlinBase(k);
        if (kbase && !kbase->isOpen && !kbase->isAbstract)
            throw CompileError(k.base->pos, kbase->name + " is final: a class derives from an open class or an abstract one");
        bool handle = isHandle(k);
        std::string baseObject = kbase && handle ? plainType(*k.base) + "::Object" : std::string();
        std::string bases = baseObject.empty() ? std::string() : "public " + baseObject;  // its base's and interfaces' objects
        for (const Type& t : interfaces) {
            const InterfaceDecl* d = interfaceNamed(t.name);
            if (!d) throw CompileError(t.pos, t.name + " isn't an interface written in Kotlin here; a base class is constructed: " + t.name + "()");
            bases += std::string(bases.empty() ? "" : ", ") + "public virtual " + plainType(t) + "::Object";
        }
        // An inner class (this generator's own, innerClass_): its object holds its outer ones' (kompoz_this_1…N, as the
        // receivers of its body: their members are reached by their names). One with inner classes: shared (kt::Shared).
        bool isInner = innerClass_ == &k;
        if (isInner && !handle) throw CompileError(k.pos, "an inner class with a C++ base isn't supported: give it a Kotlin base, or no base");
        const auto& inners = k.nested.innerClasses;
        if (handle && !inners.empty()) bases += std::string(bases.empty() ? "" : ", ") + "public virtual ::kompoz::kt::Shared";
        InterfaceMembers inherited = inheritedMembers(k);
        if (sealedBase)
            for (auto& [n, m] : interfaceMembers(sealedBase->supers))
                if (std::none_of(inherited.begin(), inherited.end(), [&](const auto& o) { return o.first == n; })) inherited.emplace_back(n, m);
        bool interfaceRooted = std::any_of(inherited.begin(), inherited.end(), [](const auto& m) { return m.second.in != nullptr; }) || !interfaces.empty();
        std::string wasSuper = std::exchange(superObject_, kbase ? (handle ? baseObject : qualified(kbase->name)) : k.base && !sealedBase ? baseClass(*k.base) : std::string());
        const InterfaceMembers* wasSuperMembers = std::exchange(superMembers_, &inherited);
        auto wasMethods = std::exchange(classMethods_, methodsOf(k.methods, inherited));
        // Its properties and functions: its sealed class's first (those it doesn't override, not the abstract ones),
        // the functions each after those it calls (C++ deduces their types in order).
        std::vector<std::pair<const ClassProperty*, const Function*>> members;
        if (sealedBase)
            for (size_t index : sealedBase->memberOrder) {
                if (index % 2 == 0) {
                    const ClassProperty& p = sealedBase->properties[index / 2];
                    if (!p.isAbstract && std::none_of(k.properties.begin(), k.properties.end(), [&](const ClassProperty& q) { return q.name == p.name; }))
                        members.emplace_back(&p, nullptr);
                } else {
                    const Function& m = sealedBase->methods[index / 2];
                    if (!m.isAbstract && std::none_of(k.methods.begin(), k.methods.end(), [&](const Function& f) { return f.name == m.name && f.params.size() == m.params.size(); })) {
                        members.emplace_back(nullptr, &m);
                        classMethods_[m.name].push_back(&m);
                    }
                }
            }
        for (const ClassProperty& p : k.properties) members.emplace_back(&p, nullptr);
        for (const Function& m : k.methods) members.emplace_back(nullptr, &m);
        if (sealedBase) calleesFirst(members);
        std::vector<const ClassProperty*> props;
        std::vector<const Function*> methods;
        for (auto [p, m] : members) {
            if (p) props.push_back(p);
            if (m) methods.push_back(m);
        }
        std::string wasSelf = std::exchange(interfaceSelf_, handle ? std::string("Object") : k.name);  // (its types' view of it)
        std::string wasThisClass = std::exchange(thisClass_, k.name);
        auto wasInnerMade = innerMade_;  // (its inner classes, made by their names in it: with it as their outer object)
        for (const auto& c : inners) innerMade_[c->name] = {c.get(), handle ? "::kompoz::kt::selfOf(this)" : "const_cast<" + k.name + "*>(this)"};
        // A template's members inherited from a base of its parameters are reached through this-> (C++ looks there then).
        std::string wasMemberPrefix = std::exchange(memberPrefix_, k.typeParams.empty() ? std::string() : "this->");
        out_->at(k.pos);
        out_->text(templateHead(k.typeParams) + "class " + k.name + (k.base && !handle ? " : public " + baseClass(*k.base) + (bases.empty() ? std::string() : ", " + bases) : std::string()) +
                  " {");
        if (k.base && !handle && thrown(*k.base))  // (an exception of its own: its name in its text, "MyError: …")
            out_->text(" public: const char* kotlinName() const override { return \"" + k.name + "\"; }");
        bool wasInClass = std::exchange(inClass_, true);
        auto own = fieldNames(k.properties);
        for (size_t i = 0; i < k.ctorParams.size(); ++i)
            if (k.ctorKinds[i] != ' ') own.insert(k.ctorParams[i].name);
        if (sealedBase) {
            for (const Param& p : sealedBase->ctorParams) own.insert(p.name);
            for (const ClassProperty& p : sealedBase->properties) own.insert(p.name);
        }
        OwnFields ownFields(*this, std::move(own));
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
            if (sealedBase) declareMembers(sealedBase->properties, std::vector<Type>{}, false);
            declareMembers(k.properties, inherited, false, accessedParams, fields);
            knowCompanion(k.companion);
            if (isInner) {  // (an inner class's own fields before its outer object's members of their names)
                for (const std::string& f : fields) declare(f, Local);
                for (const ClassProperty& p : k.properties)
                    if (!p.accessed() && !p.isLateinit) declare(p.name, Local);
            }
        }
        scopes_.emplace_back();
        // Plain constructor parameters: only the constructor and the initializers see them.
        std::set<std::string, std::less<>> plainParams;
        for (size_t i = 0; i < k.ctorParams.size(); ++i)
            if (k.ctorKinds[i] == ' ') plainParams.insert(k.ctorParams[i].name);
        bool isPublic = true;
        auto access = [&](bool wantPublic) {
            if (wantPublic != isPublic) out_->text(wantPublic ? " public:" : " private:");
            isPublic = wantPublic;
        };
        out_->text(" public:");
        companionMembers(k.companion, k.name);  // (first: its functions' types are known where the rest's bodies use them)
        nestedAliases(k.nested, k.name);
        out_->text(" public:");
        for (const auto& c : inners) out_->text(" class " + c->name + ";");  // (defined after its object: they reach its members)
        if (handle) out_->text(" struct Object" + (bases.empty() ? std::string() : " : " + bases) + " {");
        if (handle)
            for (const auto& c : inners) out_->text(" friend class " + c->name + ";");  // (they reach its private members, as Kotlin's do)
        for (size_t i = 0; isInner && i < outers_.size(); ++i) out_->text(" " + outers_[i].holder + " kompoz_this_" + std::to_string(i + 1) + ";");
        if ((k.isOpen || k.isAbstract) && !k.base && k.interfaces.empty())  // (deleted through its base's pointer: a subclass's)
            out_->text(std::string(" virtual ~") + (handle ? "Object" : k.name) + "() = default;");
        // The members: the constructor's val/var parameters, then the properties (one read through functions
        // keeps its value or delegate aside, private: kompoz_value_<name>, kompoz_by_<name>).
        auto paramStorage = [&](size_t i) { return k.ctorOverride[i] ? "kompoz_value_" + k.ctorParams[i].name : cppName(k.ctorParams[i].name); };
        for (size_t i = 0; sealedBase && i < sealedBase->ctorParams.size(); ++i) {  // (its sealed class's values first)
            const Param& p = sealedBase->ctorParams[i];
            access(true);
            out_->at(p.pos);
            out_->text(" mutable " + type(p.type) + " " + cppName(p.name) + ";");
        }
        for (size_t i = 0; i < k.ctorParams.size(); ++i) {
            if (k.ctorKinds[i] == ' ') continue;
            access(!k.ctorPrivate[i] && !k.ctorOverride[i]);
            out_->at(k.ctorParams[i].pos);
            out_->text(" mutable " + type(k.ctorParams[i].type) + " " + paramStorage(i) + ";");
        }
        auto propertyStorage = [&](const ClassProperty& p) { return p.accessed() ? storageOf(p) : cppName(p.name); };
        for (const ClassProperty* pp : props) {
            const ClassProperty& p = *pp;
            if (p.accessed() && !p.delegated && !stores(p)) continue;  // (a getter's alone)
            access(!p.accessed() && !p.isPrivate);
            out_->at(p.pos);
            const InterfaceMember* base = p.overrides ? memberNamed(inherited, p.name) : nullptr;
            const Type* baseType = base && base->property && base->property->type ? &*base->property->type : nullptr;
            if (p.isLateinit) {  // (empty until it is set)
                out_->text(" mutable std::optional<" + type(*p.type) + "> " + propertyStorage(p) + ";");
            } else if (p.type && !p.delegated) {
                out_->text(" mutable " + type(*p.type) + " " + propertyStorage(p) + ";");
            } else if (baseType && !p.delegated) {
                out_->text(" mutable " + typeIn(*base, *baseType) + " " + propertyStorage(p) + ";");
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
                out_->text(" mutable std::remove_cvref_t<decltype(");
                bool wasTyping = std::exchange(classTyping_, p.delegated);
                const Type* wasValue = std::exchange(delegateValue_, p.delegated && p.type ? &*p.type : nullptr);
                expr(*p.init);
                classTyping_ = wasTyping, delegateValue_ = wasValue;
                out_->text(")> " + propertyStorage(p) + ";");
            }
        }
        // The constructor: Base(args), the members in order, the init blocks. (A val/var parameter is
        // named for its member: the initializers and init blocks reading the Kotlin name reach the
        // member, which the parameter has filled by then.)
        access(true);
        out_->at(k.pos);
        size_t firstDefault = k.ctorParams.size();
        while (firstDefault > 0 && k.ctorParams[firstDefault - 1].defaultValue) --firstDefault;
        auto parameters = [&](bool defaults, bool forMembers) {
            for (size_t i = 0; i < k.ctorParams.size(); ++i) {
                const Param& p = k.ctorParams[i];
                bool renamed = forMembers && k.ctorKinds[i] != ' ';
                out_->text((i ? ", " : "") + paramType(p.type) + " " + (renamed ? "kompoz_" + p.name : cppName(p.name)));
                if (defaults && i >= firstDefault) {
                    out_->text(" = ");
                    expr(*p.defaultValue);
                }
            }
        };
        if (handle) out_->text(" Object(" + (isInner ? outers_.back().holder + " kompoz_outer" + (k.ctorParams.empty() ? "" : ", ") : std::string()));
        else out_->text(std::string(k.ctorParams.size() == 1 ? " explicit " : " ") + k.name + "(");
        parameters(!handle, true);
        out_->text(")");
        for (size_t i = 0; i < k.ctorParams.size(); ++i)
            if (k.ctorKinds[i] == ' ') declare(k.ctorParams[i].name, Local);
        bool any = false;
        if (k.base && !k.baseArgs.empty() && !sealedBase) {
            // (made before the members: a val/var parameter is read as the parameter there, kompoz_<name>)
            out_->text(" : " + (baseObject.empty() ? baseClass(*k.base) : baseObject) + "(");
            scopes_.emplace_back();
            parameterScope_ = scopes_.size();
            for (size_t i = 0; i < k.ctorParams.size(); ++i)
                if (k.ctorKinds[i] != ' ') {
                    declare(k.ctorParams[i].name, Local);
                    parameterNames_[k.ctorParams[i].name] = "kompoz_" + k.ctorParams[i].name;
                }
            for (size_t i = 0; i < k.baseArgs.size(); ++i) {
                if (i) out_->text(", ");
                expr(*k.baseArgs[i]);
            }
            parameterNames_.clear();
            scopes_.pop_back();
            out_->text(")");
            any = true;
        }
        for (size_t i = 0; isInner && i < outers_.size(); ++i, any = true)  // (its outer object's own outer ones: theirs)
            out_->text(std::string(any ? ", " : " : ") + "kompoz_this_" + std::to_string(i + 1) +
                       (i + 1 == outers_.size() ? "(std::move(kompoz_outer))" : "(kompoz_outer->kompoz_this_" + std::to_string(i + 1) + ")"));
        if (sealedBase) {  // its sealed class's values, from its base's arguments (a val parameter read as the parameter)
            std::vector<const Node*> given = sealedArguments(*sealedBase, k.baseArgNames, k.baseArgs, k.name, k.pos);
            scopes_.emplace_back();
            parameterScope_ = scopes_.size();
            for (size_t i = 0; i < k.ctorParams.size(); ++i)
                if (k.ctorKinds[i] != ' ') {
                    declare(k.ctorParams[i].name, Local);
                    parameterNames_[k.ctorParams[i].name] = "kompoz_" + k.ctorParams[i].name;
                }
            for (size_t i = 0; i < given.size(); ++i, any = true) {
                out_->text(std::string(any ? ", " : " : ") + cppName(sealedBase->ctorParams[i].name) + "(");
                expr(*given[i], &sealedBase->ctorParams[i].type);
                out_->text(")");
            }
            parameterNames_.clear();
            scopes_.pop_back();
        }
        for (size_t i = 0; i < k.ctorParams.size(); ++i) {
            if (k.ctorKinds[i] == ' ') continue;
            out_->text(std::string(any ? ", " : " : ") + paramStorage(i) + "(std::move(kompoz_" + k.ctorParams[i].name + "))");
            any = true;
        }
        for (const ClassProperty* pp : props) {
            const ClassProperty& p = *pp;
            if (!p.init) continue;
            out_->text(std::string(any ? ", " : " : ") + propertyStorage(p) + "(");
            expr(*p.init, p.type && !p.delegated ? &*p.type : nullptr);
            out_->text(")");
            any = true;
        }
        out_->text(" {");
        if (sealedBase)  // (its sealed class's init blocks first)
            for (const NodeP& b : sealedBase->initBlocks) statements(*b, false);
        for (const NodeP& b : k.initBlocks) statements(*b, false);
        out_->text(" }");
        scopes_.pop_back();
        // The functions of the properties read through them: an override parameter's, then the properties'.
        for (size_t i = 0; i < k.ctorParams.size(); ++i) {
            if (!k.ctorOverride[i]) continue;
            const Param& p = k.ctorParams[i];
            const InterfaceMember* base = memberNamed(inherited, p.name);
            if (!base || !base->property) throw CompileError(p.pos, p.name + " overrides no property of " + k.name + "'s interfaces");
            access(!k.ctorPrivate[i]);
            out_->at(p.pos);
            out_->text(" " + type(p.type) + " " + cppName(p.name) + "() const override { return kompoz_value_" + p.name + "; }");
            if (k.ctorKinds[i] == 'm')
                out_->text(" void " + setterOf(p.name) + "(" + paramType(p.type) + " kompoz_v) const" + (base->property->isVar ? " override" : "") +
                          " { kompoz_value_" + p.name + " = kompoz_v; }");
        }
        for (const ClassProperty* p : props)
            if (p->accessed()) accessorFunctions(*p, memberNamed(inherited, p->name), false, access);
        // Its functions, each seeing its parameters and, by their names, the class's members.
        bool ownToString = false;
        for (const Function* mp : methods) {
            const Function& m = *mp;
            if (m.isAbstract && !k.isAbstract) throw CompileError(m.pos, m.name + " is abstract: its class is too (abstract class " + k.name + ")");
            access(!m.isPrivate);
            out_->at(m.pos);
            const InterfaceMember* base = overridden(inherited, m);
            bool any = m.name == "toString" || m.name == "equals" || m.name == "hashCode";  // (Kotlin's Any's)
            ownToString = ownToString || m.name == "toString";
            bool isConst = true;
            std::string suffix, inheritedType;
            if (base) {  // an interface's or a Kotlin base class's
                suffix = " override", inheritedType = memberType(base->owner, *base);
            } else if (any) {
                if (m.name == "toString" && interfaceRooted) suffix = " override", inheritedType = "std::string";
            } else if (m.overrides && sealedBase && std::any_of(sealedBase->methods.begin(), sealedBase->methods.end(), [&](const Function& f) { return f.name == m.name; })) {
                // (its sealed class's open or abstract fun: its own, in place of that)
            } else if (m.overrides) {  // a C++ base's (a ViewModel's onCleared)
                isConst = false, suffix = " override";
            }
            if (m.isAbstract) suffix += " = 0";
            scopes_.emplace_back();
            for (const Param& p : m.params) declare(p.name, Local);
            functionHead(m, m.isOpen || m.isAbstract ? " virtual " : " ", isConst, suffix, inheritedType, !handle, !m.isOpen && !m.isAbstract);
            if (m.isAbstract) out_->text(";");
            else functionBody(m);
            scopes_.pop_back();
        }
        if (interfaceRooted && !ownToString) {
            access(true);
            out_->text(" std::string toString() const override { return ::kompoz::kt::InterfaceObject::identity(\"" + k.name + "\", this); }");
        }
        scopes_.pop_back();
        if (handle) {
            access(true);
            out_->text(" };");  // (Object)
            innerDefinitions(k, true);
            // The handle: made with the object (an abstract class's only as a subclass's), the same object in every copy,
            // one of a subclass's too, its public functions forwarded. (An inner one's: with its outer object, first.)
            out_->at(k.pos);
            if (!k.isAbstract) {
                out_->text(std::string(k.ctorParams.size() + isInner == 1 ? " explicit " : " ") + k.name + "(" +
                           (isInner ? outers_.back().holder + " kompoz_outer" + (k.ctorParams.empty() ? "" : ", ") : std::string()));
                parameters(true, false);
                out_->text(") : kompoz_object_(std::make_shared<Object>(" + std::string(isInner ? "std::move(kompoz_outer)" : ""));
                for (size_t i = 0; i < k.ctorParams.size(); ++i) out_->text((i || isInner ? ", " : "") + cppName(k.ctorParams[i].name));
                out_->text(")) {}");
            }
            if (k.isOpen || k.isAbstract)
                out_->text(" template <class kompoz_T> requires(!std::is_same_v<kompoz_T, " + k.name + "> && requires(const kompoz_T& t) { { t.kompoz_shared() } -> "
                          "std::convertible_to<std::shared_ptr<Object>>; }) " + k.name + "(const kompoz_T& t) : kompoz_object_(t.kompoz_shared()) {}");
            out_->text(" " + k.name + "(::kompoz::kt::FromObject, std::shared_ptr<Object> o) : kompoz_object_(std::move(o)) {}");
            out_->text(" static " + k.name + " kompoz_from(std::shared_ptr<Object> o) { return " + k.name + "(::kompoz::kt::FromObject{}, std::move(o)); }");
            out_->text(" Object& kompoz_object() const { return *kompoz_object_; }");
            out_->text(" const std::shared_ptr<Object>& kompoz_shared() const { return kompoz_object_; }");
            out_->text(" bool operator==(const " + k.name + "& o) const { return kompoz_object_ == o.kompoz_object_; }");
            if (interfaceRooted || ownToString) out_->text(" std::string toString() const { return kompoz_object_->toString(); }");
            for (const Function* mp : methods) {
                const Function& m = *mp;
                if (m.isPrivate || m.name == "toString") continue;
                out_->at(m.pos);
                if (!m.typeParams.empty()) {  // (a generic one's: what it is given, passed on)
                    out_->text(" template <class... kompoz_A> decltype(auto) " + cppName(m.name) + "(kompoz_A&&... a) const { return kompoz_object_->" +
                              cppName(m.name) + "(std::forward<kompoz_A>(a)...); }");
                    continue;
                }
                bool unitResult = m.result ? m.result->name == "Unit" : !m.expressionBody;
                std::string result = m.result || !m.expressionBody ? (unitResult ? (m.isSuspend ? "::kompoz::Task" : "void")
                                                                                 : m.isSuspend ? "::kompoz::TaskOf<" + type(*m.result) + ">" : type(*m.result))
                                                                   : "decltype(auto)";
                out_->text(" " + result + " " + cppName(m.name) + "(");
                for (size_t i = 0; i < m.params.size(); ++i) {
                    const Param& p = m.params[i];
                    out_->text((i ? ", " : "") + paramTypeOf(p, m.isSuspend) + " " + cppName(p.name));
                    if (p.defaultValue && i >= defaultsFrom(m)) {  // (the defaults where the call arrives)
                        out_->text(" = ");
                        expr(*p.defaultValue);
                    }
                }
                out_->text(") const { return kompoz_object_->" + cppName(m.name) + "(");
                for (size_t i = 0; i < m.params.size(); ++i) out_->text((i ? ", " : "") + cppName(m.params[i].name));
                out_->text("); }");
            }
            operators(k.methods, "kompoz_object_->", k.name);
            out_->text(" private: std::shared_ptr<Object> kompoz_object_;");
        } else {
            operators(k.methods, "", k.name);
            innerDefinitions(k, false);
        }
        out_->text(" };\n");
        outermost.end();
        inClass_ = wasInClass;
        thisClass_ = std::move(wasThisClass);
        innerMade_ = std::move(wasInnerMade);
        superObject_ = wasSuper, superMembers_ = wasSuperMembers;
        classMethods_ = std::move(wasMethods);
        interfaceSelf_ = std::move(wasSelf);
        memberPrefix_ = std::move(wasMemberPrefix);
    }

    // A body's functions put in the order C++ needs where their types are deduced: each after those it calls (a case's
    // own after its sealed class's that call them: label() calling badge()), the rest of the order kept. (Kotlin
    // wants a type written on functions calling each other in a circle: there is always such an order.)
    void calleesFirst(std::vector<std::pair<const ClassProperty*, const Function*>>& members) const {
        std::vector<size_t> slots;  // where the functions are
        for (size_t i = 0; i < members.size(); ++i)
            if (members[i].second) slots.push_back(i);
        std::vector<const Function*> sorted;
        std::set<const Function*> done, visiting;
        auto visit = [&](auto&& self, const Function* f) -> void {
            if (done.contains(f) || !visiting.insert(f).second) return;
            std::set<std::string, std::less<>> used;
            usedNames(f->body.get(), used);
            for (size_t i : slots)
                if (const Function* g = members[i].second; g != f && used.contains(g->name)) self(self, g);
            done.insert(f);
            sorted.push_back(f);
        };
        for (size_t i : slots) visit(visit, members[i].second);
        for (size_t i = 0; i < slots.size(); ++i) members[slots[i]].second = sorted[i];
    }

    // A sealed class or interface of the name (with members or not), if it is one.
    const SealedDecl* sealedNamed(const std::string& name) const {
        auto s = module_.sealeds.find(name);
        if (s == module_.sealeds.end()) s = module_.sealeds.find(std::string_view(name).substr(name.rfind('.') + 1));
        return s == module_.sealeds.end() ? nullptr : s->second;
    }
    // The sealed class (or interface) with members or interfaces of its own a case's supertypes name, if any.
    const SealedDecl* sealedOf(const std::vector<Type>& supers) const {
        for (const Type& t : supers) {
            auto s = module_.sealeds.find(t.name);
            if (s == module_.sealeds.end()) s = module_.sealeds.find(std::string_view(t.name).substr(t.name.rfind('.') + 1));
            if (s != module_.sealeds.end() && (s->second->hasMembers() || !s->second->supers.empty())) return s->second;
        }
        return nullptr;
    }
    // A case's interfaces: its own, and its sealed class's.
    std::vector<Type> caseSupers(const std::vector<Type>& supers) const {
        std::vector<Type> out = supers;
        if (const SealedDecl* s = sealedOf(supers)) out.insert(out.end(), s->supers.begin(), s->supers.end());
        return out;
    }
    static bool sealedHas(const SealedDecl& s, const std::string& n) {
        return std::any_of(s.ctorParams.begin(), s.ctorParams.end(), [&](const Param& p) { return p.name == n; }) ||
               std::any_of(s.properties.begin(), s.properties.end(), [&](const ClassProperty& p) { return p.name == n; }) ||
               std::any_of(s.methods.begin(), s.methods.end(), [&](const Function& m) { return m.name == n; });
    }
    // A case's values of its sealed class's constructor parameters, from what it gives it (Screen("home"), by position
    // or by name; a default for the rest): a data class's members (made after its fields, from them), an object's
    // static ones (`dataClass`: none).
    // Each of a sealed class's constructor parameters' value a case gives (by position or by name, else its default).
    std::vector<const Node*> sealedArguments(const SealedDecl& s, const std::vector<std::string>& names, const std::vector<NodeP>& args,
                                             const std::string& who, Pos pos) {
        if (s.ctorParams.empty() && !args.empty()) throw CompileError(pos, who + ": " + s.name + " takes no arguments");
        std::vector<const Node*> given(s.ctorParams.size(), nullptr);
        size_t next = 0;
        for (size_t i = 0; i < args.size(); ++i) {
            size_t at = next;
            if (i < names.size() && !names[i].empty()) {
                at = s.ctorParams.size();
                for (size_t p = 0; p < s.ctorParams.size(); ++p)
                    if (s.ctorParams[p].name == names[i]) at = p;
                if (at == s.ctorParams.size()) throw CompileError(args[i]->pos, s.name + " has no parameter named " + names[i]);
            } else {
                next = at + 1;
            }
            if (at >= s.ctorParams.size()) throw CompileError(args[i]->pos, "too many arguments for " + s.name);
            given[at] = args[i].get();
        }
        for (size_t i = 0; i < s.ctorParams.size(); ++i) {
            if (!given[i]) given[i] = s.ctorParams[i].defaultValue.get();
            if (!given[i]) throw CompileError(pos, who + " gives " + s.name + " no " + s.ctorParams[i].name + ": " + who + " : " + s.name + "(…)");
        }
        return given;
    }
    void sealedFields(const SealedDecl& s, const SuperCall& call, const std::string& who, Pos pos, const DataClass* dataClass) {
        std::vector<const Node*> given = sealedArguments(s, call.names, call.args, who, pos);
        for (size_t i = 0; i < s.ctorParams.size(); ++i) {
            const Param& p = s.ctorParams[i];
            const Node* value = given[i];
            out_->at(p.pos);
            bool isVar = s.ctorKinds[i] == 'm';
            if (dataClass) {
                out_->text(std::string(isVar ? " mutable " : " ") + type(p.type) + " " + cppName(p.name) + " = ");
                const DataClass* was = std::exchange(dataInit_, dataClass);
                expr(*value, &p.type);
                dataInit_ = was;
            } else {
                out_->text(std::string(" static inline ") + (isVar ? "" : "const ") + type(p.type) + " " + cppName(p.name) + " = ");
                bool wasGlobal = std::exchange(global_, true);
                expr(*value, &p.type);
                global_ = wasGlobal;
            }
            out_->text(";");
        }
    }

    // sealed class Name { … }: a struct holding one of its subclasses (std::variant), made from any of them,
    // equal when they are, written as the one it holds; its nested subclasses inside it (Name.Success is
    // Name::Success), the file's top-level ones before it.
    void sealedDefinition(const SealedDecl& s) {
        out_->at(s.pos);
        out_->text("struct " + s.name + " {");
        companionMembers(s.companion, s.name);
        std::vector<std::string> alternatives;
        for (auto [kind, i] : s.order) {
            if (kind == 'd') {
                classDefinition(s.dataClasses[i]);
                alternatives.push_back(s.name + "::" + s.dataClasses[i].name);
            } else if (kind == 'o') {
                generateObject(*out_, s.objects[i], module_);
                alternatives.push_back(s.name + "::" + s.objects[i].name);
            } else {
                generateClassDecl(*out_, s.classes[i], module_);
                alternatives.push_back(s.name + "::" + s.classes[i].name);
            }
        }
        for (const std::string& t : s.topLevel) alternatives.push_back(qualifierOf(t) + t);
        if (alternatives.empty()) throw CompileError(s.pos, "a sealed class needs a subclass (a data class or an object of its own)");
        std::string list, any;
        for (const std::string& a : alternatives) {
            list += (list.empty() ? "" : ", ") + a;
            any += (any.empty() ? "" : " || ") + std::string("std::is_same_v<std::remove_cvref_t<kompoz_T>, ") + a + ">";
        }
        out_->text(" std::variant<" + list + "> kompoz_value;");
        out_->text(" template <class kompoz_T> requires(" + any + ") " + s.name + "(kompoz_T&& value) : kompoz_value(std::forward<kompoz_T>(value)) {}");
        out_->text(" bool operator==(const " + s.name + "&) const = default;");
        out_->text(" template <class = void> std::string toString() const { return std::visit([](const auto& v) { return ::kompoz::kt::str(v); }, "
                  "kompoz_value); }");
        // Its members, read and called through the case it holds (screen.route, screen.title()): each a value of
        // one type in every case.
        auto read = [&](const std::string& n, const std::optional<Type>& t) {
            std::string get = "KOMPOZ_KT_GET((v), " + cppName(n) + ")";
            out_->text(" " + (t ? type(*t) : std::string("auto")) + " " + cppName(n) + "() const { return std::visit([](const auto& v) { return " +
                       (t ? type(*t) + "(" + get + ")" : "std::remove_cvref_t<decltype(" + get + ")>(" + get + ")") + "; }, kompoz_value); }");
        };
        for (size_t i = 0; i < s.ctorParams.size(); ++i)
            if (s.ctorKinds[i] != ' ') read(s.ctorParams[i].name, s.ctorParams[i].type);
        for (const ClassProperty& p : s.properties)
            if (!p.isPrivate) read(p.name, p.type);
        std::set<std::string, std::less<>> forwarded;
        for (const Function& m : s.methods)
            if (!m.isPrivate && m.name != "toString" && forwarded.insert(m.name).second)
                out_->text(" template <class... kompoz_A> decltype(auto) " + cppName(m.name) +
                           "(kompoz_A&&... a) const { return std::visit([&](const auto& v) { return v." + cppName(m.name) +
                           "(std::forward<kompoz_A>(a)...); }, kompoz_value); }");
        for (const Type& t : s.supers)  // (its interfaces': the case it holds as one; a template, the interface defined later)
            out_->text(" template <class kompoz_I> requires std::is_same_v<kompoz_I, " + plainType(t) + "> operator kompoz_I() const { return std::visit([](const auto& v) { return kompoz_I(v); }, kompoz_value); }");
        out_->text(" };\n");
    }

    // A body's nested declarations, at its top (public): C++ types nested in it, each written as one at the top
    // level is (by a generator of its own: it sees none of the outer one's members, as Kotlin's nested ones don't),
    // what must follow the outermost type (a nested enum's entries) kept for after it.
    // (in a struct of their own before the outer one, kompoz_<Outer>_nested: complete before it, as C++ wants a
    // nested aggregate's defaults to be where the outer one uses them; the outer one names them, nestedAliases)
    static std::string nestedHolder(const std::string& outer) { return "kompoz_" + outer + "_nested"; }
    void nestedDefinitions(const NestedDecls& n, const std::string& outer) {
        if (n.empty()) return;
        std::string holder = nestedHolder(outer);
        std::string after;  // (a rich enum's entries: after the holder, complete there)
        std::string* wasDeferred = std::exchange(deferred_, &after);
        std::string wasQualifier = outerQualifier_;
        out_->text("struct " + holder + " {");
        // (each with what it names alone inside it: its own nested ones)
        std::vector<std::unique_ptr<Module>> modules;
        Module siblings = withNested(module_, outer, n);  // (each sees the others by their names, as Kotlin's do)
        auto inner = [&](const std::string& name, const NestedDecls* own) {
            modules.push_back(std::make_unique<Module>(own ? withNested(siblings, name, *own) : siblings));
            auto g = std::make_unique<Generator>(*out_, *modules.back());
            g->deferred_ = deferred_;
            g->outerQualifier_ = wasQualifier + holder + "::";
            return g;
        };
        for (const auto& c : n.classes) out_->text(" " + templateHead(c->typeParams) + "class " + c->name + ";");  // (declared first: they name each other)
        for (const auto& d : n.dataClasses) out_->text(" " + templateHead(d->typeParams) + "struct " + d->name + ";");
        for (const auto& i : n.interfaces) out_->text(" " + templateHead(i->typeParams) + "class " + i->name + ";");
        for (const auto& o : n.objects) out_->text(" struct " + o->name + ";");
        for (const auto& s : n.sealeds) out_->text(" struct " + s->name + ";");
        for (const auto& e : n.enums) {
            if (e->rich()) inner(e->name, nullptr)->enumDefinition(*e);
            else plainEnum(*out_, *e, true);
        }
        for (const auto& d : n.dataClasses) inner(d->name, &d->nested)->classDefinition(*d);
        for (const auto& i : n.interfaces) inner(i->name, &i->nested)->interfaceDefinition(*i);
        for (const auto& o : n.objects) inner(o->name, &o->nested)->objectDefinition(*o);
        for (const auto& c : n.classes) inner(c->name, &c->nested)->classDeclDefinition(*c);
        for (const auto& s : n.sealeds) inner(s->name, nullptr)->sealedDefinition(*s);
        out_->text(" };" + after + "\n");
        deferred_ = wasDeferred;
    }
    // In the outer type: its nested ones by their names (Catalog::Entry is kompoz_Catalog_nested::Entry).
    void nestedAliases(const NestedDecls& n, const std::string& outer) {
        if (n.empty()) return;
        std::string holder = nestedHolder(outer);
        out_->text(" public:");
        auto alias = [&](const std::string& name, const std::vector<std::string>& typeParams = {}) {
            if (typeParams.empty()) return out_->text(" using " + name + " = " + holder + "::" + name + ";");
            std::string args;  // (a template's: an alias template)
            for (size_t i = 0; i < typeParams.size(); ++i) args += (i ? ", " : "") + typeParams[i];
            out_->text(" " + templateHead(typeParams) + "using " + name + " = " + holder + "::" + name + "<" + args + ">;");
        };
        for (const auto& c : n.classes) alias(c->name, c->typeParams);
        for (const auto& d : n.dataClasses) {
            alias(d->name, d->typeParams);
            if (!d->typeParams.empty())  // (a generic one's factory: Outer::kompoz_make_Box(…))
                out_->text(" template <class... kompoz_A> static auto kompoz_make_" + d->name + "(kompoz_A&&... a) { return " + holder + "::kompoz_make_" +
                           d->name + "(std::forward<kompoz_A>(a)...); }");
        }
        for (const auto& i : n.interfaces) alias(i->name, i->typeParams);
        for (const auto& o : n.objects) alias(o->name);
        for (const auto& e : n.enums) alias(e->name);
        for (const auto& s : n.sealeds) alias(s->name);
    }
    // Where an outermost type's definition starts: what its nested ones leave for after it is kept from here; `end`
    // writes it.
    struct Outermost {
        Generator& g;
        std::string buffer;
        bool owns;
        explicit Outermost(Generator& gen) : g(gen), owns(!gen.deferred_) {
            if (owns) g.deferred_ = &buffer;
        }
        ~Outermost() {
            if (owns) g.deferred_ = nullptr;
        }
        void end() {
            if (!owns) return;
            g.deferred_ = nullptr;
            if (!buffer.empty()) g.out_->text(buffer + "\n");
        }
    };
    // C++'s enum class of entries alone, and Kotlin's name, ordinal and count for it (found by ADL: friends of the
    // class it is nested in, else the namespace's).
    static void plainEnum(Output& out, const EnumClass& e, bool nested) {
        out.at(e.pos);
        std::string fn = nested ? " friend " : " [[maybe_unused]] inline ";
        out.text(std::string(nested ? " " : "") + "enum class " + e.name + " {");
        for (size_t i = 0; i < e.entries.size(); ++i) out.text(std::string(i ? ", " : " ") + e.entries[i]);
        out.text(" };" + fn + "std::string kompoz_kt_property_name(" + e.name + " kompoz_e) { static const char* const names[] = {");
        for (size_t i = 0; i < e.entries.size(); ++i) out.text(std::string(i ? ", " : "") + "\"" + e.entries[i] + "\"");
        out.text("}; return names[static_cast<int>(kompoz_e)]; }" + fn + "constexpr int kompoz_kt_property_ordinal(" + e.name +
                 " kompoz_e) { return static_cast<int>(kompoz_e); }" + fn + "constexpr int kompoz_enumCount(" + e.name + ") { return " +
                 std::to_string(e.entries.size()) + "; }" + (nested ? "" : "\n"));
    }

    // enum class Name(val a: T) { A(1), B(2); val b = …; fun f() = … }: a struct of its fields (the constructor's
    // parameters, then the body's properties), its entries its static constants (Name::A, defined after it: the
    // struct isn't complete inside), == by entry, its name and ordinal (found by ADL), its functions const.
    // The entries' bodies of an enum's function `m`, each run for its entry (its parameters, named as the
    // entry's names them, the function's); a Unit one's returns there.
    template <class EntryIs>
    void entryOverrides(const Function& m, const std::vector<std::pair<size_t, const Function*>>& overridden, const EntryIs& entryIs) {
        bool unit = m.result ? m.result->name == "Unit" : !m.expressionBody && m.name != "toString";
        for (auto [i, f] : overridden) {
            out_->text(" " + entryIs(i) + " {");
            scopes_.emplace_back();
            for (size_t j = 0; j < f->params.size(); ++j) {
                declare(f->params[j].name, Local);
                if (f->params[j].name != m.params[j].name)
                    out_->text(" auto&& " + cppName(f->params[j].name) + " = " + cppName(m.params[j].name) + ";");
            }
            statements(*f->body, false);
            if (unit) out_->text(" return;");
            out_->text(" }");
            scopes_.pop_back();
        }
    }
    void enumDefinition(const EnumClass& e) {
        nestedDefinitions(e.nested, e.name);
        auto own = fieldNames(e.properties);
        for (const Param& p : e.ctorParams) own.insert(p.name);
        OwnFields ownFields(*this, std::move(own));
        out_->at(e.pos);
        out_->text("struct " + e.name + " {");
        companionMembers(e.companion, e.name);
        nestedAliases(e.nested, e.name);
        for (const auto& c : e.nested.innerClasses) out_->text(" class " + c->name + ";");
        auto wasInnerMade = innerMade_;  // (its inner classes: made with a copy of its entry, a value)
        for (const auto& c : e.nested.innerClasses) innerMade_[c->name] = {c.get(), "std::make_shared<" + e.name + ">(*this)"};
        out_->text(" enum class kompoz_Entry {");
        for (size_t i = 0; i < e.entries.size(); ++i) out_->text(std::string(i ? ", " : " ") + e.entries[i]);
        out_->text(" }; kompoz_Entry kompoz_entry{};");
        bool wasInClass = std::exchange(inClass_, true);
        scopes_.emplace_back();
        for (const Param& p : e.ctorParams) {
            out_->at(p.pos);
            out_->text(" " + type(p.type) + " " + cppName(p.name) + "{};");
        }
        auto wasMethods = std::exchange(classMethods_, methodsOf(e.methods, {}));
        knowCompanion(e.companion);
        declareMembers(e.properties, std::vector<Type>{}, false);
        for (const char* own : {"name", "ordinal"})  // (its entry's: name(), ordinal())
            if (!lookup(own)) declare(own, Accessor);
        // What entries' bodies override (PLUS { override fun apply(…) = … }): each such member a function choosing by
        // the entry (its body's, else the class's own), a property read through one (name()).
        auto entryMethods = [&](const std::string& name, size_t params) {
            std::vector<std::pair<size_t, const Function*>> out;
            for (size_t i = 0; i < e.entryBodies.size(); ++i)
                if (e.entryBodies[i])
                    for (const Function& f : e.entryBodies[i]->methods)
                        if (f.name == name && f.params.size() == params) out.emplace_back(i, &f);
            return out;
        };
        auto entryProperties = [&](const std::string& name) {
            std::vector<std::pair<size_t, const ClassProperty*>> out;
            for (size_t i = 0; i < e.entryBodies.size(); ++i)
                if (e.entryBodies[i])
                    for (const ClassProperty& q : e.entryBodies[i]->properties)
                        if (q.name == name) out.emplace_back(i, &q);
            return out;
        };
        for (const auto& b : e.entryBodies) {
            if (!b) continue;
            for (const Function& f : b->methods)
                if (f.name != "toString" && std::none_of(e.methods.begin(), e.methods.end(), [&](const Function& m) {
                        return m.name == f.name && m.params.size() == f.params.size() && (m.isOpen || m.isAbstract);
                    }))
                    throw CompileError(f.pos, f.name + " overrides no open or abstract fun of " + e.name);
            for (const ClassProperty& q : b->properties) {
                auto base = std::find_if(e.properties.begin(), e.properties.end(), [&](const ClassProperty& p) { return p.name == q.name; });
                if (base == e.properties.end() || !(base->isOpen || base->isAbstract))
                    throw CompileError(q.pos, q.name + " overrides no open or abstract val of " + e.name);
                if (base->isVar || q.isVar) throw CompileError(q.pos, "an enum entry's body overrides a val here (" + q.name + " is a var)");
            }
        }
        for (const ClassProperty& p : e.properties)
            if (!entryProperties(p.name).empty()) declare(p.name, Accessor);
        auto entryIs = [&](size_t i) { return "if (kompoz_entry == kompoz_Entry::" + e.entries[i] + ")"; };
        auto access = [](bool) {};
        for (size_t index : e.order) {
            if (index % 2 == 0) {
                const ClassProperty& p = e.properties[index / 2];
                if (auto overridden = entryProperties(p.name); !overridden.empty()) {
                    // open val sign = "?" with PLUS { override val sign = "+" }: sign() giving the entry's
                    out_->at(p.pos);
                    out_->text(" " + (p.type ? type(*p.type) : std::string("auto")) + " " + cppName(p.name) + "() const {");
                    auto value = [&](const ClassProperty& q) {
                        if (q.getter) {
                            out_->text(" {");
                            statements(*q.getter, false);
                            out_->text(" }");
                        } else {
                            out_->text(" return ");
                            expr(*q.init, p.type ? &*p.type : nullptr);
                            out_->text(";");
                        }
                    };
                    for (auto [i, q] : overridden) {
                        out_->text(" " + entryIs(i));
                        value(*q);
                    }
                    if (p.isAbstract) out_->text(" throw ::kompoz::kt::IllegalStateException(\"" + p.name + "\");");
                    else value(p);
                    out_->text(" }");
                    continue;
                }
                if (p.accessed() && !p.delegated && !stores(p)) {
                    accessorFunctions(p, nullptr, false, access, false);
                    continue;
                }
                out_->at(p.pos);
                std::string storage = p.accessed() ? storageOf(p) : cppName(p.name);
                if (!p.type && !p.init) throw CompileError(p.pos, "give " + p.name + " a type");
                out_->text(std::string(" mutable ") + (p.type && !p.delegated ? type(*p.type) : std::string("std::remove_cvref_t<decltype(")));
                if (!p.type || p.delegated) {
                    bool wasTyping = std::exchange(classTyping_, p.delegated);
                    expr(*p.init);
                    classTyping_ = wasTyping;
                    out_->text(")>");
                }
                out_->text(" " + storage + " = ");
                if (p.init) expr(*p.init, p.type && !p.delegated ? &*p.type : nullptr);
                else out_->text("{}");
                out_->text(";");
                if (p.accessed()) accessorFunctions(p, nullptr, false, access, false);
            } else {
                const Function& m = e.methods[index / 2];
                out_->at(m.pos);
                scopes_.emplace_back();
                for (const Param& p : m.params) declare(p.name, Local);
                functionHead(m, " ", true, "", "", true, true);
                if (auto overridden = entryMethods(m.name, m.params.size()); !overridden.empty() || m.isAbstract) {
                    out_->text(" {");
                    entryOverrides(m, overridden, entryIs);
                    if (m.isAbstract) out_->text(" throw ::kompoz::kt::IllegalStateException(\"" + m.name + "\");");
                    else statements(*m.body, false);
                    out_->text(" }");
                } else {
                    functionBody(m);
                }
                scopes_.pop_back();
            }
        }
        bool entryToString = !entryMethods("toString", 0).empty();
        if (entryToString && std::none_of(e.methods.begin(), e.methods.end(), [](const Function& m) { return m.name == "toString"; })) {
            out_->text(" std::string toString() const {");  // (an entry's own, else its name)
            Function toString;
            toString.name = "toString";
            entryOverrides(toString, entryMethods("toString", 0), entryIs);
            out_->text(" return kompoz_kt_property_name(*this); }");
        }
        for (size_t i = 0; i < e.initBlocks.size(); ++i) {
            out_->text(" [[no_unique_address]] ::kompoz::kt::InitBlock kompoz_init_" + std::to_string(i) + " = " + capture("[=]") + "() {");
            statements(*e.initBlocks[i], false);
            out_->text(" };");
        }
        scopes_.pop_back();
        classMethods_ = std::move(wasMethods);
        inClass_ = wasInClass;
        if (!e.nested.innerClasses.empty()) {
            Module module = withNested(module_, e.name, e.nested);
            module.enums[e.name] = &e;
            innerDefinitions(e.name, e.nested, module, "std::shared_ptr<" + e.name + ">", "");
        }
        innerMade_ = std::move(wasInnerMade);
        for (const std::string& n : e.entries) out_->text(" static const " + e.name + " " + n + ";");
        out_->text(" bool operator==(const " + e.name + "& o) const { return kompoz_entry == o.kompoz_entry; }");
        out_->text(" std::string name() const { return kompoz_kt_property_name(*this); } int ordinal() const { return static_cast<int>(kompoz_entry); }");
        if (std::none_of(e.methods.begin(), e.methods.end(), [](const Function& m) { return m.name == "toString"; }) && !entryToString)
            out_->text(" std::string toString() const { return kompoz_kt_property_name(*this); }");
        out_->text(" static const std::vector<" + e.name + ">& kompoz_entries() { static const std::vector<" + e.name + "> all{");
        for (size_t i = 0; i < e.entries.size(); ++i) out_->text((i ? ", " : "") + e.entries[i]);
        out_->text("}; return all; }");
        out_->text(" friend std::string kompoz_kt_property_name(const " + e.name + "& kompoz_e) { static const char* const names[] = {");
        for (size_t i = 0; i < e.entries.size(); ++i) out_->text(std::string(i ? ", " : "") + "\"" + e.entries[i] + "\"");
        out_->text("}; return names[static_cast<int>(kompoz_e.kompoz_entry)]; } friend int kompoz_kt_property_ordinal(const " + e.name +
                  "& kompoz_e) { return static_cast<int>(kompoz_e.kompoz_entry); } };");
        // The entries: each made with its arguments, by position or by name (the constructor's defaults for the rest);
        // a nested enum's after the outermost type (its struct isn't complete before), named with its outer ones.
        Output entriesOut(out_->file());
        Output* wasOut = nullptr;
        std::string full = outerQualifier_ + e.name;
        if (!outerQualifier_.empty()) wasOut = std::exchange(out_, &entriesOut);
        for (size_t i = 0; i < e.entries.size(); ++i) {
            std::vector<const Node*> given(e.ctorParams.size(), nullptr);
            size_t next = 0;
            for (size_t a = 0; a < e.entryArgs[i].size(); ++a) {
                const std::string& n = e.entryArgNames[i][a];
                size_t at = next;
                if (!n.empty()) {
                    at = e.ctorParams.size();
                    for (size_t f = 0; f < e.ctorParams.size(); ++f)
                        if (e.ctorParams[f].name == n) at = f;
                    if (at == e.ctorParams.size()) throw CompileError(e.entryArgs[i][a]->pos, e.name + " has no parameter named " + n);
                } else {
                    next = at + 1;
                }
                if (at >= e.ctorParams.size()) throw CompileError(e.entryArgs[i][a]->pos, "too many arguments for " + e.entries[i]);
                given[at] = e.entryArgs[i][a].get();
            }
            out_->text(" inline const " + full + " " + full + "::" + e.entries[i] + "{" + full + "::kompoz_Entry::" + e.entries[i]);
            global_ = true;
            for (size_t f = 0; f < e.ctorParams.size(); ++f) {
                const Node* arg = given[f] ? given[f] : e.ctorParams[f].defaultValue.get();
                if (!arg) throw CompileError(e.pos, e.entries[i] + " needs " + e.ctorParams[f].name);
                out_->text(", ");
                expr(*arg, &e.ctorParams[f].type);
            }
            global_ = false;
            out_->text("};");
        }
        if (wasOut) {
            out_ = wasOut;
            *deferred_ += std::move(entriesOut).str();
        } else {
            out_->text("\n");
        }
    }

    // Kotlin's operator funs as C++'s operators, each calling its function: `call` is how they reach it ("": a
    // member's own; "kompoz_object_->": a handle's object's), `self` the type (++ and += set the value it is to the
    // new one, as Kotlin's x++ and x += y without a plusAssign do). get(i) is a[i], invoke(…) is f(…), compareTo(x)
    // gives <, <=, >, >=; contains, set and the rest are reached by their names (kt::contains, kt::setAt).
    void operators(const std::vector<Function>& methods, const std::string& call, const std::string& self) {
        static const std::map<std::string, std::string, std::less<>> binary = {{"plus", "+"}, {"minus", "-"}, {"times", "*"},
                                                                               {"div", "/"},  {"rem", "%"},   {"mod", "%"}};
        static const std::map<std::string, std::string, std::less<>> unary = {{"unaryMinus", "-"}, {"unaryPlus", "+"}, {"not", "!"}};
        static const std::map<std::string, std::string, std::less<>> assign = {{"plusAssign", "+="}, {"minusAssign", "-="}, {"timesAssign", "*="},
                                                                               {"divAssign", "/="},  {"remAssign", "%="}};
        auto has = [&](const std::string& n) { return std::any_of(methods.begin(), methods.end(), [&](const Function& f) { return f.name == n; }); };
        for (const Function& m : methods) {
            if (!m.isOperator || m.isPrivate) continue;
            std::string params, args;
            for (size_t i = 0; i < m.params.size(); ++i) {
                params += (i ? ", " : "") + paramType(m.params[i].type) + " " + cppName(m.params[i].name);
                args += (i ? ", " : "") + cppName(m.params[i].name);
            }
            std::string to = call + cppName(m.name) + "(" + args + ")";
            if (auto b = binary.find(m.name); b != binary.end() && m.params.size() == 1) {
                out_->text(" decltype(auto) operator" + b->second + "(" + params + ") const { return " + to + "; }");
                if (!has(m.name + "Assign"))
                    out_->text(" " + self + "& operator" + b->second + "=(" + params + ") { *this = " + to + "; return *this; }");
            } else if (auto u = unary.find(m.name); u != unary.end() && m.params.empty()) {
                out_->text(" decltype(auto) operator" + u->second + "() const { return " + to + "; }");
            } else if ((m.name == "inc" || m.name == "dec") && m.params.empty()) {
                std::string op = m.name == "inc" ? "++" : "--";
                out_->text(" " + self + "& operator" + op + "() { *this = " + to + "; return *this; } " + self + " operator" + op + "(int) { " + self +
                           " kompoz_old = *this; *this = " + to + "; return kompoz_old; }");
            } else if (auto a = assign.find(m.name); a != assign.end() && m.params.size() == 1) {
                out_->text(" void operator" + a->second + "(" + params + ") const { " + to + "; }");
            } else if (m.name == "get" && m.params.size() == 1) {
                out_->text(" decltype(auto) operator[](" + params + ") const { return " + to + "; }");
            } else if (m.name == "invoke") {
                out_->text(" decltype(auto) operator()(" + params + ") const { return " + to + "; }");
            } else if (m.name == "compareTo" && m.params.size() == 1) {
                out_->text(" auto operator<=>(" + params + ") const { return " + to + " <=> 0; }");
            }
        }
    }
    // A top-level extension operator fun (operator fun Vec.times(k: Int)): C++'s operator beside it, calling it.
    void extensionOperator(const Function& f) {
        static const std::map<std::string, std::string, std::less<>> binary = {{"plus", "+"}, {"minus", "-"}, {"times", "*"},
                                                                               {"div", "/"},  {"rem", "%"},   {"mod", "%"}};
        static const std::map<std::string, std::string, std::less<>> unary = {{"unaryMinus", "-"}, {"unaryPlus", "+"}, {"not", "!"}};
        if (!f.isOperator || !f.receiver || !f.typeParams.empty()) return;
        std::string receiver = "const " + type(*f.receiver) + "& kompoz_self";
        std::string head = std::string(f.isPrivate ? "static" : "inline") + " decltype(auto) operator";
        if (auto b = binary.find(f.name); b != binary.end() && f.params.size() == 1)
            out_->text(head + b->second + "(" + receiver + ", " + paramType(f.params[0].type) + " kompoz_other) { return " + qualifierOf(f.name) +
                       cppName(f.name) + "(kompoz_self, kompoz_other); }\n");
        else if (auto u = unary.find(f.name); u != unary.end() && f.params.empty())
            out_->text(head + u->second + "(" + receiver + ") { return " + qualifierOf(f.name) + cppName(f.name) + "(kompoz_self); }\n");
    }

    // typealias Names = List<String>: using Names = std::vector<std::string> (typealias Pairs<T> = …: a template's).
    void aliasDefinition(const TypeAlias& a) {
        out_->at(a.pos);
        out_->text(templateHead(a.typeParams) + "using " + a.name + " = " + type(a.type) + ";\n");
    }
    // A type as what an alias of it stands for (typealias Mapper = (Int) -> String: a lambda given one gives a value).
    const Type* resolved(const Type* t) {
        for (int depth = 0; t && depth < 16; ++depth) {
            auto a = module_.aliases.find(t->name);
            if (t->function || a == module_.aliases.end()) return t;
            Bindings bind;
            for (size_t i = 0; i < a->second->typeParams.size() && i < t->args.size(); ++i) bind.emplace(a->second->typeParams[i], t->args[i]);
            aliasTypes_.push_back(substitute(a->second->type, bind));
            if (t->nullable) aliasTypes_.back().nullable = true;
            t = &aliasTypes_.back();
        }
        return t;
    }

    // object Name { … }: a struct of its own, its members static (Kotlin's object is one value: Name.a is
    // Name::a, Name.f() is Name::f()), its init blocks run once, as the program starts; a property by a
    // delegate or with accessors read through static functions (Name.a is Name::a()). One implementing
    // interfaces is converted to them through its kompoz_Object (interfaceAdapter), the interfaces' functions
    // it doesn't declare reached as its own (static, forwarded to that object).
    void objectDefinition(const ObjectDecl& o) {
        bool route = std::find(o.annotations.begin(), o.annotations.end(), "Serializable") != o.annotations.end();
        bool ownToString = std::any_of(o.methods.begin(), o.methods.end(), [](const Function& m) { return m.name == "toString"; });
        out_->at(o.pos);
        Outermost outermost(*this);
        nestedDefinitions(o.nested, o.name);
        auto fields = fieldNames(o.properties);
        if (const SealedDecl* s = sealedOf(o.supers)) {
            for (const Param& p : s->ctorParams) fields.insert(p.name);
            for (const ClassProperty& p : s->properties) fields.insert(p.name);
        }
        OwnFields ownFields(*this, std::move(fields));
        out_->text("struct " + o.name + " {" + (route ? " KOMPOZ_ROUTE(" + o.name + ")" : std::string()) + " bool operator==(const " +
                  o.name + "&) const = default;");
        nestedAliases(o.nested, o.name);
        out_->text(" public:");
        if (!ownToString) out_->text(" template <class = void> std::string toString() const { return \"" + o.name + "\"; }");
        bool isPublic = true;
        auto access = [&](bool wantPublic) {
            if (wantPublic != isPublic) out_->text(wantPublic ? " public:" : " private:");
            isPublic = wantPublic;
        };
        // A sealed class's case: its constructor's values and members (static, as the object's), then its own.
        const SealedDecl* sealed = sealedOf(o.supers);
        std::vector<Type> supers = caseSupers(o.supers);
        InterfaceMembers inherited = interfaceMembers(supers);
        auto wasMethods = std::exchange(classMethods_, methodsOf(o.methods, inherited));
        std::string wasSelf = std::exchange(staticSelf_, o.name + "{}");  // (`this` in its static functions: its value)
        scopes_.emplace_back();
        if (sealed) {
            // (its sealed class's members first, those it doesn't override nor the abstract ones; its functions each
            // after those it calls, as a data class case's)
            declareMembers(sealed->properties, std::vector<Type>{}, true);
            sealedFields(*sealed, o.superCall, o.name, o.pos, nullptr);
            declareMembers(o.properties, supers, true);
            std::vector<std::pair<const ClassProperty*, const Function*>> members;
            for (size_t index : sealed->memberOrder) {
                if (index % 2 == 0) {
                    const ClassProperty& p = sealed->properties[index / 2];
                    if (!p.isAbstract && std::none_of(o.properties.begin(), o.properties.end(), [&](const ClassProperty& q) { return q.name == p.name; }))
                        members.emplace_back(&p, nullptr);
                } else {
                    const Function& m = sealed->methods[index / 2];
                    if (!m.isAbstract && std::none_of(o.methods.begin(), o.methods.end(), [&](const Function& f) { return f.name == m.name && f.params.size() == m.params.size(); })) {
                        members.emplace_back(nullptr, &m);
                        classMethods_[m.name].push_back(&m);
                    }
                }
            }
            for (size_t index : o.order) members.emplace_back(index % 2 == 0 ? &o.properties[index / 2] : nullptr, index % 2 == 0 ? nullptr : &o.methods[index / 2]);
            calleesFirst(members);
            objectMembers(members, {&sealed->initBlocks, &o.initBlocks}, false, inherited, access);
        } else {
            declareMembers(o.properties, supers, true);
            objectMembers(o, inherited, access);
        }
        scopes_.pop_back();
        staticSelf_ = std::move(wasSelf);
        access(true);
        auto own = [&](const std::string& n) {
            return std::any_of(o.properties.begin(), o.properties.end(), [&](const ClassProperty& p) { return p.name == n; }) ||
                   std::any_of(o.methods.begin(), o.methods.end(), [&](const Function& m) { return m.name == n; }) || (sealed && sealedHas(*sealed, n));
        };
        interfaceAdapter(o.name, supers, true, own, o.pos);
        classMethods_ = std::move(wasMethods);
        out_->text(" };\n");
        outermost.end();
    }

    // An object's members, static: a val/var its value (its type the value's unless one is written), one by a
    // delegate or with accessors kept aside and read through static functions, a fun a static function, an init
    // block run once as the program starts.
    template <class Access>
    void objectMembers(const ObjectDecl& o, const InterfaceMembers& inherited, Access& access) {
        std::vector<std::pair<const ClassProperty*, const Function*>> members;
        for (size_t index : o.order) members.emplace_back(index % 2 == 0 ? &o.properties[index / 2] : nullptr, index % 2 == 0 ? nullptr : &o.methods[index / 2]);
        objectMembers(members, {&o.initBlocks}, o.isCompanion, inherited, access);
    }
    // (`members`: in order, a property or a function each; `initBlocks`: each list's, named apart)
    template <class Access>
    void objectMembers(const std::vector<std::pair<const ClassProperty*, const Function*>>& members, const std::vector<const std::vector<NodeP>*>& initLists,
                       bool isCompanion, const InterfaceMembers& inherited, Access& access) {
        for (auto [property, method] : members) {
            if (property) {  // val / var: a static member, its type the value's unless one is written
                const ClassProperty& p = *property;
                bool accessed = p.delegated || p.getter || p.setter || p.privateSetter;
                if (accessed && !p.delegated && !stores(p)) {
                    accessorFunctions(p, memberNamed(inherited, p.name), true, access);
                    continue;
                }
                if (isCompanion && !accessed) {
                    // A companion's: a static function holding the value, made the first time it is read (the
                    // class isn't complete where its members are declared: val Zero = Money(0)); var's setter too.
                    access(!p.isPrivate);
                    out_->at(p.pos);
                    std::string t = p.type ? type(*p.type) : std::string("auto");
                    out_->text(std::string(" static ") + (p.isVar ? "" : "const ") + (p.type ? t : std::string("auto")) + "& " + cppName(p.name) +
                              "() { static " + (p.isVar ? "" : "const ") + t + " kompoz_value = ");
                    scopes_.emplace_back();
                    if (p.init) expr(*p.init, p.type ? &*p.type : nullptr);
                    else out_->text(t + "{}");
                    scopes_.pop_back();
                    out_->text("; return kompoz_value; }");
                    if (p.isVar)
                        out_->text(" template <class kompoz_V> static void " + setterOf(p.name) + "(kompoz_V&& kompoz_v) { " + cppName(p.name) +
                                  "() = std::forward<kompoz_V>(kompoz_v); }");
                    continue;
                }
                access(!accessed && !p.isPrivate);
                out_->at(p.pos);
                std::string t = p.type && !p.delegated ? type(*p.type) : std::string("auto");
                out_->text(std::string(" static inline ") + (p.isVar || accessed ? "" : "const ") + t + " " + (accessed ? storageOf(p) : cppName(p.name)) + " = ");
                global_ = true;
                scopes_.emplace_back();
                if (p.init) expr(*p.init, p.type && !p.delegated ? &*p.type : nullptr);
                else out_->text(t + "{}");
                scopes_.pop_back();
                global_ = false;
                out_->text(";");
                if (accessed) accessorFunctions(p, memberNamed(inherited, p.name), true, access);
            } else {  // fun: a static function
                const Function& m = *method;
                access(!m.isPrivate);
                out_->at(m.pos);
                scopes_.emplace_back();
                for (const Param& p : m.params) declare(p.name, Local);
                functionHead(m, " static ", false, "", "", true, true);
                functionBody(m);
                scopes_.pop_back();
            }
        }
        for (size_t list = 0; list < initLists.size(); ++list)
            for (size_t i = 0; i < initLists[list]->size(); ++i) {
                access(false);
                out_->text(" static inline const bool kompoz_init_" + (list ? std::to_string(list) + "_" : std::string()) + std::to_string(i) + " = [] {");
                scopes_.emplace_back();
                statements(*(*initLists[list])[i], false);
                scopes_.pop_back();
                out_->text(" return true; }();");
            }
    }

    // A class's companion object: its members the class's own, static (Name.MAX is Name::MAX, Name.create() is
    // Name::create(); inside the class, MAX and create() alone).
    // (`owner`: the class's name. A companion implementing interfaces, companion object : Factory<Foo>: an object of
    // them, kompoz_Companion (Foo as a value is it), reaching its members, which forward to the class's static ones;
    // the interfaces' members with bodies it doesn't declare are the class's static ones too, Foo.many(2).)
    void companionMembers(const std::shared_ptr<ObjectDecl>& c, const std::string& owner) {
        if (!c) return;
        bool isPublic = false;
        auto access = [&](bool wantPublic) {
            if (wantPublic != isPublic) out_->text(wantPublic ? " public:" : " private:");
            isPublic = wantPublic;
        };
        access(true);
        bool wasInClass = std::exchange(inClass_, false);  // (static: no this)
        scopes_.emplace_back();
        declareCompanion(*c);
        objectMembers(*c, {}, access);
        scopes_.pop_back();
        inClass_ = wasInClass;
        access(true);
        if (c->supers.empty()) return;
        auto has = [&](const std::string& n) {
            return std::any_of(c->properties.begin(), c->properties.end(), [&](const ClassProperty& p) { return p.name == n; }) ||
                   std::any_of(c->methods.begin(), c->methods.end(), [&](const Function& m) { return m.name == n; });
        };
        out_->text(" struct kompoz_Companion { std::string toString() const { return \"" + owner + ".Companion\"; }"
                   " bool operator==(const kompoz_Companion&) const = default;");
        for (const ClassProperty& p : c->properties)
            if (!p.isPrivate) out_->text(" static decltype(auto) " + cppName(p.name) + "() { return " + owner + "::" + cppName(p.name) + "(); }");
        for (const Function& m : c->methods)
            if (!m.isPrivate)
                out_->text(" template <class... kompoz_A> static decltype(auto) " + cppName(m.name) + "(kompoz_A&&... a) { return " + owner + "::" +
                           cppName(m.name) + "(std::forward<kompoz_A>(a)...); }");
        interfaceAdapter("kompoz_Companion", c->supers, true, has, c->pos);
        out_->text(" };");
        for (auto& [n, m] : interfaceMembers(c->supers))
            if (m.hasBody && !has(n))
                out_->text(" template <class... kompoz_A> static decltype(auto) " + cppName(n) + "(kompoz_A&&... a) { return kompoz_Companion::" + cppName(n) +
                           "(std::forward<kompoz_A>(a)...); }");
    }
    // What a class's body knows of its companion: its functions (called by their names) and its properties read
    // through functions.
    void knowCompanion(const std::shared_ptr<ObjectDecl>& c) {
        if (!c) return;
        for (const Function& m : c->methods) classMethods_[m.name].push_back(&m);
        declareCompanion(*c);
    }
    // A companion's properties are all read through its static functions (a delegated one through its delegate).
    void declareCompanion(const ObjectDecl& c) {
        for (const ClassProperty& p : c.properties) declare(p.name, p.delegated ? MemberDelegated : Accessor);
    }

    // Whether Name.member of an object is read through a static function (Name::member()): a property by a
    // delegate or with accessors, or an interface's with a getter that it doesn't declare.
    bool objectAccessor(const std::string& object, const std::string& member) {
        auto o = module_.objectDecls.find(object);
        if (o == module_.objectDecls.end()) return false;
        for (const ClassProperty& p : o->second->properties)
            if (p.name == member) return o->second->isCompanion || p.delegated || p.getter || p.setter || p.privateSetter;
        for (const Function& f : o->second->methods)
            if (f.name == member) return false;
        const InterfaceMember* m = memberNamed(interfaceMembers(o->second->supers), member);
        return m && m->property && m->hasBody;
    }

    // val name = value at the top: a constant of the namespace (private: of the file), its type the
    // value's unless one is written.
    // val name = value / var name = value at the top: a variable of the namespace (private: of the file), a val's
    // const; val x by lazy { … } / var n by mutableStateOf(0): it holds the delegate, read and set through it.
    void property(const Property& p) {
        const Node& d = *p.decl;
        out_->at(p.pos);
        std::string t = d.type && !d.delegated ? type(*d.type) : std::string("auto");
        out_->text(std::string(p.isPrivate ? "static" : "inline") + (d.flag || d.delegated ? " " : " const ") + t + " " + cppName(p.name) + " = ");
        global_ = true;
        scopes_.emplace_back();
        expr(*d.kids[0], d.type && !d.delegated ? &*d.type : nullptr);
        scopes_.pop_back();
        global_ = false;
        out_->text(";\n");
    }

    void definition(const Function& f) {
        bool unit = !f.result || f.result->name == "Unit";
        currentResult_ = f.result ? &*f.result : nullptr;
        if (!f.composable) {  // (no declaration before it: its defaults are here)
            scopes_.emplace_back();
            for (const Param& p : f.params) declare(p.name, Local);  // (a suspend fun's result type may read them)
            signature(f, true);
            out_->text(" {");
            if (f.receiver) {  // its members reached by their names, as a lambda's receiver's
                receivers_.push_back(qualified(f.receiver->name));
                receiverConst_.push_back(true);
                receiverLabels_.push_back({});
                out_->text(" [[maybe_unused]] auto* kompoz_this_1 = &kompoz_receiver_1;");
            }
            bool wasSuspend = std::exchange(suspend_, f.isSuspend);
            statements(*f.body, false);
            if (f.isSuspend && !f.expressionBody && (!f.result || f.result->name == "Unit")) out_->text(" co_return;");
            suspend_ = wasSuspend;
            out_->text(" }\n");
            extensionOperator(f);
            receivers_.clear();
            receiverConst_.clear();
            receiverLabels_.clear();
            scopes_.pop_back();
            if (f.setter) definition(*f.setter);  // (an extension var's: setName(receiver, value))
            return;
        }
        signature(f, false);
        out_->text(" {");
        scopes_.emplace_back();
        for (const Param& p : f.params) declare(p.name, Local);
        if (unit) {
            // Restartable and skippable, as Compose's compiler makes a Unit @Kompozable function.
            out_->text(" ::kompoz::kompozable(site, ");
            for (const Param& p : f.params) out_->text(cppName(p.name) + ", ");
            out_->text("[=] {");
            statements(*f.body, false);
            out_->text(" });");
        } else {
            // Not restartable (what it reads is its caller's), but a group of its own: what it
            // remembers is its, however many times its caller calls it from one place.
            out_->text(" ::kompoz::KompozableScope kompoz_scope(site);");
            statements(*f.body, true);
        }
        scopes_.pop_back();
        out_->text(" }\n");
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
            out_->text("static const ::kompoz::detail::PreviewRegistration kompoz_preview_" + f.name + "{{\"" + f.name + "\", ::kompoz::PreviewSpec{" + spec +
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
        for (size_t r = receivers_.size(); r > 0; --r) {
            std::string_view type = receivers_[r - 1];
            if (size_t lt = type.find('<'); lt != std::string_view::npos) type = type.substr(0, lt);  // (std::vector<T>: a generic one's)
            if (size_t colons = type.rfind("::"); colons != std::string_view::npos) type = type.substr(colons + 2);  // (ImageVector::Builder)
            if (module_.structs && module_.structs->member(type, name)) return r;
            if (kotlinMember(type, name)) return r;  // (an extension on a class written in Kotlin: its fields and functions)
        }
        return 0;
    }
    // Whether a type written in Kotlin here has a member of the name: a data class's field, property or function, a
    // class's val/var parameter, property or function, an interface's.
    bool kotlinMember(std::string_view type, const std::string& name) const {
        auto prop = [&](const std::vector<ClassProperty>& ps) { return std::any_of(ps.begin(), ps.end(), [&](const ClassProperty& p) { return p.name == name; }); };
        auto fun = [&](const std::vector<Function>& fs) { return std::any_of(fs.begin(), fs.end(), [&](const Function& f) { return f.name == name; }); };
        if (auto d = module_.classes.find(type); d != module_.classes.end())
            return std::any_of(d->second->fields.begin(), d->second->fields.end(), [&](const Param& f) { return f.name == name; }) ||
                   prop(d->second->properties) || fun(d->second->methods);
        if (auto c = module_.classDecls.find(type); c != module_.classDecls.end()) {
            for (size_t i = 0; i < c->second->ctorParams.size(); ++i)
                if (c->second->ctorKinds[i] != ' ' && c->second->ctorParams[i].name == name) return true;
            if (prop(c->second->properties) || fun(c->second->methods)) return true;
            for (const Type& t : c->second->interfaces)  // (and what it inherits: its interfaces', its Kotlin base's)
                if (t.name != type && kotlinMember(t.name, name)) return true;
            return c->second->base && c->second->base->name != type && kotlinMember(c->second->base->name, name);
        }
        if (auto e = module_.enums.find(type); e != module_.enums.end())
            return std::any_of(e->second->ctorParams.begin(), e->second->ctorParams.end(), [&](const Param& p) { return p.name == name; }) ||
                   prop(e->second->properties) || fun(e->second->methods) || name == "name" || name == "ordinal";
        if (auto i = module_.interfaces.find(type); i != module_.interfaces.end()) {
            if (prop(i->second->properties) || fun(i->second->methods)) return true;
            for (const Type& t : i->second->bases)
                if (t.name != type && kotlinMember(t.name, name)) return true;
        }
        return false;
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
        out_->text(" {");
        statements(block, tail);
        out_->text(" }");
    }

    void statement(const Node& s, bool tail) {
        out_->at(s.pos);
        out_->text(" ");
        switch (s.kind) {
            case Node::ExprStmt: {
                const Node& e = *s.kids[0];
                if (e.kind == Node::If) return ifStatement(e, tail);
                if (e.kind == Node::When) return whenStatement(e, tail);
                if (e.kind == Node::Unary && e.text == "throw") {  // (a branch giving a value may throw instead: no return)
                    if (caughtName(*e.kids[0])) return out_->text("throw;");
                    out_->text("throw ");
                    expr(*e.kids[0]);
                    out_->text(";");
                    return;
                }
                if (e.kind == Node::Unary && (e.text == "++" || e.text == "--")) {
                    if (const Node* target = e.kids[0].get(); delegatedName(*target)) {
                        out_->text(held(target->text) + ".set(::kompoz::kt::value(" + held(target->text) + ") " + e.text[0] + " 1);");
                        return;
                    }
                    if (const Node* target = e.kids[0].get(); target->kind == Node::Member && !target->flag && target->text != "value" &&
                                                              target->text != "size" && target->text != "length") {
                        // shelf.picks++: the property set to its value plus one (through its setter, where it has one)
                        out_->text("{ auto&& kompoz_o = ");
                        expr(*target->kids[0]);
                        out_->text("; KOMPOZ_KT_SET((kompoz_o), " + cppName(target->text) + ", " + setterOf(target->text) + ", (KOMPOZ_KT_GET((kompoz_o), " +
                                  cppName(target->text) + ") " + e.text[0] + " 1)); }");
                        return;
                    }
                    if (const Node* target = e.kids[0].get(); accessorName(*target)) {  // count++ on a property with a setter
                        out_->text(setterOf(target->text) + "(" + cppName(target->text) + "() " + e.text[0] + " 1);");
                        return;
                    }
                    if (std::string r = receiverHolding(*e.kids[0]); !r.empty()) {  // count++ on a receiver's (an outer object's)
                        const std::string& n = e.kids[0]->text;
                        out_->text("KOMPOZ_KT_SET((" + r + "), " + cppName(n) + ", " + setterOf(n) + ", (KOMPOZ_KT_GET((" + r + "), " + cppName(n) + ") " +
                                   e.text[0] + " 1));");
                        return;
                    }
                }
                if (tail) out_->text(taskValue_ ? "co_return " : "return ");
                expr(e);
                out_->text(";");
                return;
            }
            case Node::Decl: return declaration(s);
            case Node::Assign: return assignment(s);
            case Node::Return:
                // return@Label: from the lambda given to Label, which must be the one just around it (C++ returns from that one only)
                if (!s.text.empty() && (lambdaLabels_.empty() || lambdaLabels_.back() != s.text))
                    throw CompileError(s.pos, "return@" + s.text + ": only from the lambda just around it" +
                                                  (lambdaLabels_.empty() || lambdaLabels_.back().empty() ? std::string() : " (" + lambdaLabels_.back() + "'s)"));
                out_->text(suspend_ ? "co_return" : "return");
                if (!s.kids.empty()) {  // (of the function's type, where it is written: fun steps(): List<Step> = listOf(…))
                    out_->text(" ");
                    expr(*s.kids[0], currentResult_ && currentResult_->name != "Unit" && s.text.empty() ? currentResult_ : nullptr);
                }
                out_->text(";");
                return;
            case Node::Break: out_->text("break;"); return;
            case Node::Continue: out_->text("continue;"); return;
            case Node::For: return forStatement(s);
            case Node::While:
                out_->text("while (");
                expr(*s.kids[0]);
                out_->text(")");
                braced(*s.kids[1], false);
                return;
            case Node::Try: return tryStatement(s, tail);
            case Node::DoWhile:
                out_->text("do");
                braced(*s.kids[0], false);
                out_->text(" while (");
                expr(*s.kids[1]);
                out_->text(");");
                return;
            default: throw CompileError(s.pos, "unexpected statement");
        }
    }

    // A name that is an implicit receiver's member (not a local's): that receiver, as written ((*kompoz_this_1)); else "".
    std::string receiverHolding(const Node& n) const {
        if (n.kind != Node::Name || lookup(n.text)) return {};
        size_t r = receiverOf(n.text);
        return r ? "(*kompoz_this_" + std::to_string(r) + ")" : std::string();
    }
    bool delegatedName(const Node& n) const {
        if (n.kind != Node::Name) return false;
        const VarKind* k = lookup(n.text);
        if (!k && !receiverOf(n.text) && !memberFields_.contains(n.text))  // (a top-level var n by mutableStateOf(0))
            if (auto p = module_.properties.find(n.text); p != module_.properties.end()) return p->second->decl->delegated;
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
        if (!k && module_.properties.contains(name) && !memberFields_.contains(name)) return qualifierOf(name) + cppName(name);  // (a top-level one's)
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
            out_->text("auto&& " + held + " = ");
            expr(*d.kids[0]);
            out_->text(";");
            destructuredNames(d.names, held);
            for (auto& n : d.names) declare(n, Local);
            return;
        }
        std::string name = cppName(d.text);
        if (d.delegated) {
            // var x by remember { mutableStateOf(0) }: x holds the state; reading x reads its value.
            out_->text("auto " + name + " = ");
            expr(*d.kids[0]);
            out_->text(";");
            declare(d.text, Delegated);
            return;
        }
        out_->text((d.type ? type(*d.type) : std::string("auto")) + " " + name);
        if (!d.kids.empty()) {
            out_->text(" = ");
            expr(*d.kids[0], d.type ? &*d.type : nullptr);
        } else if (d.type) {
            out_->text("{}");
        } else {
            throw CompileError(d.pos, "a variable without a value needs a type");
        }
        out_->text(";");
        declare(d.text, d.type && d.type->function ? Callable : Local);
        if (d.flag) varLocals_.insert(d.text);  // (a var: an operand may change it)
    }

    void assignment(const Node& a) {
        const Node& target = *a.kids[0];
        std::string op = a.text;
        auto rhs = [&] {
            if (op == "=") {
                expr(*a.kids[1]);
            } else {  // x += v: x's value (a state's read through) and v
                expr(target);
                out_->text(" " + op.substr(0, 1) + " (");
                expr(*a.kids[1]);
                out_->text(")");
            }
        };
        if (delegatedName(target)) {  // x = v on a delegated x: the state set
            out_->text(held(target.text) + ".set(");
            rhs();
            out_->text(");");
            return;
        }
        if (accessorName(target)) {  // x = v on a member with a setter (a class's own or an interface's property)
            out_->text(setterOf(target.text) + "(");
            rhs();
            out_->text(");");
            return;
        }
        if (target.kind == Node::Member && target.text == "value") {  // state.value = v
            expr(*target.kids[0]);
            out_->text(".set(");
            rhs();
            out_->text(");");
            return;
        }
        // obj.name = v, or name = v on a receiver's member: the property set (its setter where it has one: setName)
        auto setter = setterOf;
        if (auto x = module_.extensionProperties.find(target.text);
            target.kind == Node::Member && !target.flag && x != module_.extensionProperties.end() && x->second->setter) {
            // counter.doubled = 4 (+= 4): an extension var's setter, given the receiver (its getter's value first; a
            // member of the name first, as in Kotlin)
            std::string ext = qualifierOf("kompoz_get_" + target.text) + "kompoz_get_" + target.text,
                        extSet = qualifierOf("kompoz_get_" + target.text) + x->second->setter->name;
            out_->text("{ auto&& kompoz_o = ");
            expr(*target.kids[0]);
            out_->text("; KOMPOZ_KT_SET_OR((kompoz_o), " + cppName(target.text) + ", " + setterOf(target.text) + ", " + extSet + ", (");
            if (op != "=") out_->text("KOMPOZ_KT_GET_OR((kompoz_o), " + cppName(target.text) + ", " + ext + ") " + op.substr(0, op.size() - 1) + " (");
            expr(*a.kids[1]);
            out_->text(op != "=" ? "))); }" : ")); }");
            return;
        }
        if (op != "=" && target.kind == Node::Member && !target.flag && target.text != "value") {  // shelf.picks += 2: through its setter
            out_->text("{ auto&& kompoz_o = ");
            expr(*target.kids[0]);
            out_->text("; KOMPOZ_KT_SET((kompoz_o), " + cppName(target.text) + ", " + setterOf(target.text) + ", (KOMPOZ_KT_GET((kompoz_o), " +
                      cppName(target.text) + ") " + op.substr(0, op.size() - 1) + " (");
            expr(*a.kids[1]);
            out_->text("))); }");
            return;
        }
        if (op == "=" && target.kind == Node::Member && !target.flag && target.text != "value") {
            out_->text("KOMPOZ_KT_SET((");
            expr(*target.kids[0]);
            out_->text("), " + cppName(target.text) + ", " + setter(target.text) + ", (");
            rhs();
            out_->text("));");
            return;
        }
        if (target.kind == Node::Name && !lookup(target.text)) {  // (x += v too: its value read through it first)
            if (size_t r = receiverOf(target.text)) {
                out_->text("KOMPOZ_KT_SET((*kompoz_this_" + std::to_string(r) + "), " + cppName(target.text) + ", " + setter(target.text) + ", (");
                rhs();
                out_->text("));");
                return;
            }
        }
        if (target.kind == Node::Index && target.kids.size() == 2 && op == "=") {  // a[k] = v: set(k, v) where there is one (a SavedStateHandle's)
            out_->text("::kompoz::kt::setAt(");
            expr(*target.kids[0]);
            out_->text(", ");
            expr(*target.kids[1]);
            out_->text(", ");
            expr(*a.kids[1]);
            out_->text(");");
            return;
        }
        if (target.kind == Node::Index) {  // a[k] += v: the element itself
            indexed(target);
            out_->text(" " + op + " ");
            expr(*a.kids[1]);
            out_->text(";");
            return;
        }
        expr(target);
        out_->text(" " + op + " ");
        expr(*a.kids[1]);
        out_->text(";");
    }

    void ifStatement(const Node& n, bool tail) {
        out_->text("if (");
        std::vector<const Node*> casts;
        castCondition(*n.kids[0], casts);
        out_->text(") {");
        scopes_.emplace_back();
        smartCasts(casts);
        statements(*n.kids[1], tail);
        scopes_.pop_back();
        out_->text(" }");
        if (n.kids.size() > 2) {
            out_->text(" else");
            braced(*n.kids[2], tail);
        }
    }

    // A condition, its `x is T` tests on locals (and a && chain's after them) giving Kotlin's smart casts:
    // what follows a test sees x as a T (`x is T && x.y > 0`), and `casts` gets the tests, for the block after.
    void castCondition(const Node& c, std::vector<const Node*>& casts) {
        if (c.kind == Node::Binary && c.text == "&&") {
            out_->text("(");
            castCondition(*c.kids[0], casts);
            out_->text(" && ");
            if (casts.empty()) {
                castCondition(*c.kids[1], casts);
            } else {
                out_->text(capture("[&]") + "() -> bool {");
                scopes_.emplace_back();
                smartCasts(casts);
                out_->text(" return ");
                castCondition(*c.kids[1], casts);
                out_->text("; }()");
                scopes_.pop_back();
            }
            out_->text(")");
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
            out_->text(" const auto& " + held + " = ::kompoz::kt::as<" + type(*is->type) + ">(" + cppName(n) + "); [[maybe_unused]] const auto& " +
                      cppName(n) + " = " + held + ";");
            declare(n, Local);
        }
    }

    void whenCondition(const Node::WhenBranch& b, const std::string& subject) {
        for (size_t i = 0; i < b.conds.size(); ++i) {
            if (i) out_->text(" || ");
            auto& [kind, e] = b.conds[i];
            out_->text("(");
            switch (kind) {
                case Node::WhenBranch::Expr:
                    if (subject.empty()) {
                        expr(*e);
                    } else {
                        out_->text(subject + " == ");
                        expr(*e);
                    }
                    break;
                case Node::WhenBranch::In:
                case Node::WhenBranch::NotIn:
                    out_->text(std::string(kind == Node::WhenBranch::NotIn ? "!" : "") + "::kompoz::kt::contains(");
                    expr(*e);
                    out_->text(", " + subject + ")");
                    break;
                default:  // is T, !is T
                    out_->text(std::string(kind == Node::WhenBranch::NotIs ? "!" : "") + "::kompoz::kt::is<" + type(b.types[i]) + ">(" + subject + ")");
                    break;
            }
            out_->text(")");
        }
    }

    void whenStatement(const Node& n, bool tail, bool asValue = false) {
        std::string subject, smartName;  // (smartName: the subject's, a local's, seen as the T of an `is T` branch)
        out_->text("{");
        if (const Node* s = n.kids[0].get()) {
            if (s->kind == Node::Decl) smartName = s->text;
            else if (const VarKind* k = s->kind == Node::Name ? lookup(s->text) : nullptr; k && *k == Local) smartName = s->text;
            subject = "kompoz_when_" + std::to_string(n.pos.line) + "_" + std::to_string(n.pos.column);
            out_->text(" const auto& " + subject + " = ");
            expr(s->kind == Node::Decl ? *s->kids[0] : *s);
            out_->text(";");
            if (s->kind == Node::Decl) out_->text(" const auto& " + cppName(s->text) + " = " + subject + ";");
            scopes_.emplace_back();
            if (s->kind == Node::Decl) declare(s->text, Local);
        } else {
            scopes_.emplace_back();
        }
        bool first = true, otherwise = false;
        for (const auto& b : n.branches) {
            out_->at(b.pos);
            std::vector<const Node*> casts;
            std::string cast;  // (an `is T` branch of a local's subject: the local as the T)
            if (b.conds.empty()) {
                out_->text(first ? " {" : " else {");
                otherwise = true;
            } else if (subject.empty() && b.conds.size() == 1) {  // when { x is T -> … }
                out_->text(first ? " if (" : " else if (");
                castCondition(*b.conds[0].second, casts);
                out_->text(") {");
            } else {
                out_->text(first ? " if (" : " else if (");
                whenCondition(b, subject);
                out_->text(") {");
                if (!smartName.empty() && b.conds.size() == 1 && b.conds[0].first == Node::WhenBranch::Is)
                    cast = " [[maybe_unused]] const auto& " + cppName(smartName) + " = ::kompoz::kt::as<" + type(b.types[0]) + ">(" + subject + ");";
            }
            scopes_.emplace_back();
            smartCasts(casts);
            if (!cast.empty()) {
                out_->text(cast);
                declare(smartName, Local);
            }
            statements(*b.body, tail);
            scopes_.pop_back();
            out_->text(" }");
            first = false;
        }
        // A when giving a value covers every case (Kotlin checks it: a sealed class's subclasses): no way out.
        if (asValue && !otherwise && !first) out_->text(" else { ::kompoz::kt::noBranch(); }");
        scopes_.pop_back();
        out_->text(" }");
    }

    // try { } catch (e: T) { } finally { }: C++'s try / catch; the finally block runs from a guard's destructor,
    // so a coroutine cancelled inside the try runs it too (its frame is destroyed), as Kotlin's does.
    // With `tail`, the body's and each catching block's last statement gives the value (a try used as one).
    void tryStatement(const Node& t, bool tail) {
        out_->text("{");
        if (t.flag) {
            out_->text(" ::kompoz::kt::Finally kompoz_finally_" + std::to_string(++finally_) + "(" + capture("[&]") + "() {");
            scopes_.emplace_back();
            statements(*t.kids.back(), false);
            scopes_.pop_back();
            out_->text(" });");
        }
        if (t.names.empty()) {
            scopes_.emplace_back();
            braced(*t.kids[0], tail);
            scopes_.pop_back();
        } else {
            out_->text(" try");
            scopes_.emplace_back();
            braced(*t.kids[0], tail);
            scopes_.pop_back();
            for (size_t i = 0; i < t.names.size(); ++i) {
                scopes_.emplace_back();
                declare(t.names[i], Local);
                caught_.insert(lookup(t.names[i]));
                out_->text(" catch (const " + type(t.typeArgs[i]) + "& " + cppName(t.names[i]) + ")");
                braced(*t.kids[1 + i], tail);
                caught_.erase(lookup(t.names[i]));
                scopes_.pop_back();
            }
        }
        out_->text(" }");
    }
    int finally_ = 0;
    std::set<const VarKind*> caught_;  // the catches' exceptions being written in (their entries in scopes_)
    // A catch's exception, by its name: thrown again as it is (throw e), its message read (e.message).
    bool caughtName(const Node& n) const {
        return n.kind == Node::Name && caught_.contains(lookup(n.text));
    }

    void forStatement(const Node& f) {
        const Node& it = *f.kids[0];
        scopes_.emplace_back();
        for (auto& n : f.names) declare(n, Local);
        if (f.names.size() == 1) {
            out_->text("for (auto&& " + cppName(f.names[0]) + " : ");
            expr(it);
            out_->text(")");
            braced(*f.kids[1], false);
        } else {
            std::string held = destructured();
            out_->text("for (auto&& " + held + " : ");
            expr(it);
            out_->text(") {");
            destructuredNames(f.names, held);
            statements(*f.kids[1], false);
            out_->text(" }");
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
                out_->text(" [[maybe_unused]] auto " + cppName(names[i]) + " = ::kompoz::kt::component<" + std::to_string(i) + ">(" + held + ");");
    }
    int destructured_ = 0;

    // ---- Expressions ----

    // `expected`: the type the value goes to, where it is known (a declaration's).
    void expr(const Node& e, const Type* expected = nullptr) {
        expected = resolved(expected);
        if (auto s = substitutes_.find(&e); s != substitutes_.end()) {  // (an operand evaluated before, in order)
            s->second.used = true;
            s->second.expected = expected;
            out_->text(s->second.spelled);
            return;
        }
        switch (e.kind) {
            case Node::Name: return name(e);
            case Node::Number: out_->text(number(e.text)); return;
            case Node::String: return string(e);
            case Node::Char: out_->text("U'" + e.text + "'"); return;
            case Node::Bool: out_->text(e.text); return;
            case Node::Null: out_->text("::kompoz::kt::null"); return;
            case Node::This: {  // in a lambda with a receiver: the receiver; this@Label: the lambda Label(…) { }'s
                // (in an inner class: its own object, unless in a lambda with a receiver; this@Outer its outer one's handle)
                size_t r = receivers_.size();
                if (!e.text.empty() && e.text == thisClass_ && std::find(receiverLabels_.begin(), receiverLabels_.end(), e.text) == receiverLabels_.end()) {
                    r = 0;
                } else if (!e.text.empty()) {
                    while (r > 0 && receiverLabels_[r - 1] != e.text) --r;
                    if (r == 0) throw CompileError(e.pos, "this@" + e.text + ": no lambda of " + e.text + " around it");
                } else if (r == outers_.size()) {
                    r = 0;
                }
                if (r > 0 && r <= outers_.size() && !outers_[r - 1].handle.empty())
                    out_->text(outers_[r - 1].handle + "::kompoz_from(kompoz_this_" + std::to_string(r) + ")");
                else if (r == 0 && !staticSelf_.empty() && !inClass_)
                    out_->text("(" + staticSelf_ + ")");
                else
                    out_->text(r == 0 ? std::string("(*this)") : "(*kompoz_this_" + std::to_string(r) + ")");
                return;
            }
            case Node::Paren:
                out_->text("(");
                expr(*e.kids[0]);
                out_->text(")");
                return;
            case Node::Call:
                if (suspend_ && awaitable(e)) {  // in a coroutine: awaited if it suspends (kotlin.hpp's kt::await)
                    out_->text("(co_await ::kompoz::kt::await((");
                    call(e);
                    out_->text(", ::kompoz::kt::VoidMark{})))");
                    return;
                }
                if (awaitedTypes_ && awaitable(e)) {  // a suspend fun's result type, taken from its expression
                    out_->text("::kompoz::kt::awaitedType((");
                    call(e);
                    out_->text(", ::kompoz::kt::VoidMark{}))");
                    return;
                }
                expectedCall_ = expected;
                return call(e);
            case Node::Member: return member(e);
            case Node::Index:
                if (e.kids.size() == 2) {  // a[k]: a map's element is nullable (kt::at), a list's is the element
                    out_->text("::kompoz::kt::at(");
                    expr(*e.kids[0]);
                    out_->text(", ");
                    expr(*e.kids[1]);
                    out_->text(")");
                    return;
                }
                indexed(e);
                return;
            case Node::Lambda: return lambda(e, expected && expected->function && expected->result && expected->result->name != "Unit");
            case Node::Unary: return unary(e);
            case Node::Binary: return binary(e);
            case Node::As:  // x as T (a sealed class's T inside it, else a cast); x as? T: none when it isn't one
                out_->text(std::string(e.flag ? "::kompoz::kt::asOrNull<" : "::kompoz::kt::as<") + type(*e.type) + ">(");
                expr(*e.kids[0]);
                out_->text(")");
                return;
            case Node::Is:
                out_->text(std::string(e.flag ? "!" : "") + "::kompoz::kt::is<" + type(*e.type) + ">(");
                expr(*e.kids[0]);
                out_->text(")");
                return;
            case Node::If: return ifExpression(e, expected);
            case Node::Object: return objectExpression(e);
            case Node::When:
                // A when as a value: run where it stands, each branch giving its value (a declared type the lambda's).
                out_->text(capture("[&]") + "()" + (expected ? " -> " + type(*expected) : std::string()) + " {");
                {
                    Scoped inside(global_);
                    whenStatement(e, true, true);
                }
                out_->text(" }()");
                return;
            case Node::Try:
                // A try as a value: likewise, the body or the catching block giving it.
                out_->text(capture("[&]") + "()" + (expected ? " -> " + type(*expected) : std::string()) + " {");
                {
                    Scoped inside(global_);
                    tryStatement(e, true);
                }
                out_->text(" }()");
                return;
            default: throw CompileError(e.pos, "unexpected expression");
        }
    }

    void name(const Node& e) {
        if (auto p = parameterNames_.find(e.text); p != parameterNames_.end()) {  // (unless a lambda's of the name hides it)
            size_t at = scopes_.size();
            while (at > 0 && !scopes_[at - 1].contains(e.text)) --at;
            if (at == parameterScope_) {
                out_->text(p->second);
                return;
            }
        }
        if (const VarKind* k = lookup(e.text)) {
            if (*k == Delegated || *k == MemberDelegated) out_->text("::kompoz::kt::delegated(" + held(e.text) + ")");
            else if (*k == Accessor) out_->text((selfPrefix_.empty() ? memberPrefix_ : selfPrefix_) + cppName(e.text) + "()");
            else if (*k == BackingField) out_->text(fieldOf_);
            else out_->text(cppName(e.text));
            return;
        }
        if (size_t r = receiverOf(e.text)) {  // a receiver's member: its getter, or the field
            out_->text("KOMPOZ_KT_GET((*kompoz_this_" + std::to_string(r) + "), " + cppName(e.text) + ")");
            return;
        }
        if (auto c = module_.objectDecls.find(e.text); c != module_.objectDecls.end() && c->second->isCompanion && !c->second->supers.empty() &&
                                                         &e != calleeNode_) {  // Foo as a value: its companion, an object of its interfaces
            out_->text(qualifierOf(e.text) + qualified(e.text) + "::kompoz_Companion{}");
            return;
        }
        if (memberFields_.contains(e.text)) {  // the class's own field (before a top-level val of its name, as in Kotlin)
            out_->text(cppName(e.text));
            return;
        }
        if (auto p = module_.properties.find(e.text); p != module_.properties.end()) {  // a top-level val (after a receiver's members, as in Kotlin)
            if (p->second->decl->delegated) out_->text("::kompoz::kt::delegated(" + qualifierOf(e.text) + cppName(e.text) + ")");
            else out_->text(qualifierOf(e.text) + cppName(e.text));
            return;
        }
        if (module_.objects.contains(e.text)) {  // an object: its one value
            out_->text(qualifierOf(e.text) + e.text + "{}");
            return;
        }
        if (e.text == "Modifier") {
            out_->text("::kompoz::Modifier()");
            return;
        }
        if (inClass_ && kBareProperties.contains(e.text)) {  // a class's own property that is a function here
            out_->text(cppName(e.text) + "()");
            return;
        }
        if (auto p = kComposableProperties.find(e.text); p != kComposableProperties.end()) {
            out_->text(p->second);
            return;
        }
        if (e.text == "Unit") {
            out_->text("::kompoz::kt::Unit");
            return;
        }
        if (auto k = kBareConstants.find(e.text); k != kBareConstants.end()) {  // PI
            out_->text(k->second);
            return;
        }
        out_->text(cppName(e.text));
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
            out_->text("std::string(\"" + text + "\")");
            return;
        }
        std::vector<const Node*> parts;  // "${next()} ${next()}": its parts left to right
        for (const NodeP& k : e.kids) parts.push_back(k.get());
        if (ordered(e, parts, [&] { string(e); })) return;
        out_->text("::kompoz::kt::str(");
        size_t k = 0;
        for (size_t i = 0; i < e.parts.size(); ++i) {
            if (i) out_->text(", ");
            if (e.parts[i].expr) expr(*e.kids[k++]);
            else out_->text("\"" + e.parts[i].text + "\"");
        }
        out_->text(")");
    }

    // A name that stands for a type or a namespace here (Arrangement, Icons.Filled): what comes
    // after it is reached with ::, not with a dot.
    bool qualifier(const Node& e) const {
        if (e.kind == Node::Name)
            return (capitalized(e.text) || (module_.structs && module_.structs->namespaces.contains(e.text))) && !lookup(e.text) && e.text != "Modifier" &&
                   !kBareConstants.contains(e.text) && !module_.properties.contains(e.text);  // (BadgeIcon.root: a top-level val's)
        if (e.kind == Node::Member && module_.enums.contains(dotted(*e.kids[0]))) return false;  // (Rank.MID: an entry, a value)
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
        if (e.kids[0]->kind == Node::Name && e.kids[0]->text == "super" && !lookup("super")) {  // super.name: the base's
            if (superObject_.empty()) throw CompileError(e.pos, "super: there is no base class here");
            const InterfaceMember* m = superMembers_ ? memberNamed(*superMembers_, e.text) : nullptr;
            out_->text(superObject_ + "::" + cppName(e.text) + (m && m->property ? "()" : ""));
            return;
        }
        const Node& obj = companionless(*e.kids[0]);
        if (!e.flag && e.text == "message" && caughtName(obj)) {  // (a C++ exception's too: its what())
            out_->text("::kompoz::kt::message(" + cppName(obj.text) + ")");
            return;
        }
        if (qualifier(obj) && module_.enums.contains(dotted(obj)) && (e.text == "entries" || e.text == "values" || e.text == "valueOf")) {
            // Level.entries, Level.values(), Level.valueOf("LOW") (called: its argument follows)
            out_->text(e.text == "valueOf" ? "::kompoz::kt::enumValueOf<" + qualified(dotted(obj)) + ">"
                                          : "::kompoz::kt::enumEntries<" + qualified(dotted(obj)) + ">" + (&e == calleeNode_ ? "" : "()"));
            return;
        }
        if (e.flag) {  // a?.b
            out_->text("::kompoz::kt::safe(");
            expr(obj);
            out_->text(", " + capture("[&]") + (e.text == "size" || e.text == "length" ? "(auto&& kompoz_safe) { return ::kompoz::kt::size(kompoz_safe); })"
                                                                                    : "(auto&& kompoz_safe) { return KOMPOZ_KT_GET((kompoz_safe), " + cppName(e.text) + "); })"));
            return;
        }
        if (obj.kind == Node::Member || obj.kind == Node::Name) {
            if (qualifier(obj)) {
                auto special = kObjectProperties.find(dotted(obj) + "." + e.text);
                if (special != kObjectProperties.end()) {
                    out_->text(special->second);
                    return;
                }
                if (dotted(obj) == "MaterialShapes" && capitalized(e.text)) {  // Compose's MaterialShapes.Cookie9Sided (a property): a function here
                    out_->text("::kompoz::MaterialShapes::" + e.text + "()");
                    return;
                }
            }
        }
        if (e.text == "dp") {
            out_->text("float(");
            expr(obj);
            out_->text(")");
            return;
        }
        if (e.text == "fr") {  // a grid track's share (1.fr)
            out_->text("::kompoz::Fr{float(");
            expr(obj);
            out_->text(")}");
            return;
        }
        if (e.text == "sp" || e.text == "em") {
            out_->text("::kompoz::TextUnit::" + e.text + "(float(");
            expr(obj);
            out_->text("))");
            return;
        }
        if (obj.kind == Node::Name && e.text == "current" && obj.text.starts_with("Local") && !lookup(obj.text)) {
            out_->text(obj.text + ".current()");  // LocalDensity.current
            return;
        }
        if (e.text == "size" || e.text == "length") {  // list.size, text.length: Kotlin's properties
            out_->text("::kompoz::kt::size(");
            expr(obj);
            out_->text(")");
            return;
        }
        if (e.text == "ordinal" || e.text == "keys" || e.text == "values") {  // an enum's place; a map's keys and values
            out_->text("::kompoz::kt::" + e.text + "(");
            expr(obj);
            out_->text(")");
            return;
        }
        if (e.text == "value") {  // state.value
            out_->text("::kompoz::kt::value(");
            expr(obj);
            out_->text(")");
            return;
        }
        if (qualifier(obj)) {
            out_->text(qualified(dotted(obj)) + "::" + cppName(e.text));
            if (&e != calleeNode_ && module_.objects.contains(dotted(obj) + "." + e.text)) {  // UiState.Loading: a sealed class's own object, its value
                out_->text("{}");
                return;
            }
            if (&e != calleeNode_ && objectAccessor(dotted(obj), e.text)) {  // Settings.dark (by mutableStateOf): its getter
                out_->text("()");
                return;
            }
            // WindowInsets.statusBars, a class's static function read as Compose's property: called.
            std::string type = dotted(obj);
            if (size_t dot = type.rfind('.'); dot != std::string::npos) type = type.substr(dot + 1);
            if (&e != calleeNode_ && module_.structs && module_.structs->staticFunction(type, e.text)) out_->text("()");
            return;
        }
        // A property: a getter of the same name where the type has one (listState.firstVisibleItemIndex is
        // firstVisibleItemIndex() here), else the field; else an extension property of the name ("abc".shout: its
        // getter given the receiver; a member comes first, as in Kotlin).
        if (auto x = module_.extensionProperties.find(e.text); x != module_.extensionProperties.end() && &e != calleeNode_) {
            out_->text("KOMPOZ_KT_GET_OR((");
            expr(obj);
            out_->text("), " + cppName(e.text) + ", " + qualifierOf("kompoz_get_" + e.text) + "kompoz_get_" + e.text + ")");
            return;
        }
        out_->text("KOMPOZ_KT_GET((");
        expr(obj);
        out_->text("), " + cppName(e.text) + ")");
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
        {  // Its arguments (and receiver) in Kotlin's order where that can matter.
            std::vector<const Node*> operands;
            const Node& head = companionless(*c.kids[0]);
            // (its receiver only when evaluating it changes something: a value it is called on is that value, not a copy)
            if (head.kind == Node::Member && !qualifier(*head.kids[0]) && effectful(head.kids[0].get())) operands.push_back(head.kids[0].get());
            for (size_t i = 1; i < c.kids.size(); ++i) operands.push_back(c.kids[i].get());
            if (ordered(c, operands, [&] {
                    expectedCall_ = expected;
                    call(c);
                }))
                return;
        }
        const Node& callee = companionless(*c.kids[0]);
        std::string calleeName = callee.kind == Node::Name ? callee.text : callee.kind == Node::Member ? callee.text : "";
        if (callee.kind == Node::Member && callee.kids[0]->kind == Node::Name && callee.kids[0]->text == "super" && !lookup("super")) {
            // super.describe(): the base class's own (not this one's override)
            if (superObject_.empty()) throw CompileError(c.pos, "super: there is no base class here");
            out_->text(superObject_ + "::" + cppName(callee.text));
            return arguments(c, calleeName, false);
        }
        if (callee.kind == Node::Name && !lookup(callee.text))  // a class's own function, or one it inherits: before a top-level one
            if (auto m = classMethods_.find(callee.text); m != classMethods_.end())
                if (auto own = withoutReceiver(m->second); !own.empty()) return overloadCall(c, own, nullptr, true);
        if (!selfPrefix_.empty() && callee.kind == Node::Name && !lookup(callee.text) && interfaceMethods_.contains(callee.text))
            out_->text(selfPrefix_);  // (an interface's own function, in a declaration's type)
        if (callee.kind == Node::Name && !lookup(callee.text) && c.lambda && c.kids.size() == 1)
            if (const InterfaceDecl* i = interfaceNamed(callee.text); i && i->isFun) {  // Validator { it.isNotEmpty() }: a fun interface's object
                const Function* m = nullptr;
                for (const Function& f : i->methods)
                    if (f.isAbstract) m = &f;
                out_->text(qualified(i->name));
                typeArguments(c);  // (Mapper<Int> { … })
                out_->text("::kompoz_lambda(");
                if (m->isSuspend) {  // Loader { delay(10); "x" }: a coroutine giving the fun's result (TaskOf<T>)
                    Bindings bind;
                    for (size_t k = 0; k < i->typeParams.size() && k < c.typeArgs.size(); ++k) bind.emplace(i->typeParams[k], c.typeArgs[k]);
                    std::string was = std::exchange(taskResult_, m->result && m->result->name != "Unit" ? type(substitute(*m->result, bind)) : std::string());
                    lambda(*c.lambda, !taskResult_.empty(), true);
                    taskResult_ = was;
                } else {
                    lambda(*c.lambda, m->result && m->result->name != "Unit");
                }
                out_->text(")");
                return;
            }
        // A local of the callee's name is not what is called when the call has a trailing lambda or named
        // arguments (a value can't take them): items(items, key = { }) { } calls the function, as Kotlin does.
        if (const VarKind* k = callee.kind == Node::Name ? lookup(callee.text) : nullptr; k && *k == Callable && c.lambda) {
            // strip("x") { t -> … }: a local of a function type called with its trailing lambda (a value: what the type returns)
            out_->text(cppName(callee.text) + "(");
            for (size_t i = 1; i < c.kids.size(); ++i) {
                if (i > 1) out_->text(", ");
                expr(*c.kids[i]);
            }
            if (c.kids.size() > 1) out_->text(", ");
            lambda(*c.lambda, true);
            out_->text(")");
            return;
        }
        if (callee.kind == Node::Name && lookup(callee.text) && (c.lambda || named(c))) {
            if (auto f = module_.functions.find(callee.text); f != module_.functions.end()) return overloadCall(c, f->second);
            out_->text("::kompoz::" + cppName(callee.text));  // (qualified: C++ would take the local)
            return arguments(c, calleeName, false);
        }
        // A receiver's member (drawCircle(…) in a Canvas's lambda): before the file's and Kotlin's own.
        if (callee.kind == Node::Name && !lookup(callee.text)) {
            if (size_t r = receiverOf(callee.text)) {
                out_->text("(*kompoz_this_" + std::to_string(r) + ")." + cppName(callee.text));
                if (!c.typeArgs.empty()) {  // kompozable<Profile> { } in a NavHost
                    out_->text("<");
                    for (size_t i = 0; i < c.typeArgs.size(); ++i) out_->text((i ? ", " : "") + plainType(c.typeArgs[i]));
                    out_->text(">");
                }
                return arguments(c, calleeName, false);
            }
            if (kScopeFunctions.contains(callee.text) && !receivers_.empty()) {  // the scope first (the PointerInputScope around it)
                size_t r = receivers_.size();
                while (r > 1 && receivers_[r - 1] != "PointerInputScope") --r;
                out_->text(callee.text + "((*kompoz_this_" + std::to_string(r) + ")");
                return arguments(c, calleeName, false, true);
            }
            if (callee.text == "flow" && c.lambda && c.kids.size() == 1 && c.typeArgs.empty()) {
                // flow { emit(…) } in a function returning Flow<T>: flow<T>, its lambda the collector's (emit)
                if (!currentResult_ || currentResult_->name != "Flow" || currentResult_->args.size() != 1)
                    throw CompileError(c.pos, "flow { } needs the function's type: fun f(): Flow<T> = flow { … }");
                out_->text("::kompoz::flow<" + type(currentResult_->args[0]) + ">");
                return arguments(c, calleeName, false);
            }
            if (callee.text == "DraggableAnchors" && c.lambda && c.kids.size() == 1) {  // Compose's builder: DraggableAnchors { A at 0f }
                out_->text("::kompoz::draggableAnchors");
                return arguments(c, calleeName, false);
            }
        }
        // An inner class made by its name in its outer class (or a sibling inner one): with that outer object.
        if (callee.kind == Node::Name && !lookup(callee.text))
            if (auto m = innerMade_.find(callee.text); m != innerMade_.end())
                return constructClass(c, *m->second.decl, [&] { out_->text(m->second.outer); }, callee.text);
        // A @Kompozable kompozc translates: arguments by position, defaults filled in.
        if (callee.kind == Node::Name && !lookup(callee.text)) {
            if (auto f = module_.functions.find(callee.text); f != module_.functions.end()) return overloadCall(c, f->second);
            if (auto k = module_.classes.find(callee.text); k != module_.classes.end()) return construct(c, *k->second);
            // (a generic one's always: its name with its namespace, as inside it its name alone is the type it is there)
            if (auto k = module_.classDecls.find(callee.text); k != module_.classDecls.end() && (named(c) || !k->second->typeParams.empty()))
                return constructClass(c, *k->second);
            if (callee.text == "Exception" || callee.text == "Throwable") {  // Exception("…"): Kotlin's (C++'s takes no message)
                out_->text("::kompoz::kt::" + callee.text);
                return arguments(c, calleeName, false);
            }
            if (auto t = kTypes.find(callee.text); t != kTypes.end() && t->second.starts_with("::")) {  // a type named in full (KompozTestRule { })
                out_->text(t->second);
                return arguments(c, calleeName, false);
            }
            if (callee.text == "Canvas" && c.kids.size() == 2 && c.names[0].empty() && !c.lambda) {
                // Compose's Canvas(image): drawing into an ImageBitmap (graphics::Canvas), not the composable, which takes its block
                out_->text("::kompoz::graphics::Canvas");
                return arguments(c, calleeName, false);
            }
            if (callee.text == "Color" && c.kids.size() == 2 && c.names[0].empty() && !c.lambda) {
                // Compose's Color(0xFF6750A4), an Int or a Long: AARRGGBB (its packed ULong form is Color(value = …), not this)
                out_->text("::kompoz::Color::argb(");
                expr(*c.kids[1]);
                out_->text(")");
                return;
            }
            if (callee.text == "Path" && named(c)) {  // a vector's path (Compose's Path(pathData = …) in rememberVectorPainter { })
                out_->text("::kompoz::vector::Path");
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
                out_->text("::kompoz::kt::" + callee.text);
                static const std::set<std::string, std::less<>> collections = {"listOf", "mutableListOf", "arrayListOf", "setOf", "mutableSetOf"};
                if (c.typeArgs.empty() && c.kids.size() > 1 && expected && expected->args.size() == 1 && collections.contains(callee.text))
                    out_->text("<" + type(expected->args[0]) + ">");  // (val tags: List<Titled> = listOf(Tag("a")): of the type wanted)
                typeArguments(c);  // listOf<String>()
                return arguments(c, calleeName, false);
            }
        }
        if (callee.kind == Node::Member) {
            const Node& obj = companionless(*callee.kids[0]);
            if (callee.flag) {  // a?.f(x): f called only when a isn't null
                out_->text("::kompoz::kt::safe(");
                expr(obj);
                out_->text(", " + capture("[&]") + "(auto&& kompoz_safe) { return ");
                Scoped inside(global_);
                if (kKtMembers.contains(callee.text)) {
                    out_->text("::kompoz::kt::" + callee.text + "(kompoz_safe");
                    arguments(c, calleeName, true, true);
                } else {  // (a?.hasRoute<T>(): the template's arguments, on a dependent object)
                    out_->text("kompoz_safe." + std::string(c.typeArgs.empty() ? "" : "template ") + cppName(callee.text));
                    typeArguments(c);
                    arguments(c, calleeName, false);
                }
                out_->text("; })");
                return;
            }
            if (auto f = module_.functions.find(callee.text); f != module_.functions.end() && !qualifier(obj))
                if (auto extensions = withReceiver(f->second); !extensions.empty())
                    return overloadCall(c, extensions, &obj);  // an extension: x.card() is card(x)
            if (obj.kind == Node::Name && obj.text == "Modifier" && !lookup("Modifier")) {
                // Modifier.animateWidth(source) in a ButtonGroup's block: the scope's, as Compose's extension is, where
                // Modifier has no member of the name.
                if (module_.structs && !module_.structs->member("Modifier", callee.text))
                    if (size_t r = receiverOf(callee.text)) {
                        out_->text("(*kompoz_this_" + std::to_string(r) + ")." + cppName(callee.text));
                        return arguments(c, calleeName, false);
                    }
                out_->text("::kompoz::Modifier()." + cppName(callee.text));
                return arguments(c, calleeName, false);
            }
            if (auto in = module_.innerClasses.find(callee.text); in != module_.innerClasses.end() && !qualifier(obj)) {
                // outer.Counter(…): an inner class's object, holding outer's (the one class with an inner class of the name)
                if (in->second.size() > 1) throw CompileError(c.pos, "several classes have an inner class " + callee.text + ": name them apart");
                std::string outer = qualified(in->second[0].first);
                return constructClass(c, *in->second[0].second, [&] {
                    out_->text("::kompoz::kt::outerOf(");
                    expr(obj);
                    out_->text(")");
                }, qualifierOf(outer.substr(0, outer.find("::"))) + outer + "::" + callee.text);
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
                out_->text("::kompoz::kt::apply(");
                expr(obj);
                out_->text(", ");
                lambda(*c.lambda, false, false, false, &type);
                out_->text(")");
                return;
            }
            if (kKtMembers.contains(callee.text) && !qualifier(obj)) {
                if (kotlinMethod(callee.text)) {  // (a Kotlin class's own of the name first, as Kotlin's member wins)
                    std::string m = cppName(callee.text), a = "std::forward<decltype(kompoz_a)>(kompoz_a)...";
                    out_->text("::kompoz::kt::detail::callOr([](auto&& kompoz_o, auto&&... kompoz_a) -> decltype(::kompoz::kt::deref(kompoz_o)." + m + "(" + a +
                               ")) { return ::kompoz::kt::deref(kompoz_o)." + m + "(" + a + "); }, [](auto&& kompoz_o, auto&&... kompoz_a) -> decltype(auto) { return ::kompoz::kt::" +
                               callee.text + "(kompoz_o, " + a + "); }, ");
                } else {
                    out_->text("::kompoz::kt::" + callee.text + "(");
                }
                expr(obj);
                return arguments(c, calleeName, true, true);
            }
            if (kFreeMembers.contains(callee.text) && !qualifier(obj)) {
                out_->text("::kompoz::" + callee.text + "(");
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
                out_->text(capture("[&]") + "(auto&& kompoz_o) -> decltype(auto) { return kompoz_o.template " + member);
                typeArguments(c);
                arguments(c, calleeName, false);
                out_->text("; }(::kompoz::kt::deref(");
                expr(companionless(*callee.kids[0]));
                out_->text("))");
                return;
            }
            out_->text("::kompoz::kt::deref(");
            expr(companionless(*callee.kids[0]));
            out_->text(")." + member);
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
        out_->text("<");
        for (size_t i = 0; i < c.typeArgs.size(); ++i) out_->text((i ? ", " : "") + plainType(c.typeArgs[i]));
        out_->text(">");
    }

    // (args): as written, named ones as kompoz's named arguments, the trailing lambda last.
    // `positional`: names dropped (kotlin.hpp's functions take their arguments by position);
    // `after`: the parenthesis is open and the first argument written.
    static bool named(const Node& c) {
        return std::any_of(c.names.begin(), c.names.end(), [](const std::string& n) { return !n.empty(); });
    }

    void arguments(const Node& c, const std::string& calleeName, bool positional, bool after = false) {
        if (!after) out_->text("(");
        bool any = after;
        auto sp = kStructParameters.find(calleeName);
        const std::vector<std::string>* fields =
            sp != kStructParameters.end() && module_.structs && !positional && named(c) ? module_.structs->find(sp->second) : nullptr;
        if (fields && structCall(c, "::kompoz::" + sp->second, *fields, any ? ", " : "")) {
            any = true;
        } else {
            for (size_t i = 0; i < c.kids.size() - 1; ++i) {
                if (any) out_->text(", ");
                any = true;
                const std::string& n = c.names[i];
                if (!n.empty() && !positional) out_->text("::kompoz::arg::" + n + " = ");
                auto receiver = kReceiverArguments.find(calleeName + "." + n);
                if (c.kids[i + 1]->kind == Node::Lambda && kValueArguments.contains(n) && receiver == kReceiverArguments.end()) lambda(*c.kids[i + 1], true);
                else if (c.kids[i + 1]->kind == Node::Lambda && receiver != kReceiverArguments.end())
                    lambda(*c.kids[i + 1], kValueArguments.contains(n), false, false, &receiver->second, calleeName);
                else expr(*c.kids[i + 1]);
            }
        }
        if (c.lambda) {
            if (any) out_->text(", ");
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
            lambda(*c.lambda, kValueLambdas.contains(calleeName) || measures || methodLambdaGivesValue(calleeName), kSuspendLambdas.contains(calleeName),
                   calleeName == "produceState",
                   type, calleeName);
        }
        out_->text(")");
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
        if (!k.typeParams.empty() && c.typeArgs.empty()) {  // Pair(1, "a"): its type arguments the values' (kompoz_make_Pair)
            // (a nested one's through its outer type's: Outer::kompoz_make_Box)
            size_t colons = spelled.rfind("::");
            out_->text((colons == std::string::npos ? qualifierOf(k.name) : spelled.substr(0, colons + 2)) + "kompoz_make_" + k.name + "(");
            for (size_t f = 0; f < k.fields.size(); ++f) {
                const Node* arg = given[f] ? given[f] : k.fields[f].defaultValue.get();
                if (!arg) throw CompileError(c.pos, k.name + " needs " + k.fields[f].name);
                if (f) out_->text(", ");
                expr(*arg);
            }
            out_->text(")");
            return;
        }
        out_->text(spelled.empty() ? (k.typeParams.empty() ? k.name : qualifierOf(k.name) + k.name) : spelled);
        typeArguments(c);  // (Pair<Int, String>(…))
        out_->text("{");
        bool any = false;
        for (size_t f = 0; f < k.fields.size(); ++f) {
            if (!given[f]) {
                if (!k.fields[f].defaultValue) throw CompileError(c.pos, k.name + " needs " + k.fields[f].name);
                continue;
            }
            out_->text(std::string(any ? ", " : "") + "." + cppName(k.fields[f].name) + " = ");
            any = true;
            expr(*given[f], k.typeParams.empty() ? &k.fields[f].type : nullptr);
        }
        out_->text("}");
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
        out_->text(prefix + name + "{");
        bool any = false;
        for (size_t f = 0; f < fields.size(); ++f) {
            if (!given[f]) continue;
            out_->text(std::string(any ? ", " : "") + "." + fields[f] + " = ");
            any = true;
            // (a field's lambda returns its last expression: a std::function<bool(…)> wants it, a void one takes it)
            if (given[f]->kind == Node::Lambda) lambda(*given[f], true);
            else expr(*given[f]);
        }
        out_->text("}");
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
        out_->text("::kompoz::kt::copy(");
        expr(obj);
        out_->text(", " + capture("[&]") + "(auto& kompoz_copy) {");
        Scoped inside(global_);
        for (size_t i = 1; i < c.kids.size(); ++i) {
            out_->text(" kompoz_copy." + cppName(c.names[i - 1]) + " = ");
            expr(*c.kids[i]);
            out_->text(";");
        }
        out_->text(" })");
        return true;
    }

    // `receiver`: an extension's, written before it (x.card()); else an extension called by its
    // name alone has the receiver it is called in (this).
    // `member`: a class's own function (called by its name in the class, before a top-level one of the name).
    // ---- Overloads: functions of one name taking different parameters ----

    static std::vector<const Function*> withReceiver(const std::vector<const Function*>& fs) {
        std::vector<const Function*> out;
        for (const Function* f : fs)
            if (f->receiver) out.push_back(f);
        return out;
    }
    static std::vector<const Function*> withoutReceiver(const std::vector<const Function*>& fs) {
        std::vector<const Function*> out;
        for (const Function* f : fs)
            if (!f->receiver) out.push_back(f);
        return out;
    }
    // Whether `f` overrides the inherited member `m`: its name, and its parameters' types as `m`'s are where inherited.
    bool overrides(const Function& f, const InterfaceMember& m) const {
        if (!m.method || m.method->name != f.name || m.method->params.size() != f.params.size()) return false;
        for (size_t i = 0; i < f.params.size(); ++i)
            if (!sameType(f.params[i].type, substitute(m.method->params[i].type, m.bind))) return false;
        return true;
    }
    // Where a call's arguments go among f's parameters (a vararg's elements at its index), as Kotlin puts them;
    // false where f can't take them (a name it hasn't, too many, a parameter left without a default).
    bool argumentsFit(const Node& c, const Function& f, std::vector<const Node*>& given) {
        given.assign(f.params.size(), nullptr);
        size_t vararg = f.params.size(), next = 0;
        for (size_t p = 0; p < f.params.size(); ++p)
            if (f.params[p].isVararg) vararg = p;
        for (size_t i = 0; i + 1 < c.kids.size(); ++i) {
            const std::string& n = c.names[i];
            size_t at = next;
            if (!n.empty()) {
                at = f.params.size();
                for (size_t p = 0; p < f.params.size(); ++p)
                    if (f.params[p].name == n) at = p;
            } else if (at == vararg && vararg < f.params.size()) {
                continue;
            } else {
                next = at + 1;
            }
            if (at >= f.params.size() || given[at]) return false;
            given[at] = c.kids[i + 1].get();
        }
        if (c.lambda) {
            if (f.params.empty() || given.back() || !resolved(&f.params.back().type)->function) return false;
            given.back() = c.lambda.get();
        }
        for (size_t p = 0; p < f.params.size(); ++p)
            if (!given[p] && !f.params[p].defaultValue && p != vararg) return false;
        return true;
    }
    // Whether a class, interface or object of the module has a function of the name (obj.take(x) may be its).
    bool kotlinMethod(const std::string& name) const {
        auto has = [&](const std::vector<Function>& methods) {
            return std::any_of(methods.begin(), methods.end(), [&](const Function& f) { return f.name == name; });
        };
        for (auto& [n, k] : module_.classDecls) if (has(k->methods)) return true;
        for (auto& [n, i] : module_.interfaces) if (has(i->methods)) return true;
        for (auto& [n, o] : module_.objectDecls) if (has(o->methods)) return true;
        for (auto& [n, d] : module_.classes) if (has(d->methods)) return true;
        for (auto& [n, e] : module_.enums) if (has(e->methods)) return true;
        for (auto& [n, outers] : module_.innerClasses)
            for (auto& [outer, k] : outers) if (has(k->methods)) return true;
        return false;
    }
    // Where a class's function's C++ defaults may start: none that would make a call taking its first k parameters
    // match another function of the name taking exactly those (f(s: String) beside f(s: String, n: Int = 2): Kotlin
    // calls the first for f("x"), and so C++ does then).
    size_t defaultsFrom(const Function& m) const {
        size_t from = 0;
        auto m2 = classMethods_.find(m.name);
        if (m2 == classMethods_.end()) return from;
        for (const Function* g : m2->second) {
            if (g == &m || g->params.size() >= m.params.size()) continue;
            size_t k = g->params.size();
            bool same = true;
            for (size_t i = 0; same && i < k; ++i) same = sameType(g->params[i].type, m.params[i].type);
            if (same) from = std::max(from, k + 1);
        }
        return from;
    }
    // Whether f has others of its name (a class's, or the module's at the top).
    bool overloadedName(const Function& f) const {
        if (auto m = classMethods_.find(f.name); m != classMethods_.end() && std::find(m->second.begin(), m->second.end(), &f) != m->second.end())
            return m->second.size() > 1;
        auto t = module_.functions.find(f.name);
        return t != module_.functions.end() && t->second.size() > 1 && std::find(t->second.begin(), t->second.end(), &f) != t->second.end();
    }
    // Whether a parameter of type `t` (of f) may take `arg`: what a literal is shows (a String, an Int, a Double,
    // null, a lambda); of anything else, kompozc doesn't know the type: it may.
    bool mayTake(const Function& f, const Type& declared, const Node& arg) {
        const Type& t = *resolved(&declared);
        if (std::find(f.typeParams.begin(), f.typeParams.end(), t.name) != f.typeParams.end() || t.name == "Any") return true;
        const Node* a = &arg;
        while (a->kind == Node::Paren || (a->kind == Node::Unary && (a->text == "-" || a->text == "+") && !a->flag)) a = a->kids[0].get();
        auto is = [&](std::initializer_list<const char*> names) {
            return std::any_of(names.begin(), names.end(), [&](const char* n) { return t.name == n; });
        };
        switch (a->kind) {
            case Node::Lambda: return t.function;
            case Node::Null: return t.nullable;
            case Node::String: return !t.function && is({"String", "CharSequence", "Comparable"});
            case Node::Bool: return !t.function && is({"Boolean", "Comparable"});
            case Node::Char: return !t.function && is({"Char", "Comparable"});
            case Node::Number: {
                if (t.function) return false;
                const std::string& n = a->text;
                bool hex = n.starts_with("0x") || n.starts_with("0X") || n.starts_with("0b") || n.starts_with("0B");
                if (!hex && (n.ends_with("f") || n.ends_with("F"))) return is({"Float", "Number", "Comparable"});
                if (!hex && n.find_first_of(".eE") != std::string::npos) return is({"Double", "Number", "Comparable"});
                if (n.ends_with("L")) return is({"Long", "Number", "Comparable"});
                if (n.ends_with("u") || n.ends_with("U") || n.ends_with("uL") || n.ends_with("UL")) return is({"UInt", "ULong", "UShort", "UByte"});
                return is({"Int", "Long", "Short", "Byte", "Number", "Comparable"});
            }
            default: return true;
        }
    }
    // A call of a name with overloads: the one its arguments fit (their number, names, a trailing lambda); several
    // that take them alike, differing in their types only (show(x: Int), show(x: String)), are C++'s to choose
    // between, as Kotlin chooses by the types. One alone is called as it is (what doesn't fit, it says).
    void overloadCall(const Node& c, const std::vector<const Function*>& fs, const Node* receiver = nullptr, bool member = false) {
        if (fs.size() == 1) return moduleCall(c, *fs[0], receiver, member);
        std::vector<const Function*> fit;
        std::vector<std::vector<const Node*>> placed;
        for (const Function* f : fs) {
            if (f->receiver && !receiver && !member && receivers_.empty()) continue;
            if (std::vector<const Node*> given; argumentsFit(c, *f, given)) {
                fit.push_back(f);
                placed.push_back(std::move(given));
            }
        }
        const std::string& name = fs[0]->name;
        if (fit.empty()) throw CompileError(c.pos, "no " + name + " takes these arguments (" + std::to_string(fs.size()) + " functions of the name)");
        // Those a literal argument can't go to ("x" to an Int), then, of those taking the arguments as the same types,
        // the one with no default filled in (as Kotlin: describe(s: String) before describe(s: String, times: Int = 2)).
        for (size_t k = fit.size(); fit.size() > 1 && k-- > 0;) {
            bool takes = true;
            for (size_t p = 0; takes && p < placed[k].size(); ++p)
                if (placed[k][p]) takes = mayTake(*fit[k], fit[k]->params[p].type, *placed[k][p]);
            if (!takes) {
                fit.erase(fit.begin() + k);
                placed.erase(placed.begin() + k);
            }
        }
        if (fit.empty()) throw CompileError(c.pos, "no " + name + " takes these arguments' types");
        auto usesDefaults = [&](size_t k) {
            for (size_t p = 0; p < placed[k].size(); ++p)
                if (!placed[k][p] && !fit[k]->params[p].isVararg) return true;
            return false;
        };
        auto sameTypes = [&](size_t a, size_t b) {  // (the arguments given to each, as the same types)
            for (size_t p = 0; p < placed[a].size(); ++p) {
                if (!placed[a][p]) continue;
                auto q = std::find(placed[b].begin(), placed[b].end(), placed[a][p]);
                if (q == placed[b].end() || !sameType(fit[a]->params[p].type, fit[b]->params[q - placed[b].begin()].type)) return false;
            }
            return true;
        };
        for (size_t k = fit.size(); fit.size() > 1 && k-- > 0;) {
            bool beaten = false;
            for (size_t j = 0; !beaten && j < fit.size(); ++j) beaten = j != k && usesDefaults(k) && !usesDefaults(j) && sameTypes(k, j);
            if (beaten) {
                fit.erase(fit.begin() + k);
                placed.erase(placed.begin() + k);
            }
        }
        if (fit.size() == 1) return moduleCall(c, *fit[0], receiver, member);
        // Alike: each argument at the same place, nothing left for kompozc to fill in but C++'s defaults at the end.
        size_t first = 0;
        for (size_t k = 0; k < fit.size(); ++k) {
            const Function& f = *fit[k];
            bool vararg = std::any_of(f.params.begin(), f.params.end(), [](const Param& p) { return p.isVararg; });
            size_t last = placed[k].size();
            while (last > 0 && !placed[k][last - 1]) --last;
            bool gap = std::find(placed[k].begin(), placed[k].begin() + last, nullptr) != placed[k].begin() + last;
            if (vararg || gap || usesDefaults(k) || (k > 0 && (last != first || !std::equal(placed[k].begin(), placed[k].begin() + last, placed[0].begin()))))
                throw CompileError(c.pos, "kompozc can't choose between " + name + "'s functions for these arguments: give them different names");
            if (k == 0) first = last;
        }
        moduleCall(c, *fit[0], receiver, member, &fit);
    }

    void moduleCall(const Node& c, const Function& f, const Node* receiver = nullptr, bool member = false, const std::vector<const Function*>* alike = nullptr) {
        if (f.isSuspend && !suspend_ && !awaitedTypes_ && !inTaskBlock_)
            throw CompileError(c.pos, f.name + " is a suspend fun: call it in a coroutine (LaunchedEffect { }, scope.launch { }, another suspend fun)");
        std::vector<const Node*> given(f.params.size(), nullptr);
        size_t vararg = f.params.size();  // (vararg xs: the positional arguments from there are its elements)
        for (size_t p = 0; p < f.params.size(); ++p)
            if (f.params[p].isVararg) vararg = p;
        std::vector<std::pair<const Node*, bool>> elements;  // (and whether each is spread: *list)
        size_t next = 0;
        for (size_t i = 0; i + 1 < c.kids.size(); ++i) {
            const std::string& n = c.names[i];
            size_t at = next;
            if (!n.empty()) {
                at = f.params.size();
                for (size_t p = 0; p < f.params.size(); ++p)
                    if (f.params[p].name == n) at = p;
                if (at == f.params.size()) throw CompileError(c.kids[i + 1]->pos, f.name + " has no parameter named " + n);
            } else if (at == vararg && vararg < f.params.size()) {
                elements.emplace_back(c.kids[i + 1].get(), std::count(c.spreadArgs.begin(), c.spreadArgs.end(), i + 1) > 0);
                continue;
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
        size_t last = given.size();  // the defaults left at the end: C++'s (an overload's are given here)
        while (last > 0 && !given[last - 1] && f.params[last - 1].defaultValue && !overloadedName(f)) --last;
        out_->text((member ? (selfPrefix_.empty() ? memberPrefix_ : selfPrefix_) : qualifierOf(f.name)) + cppName(f.name));
        if (!c.typeArgs.empty() && !f.typeParams.empty()) {  // first<String>(…): those a call may write
            GenericSig generic = genericSig(f);
            if (c.typeArgs.size() > generic.written)
                throw CompileError(c.pos, f.name + "'s last type parameters are what its lambdas give: leave them out");
            typeArguments(c);
        }
        out_->text("(");
        if (f.receiver) {
            if (receiver) expr(*receiver);
            else if (!receivers_.empty()) out_->text("(*kompoz_this_" + std::to_string(receivers_.size()) + ")");
            else throw CompileError(c.pos, f.name + " is an extension: it needs a receiver");
            if (last > 0) out_->text(", ");
        }
        if (vararg < f.params.size() && !given[vararg]) last = std::max(last, vararg + 1);  // (its elements, none too)
        for (size_t i = 0; i < last; ++i) {
            if (i) out_->text(", ");
            const Param& p = f.params[i];
            if (i == vararg && !given[i]) {
                varargList(p, elements, std::any_of(f.typeParams.begin(), f.typeParams.end(), [&](const std::string& tp) { return mentions(p.type, tp); }));
                continue;
            }
            const Node* arg = given[i] ? given[i] : p.defaultValue.get();
            if (!arg) throw CompileError(c.pos, f.name + " needs " + p.name);
            auto givesValue = [&](const Param& q) {
                const Type* t = resolved(&q.type);
                return t->function && t->result && t->result->name != "Unit";
            };
            // (overloads taking it alike: its type for it where they agree on one, else C++'s to find)
            bool agreed = !alike || std::all_of(alike->begin(), alike->end(), [&](const Function* g) { return sameType(g->params[i].type, p.type); });
            if (arg->kind == Node::Lambda) {
                if (alike && std::any_of(alike->begin(), alike->end(), [&](const Function* g) { return givesValue(g->params[i]) != givesValue(p); }))
                    throw CompileError(arg->pos, "kompozc can't choose between " + f.name + "'s functions for this lambda: give them different names");
                lambda(*arg, givesValue(p));
            } else {
                expr(*arg, f.typeParams.empty() && agreed ? &p.type : nullptr);  // (a generic one's T is the call's to find)
            }
        }
        out_->text(")");
    }

    // A vararg's elements as the list the function takes: std::vector<T>{a, b} (in order: a braced list), with *list's
    // elements spread in (kt::varargOf); a generic one's kt::listOf(a, b) (its T the elements').
    void varargList(const Param& p, const std::vector<std::pair<const Node*, bool>>& elements, bool generic) {
        bool spread = std::any_of(elements.begin(), elements.end(), [](const auto& e) { return e.second; });
        std::string element = type(p.type.args[0]);
        out_->text(generic && spread  ? "::kompoz::kt::varargOf("  // (its T the elements')
                   : generic          ? (elements.empty() ? "{" : "::kompoz::kt::listOf(")
                   : spread           ? "::kompoz::kt::varargOf<" + element + ">("
                                      : "std::vector<" + element + ">{");
        for (size_t i = 0; i < elements.size(); ++i) {
            if (i) out_->text(", ");
            if (elements[i].second) out_->text("::kompoz::kt::Spread{");
            expr(*elements[i].first, generic ? nullptr : &p.type.args[0]);
            if (elements[i].second) out_->text("}");
        }
        out_->text(generic && !elements.empty() ? ")" : spread ? ")" : "}");
    }

    // `receiver`: the type of its receiver (Kotlin's T.() -> R, one of Kompoz's), taken first; kompoz_this_N points
    // to it (a pointer, so a lambda inside that keeps it sees the same one, as Kotlin's would).
    void lambda(const Node& l, bool value, bool suspend = false, bool produceState = false, const std::string* receiver = nullptr,
                const std::string& label = {}) {
        if (classTyping_) {
            taskBlock_ = false;
            // A class's delegate's type, at the class's scope: its lambda as a function giving what the lambda
            // does (the property's type, or its one expression's: derivedStateOf { count * 2 }).
            out_->text("::kompoz::kt::returning<");
            if (delegateValue_) {
                out_->text(type(*delegateValue_));
            } else {
                const Node* last = l.kids.size() == 1 ? l.kids[0].get() : nullptr;
                const Node* e = !last ? nullptr : last->kind == Node::ExprStmt ? last->kids[0].get()
                                               : last->kind == Node::Return && !last->kids.empty() ? last->kids[0].get() : nullptr;
                if (!e || l.flag || usesIt(e)) throw CompileError(l.pos, "give the property a type: its delegate's lambda isn't one expression");
                bool wasGlobal = std::exchange(global_, true);
                classTyping_ = false;
                out_->text("std::remove_cvref_t<decltype(");
                expr(*e);
                out_->text(")>");
                classTyping_ = true, global_ = wasGlobal;
            }
            out_->text(">()");
            return;
        }
        bool wasSuspend = std::exchange(suspend_, suspend), wasTyping = std::exchange(awaitedTypes_, false);
        bool wasTaskBlock = std::exchange(inTaskBlock_, std::exchange(taskBlock_, false));
        // In a data class's property's value (by lazy { "$item x$count" }): the value is copied, so the lambda keeps
        // copies of the fields it reads, not `this` (a copy's lambda would read the one it was copied from).
        const DataClass* dataClass = std::exchange(dataInit_, nullptr);
        bool wasInClass = inClass_;
        std::string dataCaptures;
        if (dataClass) {
            std::set<std::string, std::less<>> used;
            usedNames(&l, used);
            for (const std::string& n : used) {
                if (std::any_of(dataClass->fields.begin(), dataClass->fields.end(), [&](const Param& f) { return f.name == n; })) {
                    if (!lookup(n) || *lookup(n) == Local) dataCaptures += ", " + cppName(n) + " = " + cppName(n);
                    continue;
                }
                for (const ClassProperty& p : dataClass->properties)
                    if (p.name == n) {
                        if (p.delegated) dataCaptures += ", " + storageOf(p) + " = " + storageOf(p);
                        else if (!p.accessed()) dataCaptures += ", " + cppName(n) + " = " + cppName(n);
                        else throw CompileError(l.pos, n + " is read through a getter: a data class's value can't keep it (read the fields it reads)");
                    }
                for (const Function& m : dataClass->methods)
                    if (m.name == n) throw CompileError(l.pos, n + "(): a data class's property's lambda keeps copies of its fields, not the value");
            }
            inClass_ = false;
        }
        // A read-only receiver around (a "const X" one: an AnimatedContentScope, a ToggleFloatingActionButtonScope) is
        // kept by a copy where the lambda may outlive the block (a graphicsLayer's reads checkedProgress as it draws):
        // kt::heldReceiver copies what can be copied, the pointer stays for the rest.
        std::string capture = dataClass ? "[" + (dataCaptures.empty() ? std::string() : dataCaptures.substr(2)) + "]"
                                        : this->capture(std::exchange(inline_, false) ? "[&]" : "[=]");
        if (capture.starts_with("[=")) {
            std::string held;
            for (size_t r = 0; r < receivers_.size(); ++r)
                if (receiverConst_[r]) held += ", kompoz_this_" + std::to_string(r + 1) + " = ::kompoz::kt::heldReceiver(kompoz_this_" + std::to_string(r + 1) + ")";
            capture = capture.substr(0, capture.size() - 1) + held + "]";
        }
        out_->text(capture + "(");
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
            out_->text(type + " kompoz_receiver_" + self + std::string(l.flag && !l.names.empty() ? ", " : ""));
        }
        auto named = [&] {  // the parameters it names
            for (size_t i = 0; i < l.names.size(); ++i) {
                if (l.names[i] == "_") {  // (unused, as Kotlin's)
                    out_->text((i ? ", " : "") + std::string("auto&&"));
                    continue;
                }
                // (a coroutine's by value: its parameters outlive the call that gives them)
                out_->text((i ? ", " : "") + std::string(suspend ? "auto " : "auto&& ") + cppName(l.names[i]));
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
                out_->text("auto value");
                declare("value", Delegated);
            } else if (label == "flow") {
                out_->text("auto emit");
                declare("emit", Local);
            } else if (l.flag) {
                named();
            } else if (!receiver && itUsed) {
                out_->text("auto it");
                declare("it", Local);
            } else if (!receiver) {
                out_->text("auto&&...");  // (collect { } given the element it doesn't use; LaunchedEffect { } given nothing)
            }
        } else if (l.flag) {
            named();
        } else if (receiver) {
            // (its `it` is a lambda's around it)
        } else if (itUsed) {
            out_->text("auto&& it");  // (update { it.add(x) } changes what it is given)
            declare("it", Local);
        } else {
            out_->text("auto&&...");  // { ... } where the type has parameters it doesn't use
        }
        // (a coroutine giving a value: taskResult_, its last expression co_returned)
        std::string taskResult = suspend ? std::exchange(taskResult_, std::string()) : std::string();
        out_->text(suspend ? ") -> ::kompoz::" + (taskResult.empty() ? std::string("Task {") : "TaskOf<" + taskResult + "> {") : std::string(") {"));
        bool wasTaskValue = std::exchange(taskValue_, !taskResult.empty());
        if (receiver) out_->text(" [[maybe_unused]] auto* kompoz_this_" + self + " = &kompoz_receiver_" + self + ";");
        lambdaLabels_.push_back(label);
        for (size_t i = 0; i < l.kids.size(); ++i) statement(*l.kids[i], value && i + 1 == l.kids.size());
        lambdaLabels_.pop_back();
        if (suspend && taskResult.empty()) out_->text(" co_return;");
        taskValue_ = wasTaskValue;
        scopes_.pop_back();
        if (receiver) {
            receivers_.pop_back();
            receiverConst_.pop_back();
            receiverLabels_.pop_back();
        }
        out_->text(" }");
        suspend_ = wasSuspend;
        awaitedTypes_ = wasTyping;
        inTaskBlock_ = wasTaskBlock;
        inClass_ = wasInClass;
        dataInit_ = dataClass;  // (for the value's next lambda)
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
        out_->text("::kompoz::Painter::custom(" + capture("[=]") + "(");
        {
            Scoped inside(global_);
            scopes_.emplace_back();
            receivers_.push_back("DrawScope");
            receiverConst_.push_back(false);
            receiverLabels_.push_back({});
            std::string self = std::to_string(receivers_.size());
            bool wasSuspend = std::exchange(suspend_, false);
            out_->text("::kompoz::DrawScope& kompoz_receiver_" + self + ") { [[maybe_unused]] auto* kompoz_this_" + self + " = &kompoz_receiver_" + self + ";");
            auto lambdaLabels = std::exchange(lambdaLabels_, {});
            statements(*draw->kids[0], false);
            lambdaLabels_ = std::move(lambdaLabels);
            out_->text(" }");
            suspend_ = wasSuspend;
            scopes_.pop_back();
            receivers_.pop_back();
            receiverConst_.pop_back();
            receiverLabels_.pop_back();
        }
        if (size) {
            out_->text(", ");
            const Node& body = *size->kids[0];
            if (body.kids.size() == 1 && body.kids[0]->kind == Node::Return && !body.kids[0]->kids.empty()) {
                expr(*body.kids[0]->kids[0]);
            } else {
                out_->text(capture("[&]") + "() {");
                Scoped inside(global_);
                statements(body, false);
                out_->text(" }()");
            }
        }
        out_->text(")");
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
                if (!objectMember(members, *m)) throw CompileError(m->pos, m->text + " overrides nothing of " + iface->name);
            }
        }
        for (auto& [n, im] : members)
            if (!im.hasBody && std::none_of(o.kids.begin(), o.kids.end(), [&](const NodeP& m) { return objectMember(members, *m) == &im; }))
                throw CompileError(o.pos, "the object doesn't implement " + im.from + "." + n);
        std::set<std::string, std::less<>> used;
        for (auto& m : o.kids) usedNames(m.get(), used);
        std::vector<std::pair<std::string, VarKind>> captured;
        for (const std::string& n : used)
            if (const VarKind* k = lookup(n); k && (*k == Local || *k == Delegated || *k == Callable)) captured.emplace_back(n, *k);
        out_->text(capture("[=]") + "() {");
        // (each held one's type named outside the class: inside, the name is the member's)
        for (auto& [n, k] : captured) out_->text(" using kompoz_type_" + n + " = std::remove_cvref_t<decltype(" + cppName(n) + ")>;");
        out_->text(" struct kompoz_object : " + (iface ? "public virtual " + plainType(*o.type) + "::Object" : plainType(*o.type)) + " {");
        for (auto& [n, k] : captured) out_->text(" kompoz_type_" + n + " " + cppName(n) + ";");
        if (!captured.empty()) {
            out_->text(" kompoz_object(");
            for (size_t i = 0; i < captured.size(); ++i)
                out_->text((i ? ", " : "") + std::string("kompoz_type_") + captured[i].first + " kompoz_" + captured[i].first);
            out_->text(") : ");
            for (size_t i = 0; i < captured.size(); ++i)
                out_->text((i ? ", " : "") + cppName(captured[i].first) + "(std::move(kompoz_" + captured[i].first + "))");
            out_->text(" {}");
        }
        // Its functions see only what it holds and their parameters (and `this` is the object).
        auto scopes = std::exchange(scopes_, {});
        auto receivers = std::exchange(receivers_, {});
        auto receiverConst = std::exchange(receiverConst_, {});
        auto receiverLabels = std::exchange(receiverLabels_, {});
        auto lambdaLabels = std::exchange(lambdaLabels_, {});
        bool wasSuspend = std::exchange(suspend_, false), wasGlobal = std::exchange(global_, false);
        for (auto& m : o.kids) {
            out_->at(m->pos);
            out_->text(" " + (iface ? std::string("auto") : m->type ? type(*m->type) : std::string("void")) + " " + cppName(m->text) + "(");
            scopes_.assign(1, {});
            for (auto& [n, im] : members)  // (the interface's properties, read through its getters)
                if (im.property) declare(n, Accessor);
            for (auto& [n, k] : captured) declare(n, k);
            for (size_t i = 0; i < m->names.size(); ++i) {
                out_->text((i ? ", " : "") + (iface ? paramType(m->typeArgs[i]) : type(m->typeArgs[i])) + " " + cppName(m->names[i]));
                declare(m->names[i], Local);
            }
            if (iface) out_->text(") const -> " + memberType(objectMember(members, *m)->owner, *objectMember(members, *m)) + " override {");
            else out_->text(") override {");
            statements(*m->kids[0], false);
            out_->text(" }");
        }
        scopes_ = std::move(scopes);
        receivers_ = std::move(receivers);
        receiverConst_ = std::move(receiverConst);
        receiverLabels_ = std::move(receiverLabels);
        lambdaLabels_ = std::move(lambdaLabels);
        suspend_ = wasSuspend, global_ = wasGlobal;
        out_->text(" }; return " + (iface ? plainType(*o.type) + "(" : std::string()) + "std::make_shared<kompoz_object>(");
        for (size_t i = 0; i < captured.size(); ++i) out_->text((i ? ", " : "") + cppName(captured[i].first));
        out_->text(std::string(iface ? ")" : "") + "); }()");
    }

    // a[i] (a[i, j]) as C++ writes it: the element itself, to assign to or count up.
    void indexed(const Node& e) {
        expr(*e.kids[0]);
        out_->text("[");
        for (size_t i = 1; i < e.kids.size(); ++i) {
            if (i > 1) out_->text(", ");
            expr(*e.kids[i]);
        }
        out_->text("]");
    }

    void unary(const Node& u) {
        const Node& x = *u.kids[0];
        if (u.text == "throw") {  // x ?: throw IllegalStateException("…"): a value of any type (Kotlin's Nothing)
            if (caughtName(x)) return out_->text("::kompoz::kt::rethrow()");
            out_->text("::kompoz::kt::raise(");
            expr(x);
            out_->text(")");
        } else if (u.text == "!!") {
            out_->text("::kompoz::kt::notNull(");
            expr(x);
            out_->text(")");
        } else if ((u.text == "++" || u.text == "--") && delegatedName(x)) {
            // nextId++ as a value on a state: the state set, the old (postfix) or new (prefix) value given.
            out_->text(std::string("::kompoz::kt::") + (u.flag ? "post" : "pre") + (u.text == "++" ? "Increment(" : "Decrement(") + held(x.text) + ")");
        } else if (std::string r = u.text == "++" || u.text == "--" ? receiverHolding(x) : std::string(); !r.empty()) {
            // (the same on a receiver's member: an outer object's count++)
            std::string get = "KOMPOZ_KT_GET((" + r + "), " + cppName(x.text) + ")";
            out_->text(capture("[&]") + "() { auto kompoz_old = " + get + "; KOMPOZ_KT_SET((" + r + "), " + cppName(x.text) + ", " + setterOf(x.text) +
                       ", (kompoz_old " + u.text[0] + " 1)); return " + (u.flag ? std::string("kompoz_old") : get) + "; }()");
        } else if ((u.text == "++" || u.text == "--") && accessorName(x)) {  // the same through a property's setter
            std::string get = cppName(x.text) + "()", set = setterOf(x.text);
            out_->text(capture("[&]") + "() { auto kompoz_old = " + get + "; " + set + "(kompoz_old " + u.text[0] + " 1); return " +
                      (u.flag ? std::string("kompoz_old") : get) + "; }()");
        } else if (u.flag) {
            if (x.kind == Node::Index) indexed(x);  // counts[k]++: the element itself
            else expr(x);
            out_->text(u.text);
        } else {
            out_->text(u.text);
            if ((u.text == "++" || u.text == "--") && x.kind == Node::Index) indexed(x);
            else expr(x);
        }
    }

    void binary(const Node& b) {
        const std::string& op = b.text;
        const Node& l = *b.kids[0];
        const Node& r = *b.kids[1];
        // next() * 10 + next(): its operands left to right (&&, || and ?: evaluate the right one only if needed: as they are)
        if (op != "&&" && op != "||" && op != "?:" && ordered(b, {&l, &r}, [&] { binary(b); })) return;
        auto helper = [&](const char* fn, const Node& first, const Node& second) {
            out_->text(std::string("::kompoz::kt::") + fn + "(");
            expr(first);
            out_->text(", ");
            expr(second);
            out_->text(")");
        };
        if (op == "..") return helper("rangeTo", l, r);
        if (op == "..<" || op == "until") return helper("until", l, r);
        if (op == "downTo") return helper("downTo", l, r);
        if (op == "step") return helper("step", l, r);
        if (op == "in") return helper("contains", r, l);
        if (op == "!in") {
            out_->text("!");
            return helper("contains", r, l);
        }
        if (op == "to") return helper("to", l, r);
        // (enter togetherWith exit) using SizeTransform(clip = false); 0.4f at 75 using FastOutSlowInEasing
        if (op == "using") return helper("infixUsing", l, r);
        static const std::map<std::string, std::string, std::less<>> bitwise = {{"and", "&"}, {"or", "|"}, {"xor", "^"}, {"shl", "<<"}, {"shr", ">>"}};
        if (auto bit = bitwise.find(op); bit != bitwise.end()) {  // Kotlin's bitwise infix functions on integers
            out_->text("(");
            expr(l);
            out_->text(" " + bit->second + " ");
            expr(r);
            out_->text(")");
            return;
        }
        if (op == "?:") {
            out_->text("::kompoz::kt::elvis(");
            expr(l);
            out_->text(", " + capture("[&]") + " { return ");
            Scoped inside(global_);
            expr(r);
            out_->text("; })");
            return;
        }
        if ((op == "==" || op == "!=" || op == "===" || op == "!==") && (l.kind == Node::Null || r.kind == Node::Null)) {
            out_->text(op[0] == '!' ? "!::kompoz::kt::isNull(" : "::kompoz::kt::isNull(");
            expr(l.kind == Node::Null ? r : l);
            out_->text(")");
            return;
        }
        static const std::set<std::string, std::less<>> plain = {"+", "-", "*", "/", "%", "<", ">", "<=", ">=", "==", "!=", "&&", "||"};
        if (plain.contains(op) || op == "===" || op == "!==") {
            expr(l);
            out_->text(" " + (op == "===" ? std::string("==") : op == "!==" ? std::string("!=") : op) + " ");
            expr(r);
            return;
        }
        // An infix function of the receiver's, with both sides (DragValue.Start at 0f in DraggableAnchors { }).
        if (size_t rcv = std::isalpha(static_cast<unsigned char>(op[0])) ? receiverOf(op) : 0) {
            out_->text("(*kompoz_this_" + std::to_string(rcv) + ")." + cppName(op) + "(");
            expr(l);
            out_->text(", ");
            expr(r);
            out_->text(")");
            return;
        }
        // An infix function: a.name(b).
        expr(l);
        out_->text("." + cppName(op) + "(");
        expr(r);
        out_->text(")");
    }

    // if as a value: a ? b : c where both branches are one expression, else run in place.
    void ifExpression(const Node& n, const Type* expected = nullptr) {
        auto single = [](const Node* b) { return b && b->kids.size() == 1 && b->kids[0]->kind == Node::ExprStmt && b->kids[0]->kids[0]->kind != Node::If && b->kids[0]->kids[0]->kind != Node::When; };
        if (n.kids.size() > 2 && single(n.kids[1].get()) && single(n.kids[2].get())) {
            // if (c) { { … } } else null: a lambda or none (kt::nullableLambda: the function type it goes to, empty for none).
            const Node& then = *n.kids[1]->kids[0]->kids[0];
            const Node& otherwise = *n.kids[2]->kids[0]->kids[0];
            if ((then.kind == Node::Lambda && otherwise.kind == Node::Null) || (then.kind == Node::Null && otherwise.kind == Node::Lambda)) {
                out_->text(std::string("::kompoz::kt::nullableLambda(") + (then.kind == Node::Null ? "!(" : "("));
                expr(*n.kids[0]);
                out_->text("), ");
                lambda(then.kind == Node::Lambda ? then : otherwise, false);
                out_->text(")");
                return;
            }
            if (then.kind == Node::Null || otherwise.kind == Node::Null) {  // if (c) x else null: a nullable of x (kt::nullable, x only then)
                out_->text(std::string("::kompoz::kt::nullable(") + (then.kind == Node::Null ? "!(" : "("));
                expr(*n.kids[0]);
                out_->text("), " + capture("[&]") + "() { return ");
                expr(then.kind == Node::Null ? otherwise : then);
                out_->text("; })");
                return;
            }
            out_->text("(");
            std::vector<const Node*> casts;
            castCondition(*n.kids[0], casts);
            out_->text(" ? ");
            if (casts.empty()) {
                expr(*n.kids[1]->kids[0]->kids[0]);
            } else {  // (the smart casts' view: the value made where they are)
                out_->text(capture("[&]") + "() {");
                scopes_.emplace_back();
                smartCasts(casts);
                out_->text(" return ");
                expr(*n.kids[1]->kids[0]->kids[0]);
                out_->text("; }()");
                scopes_.pop_back();
            }
            out_->text(" : ");
            expr(*n.kids[2]->kids[0]->kids[0]);
            out_->text(")");
            return;
        }
        if (n.kids.size() < 3) throw CompileError(n.pos, "an if used as a value needs an else");
        // (a declared type is the lambda's: `val x: T? = if (…) a else if (…) b else null` returns a T? in every branch)
        out_->text(capture("[&]") + "()" + (expected ? " -> " + type(*expected) : std::string()) + " {");
        Scoped inside(global_);
        ifStatement(n, true);
        out_->text(" }()");
    }

    Output* out_;  // (switched where what is written goes elsewhere: a nested enum's entries)
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
    bool taskBlock_ = false, inTaskBlock_ = false;
    const DataClass* dataInit_ = nullptr;
    struct Substitute {
        std::string spelled;
        const Type* expected;
        bool used;
    };
    std::map<const Node*, Substitute> substitutes_;  // operands written as an in-order call's parameters (ordered)
    std::set<const Node*> orderingNow_;              // and the expressions being written so
    std::set<std::string, std::less<>> varLocals_;   // the locals declared var (what an operand may change)
    int ordered_ = 0;
    Overloads classMethods_;  // in a class, an interface or an object: its functions
    std::deque<Type> aliasTypes_;  // what aliases stand for, as resolved() gives them
    std::string* deferred_ = nullptr;  // what goes after the outermost type's definition (a nested enum's entries)
    std::string outerQualifier_;       // in a nested type: its outer types' names (Outer::)
    std::string superObject_;  // in a class with a base: what `super` is (its base's object, or the base class)
    const InterfaceMembers* superMembers_ = nullptr;  // and what it inherits  // in a data class's property's value: its lambdas keep copies of the fields  // the lambda written next is async's (it gives a task), and in it
    bool classTyping_ = false;   // in a class's delegated property's type (decltype at the class's scope)
    const Type* delegateValue_ = nullptr;  // and that property's type, written
    const Type* expectedCall_ = nullptr;  // what the call written next is expected to give (val tags: List<Titled> = listOf(…))
    std::string fieldOf_;        // in a property's accessor: what `field` is
    std::string interfaceSelf_;  // in an interface's object: its name (Object)
    std::set<std::string, std::less<>> interfaceMethods_;  // and its functions, its interfaces' too
    std::string selfPrefix_;     // in a type written there: how its members are reached
    std::string memberPrefix_;   // in a class template: this-> (its members may be a dependent base's)
    // An inner class's outer classes, outermost first: the receivers of its body (kompoz_this_1…N, members of its
    // object), each as the receiver names it, the C++ type held (a shared object, a class's pointer), its handle.
    struct OuterLink {
        std::string receiver, holder, handle;
    };
    std::vector<OuterLink> outers_;
    const ClassDecl* innerClass_ = nullptr;  // the inner class this generator defines
    struct InnerMade {
        const ClassDecl* decl;
        std::string outer;  // the outer object it is made with
    };
    std::map<std::string, InnerMade, std::less<>> innerMade_;  // inner classes made by their names here (Counter(1))
    std::string thisClass_;  // the class being defined (this@Name is its object)
    std::string staticSelf_;  // in an object's static functions: `this` as its value (Home{})
    std::set<std::string, std::less<>> memberFields_;  // the fields of the type being defined (read by their names)
    // While a type's body is written: its fields known as its own.
    struct OwnFields {
        Generator& g;
        std::set<std::string, std::less<>> was;
        OwnFields(Generator& gen, std::set<std::string, std::less<>> names) : g(gen), was(std::exchange(gen.memberFields_, std::move(names))) {}
        ~OwnFields() { g.memberFields_ = std::move(was); }
    };
    static std::set<std::string, std::less<>> fieldNames(const std::vector<ClassProperty>& properties) {
        std::set<std::string, std::less<>> out;
        for (const ClassProperty& p : properties) out.insert(p.name);
        return out;
    }
    std::string taskResult_;  // the next suspend lambda's value's type (a fun interface's suspend fun: TaskOf<T>)
    bool taskValue_ = false;  // in such a lambda: its last expression is co_returned
    std::map<std::string, std::string, std::less<>> parameterNames_;  // a base's arguments: val parameters as the parameters
    size_t parameterScope_ = 0;                                       // (declared in this scope)
    std::vector<std::map<std::string, VarKind, std::less<>>> scopes_;
};

}  // namespace

void generateEnum(Output& out, const EnumClass& e, const Module& module) {
    if (e.rich()) {
        Generator g(out, module);
        g.enumDefinition(e);
        return;
    }
    // C++'s enum class (what C++ around it may use as one), and Kotlin's name and ordinal for it (found by ADL:
    // KOMPOZ_KT_GET's dir.name, kt::str's "$dir", kt::enumEntries's Dir.entries).
    Generator::plainEnum(out, e, false);
}

void generateObject(Output& out, const ObjectDecl& o, const Module& module) {
    // (with members, an interface, or a sealed class's case of one with members: its own and those)
    auto membered = [&](const Type& t) {
        auto s = module.sealeds.find(t.name);
        return module.interfaces.contains(t.name) || (s != module.sealeds.end() && (s->second->hasMembers() || !s->second->supers.empty()));
    };
    if (o.hasMembers() || std::any_of(o.supers.begin(), o.supers.end(), membered)) {
        Module local = withNested(module, o.name, o.nested);
        Generator g(out, local);
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
    Module local = withNested(module, c.name, c.nested);
    Generator g(out, local);
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
    Module local = withNested(module, i.name, i.nested);
    Generator g(out, local);
    g.interfaceDefinition(i);
}

void declareInterface(Output& out, const InterfaceDecl& i) {
    out.at(i.pos);
    std::string head;
    for (size_t k = 0; k < i.typeParams.size(); ++k) head += (k ? ", class " : "template <class ") + i.typeParams[k];
    out.text(head + (head.empty() ? "" : "> ") + "class " + i.name + ";\n");
}

void generateAlias(Output& out, const TypeAlias& a, const Module& module) {
    Generator g(out, module);
    g.aliasDefinition(a);
}

void declareType(Output& out, const std::string& name, const std::vector<std::string>& typeParams, bool isClass) {
    std::string head;
    for (size_t k = 0; k < typeParams.size(); ++k) head += (k ? ", class " : "template <class ") + typeParams[k];
    out.text(head + (head.empty() ? "" : "> ") + (isClass ? "class " : "struct ") + name + ";\n");
}

void generateClassDecl(Output& out, const ClassDecl& c, const Module& module) {
    Module local = withNested(module, c.name, c.nested);
    Generator g(out, local);
    g.classDeclDefinition(c);
}

void generateDefinition(Output& out, const Function& f, const Module& module) {
    Generator g(out, module);
    g.definition(f);
}

}  // namespace kompozc
