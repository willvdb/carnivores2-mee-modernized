#include <gtest/gtest.h>
#include "../Shared/LegacyImage.h"
#include "media_fixtures.h"
TEST(LegacyImage, CompleteTgaHeaderAndShortInputs) {
    std::vector<std::uint8_t> b{3,1,2,0x34,0x12,0x78,0x56,24,0xbc,0x9a,0xf0,0xde,0x23,1,0x56,4,16,0x21};
    LegacyImage::TgaHeader h{};
    ASSERT_TRUE(LegacyImage::DecodeTgaHeader(b.data(),b.size(),h));
    EXPECT_EQ(h.idLength,3);EXPECT_EQ(h.colorMapType,1);EXPECT_EQ(h.imageType,2);
    EXPECT_EQ(h.colorMapOffset,0x1234);EXPECT_EQ(h.colorMapLength,0x5678);EXPECT_EQ(h.colorMapBits,24);
    EXPECT_EQ(h.xOrigin,0x9abc);EXPECT_EQ(h.yOrigin,0xdef0);EXPECT_EQ(h.width,0x123);EXPECT_EQ(h.height,0x456);
    EXPECT_EQ(h.bits,16);EXPECT_EQ(h.descriptor,0x21);
    for(size_t n=0;n<18;++n) {EXPECT_FALSE(LegacyImage::DecodeTgaHeader(b.data(),n,h));EXPECT_EQ(h.width,0x123);}
    EXPECT_FALSE(LegacyImage::DecodeTgaHeader(nullptr,18,h));
    b[12]=b[13]=b[14]=b[15]=0;ASSERT_TRUE(LegacyImage::DecodeTgaHeader(b.data(),18,h));EXPECT_EQ(h.width,0);EXPECT_EQ(h.height,0);
}
TEST(LegacyImage, CompleteBmpHeaderSignedDimensionsAndTruncation) {
    auto b=MediaGolden::Bmp();
    MediaGolden::Put(b,2,0x92345678);MediaGolden::Put(b,6,0x1234,2);MediaGolden::Put(b,8,0xabcd,2);
    MediaGolden::Put(b,10,0x12345678);MediaGolden::Put(b,18,0x80000000);MediaGolden::Put(b,22,0xffffffff);
    MediaGolden::Put(b,30,0x87654321);MediaGolden::Put(b,34,0x76543210);
    MediaGolden::Put(b,38,0xfffffffe);MediaGolden::Put(b,42,0x12345678);MediaGolden::Put(b,46,0xfedcba98);MediaGolden::Put(b,50,0xabcdef01);
    LegacyImage::BmpHeader h{};ASSERT_TRUE(LegacyImage::DecodeBmpHeader(b.data(),54,h));
    EXPECT_EQ(h.signature,0x4d42);EXPECT_EQ(h.fileSize,0x92345678u);EXPECT_EQ(h.reserved1,0x1234);EXPECT_EQ(h.reserved2,0xabcd);
    EXPECT_EQ(h.pixelOffset,0x12345678u);EXPECT_EQ(h.headerSize,40u);EXPECT_EQ(h.width,INT32_MIN);EXPECT_EQ(h.height,-1);
    EXPECT_EQ(h.planes,1);EXPECT_EQ(h.bits,24);EXPECT_EQ(h.compression,0x87654321u);EXPECT_EQ(h.imageSize,0x76543210u);
    EXPECT_EQ(h.xPixelsPerMeter,-2);EXPECT_EQ(h.yPixelsPerMeter,0x12345678);EXPECT_EQ(h.colorsUsed,0xfedcba98u);EXPECT_EQ(h.colorsImportant,0xabcdef01u);
    for(size_t n=0;n<54;++n) {EXPECT_FALSE(LegacyImage::DecodeBmpHeader(b.data(),n,h));EXPECT_EQ(h.width,INT32_MIN);}
    EXPECT_FALSE(LegacyImage::DecodeBmpHeader(nullptr,54,h));
}
TEST(LegacyImage, PixelsAndCapacityOverflow) {
    const std::uint8_t b[]{0,0,1,0,0x34,0x12,0x34,0x92,255,255};
    std::uint16_t out[5]{};ASSERT_TRUE(LegacyImage::DecodePixels(b,10,out,5,5));
    const unsigned expected[]{0,1,0x1234,0x9234,0xffff};for(int i=0;i<5;++i) EXPECT_EQ(out[i],expected[i]);
    for(size_t n=0;n<10;++n) EXPECT_FALSE(LegacyImage::DecodePixels(b,n,out,5,5));
    EXPECT_FALSE(LegacyImage::DecodePixels(b,10,out,4,5));
    EXPECT_FALSE(LegacyImage::DecodePixels(b,SIZE_MAX,out,SIZE_MAX,SIZE_MAX));
    EXPECT_FALSE(LegacyImage::DecodePixels(nullptr,10,out,5,5));
    EXPECT_FALSE(LegacyImage::DecodePixels(b,10,nullptr,5,5));
    EXPECT_TRUE(LegacyImage::DecodePixels(nullptr,0,nullptr,0,0));EXPECT_EQ(out[2],0x1234);
}
