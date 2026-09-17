#include <gtest/gtest.h>
#include "../Shared/LegacyModel.h"
#include "legacy_model_fixtures.h"
using namespace LegacyModel;

TEST(LegacyModel, GeometryPreservesEveryField)
{
    auto vb=ModelGolden::Vertex(); Vertex v;
    ASSERT_TRUE(DecodeVertex(vb.data(),vb.size(),v));
    EXPECT_EQ(v.x,0x3f800000u); EXPECT_EQ(v.y,0xc0200000u); EXPECT_EQ(v.z,0x80000000u);
    EXPECT_EQ(v.owner,-32768); EXPECT_EQ(v.hide,32767);
    for(auto bits : {0u,0x80000000u,0x3eaaaaabu,0xc1200000u,1u,0x7f7fffffu}) {
        ModelGolden::Put32(vb,0,bits);
        ASSERT_TRUE(DecodeVertex(vb.data(),vb.size(),v));
        float f; SetFloat(f,v.x); std::uint32_t result; std::memcpy(&result,&f,4);
        EXPECT_EQ(result,bits);
    }
    const auto fb=ModelGolden::Face(); Face face;
    ASSERT_TRUE(DecodeFace(fb.data(),fb.size(),face));
    EXPECT_EQ(face.indices,(std::array<std::int32_t,3>{0x12345678,2,-1}));
    EXPECT_EQ(face.uv,(std::array<std::int32_t,6>{-1,0,255,128,17,0x12345678}));
    EXPECT_EQ(face.flags,0xa135); EXPECT_EQ(face.mask,0x6987);
    EXPECT_EQ(face.distant,-2023406815); EXPECT_EQ(face.next,-2); EXPECT_EQ(face.group,0x10203040);
    EXPECT_EQ(std::memcmp(face.reserved.data(),fb.data()+52,12),0);
    const auto ob=ModelGolden::Object(); Object o;
    ASSERT_TRUE(DecodeObject(ob.data(),ob.size(),o));
    EXPECT_EQ(std::memcmp(o.name.data(),ob.data(),32),0);
    EXPECT_EQ(o.origin.x,0x3f000000u); EXPECT_EQ(o.origin.y,0xc1200000u);
    EXPECT_EQ(o.origin.z,0x80000000u); EXPECT_EQ(o.origin.owner,-2); EXPECT_EQ(o.origin.hide,0x1234);
}

TEST(LegacyModel, HeaderWordsIncludeInvalidCountsWithoutNormalization)
{
    std::array<std::uint8_t,52> b{};
    // Codec reports signed values faithfully; existing loader validation remains separate.
    for(auto count : {0,1,1989,1<<20,-1,INT32_MIN,INT32_MAX}) {
        for(unsigned i=0;i<13;++i) ModelGolden::Put32(b,i*4,static_cast<std::uint32_t>(count));
        Header h; ASSERT_TRUE(DecodeHeader(b.data(),16,h));
        EXPECT_EQ(h.vertices,count); EXPECT_EQ(h.faces,count); EXPECT_EQ(h.objects,count); EXPECT_EQ(h.textureBytes,count);
        CharacterHeader c; ASSERT_TRUE(DecodeCharacterHeader(b.data(),52,c));
        EXPECT_EQ(std::memcmp(c.name.data(),b.data(),32),0);
        EXPECT_EQ(c.animations,count); EXPECT_EQ(c.sounds,count); EXPECT_EQ(c.vertices,count);
        EXPECT_EQ(c.faces,count); EXPECT_EQ(c.textureBytes,count);
        AnimationHeader a; ASSERT_TRUE(DecodeAnimationHeader(b.data(),40,a));
        EXPECT_EQ(std::memcmp(a.name.data(),b.data(),32),0); EXPECT_EQ(a.kps,count); EXPECT_EQ(a.frames,count);
        ObjectAnimationHeader o; ASSERT_TRUE(DecodeObjectAnimationHeader(b.data(),16,o));
        EXPECT_EQ(o.type,count); EXPECT_EQ(o.vertices,count); EXPECT_EQ(o.kps,count); EXPECT_EQ(o.frames,count);
    }
}

template<class T, class Decode>
void CheckTruncation(std::size_t size, Decode decode)
{
    std::array<std::uint8_t,256> b{}; b.fill(0xa5);
    T out{};
    ASSERT_TRUE(decode(b.data(),size,out));
    // Capture the initialized object's representation only as a failure oracle.
    std::array<unsigned char,sizeof(T)> before{}; std::memcpy(before.data(),&out,sizeof(T));
    for(std::size_t n=0;n<size;++n) {
        EXPECT_FALSE(decode(b.data(),n,out));
        EXPECT_EQ(std::memcmp(before.data(),&out,sizeof(T)),0);
    }
    EXPECT_FALSE(decode(nullptr,size,out));
    EXPECT_EQ(std::memcmp(before.data(),&out,sizeof(T)),0);
}
TEST(LegacyModel, EveryShortRecordLeavesDestinationUntouched)
{
    CheckTruncation<Vertex>(16,DecodeVertex); CheckTruncation<Face>(64,DecodeFace);
    CheckTruncation<Object>(48,DecodeObject); CheckTruncation<Header>(16,DecodeHeader);
    CheckTruncation<CharacterHeader>(52,DecodeCharacterHeader);
    CheckTruncation<AnimationHeader>(40,DecodeAnimationHeader);
    CheckTruncation<ObjectAnimationHeader>(16,DecodeObjectAnimationHeader);
    CheckTruncation<std::int32_t>(4,DecodeInt32);
    CheckTruncation<std::array<std::int32_t,64>>(256,DecodeAssociations);
}
TEST(LegacyModel, SignedSamplesAndTransferBounds)
{
    std::array<std::int16_t,6> samples{};
    const auto& b=ModelGolden::Samples;
    ASSERT_TRUE(DecodeSamples(b.data(),b.size(),samples.data(),samples.size()));
    EXPECT_EQ(samples,(std::array<std::int16_t,6>{-32768,32767,-1,0,4660,-4660}));
    const auto before=samples;
    for(std::size_t n=0;n<12;++n) {
        EXPECT_FALSE(DecodeSamples(b.data(),n,samples.data(),6)); EXPECT_EQ(samples,before);
    }
    EXPECT_FALSE(DecodeSamples(b.data(),12,samples.data(),SIZE_MAX));
    EXPECT_FALSE(DecodeSamples(nullptr,12,samples.data(),6)); EXPECT_EQ(samples,before);
    EXPECT_TRUE(DecodeSamples(nullptr,0,nullptr,0));
}
TEST(LegacyModel, TextureWordsAndOddLowByte)
{
    const std::array<std::uint8_t,7> b{0,0,0xff,0xff,0x34,0x92,0xab};
    std::array<std::uint16_t,4> words{};
    ASSERT_TRUE(DecodeTexture(b.data(),7,words.data(),4));
    EXPECT_EQ(words,(std::array<std::uint16_t,4>{0,65535,0x9234,0xab}));
    const auto before=words;
    EXPECT_FALSE(DecodeTexture(b.data(),7,words.data(),3)); EXPECT_EQ(words,before);
    EXPECT_FALSE(DecodeTexture(b.data(),SIZE_MAX,words.data(),4)); EXPECT_EQ(words,before);
    EXPECT_FALSE(DecodeTexture(nullptr,7,words.data(),4)); EXPECT_EQ(words,before);
    EXPECT_TRUE(DecodeTexture(nullptr,0,nullptr,0));
}
TEST(LegacyModel, AssociationsAreSignedWords)
{
    std::array<std::uint8_t,256> b{};
    for(unsigned i=0;i<64;++i) ModelGolden::Put32(b,4*i,i%2 ? i : 0xffffffffu);
    std::array<std::int32_t,64> v;
    ASSERT_TRUE(DecodeAssociations(b.data(),b.size(),v));
    for(unsigned i=0;i<64;++i) EXPECT_EQ(v[i],i%2 ? static_cast<int>(i) : -1);
}
