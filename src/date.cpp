#include "date.hpp"

#include <cstdio>

namespace sch {
namespace {

bool parseDigits(std::string_view s, int& out) {
    if (s.empty()) return false;
    int value = 0;
    for (const char c : s) {
        if (c < '0' || c > '9') return false;
        value = value * 10 + (c - '0');
    }
    out = value;
    return true;
}

bool isSeparator(char c) { return c == '-' || c == '/' || c == '.'; }

}  // namespace

std::string Date::toString() const {
    char buf[48];
    std::snprintf(buf, sizeof buf, "%04d-%02d-%02d", year, month, day);
    return buf;
}

bool isLeapYear(int year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

int daysInMonth(int year, int month) {
    static const int days[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && isLeapYear(year)) return 29;
    return days[month - 1];
}

std::optional<Date> parseDate(std::string_view text) {
    if (text.size() != 10) return std::nullopt;
    Date d;
    bool ok = false;
    if (isSeparator(text[4]) && text[7] == text[4]) {
        ok = parseDigits(text.substr(0, 4), d.year) && parseDigits(text.substr(5, 2), d.month) &&
             parseDigits(text.substr(8, 2), d.day);
    } else if (text[2] == '.' && text[5] == '.') {
        ok = parseDigits(text.substr(0, 2), d.day) && parseDigits(text.substr(3, 2), d.month) &&
             parseDigits(text.substr(6, 4), d.year);
    }
    if (!ok || d.year < 1900 || d.month < 1 || d.month > 12) return std::nullopt;
    if (d.day < 1 || d.day > daysInMonth(d.year, d.month)) return std::nullopt;
    return d;
}

std::string monthName(int year, int month) {
    static const char* names[] = {"янв", "фев", "мар", "апр", "май", "июн",
                                  "июл", "авг", "сен", "окт", "ноя", "дек"};
    return std::string(names[month - 1]) + " " + std::to_string(year);
}

}  // namespace sch
