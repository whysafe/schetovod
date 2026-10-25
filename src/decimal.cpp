#include "decimal.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>

#include "utf8.hpp"

namespace sch {
namespace {

constexpr std::int64_t kPow10[] = {1, 10, 100, 1000, 10000};
constexpr std::int64_t kMax = std::numeric_limits<std::int64_t>::max();
constexpr std::int64_t kMin = std::numeric_limits<std::int64_t>::min();

struct U128 {
    std::uint64_t hi;
    std::uint64_t lo;
};

U128 mul64(std::uint64_t a, std::uint64_t b) {
    const std::uint64_t mask = 0xffffffffu;
    const std::uint64_t aLo = a & mask, aHi = a >> 32;
    const std::uint64_t bLo = b & mask, bHi = b >> 32;
    const std::uint64_t ll = aLo * bLo;
    const std::uint64_t lh = aLo * bHi;
    const std::uint64_t hl = aHi * bLo;
    const std::uint64_t hh = aHi * bHi;
    const std::uint64_t mid = (ll >> 32) + (lh & mask) + (hl & mask);
    return {hh + (lh >> 32) + (hl >> 32) + (mid >> 32), (ll & mask) | (mid << 32)};
}

// Binary long division of a 128-bit value; the quotient must fit in 64 bits.
std::uint64_t divRound(U128 n, std::uint64_t d) {
    if (n.hi >= d) throw std::overflow_error("переполнение: сумма слишком большая");
    std::uint64_t rem = n.hi;
    std::uint64_t q = 0;
    for (int bit = 63; bit >= 0; --bit) {
        const bool carry = (rem >> 63) != 0;
        rem = (rem << 1) | ((n.lo >> bit) & 1u);
        q <<= 1;
        if (carry || rem >= d) {
            rem -= d;
            q |= 1u;
        }
    }
    if (rem >= d - rem) {
        if (q == std::numeric_limits<std::uint64_t>::max())
            throw std::overflow_error("переполнение: сумма слишком большая");
        ++q;
    }
    return q;
}

std::uint64_t magnitude(std::int64_t v) {
    return v < 0 ? std::uint64_t{0} - static_cast<std::uint64_t>(v) : static_cast<std::uint64_t>(v);
}

bool isDigit(char c) { return c >= '0' && c <= '9'; }

}  // namespace

std::int64_t mulDivRound(std::int64_t a, std::int64_t b, std::int64_t d) {
    if (d == 0) throw std::domain_error("деление на ноль");
    const bool negative = ((a < 0) != (b < 0)) != (d < 0);
    const std::uint64_t q = divRound(mul64(magnitude(a), magnitude(b)), magnitude(d));
    const std::uint64_t limit = negative ? magnitude(kMin) : static_cast<std::uint64_t>(kMax);
    if (q > limit) throw std::overflow_error("переполнение: сумма слишком большая");
    return negative ? static_cast<std::int64_t>(std::uint64_t{0} - q) : static_cast<std::int64_t>(q);
}

Decimal Decimal::fromInt(std::int64_t value) {
    if (value > kMax / kOne || value < kMin / kOne)
        throw std::overflow_error("переполнение: сумма слишком большая");
    return fromRaw(value * kOne);
}

Decimal Decimal::operator-() const {
    if (raw_ == kMin) throw std::overflow_error("переполнение: сумма слишком большая");
    return fromRaw(-raw_);
}

Decimal& Decimal::operator+=(Decimal other) {
    if ((other.raw_ > 0 && raw_ > kMax - other.raw_) || (other.raw_ < 0 && raw_ < kMin - other.raw_))
        throw std::overflow_error("переполнение: сумма слишком большая");
    raw_ += other.raw_;
    return *this;
}

Decimal& Decimal::operator-=(Decimal other) { return *this += -other; }

Decimal Decimal::mul(Decimal other) const { return fromRaw(mulDivRound(raw_, other.raw_, kOne)); }

Decimal Decimal::div(Decimal other) const { return fromRaw(mulDivRound(raw_, kOne, other.raw_)); }

Decimal Decimal::roundTo(int digits) const {
    if (digits >= kScale) return *this;
    if (digits < 0) digits = 0;
    const std::int64_t factor = kPow10[kScale - digits];
    std::int64_t q = raw_ / factor;
    const std::int64_t r = raw_ % factor;
    if (magnitude(r) * 2 >= static_cast<std::uint64_t>(factor)) q += raw_ < 0 ? -1 : 1;
    return fromRaw(q * factor);
}

std::string Decimal::toString(int digits) const {
    digits = std::clamp(digits, 0, kScale);
    const std::int64_t rounded = roundTo(digits).raw_;
    const std::uint64_t u = magnitude(rounded);
    const std::uint64_t one = static_cast<std::uint64_t>(kOne);
    const std::string intDigits = std::to_string(u / one);

    std::string out;
    if (rounded < 0) out += '-';
    const std::size_t n = intDigits.size();
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0) out += ' ';
        out += intDigits[i];
    }
    if (digits > 0) {
        const std::string frac = std::to_string((u % one) / static_cast<std::uint64_t>(kPow10[kScale - digits]));
        out += ',';
        out.append(static_cast<std::size_t>(digits) - frac.size(), '0');
        out += frac;
    }
    return out;
}

ParsedNumber parseNumber(std::string_view text) {
    const std::string original(text);
    std::size_t i = 0;
    bool negative = false;
    if (i < text.size() && (text[i] == '-' || text[i] == '+')) {
        negative = text[i] == '-';
        ++i;
    }

    // Spaces may only group digits; dots and commas are sorted out below.
    std::string cleaned;
    while (i < text.size()) {
        const char c = text[i];
        if (isDigit(c) || c == '.' || c == ',') {
            cleaned += c;
            ++i;
            continue;
        }
        const std::size_t space = spaceAt(text, i);
        if (space > 0 && !cleaned.empty() && isDigit(cleaned.back()) && i + space < text.size() &&
            isDigit(text[i + space])) {
            i += space;
            continue;
        }
        throw std::invalid_argument("не похоже на число: «" + original + "»");
    }

    // The last separator is the decimal one if both kinds are present;
    // a single kind repeated ("1,000,000") is only grouping.
    const auto count = [&](char ch) { return std::count(cleaned.begin(), cleaned.end(), ch); };
    const std::size_t lastDot = cleaned.rfind('.');
    const std::size_t lastComma = cleaned.rfind(',');
    char decimalSep = 0;
    if (lastDot != std::string::npos && lastComma != std::string::npos)
        decimalSep = lastDot > lastComma ? '.' : ',';
    else if (lastDot != std::string::npos && count('.') == 1)
        decimalSep = '.';
    else if (lastComma != std::string::npos && count(',') == 1)
        decimalSep = ',';
    if (decimalSep != 0 && count(decimalSep) > 1)
        throw std::invalid_argument("в числе два десятичных разделителя: «" + original + "»");

    constexpr std::uint64_t maxInt = static_cast<std::uint64_t>(kMax / Decimal::kOne);
    std::uint64_t intPart = 0;
    std::uint64_t frac = 0;
    int fracDigits = 0;
    bool afterPoint = false;
    bool anyDigit = false;
    for (const char c : cleaned) {
        if (c == decimalSep) {
            afterPoint = true;
            continue;
        }
        if (!isDigit(c)) continue;
        anyDigit = true;
        const auto digit = static_cast<std::uint64_t>(c - '0');
        if (afterPoint) {
            if (++fracDigits > Decimal::kScale)
                throw std::invalid_argument("больше четырёх знаков после запятой: «" + original + "»");
            frac = frac * 10 + digit;
        } else {
            intPart = intPart * 10 + digit;
            if (intPart > maxInt) throw std::invalid_argument("слишком большое число: «" + original + "»");
        }
    }
    if (!anyDigit) throw std::invalid_argument("в числе нет цифр: «" + original + "»");

    const auto raw = static_cast<std::int64_t>(intPart * static_cast<std::uint64_t>(Decimal::kOne) +
                                               frac * static_cast<std::uint64_t>(kPow10[Decimal::kScale - fracDigits]));
    return {Decimal::fromRaw(negative ? -raw : raw), fracDigits};
}

}  // namespace sch
