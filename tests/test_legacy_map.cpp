#include <gtest/gtest.h>
#include "../Shared/LegacyMap.h"
#include "legacy_map_fixtures.h"
#include <limits>
#include <vector>

TEST(LegacyMap, LiteralOffsetsAndSizes)
{
    const std::array<std::size_t,12> offsets = {
        LegacyMap::HeightOffset,LegacyMap::Texture1Offset,LegacyMap::Texture2Offset,
        LegacyMap::ObjectOffset,LegacyMap::FlagsOffset,LegacyMap::LightOffset,
        LegacyMap::LightOffset+LegacyMap::BytePlaneSize,
        LegacyMap::LightOffset+2*LegacyMap::BytePlaneSize,LegacyMap::WaterOffset,
        LegacyMap::ObjectHeightOffset,LegacyMap::FogOffset,LegacyMap::AmbientOffset
    };
    EXPECT_EQ(offsets,MapGolden::Offsets);
    EXPECT_EQ(LegacyMap::FileSize,MapGolden::Size);
    EXPECT_EQ(LegacyMap::Width,1024); EXPECT_EQ(LegacyMap::RegionWidth,512);
}
TEST(LegacyMap, GoldenSingleAndAdjacentWords)
{
    std::array<std::uint16_t,7> out{};
    ASSERT_TRUE(LegacyMap::DecodeWords(MapGolden::Words.data(),14,out.data(),7,7));
    EXPECT_EQ(out,MapGolden::Values);
    for(unsigned i=0;i<7;++i) {
        std::uint16_t value=0x5555;
        ASSERT_TRUE(LegacyMap::DecodeWords(MapGolden::Words.data()+2*i,2,&value,1,1));
        EXPECT_EQ(value,MapGolden::Values[i]);
    }
}
TEST(LegacyMap, AllFlagBitsAndChunkEdges)
{
    std::vector<std::uint8_t> bytes(8194);
    std::vector<std::uint16_t> values(4097,0x5555);
    // Includes every bit, unknown flags, and numeric patterns at chunk edges.
    for(std::size_t i=0;i<values.size();++i) {
        const unsigned v=i<16 ? 1u<<i : MapGolden::Flags[i%MapGolden::Flags.size()];
        bytes[2*i]=v%256; bytes[2*i+1]=v/256;
    }
    for(std::size_t begin=0;begin<values.size();begin+=2048) {
        const auto count=(std::min)(std::size_t(2048),values.size()-begin);
        ASSERT_TRUE(LegacyMap::DecodeWords(bytes.data()+2*begin,2*count,values.data()+begin,count,count));
    }
    for(std::size_t i=0;i<values.size();++i)
        EXPECT_EQ(values[i],i<16 ? 1u<<i : MapGolden::Flags[i%MapGolden::Flags.size()]);
}
TEST(LegacyMap, InvalidBoundsNeverPartiallyWrite)
{
    std::array<std::uint16_t,7> out; out.fill(0xa55a); const auto before=out;
    for(std::size_t size=0;size<14;++size) {
        EXPECT_FALSE(LegacyMap::DecodeWords(MapGolden::Words.data(),size,out.data(),7,7));
        EXPECT_EQ(out,before);
    }
    EXPECT_FALSE(LegacyMap::DecodeWords(MapGolden::Words.data(),14,out.data(),6,7));
    EXPECT_FALSE(LegacyMap::DecodeWords(nullptr,14,out.data(),7,7));
    EXPECT_FALSE(LegacyMap::DecodeWords(MapGolden::Words.data(),14,nullptr,7,7));
    const auto big=(std::numeric_limits<std::size_t>::max)();
    for(auto count:{big,big/2+1})
        EXPECT_FALSE(LegacyMap::DecodeWords(MapGolden::Words.data(),big,out.data(),big,count));
    EXPECT_EQ(out,before);
    EXPECT_TRUE(LegacyMap::DecodeWords(nullptr,0,nullptr,0,0));
}
