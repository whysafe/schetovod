#pragma once

#include <cstddef>
#include <string>
#include <string_view>

namespace sch {

// Length in bytes of a space-like character at position i (regular space,
// no-break space, thin and narrow no-break spaces), or 0.
std::size_t spaceAt(std::string_view s, std::size_t i);

std::string_view trim(std::string_view s);

// Terminal columns, counted as code points: enough for Cyrillic, ₽ and «».
std::size_t displayWidth(std::string_view s);

std::string padRight(std::string_view s, std::size_t width);
std::string padLeft(std::string_view s, std::size_t width);

// Cut to `width` columns with a trailing (or leading) ellipsis.
std::string truncateRight(std::string_view s, std::size_t width);
std::string truncateLeft(std::string_view s, std::size_t width);

// Lowercase for ASCII and Russian letters, including Ё.
std::string toLower(std::string_view s);
bool containsIgnoreCase(std::string_view haystack, std::string_view needle);

}  // namespace sch
