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

#include "../Hunt/Loaders/ModelSerialization.h"
TEST(ModelLayout, ExplicitAdaptersMatchEveryLegacyByteBeforeCorrection)
{
    const auto vb=ModelGolden::Vertex(); LegacyModel::Vertex v; TPoint3d vertex;
    ASSERT_TRUE(LegacyModel::DecodeVertex(vb.data(),vb.size(),v));
    EngineModel::ToRuntime(v,vertex); EXPECT_EQ(std::memcmp(&vertex,vb.data(),16),0);
    const auto fb=ModelGolden::Face(); LegacyModel::Face f; TFace face;
    ASSERT_TRUE(LegacyModel::DecodeFace(fb.data(),fb.size(),f));
    EngineModel::ToRuntime(f,face); EXPECT_EQ(std::memcmp(&face,fb.data(),64),0);
    // The old and new inputs to BOTH unchanged renderer conversions are identical.
    // Use unsigned arithmetic to express x86 wrapping without signed-shift UB.
    const auto checkUV=[](const auto& field, std::int32_t expected) {
        std::int32_t raw; std::memcpy(&raw,&field,4);
        EXPECT_EQ(raw,expected);
        EXPECT_EQ(static_cast<float>(raw),static_cast<float>(expected));
        EXPECT_EQ((static_cast<std::uint32_t>(raw)<<16)+0x8000u,
                  (static_cast<std::uint32_t>(expected)<<16)+0x8000u);
    };
    checkUV(face.tax,-1); checkUV(face.tbx,0); checkUV(face.tcx,255);
    checkUV(face.tay,128); checkUV(face.tby,17); checkUV(face.tcy,0x12345678);
    const auto ob=ModelGolden::Object(); LegacyModel::Object o; TObj object;
    ASSERT_TRUE(LegacyModel::DecodeObject(ob.data(),ob.size(),o));
    EngineModel::ToRuntime(o,object); EXPECT_EQ(std::memcmp(&object,ob.data(),48),0);
}
