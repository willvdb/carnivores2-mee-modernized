#ifndef HUNT_LOAD_VALIDATE_H
#define HUNT_LOAD_VALIDATE_H

// LoadValidate.h
// Checked arithmetic + file-data validation for the binary/text loaders.
//
// Background: the .CAR/.RSC/.MAP/BMP/TGA/WAV/_RES.TXT loaders historically
// trusted file-derived counts and dimensions. Shipped assets are all valid
// (audit-scanned), but modded or truncated files could drive signed-shift
// overflows, fixed-array overruns, and stack/heap overflows. These helpers
// fail fast (via the caller's DoHalt) instead of corrupting memory.
//
// All helpers are pure and header-inline so tests/test_load_validate.cpp
// covers them without linking engine code. Only ReadExact touches Win32,
// and it reports success/failure instead of halting so tests can drive it
// with a real temp file.

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <limits>
#include "../Platform/Files.h"

// File-derived count must fit its fixed destination array.
inline bool IsValidCount(int value, int capacity)
{
    return value >= 0 && value <= capacity;
}

// File/script-derived array index must be strictly below capacity.
inline bool IsValidIndex(int value, int capacity)
{
    return value >= 0 && value < capacity;
}

// Compute animation duration without signed overflow. A one-frame character
// animation is valid; its loader adds a duplicate interpolation frame.
inline bool CheckedAnimationDuration(int frames, int kps, int& durationMs)
{
    if (frames < 1 || kps <= 0)
        return false;
    const uint64_t totalMs = static_cast<uint64_t>(frames) * 1000u;
    const uint64_t duration = totalMs / static_cast<uint64_t>(kps);
    if (duration == 0 || duration > static_cast<uint64_t>((std::numeric_limits<int>::max)()))
        return false;
    durationMs = static_cast<int>(duration);
    return true;
}

// 8-bit fixed-point morph position with checked-width intermediates. The
// loader enforces the same frame-count limit; invalid runtime state falls
// back to frame zero rather than indexing outside animation data.
inline int CalculateMorphFrameFixed(int frameCount, int frameTime, int animationTime)
{
    constexpr int maxAnimationFrames = (std::numeric_limits<int>::max)() / 256;
    if (frameCount <= 1 || frameCount > maxAnimationFrames || animationTime <= 0)
        return 0;
    int clampedTime = frameTime;
    if (clampedTime < 0) clampedTime = 0;
    if (clampedTime >= animationTime) clampedTime = animationTime - 1;
    const int64_t fixed = static_cast<int64_t>(frameCount - 1) *
                          static_cast<int64_t>(clampedTime) * 256 /
                          static_cast<int64_t>(animationTime);
    return static_cast<int>(fixed);
}

// Map-reference helpers preserve the two object sentinels and the legacy
// 0xFFFF texture sentinel (which CreateTMap normalizes to texture 1).
inline bool IsValidMapTextureIndex(uint16_t index, int textureCount)
{
    return index == 0xFFFFu ? textureCount > 1
                            : IsValidIndex(static_cast<int>(index), textureCount);
}

inline bool IsValidMapObjectIndex(uint8_t index, int modelCount)
{
    return index == 254u || index == 255u ||
           IsValidIndex(static_cast<int>(index), modelCount);
}

inline bool IsValidMapWaterIndex(uint8_t index, int waterCount)
{
    return IsValidIndex(static_cast<int>(index), waterCount);
}

// Native storage sizes: check before multiplying, including on x64 where
// unsigned long long is no wider than size_t. Failure leaves out unchanged.
inline bool CheckedBytes2(size_t a, size_t b, size_t& out)
{
    if (b != 0 && a > (std::numeric_limits<size_t>::max)() / b)
        return false;
    out = a * b;
    return true;
}

inline bool CheckedBytes3(size_t a, size_t b, size_t c, size_t& out)
{
    if (a == 0 || b == 0 || c == 0) {
        out = 0;
        return true;
    }
    size_t ab = 0;
    if (!CheckedBytes2(a, b, ab))
        return false;
    return CheckedBytes2(ab, c, out);
}

// Single legacy/Win32 transfer sizes have a separate UINT32_MAX ceiling.
// A native allocation succeeding does not prove a ReadFile count will fit.
inline bool CheckedTransferBytes2(size_t a, size_t b, size_t& out)
{
    size_t bytes = 0;
    if (!CheckedBytes2(a, b, bytes) || bytes > UINT32_MAX)
        return false;
    out = bytes;
    return true;
}

inline bool CheckedTransferBytes3(size_t a, size_t b, size_t c, size_t& out)
{
    size_t bytes = 0;
    if (!CheckedBytes3(a, b, c, bytes) || bytes > UINT32_MAX)
        return false;
    out = bytes;
    return true;
}

// Face-vertex index must address a loaded vertex.
inline bool IsValidVertexIndex(int index, int vcount)
{
    return index >= 0 && index < vcount;
}

// BMP rows land in byte fRGB[800][3] on the stack: the width cap is
// structural, not aesthetic. Height is heap-checked via CheckedBytes.
inline bool IsValidBmpWidth(int w) { return w > 0 && w <= 800; }

// Pictures are native storage, read one bounded row at a time. Existing
// renderers index pixels with signed int, so retain that distinct count cap.
// For 16-bit pixels this preserves the historical UINT32_MAX byte ceiling.
inline bool CheckedPictureBytes(int width, int height, size_t& out)
{
    size_t pixels = 0;
    if (width <= 0 || height <= 0 ||
        !CheckedBytes2(static_cast<size_t>(width), static_cast<size_t>(height), pixels) ||
        pixels > static_cast<size_t>((std::numeric_limits<int>::max)()))
        return false;
    return CheckedBytes2(pixels, sizeof(uint16_t), out);
}

// WAV data length sanity: non-negative, bounded (16 MiB of 16-bit audio is
// far beyond any shipped effect), so a corrupt header cannot drive a
// gigantic vector::assign.
inline bool IsValidWavLength(int length)
{
    return length >= 0 && length <= (16 << 20);
}

// Samples to allocate for a byte length (round UP: a malformed odd length
// previously overflowed the floor(length/2) allocation by one byte).
inline size_t WavAllocSamples(int length)
{
    return static_cast<size_t>(length) / sizeof(short int) +
           ((static_cast<size_t>(length) % sizeof(short int)) != 0 ? 1u : 0u);
}

// Exact Win32 read: success only when every requested byte arrives.
// Truncated files previously left stack locals uninitialized and drove
// downstream loops/allocations with garbage.
inline bool ReadExact(Platform::FileHandle hfile, void* buffer, std::uint32_t bytes)
{
    if (bytes == 0)
        return true;
    std::uint32_t got = 0;
    if (!Platform::ReadFile(hfile, buffer, bytes, &got))
        return false;
    return got == bytes;
}

// Strip one layer of surrounding single quotes in place ('name' -> name).
// Script lines keep their trailing newline, so the old inline idiom was
// value[strlen(value)-2] = 0 with use of &value[1]: it dropped the closing
// quote and relied on the newline's position. This helper makes each step
// explicit and returns nullptr on malformed input (caller halts) instead
// of indexing before the buffer when the value is shorter than ''.
inline char* StripQuoted(char* value)
{
    if (!value)
        return nullptr;
    size_t len = strlen(value);
    while (len > 0 && (value[len - 1] == '\n' || value[len - 1] == '\r'))
        value[--len] = 0;
    if (len < 2 || value[0] != '\'' || value[len - 1] != '\'')
        return nullptr;
    value[len - 1] = 0;
    return value + 1;
}

// Bounded copy into a fixed char field. Returns false (caller halts)
// instead of overflowing; truncation is rejected because a silently
// shortened filename produces a confusing failure far from the cause.
inline bool CopyCapped(char* dst, size_t dstCap, const char* src)
{
    if (!dst || dstCap == 0 || !src)
        return false;
    const size_t len = strlen(src);
    if (len >= dstCap)
        return false;
    memcpy(dst, src, len + 1);
    return true;
}

// Pointer to the basename after the last path separator (either slash),
// or the whole string when no separator is present.
inline const char* PathBasename(const char* path)
{
    if (!path)
        return nullptr;
    const char* base = strrchr(path, '/');
    const char* alt = strrchr(path, '\\');
    if (alt && (!base || alt > base))
        base = alt;
    return base ? base + 1 : path;
}

// True when the project path's basename is the vanilla sixth-slot asset
// name "external" (case-insensitive, either slash convention).
inline bool ProjectBasenameIsExternal(const char* path)
{
    const char* base = PathBasename(path);
    if (!base) return false;
    const char* expected = "external";
    for (; *base && *expected; ++base, ++expected) {
        char c = *base;
        if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
        if (c != *expected) return false;
    }
    return *base == *expected;
}

// The sixth hunt slot stores its assets as external.map/.rsc in the vanilla
// layout, while every other slot is areaN. Script area filtering in
// ScriptParser.cpp keys off the fixed path offset that holds the area digit
// ("huntdat/areas/areaN" -> index 18); a bare "external" basename satisfies
// no area case there, so every overwrite/addition block is applied regardless
// of its area tag and the areatable slice is never selected (reproduced as an
// 0xC0000005 when launching prj=huntdat/areas/external).
//
// Rewriting the basename to the slot's logical name "area6" keeps the legacy
// offset logic valid while the engine's file-open path (ProjectName, set in
// CommandLine.cpp) keeps the original string and still opens external.map/.rsc.
// The rewrite only ever shortens the basename, so it cannot fail after a
// successful CopyCapped; a false return means the input was not "external"
// or the caller's buffer is inconsistent.
inline bool RewriteExternalProjectAlias(char* path, size_t cap)
{
    if (!path || cap == 0 || !ProjectBasenameIsExternal(path))
        return false;
    const char* base = PathBasename(path);
    const size_t prefixLen = static_cast<size_t>(base - path);
    if (prefixLen + strlen("area6") + 1 > cap)
        return false;
    memcpy(path + prefixLen, "area6", sizeof("area6"));
    return true;
}

#endif // HUNT_LOAD_VALIDATE_H
