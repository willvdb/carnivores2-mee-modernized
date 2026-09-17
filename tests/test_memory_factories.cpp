// Intercept the engine boundary to verify sizes without multi-GiB allocations.
// The full engine smoke checks exercise the production heap/arena dispatch.
#include <gtest/gtest.h>
#include "Memory.h"

HANDLE Heap = GetProcessHeap();

namespace {
struct AllocationStopped {};
struct SizeOverflow {};
size_t requestedBytes = 0;
DWORD requestedFlags = 0;
MemoryTag requestedTag = MemoryTag::Global;
int allocationCalls = 0;
int freeCalls = 0;
bool stopAllocation = false;
const char* trackedFile = nullptr;
int trackedLine = 0;

LPVOID CaptureAllocation(DWORD flags, size_t bytes, MemoryTag tag) {
    requestedBytes = bytes;
    requestedFlags = flags;
    requestedTag = tag;
    ++allocationCalls;
    if (stopAllocation) throw AllocationStopped{};
    return HeapAlloc(Heap, flags | HEAP_ZERO_MEMORY, bytes);
}

class MemoryFactories : public testing::Test {
    void SetUp() override {
        requestedBytes = 0;
        allocationCalls = freeCalls = 0;
        stopAllocation = false;
        trackedFile = nullptr;
        trackedLine = 0;
    }
};
}

// Parentheses bypass the MEM_DEBUG call-site macro when defining overloads.
LPVOID (_HeapAlloc)(HANDLE, DWORD flags, size_t bytes) {
    return CaptureAllocation(flags, bytes, MemoryTag::Global);
}
LPVOID (_HeapAlloc)(HANDLE, DWORD flags, size_t bytes, MemoryTag tag) {
    return CaptureAllocation(flags, bytes, tag);
}
#ifdef MEM_DEBUG
LPVOID _HeapAllocImpl(HANDLE, DWORD flags, size_t bytes, MemoryTag tag,
                     const char* file, int line) {
    trackedFile = file;
    trackedLine = line;
    return CaptureAllocation(flags, bytes, tag);
}
#endif
BOOL _HeapFree(HANDLE heap, DWORD flags, LPVOID ptr) {
    ++freeCalls;
    return HeapFree(heap, flags, ptr);
}
[[noreturn]] void DoHalt(char*) { throw SizeOverflow{}; }

namespace {
TEST_F(MemoryFactories, ArrayKeepsNativeSizeAndHeapDeleter) {
    {
        auto array = make_heap_array<uint64_t>(37);
        ASSERT_NE(array, nullptr);
        EXPECT_EQ(requestedBytes, 37 * sizeof(uint64_t));
        EXPECT_EQ(requestedTag, MemoryTag::Global);
        EXPECT_EQ(array[36], 0u);
        array[36] = 42;
    }
    EXPECT_EQ(allocationCalls, 1);
    EXPECT_EQ(freeCalls, 1);
}

TEST_F(MemoryFactories, ArrayOverflowStopsBeforeAllocator) {
    const size_t count = (std::numeric_limits<size_t>::max)() / sizeof(uint64_t) + 1;
    EXPECT_THROW(make_heap_array<uint64_t>(count), SizeOverflow);
    EXPECT_EQ(allocationCalls, 0);
}

TEST_F(MemoryFactories, LargestArrayProductReachesAllocatorUnchanged) {
    stopAllocation = true;
    const size_t count = (std::numeric_limits<size_t>::max)() / sizeof(uint64_t);
    EXPECT_THROW(make_heap_array<uint64_t>(count), AllocationStopped);
    EXPECT_EQ(requestedBytes, count * sizeof(uint64_t));
    EXPECT_EQ(allocationCalls, 1);
}

TEST_F(MemoryFactories, ZeroLengthArraysHaveSeparateOwners) {
    auto a = make_heap_array<int>(0);
    auto b = make_heap_array<int>(0);
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    EXPECT_NE(a.get(), b.get());
    EXPECT_EQ(requestedBytes, 0u);
}

TEST_F(MemoryFactories, ObjectConstructionAndDestructionKeepHeapPairing) {
    struct Object {
        int* destroyed;
        int value;
        Object(int* d, int v) : destroyed(d), value(v) {}
        ~Object() { ++*destroyed; }
    };
    int destroyed = 0;
    {
        auto object = make_heap_object<Object>(&destroyed, 42);
        EXPECT_EQ(object->value, 42);
        EXPECT_EQ(requestedBytes, sizeof(Object));
    }
    EXPECT_EQ(destroyed, 1);
    EXPECT_EQ(freeCalls, 1);
}

TEST_F(MemoryFactories, ThrowingConstructorReleasesStorage) {
    struct Object { Object() { throw 42; } };
    EXPECT_THROW(make_heap_object<Object>(), int);
    EXPECT_EQ(allocationCalls, 1);
    EXPECT_EQ(freeCalls, 1);
}

TEST_F(MemoryFactories, ForwardingKeepsNativeBytesAndDWORDFlags) {
    stopAllocation = true;
    const size_t bytes = (std::numeric_limits<size_t>::max)();
    EXPECT_THROW((void)_HeapAlloc(Heap, HEAP_NO_SERIALIZE, bytes), AllocationStopped);
    EXPECT_EQ(requestedBytes, bytes);
    EXPECT_EQ(requestedFlags, HEAP_NO_SERIALIZE);
    EXPECT_EQ(requestedTag, MemoryTag::Global);
    EXPECT_THROW((void)_HeapAlloc(Heap, 0, bytes, MemoryTag::Level), AllocationStopped);
    EXPECT_EQ(requestedBytes, bytes);
    EXPECT_EQ(requestedTag, MemoryTag::Level);
#ifdef MEM_DEBUG
    EXPECT_NE(trackedFile, nullptr);
    EXPECT_GT(trackedLine, 0);
    EXPECT_THROW((void)_AllocTrack(bytes, MemoryTag::Audio), AllocationStopped);
    EXPECT_EQ(requestedBytes, bytes);
    EXPECT_EQ(requestedTag, MemoryTag::Audio);
#endif
}

#if SIZE_MAX > UINT32_MAX
TEST_F(MemoryFactories, ArrayAboveTransferLimitReachesAllocator) {
    stopAllocation = true;
    const size_t count = size_t(UINT32_MAX) + 1;
    EXPECT_THROW(make_heap_array<uint8_t>(count), AllocationStopped);
    EXPECT_EQ(requestedBytes, count);
}
#endif
}
