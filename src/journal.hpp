#pragma once

#include <cstddef>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include "amount.hpp"
#include "date.hpp"

namespace sch {

struct Tag {
    std::string key;  // lowercased
    std::string value;
};

struct Posting {
    std::string account;
    std::optional<Amount> amount;     // always set after parsing
    std::optional<Amount> cost;       // total cost from "@" / "@@", same sign as amount
    std::optional<Amount> assertion;  // "= 1 000 ₽": balance after this transaction
    std::vector<Tag> tags;
    int line = 0;
    bool inferred = false;  // amount was left out and calculated
};

struct Transaction {
    Date date;
    char status = ' ';
    std::string description;
    std::vector<Posting> postings;
    std::vector<Tag> tags;
    int line = 0;
    std::size_t order = 0;

    // Transaction tags first, then tags on any posting.
    std::optional<std::string> tag(std::string_view key) const;
};

// "P 2026-10-01 USD 96,40 ₽": one unit of `from` costs `to`.
struct Price {
    Date date;
    std::string from;
    Amount to;
    int line = 0;
};

struct Journal {
    std::string path;
    std::vector<Transaction> transactions;  // sorted by date, file order kept for equal dates
    std::vector<Price> prices;
    Styles styles;
};

class JournalError : public std::runtime_error {
public:
    JournalError(const std::string& file, int line, const std::string& message);
    int line() const { return line_; }

private:
    int line_;
};

}  // namespace sch
