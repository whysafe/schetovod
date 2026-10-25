#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "journal.hpp"

namespace sch {

// Parses, balances every transaction, sorts by date and checks balance
// assertions. Throws JournalError pointing at the offending line.
Journal parseJournal(std::string_view text, const std::string& path);
Journal loadJournal(const std::string& path);

// "клиент: Зерно, нпд: юр" -> {клиент=Зерно, нпд=юр}
std::vector<Tag> parseTags(std::string_view comment);

}  // namespace sch
