#pragma once

#include "../Platform/Platform.h"
#include "../Core/Strings.h"
#include <limits>
#include <string_view>

namespace GameDisplay {

// Persistent syntax is decimal digits only: 0, N, or N/D. Invalid input clears
// an earlier preference so the effective behavior is always automatic.
inline bool ParseRefreshRate(std::string_view text, Platform::RefreshRate& result)
{
    result = {};
    if (text == "0") return true;
    auto positive = [](std::string_view part, std::uint32_t& value) {
        value = 0;
        if (part.empty()) return false;
        for (char c : part) {
            if (c < '0' || c > '9') return false;
            const auto digit = static_cast<std::uint32_t>(c - '0');
            if (value > ((std::numeric_limits<std::uint32_t>::max)() - digit) / 10)
                return false;
            value = value * 10 + digit;
        }
        return value != 0;
    };
    const auto slash = text.find('/');
    std::uint32_t numerator = 0, denominator = 1;
    if (!positive(text.substr(0, slash), numerator) ||
        (slash != std::string_view::npos && !positive(text.substr(slash + 1), denominator)))
        return false;
    result = {numerator, denominator};
    return true;
}

// Config passes the entire value remainder, avoiding fixed-size token truncation.
// Whitespace and # comments belong to the config line, not the numeric grammar.
inline bool ParseConfigRefresh(std::string_view value, Platform::RefreshRate& result)
{
    value = value.substr(0, value.find('#'));
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return ParseRefreshRate({}, result);
    value.remove_prefix(first);
    value = value.substr(0, value.find_last_not_of(" \t\r\n") + 1);
    return ParseRefreshRate(value, result);
}

enum class RefreshArgument { Unrelated, Applied, Invalid };

inline RefreshArgument ApplyRefreshArgument(const char* argument, Platform::RefreshRate& result)
{
    if (LegacyText::Compare(argument, "-refresh=", 9) != 0 &&
        LegacyText::Compare(argument, "/refresh=", 9) != 0)
        return RefreshArgument::Unrelated;
    return ParseRefreshRate(argument + 9, result) ? RefreshArgument::Applied : RefreshArgument::Invalid;
}

inline constexpr const char* RefreshSyntax =
    "expected 0 (automatic), positive integer Hz, or positive uint32 N/D; using automatic refresh.\n";

} // namespace GameDisplay
