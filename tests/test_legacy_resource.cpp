#include <gtest/gtest.h>
#include "../Shared/LegacyResource.h"
#include "legacy_resource_fixtures.h"
#include <cstring>
#include <limits>

namespace LR=LegacyResource;
// Compare initialized object representations only to check no writes on failure.
// These bytes are NOT disk fixtures or a codec implementation.
template<class Value, class Bytes, class Decoder>
void ShortChecks(const Bytes& b, Decoder decode)
{
    Value value{}; ASSERT_TRUE(decode(b.data(),b.size(),value));
    std::array<unsigned char,sizeof(Value)> before{}; std::memcpy(before.data(),&value,sizeof(value));
    for(std::size_t n=0;n<b.size();++n) {
        EXPECT_FALSE(decode(b.data(),n,value)); EXPECT_EQ(std::memcmp(before.data(),&value,sizeof(value)),0);
    }
    EXPECT_FALSE(decode(nullptr,b.size(),value)); EXPECT_EQ(std::memcmp(before.data(),&value,sizeof(value)),0);
}
TEST(LegacyResource, ScalarsAndColorTables)
{
    for(const auto word : {0u,1u,1024u,256u,255u,16u,1025u,257u,17u,0x80000000u,0x7fffffffu,0xffffffffu,0x12345678u}) {
        std::array<std::uint8_t,4> b{}; ResourceGolden::Put32(b,0,word);
        std::int32_t v=0; ASSERT_TRUE(LR::DecodeInt32(b.data(),4,v));
        EXPECT_EQ(static_cast<std::uint32_t>(v),word); ShortChecks<std::int32_t>(b,LR::DecodeInt32);
    }
    const auto b=ResourceGolden::Colors();
    for(unsigned table=0;table<2;++table) {
        LR::ColorTable c; ASSERT_TRUE(LR::DecodeColors(b.data()+36*table,36,c));
        for(unsigned i=0;i<9;++i) EXPECT_EQ(static_cast<std::uint32_t>(c[i]),0x81234560u+9*table+i);
    }
    std::array<std::uint8_t,36> one{}; ShortChecks<LR::ColorTable>(one,LR::DecodeColors);
}
TEST(LegacyResource, ObjectInfoEveryField)
{
    auto b=ResourceGolden::Object(); LR::ObjectInfo o;
    ASSERT_TRUE(LR::DecodeObjectInfo(b.data(),b.size(),o));
    const std::int32_t ints[]{o.radius,o.yLo,o.yHi,o.lineLength,o.lineIntensity,o.circleRadius,
        o.circleIntensity,o.flags,o.groundRadius,o.defaultLight,o.lastAnimationTime};
    for(unsigned i=0;i<11;++i) EXPECT_EQ(static_cast<std::uint32_t>(ints[i]),ResourceGolden::ObjectInts[i]);
    EXPECT_EQ(o.boundRadius,0x80000000u);
    for(unsigned i=0;i<16;++i) EXPECT_EQ(o.reserved[i],b[48+i]);
    ShortChecks<LR::ObjectInfo>(b,LR::DecodeObjectInfo);
}
TEST(LegacyResource, FogAndWaterTransportFloatBitsAndInvalidIndices)
{
    LR::Fog f; ASSERT_TRUE(LR::DecodeFog(ResourceGolden::Fog.data(),20,f));
    EXPECT_EQ(static_cast<std::uint32_t>(f.rgb),0xfedcba98u); EXPECT_EQ(f.mortal,-1);
    EXPECT_EQ(f.yBegin,0x80000000u); EXPECT_EQ(f.transparency,1u); EXPECT_EQ(f.limit,0x7fc12345u);
    for(auto bits : {0u,0x80000000u,1u,0x7f800000u,0xff800000u,0x7fc12345u}) {
        auto b=ResourceGolden::Fog; ResourceGolden::Put32(b,4,bits);
        ASSERT_TRUE(LR::DecodeFog(b.data(),20,f)); EXPECT_EQ(f.yBegin,bits);
    }
    LR::Water w; ASSERT_TRUE(LR::DecodeWater(ResourceGolden::Water.data(),16,w));
    EXPECT_EQ(w.texture,-2); EXPECT_EQ(w.level,0x12345678); EXPECT_EQ(w.transparency,0x3f400000u);
    EXPECT_EQ(static_cast<std::uint32_t>(w.rgb),0xfedcba98u);
    for(auto index : {0u,1024u,0x80000000u,0x7fffffffu,0xffffffffu}) {
        auto b=ResourceGolden::Water; ResourceGolden::Put32(b,0,index);
        ASSERT_TRUE(LR::DecodeWater(b.data(),16,w)); EXPECT_EQ(static_cast<std::uint32_t>(w.texture),index);
    }
    ShortChecks<LR::Fog>(ResourceGolden::Fog,LR::DecodeFog);
    ShortChecks<LR::Water>(ResourceGolden::Water,LR::DecodeWater);
}
TEST(LegacyResource, AllSixteenRandomEffects)
{
    const auto b=ResourceGolden::Effects(); LR::RandomEffects r;
    ASSERT_TRUE(LR::DecodeRandomEffects(b.data(),b.size(),r));
    for(unsigned i=0;i<16;++i) {
        EXPECT_EQ(static_cast<std::uint32_t>(r[i].number),0x80000000u+i);
        EXPECT_EQ(r[i].volume,-1-static_cast<int>(i)); EXPECT_EQ(r[i].frequency,0x12345678+i);
        EXPECT_EQ(r[i].environment,0x9200+i); EXPECT_EQ(r[i].flags,0xabff-i);
    }
    ShortChecks<LR::RandomEffects>(b,LR::DecodeRandomEffects);
    std::array<std::uint8_t,16> one{}; ShortChecks<LR::RandomEffect>(one,LR::DecodeRandomEffect);
}
TEST(LegacyResource, TextureBoundsAndEndian)
{
    std::array<std::uint16_t,5> words{};
    const auto& b=ResourceGolden::Pixels;
    ASSERT_TRUE(LR::DecodeTexture(b.data(),b.size(),words.data(),5,5));
    EXPECT_EQ(words,(std::array<std::uint16_t,5>{0,65535,0x9234,0x3412,0xcdab}));
    const auto before=words;
    for(unsigned n=0;n<10;++n) {
        EXPECT_FALSE(LR::DecodeTexture(b.data(),n,words.data(),5,5)); EXPECT_EQ(words,before);
    }
    EXPECT_FALSE(LR::DecodeTexture(b.data(),10,words.data(),4,5)); EXPECT_EQ(words,before);
    EXPECT_FALSE(LR::DecodeTexture(nullptr,10,words.data(),5,5));
    EXPECT_FALSE(LR::DecodeTexture(b.data(),10,nullptr,5,5));
    EXPECT_FALSE(LR::DecodeTexture(b.data(),SIZE_MAX,words.data(),5,SIZE_MAX));
    EXPECT_TRUE(LR::DecodeTexture(nullptr,0,nullptr,0,0));
}
TEST(LegacyResource, PCMIncludingOddFinalByteAndBounds)
{
    const auto& b=ResourceGolden::PCM; std::array<std::int16_t,7> samples{};
    ASSERT_TRUE(LR::DecodePCM16(b.data(),13,samples.data(),7,13));
    EXPECT_EQ(samples,(std::array<std::int16_t,7>{-32768,32767,-1,0,4660,-4660,171}));
    const auto before=samples;
    for(unsigned n=0;n<13;++n) {
        EXPECT_FALSE(LR::DecodePCM16(b.data(),n,samples.data(),7,13)); EXPECT_EQ(samples,before);
    }
    EXPECT_FALSE(LR::DecodePCM16(b.data(),13,samples.data(),6,13)); EXPECT_EQ(samples,before);
    EXPECT_FALSE(LR::DecodePCM16(nullptr,13,samples.data(),7,13));
    EXPECT_FALSE(LR::DecodePCM16(b.data(),13,nullptr,7,13));
    EXPECT_FALSE(LR::DecodePCM16(b.data(),SIZE_MAX,samples.data(),7,SIZE_MAX));
    EXPECT_TRUE(LR::DecodePCM16(nullptr,0,nullptr,0,0));
    std::uint8_t last=0xff; ASSERT_TRUE(LR::DecodePCM16(&last,1,samples.data(),7,1)); EXPECT_EQ(samples[0],255);
}
