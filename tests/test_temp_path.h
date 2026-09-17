#pragma once
#include <filesystem>
#include <stdexcept>
#ifdef _WIN32
#include <windows.h>
#endif

// Windows hosted runners can expose TEMP through an 8.3 alias (RUNNER~1).
// Fixtures exercise actual filename/case lookup, not native DOS alias expansion.
inline std::filesystem::path TestTempDirectory()
{
#ifdef _WIN32
    wchar_t temporary[MAX_PATH], actual[MAX_PATH];
    const DWORD size = GetTempPathW(MAX_PATH, temporary);
    if (!size || size >= MAX_PATH) throw std::runtime_error("Cannot find test temp directory");
    const DWORD length = GetLongPathNameW(temporary, actual, MAX_PATH);
    if (!length || length >= MAX_PATH) throw std::runtime_error("Cannot expand test temp directory");
    return actual;
#else
    return std::filesystem::temp_directory_path();
#endif
}
