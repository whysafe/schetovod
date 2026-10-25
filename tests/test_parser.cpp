#include <gtest/gtest.h>

#include <string>

#include "parser.hpp"

using sch::Decimal;
using sch::Journal;
using sch::JournalError;

namespace {

Journal parse(const std::string& text) { return sch::parseJournal(text, "t.journal"); }

std::string errorOf(const std::string& text) {
    try {
        parse(text);
    } catch (const JournalError& e) {
        return e.what();
    }
    return "";
}

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

TEST(Parser, InfersMissingAmountAndReadsTags) {
    const Journal j = parse(R"(
2026-09-03 * Лендинг  ; клиент: Зерно, нпд: юр
    активы:банк    38 000 ₽
    доходы:сайты
)");
    ASSERT_EQ(j.transactions.size(), 1u);
    const auto& t = j.transactions[0];
    EXPECT_EQ(t.description, "Лендинг");
    EXPECT_EQ(t.status, '*');
    EXPECT_EQ(t.line, 2);
    ASSERT_EQ(t.postings.size(), 2u);
    EXPECT_EQ(t.postings[1].amount->qty, Decimal::fromInt(-38000));
    EXPECT_EQ(t.postings[1].amount->commodity, "₽");
    EXPECT_TRUE(t.postings[1].inferred);
    EXPECT_EQ(t.tag("нпд").value_or(""), "юр");
    EXPECT_EQ(t.tag("Клиент").value_or(""), "Зерно");
}

TEST(Parser, SplitsMissingAmountPerCurrency) {
    const Journal j = parse(R"(
2026-09-01 Обмен
    активы:наличные    100 USD
    активы:наличные    500 ₽
    капитал
)");
    const auto& p = j.transactions[0].postings;
    ASSERT_EQ(p.size(), 4u);
    EXPECT_EQ(p[2].account, "капитал");
    EXPECT_EQ(p[2].amount->qty, Decimal::fromInt(-100));
    EXPECT_EQ(p[2].amount->commodity, "USD");
    EXPECT_EQ(p[3].amount->qty, Decimal::fromInt(-500));
    EXPECT_EQ(p[3].amount->commodity, "₽");
}

TEST(Parser, ReportsUnbalancedTransactionWithLine) {
    const std::string e = errorOf(R"(
2026-09-05 Хостинг
    расходы:хостинг    1 890 ₽
    активы:банк       -1 980 ₽
)");
    EXPECT_TRUE(contains(e, "t.journal:2:")) << e;
    EXPECT_TRUE(contains(e, "-90 ₽")) << e;
}

TEST(Parser, UnitAndTotalCost) {
    const Journal j = parse(R"(
2026-10-02 Вывод
    активы:payoneer    -600 USD @ 92,40 ₽
    расходы:комиссии    1 108,80 ₽
    активы:банк

2026-10-03 Покупка
    активы:usd    50 USD @@ 4 655 ₽
    активы:банк   -4 655 ₽
)");
    const auto& p = j.transactions[0].postings;
    EXPECT_EQ(p[0].cost->qty, Decimal::fromInt(-55440));
    EXPECT_EQ(p[2].amount->qty.raw(), 543312000);
    EXPECT_EQ(j.transactions[1].postings[0].cost->qty, Decimal::fromInt(4655));
}

TEST(Parser, ToleratesSubKopeckRemainderOnly) {
    EXPECT_NO_THROW(parse(R"(
2026-10-04 Обмен
    активы:usd    3 USD @ 33,3333 ₽
    активы:банк   -100,00 ₽
)"));
    EXPECT_THROW(parse(R"(
2026-10-04 Обмен
    активы:usd    3 USD @ 33,3333 ₽
    активы:банк   -100,05 ₽
)"),
                 JournalError);
}

TEST(Parser, BalanceAssertions) {
    const std::string base = R"(
2026-09-01 Старт
    активы:банк    1 000 ₽
    капитал

2026-09-02 Кофе
    расходы:еда    250 ₽
    активы:банк   -250 ₽ = 750 ₽

2026-09-30 Сверка
)";
    EXPECT_NO_THROW(parse(base + "    активы:банк    = 750 ₽\n"));
    const std::string e = errorOf(base + "    активы:банк    = 700 ₽\n");
    EXPECT_TRUE(contains(e, "t.journal:11:")) << e;
    EXPECT_TRUE(contains(e, "разница 50 ₽")) << e;
}

TEST(Parser, AssertionsFollowDateOrderNotFileOrder) {
    const Journal j = parse(R"(
2026-09-10 Сверка
    активы:банк    = 1 000 ₽

2026-09-01 Старт
    активы:банк    1 000 ₽
    капитал
)");
    EXPECT_EQ(j.transactions[0].description, "Старт");
    EXPECT_EQ(j.transactions[1].description, "Сверка");
}

TEST(Parser, DateFormats) {
    const Journal j = parse("03.10.2026 Кофе\n    расходы:еда  200 ₽\n    активы:наличные\n");
    EXPECT_EQ(j.transactions[0].date, (sch::Date{2026, 10, 3}));
    EXPECT_TRUE(contains(errorOf("2025-02-29 X\n    a  1 ₽\n    b\n"), "неверная дата"));
    EXPECT_NO_THROW(parse("2024-02-29 X\n    a  1 ₽\n    b\n"));
}

TEST(Parser, HintsAboutSingleSpaceBeforeAmount) {
    const std::string e = errorOf("2026-09-01 Еда\n    расходы:еда 500 ₽\n    активы:банк\n");
    EXPECT_TRUE(contains(e, "t.journal:2:")) << e;
    EXPECT_TRUE(contains(e, "два пробела")) << e;
}

TEST(Parser, AcceptsBomAndCrlf) {
    const Journal j = parse("\xEF\xBB\xBF" "2026-09-01 X\r\n    a  1 ₽\r\n    b\r\n");
    ASSERT_EQ(j.transactions.size(), 1u);
    EXPECT_EQ(j.transactions[0].postings[0].account, "a");
}

TEST(Parser, StructuralErrors) {
    EXPECT_TRUE(contains(errorOf("    активы  1 ₽\n"), "вне операции"));
    EXPECT_TRUE(contains(errorOf("2026-09-01 X\n    a\n    b\n"), "только у одной"));
    EXPECT_TRUE(contains(errorOf("2026-09-01 X\n    a::b  1 ₽\n    c\n"), "пустая часть"));
    EXPECT_TRUE(contains(errorOf("привет\n"), "непонятная строка"));
}

TEST(Parser, PriceDirective) {
    const Journal j = parse("P 2026-09-01 USD 91,80 ₽  ; ЦБ\n");
    ASSERT_EQ(j.prices.size(), 1u);
    EXPECT_EQ(j.prices[0].from, "USD");
    EXPECT_EQ(j.prices[0].to.qty.raw(), 918000);
    EXPECT_EQ(j.prices[0].to.commodity, "₽");
    EXPECT_THROW(parse("P 2026-09-01 USD 91,80 USD\n"), JournalError);
}

TEST(Parser, ParseTags) {
    const auto tags = sch::parseTags(" клиент: ООО Ромашка, нпд: юр, просто текст");
    ASSERT_EQ(tags.size(), 2u);
    EXPECT_EQ(tags[0].key, "клиент");
    EXPECT_EQ(tags[0].value, "ООО Ромашка");
    EXPECT_EQ(tags[1].value, "юр");
}
