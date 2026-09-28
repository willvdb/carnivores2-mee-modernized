// test_load_validate.cpp
// Unit tests for Hunt/Loaders/LoadValidate.h — the checked arithmetic and
// file-data validation helpers used by the binary/text loaders.
//
// The header is pure (only ReadExact touches Win32, via a caller-provided
// HANDLE), so no engine linkage is needed.

#include <gtest/gtest.h>

#include <cstdio>

#include "Core/GameTypes.h"
#include "Loaders/LoadValidate.h"

namespace {

TEST(LoadValidate, CountsFitFixedArrays) {
    EXPECT_TRUE(IsValidCount(0, 64));
    EXPECT_TRUE(IsValidCount(64, 64));
    EXPECT_TRUE(IsValidCount(22, 64));  // shipped .CAR AniCount maximum
    EXPECT_FALSE(IsValidCount(-1, 64));
    EXPECT_FALSE(IsValidCount(65, 64));
    EXPECT_FALSE(IsValidCount(1025, 1024));  // gObj capacity
}

TEST(LoadValidate, OrderedSpawnRangesFitCapacity) {
    EXPECT_TRUE(IsValidOrderedRange(0, 0, 256));
    EXPECT_TRUE(IsValidOrderedRange(2, 13, 256));
    EXPECT_TRUE(IsValidOrderedRange(0, 256, 256));
    EXPECT_FALSE(IsValidOrderedRange(-1, 3, 256));
    EXPECT_FALSE(IsValidOrderedRange(4, 3, 256));
    EXPECT_FALSE(IsValidOrderedRange(0, 257, 256));
}

TEST(LoadValidate, NativeByteSizesCoverArchitectureBoundaries) {
    const size_t limit = (std::numeric_limits<size_t>::max)();
    size_t out = 17;
    EXPECT_TRUE(CheckedBytes2(0, limit, out));
    EXPECT_EQ(out, 0u);
    EXPECT_TRUE(CheckedBytes2(limit, 0, out));
    EXPECT_EQ(out, 0u);
    EXPECT_TRUE(CheckedBytes2(1, limit, out));
    EXPECT_EQ(out, limit);
    EXPECT_TRUE(CheckedBytes2(limit, 1, out));
    EXPECT_EQ(out, limit);
    EXPECT_FALSE(CheckedBytes2(limit, 2, out));
    EXPECT_EQ(out, limit);  // failure does not publish a wrapped result
    const size_t highBit = size_t(1) << (std::numeric_limits<size_t>::digits - 1);
    EXPECT_FALSE(CheckedBytes2(highBit, 2, out));  // 1 << 63 on x64
    EXPECT_TRUE(CheckedBytes2(limit / 2, 2, out));
    EXPECT_EQ(out, limit - 1);
}

TEST(LoadValidate, ThreeFactorSizesCheckBothMultiplications) {
    const size_t limit = (std::numeric_limits<size_t>::max)();
    size_t out = 17;
    EXPECT_FALSE(CheckedBytes3(limit, 2, 1, out));
    EXPECT_EQ(out, 17u);
    EXPECT_FALSE(CheckedBytes3(limit / 2 + 1, 1, 2, out));
    EXPECT_EQ(out, 17u);
    EXPECT_TRUE(CheckedBytes3(limit, 1, 1, out));
    EXPECT_EQ(out, limit);
    EXPECT_TRUE(CheckedBytes3(0, limit, limit, out));
    EXPECT_EQ(out, 0u);
    EXPECT_TRUE(CheckedBytes3(limit, 0, limit, out));
    EXPECT_EQ(out, 0u);
    EXPECT_TRUE(CheckedBytes3(limit, limit, 0, out));
    EXPECT_EQ(out, 0u);
}

TEST(LoadValidate, TransferSizesRetain32BitLimit) {
    const size_t nativeLimit = (std::numeric_limits<size_t>::max)();
    size_t out = 17;
    EXPECT_TRUE(CheckedTransferBytes2(0, nativeLimit, out));
    EXPECT_EQ(out, 0u);
    EXPECT_TRUE(CheckedTransferBytes2(1, UINT32_MAX, out));
    EXPECT_EQ(out, UINT32_MAX);
    EXPECT_TRUE(CheckedTransferBytes2(UINT32_MAX, 1, out));
    EXPECT_EQ(out, UINT32_MAX);
    EXPECT_TRUE(CheckedTransferBytes3(UINT32_MAX, 1, 1, out));
    EXPECT_EQ(out, UINT32_MAX);
    EXPECT_FALSE(CheckedTransferBytes2(UINT32_MAX, 2, out));
    EXPECT_EQ(out, UINT32_MAX);
    // 2^32: one byte beyond the transfer limit, also native overflow on x86.
    EXPECT_FALSE(CheckedTransferBytes2(65536, 65536, out));
    EXPECT_FALSE(CheckedTransferBytes3(65536, 65536, 1, out));
    EXPECT_FALSE(CheckedTransferBytes3(65536, 1, 65536, out));
    EXPECT_FALSE(CheckedTransferBytes2(nativeLimit, 2, out));
    EXPECT_TRUE(CheckedTransferBytes3(nativeLimit, nativeLimit, 0, out));
    EXPECT_EQ(out, 0u);
#if SIZE_MAX > UINT32_MAX
    const size_t beyondTransfer = size_t(UINT32_MAX) + 1;
    EXPECT_TRUE(CheckedBytes2(beyondTransfer, 1, out));
    EXPECT_EQ(out, beyondTransfer);
    EXPECT_FALSE(CheckedTransferBytes2(beyondTransfer, 1, out));
    EXPECT_FALSE(CheckedTransferBytes3(beyondTransfer, 1, 1, out));
    EXPECT_FALSE(CheckedTransferBytes2(size_t(1) << 63, 2, out));
#endif
}

TEST(LoadValidate, NormalContentByteSizesAreUnchanged) {
    size_t out = 0;
    EXPECT_TRUE(CheckedBytes2(1989, 16, out));  // shipped max VCount * 16
    EXPECT_EQ(out, 1989u * 16u);
    EXPECT_TRUE(CheckedTransferBytes2(1488, 64, out));
    EXPECT_EQ(out, 1488u * 64u);
    EXPECT_TRUE(CheckedBytes3(1989, 365, 6, out));
    EXPECT_EQ(out, 1989u * 365u * 6u);
    EXPECT_TRUE(CheckedTransferBytes3(1989, 365, 6, out));
    EXPECT_EQ(out, 1989u * 365u * 6u);
    EXPECT_TRUE(CheckedTransferBytes2(255, 20, out));
    EXPECT_EQ(out, 5100u);
    EXPECT_TRUE(CheckedBytes3(256, 256, 2, out));
    EXPECT_EQ(out, 131072u);
}

TEST(LoadValidate, VertexIndicesStayInRange) {
    EXPECT_TRUE(IsValidVertexIndex(0, 1989));
    EXPECT_TRUE(IsValidVertexIndex(1988, 1989));
    EXPECT_FALSE(IsValidVertexIndex(-1, 1989));
    EXPECT_FALSE(IsValidVertexIndex(1989, 1989));
    EXPECT_FALSE(IsValidVertexIndex(100000, 1989));

    EXPECT_TRUE(IsValidIndex(0, 32));
    EXPECT_TRUE(IsValidIndex(31, 32));
    EXPECT_FALSE(IsValidIndex(-1, 32));
    EXPECT_FALSE(IsValidIndex(32, 32));
}

TEST(LoadValidate, AnimationDurationIsPositiveAndChecked) {
    int duration = 0;
    EXPECT_TRUE(CheckedAnimationDuration(20, 10, duration));
    EXPECT_EQ(duration, 2000);
    EXPECT_TRUE(CheckedAnimationDuration(1, 10, duration));
    EXPECT_EQ(duration, 100);
    EXPECT_FALSE(CheckedAnimationDuration(0, 10, duration));
    EXPECT_FALSE(CheckedAnimationDuration(20, 0, duration));
    EXPECT_FALSE(CheckedAnimationDuration(20, -1, duration));
    EXPECT_FALSE(CheckedAnimationDuration(2, 3000, duration));
    EXPECT_FALSE(CheckedAnimationDuration((std::numeric_limits<int>::max)(), 1, duration));
}

TEST(LoadValidate, WeaponAnimationReferencesRespectModelCount) {
    EXPECT_TRUE(IsValidWeaponAnimationIndex(0, 3, true));
    EXPECT_TRUE(IsValidWeaponAnimationIndex(2, 3, false));
    EXPECT_TRUE(IsValidWeaponAnimationIndex(-1, 3, false));
    EXPECT_FALSE(IsValidWeaponAnimationIndex(-1, 3, true));
    EXPECT_FALSE(IsValidWeaponAnimationIndex(-2, 3, false));
    EXPECT_FALSE(IsValidWeaponAnimationIndex(3, 3, false));

    TWeapInfo weapon{};
    weapon.getAnim = 0;
    weapon.shtAnim = 1;
    weapon.putAnim = 2;
    weapon.rldAnim = -1;
    EXPECT_TRUE(AreWeaponAnimationReferencesValid(weapon, 3));

    weapon.Reload = 1;
    EXPECT_FALSE(AreWeaponAnimationReferencesValid(weapon, 3));
    weapon.rldAnim = 2;
    EXPECT_TRUE(AreWeaponAnimationReferencesValid(weapon, 3));
    weapon.modAnim = 3;
    EXPECT_FALSE(AreWeaponAnimationReferencesValid(weapon, 3));
}

TEST(LoadValidate, MorphFrameCalculationIsBoundedAndOverflowSafe) {
    EXPECT_EQ(CalculateMorphFrameFixed(1, 100, 1000), 0);
    EXPECT_EQ(CalculateMorphFrameFixed(10, -1, 1000), 0);
    EXPECT_EQ(CalculateMorphFrameFixed(10, 1000, 1000),
              CalculateMorphFrameFixed(10, 999, 1000));
    const int nearEnd = CalculateMorphFrameFixed(365, 24332, 24333);
    EXPECT_GE(nearEnd, 0);
    EXPECT_LT((nearEnd >> 8) + 1, 365);
    EXPECT_EQ(CalculateMorphFrameFixed((std::numeric_limits<int>::max)(),
                                       1000, 1000), 0);
    EXPECT_EQ(CalculateMorphFrameFixed(10, 100, 0), 0);
}

TEST(LoadValidate, MapReferencesRespectCountsAndSentinels) {
    EXPECT_TRUE(IsValidMapTextureIndex(0, 1));
    EXPECT_FALSE(IsValidMapTextureIndex(1, 1));
    EXPECT_TRUE(IsValidMapTextureIndex(0xFFFFu, 2));
    EXPECT_FALSE(IsValidMapTextureIndex(0xFFFFu, 1));

    EXPECT_TRUE(IsValidMapObjectIndex(0, 1));
    EXPECT_FALSE(IsValidMapObjectIndex(1, 1));
    EXPECT_TRUE(IsValidMapObjectIndex(254, 0));
    EXPECT_TRUE(IsValidMapObjectIndex(255, 0));

    EXPECT_TRUE(IsValidMapWaterIndex(0, 1));
    EXPECT_FALSE(IsValidMapWaterIndex(1, 1));
    EXPECT_FALSE(IsValidMapWaterIndex(255, 0));
}

TEST(LoadValidate, BmpWidthGuardsStackBuffer) {
    EXPECT_TRUE(IsValidBmpWidth(1));
    EXPECT_TRUE(IsValidBmpWidth(800));
    EXPECT_FALSE(IsValidBmpWidth(0));
    EXPECT_FALSE(IsValidBmpWidth(-40));
    EXPECT_FALSE(IsValidBmpWidth(801));
}

TEST(LoadValidate, PictureStoragePreservesSignedPixelIndexLimit) {
    size_t out = 17;
    const int limit = (std::numeric_limits<int>::max)();
    EXPECT_TRUE(CheckedPictureBytes(1, limit, out));
    EXPECT_EQ(out, size_t(limit) * sizeof(uint16_t));
    EXPECT_TRUE(CheckedPictureBytes(800, 600, out));
    EXPECT_EQ(out, 800u * 600u * 2u);
    EXPECT_FALSE(CheckedPictureBytes(32768, 65536, out));
    EXPECT_EQ(out, 800u * 600u * 2u);
    EXPECT_FALSE(CheckedPictureBytes(65535, 65535, out));
    EXPECT_FALSE(CheckedPictureBytes(0, 1, out));
    EXPECT_FALSE(CheckedPictureBytes(1, -1, out));
}

TEST(LoadValidate, WavLengthsAreSane) {
    EXPECT_TRUE(IsValidWavLength(0));
    EXPECT_TRUE(IsValidWavLength(44100 * 2));
    EXPECT_FALSE(IsValidWavLength(-1));
    EXPECT_FALSE(IsValidWavLength((16 << 20) + 1));
    // Odd lengths allocate a rounded-up sample count (no 1-byte overflow).
    EXPECT_EQ(WavAllocSamples(100), 50u);
    EXPECT_EQ(WavAllocSamples(101), 51u);
    EXPECT_EQ(WavAllocSamples(0), 0u);
}

TEST(LoadValidate, ScriptKeyMatchesAssignmentTokenOnly) {
    // Anything containing "file" used to be read as the model-file field:
    // a // comment, a path like models/modname/x.car, or the value of the
    // name key. The key is the token before '=', nothing else.
    EXPECT_TRUE(ScriptKeyIs("file = 'para.car'", "file"));
    EXPECT_TRUE(ScriptKeyIs(" file    = 'models/main_hunt/para.car'", "file"));
    EXPECT_TRUE(ScriptKeyIs("name = 'Parasaurolophus'", "name"));
    EXPECT_FALSE(ScriptKeyIs("name = 'Chilantaisaurus'", "ai"));
    EXPECT_TRUE(ScriptKeyIs("\tpicc = 'ammo/chamb1.tga'", "picc"));
    EXPECT_TRUE(ScriptKeyIs("bModel = 'Weapons/proj/b_pist.car'", "bModel"));

    EXPECT_FALSE(ScriptKeyIs("filename = 'para.car'", "file"));
    EXPECT_FALSE(ScriptKeyIs("junk file = 'para.car'", "file"));
    EXPECT_FALSE(ScriptKeyIs("// file = 'disabled.car'", "file"));
    EXPECT_FALSE(ScriptKeyIs("name = 'Profile'", "file"));
    EXPECT_FALSE(ScriptKeyIs("file = 'models/modname/para.car'", "name"));
    EXPECT_FALSE(ScriptKeyIs("overwrite area3 {", "file"));  // no assignment
    EXPECT_FALSE(ScriptKeyIs("file = 'para.car'", "filename"));
    EXPECT_FALSE(ScriptKeyIs(nullptr, "file"));
    EXPECT_FALSE(ScriptKeyIs("file = 'x'", nullptr));
}

TEST(LoadValidate, QuotedValueKeepsTrailingComments) {
    char dst[48];
    // Callers pass the text after '='; the leading space, trailing spaces,
    // semicolons and // comments all sit outside the value.
    EXPECT_TRUE(CopyQuotedValue(dst, sizeof(dst), " 'para.car'"));
    EXPECT_STREQ(dst, "para.car");
    EXPECT_TRUE(CopyQuotedValue(dst, sizeof(dst), " 'jager.CAR'       // 3D model file"));
    EXPECT_STREQ(dst, "jager.CAR");
    EXPECT_TRUE(CopyQuotedValue(dst, sizeof(dst), " 'para.car'   "));
    EXPECT_STREQ(dst, "para.car");
    EXPECT_TRUE(CopyQuotedValue(dst, sizeof(dst), " 'para.car';\n"));
    EXPECT_STREQ(dst, "para.car");

    // Values the format cannot represent still fail loudly.
    EXPECT_FALSE(CopyQuotedValue(dst, sizeof(dst), " 'para.car"));
    EXPECT_FALSE(CopyQuotedValue(dst, sizeof(dst), " '"));
    EXPECT_FALSE(CopyQuotedValue(dst, sizeof(dst), "para.car"));
    EXPECT_FALSE(CopyQuotedValue(dst, sizeof(dst), ""));
    EXPECT_FALSE(CopyQuotedValue(dst, sizeof(dst), nullptr));
    EXPECT_FALSE(CopyQuotedValue(nullptr, sizeof(dst), " 'x'"));
    EXPECT_FALSE(CopyQuotedValue(dst, 0, " 'x'"));

    // '' is an empty value, not a malformed one (the legacy parser accepted
    // it and the field stayed empty).
    EXPECT_TRUE(CopyQuotedValue(dst, sizeof(dst), " ''"));
    EXPECT_STREQ(dst, "");

    // This local 48-byte destination accepts 47 characters, but not 48.
    char quoted[64];
    quoted[0] = '\'';
    memset(quoted + 1, 'x', 47);
    quoted[48] = '\'';
    quoted[49] = '\0';
    EXPECT_TRUE(CopyQuotedValue(dst, sizeof(dst), quoted));
    EXPECT_EQ(strlen(dst), 47u);

    memset(quoted + 1, 'x', 48);
    quoted[49] = '\'';
    quoted[50] = '\0';
    EXPECT_FALSE(CopyQuotedValue(dst, sizeof(dst), quoted));
}

TEST(LoadValidate, FindQuotedValueExposesSpan) {
    const char* value = nullptr;
    size_t length = 0;
    ASSERT_TRUE(FindQuotedValue(" 'bag1.car'\r\n", &value, &length));
    EXPECT_EQ(length, 8u);
    EXPECT_EQ(strncmp(value, "bag1.car", 8), 0);
    EXPECT_FALSE(FindQuotedValue("'unclosed", &value, &length));
    EXPECT_FALSE(FindQuotedValue(nullptr, &value, &length));
}

TEST(LoadValidate, ReportedModelPathLineNowParses) {
    // Regression for "Script loading error: Characters file missing, too
    // long, or malformed.": this path contains "name", and the old strstr()
    // key match plus the in-place quote strip made the file branch eat a
    // quote the name branch had already consumed.
    char line[] = " file = 'models/modname/para.car'\n";
    char dst[48];
    EXPECT_FALSE(ScriptKeyIs(line, "name"));
    ASSERT_TRUE(ScriptKeyIs(line, "file"));
    ASSERT_TRUE(CopyQuotedValue(dst, sizeof(dst), strchr(line, '=') + 1));
    EXPECT_STREQ(dst, "models/modname/para.car");
}

TEST(LoadValidate, CopyCappedRejectsOverflow) {
    char dst[48];
    EXPECT_TRUE(CopyCapped(dst, sizeof(dst), "jager.car"));
    EXPECT_STREQ(dst, "jager.car");
    char exact[4];
    EXPECT_TRUE(CopyCapped(exact, sizeof(exact), "abc"));
    EXPECT_FALSE(CopyCapped(exact, sizeof(exact), "abcd"));  // needs NUL room
    EXPECT_FALSE(CopyCapped(nullptr, 48, "x"));
    EXPECT_FALSE(CopyCapped(dst, 0, "x"));
    EXPECT_FALSE(CopyCapped(dst, sizeof(dst), nullptr));
}

TEST(LoadValidate, ExternalSlotSixAliasDetectsVanillaProjectName) {
    EXPECT_TRUE(ProjectBasenameIsExternal("huntdat/areas/external"));
    EXPECT_TRUE(ProjectBasenameIsExternal("huntdat\\areas\\external"));
    EXPECT_TRUE(ProjectBasenameIsExternal("huntdat/areas/External"));  // case-insensitive paths
    EXPECT_TRUE(ProjectBasenameIsExternal("external"));               // bare basename
    EXPECT_FALSE(ProjectBasenameIsExternal("huntdat/areas/area6"));
    EXPECT_FALSE(ProjectBasenameIsExternal("huntdat/areas/myexternal"));
    EXPECT_FALSE(ProjectBasenameIsExternal("huntdat/areas/trophy"));
    EXPECT_FALSE(ProjectBasenameIsExternal(nullptr));
}

TEST(LoadValidate, ExternalSlotSixAliasRewritesToLogicalAreaName) {
    // The engine's script area filtering reads the fixed offset that holds the
    // area digit ("huntdat/areas/areaN" -> index 18). After the rewrite the
    // legacy offset logic must see exactly "area6" there.
    char forward[] = "huntdat/areas/external";
    ASSERT_TRUE(RewriteExternalProjectAlias(forward, sizeof(forward)));
    EXPECT_STREQ(forward, "huntdat/areas/area6");
    EXPECT_EQ(forward[18], '6');  // legacy area-filter offset
    EXPECT_EQ(forward[19], '\0'); // no area10 second digit

    char back[] = "huntdat\\areas\\external";
    ASSERT_TRUE(RewriteExternalProjectAlias(back, sizeof(back)));
    EXPECT_STREQ(back, "huntdat\\areas\\area6");

    // Non-external projects are untouched (returns false, buffer unchanged).
    char area1[] = "huntdat/areas/area1";
    EXPECT_FALSE(RewriteExternalProjectAlias(area1, sizeof(area1)));
    EXPECT_STREQ(area1, "huntdat/areas/area1");
    char trophy[] = "huntdat/areas/trophy";
    EXPECT_FALSE(RewriteExternalProjectAlias(trophy, sizeof(trophy)));
    EXPECT_STREQ(trophy, "huntdat/areas/trophy");
}

TEST(LoadValidate, ExternalSlotSixAliasRespectsBufferCap) {
    // The rewrite only shortens the basename, so it must succeed in any buffer
    // that already held the full path.
    char tight[] = "x/external";
    EXPECT_TRUE(RewriteExternalProjectAlias(tight, sizeof(tight)));
    EXPECT_STREQ(tight, "x/area6");
    // Degenerate caps are rejected without touching the buffer.
    char path[] = "huntdat/areas/external";
    EXPECT_FALSE(RewriteExternalProjectAlias(path, 0));
    EXPECT_FALSE(RewriteExternalProjectAlias(nullptr, sizeof(path)));
}

TEST(LoadValidate, ReadExactDetectsTruncation) {
    const char* path = "load_validate_probe.bin";
    const unsigned char data[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    FILE* f = fopen(path, "wb");
    ASSERT_NE(f, nullptr);
    ASSERT_EQ(fwrite(data, 1, sizeof(data), f), sizeof(data));
    fclose(f);

    auto h = Platform::OpenFile(path, Platform::FileMode::Read);
    ASSERT_NE(h, Platform::InvalidFile);
    unsigned char buf[16];
    EXPECT_TRUE(ReadExact(h, buf, 8));
    EXPECT_EQ(memcmp(buf, data, 8), 0);
    Platform::SeekFile(h, 0, Platform::SeekOrigin::Begin);
    EXPECT_FALSE(ReadExact(h, buf, 9));  // one byte past EOF
    EXPECT_TRUE(ReadExact(h, buf, 0));   // zero-length reads succeed
    Platform::CloseFile(h);
    remove(path);
}

}  // namespace
