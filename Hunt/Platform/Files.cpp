#include "Files.h"
#include "../Session/Session.h"
#include <algorithm>
#include <cerrno>
#include <cstring>
#include <filesystem>
#include <vector>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <unistd.h>
#include <fcntl.h>
#endif

namespace Platform {
std::string NormalizePath(std::string path)
{
    std::replace(path.begin(), path.end(), '\\', '/');
    return path;
}
namespace {
std::string Fold(std::string value)
{
    for (char& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
}
bool ResolveLegacyPath(const std::string& path, std::string& resolved, bool createLeaf)
{
    namespace fs = std::filesystem;
    const fs::path normalized(NormalizePath(path));
    if (normalized.empty()) return false;
    fs::path current = normalized.root_path();
    const auto relative = normalized.relative_path();
    for (auto it = relative.begin(); it != relative.end(); ++it) {
        const auto candidate = current / *it;
        std::error_code ec;
        if (*it == "." || *it == ".." || fs::exists(candidate, ec)) {
            current = candidate;
            continue;
        }
        fs::path match;
        const auto folded = Fold(it->string());
        for (fs::directory_iterator dir(current.empty() ? fs::path(".") : current, ec), end;
             !ec && dir != end; dir.increment(ec)) {
            if (Fold(dir->path().filename().string()) != folded) continue;
            if (!match.empty()) { errno = EEXIST; return false; }
            match = dir->path().filename();
        }
        if (ec) { errno = ec.value(); return false; }
        if (match.empty()) {
            auto next = it; ++next;
            if (!createLeaf || next != relative.end()) { errno = ENOENT; return false; }
            current = candidate;
        } else current /= match;
    }
    resolved = current.string();
    return true;
}
static FileHandle OpenNativeFile(const char* path, FileMode mode, bool shareRead)
{
    if (!path) return InvalidFile;
#ifdef _WIN32
    // Preserve the native file sharing/creation behavior of the reference build.
    DWORD access = mode == FileMode::Read ? GENERIC_READ : GENERIC_WRITE;
    DWORD disposition = mode == FileMode::Read || mode == FileMode::Update ? OPEN_EXISTING :
                        mode == FileMode::CreateNew ? CREATE_NEW : CREATE_ALWAYS;
    return CreateFileA(path, access, shareRead ? FILE_SHARE_READ : 0, nullptr,
                       disposition, FILE_ATTRIBUTE_NORMAL, nullptr);
#else
    std::string resolved;
    // Session-owned targets have already been checked. Do not case-resolve a
    // missing leaf into a different (possibly linked) output file afterwards.
    if (EngineSession::Active() && mode != FileMode::Read) resolved = NormalizePath(path);
    else if (!ResolveLegacyPath(path, resolved, mode == FileMode::Write || mode == FileMode::CreateNew)) return InvalidFile;
    FILE* file = nullptr;
    if (mode == FileMode::CreateNew) {
        const int fd = open(resolved.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0666);
        if (fd >= 0) {
            file = fdopen(fd, "wb");
            if (!file) close(fd);
        }
    } else file = std::fopen(resolved.c_str(), mode == FileMode::Read ? "rb" : mode == FileMode::Update ? "r+b" : "wb");
    if (file && mode != FileMode::Read) std::setvbuf(file, nullptr, _IONBF, 0);
    return file ? file : InvalidFile;
#endif
}
FileHandle OpenFile(const char* path, FileMode mode, bool shareRead)
{
    std::string resolved;
    if (!path || !EngineSession::Resolve(path, mode != FileMode::Read, resolved)) {
        EngineSession::Fail();
        return InvalidFile;
    }
    const auto file = OpenNativeFile(resolved.c_str(), mode, shareRead);
    if (mode != FileMode::Read) EngineSession::Check(file != InvalidFile);
    return file;
}
static bool CloseNativeFile(FileHandle file)
{
    if (!file || file == InvalidFile) return false;
#ifdef _WIN32
    return CloseHandle(file) != 0;
#else
    return std::fclose(static_cast<FILE*>(file)) == 0;
#endif
}
bool CloseFile(FileHandle file) { return EngineSession::Check(CloseNativeFile(file)); }
bool ReadFile(FileHandle file, void* data, std::uint32_t bytes, std::uint32_t* read)
{
    if (read) *read = 0;
    if (!file || file == InvalidFile) return false;
#ifdef _WIN32
    DWORD count = 0;
    const bool ok = ::ReadFile(file, data, bytes, &count, nullptr) != 0;
    if (read) *read = count;
    return ok;
#else
    auto* stream = static_cast<FILE*>(file);
    const auto count = std::fread(data, 1, bytes, stream);
    if (read) *read = static_cast<std::uint32_t>(count);
    return !std::ferror(stream);
#endif
}
static bool WriteNativeFile(FileHandle file, const void* data, std::uint32_t bytes, std::uint32_t* written)
{
    if (written) *written = 0;
    if (!file || file == InvalidFile) return false;
#ifdef _WIN32
    DWORD count = 0;
    const bool ok = ::WriteFile(file, data, bytes, &count, nullptr) != 0;
    if (written) *written = count;
    return ok && count == bytes;
#else
    const auto count = std::fwrite(data, 1, bytes, static_cast<FILE*>(file));
    if (written) *written = static_cast<std::uint32_t>(count);
    return count == bytes;
#endif
}
bool WriteFile(FileHandle file, const void* data, std::uint32_t bytes, std::uint32_t* written)
{
    return EngineSession::Check(WriteNativeFile(file, data, bytes, written));
}
std::int64_t SeekFile(FileHandle file, std::int64_t offset, SeekOrigin origin)
{
    if (!file || file == InvalidFile) return -1;
#ifdef _WIN32
    LARGE_INTEGER distance{}, position{};
    distance.QuadPart = offset;
    DWORD method = origin == SeekOrigin::Begin ? FILE_BEGIN : origin == SeekOrigin::Current ? FILE_CURRENT : FILE_END;
    return SetFilePointerEx(file, distance, &position, method) ? position.QuadPart : -1;
#else
    auto* stream = static_cast<FILE*>(file);
    const int method = origin == SeekOrigin::Begin ? SEEK_SET : origin == SeekOrigin::Current ? SEEK_CUR : SEEK_END;
    if (fseeko(stream, offset, method)) return -1;
    return ftello(stream);
#endif
}
std::int64_t FileSize(FileHandle file)
{
#ifdef _WIN32
    LARGE_INTEGER size{};
    return GetFileSizeEx(file, &size) ? size.QuadPart : -1;
#else
    const auto position = SeekFile(file, 0, SeekOrigin::Current);
    if (position < 0) return -1;
    const auto size = SeekFile(file, 0, SeekOrigin::End);
    return SeekFile(file, position, SeekOrigin::Begin) < 0 ? -1 : size;
#endif
}
bool FileExists(const std::string& path)
{
    std::string resolved;
    std::string routed;
    return EngineSession::Resolve(path, false, routed) && ResolveLegacyPath(routed, resolved);
}
std::FILE* OpenTextFile(const char* path, const char* mode)
{
    std::string routed, resolved;
    const bool write = mode && (mode[0] != 'r' || std::strchr(mode, '+'));
    if (!path || !mode || !EngineSession::Resolve(path, write, routed) ||
        !ResolveLegacyPath(routed, resolved, write)) {
        if (write) EngineSession::Fail();
        return nullptr;
    }
    if (EngineSession::Active() && write) resolved = routed;
    auto* file = std::fopen(resolved.c_str(), mode);
    if (write) EngineSession::Check(file != nullptr);
    return file;
}
bool CloseTextFile(std::FILE* file)
{
    if (!file) return EngineSession::Check(false);
    const bool ok = !std::ferror(file);
    const bool closed = std::fclose(file) == 0;
    return EngineSession::Check(ok && closed);
}
std::string ModuleDirectory()
{
    std::vector<char> buffer(4096);
#ifdef _WIN32
    const auto count = GetModuleFileNameA(nullptr, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (!count || count >= buffer.size()) return {};
#else
    const auto count = readlink("/proc/self/exe", buffer.data(), buffer.size());
    if (count < 0 || static_cast<std::size_t>(count) >= buffer.size()) return {};
#endif
    return std::filesystem::path(std::string(buffer.data(), count)).parent_path().string();
}
std::string FindShader(const char* path)
{
    std::string resolved;
    if (ResolveLegacyPath(path, resolved)) return resolved;
    if (ResolveLegacyPath(ModuleDirectory() + "/" + path, resolved)) return resolved;
    return path;
}
}
