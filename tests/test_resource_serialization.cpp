#include <gtest/gtest.h>
#include "Hunt.h"
#include "legacy_resource_fixtures.h"

TEST(ResourceLayout, GoldenRecordsMatchStableRuntime)
{
    const auto b=ResourceGolden::Object(); TObjInfo o;
    std::memcpy(&o,b.data(),b.size());
    const int values[]{o.Radius,o.YLo,o.YHi,o.linelenght,o.lintensity,o.circlerad,
        o.cintensity,o.flags,o.GrRad,o.DefLight,o.LastAniTime};
    for(unsigned i=0;i<11;++i) EXPECT_EQ(static_cast<std::uint32_t>(values[i]),ResourceGolden::ObjectInts[i]);
    EXPECT_TRUE(std::signbit(o.BoundR)); EXPECT_EQ(o.BoundR,0.f);
    EXPECT_EQ(std::memcmp(o.res,b.data()+48,16),0);
    TFogEntity f; std::memcpy(&f,ResourceGolden::Fog.data(),20);
    EXPECT_EQ(static_cast<std::uint32_t>(f.fogRGB),0xfedcba98u);
    EXPECT_TRUE(std::signbit(f.YBegin)); EXPECT_EQ(f.Mortal,-1);
    EXPECT_EQ(f.Transp,std::numeric_limits<float>::denorm_min()); EXPECT_TRUE(std::isnan(f.FLimit));
    const auto effects=ResourceGolden::Effects(); TRD r[16]; std::memcpy(r,effects.data(),256);
    for(unsigned i=0;i<16;++i) {
        EXPECT_EQ(static_cast<std::uint32_t>(r[i].RNumber),0x80000000u+i);
        EXPECT_EQ(r[i].RVolume,-1-static_cast<int>(i)); EXPECT_EQ(r[i].RFreq,0x12345678+i);
        EXPECT_EQ(r[i].REnvir,0x9200+i); EXPECT_EQ(r[i].Flags,0xabff-i);
    }
    TWaterEntity w; std::memcpy(&w,ResourceGolden::Water.data(),16);
    EXPECT_EQ(w.tindex,-2); EXPECT_EQ(w.wlevel,0x12345678); EXPECT_EQ(w.transp,.75f);
    EXPECT_EQ(static_cast<std::uint32_t>(w.fogRGB),0xfedcba98u);
    short pcm[7]{}; std::memcpy(pcm,ResourceGolden::PCM.data(),13);
    const short expected[]{-32768,32767,-1,0,4660,-4660,171};
    for(unsigned i=0;i<7;++i) EXPECT_EQ(pcm[i],expected[i]);
}

#include "Loaders/ResourceSerialization.h"
TEST(ResourceLayout, AdaptersMatchEveryLegacyByte)
{
    const auto ob=ResourceGolden::Object(); LegacyResource::ObjectInfo o; TObjInfo ro;
    ASSERT_TRUE(LegacyResource::DecodeObjectInfo(ob.data(),64,o)); EngineResource::ToRuntime(o,ro);
    EXPECT_EQ(std::memcmp(&ro,ob.data(),64),0);
    LegacyResource::Fog f; TFogEntity rf;
    ASSERT_TRUE(LegacyResource::DecodeFog(ResourceGolden::Fog.data(),20,f)); EngineResource::ToRuntime(f,rf);
    EXPECT_EQ(std::memcmp(&rf,ResourceGolden::Fog.data(),20),0);
    const auto eb=ResourceGolden::Effects(); LegacyResource::RandomEffects effects; TRD re[16];
    ASSERT_TRUE(LegacyResource::DecodeRandomEffects(eb.data(),256,effects));
    for(unsigned i=0;i<16;++i) EngineResource::ToRuntime(effects[i],re[i]);
    EXPECT_EQ(std::memcmp(re,eb.data(),256),0);
    LegacyResource::Water w; TWaterEntity rw;
    ASSERT_TRUE(LegacyResource::DecodeWater(ResourceGolden::Water.data(),16,w)); EngineResource::ToRuntime(w,rw);
    EXPECT_EQ(std::memcmp(&rw,ResourceGolden::Water.data(),16),0);
    const auto cb=ResourceGolden::Colors();
    for(unsigned table=0;table<2;++table) {
        LegacyResource::ColorTable c; int rc[3][3];
        ASSERT_TRUE(LegacyResource::DecodeColors(cb.data()+36*table,36,c)); EngineResource::ToRuntime(c,rc);
        EXPECT_EQ(std::memcmp(rc,cb.data()+36*table,36),0);
    }
}
