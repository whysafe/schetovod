#include "utf8.hpp"

namespace sch {
namespace {

bool isContinuation(char c) { return (static_cast<unsigned char>(c) & 0xC0u) == 0x80u; }

// Byte offset where code point number `n` starts.
std::size_t offsetOfCodePoint(std::string_view s, std::size_t n) {
    std::size_t seen = 0;
    for (std::size_t i = 0; i < s.size(); ++i) {
        if (isContinuation(s[i])) continue;
        if (seen == n) return i;
        ++seen;
    }
    return s.size();
}

const std::string kEllipsis = "\xE2\x80\xA6";

}  // namespace

std::size_t spaceAt(std::string_view s, std::size_t i) {
    if (i >= s.size()) return 0;
    if (s[i] == ' ' || s[i] == '\t') return 1;
    const std::string_view rest = s.substr(i);
    if (rest.substr(0, 2) == "\xC2\xA0") return 2;
    if (rest.substr(0, 3) == "\xE2\x80\xAF" || rest.substr(0, 3) == "\xE2\x80\x89") return 3;
    return 0;
}

std::string_view trim(std::string_view s) {
    const auto isBlank = [](char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; };
    while (!s.empty() && isBlank(s.front())) s.remove_prefix(1);
    while (!s.empty() && isBlank(s.back())) s.remove_suffix(1);
    return s;
}

std::size_t displayWidth(std::string_view s) {
    std::size_t width = 0;
    for (const char c : s)
        if (!isContinuation(c)) ++width;
    return width;
}

std::string padRight(std::string_view s, std::size_t width) {
    std::string out(s);
    const std::size_t w = displayWidth(s);
    if (w < width) out.append(width - w, ' ');
    return out;
}

std::string padLeft(std::string_view s, std::size_t width) {
    const std::size_t w = displayWidth(s);
    std::string out = w < width ? std::string(width - w, ' ') : std::string();
    out += s;
    return out;
}

std::string truncateRight(std::string_view s, std::size_t width) {
    if (displayWidth(s) <= width) return std::string(s);
    if (width == 0) return {};
    return std::string(s.substr(0, offsetOfCodePoint(s, width - 1))) + kEllipsis;
}

std::string truncateLeft(std::string_view s, std::size_t width) {
    const std::size_t w = displayWidth(s);
    if (w <= width) return std::string(s);
    if (width == 0) return {};
    return kEllipsis + std::string(s.substr(offsetOfCodePoint(s, w - (width - 1))));
}

std::string toLower(std::string_view s) {
    std::string out;
    out.reserve(s.size());
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c >= 'A' && c <= 'Z') {
            out += static_cast<char>(c - 'A' + 'a');
            continue;
        }
        if (c == 0xD0 && i + 1 < s.size()) {
            const auto next = static_cast<unsigned char>(s[i + 1]);
            if (next >= 0x90 && next <= 0x9F) {  // А..П -> а..п
                out += static_cast<char>(0xD0);
                out += static_cast<char>(next + 0x20);
                ++i;
                continue;
            }
            if (next >= 0xA0 && next <= 0xAF) {  // Р..Я -> р..я
                out += static_cast<char>(0xD1);
                out += static_cast<char>(next - 0x20);
                ++i;
                continue;
            }
            if (next == 0x81) {  // Ё -> ё
                out += static_cast<char>(0xD1);
                out += static_cast<char>(0x91);
                ++i;
                continue;
            }
        }
        out += static_cast<char>(c);
    }
    return out;
}

bool containsIgnoreCase(std::string_view haystack, std::string_view needle) {
    return toLower(haystack).find(toLower(needle)) != std::string::npos;
}

}  // namespace sch
