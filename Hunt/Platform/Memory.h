#pragma once
#include <cstddef>
#include <cstdint>

namespace Platform {
// Opaque heap identity. Only the implementation knows the native allocator.
using HeapHandle = void*;
inline constexpr std::uint32_t ZeroMemoryFlag = 8;
HeapHandle CreateHeap();
void* AllocateHeap(HeapHandle heap, std::uint32_t flags, std::size_t bytes);
bool FreeHeap(HeapHandle heap, std::uint32_t flags, void* memory);
std::size_t HeapAllocationSize(HeapHandle heap, void* memory);
void* AllocatePages(std::size_t bytes);
void FreePages(void* memory, std::size_t bytes);
}
