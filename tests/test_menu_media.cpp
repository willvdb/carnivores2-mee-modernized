#define _MAIN_
#include "../Menu/Hunt.h"
#include "media_fixtures.h"
#include "media_test_file.h"
int g_ResCount=0; TRes g_ResolutionList[128]{};
void ShowErrorMessage(const std::string& m) { throw std::runtime_error(m); }
TEST(MenuMedia, TgaHeaderBytesOrderAndOpaquePictures) {
    auto b=MediaGolden::Tga();b[0]=3;b.insert(b.begin()+18,{'I','D',0});b.push_back(99);
    MediaFile f(b);TargaImage t;ASSERT_TRUE(ReadTGAFile(f.path,t));
    EXPECT_EQ(t.m_Header.tgaWidth,3);EXPECT_EQ(t.m_Header.tgaHeight,2);EXPECT_EQ(t.m_Header.tgaDescriptor,1);
    for(size_t i=0;i<12;++i) EXPECT_EQ(t.m_Data[i],b[21+i]);
    Picture p;ASSERT_TRUE(LoadPicture(p,f.path));
    const unsigned words[]{0x8000,0x8001,0x9234,0x9234,0xffff,0x8000};
    for(int i=0;i<6;++i) EXPECT_EQ(p.m_Data[i],words[i]);
}
TEST(MenuMedia, TgaPolicyAndWavEvenPayload) {
    for(int field:{1,2}) {auto b=MediaGolden::Tga();b[field]=1;MediaFile f(b);TargaImage t;EXPECT_FALSE(ReadTGAFile(f.path,t));}
    auto b=MediaGolden::Wav();MediaFile f(b);SoundFX s;ASSERT_TRUE(LoadWave(s,f.path));
    EXPECT_EQ(s.m_Length,10u);EXPECT_EQ(s.m_Frequency,22050u);
    const int words[]{-32768,32767,-1,0,0x1234};for(int i=0;i<5;++i) EXPECT_EQ(s.m_Data[i],words[i]);
}
