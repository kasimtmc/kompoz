#pragma once

// The library's own texts: what its components show (a text field's menu, a date picker's
// months) or tell accessibility services (an icon's description, "Selected"), as Compose's string
// resources. They are in the system's language where Kompoz has it (English and Turkish; the
// platform tells the host, KOMPOZ_LANGUAGE=tr or en decides instead) and in English otherwise.
// An app with texts of its own provides them, whole or changed:
//
//   Strings mine = Strings::english();
//   mine.copy = "Duplicate";
//   CompositionLocalProvider({LocalStrings.provides(mine)}, content);

#include "runtime.hpp"

#include <array>
#include <string>
#include <string_view>

namespace kompoz {

// The names and formats a date picker shows (DatePicker, DateRangePicker).
struct DatePickerStrings {
    std::array<std::string, 12> months, shortMonths;  // January.., Jan..
    std::array<std::string, 7> weekdays;               // column labels, Sunday first
    unsigned firstDayOfWeek = 0;                       // 0 Sunday .. 6 Saturday
    bool dayFirst = false;                             // headline "17 Aug 2026" rather than "Aug 17, 2026"
    std::string title = "Select date", noSelection = "Selected date";
    std::string rangeTitle = "Select dates", startDate = "Start date", endDate = "End date";  // DateRangePicker
    // Typing a date (DisplayMode::Input): the order of its digits and the separator, as
    // "MM/dd/yyyy" (only the digits are typed); the field's label, the headline with nothing
    // typed, the errors (the pattern, the year range follow) and the mode button's descriptions.
    std::string inputPattern = "MM/dd/yyyy";
    std::string inputLabel = "Date", inputNoSelection = "Entered date";
    std::string invalidPattern = "Date does not match expected pattern: ", invalidYearRange = "Date out of expected year range ";
    std::string invalidRange = "Invalid date range input";  // a typed start after the end
    std::string switchToInput = "Switch to text input mode", switchToCalendar = "Switch to calendar input mode";
    // The calendar's buttons and the days' states, for accessibility.
    std::string selectYear = "Select year", previousMonth = "Previous month", nextMonth = "Next month";
    std::string selected = "Selected", inRange = "In range";
    static DatePickerStrings english();
    static DatePickerStrings turkish();
    friend bool operator==(const DatePickerStrings&, const DatePickerStrings&) = default;
};

struct Strings {
    std::string language = "en";  // the language they are in (ISO 639)
    // A text field's or a selection's menu and toolbar.
    std::string cut = "Cut", copy = "Copy", paste = "Paste", selectAll = "Select all";
    // For accessibility: what icons stand for and states mean.
    std::string selected = "Selected", checked = "Checked", partiallyChecked = "Partially checked";
    std::string moreOptions = "More options", submenu = "Submenu";
    std::string open = "Open", close = "Close";            // an exposed dropdown menu's arrow, a window
    std::string openMenu = "Open menu", closeMenu = "Close menu";  // a split button's trailing arrow
    std::string dismiss = "Dismiss";                       // a snackbar's close button
    std::string dragHandle = "Drag handle";                // a bottom sheet's
    std::string pullToRefresh = "Pull to refresh", refreshing = "Refreshing";
    // The title bar Kompoz draws where the desktop leaves a window undecorated (its close button: `close`).
    std::string minimize = "Minimize", maximize = "Maximize", restore = "Restore";
    // Pickers.
    DatePickerStrings datePicker = DatePickerStrings::english();
    std::string am = "AM", pm = "PM", hour = "Hour", minute = "Minute";  // TimePicker, TimeInput

    static Strings english();
    static Strings turkish();
    // A language as the platform names it ("tr", "tr-TR", "tr_TR.UTF-8"): Turkish's, else English's.
    static Strings forLanguage(std::string_view language);
    friend bool operator==(const Strings&, const Strings&) = default;
};
inline const CompositionLocal<Strings> LocalStrings{Strings{}};

} // namespace kompoz
