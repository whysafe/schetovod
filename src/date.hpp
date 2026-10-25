#pragma once

#include <optional>
#include <string>
#include <string_view>
#include <tuple>

namespace sch {

struct Date {
    int year = 0;
    int month = 0;
    int day = 0;

    std::string toString() const;  // 2026-10-03

    friend bool operator==(const Date& a, const Date& b) {
        return std::tie(a.year, a.month, a.day) == std::tie(b.year, b.month, b.day);
    }
    friend bool operator!=(const Date& a, const Date& b) { return !(a == b); }
    friend bool operator<(const Date& a, const Date& b) {
        return std::tie(a.year, a.month, a.day) < std::tie(b.year, b.month, b.day);
    }
    friend bool operator<=(const Date& a, const Date& b) { return !(b < a); }
    friend bool operator>(const Date& a, const Date& b) { return b < a; }
};

bool isLeapYear(int year);
int daysInMonth(int year, int month);

// 2026-10-03, 2026/10/03 or 03.10.2026; nullopt if the day does not exist.
std::optional<Date> parseDate(std::string_view text);

// "окт 2026"
std::string monthName(int year, int month);

}  // namespace sch
