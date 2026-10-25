#include "reports.hpp"

#include <algorithm>
#include <map>
#include <set>
#include <stdexcept>

#include "ledger.hpp"
#include "utf8.hpp"

namespace sch {
namespace {

// Orders accounts as a tree: "активы:банк:сбер" right after "активы:банк",
// even though '-' and ' ' sort before ':' in plain byte order.
struct AccountOrder {
    static unsigned char key(char c) { return c == ':' ? 1 : static_cast<unsigned char>(c); }
    bool operator()(const std::string& a, const std::string& b) const {
        return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(),
                                            [](char x, char y) { return key(x) < key(y); });
    }
};

using AccountMap = std::map<std::string, MixedAmount, AccountOrder>;

std::string clipAccount(const std::string& account, int depth) {
    if (depth <= 0) return account;
    std::size_t pos = 0;
    for (int level = 0; level < depth; ++level) {
        pos = account.find(':', pos);
        if (pos == std::string::npos) return account;
        if (level + 1 < depth) ++pos;
    }
    return account.substr(0, pos);
}

std::vector<std::string> prefixes(const std::string& account) {
    std::vector<std::string> out;
    std::size_t pos = 0;
    while ((pos = account.find(':', pos)) != std::string::npos) {
        out.push_back(account.substr(0, pos));
        ++pos;
    }
    out.push_back(account);
    return out;
}

std::size_t accountLevel(const std::string& account) {
    return static_cast<std::size_t>(std::count(account.begin(), account.end(), ':'));
}

std::string leafName(const std::string& account) {
    const std::size_t pos = account.rfind(':');
    return pos == std::string::npos ? account : account.substr(pos + 1);
}

Date lastDate(const Journal& j) { return j.transactions.empty() ? Date{} : j.transactions.back().date; }

enum class Align { Left, Right };

// Columns are separated by three spaces; widths come from the content.
void printTable(std::ostream& out, const std::vector<std::string>& header, const std::vector<Align>& align,
                const std::vector<std::vector<std::string>>& rows, const std::vector<std::vector<std::string>>& footer) {
    std::vector<std::size_t> width(header.size(), 0);
    const auto measure = [&](const std::vector<std::string>& row) {
        for (std::size_t i = 0; i < row.size() && i < width.size(); ++i)
            width[i] = std::max(width[i], displayWidth(row[i]));
    };
    measure(header);
    for (const auto& r : rows) measure(r);
    for (const auto& r : footer) measure(r);

    const auto printRow = [&](const std::vector<std::string>& row) {
        std::string line;
        for (std::size_t i = 0; i < width.size(); ++i) {
            const std::string cell = i < row.size() ? row[i] : "";
            if (i > 0) line += "   ";
            line += align[i] == Align::Left ? padRight(cell, width[i]) : padLeft(cell, width[i]);
        }
        while (!line.empty() && line.back() == ' ') line.pop_back();
        out << line << '\n';
    };
    std::size_t total = 0;
    for (const std::size_t w : width) total += w;
    total += 3 * (width.size() - 1);

    printRow(header);
    out << std::string(total, '-') << '\n';
    for (const auto& r : rows) printRow(r);
    if (footer.empty()) return;
    out << std::string(total, '-') << '\n';
    for (const auto& r : footer) printRow(r);
}

std::string mostUsedCommodity(const Journal& j, const ReportOptions& o) {
    std::map<std::string, int> uses;
    for (const Transaction& t : j.transactions) {
        if (!inPeriod(t.date, o)) continue;
        for (const Posting& p : t.postings)
            if ((isIncomeAccount(p.account) || isExpenseAccount(p.account)) && matchesAccount(p.account, o))
                ++uses[p.amount->commodity];
    }
    std::string best;
    int bestCount = 0;
    for (const auto& [c, n] : uses)
        if (n > bestCount) {
            best = c;
            bestCount = n;
        }
    return best;
}

std::string detectRub(const Journal& j) {
    std::set<std::string> used;
    for (const Transaction& t : j.transactions)
        for (const Posting& p : t.postings) used.insert(p.amount->commodity);
    // Income may be only in dollars while rubles appear just in the rates.
    for (const Price& p : j.prices) used.insert(p.to.commodity);
    for (const char* candidate : {"₽", "RUB", "руб", "руб.", "р."})
        if (used.count(candidate) > 0) return candidate;
    throw std::runtime_error("не нашёл в журнале рублей; укажите валюту так: --rub RUB");
}

}  // namespace

bool inPeriod(const Date& d, const ReportOptions& o) {
    if (o.from && d < *o.from) return false;
    if (o.to && *o.to < d) return false;
    return true;
}

bool matchesAccount(const std::string& account, const ReportOptions& o) {
    if (o.queries.empty()) return true;
    return std::any_of(o.queries.begin(), o.queries.end(),
                       [&](const std::string& q) { return containsIgnoreCase(account, q); });
}

void balanceReport(const Journal& j, const ReportOptions& o, std::ostream& out) {
    AccountMap own;
    for (const Transaction& t : j.transactions) {
        if (!inPeriod(t.date, o)) continue;
        for (const Posting& p : t.postings)
            if (matchesAccount(p.account, o)) own[clipAccount(p.account, o.depth)].add(*p.amount);
    }

    if (o.valueIn) {
        const PriceDb prices(j);
        const Date on = o.to ? *o.to : lastDate(j);
        for (auto& [account, m] : own) {
            MixedAmount valued;
            valued.add(prices.convert(m, *o.valueIn, on), *o.valueIn);
            m = valued;
        }
    }

    AccountMap total;
    MixedAmount grand;
    for (const auto& [account, m] : own) {
        grand.add(m);
        for (const std::string& prefix : prefixes(account)) total[prefix].add(m);
    }

    // A zero parent stays visible when one of its children is not zero.
    std::set<std::string, AccountOrder> shown;
    for (const auto& [account, m] : total)
        if (!m.isZero())
            for (const std::string& prefix : prefixes(account)) shown.insert(prefix);

    std::size_t width = 10;
    for (const std::string& account : shown)
        for (const std::string& line : j.styles.formatLines(total[account])) width = std::max(width, displayWidth(line));
    for (const std::string& line : j.styles.formatLines(grand)) width = std::max(width, displayWidth(line));

    for (const std::string& account : shown) {
        const std::vector<std::string> lines = j.styles.formatLines(total[account]);
        for (std::size_t i = 0; i + 1 < lines.size(); ++i) out << padLeft(lines[i], width) << '\n';
        out << padLeft(lines.back(), width) << "  " << std::string(2 * accountLevel(account), ' ')
            << leafName(account) << '\n';
    }
    out << std::string(width, '-') << '\n';
    for (const std::string& line : j.styles.formatLines(grand)) out << padLeft(line, width) << '\n';
}

void registerReport(const Journal& j, const ReportOptions& o, std::ostream& out) {
    constexpr std::size_t dateW = 10, descW = 24, accountW = 26, amountW = 14, totalW = 14;
    const std::string blankLeft(dateW + 1 + descW, ' ');
    const std::string blankAll(dateW + 1 + descW + 1 + accountW + 1 + amountW, ' ');
    MixedAmount running;
    for (const Transaction& t : j.transactions) {
        if (!inPeriod(t.date, o)) continue;
        bool first = true;
        for (const Posting& p : t.postings) {
            if (!matchesAccount(p.account, o)) continue;
            running.add(*p.amount);
            const std::vector<std::string> lines = j.styles.formatLines(running);
            const std::string left =
                first ? t.date.toString() + " " + padRight(truncateRight(t.description, descW), descW) : blankLeft;
            std::string row = left + " " + padRight(truncateLeft(p.account, accountW), accountW) + " " +
                              padLeft(j.styles.format(*p.amount), amountW) + " " + padLeft(lines[0], totalW);
            out << row << '\n';
            for (std::size_t i = 1; i < lines.size(); ++i) out << blankAll << " " << padLeft(lines[i], totalW) << '\n';
            first = false;
        }
    }
}

void accountsReport(const Journal& j, const ReportOptions& o, std::ostream& out) {
    std::set<std::string, AccountOrder> accounts;
    for (const Transaction& t : j.transactions)
        for (const Posting& p : t.postings)
            if (matchesAccount(p.account, o)) accounts.insert(p.account);
    for (const std::string& a : accounts) out << a << '\n';
}

void checkReport(const Journal& j, std::ostream& out) {
    std::set<std::string> accounts;
    std::set<std::string> commodities;
    std::size_t postings = 0;
    std::size_t assertions = 0;
    for (const Transaction& t : j.transactions) {
        for (const Posting& p : t.postings) {
            ++postings;
            accounts.insert(p.account);
            commodities.insert(p.amount->commodity);
            if (p.assertion) ++assertions;
        }
    }
    out << "ок: " << j.path << '\n'
        << "операций " << j.transactions.size() << ", проводок " << postings << ", счетов " << accounts.size()
        << ", валют " << commodities.size() << ", курсов " << j.prices.size() << ", сверок " << assertions << '\n';
    if (!j.transactions.empty())
        out << "период: " << j.transactions.front().date.toString() << " .. " << lastDate(j).toString() << '\n';
}

MonthlyResult computeMonthly(const Journal& j, const ReportOptions& o) {
    MonthlyResult result;
    result.commodity = o.valueIn ? *o.valueIn : mostUsedCommodity(j, o);
    const PriceDb prices(j);
    std::map<std::pair<int, int>, MonthRow> byMonth;
    for (const Transaction& t : j.transactions) {
        if (!inPeriod(t.date, o)) continue;
        for (const Posting& p : t.postings) {
            const bool income = isIncomeAccount(p.account);
            if ((!income && !isExpenseAccount(p.account)) || !matchesAccount(p.account, o)) continue;
            // Each posting is valued at the rate of its own day, not at today's.
            const Decimal v = prices.convert(p.amount->qty, p.amount->commodity, result.commodity, t.date);
            MonthRow& row = byMonth[{t.date.year, t.date.month}];
            row.year = t.date.year;
            row.month = t.date.month;
            if (income)
                row.income -= v;
            else
                row.expense += v;
        }
    }
    if (byMonth.empty()) return result;

    int y = byMonth.begin()->first.first;
    int m = byMonth.begin()->first.second;
    const auto last = byMonth.rbegin()->first;
    while (std::make_pair(y, m) <= last) {
        const auto it = byMonth.find({y, m});
        MonthRow row = it == byMonth.end() ? MonthRow{y, m, {}, {}} : it->second;
        result.income += row.income;
        result.expense += row.expense;
        result.rows.push_back(row);
        if (++m > 12) {
            m = 1;
            ++y;
        }
    }
    return result;
}

void monthlyReport(const Journal& j, const ReportOptions& o, std::ostream& out) {
    const MonthlyResult r = computeMonthly(j, o);
    if (r.rows.empty()) {
        out << "За этот период нет доходов и расходов.\n";
        return;
    }
    const auto fmt = [&](Decimal v) { return j.styles.format(v, r.commodity); };
    std::vector<std::vector<std::string>> rows;
    for (const MonthRow& row : r.rows)
        rows.push_back({monthName(row.year, row.month), fmt(row.income), fmt(row.expense), fmt(row.income - row.expense)});
    const Decimal months = Decimal::fromInt(static_cast<std::int64_t>(r.rows.size()));
    std::vector<std::vector<std::string>> footer = {
        {"итого", fmt(r.income), fmt(r.expense), fmt(r.income - r.expense)},
        {"в среднем", fmt(r.income.div(months)), fmt(r.expense.div(months)), fmt((r.income - r.expense).div(months))},
    };
    printTable(out, {"месяц", "доходы", "расходы", "остаток"}, {Align::Left, Align::Right, Align::Right, Align::Right},
               rows, footer);
}

NpdResult computeNpd(const Journal& j, const NpdOptions& o) {
    NpdResult r;
    r.year = o.year;
    r.rub = o.rub ? *o.rub : detectRub(j);

    struct Receipt {
        Date date;
        bool legal;
        Decimal income;
    };
    const PriceDb prices(j);
    std::vector<Receipt> receipts;
    for (const Transaction& t : j.transactions) {
        if (t.date.year > o.year) break;
        std::optional<std::string> kind = t.tag("нпд");
        if (!kind) kind = t.tag("npd");
        if (!kind) continue;
        const std::string k = toLower(*kind);
        bool legal = false;
        if (k == "юр" || k == "юл" || k == "юрлицо" || k == "ип")
            legal = true;
        else if (k != "физ" && k != "фл" && k != "физлицо")
            throw JournalError(j.path, t.line, "в теге «нпд» укажите «физ» или «юр», а не «" + *kind + "»");

        Decimal income;
        bool touchesIncome = false;
        for (const Posting& p : t.postings) {
            if (!isIncomeAccount(p.account)) continue;
            touchesIncome = true;
            try {
                income -= prices.convert(p.amount->qty, p.amount->commodity, r.rub, t.date);
            } catch (const ConversionError& e) {
                throw JournalError(j.path, t.line, e.what());
            }
        }
        if (!touchesIncome) throw JournalError(j.path, t.line, "у операции с тегом «нпд» нет проводки по счёту доходов");
        if (income.isNegative())
            throw JournalError(j.path, t.line,
                               "возвраты (отрицательный доход) не поддерживаются: аннулируйте чек и поправьте исходную операцию");
        if (!income.isZero()) receipts.push_back({t.date, legal, income});
    }

    const Decimal hundred = Decimal::fromInt(100);
    const Decimal limit = Decimal::fromInt(2400000);
    Decimal deductionLeft = o.initialDeduction;
    Decimal carry;
    NpdMonth current;
    bool open = false;

    const auto closeMonth = [&]() {
        current.carriedIn = carry;
        const Decimal due = carry + current.tax;
        current.carriedOut = due <= hundred;
        current.payable = current.carriedOut ? Decimal{} : due;
        carry = current.carriedOut ? due : Decimal{};
        if (current.year == o.year) r.months.push_back(current);
    };

    // Earlier years are processed too: the deduction is spent once for good,
    // and a small December tax moves into January.
    for (const Receipt& rc : receipts) {
        if (!open || rc.date.year != current.year || rc.date.month != current.month) {
            if (open) closeMonth();
            current = NpdMonth{};
            current.year = rc.date.year;
            current.month = rc.date.month;
            current.due = current.month == 12 ? Date{current.year + 1, 1, 28} : Date{current.year, current.month + 1, 28};
            open = true;
        }
        const Decimal rate = Decimal::fromRaw(rc.legal ? 600 : 400);
        const Decimal cut = Decimal::fromRaw(rc.legal ? 200 : 100);
        const Decimal base = rc.income.mul(rate).roundTo(2);
        Decimal reduction = rc.income.mul(cut).roundTo(2);
        if (deductionLeft < reduction) reduction = deductionLeft;
        deductionLeft -= reduction;
        const Decimal tax = base - reduction;

        (rc.legal ? current.legal : current.individuals) += rc.income;
        current.tax += tax;
        current.deduction += reduction;
        if (rc.date.year == o.year) {
            r.income += rc.income;
            r.tax += tax;
            r.deduction += reduction;
            if (!r.limitExceededOn && r.income > limit) r.limitExceededOn = rc.date;
        }
    }
    if (open) closeMonth();
    r.deductionLeft = deductionLeft;
    r.carryOver = carry;
    return r;
}

void npdReport(const NpdResult& r, const Styles& styles, std::ostream& out) {
    const auto fmt = [&](Decimal v) {
        CommodityStyle s = styles.styleOf(r.rub);
        Styles kopecks;
        s.precision = 2;
        kopecks.observe(r.rub, s, true);
        return kopecks.format(v, r.rub);
    };
    out << "НПД за " << r.year << " год\n\n";
    if (r.months.empty()) {
        out << "В этом году нет операций с тегом «нпд».\n";
    } else {
        std::vector<std::vector<std::string>> rows;
        for (const NpdMonth& m : r.months) {
            rows.push_back({monthName(m.year, m.month), fmt(m.individuals), fmt(m.legal), fmt(m.tax), fmt(m.deduction),
                            fmt(m.payable), m.carriedOut ? "до 100 ₽, переносится" : "до " + m.due.toString()});
        }
        printTable(out, {"месяц", "от физлиц", "от юрлиц", "налог", "вычет", "к оплате", "срок"},
                   {Align::Left, Align::Right, Align::Right, Align::Right, Align::Right, Align::Right, Align::Left}, rows,
                   {});
    }
    const std::vector<std::pair<std::string, Decimal>> summary = {
        {"доход за год", r.income},
        {"налог начислен", r.tax},
        {"вычет использован", r.deduction},
        {"остаток вычета", r.deductionLeft},
    };
    out << '\n';
    std::size_t width = 0;
    for (const auto& [label, value] : summary) width = std::max(width, displayWidth(fmt(value)));
    for (const auto& [label, value] : summary) out << padRight(label, 20) << padLeft(fmt(value), width) << '\n';
    if (!r.carryOver.isZero())
        out << padRight("переносится", 20) << padLeft(fmt(r.carryOver), width) << "  (прибавится к налогу следующего месяца)\n";
    if (r.limitExceededOn)
        out << "\nВнимание: доход за год превысил 2 400 000 ₽ " << r.limitExceededOn->toString()
            << ". С этой даты НПД применять нельзя.\n";
}

}  // namespace sch
