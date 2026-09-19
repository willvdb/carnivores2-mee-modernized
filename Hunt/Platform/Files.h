#pragma once
#include <cstdint>
#include <cstdio>
#include <string>

namespace Platform {
using FileHandle = void*; // Opaque open file; no native types in loader headers.
inline const FileHandle InvalidFile = reinterpret_cast<FileHandle>(std::intptr_t(-1));
enum class FileMode { Read, Write, CreateNew, Update };
enum class SeekOrigin { Begin, Current, End };
std::string NormalizePath(std::string path);
// Exact spelling wins. Otherwise resolve each component using ASCII case folding.
// Ambiguous matches fail; writes reuse existing spelling, never rename files.
bool ResolveLegacyPath(const std::string& path, std::string& resolved, bool createLeaf = false);
FileHandle OpenFile(const char* path, FileMode mode, bool shareRead = true);
bool CloseFile(FileHandle file);
bool ReadFile(FileHandle file, void* data, std::uint32_t bytes, std::uint32_t* read);
bool WriteFile(FileHandle file, const void* data, std::uint32_t bytes, std::uint32_t* written);
std::int64_t FileSize(FileHandle file); // -1 on error
std::int64_t SeekFile(FileHandle file, std::int64_t offset, SeekOrigin origin);
bool FileExists(const std::string& path);
std::FILE* OpenTextFile(const char* path, const char* mode);
bool CloseTextFile(std::FILE* file); // records buffered write/close failures in session mode
std::string ModuleDirectory();
std::string FindShader(const char* path); // CWD, then executable directory
}
