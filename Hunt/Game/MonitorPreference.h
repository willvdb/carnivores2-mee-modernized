#pragma once
#include "DisplayPreference.h"
#include "../Platform/Platform.h"

namespace GameDisplay {
enum class MonitorPreferenceKind { Primary, SessionIndex, Identity };
struct MonitorPreference {
    MonitorPreferenceKind kind = MonitorPreferenceKind::Primary;
    std::uint32_t index = 0;
    Platform::DisplayIdentity identity;
};
inline std::string IdentityToken(const Platform::DisplayIdentity& identity)
{
    return "v" + std::to_string(identity.version) + ":" + identity.domain + ":" + identity.value;
}
// Syntax is owned/versioned; unknown versions/domains remain valid preferences
// but cannot resolve here. No numeric runtime index is accepted in config.
inline bool ParseMonitorIdentity(std::string_view text, MonitorPreference& result)
{
    result = {};
    if (text == "primary") return true;
    if (text.empty() || text[0] != 'v') return false;
    const auto first = text.find(':');
    if (first == std::string_view::npos) return false;
    std::optional<std::uint32_t> version;
    if (!ParseDisplayIndex(text.substr(1, first - 1), version) || !*version) return false;
    const auto second = text.find(':', first + 1);
    if (second == std::string_view::npos) return false;
    const auto domain = text.substr(first + 1, second - first - 1);
    const auto value = text.substr(second + 1);
    if (domain.empty() || domain.size() > 48 || value.empty() || value.size() > 2048 || value.size() % 2) return false;
    for (char c : domain)
        if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-')) return false;
    for (char c : value)
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'))) return false;
    result.kind = MonitorPreferenceKind::Identity;
    result.identity = {*version, std::string(domain), std::string(value)};
    return true;
}
inline bool ParseConfigMonitor(std::string_view value, MonitorPreference& result)
{
    value = value.substr(0, value.find('#'));
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return ParseMonitorIdentity({}, result);
    value.remove_prefix(first);
    value = value.substr(0, value.find_last_not_of(" \t\r\n") + 1);
    return ParseMonitorIdentity(value, result);
}
inline DisplayArgument ApplyMonitorArgument(const char* argument, MonitorPreference& result)
{
    if (LegacyText::Compare(argument, "-display-id=", 12) == 0 ||
        LegacyText::Compare(argument, "/display-id=", 12) == 0)
        return ParseMonitorIdentity(argument + 12, result) ? DisplayArgument::Applied : DisplayArgument::Invalid;
    if (LegacyText::Compare(argument, "-display=", 9) != 0 &&
        LegacyText::Compare(argument, "/display=", 9) != 0) return DisplayArgument::Unrelated;
    result = {};
    if (LegacyText::Compare(argument + 9, "primary") == 0) return DisplayArgument::Applied;
    std::optional<std::uint32_t> index;
    if (!ParseDisplayIndex(argument + 9, index)) return DisplayArgument::Invalid;
    result.kind = MonitorPreferenceKind::SessionIndex;
    result.index = *index;
    return DisplayArgument::Applied;
}
inline bool WantsDisplayList(const char* argument)
{
    return LegacyText::Compare(argument, "-list-displays") == 0 || LegacyText::Compare(argument, "/list-displays") == 0;
}
} // namespace GameDisplay
