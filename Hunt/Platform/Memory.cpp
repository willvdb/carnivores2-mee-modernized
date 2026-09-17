#include "Memory.h"
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <cstdlib>
#include <malloc.h>
#include <sys/mman.h>
#endif

namespace Platform {
HeapHandle CreateHeap()
{
#ifdef _WIN32
    return HeapCreate(0, 60000000, 0);
#else
    // Linux uses the process allocator; identity is intentionally not a native handle.
    static int processHeap;
    return &processHeap;
#endif
}
void* AllocateHeap(HeapHandle heap, std::uint32_t flags, std::size_t bytes)
{
#ifdef _WIN32
    return HeapAlloc(heap, flags, bytes);
#else
    if (!heap) return nullptr;
    if (!bytes) bytes = 1; // Preserve independently ownable zero-byte allocations.
    return flags & ZeroMemoryFlag ? std::calloc(1, bytes) : std::malloc(bytes);
#endif
}
bool FreeHeap(HeapHandle heap, std::uint32_t flags, void* memory)
{
#ifdef _WIN32
    return HeapFree(heap, flags, memory) != 0;
#else
    if (!heap) return false;
    std::free(memory);
    return true;
#endif
}
std::size_t HeapAllocationSize(HeapHandle heap, void* memory)
{
#ifdef _WIN32
    return HeapSize(heap, HEAP_NO_SERIALIZE, memory);
#else
    return heap && memory ? malloc_usable_size(memory) : std::size_t(-1);
#endif
}
void* AllocatePages(std::size_t bytes)
{
#ifdef _WIN32
    return VirtualAlloc(nullptr, bytes, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
#else
    void* memory = mmap(nullptr, bytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    return memory == MAP_FAILED ? nullptr : memory;
#endif
}
void FreePages(void* memory, std::size_t bytes)
{
#ifdef _WIN32
    VirtualFree(memory, 0, MEM_RELEASE);
#else
    munmap(memory, bytes);
#endif
}
}
