#define _MAIN_
#include "../Menu/Hunt.h"
#include "media_fixtures.h"
#include "media_test_file.h"
#include <memory>
int g_ResCount=0; TRes g_ResolutionList[128]{};
void ShowErrorMessage(const std::string& m) { throw std::runtime_error(m); }
TEST(MenuMedia, TgaHeaderBytesOrderAndOpaquePictures) {
    auto b=MediaGolden::Tga();b[0]=3;MediaGolden::Put(b,3,0x1234,2);MediaGolden::Put(b,5,0x5678,2);b[7]=24;MediaGolden::Put(b,8,0x9abc,2);MediaGolden::Put(b,10,0xdef0,2);b.insert(b.begin()+18,{'I','D',0});b.push_back(99);
    MediaFile f(b);TargaImage t;ASSERT_TRUE(ReadTGAFile(f.path,t));
    EXPECT_EQ(t.m_Header.tgaWidth,3);EXPECT_EQ(t.m_Header.tgaHeight,2);EXPECT_EQ(t.m_Header.tgaDescriptor,1);
    EXPECT_EQ(t.m_Header.tgaColorMapOffset,0x1234);EXPECT_EQ(t.m_Header.tgaColorMapLength,0x5678);
    EXPECT_EQ(t.m_Header.tgaColorMapBits,24);EXPECT_EQ(t.m_Header.tgaXStart,0x9abc);EXPECT_EQ(t.m_Header.tgaYStart,0xdef0);
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

TEST(MenuMedia, TgaTruncationZeroAndDescriptorPolicy) {
    auto good=MediaGolden::Tga();
    for(size_t n=0;n<good.size();++n) {auto b=good;b.resize(n);MediaFile f(b);TargaImage t;EXPECT_FALSE(ReadTGAFile(f.path,t));}
    good[17]=0x30;MediaGolden::Put(good,8,0x1234,2);MediaGolden::Put(good,10,0x5678,2);
    {MediaFile f(good);Picture p;ASSERT_TRUE(LoadPicture(p,f.path));EXPECT_EQ(p.m_Data[0],0x8000);}
    good[12]=0;{MediaFile f(good);Picture p;EXPECT_TRUE(LoadPicture(p,f.path));EXPECT_EQ(p.m_Width,0u);}
    good=MediaGolden::Tga();good[16]=24;good.resize(36);
    {MediaFile f(good);TargaImage t;EXPECT_TRUE(ReadTGAFile(f.path,t));Picture p;EXPECT_FALSE(LoadPicture(p,f.path));}
    good.resize(18);good[0]=255;{MediaFile f(good);TargaImage t;EXPECT_FALSE(ReadTGAFile(f.path,t));}
    good=MediaGolden::Tga();good[12]=good[13]=good[14]=good[15]=255;
    {MediaFile f(good);TargaImage t;EXPECT_FALSE(ReadTGAFile(f.path,t));}
}
TEST(MenuMedia, BackgroundPreservesAlphaFileOrderAndChecksCapacity) {
    auto b=MediaGolden::Tga();MediaGolden::Put(b,12,800,2);MediaGolden::Put(b,14,600,2);b.resize(18+960000);
    MediaGolden::Put(b,18+959998,0x1234,2);b.push_back(0xab);
    struct Background { uint16_t pixels[800*600]{}; };
    auto storage=std::make_unique<Background>();
    auto& pixels=storage->pixels;
    {MediaFile f(b);ASSERT_TRUE(LoadMenuBackground(pixels,f.path));EXPECT_EQ(pixels[0],0);EXPECT_EQ(pixels[1],1);EXPECT_EQ(pixels[2],0x1234);EXPECT_EQ(pixels[3],0x9234);EXPECT_EQ(pixels[479999],0x1234);}
    b=MediaGolden::Tga();{MediaFile f(b);EXPECT_FALSE(LoadMenuBackground(pixels,f.path));}
}

#include <filesystem>
#include <fstream>
AreaInfo MakeOldAreaInfo(int index, int price);
void LoadC2Maps();
namespace {
class MenuPaths : public testing::Test {
protected:
    std::filesystem::path root, previous;
    void SetUp() override {
        char dir[MAX_PATH], name[MAX_PATH];
        ASSERT_NE(GetTempPathA(MAX_PATH, dir), 0u);
        ASSERT_NE(GetTempFileNameA(dir, "pth", 0, name), 0u);
        ASSERT_TRUE(DeleteFileA(name));
        root = name;
        ASSERT_TRUE(std::filesystem::create_directory(root));
        previous = std::filesystem::current_path();
        std::filesystem::current_path(root);
        g_AreaInfo.clear();
    }
    void TearDown() override {
        g_AreaInfo.clear();
        std::filesystem::current_path(previous);
        std::filesystem::remove_all(root);
    }
    void Text(const std::filesystem::path& path, const std::string& text) {
        std::filesystem::create_directories(path.parent_path());
        std::ofstream out(path); out << text;
        ASSERT_TRUE(out.good());
    }
};
}
TEST_F(MenuPaths, AreaSixKeepsExternalFirstAndAreaSixFallback) {
    Text("HuNtDaT/ArEaS/ArEa6.MAP", "map probe");
    auto area = MakeOldAreaInfo(6, 500);
    EXPECT_TRUE(area.m_Valid);
    EXPECT_EQ(area.m_ProjectName, "area6");
    EXPECT_EQ(area.m_MapFile, "area6");
    Text("HuNtDaT/ArEaS/ExTeRnAl.MAP", "external probe");
    area = MakeOldAreaInfo(6, 500);
    EXPECT_TRUE(area.m_Valid);
    EXPECT_EQ(area.m_ProjectName, "area6");
    EXPECT_EQ(area.m_MapFile, "external");
    EXPECT_EQ(area.m_Price, 500);
    // No RSC, description or thumbnail required by existing menu policy.
}
TEST_F(MenuPaths, ResourceScriptFallbackRetainsPrecedence) {
    Text("HuNtDaT/_ReS.TxT", "accessories\n{\ncamo = 0.55\n}\n.\n");
    LoadResourcesScript();
    EXPECT_FLOAT_EQ(g_AccessoryScoreMods.at("camo"), 0.55f);
    Text("HuNtDaT/_MeNu.TxT", "accessories\n{\ncamo = 0.77\n}\n.\n");
    LoadResourcesScript();
    EXPECT_FLOAT_EQ(g_AccessoryScoreMods.at("camo"), 0.77f);
}
TEST_F(MenuPaths, DescriptorPathsResolveWithoutChangingCandidateRules) {
    Text("HuNtDaT/ArEaS/CuStOm.MAP", "map probe");
    Text("HuNtDaT/MeNu/TxT/Custom.TXT", "Custom description\n");
    Text("HuNtDaT/ArEaS/test.c2map",
         "info\n{\nname = 'Custom'\nmapfile = 'huntdat\\areas\\custom'\n"
         "text = 'HUNTDAT\\menu/TXT/custom.txt'\nprice = 12\n}\n.\n");
    LoadC2Maps();
    ASSERT_EQ(g_AreaInfo.size(), 1u);
    EXPECT_EQ(g_AreaInfo[0].m_ProjectName, "custom");
    EXPECT_EQ(g_AreaInfo[0].m_Description[0], "Custom description");
    EXPECT_EQ(g_AreaInfo[0].m_Price, 12);
    // Script-defined entries still win; missing descriptor MAPs stay skipped.
    Text("HuNtDaT/ArEaS/missing.c2map", "info\n{\nmapfile = 'absent'\n}\n.\n");
    LoadC2Maps();
    EXPECT_EQ(g_AreaInfo.size(), 1u);
}
