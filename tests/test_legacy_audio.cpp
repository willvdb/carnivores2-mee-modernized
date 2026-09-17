#include <gtest/gtest.h>
#include "../Shared/LegacyAudio.h"
TEST(LegacyAudio, LengthAndSignedEvenOddSamples) {
    const std::uint8_t b[]{0,0x80,255,0x7f,255,255,0,0,0x34,0x12,0x34,0x92,0xab};
    std::int16_t out[7]{};ASSERT_TRUE(LegacyAudio::DecodePCM16(b,13,out,7,13));
    const int expected[]{-32768,32767,-1,0,0x1234,-28108,0xab};
    for(int i=0;i<7;++i) EXPECT_EQ(out[i],expected[i]);
    ASSERT_TRUE(LegacyAudio::DecodePCM16(b,12,out,6,12));
    std::uint32_t length=0;ASSERT_TRUE(LegacyAudio::DecodeLength(b+8,4,length));EXPECT_EQ(length,0x92341234u);
    for(size_t n=0;n<4;++n) EXPECT_FALSE(LegacyAudio::DecodeLength(b,n,length));
    EXPECT_FALSE(LegacyAudio::DecodeLength(nullptr,4,length));EXPECT_EQ(length,0x92341234u);
}
TEST(LegacyAudio, TruncationCapacityZeroAndOverflow) {
    const std::uint8_t b[]{0x34,0x12,0xab};std::int16_t out[]{99,99};
    for(size_t n=0;n<3;++n) EXPECT_FALSE(LegacyAudio::DecodePCM16(b,n,out,2,3));
    EXPECT_FALSE(LegacyAudio::DecodePCM16(b,3,out,1,3));
    EXPECT_FALSE(LegacyAudio::DecodePCM16(b,3,out,2,SIZE_MAX));
    EXPECT_FALSE(LegacyAudio::DecodePCM16(b,SIZE_MAX,out,2,SIZE_MAX));
    EXPECT_EQ(LegacyAudio::SampleCount(SIZE_MAX),SIZE_MAX/2+1);
    EXPECT_FALSE(LegacyAudio::DecodePCM16(nullptr,3,out,2,3));
    EXPECT_FALSE(LegacyAudio::DecodePCM16(b,3,nullptr,2,3));
    EXPECT_TRUE(LegacyAudio::DecodePCM16(nullptr,0,nullptr,0,0));EXPECT_EQ(out[0],99);EXPECT_EQ(out[1],99);
}
