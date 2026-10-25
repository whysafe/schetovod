#include <gtest/gtest.h>

#include <cstdint>
#include <sstream>
#include <string>

#include "parser.hpp"
#include "reports.hpp"

using sch::Date;
using sch::Decimal;
using sch::Journal;
using sch::NpdOptions;

namespace {

Journal parse(const std::string& text) { return sch::parseJournal(text, "t.journal"); }

NpdOptions forYear(int year) {
    NpdOptions o;
    o.year = year;
    return o;
}

Decimal rub(std::int64_t value) { return Decimal::fromInt(value); }

const char* kSmallReceipts = R"(
2026-09-03 Заказ  ; нпд: юр
    активы:банк    38 000 ₽
    доходы:сайты

2026-09-12 Правки  ; нпд: физ
    активы:банк    12 500 ₽
    доходы:сайты

2026-10-14 Консультация  ; нпд: физ
    активы:банк    2 000 ₽
    доходы:консультации

2026-11-03 Консультация  ; нпд: физ
    активы:банк    1 500 ₽
    доходы:консультации

2026-12-01 Консультация  ; нпд: физ
    активы:банк    3 000 ₽
    доходы:консультации
)";

}  // namespace

TEST(Npd, RatesDeductionAndSmallTaxCarry) {
    const auto r = sch::computeNpd(parse(kSmallReceipts), forYear(2026));
    ASSERT_EQ(r.months.size(), 4u);

    // 38 000 × 6% − 2% вычета = 1 520; 12 500 × 4% − 1% = 375.
    const auto& sep = r.months[0];
    EXPECT_EQ(sep.legal, rub(38000));
    EXPECT_EQ(sep.individuals, rub(12500));
    EXPECT_EQ(sep.tax, rub(1895));
    EXPECT_EQ(sep.deduction, rub(885));
    EXPECT_EQ(sep.payable, rub(1895));
    EXPECT_EQ(sep.due, (Date{2026, 10, 28}));

    // 60 ₽ за октябрь не платится, а прибавляется к ноябрю: 60 + 45 = 105.
    EXPECT_TRUE(r.months[1].carriedOut);
    EXPECT_TRUE(r.months[1].payable.isZero());
    EXPECT_EQ(r.months[2].carriedIn, rub(60));
    EXPECT_EQ(r.months[2].payable, rub(105));
    EXPECT_EQ(r.months[2].due, (Date{2026, 12, 28}));

    // 90 ₽ за декабрь уходят на январь следующего года.
    EXPECT_TRUE(r.months[3].carriedOut);
    EXPECT_EQ(r.carryOver, rub(90));

    EXPECT_EQ(r.income, rub(57000));
    EXPECT_EQ(r.tax, rub(2090));
    EXPECT_EQ(r.deduction, rub(950));
    EXPECT_EQ(r.deductionLeft, rub(9050));
}

TEST(Npd, DeductionIsSpentOnceAcrossYears) {
    const Journal j = parse(R"(
2025-03-01 Большой заказ  ; нпд: юр
    активы:банк    450 000 ₽
    доходы

2026-01-15 Заказ  ; нпд: юр
    активы:банк    100 000 ₽
    доходы

2026-02-10 Заказ  ; нпд: физ
    активы:банк    10 000 ₽
    доходы
)");
    const auto r = sch::computeNpd(j, forYear(2026));
    ASSERT_EQ(r.months.size(), 2u);
    // 9 000 ₽ вычета ушло в 2025 году, на январь остаётся только 1 000.
    EXPECT_EQ(r.months[0].deduction, rub(1000));
    EXPECT_EQ(r.months[0].tax, rub(5000));
    EXPECT_EQ(r.months[1].tax, rub(400));
    EXPECT_TRUE(r.deductionLeft.isZero());

    NpdOptions noDeduction = forYear(2026);
    noDeduction.initialDeduction = Decimal{};
    EXPECT_EQ(sch::computeNpd(j, noDeduction).months[0].tax, rub(6000));
}

TEST(Npd, ForeignIncomeUsesRateOfReceiptDay) {
    const Journal j = parse(R"(
P 2026-09-01 USD 91,80 ₽
P 2026-09-25 USD 95 ₽

2026-09-20 Игра на Unity  ; нпд: юр
    активы:payoneer    650 USD
    доходы:игры
)");
    const auto r = sch::computeNpd(j, forYear(2026));
    ASSERT_EQ(r.months.size(), 1u);
    EXPECT_EQ(r.months[0].legal, rub(59670));
    EXPECT_EQ(r.months[0].tax.raw(), 23868000);  // 3 580,20 − 1 193,40
}

TEST(Npd, WarnsWhenYearlyLimitIsExceeded) {
    const Journal j = parse(R"(
2026-03-01 Контракт  ; нпд: юр
    активы:банк    2 000 000 ₽
    доходы

2026-11-14 Контракт  ; нпд: юр
    активы:банк    500 000 ₽
    доходы
)");
    const auto r = sch::computeNpd(j, forYear(2026));
    ASSERT_TRUE(r.limitExceededOn.has_value());
    EXPECT_EQ(*r.limitExceededOn, (Date{2026, 11, 14}));
}

TEST(Npd, RejectsUnknownPayerAndRefunds) {
    EXPECT_THROW(sch::computeNpd(parse("2026-05-01 X ; нпд: самозанятый\n    активы  1 ₽\n    доходы\n"), forYear(2026)),
                 sch::JournalError);
    EXPECT_THROW(sch::computeNpd(parse("2026-05-01 Возврат ; нпд: физ\n    доходы  1 000 ₽\n    активы\n"), forYear(2026)),
                 sch::JournalError);
    EXPECT_THROW(sch::computeNpd(parse("2026-05-01 X ; нпд: физ\n    активы  1 ₽\n    капитал\n"), forYear(2026)),
                 sch::JournalError);
}

TEST(Npd, ReportShowsDueDatesAndCarry) {
    std::ostringstream out;
    const Journal j = parse(kSmallReceipts);
    sch::npdReport(sch::computeNpd(j, forYear(2026)), j.styles, out);
    const std::string s = out.str();
    EXPECT_NE(s.find("до 2026-10-28"), std::string::npos) << s;
    EXPECT_NE(s.find("переносится"), std::string::npos) << s;
    EXPECT_NE(s.find("1 895,00 ₽"), std::string::npos) << s;
}
