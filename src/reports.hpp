#pragma once

#include <optional>
#include <ostream>
#include <string>
#include <vector>

#include "journal.hpp"

namespace sch {

struct ReportOptions {
    std::optional<Date> from;  // inclusive
    std::optional<Date> to;    // inclusive
    std::vector<std::string> queries;  // case-insensitive substrings of account names
    int depth = 0;                     // 0 = all levels
    std::optional<std::string> valueIn;
};

bool inPeriod(const Date& d, const ReportOptions& o);
bool matchesAccount(const std::string& account, const ReportOptions& o);

void balanceReport(const Journal& j, const ReportOptions& o, std::ostream& out);
void registerReport(const Journal& j, const ReportOptions& o, std::ostream& out);
void accountsReport(const Journal& j, const ReportOptions& o, std::ostream& out);
void checkReport(const Journal& j, std::ostream& out);

struct MonthRow {
    int year = 0;
    int month = 0;
    Decimal income;
    Decimal expense;
};

struct MonthlyResult {
    std::string commodity;
    std::vector<MonthRow> rows;  // no gaps between the first and the last month
    Decimal income;
    Decimal expense;
};

MonthlyResult computeMonthly(const Journal& j, const ReportOptions& o);
void monthlyReport(const Journal& j, const ReportOptions& o, std::ostream& out);

// Налог на профессиональный доход (самозанятые): 4% с физлиц, 6% с юрлиц и ИП,
// одноразовый вычет 10 000 ₽ снижает ставки до 3% и 4%, налог до 100 ₽
// включительно переносится на следующий месяц.
struct NpdOptions {
    int year = 0;
    Decimal initialDeduction = Decimal::fromInt(10000);
    std::optional<std::string> rub;
};

struct NpdMonth {
    int year = 0;
    int month = 0;
    Decimal individuals;
    Decimal legal;
    Decimal tax;
    Decimal deduction;
    Decimal carriedIn;
    Decimal payable;
    bool carriedOut = false;
    Date due;
};

struct NpdResult {
    int year = 0;
    std::string rub;
    std::vector<NpdMonth> months;
    Decimal income;
    Decimal tax;
    Decimal deduction;
    Decimal deductionLeft;
    Decimal carryOver;
    std::optional<Date> limitExceededOn;
};

NpdResult computeNpd(const Journal& j, const NpdOptions& o);
void npdReport(const NpdResult& r, const Styles& styles, std::ostream& out);

}  // namespace sch
