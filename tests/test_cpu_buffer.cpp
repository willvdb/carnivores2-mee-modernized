#include <gtest/gtest.h>
#include "../Hunt/Renderer/CPUTextRaster.h"
#include <algorithm>

TEST(CPUBuffer, OddWidthHasExplicitTightlyPackedPitch) {
    CPUText::Buffer buffer;
    ASSERT_TRUE(buffer.Resize(801, 17));
    EXPECT_EQ(buffer.Pitch(), 801);
    EXPECT_EQ(buffer.Height(), 17);
    EXPECT_TRUE(std::all_of(buffer.Pixels(), buffer.Pixels()+801*17, [](auto v){ return v == 0; }));
    buffer.Pixels()[801*16+800] = 0x7fff;
    EXPECT_FALSE(buffer.Resize(-1, 17));
    EXPECT_EQ(buffer.Pixels()[801*16+800], 0x7fff);
}
TEST(CPUBuffer, RasterClipsToViewAndPreservesPadding) {
    std::uint16_t pixels[50];
    std::fill(std::begin(pixels),std::end(pixels),0x1234);
    CPUText::RasterCanvas canvas;
    ASSERT_TRUE(canvas.Ready());
    canvas.SetBuffer(pixels+10, 7, 3, 10);
    canvas.Draw(-3,-3,"Wide text",0x00ffffff,{16,7,100});
    for (int i=0;i<10;++i) EXPECT_EQ(pixels[i],0x1234);
    for (int i=40;i<50;++i) EXPECT_EQ(pixels[i],0x1234);
    for (int row=1;row<4;++row)
        for (int col=7;col<10;++col) EXPECT_EQ(pixels[row*10+col],0x1234);
}
TEST(CPUBuffer, RasterWritesLegacyRGB555AndMeasuresProportionalText) {
    CPUText::Buffer buffer;
    ASSERT_TRUE(buffer.Resize(160, 40));
    CPUText::RasterCanvas canvas;
    ASSERT_TRUE(canvas.Ready());
    canvas.SetBuffer(buffer.Pixels(),160,40,160);
    EXPECT_GT(canvas.Width("WWW",{16,7,100}),canvas.Width("iii",{16,7,100}));
    canvas.Draw(2,2,"Loading...",0x000000ff,{16,7,100});
    int painted=0;
    for (int i=0;i<160*40;++i) if(buffer.Pixels()[i]) { EXPECT_EQ(buffer.Pixels()[i],0x7c00); ++painted; }
    EXPECT_GT(painted,0);
}
