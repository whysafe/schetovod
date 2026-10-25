#include "amount.hpp"

#include <algorithm>
#include <stdexcept>

#include "utf8.hpp"

namespace sch {
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

std::size_t numberEnd(std::string_view t, std::size_t i) {
    while (i < t.size()) {
        if (isDigit(t[i]) || t[i] == '.' || t[i] == ',') {
            ++i;
            continue;
        }
        const std::size_t space = spaceAt(t, i);
        if (space > 0 && i > 0 && isDigit(t[i - 1]) && i + space < t.size() && isDigit(t[i + space])) {
            i += space;
            continue;
        }
        break;
    }
    return i;
}

std::size_t skipSpaces(std::string_view t, std::size_t i) {
    while (i < t.size() && spaceAt(t, i) > 0) i += spaceAt(t, i);
    return i;
}

}  // namespace

bool isValidCommodity(std::string_view symbol) {
    for (std::size_t i = 0; i < symbol.size(); ++i) {
        const char c = symbol[i];
        if (isDigit(c) || spaceAt(symbol, i) > 0) return false;
        if (std::string_view("-+@=;\"").find(c) != std::string_view::npos) return false;
    }
    return true;
}

ParsedAmount parseAmount(std::string_view text) {
    const std::string_view t = trim(text);
    if (t.empty()) throw std::invalid_argument("пустая сумма");

    std::size_t i = 0;
    bool negative = false;
    if (t[0] == '-' || t[0] == '+') {
        negative = t[0] == '-';
        i = skipSpaces(t, 1);
    }

    ParsedAmount out;
    if (i < t.size() && isDigit(t[i])) {
        const std::size_t end = numberEnd(t, i);
        const ParsedNumber n = parseNumber(t.substr(i, end - i));
        const std::string_view tail = t.substr(end);
        const std::string_view symbol = trim(tail);
        if (!isValidCommodity(symbol))
            throw std::invalid_argument("непонятная валюта «" + std::string(symbol) + "» в сумме «" + std::string(t) + "»");
        out.amount = {n.value, std::string(symbol)};
        out.style = {false, spaceAt(tail, 0) > 0, n.digits};
    } else {
        std::size_t j = i;
        while (j < t.size() && !isDigit(t[j]) && t[j] != '-' && t[j] != '+' && spaceAt(t, j) == 0) ++j;
        const std::string_view symbol = t.substr(i, j - i);
        if (symbol.empty() || !isValidCommodity(symbol))
            throw std::invalid_argument("не похоже на сумму: «" + std::string(t) + "»");
        std::size_t k = skipSpaces(t, j);
        const bool spaced = k > j;
        if (k < t.size() && (t[k] == '-' || t[k] == '+')) {
            if (i > 0) throw std::invalid_argument("знак указан дважды: «" + std::string(t) + "»");
            negative = t[k] == '-';
            ++k;
        }
        if (k >= t.size() || !isDigit(t[k]))
            throw std::invalid_argument("после «" + std::string(symbol) + "» ожидается число");
        const std::size_t end = numberEnd(t, k);
        if (end != t.size()) throw std::invalid_argument("лишний текст после суммы: «" + std::string(t) + "»");
        const ParsedNumber n = parseNumber(t.substr(k, end - k));
        out.amount = {n.value, std::string(symbol)};
        out.style = {true, spaced, n.digits};
    }
    if (negative) out.amount.qty = -out.amount.qty;
    return out;
}

void MixedAmount::add(Decimal qty, const std::string& commodity) {
    if (qty.isZero()) return;
    Decimal& v = parts_[commodity];
    v += qty;
    if (v.isZero()) parts_.erase(commodity);
}

void MixedAmount::add(const MixedAmount& other) {
    for (const auto& [c, q] : other.parts_) add(q, c);
}

MixedAmount MixedAmount::negated() const {
    MixedAmount out;
    for (const auto& [c, q] : parts_) out.parts_[c] = -q;
    return out;
}

Decimal MixedAmount::get(const std::string& commodity) const {
    const auto it = parts_.find(commodity);
    return it == parts_.end() ? Decimal{} : it->second;
}

void Styles::observe(const std::string& commodity, const CommodityStyle& seen, bool withPrecision) {
    const auto [it, inserted] = styles_.try_emplace(commodity, seen);
    if (inserted) {
        if (!withPrecision) it->second.precision = 0;
        return;
    }
    if (withPrecision) it->second.precision = std::max(it->second.precision, seen.precision);
}

CommodityStyle Styles::styleOf(const std::string& commodity) const {
    const auto it = styles_.find(commodity);
    return it == styles_.end() ? CommodityStyle{} : it->second;
}

std::string Styles::format(Decimal qty, const std::string& commodity) const {
    const CommodityStyle s = styleOf(commodity);
    const Decimal rounded = qty.roundTo(s.precision);
    if (commodity.empty()) return rounded.toString(s.precision);
    const std::string gap = s.spaced ? " " : "";
    if (!s.symbolLeft) return rounded.toString(s.precision) + gap + commodity;
    const std::string sign = rounded.isNegative() ? "-" : "";
    return sign + commodity + gap + rounded.abs().toString(s.precision);
}

std::vector<std::string> Styles::formatLines(const MixedAmount& m) const {
    if (m.isZero()) return {"0"};
    std::vector<std::string> lines;
    for (const auto& [c, q] : m.parts()) lines.push_back(format(q, c));
    return lines;
}

}  // namespace sch
