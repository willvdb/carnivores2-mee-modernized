#include <gtest/gtest.h>
#include <cstdlib>
#include <algorithm>
int siRand(int);
float ca=1,sa=0,cb=1,sb=0; // Unused rotation globals referenced by Vector.cpp.

TEST(LegacyRandom, SignedRangeSurvivesLargeLibcRandMax) {
    std::srand(1);
    int low=256,high=-256;
    for(int i=0;i<10000;++i) {
        const int value=siRand(255);
        ASSERT_GE(value,-255); ASSERT_LE(value,256); // Legacy inclusive rounding.
        low=std::min(low,value); high=std::max(high,value);
    }
    EXPECT_LT(low,-128); EXPECT_GT(high,128);
    EXPECT_EQ(siRand(RAND_MAX),0); // Existing sentinel.
}
