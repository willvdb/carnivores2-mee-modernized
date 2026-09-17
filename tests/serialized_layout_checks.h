#pragma once

#include <gtest/gtest.h>

#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include <type_traits>

// These checks describe bytes consumed by the existing readers, not pointer-
// owning runtime objects. No test fixture is copied from original game assets.
#define RECORD_SIZE(type, bytes) \
    static_assert(std::is_standard_layout_v<type> && std::is_trivially_copyable_v<type>); \
    static_assert(sizeof(type) == bytes, #type " serialized size changed")
#define RECORD_OFFSET(type, member, bytes) \
    static_assert(offsetof(type, member) == bytes, #type "::" #member " serialized offset changed")

static_assert(CHAR_BIT == 8 && sizeof(short) == 2 && sizeof(int) == 4);
static_assert(sizeof(float) == 4 && std::numeric_limits<float>::is_iec559);

namespace SerializedLayout {

// Common engine/menu contracts. Both definitions must independently match.
template<class Profile, class Stats, class Item, class Keys>
constexpr bool CheckProfile()
{
    RECORD_SIZE(Profile, 1516);
    RECORD_OFFSET(Profile, RegNumber, 128);
    RECORD_OFFSET(Profile, Score, 132);
    RECORD_OFFSET(Profile, Rank, 136);
    RECORD_OFFSET(Profile, Last, 140);
    RECORD_OFFSET(Profile, Total, 156);
    RECORD_OFFSET(Profile, Body, 172);
    RECORD_SIZE(Stats, 16);
    RECORD_OFFSET(Stats, smade, 0);
    RECORD_OFFSET(Stats, success, 4);
    RECORD_OFFSET(Stats, path, 8);
    RECORD_OFFSET(Stats, time, 12);
    RECORD_SIZE(Item, 56);
    RECORD_SIZE(Keys, 68);
    RECORD_OFFSET(Keys, fkForward, 0);
    RECORD_OFFSET(Keys, fkBackward, 4);
    RECORD_OFFSET(Keys, fkReload, 8);
    RECORD_OFFSET(Keys, fkResupply, 12);
    RECORD_OFFSET(Keys, fkHoldBreath, 16);
    RECORD_OFFSET(Keys, fkFiringMode, 20);
    RECORD_OFFSET(Keys, fkFire, 24);
    RECORD_OFFSET(Keys, fkShow, 28);
    RECORD_OFFSET(Keys, fkSLeft, 32);
    RECORD_OFFSET(Keys, fkSRight, 36);
    RECORD_OFFSET(Keys, fkStrafe, 40);
    RECORD_OFFSET(Keys, fkJump, 44);
    RECORD_OFFSET(Keys, fkRun, 48);
    RECORD_OFFSET(Keys, fkCrouch, 52);
    RECORD_OFFSET(Keys, fkCall, 56);
    RECORD_OFFSET(Keys, fkCCall, 60);
    RECORD_OFFSET(Keys, fkBinoc, 64);
    return true;
}

// Explicit little-endian fixtures, independent of compiler field layout.
template<std::size_t N>
void Put32(std::array<std::uint8_t, N>& bytes, std::size_t offset, std::uint32_t value)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[offset + i] = static_cast<std::uint8_t>(value >> (8 * i));
}

inline std::array<std::uint8_t, 1516> ProfileFixture()
{
    std::array<std::uint8_t, 1516> bytes{};
    bytes[0] = 'T';
    Put32(bytes, 128, 7);
    Put32(bytes, 132, 0x12345678);
    Put32(bytes, 136, 2);
    Put32(bytes, 140, 11);
    Put32(bytes, 156 + 8, 0x3fc00000); // Total.path = 1.5f
    Put32(bytes, 172, 10);             // first trophy type
    Put32(bytes, 172 + 32, 0x40000000); // first trophy scale = 2.0f
    Put32(bytes, 172 + 23 * 56 + 52, 0x76543210); // last reserved field
    return bytes;
}

// Test both the raw-read interpretation and the raw-write representation.
// Callers also inspect their differently named trophy/name fields.
template<class Profile>
Profile CheckProfileBytes()
{
    auto bytes = ProfileFixture();
    Profile profile{};
    std::memcpy(&profile, bytes.data(), bytes.size());
    EXPECT_EQ(profile.RegNumber, 7);
    EXPECT_EQ(profile.Score, 0x12345678);
    EXPECT_EQ(profile.Rank, 2);
    EXPECT_EQ(profile.Last.smade, 11);
    EXPECT_FLOAT_EQ(profile.Total.path, 1.5f);
    profile.Score = 0x23456701;
    Put32(bytes, 132, 0x23456701);
    EXPECT_EQ(std::memcmp(&profile, bytes.data(), bytes.size()), 0);
    return profile;
}

} // namespace SerializedLayout
