#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace sch {

// Fixed-point number with four fractional digits stored in a 64-bit integer.
// Money never goes through floating point: 0,1 + 0,2 is exactly 0,3 here.
class Decimal {
public:
    static constexpr int kScale = 4;
    static constexpr std::int64_t kOne = 10000;

    constexpr Decimal() = default;
    static constexpr Decimal fromRaw(std::int64_t raw) {
        Decimal d;
        d.raw_ = raw;
        return d;
    }
    static Decimal fromInt(std::int64_t value);

    std::int64_t raw() const { return raw_; }
    bool isZero() const { return raw_ == 0; }
    bool isNegative() const { return raw_ < 0; }
    Decimal abs() const { return isNegative() ? -*this : *this; }

    Decimal operator-() const;
    Decimal& operator+=(Decimal other);
    Decimal& operator-=(Decimal other);
    friend Decimal operator+(Decimal a, Decimal b) { return a += b; }
    friend Decimal operator-(Decimal a, Decimal b) { return a -= b; }
    friend bool operator==(Decimal a, Decimal b) { return a.raw_ == b.raw_; }
    friend bool operator!=(Decimal a, Decimal b) { return a.raw_ != b.raw_; }
    friend bool operator<(Decimal a, Decimal b) { return a.raw_ < b.raw_; }
    friend bool operator>(Decimal a, Decimal b) { return a.raw_ > b.raw_; }
    friend bool operator<=(Decimal a, Decimal b) { return a.raw_ <= b.raw_; }
    friend bool operator>=(Decimal a, Decimal b) { return a.raw_ >= b.raw_; }

    // Both round half away from zero to four digits.
    Decimal mul(Decimal other) const;
    Decimal div(Decimal other) const;

    Decimal roundTo(int digits) const;

    // Russian notation: "1 234 567,89".
    std::string toString(int digits) const;

private:
    std::int64_t raw_ = 0;
};

struct ParsedNumber {
    Decimal value;
    int digits = 0;  // fractional digits as written
};

// Accepts "45 000,50", "45000.50", "1.234,56", "1,234.56", "-12".
// Throws std::invalid_argument with a message for the user.
ParsedNumber parseNumber(std::string_view text);

// a * b / d rounded half away from zero, using a 128-bit intermediate
// product so that 90 млрд × 96,4012 does not overflow.
std::int64_t mulDivRound(std::int64_t a, std::int64_t b, std::int64_t d);

}  // namespace sch
