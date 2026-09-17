#ifndef HUNT_MEMORY_H
#define HUNT_MEMORY_H

// Memory.h
// ============================================================================
// Phase 5A: Memory management system migration types.
//
// Ported from C1 (E:/Munka/Programming/C++/Carnivores1/Hunt/Memory.h) with
// the C2-specific additions documented in
// docs/design/memory-system-migration.md layered on top:
//
//   * LEVEL_ARENA_SIZE constant (256 MiB; C1 hardcodes 128 MiB at the
//     construction site because its world is half C2 ME's scale).
//   * static_assert checks confirming the smart-pointer types are the
//     same size as raw pointers (so adopting them in TModel / TObject /
//     TPicture / TAni / TCharacterInfo in later phases doesn't bloat the
//     structures or shift the MObjects[256] global).
//
// The bulk of the content below is C1 verbatim (C1's comment headers and
// naming conventions are preserved). The C2-specific items are clearly
// marked.
// ============================================================================

#include <cstdint>
#include <cstddef>
#include "Platform/Memory.h"
#include <cstdio>
#include <cstring>
#include <memory>
#include <limits>
#include <new>
#include <type_traits>
#include <utility>


// ----------------------------------------------------------------------------
// Configuration (C2 ME addition — not present in C1)
// ----------------------------------------------------------------------------

// Per-level arena size. C2 ME's world is 4x larger than C1's (ctHScale=64
// vs 32, ctMapSize=1024 vs 512), so C1's 128 MiB default is not always
// enough. The 2026-09-07 shipped-area sweep peaked at 7.5% arena use;
// process address-space use is tracked separately. Tunable here. A future
// Phase 5C pass can expose a command-line override
// (smod=arena=N) and emit peak-usage stats to carnivor.log.
inline constexpr size_t LEVEL_ARENA_SIZE = 256 * 1024 * 1024;  // 256 MiB


// ----------------------------------------------------------------------------
// Memory tag
// ----------------------------------------------------------------------------

// Tags that classify allocations by subsystem. Used by _HeapAlloc to dispatch
// between arena (Level) and heap (everything else), and (in Phase 5F) by
// the MEM_DEBUG leak detector to attribute leaks to subsystems.
//
// Only Global and Level are exercised in current C2 ME code. The other tags
// are reserved for future subsystems and cost nothing to keep — the type
// system stays symmetric with C1's Memory.h.
enum class MemoryTag {
    Global,    // Lives for the entire session (SunModel, SFX, ChInfo[])
    Level,     // Lives for the current hunt (per-level textures, MObjects)
    Graphics,  // Reserved for future per-renderer allocations
    Audio,     // Reserved for future per-audio allocations
    AI,        // Reserved for future per-AI allocations
    Physics    // Reserved for future per-physics allocations
};

// Actual backing store is independent of the requested lifetime tag when a
// Level allocation falls back to the heap before the arena exists.
enum class AllocBackend { Heap, Arena };

inline bool IsReclaimedByArenaReset(MemoryTag allocationTag,
                                    AllocBackend backend,
                                    MemoryTag resetTag)
{
    return allocationTag == resetTag && backend == AllocBackend::Arena;
}


// ----------------------------------------------------------------------------
// Forward declarations (match C1)
// ----------------------------------------------------------------------------

extern Platform::HeapHandle Heap;
[[nodiscard]] void* _HeapAlloc(Platform::HeapHandle hHeap, std::uint32_t dwFlags, size_t bytes);
[[nodiscard]] void* _HeapAlloc(Platform::HeapHandle hHeap, std::uint32_t dwFlags, size_t bytes, MemoryTag tag);
[[nodiscard]] std::int32_t   _HeapFree(Platform::HeapHandle hHeap, std::uint32_t dwFlags, void* lpMem);
[[noreturn]] void   DoHalt(const char* msg);
void   PrintLog(const char* msg);


// ----------------------------------------------------------------------------
// Deleter + smart pointer aliases (match C1)
// ----------------------------------------------------------------------------

// Stateless function-object deleter that:
//   1. Calls T's destructor if T is destructible (skipped for trivially
//      destructible types by the compiler; the if constexpr guard keeps
//      this safe for types that opt out of destructibility).
//   2. Frees the underlying memory via _HeapFree, which silently ignores
//      arena-owned pointers (LevelArena->Contains() check in _HeapFree).
//
// The struct has no members, so std::unique_ptr applies the empty base
// optimization and ends up the same size as a raw pointer — verified by
// the static_assert at the bottom of this file.
template<typename T>
struct HeapDeleter {
    void operator()(T *ptr) const
    {
        if (!ptr)
            return;
        if constexpr (std::is_destructible<T>::value)
            ptr->~T();
        (void)_HeapFree(Heap, 0, ptr);
    }
};

// Owns a heap-allocated array. std::remove_extent<T>::type strips the
// array extent so HeapDeleter is templated on the element type (so the
// is_destructible check works on WORD/int/etc., not on the array type).
//
// HeapDeleter only calls the destructor for the first element of an array
// (it does not iterate). Therefore unique_heap_ptr<T[]> requires the
// element type to be trivially destructible. This static_assert catches
// non-trivial element types at compile time instead of silently
// misbehaving at run time.
template<typename T>
struct unique_heap_ptr_deleter : HeapDeleter<typename std::remove_extent<T>::type> {
    static_assert(std::is_trivially_destructible_v<typename std::remove_extent<T>::type>,
                  "unique_heap_ptr<T[]> requires trivially destructible element type "
                  "(HeapDeleter only destroys element 0)");
};

template<typename T>
using unique_heap_ptr = std::unique_ptr<T, unique_heap_ptr_deleter<T>>;

// Owns a single object allocated with _HeapAlloc.
template<typename T>
using unique_obj_ptr = std::unique_ptr<T, HeapDeleter<T>>;

// Allocator-matched factories for persistent objects and trivial arrays.
// These deliberately allocate through _HeapAlloc; constructing either smart
// pointer from CRT new/new[] would later free it through the private game heap.
template<typename T, typename... Args>
unique_obj_ptr<T> make_heap_object(Args&&... args)
{
    void* storage = _HeapAlloc(Heap, 0, sizeof(T), MemoryTag::Global);
    try {
        return unique_obj_ptr<T>(new(storage) T(std::forward<Args>(args)...));
    } catch (...) {
        (void)_HeapFree(Heap, 0, storage);
        throw;
    }
}

template<typename T>
unique_heap_ptr<T[]> make_heap_array(size_t count)
{
    static_assert(std::is_trivially_destructible_v<T>,
                  "make_heap_array requires trivially destructible elements");
    if (count > (std::numeric_limits<size_t>::max)() / sizeof(T))
        DoHalt("Heap array allocation size overflow.");
    return unique_heap_ptr<T[]>(static_cast<T*>(
        _HeapAlloc(Heap, 0, count * sizeof(T), MemoryTag::Global)));
}


// ----------------------------------------------------------------------------
// Per-level arena (bump allocator backed by VirtualAlloc) — match C1
// ----------------------------------------------------------------------------

class MemoryArena {
public:
    MemoryArena(size_t size, const char* debugName = nullptr)
        : m_Size(size), m_Offset(0), m_DebugName(debugName)
    {
        m_Base = static_cast<uint8_t*>(
            Platform::AllocatePages(m_Size));
    }

    ~MemoryArena() {
        if (m_Base) {
            Platform::FreePages(m_Base, m_Size);
        }
    }

    // Non-copyable, non-movable: the arena owns a VirtualAlloc block that
    // must be released exactly once.
    MemoryArena(const MemoryArena&) = delete;
    MemoryArena& operator=(const MemoryArena&) = delete;

    [[nodiscard]] void* Allocate(size_t size, size_t alignment = 16) {
        if (!m_Base) return nullptr;

        // HeapAlloc permits zero-byte requests and returns independently
        // ownable pointers. Reserve one arena byte too: returning the same
        // address for repeated zero-byte allocations would alias owners and
        // overwrite the pointer-keyed MEM_DEBUG record.
        const size_t allocationSize = size == 0 ? 1 : size;

        // Harden the public alignment parameter. All current callers use
        // the default 16; anything else must be an explicit power of two.
        // alignment==0 would divide by zero below, and a huge alignment
        // could wrap m_Offset + padding before the capacity check runs.
        if (alignment == 0 || (alignment & (alignment - 1)) != 0)
            return nullptr;

        // Padding-based alignment: aligns the actual returned address
        // (not just the offset), so this is correct even if m_Base were
        // not naturally aligned to `alignment`. VirtualAlloc returns
        // 64-KiB-aligned memory so alignment=16 is always satisfied on
        // the first allocation; the general formula is what matters for
        // correctness on subsequent allocations.
        size_t padding = (alignment - (reinterpret_cast<uintptr_t>(m_Base + m_Offset) % alignment)) % alignment;

        // Wrap-safe capacity check: size alone must fit within the arena,
        // and the true remaining space must be sufficient. Computing
        // size > m_Size - (m_Offset + padding) avoids the 32-bit integer
        // overflow that would let a near-4-GiB size pass the check.
        // The intermediate guards prove `used <= m_Size` before the
        // subtraction, so `m_Size - used` cannot underflow even for
        // adversarial (size, alignment) pairs.
        if (allocationSize > m_Size) return nullptr;
        if (padding > m_Size) return nullptr;
        size_t used = m_Offset + padding;
        if (used < m_Offset) return nullptr;  // defensive wrap guard
        if (used > m_Size) return nullptr;
        if (allocationSize > m_Size - used) {
#ifdef _DEBUG
            char buf[256];
            sprintf(buf, "Arena '%s' overflow: need %zu, free %zu (used %zu / %zu)\n",
                    m_DebugName ? m_DebugName : "?",
                    allocationSize, m_Size - m_Offset,
                    m_Offset, m_Size);
            PrintLog(buf);
#endif
            return nullptr;
        }

        void* ptr = m_Base + m_Offset + padding;
        m_Offset += padding + allocationSize;
        m_AllocCount++;
        if (m_Offset > m_PeakUsage) m_PeakUsage = m_Offset;
        if (m_Offset > m_SessionPeak) m_SessionPeak = m_Offset;
        return ptr;
    }

    // Bulk-reclaim everything. Also advances the allocation generation so
    // the leak tracker can tell which arena epoch a block belongs to.
    void Reset() {
        m_Offset = 0;
        m_AllocCount = 0;
        m_PeakUsage = 0;  // per-level peak, not session max
        // m_SessionPeak intentionally survives Reset: it is the telemetry
        // for restart-accumulation and mod headroom (the arena size itself
        // stays fixed; see LEVEL_ARENA_SIZE).
        m_Generation++;
    }

    unsigned GetGeneration() const { return m_Generation; }

    size_t GetUsed() const      { return m_Offset; }
    size_t GetCapacity() const   { return m_Size; }
    size_t GetRemaining() const  { return m_Size - m_Offset; }
    float  GetUtilization() const { return static_cast<float>(m_Offset) / static_cast<float>(m_Size); }
    size_t GetAllocCount() const { return m_AllocCount; }
    size_t GetPeakUsage() const  { return m_PeakUsage; }
    size_t GetSessionPeak() const { return m_SessionPeak; }

    void LogStats(const char* context = nullptr) const {
        char buf[384];
        sprintf(buf, "Arena '%s'%s: %zu KB used / %zu KB (%.1f%%), %zu allocs, peak %zu KB, session peak %zu KB\n",
                m_DebugName ? m_DebugName : "?",
                context ? context : "",
                m_Offset / 1024,
                m_Size / 1024,
                GetUtilization() * 100.0f,
                m_AllocCount,
                m_PeakUsage / 1024,
                m_SessionPeak / 1024);
        PrintLog(buf);
    }

    bool Contains(void* ptr) const {
        if (!m_Base || !ptr) return false;
        auto base = reinterpret_cast<uintptr_t>(m_Base);
        auto addr = reinterpret_cast<uintptr_t>(ptr);
        return addr >= base && addr < (base + m_Size);
    }

private:
    uint8_t* m_Base;
    size_t m_Size;
    size_t m_Offset;
    size_t m_AllocCount = 0;
    size_t m_PeakUsage = 0;
    size_t m_SessionPeak = 0;
    unsigned m_Generation = 0;
    const char* m_DebugName;
};


// ----------------------------------------------------------------------------
// Size sanity checks (C2 ME addition — not present in C1)
// ----------------------------------------------------------------------------

// The smart-pointer types are designed to be drop-in replacements for raw
// pointers: their sizeof must match sizeof(void*) so adopting them in
// TModel, TObject, TPicture, TAni, TCharacterInfo, etc. adds no ownership
// overhead. Runtime pointers may grow on x64; they are not serialized.
static_assert(sizeof(void*) == 4 || sizeof(void*) == 8,
              "Expected a 32-bit or 64-bit runtime");
static_assert(sizeof(unique_heap_ptr<std::uint16_t[]>) == sizeof(void*),
              "unique_heap_ptr<std::uint16_t[]> must be the same size as a raw pointer "
              "(empty base optimization on HeapDeleter must apply)");
static_assert(sizeof(unique_obj_ptr<int>) == sizeof(void*),
              "unique_obj_ptr<T> must be the same size as a raw pointer "
              "(empty base optimization on HeapDeleter must apply)");


// ----------------------------------------------------------------------------
// Leak detection (Phase 5F) — only compiled when MEM_DEBUG is defined
// ----------------------------------------------------------------------------
//
// Wrapped in #ifdef MEM_DEBUG so release builds pay zero cost (no map, no
// mutex, no string members in AllocationInfo, no extra code in _HeapAlloc
// / _HeapFree). The MEM_DEBUG flag is set by CMakeLists.txt for Debug
// builds; the menu target excludes itself (it doesn't allocate textures,
// so the report would always be empty noise).
//
// Bootstrap note: g_Allocations is a std::map<void*, AllocationInfo> that
// itself heap-allocates. It is created lazily on the first _HeapAlloc call
// and intentionally NOT tracked by itself (recursive tracking would be
// unsafe and is the reason the bootstrap path skips the recording step).
// The mutex g_AllocMutex is also bootstrap-allocated in Resources.cpp and
// likewise untracked.

#ifdef MEM_DEBUG

#include <map>
#include <string>

// Backing store is recorded at alloc time because the tag alone cannot
// answer it: a Level-tagged allocation made while LevelArena was null falls
// back to the heap and must survive ClearTagAllocations(Level).
struct AllocationInfo {
    size_t      size;
    MemoryTag   tag;
    std::string file;
    int         line;
    AllocBackend backend = AllocBackend::Heap;
    unsigned    arenaGen = 0;
};

// The map is heap-allocated lazily (see bootstrap note above) and freed
// at the end of PrintMemoryLeaks. The pointer itself is not a smart
// pointer because the leak detector must outlive every other subsystem
// to print its report.
extern std::map<void*, AllocationInfo>* g_Allocations;

void PrintMemoryLeaks();
void ClearTagAllocations(MemoryTag tag);

// Automatic call-site capture (MEM_DEBUG only). Every 3-arg and 4-arg
// _HeapAlloc call in every TU that includes this header is rerouted to
// _HeapAllocImpl with __FILE__/__LINE__ appended — contributors get full
// leak attribution without remembering a special macro, and the plain
// function signatures keep working for any TU that bypasses the header.
//
// Only 3-arg and 4-arg shapes exist in the codebase (verified by audit);
// a call with any other arity fails loudly at the selector instead of
// silently recording a wrong tag.
void* _HeapAllocImpl(Platform::HeapHandle hHeap, std::uint32_t dwFlags, size_t bytes,
                      MemoryTag tag, const char* file, int line);
#define _HeapAlloc3Dbg(hHeap, dwFlags, bytes) \
    _HeapAllocImpl(hHeap, dwFlags, bytes, MemoryTag::Global, __FILE__, __LINE__)
#define _HeapAlloc4Dbg(hHeap, dwFlags, bytes, tag) \
    _HeapAllocImpl(hHeap, dwFlags, bytes, tag, __FILE__, __LINE__)
#define _HeapAllocSelect(_1, _2, _3, _4, NAME, ...) NAME
// Extra indirection: MSVC's traditional preprocessor (C++17 without
// /Zc:preprocessor) does not rescan the selector result before the
// trailing (args) are applied, so without this layer every call
// mis-selects the 3-arg form (observed as C4003 + C2440 at call sites).
#define _HeapAllocExpand(x) x
#define _HeapAlloc(...) \
    _HeapAllocExpand(_HeapAllocSelect(__VA_ARGS__, _HeapAlloc4Dbg, _HeapAlloc3Dbg)(__VA_ARGS__))

// Note: originally named _Alloc in the design doc and in C1, but MSVC's
// STL uses _Alloc internally in <unordered_map> and <map>, so naming
// our macro _Alloc produces a 'not enough arguments for function-like
// macro' warning every time those headers are included. _AllocTrack
// avoids the collision without changing semantics. It now forwards to
// the plain 4-arg form; the selector macro above captures the location.
#define _AllocTrack(size, tag) _HeapAlloc(Heap, 0, (size_t)(size), (tag))

#endif // MEM_DEBUG


#endif // HUNT_MEMORY_H
