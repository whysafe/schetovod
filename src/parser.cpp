#include "parser.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <map>
#include <sstream>

#include "ledger.hpp"
#include "utf8.hpp"

namespace sch {
namespace {

bool isDigit(char c) { return c >= '0' && c <= '9'; }

const char* const kConversionAccount = "капитал:обмен";

// Account and amount are separated by a tab or at least two spaces,
// because account names themselves may contain single spaces.
std::size_t findAccountEnd(std::string_view s) {
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\t') return i;
        if (s[i] == ' ' && i + 1 < s.size() && (s[i + 1] == ' ' || s[i + 1] == '\t')) return i;
    }
    return s.size();
}

bool hasEmptySegment(std::string_view account) {
    std::size_t start = 0;
    while (true) {
        const std::size_t colon = account.find(':', start);
        const std::string_view segment = account.substr(start, colon == std::string_view::npos ? colon : colon - start);
        if (trim(segment).empty()) return true;
        if (colon == std::string_view::npos) return false;
        start = colon + 1;
    }
}

// "расходы:еда 500 ₽" with one space is a common typo: the whole line
// becomes an account name. Detect a trailing amount with a currency.
bool looksLikeGluedAmount(std::string_view account) {
    for (std::size_t i = 0; i < account.size(); ++i) {
        if (account[i] != ' ') continue;
        try {
            if (!parseAmount(account.substr(i + 1)).amount.commodity.empty()) return true;
        } catch (const std::exception&) {
        }
    }
    return false;
}

std::int64_t halfUnit(int digits) {
    if (digits >= Decimal::kScale) return 1;
    std::int64_t unit = 1;
    for (int i = digits; i < Decimal::kScale; ++i) unit *= 10;
    return unit / 2;
}

class Parser {
public:
    Parser(std::string_view text, std::string path) : text_(text), path_(std::move(path)) {}

    Journal run() {
        journal_.path = path_;
        std::string_view rest = text_;
        if (rest.substr(0, 3) == "\xEF\xBB\xBF") rest.remove_prefix(3);
        std::size_t pos = 0;
        while (pos <= rest.size()) {
            std::size_t nl = rest.find('\n', pos);
            if (nl == std::string_view::npos) nl = rest.size();
            std::string_view line = rest.substr(pos, nl - pos);
            if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
            ++lineNo_;
            handleLine(line);
            if (nl == rest.size()) break;
            pos = nl + 1;
        }
        finishTransaction();

        std::stable_sort(journal_.transactions.begin(), journal_.transactions.end(),
                         [](const Transaction& a, const Transaction& b) { return a.date < b.date; });
        std::stable_sort(journal_.prices.begin(), journal_.prices.end(),
                         [](const Price& a, const Price& b) { return a.date < b.date; });
        return std::move(journal_);
    }

private:
    [[noreturn]] void fail(const std::string& message) const { throw JournalError(path_, lineNo_, message); }

    void handleLine(std::string_view line) {
        if (trim(line).empty()) {
            finishTransaction();
            return;
        }
        const char first = line[0];
        if (first == ' ' || first == '\t') {
            if (!current_) fail("строка с отступом вне операции: выше нет строки с датой");
            const std::string_view body = trim(line);
            if (body[0] == ';' || body[0] == '#') {
                addComment(body.substr(1));
                return;
            }
            addPosting(body);
            return;
        }
        finishTransaction();
        if (first == ';' || first == '#') return;
        if (isDigit(first)) {
            startTransaction(line);
            return;
        }
        if (line.size() > 1 && first == 'P' && (line[1] == ' ' || line[1] == '\t')) {
            addPrice(line.substr(2));
            return;
        }
        fail("непонятная строка: ожидается дата операции, курс «P ...» или комментарий «;»");
    }

    void startTransaction(std::string_view line) {
        std::string_view header = line;
        std::string_view comment;
        if (const std::size_t sc = header.find(';'); sc != std::string_view::npos) {
            comment = header.substr(sc + 1);
            header = header.substr(0, sc);
        }
        header = trim(header);
        const std::size_t sp = header.find_first_of(" \t");
        const std::string_view dateText = header.substr(0, sp);
        const auto date = parseDate(dateText);
        if (!date)
            fail("неверная дата «" + std::string(dateText) +
                 "»: нужен формат ГГГГ-ММ-ДД или ДД.ММ.ГГГГ, и такой день должен существовать");

        Transaction t;
        t.date = *date;
        t.line = lineNo_;
        std::string_view rest = sp == std::string_view::npos ? std::string_view{} : trim(header.substr(sp));
        if (!rest.empty() && (rest[0] == '*' || rest[0] == '!')) {
            t.status = rest[0];
            rest = trim(rest.substr(1));
        }
        t.description = std::string(rest);
        t.tags = parseTags(comment);
        current_ = std::move(t);
        txnPrecision_.clear();
    }

    void addComment(std::string_view text) {
        std::vector<Tag> tags = parseTags(text);
        auto& target = current_->postings.empty() ? current_->tags : current_->postings.back().tags;
        target.insert(target.end(), tags.begin(), tags.end());
    }

    void addPosting(std::string_view body) {
        Posting p;
        p.line = lineNo_;
        if (const std::size_t sc = body.find(';'); sc != std::string_view::npos) {
            p.tags = parseTags(body.substr(sc + 1));
            body = trim(body.substr(0, sc));
        }
        const std::size_t split = findAccountEnd(body);
        p.account = std::string(trim(body.substr(0, split)));
        if (p.account.empty()) fail("у проводки нет счёта");
        if (hasEmptySegment(p.account)) fail("пустая часть в имени счёта «" + p.account + "»");
        const std::string_view amountText = split < body.size() ? trim(body.substr(split)) : std::string_view{};
        if (amountText.empty() && looksLikeGluedAmount(p.account))
            fail("между счётом и суммой нужны два пробела или табуляция: «" + p.account + "»");
        try {
            readAmounts(amountText, p);
        } catch (const std::invalid_argument& e) {
            fail(e.what());
        } catch (const std::overflow_error& e) {
            fail(e.what());
        }
        current_->postings.push_back(std::move(p));
    }

    void notePrecision(const std::string& commodity, int digits) {
        int& seen = txnPrecision_[commodity];
        seen = std::max(seen, digits);
    }

    void readAmounts(std::string_view text, Posting& p) {
        if (text.empty()) return;
        if (const std::size_t eq = text.find('='); eq != std::string_view::npos) {
            const std::string_view expected = trim(text.substr(eq + 1));
            if (expected.empty()) throw std::invalid_argument("после «=» нужна сумма, которая должна быть на счёте");
            const ParsedAmount a = parseAmount(expected);
            journal_.styles.observe(a.amount.commodity, a.style, true);
            p.assertion = a.amount;
            text = trim(text.substr(0, eq));
            if (text.empty()) {
                // An assertion-only posting changes nothing, it only checks.
                p.amount = Amount{Decimal{}, a.amount.commodity};
                return;
            }
        }

        std::string_view costText;
        bool totalCost = false;
        if (const std::size_t at = text.find('@'); at != std::string_view::npos) {
            totalCost = at + 1 < text.size() && text[at + 1] == '@';
            costText = trim(text.substr(at + (totalCost ? 2 : 1)));
            text = trim(text.substr(0, at));
            if (text.empty()) throw std::invalid_argument("цена «@» указана без количества");
            if (costText.empty()) throw std::invalid_argument("после «@» нужна цена");
        }

        const ParsedAmount a = parseAmount(text);
        journal_.styles.observe(a.amount.commodity, a.style, true);
        notePrecision(a.amount.commodity, a.style.precision);
        p.amount = a.amount;
        if (costText.empty()) return;

        const ParsedAmount c = parseAmount(costText);
        if (c.amount.commodity == a.amount.commodity)
            throw std::invalid_argument("цена должна быть в другой валюте, чем количество");
        if (c.amount.qty.isNegative()) throw std::invalid_argument("цена не может быть отрицательной");
        if (a.amount.qty.isZero()) throw std::invalid_argument("у нулевого количества не бывает цены");
        Decimal total = totalCost ? c.amount.qty : c.amount.qty.mul(a.amount.qty.abs());
        if (a.amount.qty.isNegative()) total = -total;
        journal_.styles.observe(c.amount.commodity, c.style, totalCost);
        if (totalCost) notePrecision(c.amount.commodity, c.style.precision);
        p.cost = Amount{total, c.amount.commodity};
    }

    void addPrice(std::string_view text) {
        std::string_view s = text;
        if (const std::size_t sc = s.find(';'); sc != std::string_view::npos) s = s.substr(0, sc);
        s = trim(s);
        const char* format = "курс записывается так: P 2026-10-01 USD 96,40 ₽";
        std::size_t sp = s.find_first_of(" \t");
        if (sp == std::string_view::npos) fail(format);
        const auto date = parseDate(s.substr(0, sp));
        if (!date) fail("неверная дата в курсе; " + std::string(format));
        s = trim(s.substr(sp));
        sp = s.find_first_of(" \t");
        if (sp == std::string_view::npos) fail(format);
        const std::string from(s.substr(0, sp));
        if (!isValidCommodity(from)) fail("непонятная валюта «" + from + "» в курсе");

        ParsedAmount to;
        try {
            to = parseAmount(trim(s.substr(sp)));
        } catch (const std::invalid_argument& e) {
            fail(e.what());
        }
        if (to.amount.commodity.empty() || to.amount.commodity == from)
            fail("в курсе нужна вторая валюта: " + std::string(format));
        if (to.amount.qty <= Decimal{}) fail("курс должен быть больше нуля");
        journal_.styles.observe(to.amount.commodity, to.style, false);
        journal_.prices.push_back({*date, from, to.amount, lineNo_});
    }

    void finishTransaction() {
        if (!current_) return;
        Transaction t = std::move(*current_);
        current_.reset();
        if (t.postings.empty()) throw JournalError(path_, t.line, "у операции нет ни одной проводки");
        balance(t);
        addConversionPostings(t);
        t.order = journal_.transactions.size();
        journal_.transactions.push_back(std::move(t));
    }

    void balance(Transaction& t) {
        MixedAmount sum;
        std::size_t elided = t.postings.size();
        for (std::size_t i = 0; i < t.postings.size(); ++i) {
            const Posting& p = t.postings[i];
            if (!p.amount) {
                if (elided != t.postings.size())
                    throw JournalError(path_, p.line, "сумму можно пропустить только у одной проводки в операции");
                elided = i;
                continue;
            }
            sum.add(p.cost ? *p.cost : *p.amount);
        }

        if (elided == t.postings.size()) {
            checkBalanced(t, sum);
            return;
        }

        // The missing amount takes whatever is left; with several currencies
        // left over the posting is split, one per currency.
        const MixedAmount rest = sum.negated();
        if (rest.isZero()) {
            const auto withAmount = std::find_if(t.postings.begin(), t.postings.end(),
                                                 [](const Posting& p) { return p.amount.has_value(); });
            const std::string commodity = withAmount == t.postings.end() ? "" : withAmount->amount->commodity;
            t.postings[elided].amount = Amount{Decimal{}, commodity};
            t.postings[elided].inferred = true;
            return;
        }
        Posting templ = t.postings[elided];
        templ.inferred = true;
        std::vector<Posting> filled;
        for (const auto& [commodity, qty] : rest.parts()) {
            Posting q = templ;
            q.amount = Amount{qty, commodity};
            filled.push_back(std::move(q));
        }
        t.postings.erase(t.postings.begin() + static_cast<std::ptrdiff_t>(elided));
        t.postings.insert(t.postings.begin() + static_cast<std::ptrdiff_t>(elided), filled.begin(), filled.end());
    }

    // "-600 USD @ 92,40 ₽" balances by cost, but the raw amounts do not:
    // dollars leave and rubles arrive. A trading account takes both sides,
    // so every report totals to zero and, valued at today's rate, shows the
    // exchange gain or loss.
    static void addConversionPostings(Transaction& t) {
        std::vector<Posting> conversions;
        for (const Posting& p : t.postings) {
            if (!p.cost) continue;
            Posting out;
            out.account = kConversionAccount;
            out.line = p.line;
            out.inferred = true;
            Posting in = out;
            out.amount = Amount{-p.amount->qty, p.amount->commodity};
            in.amount = *p.cost;
            conversions.push_back(std::move(out));
            conversions.push_back(std::move(in));
        }
        t.postings.insert(t.postings.end(), conversions.begin(), conversions.end());
    }

    // Unit prices like "@ 96,4012 ₽" leave sub-kopeck remainders; anything
    // below half of the smallest written digit counts as balanced.
    void checkBalanced(const Transaction& t, const MixedAmount& sum) const {
        std::string residual;
        for (const auto& [commodity, qty] : sum.parts()) {
            const auto seen = txnPrecision_.find(commodity);
            const int digits = seen == txnPrecision_.end() ? 2 : seen->second;
            const std::int64_t raw = qty.raw() < 0 ? -qty.raw() : qty.raw();
            if (raw < halfUnit(digits)) continue;
            if (!residual.empty()) residual += ", ";
            residual += journal_.styles.format(qty, commodity);
        }
        if (!residual.empty())
            throw JournalError(path_, t.line,
                               "операция не сходится, лишнее: " + residual +
                                   ". Поправьте суммы или оставьте одну проводку без суммы");
    }

    std::string_view text_;
    std::string path_;
    int lineNo_ = 0;
    Journal journal_;
    std::optional<Transaction> current_;
    std::map<std::string, int> txnPrecision_;
};

}  // namespace

JournalError::JournalError(const std::string& file, int line, const std::string& message)
    : std::runtime_error(line > 0 ? file + ":" + std::to_string(line) + ": " + message : file + ": " + message),
      line_(line) {}

std::optional<std::string> Transaction::tag(std::string_view key) const {
    const std::string wanted = toLower(key);
    for (const Tag& tag : tags)
        if (tag.key == wanted) return tag.value;
    for (const Posting& p : postings)
        for (const Tag& tag : p.tags)
            if (tag.key == wanted) return tag.value;
    return std::nullopt;
}

std::vector<Tag> parseTags(std::string_view comment) {
    std::vector<Tag> tags;
    std::size_t start = 0;
    while (start <= comment.size()) {
        std::size_t comma = comment.find(',', start);
        if (comma == std::string_view::npos) comma = comment.size();
        const std::string_view piece = trim(comment.substr(start, comma - start));
        const std::size_t colon = piece.find(':');
        if (colon != std::string_view::npos && colon > 0) {
            const std::string_view key = trim(piece.substr(0, colon));
            if (!key.empty() && key.find_first_of(" \t") == std::string_view::npos)
                tags.push_back({toLower(key), std::string(trim(piece.substr(colon + 1)))});
        }
        if (comma == comment.size()) break;
        start = comma + 1;
    }
    return tags;
}

Journal parseJournal(std::string_view text, const std::string& path) {
    Journal journal = Parser(text, path).run();
    checkAssertions(journal);
    return journal;
}

Journal loadJournal(const std::string& path) {
    std::ifstream in(std::filesystem::u8path(path), std::ios::binary);
    if (!in) throw JournalError(path, 0, "не удалось открыть файл");
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return parseJournal(buffer.str(), path);
}

}  // namespace sch
