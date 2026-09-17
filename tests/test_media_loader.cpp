#include <gtest/gtest.h>
#include "Hunt.h"
#include "temp_path.h"
#include "media_fixtures.h"
#include "media_test_file.h"
#include <stdexcept>
Platform::FileHandle Heap=Platform::CreateHeap();std::int32_t HARD3D=true,NightVisionOn=false;
extern std::uint32_t MediaEndPosition;
extern std::vector<Platform::FileHandle> MediaHandles;
void* _HeapAlloc(Platform::FileHandle h,std::uint32_t f,size_t n,MemoryTag) { return Platform::AllocateHeap(h,f|Platform::ZeroMemoryFlag,n); }
std::int32_t _HeapFree(Platform::FileHandle h,std::uint32_t f,void* p) { return Platform::FreeHeap(h,f,p); }
[[noreturn]] void DoHalt(const char* m) {
    for(auto h:MediaHandles) if(h!=Platform::InvalidFile) Platform::CloseFile(h);
    MediaHandles.clear();throw std::runtime_error(m);
}
std::uint16_t conv_565(std::uint16_t c) { return (c&31)+((c&0xffe0)<<1); }
TEST(MediaLoader, TgaRowsWordsAndConversion) {
    auto b=MediaGolden::Tga();b.push_back(0x77);MediaFile file(b);TPicture p;
    LoadPictureTGA(p,file.path,MemoryTag::Global);
    ASSERT_EQ(p.W,3);ASSERT_EQ(p.H,2);EXPECT_EQ(MediaEndPosition,30u);
    const unsigned words[]{0x9234,0xffff,0x8000,0,1,0x1234};
    for(int i=0;i<6;++i) EXPECT_EQ(p.lpImage[i],words[i]);
    conv_pic(p);for(int i=0;i<6;++i) EXPECT_EQ(p.lpImage[i],conv_565(words[i]));
}
TEST(MediaLoader, BmpTightRowsAndIgnoredFields) {
    auto b=MediaGolden::Bmp();b[0]='X';MediaGolden::Put(b,10,1234);MediaGolden::Put(b,14,99);
    MediaFile file(b);TPicture p;LoadPicture(p,file.path,MemoryTag::Global);
    ASSERT_EQ(p.W,3);ASSERT_EQ(p.H,2);EXPECT_EQ(MediaEndPosition,72u);
    const unsigned words[]{0x1234,0x7fff,0,0,1,0x1234};
    for(int i=0;i<6;++i) EXPECT_EQ(p.lpImage[i],words[i]);
}
TEST(MediaLoader, RejectsImageDimensionsAndTruncation) {
    for(bool tga:{false,true}) {
        auto good=tga?MediaGolden::Tga():MediaGolden::Bmp();
        for(size_t n=0;n<good.size();++n) {
            auto b=good;b.resize(n);MediaFile file(b);TPicture p;
            if(tga) EXPECT_THROW(LoadPictureTGA(p,file.path,MemoryTag::Global),std::runtime_error);
            else EXPECT_THROW(LoadPicture(p,file.path,MemoryTag::Global),std::runtime_error);
        }
    }
    for(int w:{0,-1,801}) {auto b=MediaGolden::Bmp();MediaGolden::Put(b,18,w);MediaFile f(b);TPicture p;EXPECT_THROW(LoadPicture(p,f.path,MemoryTag::Global),std::runtime_error);}
    for(int h:{0,-1}) {auto b=MediaGolden::Bmp();MediaGolden::Put(b,22,h);MediaFile f(b);TPicture p;EXPECT_THROW(LoadPicture(p,f.path,MemoryTag::Global),std::runtime_error);}
    auto b=MediaGolden::Tga();b[12]=0;MediaFile f(b);TPicture p;EXPECT_THROW(LoadPictureTGA(p,f.path,MemoryTag::Global),std::runtime_error);
}
TEST(MediaLoader, WavSignedOddZeroChunkAndPosition) {
    for(size_t length:{size_t(0),size_t(10),size_t(11),size_t(4097)}) {
        std::vector<std::uint8_t> pcm(length);for(size_t i=0;i<length;++i) pcm[i]=static_cast<std::uint8_t>(i*37);
        auto b=MediaGolden::Wav(pcm);b.push_back(0xab);MediaFile f(b);TSFX s;LoadWav(f.path,s);
        EXPECT_EQ(s.length,length);ASSERT_EQ(s.lpData.size(),length/2+length%2);EXPECT_EQ(MediaEndPosition,46+length);
        for(size_t i=0;i<s.lpData.size();++i) {int v=pcm[2*i]+(2*i+1<length?256*pcm[2*i+1]:0);EXPECT_EQ(s.lpData[i],v<32768?v:v-65536);}
    }
}
TEST(MediaLoader, WavRejectsTruncationAndLengths) {
    auto good=MediaGolden::Wav();
    for(size_t n=0;n<good.size();++n) {auto b=good;b.resize(n);MediaFile f(b);TSFX s;EXPECT_THROW(LoadWav(f.path,s),std::runtime_error);}
    for(unsigned n:{0xffffffffu,0x80000000u,16777217u}) {auto b=good;MediaGolden::Put(b,42,n);MediaFile f(b);TSFX s;EXPECT_THROW(LoadWav(f.path,s),std::runtime_error);}
}

#include "Loaders/ImageIO.h"
#include "Loaders/AudioIO.h"
TEST(MediaLoader, ChunkedTgaAndIgnoredMetadata) {
    auto b=MediaGolden::Tga();MediaGolden::Put(b,12,2051,2);b.resize(18+2051*2*2);
    // Engine historically ignores ID, color map, type, depth and origin.
    b[0]=7;b[1]=1;b[2]=10;b[16]=24;b[17]=0x30;
    for(size_t i=0;i<4102;++i) MediaGolden::Put(b,18+2*i,static_cast<unsigned>(i*37),2);
    MediaFile f(b);TPicture p;LoadPictureTGA(p,f.path,MemoryTag::Global);
    ASSERT_EQ(p.W,2051);ASSERT_EQ(p.H,2);EXPECT_EQ(MediaEndPosition,b.size());
    for(size_t i=0;i<4102;++i) EXPECT_EQ(p.lpImage[i],static_cast<std::uint16_t>(((i+2051)%4102)*37));
}
TEST(MediaLoader, StreamingCapacityFailuresDoNotConsumeInput) {
    auto b=MediaGolden::Wav();MediaFile f(b);
    Platform::FileHandle file=Platform::OpenFile(f.path, Platform::FileMode::Read);
    std::uint16_t pixels[2]{99,99};short pcm[2]{99,99};
    EXPECT_FALSE(EngineImage::ReadPixels(file,pixels,2,3));
    EXPECT_FALSE(EngineImage::ReadPixels(file,pixels,SIZE_MAX,SIZE_MAX));
    EXPECT_FALSE(EngineImage::ReadPixels(file,nullptr,2,2));
    EXPECT_FALSE(EngineAudio::ReadPCM16(file,pcm,2,5));
    EXPECT_FALSE(EngineAudio::ReadPCM16(file,pcm,2,SIZE_MAX));
    EXPECT_FALSE(EngineAudio::ReadPCM16(file,nullptr,2,3));
    EXPECT_EQ(Platform::SeekFile(file, 0, Platform::SeekOrigin::Current),0u);
    EXPECT_EQ(pixels[0],99);EXPECT_EQ(pcm[0],99);
    EXPECT_TRUE(EngineImage::ReadPixels(file,nullptr,0,0));
    EXPECT_TRUE(EngineAudio::ReadPCM16(file,nullptr,0,0));
    Platform::SeekFile(file, static_cast<std::int32_t>(b.size()-1), Platform::SeekOrigin::Begin);
    EXPECT_FALSE(EngineImage::ReadPixels(file,pixels,2,1));EXPECT_EQ(pixels[0],99);
    Platform::SeekFile(file, static_cast<std::int32_t>(b.size()-1), Platform::SeekOrigin::Begin);
    EXPECT_FALSE(EngineAudio::ReadPCM16(file,pcm,2,2));EXPECT_EQ(pcm[0],99);
    Platform::CloseFile(file);
}
TEST(MediaLoader, WavSignedExtremes) {
    MediaFile f(MediaGolden::Wav());TSFX s;LoadWav(f.path,s);
    const int expected[]{-32768,32767,-1,0,0x1234};
    ASSERT_EQ(s.lpData.size(),5u);for(int i=0;i<5;++i) EXPECT_EQ(s.lpData[i],expected[i]);
}
