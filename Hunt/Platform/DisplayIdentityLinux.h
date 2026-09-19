#pragma once
#include "Platform.h"
#include <algorithm>
#include <iterator>
#include <string_view>

// Native discovery facts and validation only. No game selection/config policy.
namespace Platform::LinuxIdentity {
inline std::string Hex(std::string_view bytes)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string value;
    for (unsigned char byte : bytes) {
        value += digits[byte >> 4];
        value += digits[byte & 15];
    }
    return value;
}
inline bool ValidText(std::string_view text)
{
    if (text.empty() || text.size() > 128 || text.front() == ' ' || text.back() == ' ') return false;
    // Deliberately bounded printable ASCII; unsupported metadata is not truncated.
    for (unsigned char c : text) if (c < 32 || c > 126) return false;
    std::string folded(text);
    for (char& c : folded) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    for (const auto* invalid : {"unknown", "none", "null", "n/a", "na", "default", "serial", "serial number", "unspecified"})
        if (folded == invalid) return false;
    return true;
}
inline bool ValidSerial(std::string_view text)
{
    if (!ValidText(text)) return false;
    // Common numeric placeholders, including zero padded zero/one and all-ones hex.
    const auto significant = text.find_first_not_of('0');
    if (significant == std::string_view::npos || text.substr(significant) == "1") return false;
    if (text == "123456789" || text == "1234567890" || text == "0123456789") return false;
    if (std::all_of(text.begin(), text.end(), [](char c) { return c == 'f' || c == 'F'; })) return false;
    return true;
}
inline void AppendField(std::string& bytes, std::string_view field)
{
    bytes += static_cast<char>(field.size() >> 8);
    bytes += static_cast<char>(field.size() & 255);
    bytes.append(field);
}
inline std::optional<DisplayIdentity> WaylandSerial(std::string_view make, std::string_view model, std::string_view serial)
{
    if (!ValidText(make) || !ValidText(model) || !ValidSerial(serial)) return std::nullopt;
    std::string bytes;
    AppendField(bytes, make); AppendField(bytes, model); AppendField(bytes, serial);
    return DisplayIdentity{1, "linux-wayland-wlr-serial", Hex(bytes)};
}
inline std::optional<DisplayIdentity> X11Edid(const std::vector<std::uint8_t>& data)
{
    constexpr std::uint8_t header[] = {0, 255, 255, 255, 255, 255, 255, 0};
    if (data.size() < 128 || data.size() != 128u * (std::size_t{data[126]} + 1) ||
        !std::equal(std::begin(header), std::end(header), data.begin()) ||
        data[18] != 1 || data[19] < 3 || data[19] > 4) return std::nullopt;
    for (std::size_t block = 0; block < data.size(); block += 128) {
        unsigned sum = 0;
        for (std::size_t i = 0; i < 128; ++i) sum += data[block + i];
        if (sum % 256) return std::nullopt;
    }
    const unsigned manufacturer = (unsigned{data[8]} << 8) | data[9];
    if (manufacturer & 0x8000) return std::nullopt;
    for (unsigned shift : {0u, 5u, 10u}) {
        const auto c = (manufacturer >> shift) & 31;
        if (c < 1 || c > 26) return std::nullopt;
    }
    const unsigned product = data[10] | (unsigned{data[11]} << 8);
    if (!product || product == 65535) return std::nullopt;
    const std::uint32_t serial = data[12] | (std::uint32_t{data[13]} << 8) |
        (std::uint32_t{data[14]} << 16) | (std::uint32_t{data[15]} << 24);
    std::string text;
    bool hasText = false;
    for (std::size_t i = 54; i < 126; i += 18) {
        if (data[i] || data[i + 1] || data[i + 3] != 0xff) continue;
        if (hasText || data[i + 2] || data[i + 4]) return std::nullopt;
        hasText = true;
        bool ended = false;
        for (std::size_t j = i + 5; j < i + 18; ++j) {
            const auto c = data[j];
            if (ended) { if (c != ' ') return std::nullopt; }
            else if (c == 10) ended = true;
            else { if (c < 32 || c > 126) return std::nullopt; text += static_cast<char>(c); }
        }
        while (!text.empty() && text.back() == ' ') text.pop_back();
        if (!ValidSerial(text)) return std::nullopt;
    }
    const bool numeric = serial > 1 && serial != 0xffffffffu && serial != 0x01010101u;
    if (!numeric && !hasText) return std::nullopt;
    // Preserve both genuine serial forms when present; no timing/connector data.
    std::string bytes(reinterpret_cast<const char*>(data.data() + 8), 4);
    bytes += numeric ? '\1' : '\0';
    if (numeric) bytes.append(reinterpret_cast<const char*>(data.data() + 12), 4);
    AppendField(bytes, text);
    return DisplayIdentity{1, "linux-x11-edid-serial", Hex(bytes)};
}
struct Output {
    std::uint64_t key = 0; // Native same-session join key, never persisted.
    std::string name; // Wayland protocol join only, never an identity.
    std::optional<DisplayIdentity> identity;
    bool active = false;
};
// Include all connected heads (even disabled) as duplicate witnesses. All
// associations must be unique, including records with unavailable identities.
inline void Associate(DisplayCatalog& catalog, const std::vector<std::uint64_t>& keys,
                      const std::vector<Output>& outputs)
{
    for (std::size_t i = 0; i < catalog.displays.size(); ++i) {
        auto& display = catalog.displays[i];
        display.identity.reset();
        if (i >= keys.size() || !keys[i] || std::count(keys.begin(), keys.end(), keys[i]) != 1) continue;
        const Output* match = nullptr;
        unsigned matches = 0;
        for (const auto& output : outputs) if (output.key == keys[i]) { match = &output; ++matches; }
        if (matches != 1 || !match->active || !match->identity) continue;
        unsigned identities = 0;
        for (const auto& output : outputs)
            if (output.identity && EqualDisplayIdentity(*output.identity, *match->identity)) ++identities;
        if (identities != 1) { display.identityStatus = "duplicate native monitor serial metadata"; continue; }
        display.identity = match->identity;
        display.identityStatus.clear();
    }
}
// The output-management protocol requires the enabled head's name to equal
// xdg_output.name. This is a same-connection protocol join, not name guessing.
inline void JoinWaylandOutputs(std::vector<Output>& heads, const std::vector<std::pair<std::uint64_t, std::string>>& outputs)
{
    for (auto& head : heads) {
        head.key = 0;
        if (!head.active || head.name.empty()) continue;
        unsigned matches = 0, names = 0;
        for (const auto& other : heads) if (other.name == head.name) ++names;
        for (const auto& output : outputs) if (output.second == head.name) { head.key = output.first; ++matches; }
        if (names != 1 || matches != 1) head.key = 0;
    }
}
} // namespace Platform::LinuxIdentity
