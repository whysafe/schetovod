#pragma once

#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "journal.hpp"

namespace sch {

// Walks transactions in date order and compares each "= amount" with the
// running balance of that account (sub-accounts not included).
void checkAssertions(const Journal& journal);

class ConversionError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Exchange rates from "P" lines plus rates implied by "@" prices.
class PriceDb {
public:
    explicit PriceDb(const Journal& journal);

    // Uses the latest rate on or before `on`, in either direction.
    Decimal convert(Decimal qty, const std::string& from, const std::string& to, const Date& on) const;
    Decimal convert(const MixedAmount& m, const std::string& to, const Date& on) const;

private:
    using Key = std::pair<std::string, std::string>;
    struct Rate {
        Date date;
        Decimal value;
    };
    const Rate* latest(const Key& key, const Date& on) const;

    std::map<Key, std::vector<Rate>> rates_;
};

std::string topLevelAccount(const std::string& account);
bool isIncomeAccount(const std::string& account);
bool isExpenseAccount(const std::string& account);

}  // namespace sch
