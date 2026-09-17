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

TEST(MenuMedia, WaveRejectsMissingChunkTruncationAndImpossibleLength) {
    auto good=MediaGolden::Wav();
    for(size_t n=0;n<good.size();++n) {auto b=good;b.resize(n);MediaFile f(b);SoundFX s;EXPECT_FALSE(LoadWave(s,f.path));EXPECT_EQ(s.m_Data,nullptr);}
    for(unsigned n:{0xffffffffu,0x80000000u,16777217u}) {auto b=good;MediaGolden::Put(b,42,n);MediaFile f(b);SoundFX s;EXPECT_FALSE(LoadWave(s,f.path));}
}
TEST(MenuMedia, WaveOddZeroAndChunkBoundary) {
    for(size_t length:{size_t(0),size_t(1),size_t(4097)}) {
        std::vector<std::uint8_t> pcm(length);for(size_t i=0;i<length;++i) pcm[i]=static_cast<std::uint8_t>(i*37+0xab);
        auto b=MediaGolden::Wav(pcm);b.push_back(0xee);MediaFile f(b);SoundFX s;ASSERT_TRUE(LoadWave(s,f.path));
        EXPECT_EQ(s.m_Length,length);
        for(size_t i=0;i<length/2+length%2;++i) {int v=pcm[2*i]+(2*i+1<length?256*pcm[2*i+1]:0);EXPECT_EQ(s.m_Data[i],v<32768?v:v-65536);}
    }
}
