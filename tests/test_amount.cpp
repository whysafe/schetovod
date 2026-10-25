#include <gtest/gtest.h>

#include <stdexcept>

#include "amount.hpp"
#include "utf8.hpp"

using sch::Amount;
using sch::Decimal;
using sch::MixedAmount;
using sch::parseAmount;
using sch::Styles;

TEST(Amount, NumberThenSymbol) {
    const auto a = parseAmount("45 000 ₽");
    EXPECT_EQ(a.amount.qty, Decimal::fromInt(45000));
    EXPECT_EQ(a.amount.commodity, "₽");
    EXPECT_FALSE(a.style.symbolLeft);
    EXPECT_TRUE(a.style.spaced);
    EXPECT_EQ(a.style.precision, 0);
}

TEST(Amount, SymbolThenNumber) {
    const auto a = parseAmount("$12.50");
    EXPECT_EQ(a.amount.qty.raw(), 125000);
    EXPECT_EQ(a.amount.commodity, "$");
    EXPECT_TRUE(a.style.symbolLeft);
    EXPECT_FALSE(a.style.spaced);
    EXPECT_EQ(a.style.precision, 2);

    EXPECT_EQ(parseAmount("-$ 3").amount.qty, Decimal::fromInt(-3));
    EXPECT_TRUE(parseAmount("-$ 3").style.spaced);
    EXPECT_EQ(parseAmount("₽ -300").amount.qty, Decimal::fromInt(-300));
}

TEST(Amount, PlainNumberHasNoCommodity) {
    const auto a = parseAmount("1500");
    EXPECT_EQ(a.amount.commodity, "");
    EXPECT_EQ(a.amount.qty, Decimal::fromInt(1500));
}

TEST(Amount, RejectsBadInput) {
    EXPECT_THROW(parseAmount("12 US D"), std::invalid_argument);
    EXPECT_THROW(parseAmount("abc"), std::invalid_argument);
    EXPECT_THROW(parseAmount(""), std::invalid_argument);
    EXPECT_THROW(parseAmount("--5 ₽"), std::invalid_argument);
    EXPECT_THROW(parseAmount("$5 extra"), std::invalid_argument);
    EXPECT_THROW(parseAmount("-$-5"), std::invalid_argument);
}

TEST(Styles, FormatsLikeTheJournal) {
    Styles s;
    s.observe("₽", parseAmount("45 000 ₽").style, true);
    s.observe("₽", parseAmount("1 201,20 ₽").style, true);
    s.observe("$", parseAmount("$12.50").style, true);
    EXPECT_EQ(s.format(Decimal::fromInt(-1500), "₽"), "-1 500,00 ₽");
    EXPECT_EQ(s.format(Decimal::fromRaw(-125000), "$"), "-$12,50");
    EXPECT_EQ(s.format(Decimal::fromInt(7), "USD"), "7,00 USD");
}

TEST(Styles, PriceDoesNotRaisePrecision) {
    Styles s;
    s.observe("₽", parseAmount("96,4012 ₽").style, false);
    s.observe("₽", parseAmount("100 ₽").style, true);
    EXPECT_EQ(s.format(Decimal::fromInt(100), "₽"), "100 ₽");
}

TEST(MixedAmount, DropsZeroParts) {
    MixedAmount m;
    m.add(Amount{Decimal::fromInt(100), "₽"});
    m.add(Amount{Decimal::fromInt(-100), "₽"});
    EXPECT_TRUE(m.isZero());
    m.add(Amount{Decimal::fromInt(5), "USD"});
    EXPECT_EQ(m.parts().size(), 1u);
    EXPECT_EQ(m.negated().get("USD"), Decimal::fromInt(-5));
}

TEST(Utf8, CountsCodePointsAndTruncates) {
    EXPECT_EQ(sch::displayWidth("₽ ёж"), 4u);
    EXPECT_EQ(sch::truncateRight("активы:банк", 6), "актив…");
    EXPECT_EQ(sch::truncateLeft("активы:банк:сбер", 6), "…:сбер");
    EXPECT_EQ(sch::padLeft("₽", 3), "  ₽");
    EXPECT_EQ(sch::toLower("ЁЛКА Тинькофф ABC"), "ёлка тинькофф abc");
    EXPECT_TRUE(sch::containsIgnoreCase("Активы:Банк:Тинькофф", "тинь"));
}
