#include <kompoz/strings.hpp>

#include <cctype>

namespace kompoz {

Strings Strings::english() { return {}; }

Strings Strings::turkish() {
    return {.language = "tr",
            .cut = "Kes",
            .copy = "Kopyala",
            .paste = "Yapıştır",
            .selectAll = "Tümünü seç",
            .selected = "Seçili",
            .checked = "İşaretli",
            .partiallyChecked = "Kısmen işaretli",
            .moreOptions = "Diğer seçenekler",
            .submenu = "Alt menü",
            .open = "Aç",
            .close = "Kapat",
            .openMenu = "Menüyü aç",
            .closeMenu = "Menüyü kapat",
            .dismiss = "Kapat",
            .dragHandle = "Sürükleme tutamacı",
            .pullToRefresh = "Yenilemek için çekin",
            .refreshing = "Yenileniyor",
            .minimize = "Simge durumuna küçült",
            .maximize = "Ekranı kapla",
            .restore = "Önceki boyutuna getir",
            .datePicker = DatePickerStrings::turkish(),
            .am = "ÖÖ",
            .pm = "ÖS",
            .hour = "Saat",
            .minute = "Dakika"};
}

Strings Strings::forLanguage(std::string_view language) {
    auto lower = [&](size_t i) { return i < language.size() ? char(std::tolower(static_cast<unsigned char>(language[i]))) : '\0'; };
    bool isTurkish = lower(0) == 't' && lower(1) == 'r' && (language.size() == 2 || !std::isalpha(static_cast<unsigned char>(language[2])));
    return isTurkish ? turkish() : english();
}

} // namespace kompoz
