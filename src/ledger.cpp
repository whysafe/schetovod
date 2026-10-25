#include "ledger.hpp"

#include <algorithm>

#include "utf8.hpp"

namespace sch {

void checkAssertions(const Journal& journal) {
    std::map<std::string, MixedAmount> balances;
    for (const Transaction& t : journal.transactions) {
        for (const Posting& p : t.postings) balances[p.account].add(*p.amount);
        for (const Posting& p : t.postings) {
            if (!p.assertion) continue;
            const Decimal expected = p.assertion->qty;
            const Decimal actual = balances[p.account].get(p.assertion->commodity);
            if (actual == expected) continue;
            const std::string& c = p.assertion->commodity;
            throw JournalError(journal.path, p.line,
                               "сверка не сошлась на счёте «" + p.account + "»: ожидалось " +
                                   journal.styles.format(expected, c) + ", а по журналу " +
                                   journal.styles.format(actual, c) + " (разница " +
                                   journal.styles.format(actual - expected, c) + ")");
        }
    }
}

PriceDb::PriceDb(const Journal& journal) {
    for (const Price& p : journal.prices) rates_[{p.from, p.to.commodity}].push_back({p.date, p.to.qty});
    for (const Transaction& t : journal.transactions) {
        for (const Posting& p : t.postings) {
            if (!p.cost || p.amount->qty.isZero()) continue;
            const Decimal unit = p.cost->qty.div(p.amount->qty);
            rates_[{p.amount->commodity, p.cost->commodity}].push_back({t.date, unit});
        }
    }
    for (auto& [key, list] : rates_)
        std::stable_sort(list.begin(), list.end(), [](const Rate& a, const Rate& b) { return a.date < b.date; });
}

const PriceDb::Rate* PriceDb::latest(const Key& key, const Date& on) const {
    const auto it = rates_.find(key);
    if (it == rates_.end()) return nullptr;
    const auto& list = it->second;
    // The last rate with date <= on; for equal dates the one written later wins.
    const auto after = std::upper_bound(list.begin(), list.end(), on,
                                        [](const Date& d, const Rate& r) { return d < r.date; });
    if (after == list.begin()) return nullptr;
    return &*(after - 1);
}

Decimal PriceDb::convert(Decimal qty, const std::string& from, const std::string& to, const Date& on) const {
    if (from == to || qty.isZero()) return qty;
    const Rate* direct = latest({from, to}, on);
    const Rate* inverse = latest({to, from}, on);
    // Dividing by the inverse rate keeps precision: 9 640 ₽ / 96,40 = 100 USD exactly.
    if (direct && (!inverse || !(direct->date < inverse->date))) return qty.mul(direct->value);
    if (inverse) return qty.div(inverse->value);
    throw ConversionError("нет курса " + from + " → " + to + " на " + on.toString() +
                          " или раньше; добавьте строку «P " + on.toString() + " " + from + " <курс> " + to + "»");
}

Decimal PriceDb::convert(const MixedAmount& m, const std::string& to, const Date& on) const {
    Decimal total;
    for (const auto& [commodity, qty] : m.parts()) total += convert(qty, commodity, to, on);
    return total;
}

std::string topLevelAccount(const std::string& account) { return toLower(account.substr(0, account.find(':'))); }

bool isIncomeAccount(const std::string& account) {
    const std::string top = topLevelAccount(account);
    return top == "доходы" || top == "income" || top == "revenue" || top == "revenues";
}

bool isExpenseAccount(const std::string& account) {
    const std::string top = topLevelAccount(account);
    return top == "расходы" || top == "expenses" || top == "expense";
}

}  // namespace sch
