#include <gtest/gtest.h>

#include <algorithm>
#include <sstream>
#include <string>

#include "ledger.hpp"
#include "parser.hpp"
#include "reports.hpp"

using sch::Decimal;
using sch::Journal;
using sch::ReportOptions;

namespace {

Journal parse(const std::string& text) { return sch::parseJournal(text, "t.journal"); }

const char* kFreelance = R"(
2026-09-01 * Старт
    активы:банк:тинькофф    10 000 ₽
    активы:наличные          2 000 ₽
    капитал

2026-09-03 * Заказ  ; нпд: юр
    активы:банк:тинькофф    38 000 ₽
    доходы:сайты

2026-09-05 Хостинг
    расходы:работа:хостинг   1 890 ₽
    активы:банк:тинькофф

2026-11-02 Еда
    расходы:еда               500 ₽
    активы:наличные
)";

const char* kCurrencies = R"(
P 2026-09-01 USD 90 ₽
P 2026-10-01 USD 100 ₽

2026-09-10 Зарубежный заказ
    активы:usd    100 USD
    доходы:игры

2026-09-12 Хостинг
    расходы:хостинг   1 000 ₽
    активы:банк       -1 000 ₽

2026-11-05 Кофе
    расходы:еда   300 ₽
    активы:банк  -300 ₽
)";

std::string balance(const Journal& j, const ReportOptions& o) {
    std::ostringstream out;
    sch::balanceReport(j, o, out);
    return out.str();
}

}  // namespace

TEST(Balance, PrintsTreeWithSubtotals) {
    const std::string expected =
        "  47 610 ₽  активы\n"
        "  46 110 ₽    банк\n"
        "  46 110 ₽      тинькофф\n"
        "   1 500 ₽    наличные\n"
        " -38 000 ₽  доходы\n"
        " -38 000 ₽    сайты\n"
        " -12 000 ₽  капитал\n"
        "   2 390 ₽  расходы\n"
        "     500 ₽    еда\n"
        "   1 890 ₽    работа\n"
        "   1 890 ₽      хостинг\n"
        "----------\n"
        "         0\n";
    EXPECT_EQ(balance(parse(kFreelance), {}), expected);
}

TEST(Balance, DepthAndQuery) {
    const Journal j = parse(kFreelance);
    ReportOptions top;
    top.depth = 1;
    EXPECT_EQ(balance(j, top),
              "  47 610 ₽  активы\n"
              " -38 000 ₽  доходы\n"
              " -12 000 ₽  капитал\n"
              "   2 390 ₽  расходы\n"
              "----------\n"
              "         0\n");

    ReportOptions spending;
    spending.queries = {"РАСХОДЫ"};
    const std::string out = balance(j, spending);
    EXPECT_NE(out.find("хостинг"), std::string::npos);
    EXPECT_EQ(out.find("активы"), std::string::npos);
    EXPECT_NE(out.find("----------\n   2 390 ₽\n"), std::string::npos) << out;
}

TEST(Balance, PeriodFilter) {
    ReportOptions november;
    november.from = sch::Date{2026, 11, 1};
    november.to = sch::Date{2026, 11, 30};
    const std::string out = balance(parse(kFreelance), november);
    EXPECT_NE(out.find("    -500 ₽    наличные"), std::string::npos) << out;
    EXPECT_EQ(out.find("тинькофф"), std::string::npos) << out;
}

TEST(Balance, ValuesForeignCurrencyAtReportDate) {
    ReportOptions o;
    o.valueIn = "₽";
    const std::string out = balance(parse(kCurrencies), o);
    // 100 USD at the rate of 2026-10-01, not the one of the day it arrived.
    EXPECT_NE(out.find("10 000 ₽    usd"), std::string::npos) << out;
}

TEST(Balance, CurrencyExchangeTotalsToZero) {
    const Journal j = parse(R"(
2026-09-20 Заказ
    активы:payoneer    650 USD
    доходы:игры

2026-10-02 Вывод
    активы:payoneer    -600 USD @ 92,40 ₽
    активы:банк

P 2026-11-01 USD 93,10 ₽
)");
    const std::string plain = balance(j, {});
    EXPECT_NE(plain.find(" -55 440 ₽    обмен"), std::string::npos) << plain;
    EXPECT_EQ(plain.substr(plain.size() - 3), " 0\n") << plain;

    // At the later rate the trading account shows the exchange gain.
    ReportOptions valued;
    valued.valueIn = "₽";
    const std::string out = balance(j, valued);
    EXPECT_NE(out.find("420 ₽    обмен"), std::string::npos) << out;
    EXPECT_EQ(out.substr(out.size() - 3), " 0\n") << out;
}

TEST(Register, RunningTotal) {
    ReportOptions o;
    o.queries = {"тинькофф"};
    std::ostringstream out;
    sch::registerReport(parse(kFreelance), o, out);
    const std::string s = out.str();
    EXPECT_NE(s.find("2026-09-01 Старт"), std::string::npos) << s;
    EXPECT_NE(s.find("-1 890 ₽       46 110 ₽\n"), std::string::npos) << s;
    EXPECT_EQ(std::count(s.begin(), s.end(), '\n'), 3);
}

TEST(Monthly, ConvertsAtTransactionDateAndFillsGaps) {
    const auto r = sch::computeMonthly(parse(kCurrencies), {});
    EXPECT_EQ(r.commodity, "₽");
    ASSERT_EQ(r.rows.size(), 3u);
    EXPECT_EQ(r.rows[0].income, Decimal::fromInt(9000));
    EXPECT_EQ(r.rows[0].expense, Decimal::fromInt(1000));
    EXPECT_EQ(r.rows[1].month, 10);
    EXPECT_TRUE(r.rows[1].income.isZero());
    EXPECT_EQ(r.rows[2].expense, Decimal::fromInt(300));
    EXPECT_EQ(r.income, Decimal::fromInt(9000));
    EXPECT_EQ(r.expense, Decimal::fromInt(1300));

    std::ostringstream out;
    sch::monthlyReport(parse(kCurrencies), {}, out);
    EXPECT_NE(out.str().find("окт 2026"), std::string::npos) << out.str();
}

TEST(Monthly, MissingRateIsAnError) {
    ReportOptions o;
    o.valueIn = "EUR";
    EXPECT_THROW(sch::computeMonthly(parse(kCurrencies), o), sch::ConversionError);
}

TEST(Prices, InverseAndImpliedRates) {
    const Journal j = parse(R"(
P 2026-09-01 USD 96,40 ₽

2026-10-02 Вывод
    активы:payoneer    -600 USD @ 92,40 ₽
    активы:банк
)");
    const sch::PriceDb db(j);
    EXPECT_EQ(db.convert(Decimal::fromInt(9640), "₽", "USD", sch::Date{2026, 9, 2}), Decimal::fromInt(100));
    EXPECT_EQ(db.convert(Decimal::fromInt(1), "USD", "₽", sch::Date{2026, 10, 2}), Decimal::fromRaw(924000));
    EXPECT_THROW(db.convert(Decimal::fromInt(1), "USD", "₽", sch::Date{2026, 8, 31}), sch::ConversionError);
}

TEST(Accounts, ListsInTreeOrder) {
    const Journal j = parse(R"(
2026-09-01 X
    активы:банк-2      1 ₽
    активы:банк:сбер   1 ₽
    активы:банк       -2 ₽
)");
    std::ostringstream out;
    sch::accountsReport(j, {}, out);
    EXPECT_EQ(out.str(), "активы:банк\nактивы:банк:сбер\nактивы:банк-2\n");
}
