#pragma once

#include "../Core/Strings.h"
#include <cstdint>
#include <limits>
#include <optional>
#include <string_view>

namespace GameDisplay {

// CLI only. Enumeration numbers are ephemeral, even between launches on the
// same machine. An invalid last occurrence clears any earlier request.
inline bool ParseDisplayIndex(std::string_view text, std::optional<std::uint32_t>& result)
{
    result.reset();
    if (text.empty()) return false;
    std::uint32_t value = 0;
    for (char c : text) {
        if (c < '0' || c > '9') return false;
        const auto digit = static_cast<std::uint32_t>(c - '0');
        if (value > ((std::numeric_limits<std::uint32_t>::max)() - digit) / 10)
            return false;
        value = value * 10 + digit;
    }
    result = value;
    return true;
}

enum class DisplayArgument { Unrelated, Applied, Invalid };

inline DisplayArgument ApplyDisplayArgument(const char* argument, std::optional<std::uint32_t>& result)
{
    if (LegacyText::Compare(argument, "-display=", 9) != 0 &&
        LegacyText::Compare(argument, "/display=", 9) != 0)
        return DisplayArgument::Unrelated;
    return ParseDisplayIndex(argument + 9, result) ? DisplayArgument::Applied : DisplayArgument::Invalid;
}

} // namespace GameDisplay
