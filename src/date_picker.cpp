// Material 3 date picker and its dialog.

#include <kompoz/draw.hpp>
#include <kompoz/icons.hpp>
#include <kompoz/lazy.hpp>
#include <kompoz/material.hpp>
#include <kompoz/pager.hpp>
#include <kompoz/state.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <format>

namespace kompoz {

using namespace std::chrono;

DatePickerStrings DatePickerStrings::english() {
    return {.months = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October",
                       "November", "December"},
            .shortMonths = {"Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"},
            .weekdays = {"S", "M", "T", "W", "T", "F", "S"}};
}

DatePickerStrings DatePickerStrings::turkish() {
    return {.months = {"Ocak", "Şubat", "Mart", "Nisan", "Mayıs", "Haziran", "Temmuz", "Ağustos", "Eylül", "Ekim", "Kasım",
                       "Aralık"},
            .shortMonths = {"Oca", "Şub", "Mar", "Nis", "May", "Haz", "Tem", "Ağu", "Eyl", "Eki", "Kas", "Ara"},
            .weekdays = {"Pz", "Pt", "Sa", "Ça", "Pe", "Cu", "Ct"},
            .firstDayOfWeek = 1,
            .dayFirst = true,
            .title = "Tarih seçin",
            .noSelection = "Tarih seçilmedi",
            .rangeTitle = "Tarih aralığı seçin",
            .startDate = "Başlangıç",
            .endDate = "Bitiş",
            .inputPattern = "dd.MM.yyyy",
            .inputLabel = "Tarih",
            .inputNoSelection = "Girilen tarih",
            .invalidPattern = "Tarih beklenen kalıpla eşleşmiyor: ",
            .invalidYearRange = "Tarih beklenen yıl aralığının dışında: ",
            .invalidRange = "Geçersiz tarih aralığı girişi",
            .switchToInput = "Metin girişine geç",
            .switchToCalendar = "Takvime geç",
            .selectYear = "Yıl seç",
            .previousMonth = "Önceki ay",
            .nextMonth = "Sonraki ay",
            .selected = "Seçili",
            .inRange = "Aralıkta"};
}

namespace {

// The months a picker offers, one index each.
struct MonthRange {
    MutableState<year_month> shown{year_month{year{2000}, January}};
    std::pair<int, int> years{1900, 2100};

    year_month clamp(year_month m) const {
        if (m.year() < year{years.first}) return year{years.first} / January;
        if (m.year() > year{years.second}) return year{years.second} / December;
        return m;
    }
    int indexOf(year_month m) const { return (int(m.year()) - years.first) * 12 + int(unsigned(m.month())) - 1; }
    year_month monthAt(int index) const { return year{years.first + index / 12} / month{unsigned(index % 12 + 1)}; }
    int months() const { return (years.second - years.first + 1) * 12; }
    void show(year_month m) {
        m = clamp(m);
        if (m != shown.peek()) shown.set(m);
    }
};

} // namespace

struct DatePickerState::Data : MonthRange {
    MutableState<std::optional<year_month_day>> selected{std::nullopt};
    MutableState<DisplayMode> mode{DisplayMode::Picker};
};

struct DateRangePickerState::Data : MonthRange {
    MutableState<std::optional<year_month_day>> start{std::nullopt}, end{std::nullopt};
    MutableState<DisplayMode> mode{DisplayMode::Picker};
};

namespace {

year_month_day localToday() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    localtime_s(&tm, &now);
#else
    localtime_r(&now, &tm);
#endif
    return year{tm.tm_year + 1900} / month{unsigned(tm.tm_mon + 1)} / day{unsigned(tm.tm_mday)};
}

std::string headline(const DatePickerStrings& s, year_month_day d) {
    const std::string& m = s.shortMonths[unsigned(d.month()) - 1];
    return s.dayFirst ? std::format("{} {} {}", unsigned(d.day()), m, int(d.year()))
                      : std::format("{} {}, {}", m, unsigned(d.day()), int(d.year()));
}

std::string longDate(const DatePickerStrings& s, year_month_day d) {
    const std::string& m = s.months[unsigned(d.month()) - 1];
    return s.dayFirst ? std::format("{} {} {}", unsigned(d.day()), m, int(d.year()))
                      : std::format("{} {}, {}", m, unsigned(d.day()), int(d.year()));
}

// Where a day is in a selected range: the band runs from the start's center to the end's.
enum class Band { None, Start, Inside, End };

void DayCell(std::function<void()> onClick, year_month_day date, bool selected, bool today, Band band, bool enabled,
             const DatePickerStrings& strings, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    Modifier circle = Modifier().size(40);
    if (selected) circle = circle.background(cs.primary, CircleShape);
    if (today && !selected) circle = circle.border(1, cs.primary, CircleShape);
    circle = circle.clip(CircleShape)
                 .clickable(std::move(onClick), enabled)
                 .semantics({.contentDescription = longDate(strings, date), .role = SemanticsRole::Button,
                             .stateDescription = selected ? strings.selected : band == Band::Inside ? strings.inRange : ""});
    Modifier cell = Modifier().weight(1).height(48);
    if (band != Band::None)
        cell = cell.drawBehind([band, color = cs.secondaryContainer](DrawScope& d) {
            float w = d.size().width, h = d.size().height;
            float x0 = band == Band::Start ? w / 2 : 0, x1 = band == Band::End ? w / 2 : w;
            d.drawRect(color, {x0, (h - 40) / 2}, Size{x1 - x0, 40});
        });
    Color text = selected               ? cs.onPrimary
                 : !enabled             ? cs.onSurface.copy(0.38f)
                 : band == Band::Inside ? cs.onSecondaryContainer
                 : today                ? cs.primary
                                        : cs.onSurface;
    Box(cell, Alignment::Center, [&] {
        Box(circle, Alignment::Center, [&] { Text(std::to_string(unsigned(date.day())), {.fontSize = 14, .color = text}); });
    });
}

// Which column a month's first day falls in, and how many days it has.
std::pair<int, int> monthLayout(year_month month, const DatePickerStrings& strings) {
    unsigned firstWeekday = weekday{sys_days{month / 1}}.c_encoding();
    return {int((firstWeekday + 7 - strings.firstDayOfWeek) % 7), int(unsigned(year_month_day_last{month / last}.day()))};
}

// How a calendar marks and picks days: one date (DatePicker) or a range.
struct DaySelection {
    std::function<bool(year_month_day)> selected;
    std::function<Band(year_month_day)> band;
    std::function<bool(year_month_day)> enabled;
    std::function<void(year_month_day)> pick;
    std::function<void(year_month)> moved;  // the user went to another month (arrows, swipe, a year)
};

// `date` in another month, on the same day or that month's last.
year_month_day sameDayIn(year_month_day date, year_month month) {
    return month / std::min(date.day(), year_month_day_last{month / last}.day());
}

// Six weeks of a month, starting on the strings' first day of the week.
void MonthGrid(year_month month, year_month_day today, const DatePickerStrings& strings, const DaySelection& days,
               CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    auto [offset, count] = monthLayout(month, strings);
    Column(Modifier().fillMaxWidth().padding(12, 0), [&] {
        for (int week = 0; week < 6; ++week)
            Row(Modifier().fillMaxWidth(), [&] {
                for (int k = 0; k < 7; ++k) {
                    int d = week * 7 + k - offset + 1;
                    if (d < 1 || d > count) {
                        Box(Modifier().weight(1).height(48));
                        continue;
                    }
                    year_month_day date = month / day{unsigned(d)};
                    key(d, [&] {
                        DayCell([pick = days.pick, date] { pick(date); }, date, days.selected(date), date == today, days.band(date),
                                days.enabled(date), strings);
                    });
                }
            });
    });
}

void WeekdayLabels(const DatePickerStrings& strings, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    Row(Modifier().fillMaxWidth().padding(12, 0), [&] {
        for (int k = 0; k < 7; ++k)
            Box(Modifier().weight(1).height(48), Alignment::Center, [&] {
                Text(strings.weekdays[(strings.firstDayOfWeek + unsigned(k)) % 7], {.fontSize = 14, .color = colorScheme().onSurface});
            });
    });
}

// The month on show with arrows and a swipeable pager, or (from its name) a grid of years.
void Calendar(std::shared_ptr<MonthRange> d, year_month_day today, const DatePickerStrings& strings, const DaySelection& days,
              CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    const Typography& t = typography();
    year_month shown = d->shown.get();
    int index = d->indexOf(shown);
    int count = d->months();
    auto pager = rememberPagerState(index, [d] { return d->months(); });
    auto yearPicking = remember([] { return mutableStateOf(false); });
    // Going to another month takes the date being picked along: the day, month and year can be
    // picked in any order. (A month set from code only shows it.)
    auto go = [d, moved = days.moved](year_month m) {
        m = d->clamp(m);
        if (m == d->shown.peek()) return;
        d->show(m);
        moved(m);
    };
    // The pager follows the month on show (arrows, a picked year) and the other way round (swipes).
    int current = pager.currentPage();
    LaunchedEffect(current, [d, go, current]() -> Task {
        go(d->monthAt(current));
        co_return;
    });
    LaunchedEffect(index, [pager, index]() -> Task {
        int from = pager.currentPage();
        if (from == index) co_return;
        if (std::abs(from - index) > 1)
            pager.scrollToPage(index);  // a far jump: no flight through every month in between
        else
            co_await pager.animateScrollToPage(index);
    });

    // The month and the arrows in the navigation content color (Compose's navigationContentColor).
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurfaceVariant)}, [&] {
    Row(Modifier().fillMaxWidth().height(56).padding(12, 0), {.verticalAlignment = Alignment::CenterVertically}, [&] {
        Row(Modifier().clip(RoundedCornerShape(20)).clickable([yearPicking] { yearPicking.set(!yearPicking.get()); }).padding(12, 8),
            {.horizontalArrangement = Arrangement::spacedBy(4), .verticalAlignment = Alignment::CenterVertically}, [&] {
                Text(std::format("{} {}", strings.months[unsigned(shown.month()) - 1], int(shown.year())),
                     {.font = t.labelLarge, .color = cs.onSurfaceVariant});
                Icon(yearPicking.get() ? Icons::Filled::KeyboardArrowUp : Icons::Filled::KeyboardArrowDown, strings.selectYear, Modifier().size(18));
            });
        Spacer(Modifier().weight(1));
        if (!yearPicking.get()) {
            IconButton([go, shown] { go(shown - months{1}); }, Modifier(), {.enabled = index > 0},
                       [&] { Icon(Icons::AutoMirrored::Filled::KeyboardArrowLeft, strings.previousMonth); });
            IconButton([go, shown] { go(shown + months{1}); }, Modifier(), {.enabled = index < count - 1},
                       [&] { Icon(Icons::AutoMirrored::Filled::KeyboardArrowRight, strings.nextMonth); });
        }
    });
    });
    if (yearPicking.get()) {
        // Years in three columns, the shown one picked out; it opens around it.
        auto grid = rememberLazyGridState();
        int years = d->years.second - d->years.first + 1, shownYear = int(shown.year());
        LaunchedEffect(0, [grid, d, shownYear]() -> Task {
            grid.scrollToItem(std::max(0, shownYear - d->years.first - 6));  // two rows above the shown year
            co_return;
        });
        LazyVerticalGrid(GridCells::Fixed(3), Modifier().fillMaxWidth().height(336), {.state = grid, .contentPadding = {12, 0, 12, 12}},
                         [&](LazyGridScope& cells) {
                             cells.items(years, [&, go, shown, yearPicking](int i) {
                                 int y = d->years.first + i;
                                 bool picked = y == shownYear;
                                 Box(Modifier().fillMaxWidth().height(52), Alignment::Center, [&] {
                                     Box(Modifier()
                                             .size(72, 36)
                                             .background(picked ? cs.primary : colors::Transparent, 18)
                                             .clip(RoundedCornerShape(18))
                                             .clickable([go, shown, yearPicking, y] {
                                                 go(year{y} / shown.month());
                                                 yearPicking.set(false);
                                             }),
                                         Alignment::Center, [&] {
                                             Text(std::to_string(y), {.fontSize = 16, .color = picked ? cs.onPrimary : cs.onSurfaceVariant});
                                         });
                                 });
                             });
                         });
    } else {
        WeekdayLabels(strings);
        HorizontalPager(pager, Modifier().fillMaxWidth().height(288), [&](int page) { MonthGrid(d->monthAt(page), today, strings, days); });
    }
}

} // namespace

DatePickerState::DatePickerState(std::optional<year_month_day> initialSelectedDate, std::optional<year_month> initialDisplayedMonth,
                                 std::pair<int, int> yearRange, DisplayMode initialDisplayMode)
    : data_(std::make_shared<Data>()) {
    data_->years = yearRange;
    data_->mode.set(initialDisplayMode);
    data_->selected.set(initialSelectedDate);
    year_month_day base = initialSelectedDate.value_or(localToday());
    data_->shown.set(data_->clamp(initialDisplayedMonth.value_or(base.year() / base.month())));
}

std::optional<year_month_day> DatePickerState::selectedDate() const { return data_->selected.get(); }
void DatePickerState::setSelectedDate(std::optional<year_month_day> date) const {
    if (date != data_->selected.peek()) data_->selected.set(date);
}
year_month DatePickerState::displayedMonth() const { return data_->shown.get(); }
void DatePickerState::setDisplayedMonth(year_month m) const { data_->show(m); }
std::pair<int, int> DatePickerState::yearRange() const { return data_->years; }
DisplayMode DatePickerState::displayMode() const { return data_->mode.get(); }
void DatePickerState::setDisplayMode(DisplayMode mode) const {
    if (mode != data_->mode.peek()) data_->mode.set(mode);
}

namespace {
// Saved dates: days from 1970-01-01 (nothing for none); months: year * 12 + month - 1.
SavedValue savedDate(const std::optional<year_month_day>& date) {
    return date ? SavedValue(int64_t(sys_days(*date).time_since_epoch().count())) : SavedValue();
}
std::optional<std::optional<year_month_day>> restoredDate(const SavedValue& v) {
    if (v.isNull()) return std::optional<year_month_day>{};
    std::optional<int64_t> n = v.integer();
    if (!n) return std::nullopt;
    return std::optional<year_month_day>(year_month_day(sys_days(days(*n))));
}
SavedValue savedMonth(year_month month) { return int64_t(int(month.year()) * 12 + int(unsigned(month.month())) - 1); }
std::optional<year_month> restoredMonth(const SavedValue& v) {
    std::optional<int64_t> n = v.integer();
    if (!n || *n < 0) return std::nullopt;
    return year_month(year(int(*n / 12)), month(unsigned(*n % 12 + 1)));
}
} // namespace

SavedValue Saver<DatePickerState>::save(const DatePickerState& s) {
    return SavedValue::Map{{"selected", savedDate(s.selectedDate())}, {"month", savedMonth(s.displayedMonth())},
                           {"from", s.yearRange().first}, {"to", s.yearRange().second}, {"mode", int64_t(s.displayMode())}};
}
std::optional<DatePickerState> Saver<DatePickerState>::restore(const SavedValue& v) {
    auto selected = restoredDate(v["selected"]);
    auto shown = restoredMonth(v["month"]);
    auto from = v["from"].integer(), to = v["to"].integer();
    auto mode = Saver<DisplayMode>::restore(v["mode"]);
    if (!selected || !shown || !from || !to || !mode) return std::nullopt;
    return DatePickerState(*selected, *shown, {int(*from), int(*to)}, *mode);
}

DatePickerState rememberDatePickerState(std::optional<year_month_day> initialSelectedDate,
                                        std::optional<year_month> initialDisplayedMonth, std::pair<int, int> yearRange,
                                        DisplayMode initialDisplayMode, CallSite site) {
    return rememberSaveable([&] { return DatePickerState(initialSelectedDate, initialDisplayedMonth, yearRange, initialDisplayMode); }, site);
}

namespace {
// A date typed as digits in the pattern's order ("dd.MM.yyyy": 8 of them): the date, if real.
std::optional<year_month_day> parseDigits(std::string_view digits, std::string_view pattern) {
    std::string order;  // the pattern without its separators
    for (char c : pattern)
        if (c == 'd' || c == 'M' || c == 'y') order += c;
    if (digits.size() != order.size()) return std::nullopt;
    int d = 0, m = 0, y = 0;
    for (size_t i = 0; i < order.size(); ++i) {
        int& part = order[i] == 'd' ? d : order[i] == 'M' ? m : y;
        part = part * 10 + (digits[i] - '0');
    }
    year_month_day date{year{y}, month{unsigned(m)}, day{unsigned(d)}};
    return date.ok() ? std::optional(date) : std::nullopt;
}

std::string digitsOf(year_month_day date, std::string_view pattern) {
    std::string out;
    int count[3] = {};  // how many of each so far, for the width of each part
    for (char c : pattern)
        if (c == 'd' || c == 'M' || c == 'y') ++count[c == 'd' ? 0 : c == 'M' ? 1 : 2];
    bool done[3] = {};
    for (char c : pattern) {
        int k = c == 'd' ? 0 : c == 'M' ? 1 : c == 'y' ? 2 : -1;
        if (k < 0 || done[k]) continue;
        done[k] = true;
        int value = k == 0 ? int(unsigned(date.day())) : k == 1 ? int(unsigned(date.month())) : int(date.year());
        out += std::format("{:0{}}", value, count[k]);
    }
    return out;
}

std::string upper(std::string s) {
    for (char& c : s) c = char(std::toupper(static_cast<unsigned char>(c)));
    return s;
}

// A date typed in an outlined field (Compose's DateInputTextField): digits only, the separators
// shown by an output transformation, told the date once it is whole, real, in the year range and
// passes `check` (an error of its own otherwise; nothing while it is still being typed).
void DateTextField(std::optional<year_month_day> initial, const std::string& label, const DatePickerStrings& strings,
                   std::pair<int, int> years, std::function<std::string(year_month_day)> check,
                   std::function<void(std::optional<year_month_day>)> onDate, const Modifier& modifier, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    auto text = remember([&] { return mutableStateOf(initial ? digitsOf(*initial, strings.inputPattern) : std::string()); });
    auto error = remember([] { return mutableStateOf(std::string()); });
    std::string pattern = strings.inputPattern;
    size_t length = 0;
    for (char c : pattern) length += c == 'd' || c == 'M' || c == 'y';
    // Where a separator follows a digit: after the 2nd and the 4th of "dd.MM.yyyy".
    std::vector<std::pair<int, char>> separators;
    for (size_t i = 0, digits = 0; i < pattern.size(); ++i) {
        if (pattern[i] == 'd' || pattern[i] == 'M' || pattern[i] == 'y') ++digits;
        else separators.push_back({int(digits), pattern[i]});
    }
    auto onChange = [text, error, pattern, length, years, strings, check, onDate](std::string v) {
        if (v.size() > length || !std::all_of(v.begin(), v.end(), [](char c) { return c >= '0' && c <= '9'; })) return;
        text.set(v);
        if (v.size() < length) {
            error.set("");
            onDate(std::nullopt);
            return;
        }
        std::optional<year_month_day> date = parseDigits(v, pattern);
        std::string problem = !date ? strings.invalidPattern + upper(pattern)
                              : int(date->year()) < years.first || int(date->year()) > years.second
                                  ? std::format("{}{} - {}", strings.invalidYearRange, years.first, years.second)
                              : check ? check(*date)
                                      : std::string();
        error.set(problem);
        onDate(problem.empty() ? date : std::nullopt);
    };
    std::string problem = error.get();
    OutlinedTextField(text.get(), onChange, modifier.padding(0, 0, 0, problem.empty() ? 16.f : 12.f),
                      {.singleLine = true,
                       .placeholder = upper(pattern),
                       .label = label,
                       .supportingText = problem,
                       .isError = !problem.empty(),
                       .keyboardOptions = {.autoCorrect = false, .keyboardType = KeyboardType::Number, .imeAction = ImeAction::Done},
                       .outputTransformation = OutputTransformation{[separators](TextFieldBuffer& b) {
                           for (size_t i = separators.size(); i-- > 0;)  // from the end, the earlier places stay put
                               if (int(b.text().size()) >= separators[i].first)
                                   b.insert(separators[i].first, std::string(1, separators[i].second));
                       }}});
}

// The typed date (Compose's DateInputContent): picked once it is whole and valid, the calendar
// then opening on its month.
void DateInputField(const DatePickerState& state, const DatePickerStrings& strings, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    DateTextField(state.selectedDate(), strings.inputLabel, strings, state.yearRange(), {}, [state](std::optional<year_month_day> date) {
        state.setSelectedDate(date);
        if (date) state.setDisplayedMonth(date->year() / date->month());
    }, Modifier().fillMaxWidth().padding(24, 10, 24, 0));
}

// The start and the end typed side by side (Compose's DateRangeInputContent). Each field's last
// valid date is kept here: a start mistyped for a moment doesn't lose the end typed already (the
// state holds no end without a start).
void DateRangeInputFields(const DateRangePickerState& state, const DatePickerStrings& strings, CallSite site = CallSite::current()) {
    ComposableScope scope(site);
    auto typed = remember([&] { return std::make_shared<std::pair<std::optional<year_month_day>, std::optional<year_month_day>>>(
                                    state.selectedStartDate(), state.selectedEndDate()); });
    Row(Modifier().fillMaxWidth().padding(24, 10, 24, 0), {.horizontalArrangement = Arrangement::spacedBy(8)}, [&] {
        DateTextField(typed->first, strings.startDate, strings, state.yearRange(),
                      [typed, strings](year_month_day d) { return typed->second && d > *typed->second ? strings.invalidRange : std::string(); },
                      [state, typed](std::optional<year_month_day> d) {
                          typed->first = d;
                          state.setSelection(d, typed->second);
                          if (d) state.setDisplayedMonth(d->year() / d->month());
                      }, Modifier().weight(1));
        DateTextField(typed->second, strings.endDate, strings, state.yearRange(),
                      [typed, strings](year_month_day d) { return typed->first && d < *typed->first ? strings.invalidRange : std::string(); },
                      [state, typed](std::optional<year_month_day> d) {
                          typed->second = d;
                          state.setSelection(typed->first, d);
                      }, Modifier().weight(1));
    });
}
} // namespace

void DatePicker(const DatePickerState& state, const Modifier& modifier, DatePickerOptions options, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    DatePickerStrings strings = options.strings.value_or(LocalStrings.current().datePicker);
    std::optional<year_month_day> selected = state.selectedDate();
    DaySelection days{.selected = [selected](year_month_day d) { return d == selected; },
                      .band = [](year_month_day) { return Band::None; },
                      .enabled = [](year_month_day) { return true; },
                      .pick = [state](year_month_day d) { state.setSelectedDate(d); },
                      .moved = [state](year_month m) {
                          if (auto d = state.selectedDate()) state.setSelectedDate(sameDayIn(*d, m));
                      }};
    bool typing = state.displayMode() == DisplayMode::Input;
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurfaceVariant)}, [&] {
        Column(modifier.widthIn(0, 360).fillMaxWidth(), [&] {
            Column(Modifier().fillMaxWidth().padding(24, 16, 12, 12), [&] {
                Text(strings.title, {.font = typography().labelLarge, .color = cs.onSurfaceVariant});
                Spacer(Modifier().height(36));
                Row(Modifier().fillMaxWidth(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                    Text(selected ? headline(strings, *selected) : typing ? strings.inputNoSelection : strings.noSelection, Modifier().weight(1),
                         {.fontSize = 32, .color = cs.onSurface, .maxLines = 1, .overflow = TextOverflow::Ellipsis});
                    if (options.showModeToggle)  // the pencil to type the date, the calendar to pick it
                        CompositionLocalProvider({LocalContentColor.provides(cs.onSurface)}, [&] {
                            IconButton([state, typing] { state.setDisplayMode(typing ? DisplayMode::Picker : DisplayMode::Input); }, [&] {
                                Icon(typing ? Icons::Filled::DateRange : Icons::Filled::Edit, typing ? strings.switchToCalendar : strings.switchToInput);
                            });
                        });
                });
            });
            HorizontalDivider();
            if (typing) DateInputField(state, strings);
            else Calendar(state.data_, options.today.value_or(localToday()), strings, days);
        });
    });
}

DateRangePickerState::DateRangePickerState(std::optional<year_month_day> initialSelectedStartDate,
                                           std::optional<year_month_day> initialSelectedEndDate,
                                           std::optional<year_month> initialDisplayedMonth, std::pair<int, int> yearRange,
                                           DisplayMode initialDisplayMode)
    : data_(std::make_shared<Data>()) {
    data_->years = yearRange;
    data_->mode.set(initialDisplayMode);
    setSelection(initialSelectedStartDate, initialSelectedEndDate);
    year_month_day base = initialSelectedStartDate.value_or(localToday());
    data_->shown.set(data_->clamp(initialDisplayedMonth.value_or(base.year() / base.month())));
}

std::optional<year_month_day> DateRangePickerState::selectedStartDate() const { return data_->start.get(); }
std::optional<year_month_day> DateRangePickerState::selectedEndDate() const { return data_->end.get(); }
void DateRangePickerState::setSelection(std::optional<year_month_day> start, std::optional<year_month_day> end) const {
    if (!start || (end && *end < *start)) end.reset();
    if (start != data_->start.peek()) data_->start.set(start);
    if (end != data_->end.peek()) data_->end.set(end);
}
year_month DateRangePickerState::displayedMonth() const { return data_->shown.get(); }
void DateRangePickerState::setDisplayedMonth(year_month m) const { data_->show(m); }
DisplayMode DateRangePickerState::displayMode() const { return data_->mode.get(); }
void DateRangePickerState::setDisplayMode(DisplayMode mode) const {
    if (mode != data_->mode.peek()) data_->mode.set(mode);
}
std::pair<int, int> DateRangePickerState::yearRange() const { return data_->years; }

DateRangePickerState rememberDateRangePickerState(std::optional<year_month_day> initialSelectedStartDate,
                                                  std::optional<year_month_day> initialSelectedEndDate,
                                                  std::optional<year_month> initialDisplayedMonth, std::pair<int, int> yearRange,
                                                  DisplayMode initialDisplayMode, CallSite site) {
    return rememberSaveable([&] {
        return DateRangePickerState(initialSelectedStartDate, initialSelectedEndDate, initialDisplayedMonth, yearRange, initialDisplayMode);
    }, site);
}

SavedValue Saver<DateRangePickerState>::save(const DateRangePickerState& s) {
    return SavedValue::Map{{"start", savedDate(s.selectedStartDate())}, {"end", savedDate(s.selectedEndDate())},
                           {"month", savedMonth(s.displayedMonth())}, {"from", s.yearRange().first}, {"to", s.yearRange().second},
                           {"mode", int64_t(s.displayMode())}};
}
std::optional<DateRangePickerState> Saver<DateRangePickerState>::restore(const SavedValue& v) {
    auto start = restoredDate(v["start"]), end = restoredDate(v["end"]);
    auto shown = restoredMonth(v["month"]);
    auto from = v["from"].integer(), to = v["to"].integer();
    auto mode = Saver<DisplayMode>::restore(v["mode"]);
    if (!start || !end || !shown || !from || !to || !mode) return std::nullopt;
    return DateRangePickerState(*start, *end, *shown, {int(*from), int(*to)}, *mode);
}

void DateRangePicker(const DateRangePickerState& state, const Modifier& modifier, DatePickerOptions options, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    DatePickerStrings strings = options.strings.value_or(LocalStrings.current().datePicker);
    auto d = state.data_;
    std::optional<year_month_day> start = state.selectedStartDate(), end = state.selectedEndDate();
    // The calendar picks the start or the end, each like a DatePicker; the end waits for a start.
    auto editingEnd = remember([] { return mutableStateOf(false); });
    bool pickingEnd = editingEnd.get() && start;
    bool typing = state.displayMode() == DisplayMode::Input;
    DaySelection days{
        .selected = [start, end](year_month_day day) { return day == start || day == end; },
        .band =
            [start, end](year_month_day day) {
                if (!start || !end || *start == *end) return Band::None;
                return day == *start ? Band::Start : day == *end ? Band::End : day > *start && day < *end ? Band::Inside : Band::None;
            },
        .enabled = [start, pickingEnd](year_month_day day) { return !pickingEnd || day >= *start; },
        .pick =
            [state, pickingEnd](year_month_day day) {
                if (pickingEnd)
                    state.setSelection(state.selectedStartDate(), day);
                else
                    state.setSelection(day, state.selectedEndDate());  // an end now before it is dropped
            },
        .moved =
            [state, pickingEnd](year_month m) {
                auto s = state.selectedStartDate(), e = state.selectedEndDate();
                if (pickingEnd && e)
                    state.setSelection(s, sameDayIn(*e, m));
                else if (!pickingEnd && s)
                    state.setSelection(sameDayIn(*s, m), e);
            }};
    // Start and end in the headline, each a tab that shows its month.
    auto tab = [&](bool isEnd, std::optional<year_month_day> date, const std::string& placeholder) {
        if (typing) {  // typed in the fields below: the headline only shows them
            Text(date ? headline(strings, *date) : placeholder, Modifier().padding(8, 4),
                 {.fontSize = 22, .color = cs.onSurface, .maxLines = 1, .overflow = TextOverflow::Ellipsis});
            return;
        }
        bool active = isEnd == pickingEnd;
        bool enabled = !isEnd || start;
        Box(Modifier()
                .clip(RoundedCornerShape(8))
                .background(active ? cs.secondaryContainer : colors::Transparent)
                .clickable([editingEnd, d, isEnd, date] {
                    editingEnd.set(isEnd);
                    if (date) d->show(date->year() / date->month());
                }, enabled)
                .semantics({.contentDescription = placeholder, .role = SemanticsRole::Tab, .checked = active})
                .padding(8, 4),
            [&] {
                Text(date ? headline(strings, *date) : placeholder,
                     {.fontSize = 22, .color = active ? cs.onSecondaryContainer : enabled ? cs.onSurface : cs.onSurface.copy(0.38f),
                      .maxLines = 1, .overflow = TextOverflow::Ellipsis});
            });
    };
    CompositionLocalProvider({LocalContentColor.provides(cs.onSurfaceVariant)}, [&] {
        Column(modifier.widthIn(0, 360).fillMaxWidth(), [&] {
            Column(Modifier().fillMaxWidth().padding(16, 16, 12, 8), [&] {
                Text(strings.rangeTitle, Modifier().padding(8, 0, 0, 0), {.font = typography().labelLarge, .color = cs.onSurfaceVariant});
                Spacer(Modifier().height(28));
                Row(Modifier().fillMaxWidth(), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                    // The button stays at the end, all of it: two long dates give way, the end's cut off first.
                    Row(Modifier().weight(1), {.verticalAlignment = Alignment::CenterVertically}, [&] {
                        tab(false, start, strings.startDate);
                        Text("–", Modifier().padding(4, 0), {.fontSize = 22, .color = cs.onSurface});
                        tab(true, end, strings.endDate);
                    });
                    if (options.showModeToggle)
                        CompositionLocalProvider({LocalContentColor.provides(cs.onSurface)}, [&] {
                            IconButton([state, typing] { state.setDisplayMode(typing ? DisplayMode::Picker : DisplayMode::Input); }, [&] {
                                Icon(typing ? Icons::Filled::DateRange : Icons::Filled::Edit, typing ? strings.switchToCalendar : strings.switchToInput);
                            });
                        });
                });
            });
            HorizontalDivider();
            if (typing) DateRangeInputFields(state, strings);
            else Calendar(d, options.today.value_or(localToday()), strings, days);
        });
    });
}

void DatePickerDialog(std::function<void()> onDismissRequest, const Content& confirmButton, const Content& dismissButton,
                      const Content& content, CallSite site) {
    ComposableScope scope(site);
    const ColorScheme& cs = colorScheme();
    // 360 dp wide, as Compose's (not the platform's dialog width, 48 dp in from each side).
    Dialog(std::move(onDismissRequest), [&] {
        Column(Modifier().widthIn(0, 360).fillMaxWidth().background(cs.surfaceContainerHigh, 28).padding(0, 0, 0, 12), [&] {
            content();
            Row(Modifier().fillMaxWidth().padding(12, 8, 12, 0), {.horizontalArrangement = Arrangement::spacedBy(8, Arrangement::Kind::End)},
                [&] {
                    if (dismissButton) dismissButton();
                    if (confirmButton) confirmButton();
                });
        });
    }, {.usePlatformDefaultWidth = false});
}

} // namespace kompoz
