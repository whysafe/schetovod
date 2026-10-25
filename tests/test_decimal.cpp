#include <gtest/gtest.h>

#include <cstdint>
#include <limits>
#include <stdexcept>

#include "decimal.hpp"

using sch::Decimal;
using sch::mulDivRound;
using sch::parseNumber;

TEST(Decimal, ParsesRussianAndEnglishNotation) {
    EXPECT_EQ(parseNumber("45 000,50").value.raw(), 450005000);
    EXPECT_EQ(parseNumber("45000.50").value.raw(), 450005000);
    EXPECT_EQ(parseNumber("1.234,56").value.raw(), 12345600);
    EXPECT_EQ(parseNumber("1,234.56").value.raw(), 12345600);
    EXPECT_EQ(parseNumber("1,000,000").value.raw(), 10000000000LL);
    EXPECT_EQ(parseNumber("-12,5").value.raw(), -125000);
    EXPECT_EQ(parseNumber("7\xC2\xA0" "500").value.raw(), 75000000);  // no-break space from a bank app
    EXPECT_EQ(parseNumber("45 000,50").digits, 2);
    EXPECT_EQ(parseNumber("1,000,000").digits, 0);
}

TEST(Decimal, RejectsBrokenNumbers) {
    EXPECT_THROW(parseNumber("1,2.3,4"), std::invalid_argument);
    EXPECT_THROW(parseNumber("0,12345"), std::invalid_argument);
    EXPECT_THROW(parseNumber("12 "), std::invalid_argument);
    EXPECT_THROW(parseNumber("abc"), std::invalid_argument);
    EXPECT_THROW(parseNumber("1000000000000000"), std::invalid_argument);
}

TEST(Decimal, FormatsWithSpacesAndComma) {
    EXPECT_EQ(Decimal::fromRaw(12345678900LL).toString(2), "1 234 567,89");
    EXPECT_EQ(Decimal::fromInt(-500).toString(0), "-500");
    EXPECT_EQ(Decimal::fromRaw(5).toString(2), "0,00");
    EXPECT_EQ(Decimal::fromRaw(50).toString(2), "0,01");
    EXPECT_EQ(Decimal::fromRaw(-50).toString(2), "-0,01");
}

TEST(Decimal, RoundsHalfAwayFromZero) {
    EXPECT_EQ(mulDivRound(5, 1, 10), 1);
    EXPECT_EQ(mulDivRound(-5, 1, 10), -1);
    EXPECT_EQ(mulDivRound(4, 1, 10), 0);
    EXPECT_EQ(mulDivRound(15, 1, 10), 2);
    EXPECT_EQ(mulDivRound(-15, 1, 10), -2);
}

TEST(Decimal, MultipliesBeyondSixtyFourBitProduct) {
    // 9e14 * 964012 does not fit in 64 bits; the result does.
    const Decimal big = Decimal::fromInt(90000000000LL);
    const Decimal rate = Decimal::fromRaw(964012);
    EXPECT_EQ(big.mul(rate).raw(), 86761080000000000LL);
    EXPECT_EQ(Decimal::fromInt(9640).div(Decimal::fromRaw(964000)), Decimal::fromInt(100));
}

TEST(Decimal, DetectsOverflow) {
    const Decimal max = Decimal::fromRaw(std::numeric_limits<std::int64_t>::max());
    EXPECT_THROW(max.mul(Decimal::fromInt(2)), std::overflow_error);
    EXPECT_THROW(max + Decimal::fromRaw(1), std::overflow_error);
    EXPECT_THROW(Decimal::fromInt(1).div(Decimal{}), std::domain_error);
}
