#include <gtest/gtest.h>
#include "Hunt.h"
#include "Loaders/LoadValidate.h"
#include "legacy_model_fixtures.h"
#include <stdexcept>

// Isolate real ModelLoader Win32 I/O and conversion from the game/renderer.
HANDLE Heap=GetProcessHeap(), hfile=INVALID_HANDLE_VALUE;
std::uint32_t l=0;
int OCount=0, MaxObjectVCount=0, OptBrightness=128;
TObj gObj[1024]{};
#ifdef _soft
BOOL HARD3D=FALSE;
#else
BOOL HARD3D=TRUE;
#endif
// Unused resource texture/map paths share this translation unit.
decltype(SkyFade) SkyFade{}; decltype(SkyMap) SkyMap{};
decltype(Textures) Textures{}; decltype(FadeTab) FadeTab{};
decltype(OptDayNight) OptDayNight{}; decltype(SkyPic) SkyPic{};
decltype(WMap) WMap{}; decltype(FMap) FMap{}; decltype(TMap1) TMap1{};
decltype(MapPic) MapPic{}; decltype(WaterList) WaterList{};
int SkyR=0, SkyG=0, SkyB=0;
WORD conv_565(WORD) { throw std::runtime_error("unexpected map conversion"); }
LPVOID _HeapAlloc(HANDLE heap, std::uint32_t flags, size_t bytes, MemoryTag)
{ return HeapAlloc(heap,flags|HEAP_ZERO_MEMORY,bytes); }
LPVOID _HeapAlloc(HANDLE heap, std::uint32_t flags, size_t bytes)
{ return _HeapAlloc(heap,flags,bytes,MemoryTag::Global); }
BOOL _HeapFree(HANDLE heap,std::uint32_t flags,LPVOID p) { return HeapFree(heap,flags,p); }
[[noreturn]] void DoHalt(char* message) { throw std::runtime_error(message); }
void CalcLights(TModel*) {}
void ReleaseModelTexture(const TModel*) {}
void LoadAnimation(TVTL&,int);
void ReleaseModelBuffers(TModel*);

namespace {
struct File {
    char path[MAX_PATH]{};
    explicit File(const std::vector<std::uint8_t>& b) {
        char dir[MAX_PATH]; GetTempPathA(MAX_PATH,dir); GetTempFileNameA(dir,"mdl",0,path);
        HANDLE f=CreateFileA(path,GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
        DWORD got=0; EXPECT_TRUE(WriteFile(f,b.data(),static_cast<DWORD>(b.size()),&got,nullptr));
        EXPECT_EQ(got,b.size()); CloseHandle(f);
    }
    ~File() { if(hfile!=INVALID_HANDLE_VALUE) { CloseHandle(hfile); hfile=INVALID_HANDLE_VALUE; } DeleteFileA(path); }
};
void Word(std::vector<std::uint8_t>& b,std::uint32_t value)
{ for(unsigned i=0;i<4;++i) { b.push_back(value%256); value/=256; } }
template<class T> void Append(std::vector<std::uint8_t>& b,const T& a)
{ b.insert(b.end(),a.begin(),a.end()); }
}
TEST(ModelLoader, CharacterSingleFrameDuplicationAndOptionalTail)
{
    std::vector<std::uint8_t> b(32,'C');
    Word(b,1); Word(b,0); Word(b,1); Word(b,0); Word(b,512);
    Append(b,ModelGolden::Vertex()); b.resize(b.size()+512,0);
    b.resize(b.size()+32,'A'); Word(b,20); Word(b,1);
    b.insert(b.end(),ModelGolden::Samples.begin(),ModelGolden::Samples.begin()+6);
    // Short optional tail must discard every association.
    Word(b,7);
    File file(b); TCharacterInfo ch{};
    LoadCharacterInfo(ch,file.path);
    EXPECT_EQ(ch.AniCount,1); EXPECT_EQ(ch.Animation[0].FramesCount,1);
    EXPECT_EQ(ch.Animation[0].aniKPS,20); EXPECT_EQ(ch.Animation[0].AniTime,50);
    for(int i=0;i<3;++i) EXPECT_EQ(ch.Animation[0].aniData[i],ch.Animation[0].aniData[i+3]);
    EXPECT_EQ(ch.Animation[0].aniData[0],-32768);
    EXPECT_EQ(ch.Animation[0].aniData[1],32767); EXPECT_EQ(ch.Animation[0].aniData[2],-1);
    for(int value:ch.Anifx) EXPECT_EQ(value,-1);
    EXPECT_EQ(ch.mptr->gVertex[0].x,2.f); EXPECT_EQ(ch.mptr->gVertex[0].y,-5.f);
    ReleaseCharacterInfo(ch);
}
TEST(ModelLoader, ObjectAnimationReadsExtraStoredFrame)
{
    std::vector<std::uint8_t> b; Word(b,0x12345678); Word(b,1); Word(b,20); Word(b,1);
    Append(b,ModelGolden::Samples); Word(b,0x76543210);
    File file(b); hfile=CreateFileA(file.path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    TVTL vtl{}; LoadAnimation(vtl,1);
    EXPECT_EQ(vtl.FramesCount,2); EXPECT_EQ(vtl.AniTime,100);
    const short expected[]{-32768,32767,-1,0,4660,-4660};
    for(int i=0;i<6;++i) EXPECT_EQ(vtl.aniData[i],expected[i]);
    EXPECT_EQ(SetFilePointer(hfile,0,nullptr,FILE_CURRENT),28u);
}
TEST(ModelLoader, ThreeDFGeometryAndRendererUVConversion)
{
    std::vector<std::uint8_t> b; Word(b,1); Word(b,1); Word(b,1); Word(b,512);
    auto face=ModelGolden::Face();
    for(unsigned i=0;i<3;++i) ModelGolden::Put32(face,4*i,0);
    for(unsigned i=0;i<6;++i) ModelGolden::Put32(face,12+4*i,16+i);
    face[36]=0; face[37]=0; Append(b,face);
    Append(b,ModelGolden::Vertex()); Append(b,ModelGolden::Object());
    b.resize(b.size()+512,0); b[b.size()-512]=0x34; b[b.size()-511]=0x12;
    File file(b); unique_obj_ptr<TModel> m; LoadModelEx(m,file.path);
    EXPECT_EQ(m->VCount,1); EXPECT_EQ(m->FCount,1); EXPECT_EQ(OCount,1);
    EXPECT_EQ(m->lpTexture[0],HARD3D ? 0x1234 : 0x2468); EXPECT_EQ(m->gFace[0].DMask,0x6987);
#ifdef _soft
    EXPECT_EQ(m->gFace[0].tax,(16<<16)+0x8000);
    EXPECT_EQ(m->gFace[0].tcy,(21<<16)+0x8000);
#else
    EXPECT_EQ(m->gFace[0].tax,16.f); EXPECT_EQ(m->gFace[0].tcy,21.f);
#endif
    EXPECT_EQ(std::memcmp(gObj[0].OName,ModelGolden::Object().data(),32),0);
    ReleaseModelBuffers(m.get());
}
TEST(ModelLoader, ExistingCountDurationAndSizeBoundaries)
{
    EXPECT_TRUE(IsValidCount(0,1024)); EXPECT_TRUE(IsValidCount(1024,1024));
    EXPECT_FALSE(IsValidCount(-1,1024)); EXPECT_FALSE(IsValidCount(1025,1024));
    EXPECT_TRUE(IsValidCount(64,64)); EXPECT_FALSE(IsValidCount(65,64));
    size_t bytes=123;
    EXPECT_TRUE(CheckedTransferBytes3(1<<20,1,6,bytes)); EXPECT_EQ(bytes,6291456u);
    EXPECT_FALSE(CheckedTransferBytes3(1<<20,INT32_MAX/256,6,bytes));
    EXPECT_FALSE(CheckedBytes3(SIZE_MAX,2,6,bytes));
    int duration=123;
    EXPECT_TRUE(CheckedAnimationDuration(1,20,duration)); EXPECT_EQ(duration,50);
    EXPECT_FALSE(CheckedAnimationDuration(1,0,duration));
    EXPECT_FALSE(CheckedAnimationDuration(1,-1,duration));
    EXPECT_FALSE(CheckedAnimationDuration(1,1001,duration));
    EXPECT_TRUE(CheckedAnimationDuration(INT32_MAX/256,1000,duration));
}

TEST(ModelLoader, RejectsInvalidGeometryCountsBeforeAllocation)
{
    for(const auto counts : {std::pair<int,int>{0,0}, {-1,0}, {(1<<20)+1,0}, {1,-1}, {1,(1<<20)+1}}) {
        std::vector<std::uint8_t> b; Word(b,counts.first); Word(b,counts.second); Word(b,0); Word(b,0);
        File file(b); unique_obj_ptr<TModel> model;
        EXPECT_THROW(LoadModelEx(model,file.path),std::runtime_error);
        ASSERT_TRUE(model); EXPECT_EQ(model->gVertex.get(),nullptr); EXPECT_EQ(model->gFace,nullptr);
    }
}
TEST(ModelLoader, RejectsInvalidAnimationHeadersAndTruncation)
{
    for(const int frames : {0,-1,INT32_MAX/256}) {
        std::vector<std::uint8_t> b; Word(b,0); Word(b,1); Word(b,20); Word(b,frames);
        File file(b); hfile=CreateFileA(file.path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        TVTL vtl{}; EXPECT_THROW(LoadAnimation(vtl,1),std::runtime_error);
        EXPECT_EQ(vtl.aniData.get(),nullptr);
    }
    std::vector<std::uint8_t> b(15,0); File file(b);
    hfile=CreateFileA(file.path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
    TVTL vtl{}; EXPECT_THROW(LoadAnimation(vtl,1),std::runtime_error);
    EXPECT_EQ(vtl.aniData.get(),nullptr);
}

TEST(ModelLoader, CharacterPCMAndAssociationAlignment)
{
    for(size_t length:{size_t(0),size_t(10),size_t(11),size_t(4097)}) {
        std::vector<std::uint8_t> b(32,'C');
        Word(b,0);Word(b,2);Word(b,1);Word(b,0);Word(b,512);
        Append(b,ModelGolden::Vertex());b.resize(b.size()+512);
        std::vector<std::uint8_t> pcm(length);
        for(size_t i=0;i<length;++i) pcm[i]=static_cast<std::uint8_t>(i*37+0xab);
        const std::uint8_t extremes[]{0,0x80,255,0x7f,255,255,0,0,0x34,0x12};
        for(size_t i=0;i<length && i<10;++i) pcm[i]=extremes[i];
        b.resize(b.size()+32,'S');Word(b,static_cast<unsigned>(length));Append(b,pcm);
        b.resize(b.size()+32,'T');Word(b,2);b.push_back(0);b.push_back(0x80);
        for(int i=0;i<64;++i) Word(b,0x12340000+i);
        File file(b);TCharacterInfo ch{};LoadCharacterInfo(ch,file.path);
        ASSERT_EQ(ch.SoundFX[0].lpData.size(),length/2+length%2);
        EXPECT_EQ(ch.SoundFX[0].length,length);
        for(size_t i=0;i<ch.SoundFX[0].lpData.size();++i) {
            int v=pcm[2*i]+(2*i+1<length?256*pcm[2*i+1]:0);
            EXPECT_EQ(ch.SoundFX[0].lpData[i],v<32768?v:v-65536);
        }
        EXPECT_EQ(ch.SoundFX[1].length,2);EXPECT_EQ(ch.SoundFX[1].lpData[0],-32768);
        for(int i=0;i<64;++i) EXPECT_EQ(ch.Anifx[i],0x12340000+i);
        ReleaseCharacterInfo(ch);
    }
}
TEST(ModelLoader, CharacterPCMRejectsBadLengthAndTruncation)
{
    for(unsigned length:{0xffffffffu,0x80000000u,16777217u,1u,4097u}) {
        std::vector<std::uint8_t> b(32,'C');Word(b,0);Word(b,1);Word(b,1);Word(b,0);Word(b,512);
        Append(b,ModelGolden::Vertex());b.resize(b.size()+512+32);Word(b,length);
        File file(b);TCharacterInfo ch{};EXPECT_THROW(LoadCharacterInfo(ch,file.path),std::runtime_error);
        ReleaseCharacterInfo(ch);
    }
}
