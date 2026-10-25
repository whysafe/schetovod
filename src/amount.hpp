#pragma once

#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "decimal.hpp"

namespace sch {

struct Amount {
    Decimal qty;
    std::string commodity;  // "₽", "USD", "$" or empty
};

// How a commodity was written in the journal; reports print it the same way.
struct CommodityStyle {
    bool symbolLeft = false;  // $12 vs 12 USD
    bool spaced = true;       // 12 USD vs 12USD
    int precision = 2;
};

struct ParsedAmount {
    Amount amount;
    CommodityStyle style;
};

// "45 000 ₽", "-12,50 USD", "$100", "₽ -300". Throws std::invalid_argument.
ParsedAmount parseAmount(std::string_view text);
bool isValidCommodity(std::string_view symbol);

// A sum of several commodities; zero parts are dropped.
class MixedAmount {
public:
    void add(const Amount& a) { add(a.qty, a.commodity); }
    void add(Decimal qty, const std::string& commodity);
    void add(const MixedAmount& other);
    MixedAmount negated() const;
    bool isZero() const { return parts_.empty(); }
    Decimal get(const std::string& commodity) const;
    const std::map<std::string, Decimal>& parts() const { return parts_; }

private:
    std::map<std::string, Decimal> parts_;
};

class Styles {
public:
    // The first sighting fixes the symbol position; precision grows to the
    // largest one seen when `withPrecision` is set.
    void observe(const std::string& commodity, const CommodityStyle& seen, bool withPrecision);
    CommodityStyle styleOf(const std::string& commodity) const;
    std::string format(Decimal qty, const std::string& commodity) const;
    std::string format(const Amount& a) const { return format(a.qty, a.commodity); }
    std::vector<std::string> formatLines(const MixedAmount& m) const;
    std::size_t size() const { return styles_.size(); }

private:
    std::map<std::string, CommodityStyle> styles_;
};

}  // namespace sch
