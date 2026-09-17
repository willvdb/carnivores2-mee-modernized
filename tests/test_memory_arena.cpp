// test_memory_arena.cpp
// Unit tests for MemoryArena (Hunt/Memory.h) — the per-level bump allocator.
//
// The arena is header-implemented except for PrintLog (used only on the
// _DEBUG overflow path), so this TU provides a no-op PrintLog stub instead
// of linking engine code. Arena sizes here are small (KiB range); the
// 256 MiB production size is exercised by game runs, not unit tests.

#include <gtest/gtest.h>

#include <cstdint>
#include <string>

// Test-double captures diagnostics without engine linkage.
static std::string arenaLog;
void PrintLog(const char* msg) { arenaLog += msg; }

#include "Memory.h"

namespace {

TEST(MemoryArena, FirstAllocationIsAlignedAndZeroUsable) {
    MemoryArena arena(4096, "test");
    void* p = arena.Allocate(64);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 16, 0u);
    EXPECT_EQ(arena.GetAllocCount(), 1u);
    EXPECT_GE(arena.GetUsed(), 64u);
}

TEST(MemoryArena, SequentialAllocationsDoNotOverlap) {
    MemoryArena arena(4096, "test");
    auto* a = static_cast<uint8_t*>(arena.Allocate(100));
    auto* b = static_cast<uint8_t*>(arena.Allocate(200));
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_GE(b, a + 100);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(b) % 16, 0u);
    EXPECT_EQ(arena.GetAllocCount(), 2u);
}

TEST(MemoryArena, ExactFitSucceeds) {
    // 16 is the default alignment; a 64-byte arena fits exactly 4x16.
    MemoryArena arena(64, "test");
    for (int i = 0; i < 4; i++)
        EXPECT_NE(arena.Allocate(16), nullptr) << "allocation " << i;
    EXPECT_EQ(arena.GetRemaining(), 0u);
}

TEST(MemoryArena, ZeroByteAllocationsHaveDistinctOwnership) {
    MemoryArena arena(64, "test");
    void* first = arena.Allocate(0);
    void* second = arena.Allocate(0);
    ASSERT_NE(first, nullptr);
    ASSERT_NE(second, nullptr);
    EXPECT_NE(first, second);
    EXPECT_EQ(arena.GetAllocCount(), 2u);
    EXPECT_GT(arena.GetUsed(), 1u);
}

TEST(MemoryArena, OverflowReturnsNullWithoutGrowing) {
    MemoryArena arena(64, "test");
    ASSERT_NE(arena.Allocate(64), nullptr);
    const size_t usedBefore = arena.GetUsed();
    EXPECT_EQ(arena.Allocate(16), nullptr);
    EXPECT_EQ(arena.GetUsed(), usedBefore);
    EXPECT_EQ(arena.GetAllocCount(), 1u);
}

TEST(MemoryArena, SingleOversizedRequestReturnsNull) {
    MemoryArena arena(64, "test");
    EXPECT_EQ(arena.Allocate(65), nullptr);
    EXPECT_EQ(arena.Allocate(1u << 31), nullptr);  // wrap-safe capacity check
    EXPECT_EQ(arena.GetUsed(), 0u);
    EXPECT_EQ(arena.GetAllocCount(), 0u);
}

TEST(MemoryArena, ResetReclaimsEverything) {
    MemoryArena arena(256, "test");
    ASSERT_NE(arena.Allocate(200), nullptr);
    arena.Reset();
    EXPECT_EQ(arena.GetUsed(), 0u);
    EXPECT_EQ(arena.GetAllocCount(), 0u);
    EXPECT_NE(arena.Allocate(200), nullptr);
}

TEST(MemoryArena, ContainsMatchesReservedRange) {
    MemoryArena arena(1024, "test");
    auto* p = static_cast<uint8_t*>(arena.Allocate(32));
    ASSERT_NE(p, nullptr);
    EXPECT_TRUE(arena.Contains(p));
    EXPECT_TRUE(arena.Contains(p + 31));
    EXPECT_FALSE(arena.Contains(nullptr));
    int stackValue = 0;
    EXPECT_FALSE(arena.Contains(&stackValue));
}

TEST(MemoryArena, PeakTracksHighWaterAcrossAllocations) {
    MemoryArena arena(512, "test");
    ASSERT_NE(arena.Allocate(100), nullptr);
    const size_t peakAfterFirst = arena.GetPeakUsage();
    EXPECT_GT(peakAfterFirst, 0u);
    ASSERT_NE(arena.Allocate(100), nullptr);
    EXPECT_GE(arena.GetPeakUsage(), peakAfterFirst);
    EXPECT_LE(arena.GetPeakUsage(), arena.GetCapacity());
}

TEST(MemoryArena, InvalidAlignmentReturnsNull) {
    MemoryArena arena(1024, "test");
    EXPECT_EQ(arena.Allocate(16, 0), nullptr);    // divide-by-zero guard
    EXPECT_EQ(arena.Allocate(16, 3), nullptr);    // non-power-of-two
    EXPECT_EQ(arena.Allocate(16, 24), nullptr);   // non-power-of-two
    EXPECT_EQ(arena.Allocate(16, 1u << 31), nullptr);  // padding wrap guard
    EXPECT_EQ(arena.GetUsed(), 0u);
    EXPECT_EQ(arena.GetAllocCount(), 0u);
    // Valid non-default power-of-two alignments still work.
    void* p = arena.Allocate(16, 32);
    ASSERT_NE(p, nullptr);
    EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 32, 0u);
}

TEST(MemoryArena, SessionPeakSurvivesReset) {
    MemoryArena arena(512, "test");
    ASSERT_NE(arena.Allocate(200), nullptr);
    const size_t sessionPeak = arena.GetSessionPeak();
    EXPECT_GT(sessionPeak, 0u);
    arena.Reset();
    EXPECT_EQ(arena.GetUsed(), 0u);
    EXPECT_EQ(arena.GetPeakUsage(), 0u);
    EXPECT_EQ(arena.GetSessionPeak(), sessionPeak);
    (void)arena.Allocate(50);
    EXPECT_EQ(arena.GetSessionPeak(), sessionPeak);  // smaller reuse keeps max
    arena.Reset();
    (void)arena.Allocate(300);
    EXPECT_GT(arena.GetSessionPeak(), sessionPeak);  // larger level raises max
}

TEST(MemoryArena, GenerationAdvancesOnReset) {
    MemoryArena arena(256, "test");
    EXPECT_EQ(arena.GetGeneration(), 0u);
    (void)arena.Allocate(64);
    EXPECT_EQ(arena.GetGeneration(), 0u);  // allocs do not advance it
    arena.Reset();
    EXPECT_EQ(arena.GetGeneration(), 1u);
    arena.Reset();
    EXPECT_EQ(arena.GetGeneration(), 2u);
}

TEST(MemoryArena, ArenaResetClearsOnlyArenaBackedMatchingTags) {
    EXPECT_TRUE(IsReclaimedByArenaReset(MemoryTag::Level,
                                        AllocBackend::Arena,
                                        MemoryTag::Level));
    EXPECT_FALSE(IsReclaimedByArenaReset(MemoryTag::Level,
                                         AllocBackend::Heap,
                                         MemoryTag::Level));
    EXPECT_FALSE(IsReclaimedByArenaReset(MemoryTag::Global,
                                         AllocBackend::Arena,
                                         MemoryTag::Level));
}

TEST(MemoryArena, RepeatedLevelLifecycleDoesNotAccumulate) {
    MemoryArena arena(4096, "test");
    size_t expectedUsed = 0;
    for (int restart = 0; restart < 100; restart++) {
        ASSERT_NE(arena.Allocate(111), nullptr);
        ASSERT_NE(arena.Allocate(257), nullptr);
        ASSERT_NE(arena.Allocate(19), nullptr);
        if (restart == 0)
            expectedUsed = arena.GetUsed();
        EXPECT_EQ(arena.GetUsed(), expectedUsed) << "restart " << restart;
        EXPECT_EQ(arena.GetAllocCount(), 3u);
        arena.Reset();
        EXPECT_EQ(arena.GetUsed(), 0u);
        EXPECT_EQ(arena.GetAllocCount(), 0u);
    }
    EXPECT_EQ(arena.GetSessionPeak(), expectedUsed);
    EXPECT_EQ(arena.GetGeneration(), 100u);
}

TEST(MemoryArena, NativeLimitRequestDoesNotWrapIntoSmallAllocation) {
    MemoryArena arena(64, "test");
    EXPECT_EQ(arena.Allocate((std::numeric_limits<size_t>::max)()), nullptr);
#if SIZE_MAX > UINT32_MAX
    EXPECT_EQ(arena.Allocate(size_t(UINT32_MAX) + 1), nullptr);
    EXPECT_EQ(arena.Allocate(size_t(1) << 63), nullptr);
#endif
    EXPECT_EQ(arena.GetUsed(), 0u);
    EXPECT_EQ(arena.GetAllocCount(), 0u);
}

#if SIZE_MAX > UINT32_MAX
TEST(MemoryArena, CapacityDiagnosticsDoNotTruncateTo32Bits) {
    // An impossible reservation fails without consuming physical memory, but
    // its requested capacity is still available to the diagnostic path.
    const size_t capacity = (std::numeric_limits<size_t>::max)();
    MemoryArena arena(capacity, "wide");
    arenaLog.clear();
    arena.LogStats();
    EXPECT_NE(arenaLog.find(std::to_string(capacity / 1024) + " KB"), std::string::npos);
}
#endif

TEST(MemoryArena, LogStatsDoesNotCrash) {
    MemoryArena arena(512, "test");
    (void)arena.Allocate(64);
    arena.LogStats(" after load");
    SUCCEED();
}

}  // namespace
