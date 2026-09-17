#include <gtest/gtest.h>
#include "Hunt.h"
#include "Loaders/ResourceIO.h"
#include "legacy_resource_fixtures.h"
#include "legacy_map_fixtures.h"
#include <stdexcept>
#include <string>

// Actual LoadResources + ModelLoader + StateDefs, with no graphics context.
// Existing notifications bound RSC-only tests and complete MAP loads.
struct ResourceComplete {};
struct MapComplete {};
static bool loadMap = false;
static DWORD closedPosition = 0;
BOOL ResourceTestCloseHandle(HANDLE file) {
    if(file==hfile) closedPosition=SetFilePointer(file,0,nullptr,FILE_CURRENT);
    return CloseHandle(file);
}
void PrintLoad(char* text) {
    if(std::string(text)=="Loading .map..." && !loadMap) throw ResourceComplete{};
    if(std::string(text)=="Prepearing maps...") throw MapComplete{};
}
[[noreturn]] void DoHalt(char* text) { throw std::runtime_error(text); }
void CalcLights(TModel*) {}
void CalcBoundBox(TModel*,TBound*) {}
void ReleaseModelTexture(const TModel*) {}
void ClearRendererLevelCache() {}
void ClearRendererTerrainCache() {}
int rRand(int) { return 0; }
void CreateTMap() { throw std::runtime_error("unexpected map processing"); }
void LoadPictureTGA(TPicture&,LPSTR,MemoryTag) { throw std::runtime_error("unexpected picture loading"); }
void conv_pic(TPicture&) { throw std::runtime_error("unexpected picture conversion"); }
float GetLandQH(float,float) { throw std::runtime_error("unexpected map height"); }
float GetLandH(float,float) { throw std::runtime_error("unexpected map height"); }
void CopyHARDToDIB() { throw std::runtime_error("unexpected screenshot"); }
void NormVector(Vector3d&,float) { throw std::runtime_error("unexpected map lighting"); }

namespace {
void Word(std::vector<std::uint8_t>& b,std::uint32_t value)
{ for(unsigned i=0;i<4;++i) { b.push_back(value%256); value/=256; } }
template<class T> void Append(std::vector<std::uint8_t>& b,const T& a)
{ b.insert(b.end(),a.begin(),a.end()); }
struct Fixture {
    std::vector<std::uint8_t> bytes;
    std::size_t object,sky,fog,random,ambient,effects,effectCount,water;
    Fixture() {
        Word(bytes,1); Word(bytes,1);
        for(unsigned i=0;i<18;++i) Word(bytes,20+i);
        for(unsigned i=0;i<128*128;++i) { bytes.push_back(0x34); bytes.push_back(0x12); }
        bytes[80]=bytes[81]=0;
        object=bytes.size(); bytes.resize(bytes.size()+64,0);
        ResourceGolden::Put32(bytes,static_cast<unsigned>(object),10);
        ResourceGolden::Put32(bytes,static_cast<unsigned>(object+4),0xfffffffc);
        ResourceGolden::Put32(bytes,static_cast<unsigned>(object+8),6);
        ResourceGolden::Put32(bytes,static_cast<unsigned>(object+12),255);
        for(unsigned i=0;i<16;++i) bytes[object+48+i]=0xa0+i;
        // Embedded model: one vertex, no faces/objects, one texture row.
        Word(bytes,1); Word(bytes,0); Word(bytes,0); Word(bytes,512);
        Word(bytes,0x3f800000); Word(bytes,0x40000000); Word(bytes,0); Word(bytes,0);
        bytes.resize(bytes.size()+512,0);
        for(unsigned i=0;i<128*128;++i) { bytes.push_back(0x34); bytes.push_back(0x12); }
        sky=bytes.size();
        for(unsigned day=0;day<3;++day)
            for(unsigned i=0;i<256*256;++i) { bytes.push_back(0x34+day); bytes.push_back(0x12); }
        for(unsigned i=0;i<128*128;++i) bytes.push_back(i%256);
        fog=bytes.size(); Word(bytes,1); Append(bytes,ResourceGolden::Fog);
        random=bytes.size(); Word(bytes,1); Word(bytes,13); Append(bytes,ResourceGolden::PCM);
        ambient=bytes.size(); Word(bytes,1); Word(bytes,13); Append(bytes,ResourceGolden::PCM);
        effects=bytes.size(); auto e=ResourceGolden::Effects();
        for(unsigned i=0;i<16;++i) {
            ResourceGolden::Put32(e,i*16,i); ResourceGolden::Put32(e,i*16+8,10+i);
            e[i*16+14]=i==0 ? 1 : 0; e[i*16+15]=0;
        }
        Append(bytes,e); effectCount=bytes.size(); Word(bytes,2); Word(bytes,123);
        water=bytes.size(); Word(bytes,1); Word(bytes,0); Word(bytes,17); Word(bytes,0x3f400000); Word(bytes,0xfedcba98);
    }
};
struct File {
    std::string path;
    explicit File(const std::vector<std::uint8_t>& bytes) {
        char dir[MAX_PATH],name[MAX_PATH]; GetTempPathA(MAX_PATH,dir); GetTempFileNameA(dir,"rsc",0,name);
        DeleteFileA(name); path=std::string(name)+".rsc";
        HANDLE f=CreateFileA(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
        DWORD got=0; EXPECT_TRUE(WriteFile(f,bytes.data(),static_cast<DWORD>(bytes.size()),&got,nullptr));
        EXPECT_EQ(got,bytes.size()); CloseHandle(f);
    }
    void Open() { hfile=CreateFileA(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr); }
    void Load() {
        auto stem=path.substr(0,path.size()-4); strcpy_s(ProjectName,stem.c_str());
        try { LoadResources(); } catch(const ResourceComplete&) { hfile=INVALID_HANDLE_VALUE; throw; }
        catch(const MapComplete&) { hfile=INVALID_HANDLE_VALUE; throw; }
    }
    ~File() { if(hfile!=INVALID_HANDLE_VALUE) { CloseHandle(hfile); hfile=INVALID_HANDLE_VALUE; } DeleteFileA(path.c_str()); }
};
class ResourceLoader : public testing::Test {
    void SetUp() override {
        Heap=GetProcessHeap(); hfile=INVALID_HANDLE_VALUE; hlog=INVALID_HANDLE_VALUE;
        OptBrightness=128; OptDayNight=1; srand(1); loadMap=false; closedPosition=0;
#ifdef _soft
        HARD3D=FALSE;
#else
        HARD3D=TRUE;
#endif
    }
    void TearDown() override { ReleaseResources(); }
};
}
TEST_F(ResourceLoader, CompleteStreamAndDayNightFiltering)
{
    Fixture f;
    for(int day=0;day<3;++day) {
        SCOPED_TRACE(day); File file(f.bytes); OptDayNight=day;
        EXPECT_THROW(file.Load(),ResourceComplete);
        EXPECT_EQ(SkyG,21+day*3); EXPECT_EQ(SkyTG,30+day*3);
        EXPECT_EQ(SkyR,day==2 ? 0 : 20+day*3); EXPECT_EQ(SkyB,day==2 ? 0 : 22+day*3);
        EXPECT_EQ(MObjects[0].info.Radius,20); EXPECT_EQ(MObjects[0].info.YLo,-8);
        EXPECT_EQ(MObjects[0].info.YHi,12); EXPECT_EQ(MObjects[0].info.linelenght,128);
        EXPECT_EQ(MObjects[0].info.BoundR,2.f);
        for(unsigned i=0;i<16;++i) EXPECT_EQ(MObjects[0].info.res[i],0xa0+i);
        EXPECT_EQ(Textures[0]->DataA[0],HARD3D ? 1 : 2);
        EXPECT_EQ(Textures[0]->DataA[1],HARD3D ? 0x1234 : 0x2468);
        EXPECT_EQ(MObjects[0].bmpmodel.lpTexture[0],HARD3D ? 0x9234 : 0x2468);
        EXPECT_EQ(SkyPic[0],0x1234+day); EXPECT_EQ(SkyMap[16383],255);
        EXPECT_EQ(static_cast<std::uint32_t>(FogsList[1].fogRGB),0xfedcba98u);
        EXPECT_TRUE(std::signbit(FogsList[1].YBegin)); EXPECT_EQ(FogsList[1].Mortal,-1);
        const short pcm[]{-32768,32767,-1,0,4660,-4660,171};
        EXPECT_EQ(RandSound[0].length,13); EXPECT_EQ(Ambient[0].sfx.length,13);
        ASSERT_EQ(RandSound[0].lpData.size(),7); ASSERT_EQ(Ambient[0].sfx.lpData.size(),7);
        for(unsigned i=0;i<7;++i) { EXPECT_EQ(RandSound[0].lpData[i],pcm[i]); EXPECT_EQ(Ambient[0].sfx.lpData[i],pcm[i]); }
        EXPECT_EQ(Ambient[0].AVolume,123); EXPECT_EQ(Ambient[0].RndTime,5000);
        EXPECT_EQ(Ambient[0].RSFXCount,day==2 ? 1 : 2);
        EXPECT_EQ(Ambient[0].rdata[0].RNumber,day==2 ? 1 : 0);
        EXPECT_EQ(Ambient[0].rdata[0].RFreq,10); EXPECT_EQ(Ambient[0].rdata[0].REnvir,0x9200);
        EXPECT_EQ(Ambient[0].rdata[15].RNumber,15);
        EXPECT_EQ(WaterList[0].tindex,0); EXPECT_EQ(WaterList[0].wlevel,17); EXPECT_EQ(WaterList[0].transp,.75f);
        EXPECT_EQ(WaterList[0].fogRGB,Textures[0]->mB+(Textures[0]->mG<<8)+(Textures[0]->mR<<16));
    }
}
TEST_F(ResourceLoader, TruncatedSectionsFailBeforeMap)
{
    Fixture f;
    const std::size_t ends[]{4,8,44,80,f.object,f.object+64,f.sky,f.sky+3*131072,
        f.fog,f.fog+4,f.random,f.random+4,f.random+8,f.ambient,f.ambient+4,f.ambient+8,
        f.effects,f.effectCount,f.effectCount+4,f.water,f.water+4,f.bytes.size()};
    for(auto end:ends) {
        SCOPED_TRACE(end); auto b=f.bytes; b.resize(end-1); File file(b);
        EXPECT_THROW(file.Load(),std::runtime_error);
    }
}
TEST_F(ResourceLoader, InvalidCountsAndLengthsAreRejected)
{
    Fixture f;
    for(auto entry : {std::pair<std::size_t,unsigned>{0,1024},{4,256},{f.fog,255},{f.random,256},
                     {f.ambient,256},{f.effectCount,16},{f.water,256},{f.random+4,16u<<20},{f.ambient+4,16u<<20}}) {
        for(auto invalid : {0xffffffffu,0x80000000u,entry.second+1}) {
            SCOPED_TRACE(entry.first); auto b=f.bytes;
            ResourceGolden::Put32(b,static_cast<unsigned>(entry.first),invalid); File file(b);
            EXPECT_THROW(file.Load(),std::runtime_error);
        }
    }
    for(auto invalid : {0xffffffffu,1u,0x7fffffffu}) {
        auto b=f.bytes; ResourceGolden::Put32(b,static_cast<unsigned>(f.water+4),invalid); File file(b);
        EXPECT_THROW(file.Load(),std::runtime_error);
    }
}
TEST_F(ResourceLoader, ProductionHelpersPreservePositionAndShortOutputs)
{
    std::vector<std::uint8_t> b; Append(b,ResourceGolden::Colors()); Append(b,ResourceGolden::Object());
    Append(b,ResourceGolden::Fog); Append(b,ResourceGolden::Effects()); Append(b,ResourceGolden::Water);
    Append(b,ResourceGolden::Pixels); Append(b,ResourceGolden::PCM); Word(b,0x12345678);
    File file(b); file.Open();
    int colors[3][3]; TObjInfo object; TFogEntity fog; TRD effects[16]; TWaterEntity water;
    ASSERT_TRUE(EngineResource::Read(hfile,colors)); ASSERT_TRUE(EngineResource::Read(hfile,colors));
    EXPECT_EQ(SetFilePointer(hfile,0,nullptr,FILE_CURRENT),72u);
    ASSERT_TRUE(EngineResource::Read(hfile,object)); EXPECT_EQ(SetFilePointer(hfile,0,nullptr,FILE_CURRENT),136u);
    ASSERT_TRUE(EngineResource::Read(hfile,fog)); ASSERT_TRUE(EngineResource::Read(hfile,effects));
    EXPECT_EQ(std::memcmp(effects,ResourceGolden::Effects().data(),256),0);
    ASSERT_TRUE(EngineResource::Read(hfile,water)); EXPECT_EQ(SetFilePointer(hfile,0,nullptr,FILE_CURRENT),428u);
    unsigned short pixels[5]; ASSERT_TRUE(EngineResource::ReadTexture(hfile,pixels,5,5)); EXPECT_EQ(pixels[2],0x9234);
    short pcm[7]; ASSERT_TRUE(EngineResource::ReadPCM16(hfile,pcm,7,13)); EXPECT_EQ(pcm[6],171);
    int sentinel=0; ASSERT_TRUE(EngineResource::Read(hfile,sentinel)); EXPECT_EQ(sentinel,0x12345678);
    EXPECT_EQ(SetFilePointer(hfile,0,nullptr,FILE_CURRENT),455u);
    EXPECT_FALSE(EngineResource::Read(hfile,sentinel)); EXPECT_EQ(sentinel,0x12345678);
    const auto old=object; EXPECT_FALSE(EngineResource::Read(hfile,object)); EXPECT_EQ(std::memcmp(&old,&object,64),0);
    EXPECT_FALSE(EngineResource::ReadTexture(hfile,pixels,4,5));
    EXPECT_FALSE(EngineResource::ReadPCM16(hfile,pcm,6,13));
    EXPECT_EQ(SetFilePointer(hfile,0,nullptr,FILE_CURRENT),455u);
}
TEST_F(ResourceLoader, ChunkedPayloadsAndSkySeekPositions)
{
    std::vector<std::uint8_t> b(4099);
    for(unsigned i=0;i<b.size();++i) b[i]=i%256;
    File file(b); file.Open(); short samples[2050]{};
    ASSERT_TRUE(EngineResource::ReadPCM16(hfile,samples,2050,4099));
    EXPECT_EQ(samples[2047],-2); EXPECT_EQ(samples[2048],256); EXPECT_EQ(samples[2049],2);
    EXPECT_EQ(SetFilePointer(hfile,0,nullptr,FILE_CURRENT),4099u);
    EXPECT_TRUE(EngineResource::ReadPCM16(hfile,nullptr,0,0));
    EXPECT_TRUE(EngineResource::ReadTexture(hfile,nullptr,0,0));
    CloseHandle(hfile); hfile=INVALID_HANDLE_VALUE;
    for(int day=0;day<3;++day) {
        std::vector<std::uint8_t> sky(3*131072+16384,0);
        for(unsigned image=0;image<3;++image) {
            sky[image*131072]=0x34+image; sky[image*131072+1]=0x12;
        }
        sky[3*131072]=0xab; File sf(sky); sf.Open(); OptDayNight=day;
        LoadSky(); EXPECT_EQ(SkyPic[0],0x1234+day);
        EXPECT_EQ(SetFilePointer(hfile,0,nullptr,FILE_CURRENT),3*131072u);
        LoadSkyMap(); EXPECT_EQ(SkyMap[0],0xab);
        EXPECT_EQ(SetFilePointer(hfile,0,nullptr,FILE_CURRENT),3*131072u+16384);
    }
}
TEST_F(ResourceLoader, EmptyCountsAndMaximumFogWaterTables)
{
    // No textures/models/fog/sounds/ambient/water is accepted by the RSC path.
    std::vector<std::uint8_t> empty(80+3*131072+16384+16,0);
    File ef(empty); EXPECT_THROW(ef.Load(),ResourceComplete);
    Fixture fixture; auto b=fixture.bytes;
    // Expand tables in reverse offset order to preserve earlier offsets.
    std::vector<std::uint8_t> waters;
    for(unsigned i=0;i<256;++i) { Word(waters,0); Word(waters,17); Word(waters,0x3f400000); Word(waters,0); }
    b.erase(b.begin()+fixture.water+4,b.end()); Append(b,waters);
    ResourceGolden::Put32(b,static_cast<unsigned>(fixture.water),256);
    std::vector<std::uint8_t> fogs;
    for(unsigned i=0;i<254;++i) Append(fogs,ResourceGolden::Fog);
    b.insert(b.begin()+fixture.fog+24,fogs.begin(),fogs.end());
    ResourceGolden::Put32(b,static_cast<unsigned>(fixture.fog),255);
    File file(b); EXPECT_THROW(file.Load(),ResourceComplete);
    EXPECT_EQ(FogsList[255].Mortal,-1); EXPECT_EQ(WaterList[254].wlevel,17);
    EXPECT_EQ(WaterList[255].wlevel,0);
}
TEST_F(ResourceLoader, OptionalAnimationRemainsAtLegacyBoundary)
{
    Fixture f; auto b=f.bytes;
    ResourceGolden::Put32(b,static_cast<unsigned>(f.object+28),0x80000000);
    std::vector<std::uint8_t> animation;
    Word(animation,0x12345678); Word(animation,1); Word(animation,20); Word(animation,1);
    animation.insert(animation.end(),ResourceGolden::PCM.begin(),ResourceGolden::PCM.begin()+12);
    b.insert(b.begin()+f.sky,animation.begin(),animation.end());
    File file(b); EXPECT_THROW(file.Load(),ResourceComplete);
    EXPECT_EQ(MObjects[0].vtl.FramesCount,2); EXPECT_EQ(MObjects[0].vtl.AniTime,100);
    EXPECT_EQ(MObjects[0].vtl.aniData[0],-32768); EXPECT_EQ(MObjects[0].vtl.aniData[5],-4660);
    EXPECT_EQ(SkyPic[0],0x1235); EXPECT_EQ(Ambient[0].AVolume,123);
    b.resize(f.sky+animation.size()-1); File shortFile(b);
    EXPECT_THROW(shortFile.Load(),std::runtime_error);
}

namespace {
// Streaming fixture writer uses one row, not copies of a whole map.
struct MapFile {
    std::string path;
    explicit MapFile(const File& rsc) : path(rsc.path.substr(0,rsc.path.size()-4)+".map") {
        HANDLE f=CreateFileA(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,0,nullptr);
        for(unsigned plane=0;plane<12;++plane) {
            const bool word=plane==1 || plane==2 || plane==4;
            const unsigned width=plane>=10 ? 512 : 1024;
            std::vector<std::uint8_t> row(width*(word ? 2 : 1),plane==3 ? 255 : 0);
            for(unsigned y=0;y<width;++y) {
                std::fill(row.begin(),row.end(),plane==3 ? 255 : 0);
                if(y==0) {
                    if(plane==1 || plane==2) { row[2]=255; row[3]=255; } // sentinel
                    else if(plane==4) {
                        for(unsigned x=0;x<MapGolden::Flags.size();++x) {
                            row[2*x]=MapGolden::Flags[x]%256;
                            row[2*x+1]=MapGolden::Flags[x]/256;
                        }
                    } else if(plane==3) { row[0]=0; row[1]=254; }
                    else if(plane==8) { row[0]=255; } // dry cell, intentionally unvalidated
                    else { row[0]=0; row[1]=128; row[2]=255; }
                }
                if(plane>=5 && plane<=7) row[width-1]=static_cast<std::uint8_t>(40+plane);
                DWORD wrote=0; EXPECT_TRUE(WriteFile(f,row.data(),static_cast<DWORD>(row.size()),&wrote,nullptr));
                EXPECT_EQ(wrote,row.size());
            }
        }
        EXPECT_EQ(SetFilePointer(f,0,nullptr,FILE_CURRENT),MapGolden::Size);
        CloseHandle(f);
    }
    void Patch(std::size_t offset,std::initializer_list<std::uint8_t> bytes) {
        HANDLE f=CreateFileA(path.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        SetFilePointer(f,static_cast<LONG>(offset),nullptr,FILE_BEGIN); DWORD wrote=0;
        ASSERT_TRUE(WriteFile(f,bytes.begin(),static_cast<DWORD>(bytes.size()),&wrote,nullptr));
        CloseHandle(f);
    }
    void Truncate(std::size_t size) {
        HANDLE f=CreateFileA(path.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,0,nullptr);
        SetFilePointer(f,static_cast<LONG>(size),nullptr,FILE_BEGIN); EXPECT_TRUE(SetEndOfFile(f)); CloseHandle(f);
    }
    ~MapFile() { DeleteFileA(path.c_str()); }
};
std::vector<std::uint8_t> MapResource() {
    Fixture f;
    // Two textures make the 0xffff -> 1 sentinel valid. Other counts stay 1.
    ResourceGolden::Put32(f.bytes,0,2);
    f.bytes.insert(f.bytes.begin()+f.object,32768,0);
    return f.bytes;
}
}
TEST_F(ResourceLoader, MapCompletePlanesAndAllLightSelections)
{
    File resource(MapResource()); MapFile map(resource); loadMap=true;
    for(int day=0;day<3;++day) {
        SCOPED_TRACE(day); OptDayNight=day;
        EXPECT_THROW(resource.Load(),MapComplete);
        EXPECT_EQ(closedPosition,14155776u);
        for(auto plane:{HMap,HMapO,LMap}) {
            EXPECT_EQ(plane[0][0],0); EXPECT_EQ(plane[0][1],128); EXPECT_EQ(plane[0][2],255);
        }
        EXPECT_EQ(LMap[1023][1023],45+day);
        EXPECT_EQ(TMap1[0][1],0xffff); EXPECT_EQ(TMap2[0][1],0xffff);
        EXPECT_EQ(TMap1[1023][1023],0); EXPECT_EQ(TMap2[1023][1023],0);
        for(unsigned x=0;x<MapGolden::Flags.size();++x) EXPECT_EQ(FMap[0][x],MapGolden::Flags[x]);
        EXPECT_EQ(OMap[0][0],0); EXPECT_EQ(OMap[0][1],254); EXPECT_EQ(OMap[1023][1023],255);
        EXPECT_EQ(WMap[0][0],255); EXPECT_EQ(WMap[0][1],0);
        for(auto plane:{FogsMap,AmbMap}) {
            EXPECT_EQ(plane[0][0],0); EXPECT_EQ(plane[0][1],128); EXPECT_EQ(plane[0][2],255);
            EXPECT_EQ(plane[511][511],0);
        }
    }
}
TEST_F(ResourceLoader, MapTruncationNeverReachesPostprocessing)
{
    File resource(MapResource()); loadMap=true;
    // Includes each boundary and its interior, including all skipped light maps.
    for(int day=0;day<3;++day) for(auto end:MapGolden::Offsets) for(auto delta:{0u,17u}) {
        SCOPED_TRACE(day);
        SCOPED_TRACE(end+delta); OptDayNight=day;
        MapFile map(resource); map.Truncate(end+delta);
        EXPECT_THROW(resource.Load(),std::runtime_error);
        CloseHandle(hfile); hfile=INVALID_HANDLE_VALUE;
    }
    MapFile map(resource); map.Truncate(MapGolden::Size-1);
    EXPECT_THROW(resource.Load(),std::runtime_error);
}
TEST_F(ResourceLoader, MapReferenceValidationRetainsFullWordsAndSentinels)
{
    File resource(MapResource()); loadMap=true;
    for(unsigned plane:{1u,2u}) for(auto value:{2u,0x1234u,0x9234u}) {
        MapFile map(resource); map.Patch(MapGolden::Offsets[plane],{static_cast<std::uint8_t>(value%256),static_cast<std::uint8_t>(value/256)});
        try { resource.Load(); FAIL()<<"expected texture rejection"; }
        catch(const std::runtime_error& e) { EXPECT_NE(std::string(e.what()).find("texture index out of range"),std::string::npos); }
        EXPECT_EQ(TMap1[0][0],plane==1 ? value : 0);
        EXPECT_EQ(TMap2[0][0],plane==2 ? value : 0);
        EXPECT_EQ(SetFilePointer(hfile,0,nullptr,FILE_CURRENT),MapGolden::Size);
        CloseHandle(hfile); hfile=INVALID_HANDLE_VALUE;
    }
    for(auto entry:{std::pair<unsigned,unsigned>{3,1},{8,1},{8,255}}) {
        MapFile map(resource); map.Patch(MapGolden::Offsets[entry.first]+1,{static_cast<std::uint8_t>(entry.second)});
        EXPECT_THROW(resource.Load(),std::runtime_error);
        CloseHandle(hfile); hfile=INVALID_HANDLE_VALUE;
    }
    for(unsigned count:{64u,65u}) {
        MapFile map(resource);
        for(unsigned i=0;i<count;++i) map.Patch(MapGolden::Offsets[3]+i,{254});
        if(count==64) EXPECT_THROW(resource.Load(),MapComplete);
        else { EXPECT_THROW(resource.Load(),std::runtime_error); CloseHandle(hfile); hfile=INVALID_HANDLE_VALUE; }
    }
}
