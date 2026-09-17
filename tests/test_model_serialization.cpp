#include <gtest/gtest.h>
#include "Hunt.h"
#include "legacy_model_fixtures.h"

TEST(ModelLayout, GoldenGeometryMatchesStableRuntime)
{
    const auto vb=ModelGolden::Vertex(); TPoint3d v;
    std::memcpy(&v,vb.data(),vb.size());
    EXPECT_EQ(v.x,1.f); EXPECT_EQ(v.y,-2.5f); EXPECT_TRUE(std::signbit(v.z));
    EXPECT_EQ(v.owner,-32768); EXPECT_EQ(v.hide,32767);
    const auto fb=ModelGolden::Face(); TFace f;
    std::memcpy(&f,fb.data(),fb.size());
    EXPECT_EQ(f.v1,0x12345678); EXPECT_EQ(f.v2,2); EXPECT_EQ(f.v3,-1);
    std::int32_t uv[6]; std::memcpy(uv,fb.data()+12,24);
    EXPECT_EQ(uv[0],-1); EXPECT_EQ(uv[5],0x12345678);
    EXPECT_EQ(f.Flags,0xa135); EXPECT_EQ(f.DMask,0x6987);
    EXPECT_EQ(f.Distant,-2023406815); EXPECT_EQ(f.Next,-2); EXPECT_EQ(f.group,0x10203040);
    EXPECT_EQ(std::memcmp(f.reserv,fb.data()+52,12),0);
    const auto ob=ModelGolden::Object(); TObj o;
    std::memcpy(&o,ob.data(),ob.size());
    EXPECT_EQ(std::memcmp(o.OName,ob.data(),32),0);
    EXPECT_EQ(o.ox,.5f); EXPECT_EQ(o.oy,-10.f); EXPECT_TRUE(std::signbit(o.oz));
    EXPECT_EQ(o.owner,-2); EXPECT_EQ(o.hide,0x1234);
    short samples[6]; std::memcpy(samples,ModelGolden::Samples.data(),12);
    EXPECT_EQ(samples[0],-32768); EXPECT_EQ(samples[1],32767); EXPECT_EQ(samples[2],-1);
    EXPECT_EQ(samples[3],0); EXPECT_EQ(samples[4],4660); EXPECT_EQ(samples[5],-4660);
}
