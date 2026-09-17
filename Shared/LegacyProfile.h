#pragma once
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>

// C2 .sav/.sab only. These value objects are never dumped to disk.
namespace LegacyProfile {
inline constexpr std::size_t NameSize = 128, HeaderSize = 140;
inline constexpr std::size_t ItemSize = 56, PrefixSize = 1516;
inline constexpr std::size_t KeyOffset = 1556, KeySize = 68, KeyEnd = 1624;
inline constexpr std::size_t SuffixSize = 144, SaveSize = 1660, RoomSize = 7176;
using PrefixBytes = std::array<std::uint8_t, PrefixSize>;
using SaveBytes = std::array<std::uint8_t, SaveSize>;
using RoomBytes = std::array<std::uint8_t, RoomSize>;
struct Header {
    std::array<std::uint8_t, NameSize> name{};
    std::int32_t registration{}, score{}, rank{};
};
struct Stats {
    std::int32_t shots{}, hits{};
    std::uint32_t path{}, time{}; // float bits
};
struct Item {
    std::int32_t type{}, weapon{}, phase{}, height{}, weight{}, score{}, date{}, time{};
    std::uint32_t scale{}, range{}; // float bits
    std::array<std::int32_t, 4> reserved{};
};
struct Prefix {
    Header header{};
    Stats last{}, total{};
    std::array<Item, 24> items{};
};
struct Options {
    std::int32_t aggression{};
    std::int32_t density{};
    std::int32_t sensitivity{};
    std::int32_t resolution{};
    std::int32_t fog{};
    std::int32_t textures{};
    std::int32_t viewRange{};
    std::int32_t shadows{};
    std::int32_t mouseSensitivity{};
    std::int32_t brightness{};
    std::array<std::int32_t, 17> keys{};
    std::int32_t mouseInvert{};
    std::int32_t scent{};
    std::int32_t camo{};
    std::int32_t radar{};
    std::int32_t tranq{};
    std::int32_t alphaColorKey{};
    std::int32_t system{};
    std::int32_t sound{};
    std::int32_t renderer{};
};
struct Save { Prefix profile{}; Options options{}; };
struct Room {
    std::int32_t version{}, highScore{};
    std::array<Item, 128> items{};
};
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559,
              "Legacy profiles require IEEE-754 binary32 runtime floats");
inline std::uint32_t FloatBits(const float& f)
{
    std::uint32_t bits;
    std::memcpy(&bits, &f, 4);
    return bits;
}
inline void SetFloat(float& f, std::uint32_t bits) { std::memcpy(&f, &bits, 4); }

namespace detail {
inline std::uint32_t U32(const std::uint8_t* p)
{
    return std::uint32_t(p[0]) | (std::uint32_t(p[1]) << 8) |
           (std::uint32_t(p[2]) << 16) | (std::uint32_t(p[3]) << 24);
}
inline std::int32_t I32(const std::uint8_t* p)
{
    const auto v = U32(p);
    // Avoid an implementation-defined unsigned-to-signed conversion.
    return v <= INT32_MAX ? static_cast<std::int32_t>(v)
                         : -1 - static_cast<std::int32_t>(UINT32_MAX - v);
}
inline void Put(std::uint8_t* p, std::uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = static_cast<std::uint8_t>(v >> (i * 8));
}
inline Header ReadHeader(const std::uint8_t* p)
{
    Header h;
    std::memcpy(h.name.data(), p, NameSize);
    h.registration = I32(p + 128); h.score = I32(p + 132); h.rank = I32(p + 136);
    return h;
}
inline void WriteHeader(std::uint8_t* p, const Header& h)
{
    std::memcpy(p, h.name.data(), NameSize);
    Put(p + 128, h.registration); Put(p + 132, h.score); Put(p + 136, h.rank);
}
inline Stats ReadStats(const std::uint8_t* p)
{
    return {I32(p), I32(p + 4), U32(p + 8), U32(p + 12)};
}
inline void WriteStats(std::uint8_t* p, const Stats& s)
{
    Put(p, s.shots); Put(p + 4, s.hits); Put(p + 8, s.path); Put(p + 12, s.time);
}
inline Item ReadItem(const std::uint8_t* p)
{
    Item v;
    v.type = I32(p + 0);
    v.weapon = I32(p + 4);
    v.phase = I32(p + 8);
    v.height = I32(p + 12);
    v.weight = I32(p + 16);
    v.score = I32(p + 20);
    v.date = I32(p + 24);
    v.time = I32(p + 28);
    v.scale = U32(p + 32);
    v.range = U32(p + 36);
    for (unsigned i = 0; i < 4; ++i) v.reserved[i] = I32(p + 40 + i * 4);
    return v;
}
inline void WriteItem(std::uint8_t* p, const Item& v)
{
    Put(p + 0, v.type);
    Put(p + 4, v.weapon);
    Put(p + 8, v.phase);
    Put(p + 12, v.height);
    Put(p + 16, v.weight);
    Put(p + 20, v.score);
    Put(p + 24, v.date);
    Put(p + 28, v.time);
    Put(p + 32, v.scale);
    Put(p + 36, v.range);
    for (unsigned i = 0; i < 4; ++i) Put(p + 40 + i * 4, v.reserved[i]);
}
inline void WritePrefix(std::uint8_t* p, const Prefix& v)
{
    WriteHeader(p, v.header);
    WriteStats(p + 140, v.last); WriteStats(p + 156, v.total);
    for (unsigned i = 0; i < 24; ++i) WriteItem(p + 172 + i * ItemSize, v.items[i]);
}
} // namespace detail

// Bounded decoders leave the destination untouched on failure. Trailing bytes
// are allowed for prefix/record consumers; menu file-size policy stays in I/O.
inline bool DecodeHeader(const std::uint8_t* p, std::size_t n, Header& v)
{
    if (!p || n < HeaderSize) return false;
    v = detail::ReadHeader(p);
    return true;
}
inline bool DecodePrefix(const std::uint8_t* p, std::size_t n, Prefix& v)
{
    if (!p || n < PrefixSize) return false;
    v.header = detail::ReadHeader(p);
    v.last = detail::ReadStats(p + 140); v.total = detail::ReadStats(p + 156);
    for (unsigned i = 0; i < 24; ++i) v.items[i] = detail::ReadItem(p + 172 + i * ItemSize);
    return true;
}
inline PrefixBytes EncodePrefix(const Prefix& v)
{
    PrefixBytes b{}; detail::WritePrefix(b.data(), v); return b;
}
inline bool DecodeKeys(const std::uint8_t* p, std::size_t n, std::array<std::int32_t, 17>& keys)
{
    if (!p || n < KeySize) return false;
    for (unsigned i = 0; i < 17; ++i) keys[i] = detail::I32(p + i * 4);
    return true;
}
// Engine compatibility: apply only complete option words; keys are atomic.
// Caller supplies defaults. This is separate from strict complete-save decoding.
inline void DecodeAvailableOptions(const std::uint8_t* p, std::size_t n, Options& v)
{
    if (!p) return;
    if (n >= 4) v.aggression = detail::I32(p + 0);
    if (n >= 8) v.density = detail::I32(p + 4);
    if (n >= 12) v.sensitivity = detail::I32(p + 8);
    if (n >= 16) v.resolution = detail::I32(p + 12);
    if (n >= 20) v.fog = detail::I32(p + 16);
    if (n >= 24) v.textures = detail::I32(p + 20);
    if (n >= 28) v.viewRange = detail::I32(p + 24);
    if (n >= 32) v.shadows = detail::I32(p + 28);
    if (n >= 36) v.mouseSensitivity = detail::I32(p + 32);
    if (n >= 40) v.brightness = detail::I32(p + 36);
    if (n < 108) return;
    DecodeKeys(p + 40, n - 40, v.keys);
    if (n >= 112) v.mouseInvert = detail::I32(p + 108);
    if (n >= 116) v.scent = detail::I32(p + 112);
    if (n >= 120) v.camo = detail::I32(p + 116);
    if (n >= 124) v.radar = detail::I32(p + 120);
    if (n >= 128) v.tranq = detail::I32(p + 124);
    if (n >= 132) v.alphaColorKey = detail::I32(p + 128);
    if (n >= 136) v.system = detail::I32(p + 132);
    if (n >= 140) v.sound = detail::I32(p + 136);
    if (n >= 144) v.renderer = detail::I32(p + 140);
}
inline bool DecodeOptions(const std::uint8_t* p, std::size_t n, Options& v)
{
    if (!p || n < SuffixSize) return false;
    DecodeAvailableOptions(p, n, v); return true;
}
inline bool DecodeSave(const std::uint8_t* p, std::size_t n, Save& v)
{
    if (!p || n < SaveSize) return false;
    DecodePrefix(p, n, v.profile);
    DecodeOptions(p + PrefixSize, n - PrefixSize, v.options);
    return true;
}
inline SaveBytes EncodeSave(const Save& v)
{
    SaveBytes b{};
    detail::WritePrefix(b.data(), v.profile);
    auto* p = b.data() + PrefixSize;
    detail::Put(p + 0, v.options.aggression);
    detail::Put(p + 4, v.options.density);
    detail::Put(p + 8, v.options.sensitivity);
    detail::Put(p + 12, v.options.resolution);
    detail::Put(p + 16, v.options.fog);
    detail::Put(p + 20, v.options.textures);
    detail::Put(p + 24, v.options.viewRange);
    detail::Put(p + 28, v.options.shadows);
    detail::Put(p + 32, v.options.mouseSensitivity);
    detail::Put(p + 36, v.options.brightness);
    for (unsigned i = 0; i < 17; ++i) detail::Put(p + 40 + i * 4, v.options.keys[i]);
    detail::Put(p + 108, v.options.mouseInvert);
    detail::Put(p + 112, v.options.scent);
    detail::Put(p + 116, v.options.camo);
    detail::Put(p + 120, v.options.radar);
    detail::Put(p + 124, v.options.tranq);
    detail::Put(p + 128, v.options.alphaColorKey);
    detail::Put(p + 132, v.options.system);
    detail::Put(p + 136, v.options.sound);
    detail::Put(p + 140, v.options.renderer);
    return b;
}
inline bool DecodeRoom(const std::uint8_t* p, std::size_t n, Room& v)
{
    if (!p || n < RoomSize) return false;
    v.version = detail::I32(p); v.highScore = detail::I32(p + 4);
    for (unsigned i = 0; i < 128; ++i) v.items[i] = detail::ReadItem(p + 8 + i * ItemSize);
    return true;
}
inline RoomBytes EncodeRoom(const Room& v)
{
    RoomBytes b{};
    detail::Put(b.data(), v.version); detail::Put(b.data() + 4, v.highScore);
    for (unsigned i = 0; i < 128; ++i) detail::WriteItem(b.data() + 8 + i * ItemSize, v.items[i]);
    return b;
}
} // namespace LegacyProfile
