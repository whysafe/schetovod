#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

#include "ledger.hpp"
#include "parser.hpp"
#include "reports.hpp"
#include "utf8.hpp"

#ifdef _WIN32
#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <shellapi.h>
#endif

namespace {

constexpr const char* kVersion = "1.0.0";

constexpr const char* kHelp = R"(schetovod: учёт денег в текстовом файле и налог самозанятого

  schetovod [-f ЖУРНАЛ] КОМАНДА [параметры] [счёт ...]

Команды:
  check              проверить журнал: суммы сходятся, сверки остатков верны
  bal                остатки по счетам деревом
  reg                список проводок с нарастающим итогом
  month              доходы и расходы по месяцам
  npd                налог на профессиональный доход по месяцам
  accounts           список счетов

Параметры:
  -f, --file ФАЙЛ    журнал (по умолчанию из переменной SCHETOVOD_FILE)
  -p, --period П     2026 или 2026-10
      --from ДАТА    с этой даты включительно
      --to ДАТА      по эту дату включительно
  -d, --depth N      bal: глубина дерева
  -X, --value ВАЛ    bal, month: пересчитать в валюту по курсам из журнала
      --year ГГГГ    npd: год (по умолчанию год последней операции)
      --vychet СУММА npd: остаток вычета на начало журнала (по умолчанию 10 000)
      --rub ВАЛ      npd: как в журнале записаны рубли (по умолчанию ищет ₽, RUB, руб)
  -h, --help         эта справка
      --version      версия

Счета после команды фильтруют проводки по части имени без учёта регистра:
  schetovod -f деньги.journal reg тинькофф
)";

struct UsageError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

struct Args {
    std::string file;
    std::string command;
    sch::ReportOptions report;
    std::optional<int> year;
    std::optional<sch::Decimal> deduction;
    std::optional<std::string> rub;
    bool help = false;
    bool version = false;
};

sch::Date requireDate(const std::string& text) {
    const auto d = sch::parseDate(text);
    if (!d) throw UsageError("неверная дата «" + text + "»");
    return *d;
}

int requireInt(const std::string& text, const std::string& what) {
    try {
        std::size_t used = 0;
        const int value = std::stoi(text, &used);
        if (used == text.size()) return value;
    } catch (const std::exception&) {
    }
    throw UsageError(what + ": нужно целое число, а не «" + text + "»");
}

void applyPeriod(const std::string& text, sch::ReportOptions& o) {
    if (text.size() == 4) {
        const int y = requireInt(text, "--period");
        o.from = sch::Date{y, 1, 1};
        o.to = sch::Date{y, 12, 31};
        return;
    }
    const auto first = sch::parseDate(text + "-01");
    if (text.size() != 7 || !first) throw UsageError("--period: нужен год (2026) или месяц (2026-10)");
    o.from = *first;
    o.to = sch::Date{first->year, first->month, sch::daysInMonth(first->year, first->month)};
}

Args parseArgs(const std::vector<std::string>& argv) {
    Args a;
    for (std::size_t i = 0; i < argv.size(); ++i) {
        std::string arg = argv[i];
        std::optional<std::string> inlineValue;
        if (arg.rfind("--", 0) == 0) {
            if (const std::size_t eq = arg.find('='); eq != std::string::npos) {
                inlineValue = arg.substr(eq + 1);
                arg = arg.substr(0, eq);
            }
        }
        const auto value = [&]() -> std::string {
            if (inlineValue) return *inlineValue;
            if (i + 1 >= argv.size()) throw UsageError("после " + arg + " нужно значение");
            return argv[++i];
        };

        if (arg == "-h" || arg == "--help") {
            a.help = true;
        } else if (arg == "--version") {
            a.version = true;
        } else if (arg == "-f" || arg == "--file") {
            a.file = value();
        } else if (arg == "-p" || arg == "--period") {
            applyPeriod(value(), a.report);
        } else if (arg == "--from") {
            a.report.from = requireDate(value());
        } else if (arg == "--to") {
            a.report.to = requireDate(value());
        } else if (arg == "-d" || arg == "--depth") {
            a.report.depth = requireInt(value(), "--depth");
        } else if (arg == "-X" || arg == "--value") {
            a.report.valueIn = value();
        } else if (arg == "--year") {
            a.year = requireInt(value(), "--year");
        } else if (arg == "--vychet") {
            try {
                a.deduction = sch::parseNumber(value()).value;
            } catch (const std::invalid_argument& e) {
                throw UsageError(std::string("--vychet: ") + e.what());
            }
        } else if (arg == "--rub") {
            a.rub = value();
        } else if (arg.size() > 1 && arg[0] == '-') {
            throw UsageError("неизвестный параметр " + arg);
        } else if (a.command.empty()) {
            a.command = arg;
        } else {
            a.report.queries.push_back(arg);
        }
    }
    return a;
}

int run(const std::vector<std::string>& argv) {
    Args a = parseArgs(argv);
    if (a.help) {
        std::cout << kHelp;
        return 0;
    }
    if (a.version) {
        std::cout << "schetovod " << kVersion << '\n';
        return 0;
    }
    if (a.command.empty()) {
        std::cout << kHelp;
        return 2;
    }
    if (a.file.empty()) {
        if (const char* env = std::getenv("SCHETOVOD_FILE")) a.file = env;
    }
    if (a.file.empty()) throw UsageError("не указан журнал: -f ФАЙЛ или переменная SCHETOVOD_FILE");

    const sch::Journal journal = sch::loadJournal(a.file);
    const std::string& c = a.command;
    if (c == "check") {
        sch::checkReport(journal, std::cout);
    } else if (c == "bal" || c == "balance") {
        sch::balanceReport(journal, a.report, std::cout);
    } else if (c == "reg" || c == "register") {
        sch::registerReport(journal, a.report, std::cout);
    } else if (c == "month" || c == "monthly") {
        sch::monthlyReport(journal, a.report, std::cout);
    } else if (c == "accounts") {
        sch::accountsReport(journal, a.report, std::cout);
    } else if (c == "npd") {
        sch::NpdOptions o;
        if (a.year) {
            o.year = *a.year;
        } else if (!journal.transactions.empty()) {
            o.year = journal.transactions.back().date.year;
        }
        if (a.deduction) o.initialDeduction = *a.deduction;
        o.rub = a.rub;
        sch::npdReport(sch::computeNpd(journal, o), journal.styles, std::cout);
    } else {
        throw UsageError("неизвестная команда «" + c + "», список команд: schetovod --help");
    }
    return 0;
}

std::vector<std::string> utf8Args(int argc, char** argv) {
#ifdef _WIN32
    // argv on Windows is in the ANSI code page; take the wide command line
    // so that Cyrillic account names survive.
    (void)argv;
    int count = 0;
    LPWSTR* wide = CommandLineToArgvW(GetCommandLineW(), &count);
    std::vector<std::string> out;
    for (int i = 1; i < count; ++i) {
        const int size = WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, nullptr, 0, nullptr, nullptr);
        std::string s(static_cast<std::size_t>(size > 0 ? size - 1 : 0), '\0');
        if (size > 1) WideCharToMultiByte(CP_UTF8, 0, wide[i], -1, s.data(), size, nullptr, nullptr);
        out.push_back(s);
    }
    LocalFree(wide);
    (void)argc;
    return out;
#else
    return std::vector<std::string>(argv + 1, argv + argc);
#endif
}

}  // namespace

int main(int argc, char** argv) {
#ifdef _WIN32
    SetConsoleOutputCP(CP_UTF8);
#endif
    try {
        return run(utf8Args(argc, argv));
    } catch (const UsageError& e) {
        std::cerr << "schetovod: " << e.what() << '\n';
        return 2;
    } catch (const std::exception& e) {
        std::cerr << "schetovod: " << e.what() << '\n';
        return 1;
    }
}
