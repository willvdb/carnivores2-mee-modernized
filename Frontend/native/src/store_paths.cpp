#include "store_paths.hpp"
#include "schema_compat.hpp"
#include <deque>
#include <set>
#include <algorithm>
#include <array>
#include <cstdint>
#include <cerrno>
#include <cstdlib>
#include <limits>
#include <system_error>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <pwd.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace c2::frontend::store_paths {
namespace fs = std::filesystem;
namespace {
#ifdef _WIN32
std::optional<fs::path> environment(const wchar_t* name) {
    SetLastError(ERROR_SUCCESS);
    auto n = GetEnvironmentVariableW(name, nullptr, 0);
    if (!n) return GetLastError() == ERROR_ENVVAR_NOT_FOUND ? std::nullopt : std::optional<fs::path>(fs::path{});
    std::wstring value(n, L'\0');
    auto used = GetEnvironmentVariableW(name, value.data(), n);
    if (used >= n) throw StoreError("environment changed while resolving store");
    value.resize(used); return fs::path(value);
}
fs::path home() {
    if (auto p = environment(L"USERPROFILE")) return *p;
    auto drive = environment(L"HOMEDRIVE"), tail = environment(L"HOMEPATH");
    if (!tail) throw StoreError("cannot determine home directory");
    return fs::path((drive ? drive->native() : L"") + tail->native());
}
struct Handle {
    HANDLE value;
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
};
std::u32string points(const std::wstring& s) {
    std::u32string out;
    for (std::size_t i = 0; i < s.size(); ++i) {
        auto c = static_cast<char32_t>(s[i]);
        if (c >= 0xd800 && c <= 0xdbff && i + 1 < s.size() && s[i + 1] >= 0xdc00 && s[i + 1] <= 0xdfff)
            c = 0x10000 + ((c - 0xd800) << 10) + (s[++i] - 0xdc00);
        out.push_back(c);
    }
    return out;
}
std::wstring units(std::u32string_view s) {
    std::wstring out;
    for (auto c : s) {
        if (c > 0xffff) { c -= 0x10000; out.push_back(static_cast<wchar_t>(0xd800 + (c >> 10))); out.push_back(static_cast<wchar_t>(0xdc00 + (c & 1023))); }
        else out.push_back(static_cast<wchar_t>(c));
    }
    return out;
}
std::optional<std::wstring> final_name(const std::wstring& path, DWORD& error) {
    Handle h{CreateFileW(path.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (h.value == INVALID_HANDLE_VALUE) { error = GetLastError(); return std::nullopt; }
    auto n = GetFinalPathNameByHandleW(h.value, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!n) { error = GetLastError(); return std::nullopt; }
    std::wstring out(n, L'\0');
    auto used = GetFinalPathNameByHandleW(h.value, out.data(), n, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (!used || used >= n) throw StoreError("manifest path changed while resolving");
    out.resize(used); error = 0; return out;
}
// Match ntpath.realpath's prefix retention rather than STL canonical(), which
// strips device prefixes and treats UNC server pseudo-parents as ancestors.
fs::path resolve(const fs::path& source) {
    auto original = fs::absolute(source).lexically_normal().native();
    const bool prefixed = original.rfind(L"\\\\?\\", 0) == 0;
    DWORD initial = 0;
    auto found = final_name(original, initial);
    auto path = original;
    if (found) path = *found;
    else {
        std::vector<std::wstring> tail;
        std::set<std::wstring> links;
        for (;;) {
            DWORD error = 0;
            if (auto p = final_name(path, error)) { path = *p; break; }
            switch (error) {
            case 1: case 2: case 3: case 5: case 21: case 32: case 50:
            case 53: case 65: case 67: case 87: case 123: case 161: case 1920: case 1921: break;
            default: throw StoreError("cannot resolve manifest path");
            }
            std::error_code ec;
            auto target = fs::read_symlink(fs::path(path), ec);
            if (!ec && links.insert(path).second) {
                if (target.is_relative()) target = fs::path(path).parent_path() / target;
                path = target.lexically_normal().native();
                continue;
            }
            const auto parsed = schema::path(points(path), true);
            if (parsed.parts.empty()) break;
            tail.push_back(units(parsed.parts.back()));
            auto cut = path.find_last_of(L"\\/");
            const auto anchor = units(parsed.drive + parsed.root);
            path = cut < anchor.size() ? anchor : path.substr(0, cut);
        }
        for (auto it = tail.rbegin(); it != tail.rend(); ++it) path = (fs::path(path) / *it).native();
    }
    if (!prefixed && path.rfind(L"\\\\?\\", 0) == 0) {
        auto plain = path.rfind(L"\\\\?\\UNC\\", 0) == 0 ? L"\\\\" + path.substr(8) : path.substr(4);
        DWORD error = 0;
        auto check = final_name(plain, error);
        if ((check && *check == path) || (!check && error == initial)) path = plain;
    }
    return fs::path(path);
}
void check_handle(HANDLE h) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info)) throw StoreError("cannot inspect manifest path");
    if (info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) throw StoreError("session path contains a reparse point");
    if (GetFileType(h) != FILE_TYPE_DISK) throw StoreError("session path contains a special file");
    if (!(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && info.nNumberOfLinks != 1)
        throw StoreError("session file has multiple hard links");
}
#else
std::optional<fs::path> environment(const char* name) {
    if (auto p = std::getenv(name)) return fs::path(p);
    return std::nullopt;
}
fs::path home() {
    if (auto p = environment("HOME")) return p->empty() ? fs::path("/") : *p;
    if (auto p = getpwuid(getuid())) return fs::path(p->pw_dir);
    throw StoreError("cannot determine home directory");
}
struct Handle { int value; ~Handle() { if (value >= 0) ::close(value); } };
fs::path resolve(const fs::path& source) {
    struct Part { fs::path path; bool leave = false; };
    std::deque<Part> pending;
    const auto absolute = fs::absolute(source);
    for (const auto& p : absolute.relative_path()) pending.push_back({p, false});
    fs::path result = absolute.root_path();
    std::set<fs::path> active;
    while (!pending.empty()) {
        auto part = pending.front(); pending.pop_front();
        if (part.leave) { active.erase(part.path); continue; }
        if (part.path.empty() || part.path == ".") continue;
        if (part.path == "..") { result = result.parent_path(); continue; }
        auto next = result / part.path;
        struct stat info{};
        if (::lstat(next.c_str(), &info) == 0 && S_ISLNK(info.st_mode)) {
            if (!active.insert(next).second) throw StoreError("symlink loop in store path");
            auto target = fs::read_symlink(next);
            pending.push_front({next, true});
            std::vector<fs::path> parts;
            for (const auto& p : target.relative_path()) parts.push_back(p);
            for (auto it = parts.rbegin(); it != parts.rend(); ++it) pending.push_front({*it, false});
            if (target.is_absolute()) result = target.root_path();
        } else result = next;
    }
    return result;
}
void check_stat(const struct stat& s) {
    if (S_ISLNK(s.st_mode)) throw StoreError("session path contains an alias");
    if (!S_ISDIR(s.st_mode) && !S_ISREG(s.st_mode)) throw StoreError("session path contains a special file");
    if (S_ISREG(s.st_mode) && s.st_nlink != 1) throw StoreError("session file has multiple hard links");
}
#endif
void safe_path_impl(const fs::path& path) {
#ifdef _WIN32
    auto shape = schema::path(points(path.native()), true);
    if (!shape.absolute() || shape.parent()) throw StoreError("session path must be absolute without traversal");
#else
    if (!path.is_absolute()) throw StoreError("session path must be absolute without traversal");
    for (const auto& part : path) if (part == "..") throw StoreError("session path must be absolute without traversal");
#endif
    const auto prefixes = ancestor_paths(path);
    for (const auto& prefix : prefixes) {
        std::error_code ec;
        auto info = fs::symlink_status(prefix, ec);
        if (ec && ec != std::errc::no_such_file_or_directory && ec != std::errc::not_a_directory)
            throw StoreError("cannot inspect manifest path: " + ec.message());
        if (fs::is_symlink(info)) throw StoreError("session path contains an alias");
        // Compare canonical spelling too: Windows short-name aliases are not links.
        auto resolved = resolve(prefix);
#ifdef _WIN32
        // pathlib uses pinned Unicode lower(), not the OS uppercase table.
        if (!windows_path_equal(points(resolved.native()), points(prefix.native())))
#else
        if (resolved != prefix)
#endif
            throw StoreError("session path contains an alias");
        if (!fs::exists(info)) continue;
#ifdef _WIN32
        Handle h{CreateFileW(prefix.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
        if (h.value == INVALID_HANDLE_VALUE) throw StoreError("cannot inspect manifest path");
        check_handle(h.value);
#else
        struct stat s{};
        if (::lstat(prefix.c_str(), &s)) throw StoreError("cannot inspect manifest path");
        check_stat(s);
#endif
    }
}
}
void safe_path(const fs::path& path) { safe_path_impl(path); }
bool resolves_to_self(const fs::path& path) {
#ifdef _WIN32
    return windows_path_equal(points(resolve(path).native()), points(path.native()));
#else
    return resolve(path) == path;
#endif
}
std::u32string native_points(const fs::path& path) {
#ifdef _WIN32
    return points(path.native());
#else
    // Decode one valid UTF-8 sequence, escaping each undecodable byte separately.
    // This is the UTF-8 filesystem encoding pinned by the Python oracle, not a locale conversion.
    const auto& s = path.native();
    std::u32string out;
    for (std::size_t i = 0; i < s.size();) {
        auto c = static_cast<unsigned char>(s[i]);
        if (c < 128) { out.push_back(c); ++i; continue; }
        unsigned n = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        char32_t point = n ? c & ((1u << (7 - n)) - 1) : 0;
        bool valid = n && i + n <= s.size();
        for (unsigned j = 1; valid && j < n; ++j) {
            auto tail = static_cast<unsigned char>(s[i + j]);
            valid = (tail & 0xc0) == 0x80; point = (point << 6) | (tail & 63);
        }
        valid = valid && (n != 3 || point >= 0x800) && (n != 4 || point >= 0x10000) &&
            point <= 0x10ffff && !(point >= 0xd800 && point <= 0xdfff);
        if (valid) { out.push_back(point); i += n; }
        else { out.push_back(0xdc00 + c); ++i; }
    }
    return out;
#endif
}
bool windows_path_equal(std::u32string a, std::u32string b) {
    const auto x = schema::path(std::move(a), true), y = schema::path(std::move(b), true);
    if (schema::lower(x.drive) != schema::lower(y.drive) || x.root != y.root || x.parts.size() != y.parts.size()) return false;
    for (std::size_t i = 0; i < x.parts.size(); ++i)
        if (schema::lower(x.parts[i]) != schema::lower(y.parts[i])) return false;
    return true;
}
std::vector<fs::path> ancestor_paths(const fs::path& path) {
    std::vector<fs::path> prefixes;
#ifdef _WIN32
    auto parsed = schema::path(points(path.native()), true);
    prefixes.emplace_back(units(parsed.drive + parsed.root));
    for (const auto& part : parsed.parts) prefixes.push_back(prefixes.back() / units(part));
#else
    prefixes.push_back(path.root_path());
    for (const auto& part : path.relative_path()) prefixes.push_back(prefixes.back() / part);
#endif
    return prefixes;
}
fs::path resolve_native(const fs::path& path) {
    if (path.native().find(fs::path::value_type{}) != fs::path::string_type::npos)
        throw StoreError("native path contains NUL");
    return resolve(path.empty() ? fs::path(".") : path);
}
fs::path resolve_root(fs::path path) {
    if (path.empty()) path = ".";
    auto s = path.native();
    if (s.find(decltype(s)::value_type{}) != s.npos) throw StoreError("store path contains NUL");
    if (!s.empty() && s[0] == '~') {
        auto end = s.find_first_of(
#ifdef _WIN32
            L"/\\"
#else
            "/"
#endif
        );
        auto user = s.substr(1, end == s.npos ? s.npos : end - 1);
        fs::path base;
        if (user.empty()) base = home();
        else {
#ifdef _WIN32
            auto current = home(); auto username = environment(L"USERNAME");
            if (username && user == username->native()) base = current;
            else {
                if (!username || current.filename() != *username) throw StoreError("cannot determine home directory");
                base = current.parent_path() / user;
            }
#else
            auto p = getpwnam(user.c_str());
            if (!p) throw StoreError("cannot determine home directory");
            base = p->pw_dir;
#endif
        }
        path = end == s.npos ? base : fs::path(base.native() + s.substr(end));
    }
    // pathlib turns an empty expanduser result back into the current directory.
    if (path.empty()) path = ".";
    return resolve(path);
}
fs::path default_directory() {
#ifdef _WIN32
    auto local = environment(L"LOCALAPPDATA");
#else
    auto local = environment("LOCALAPPDATA");
#endif
    return (local ? *local : home() / ".local" / "share") / "carnivores-lodge";
}
std::optional<std::string> read(const fs::path& path, const ReadPolicy& policy, bool require_eof) {
    safe_path(path);
#ifdef _WIN32
    Handle h{CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                         nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (h.value == INVALID_HANDLE_VALUE) {
        auto e = GetLastError();
        if (e == ERROR_FILE_NOT_FOUND || e == ERROR_PATH_NOT_FOUND) return std::nullopt;
        throw StoreError("cannot read manifest");
    }
    check_handle(h.value);
    BY_HANDLE_FILE_INFORMATION before{};
    if (!GetFileInformationByHandle(h.value, &before) || (before.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        throw StoreError("cannot read manifest: not a regular file");
    std::uint64_t size = (std::uint64_t(before.nFileSizeHigh) << 32) | before.nFileSizeLow;
#else
    Handle h{::open(path.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC)};
    if (h.value < 0) {
        if (errno == ENOENT || errno == ENOTDIR) return std::nullopt;
        throw StoreError("cannot read manifest");
    }
    struct stat before{};
    if (::fstat(h.value, &before)) throw StoreError("cannot inspect manifest");
    check_stat(before);
    if (!S_ISREG(before.st_mode)) throw StoreError("cannot read manifest: not a regular file");
    auto size = static_cast<std::uint64_t>(before.st_size);
#endif
    if (size > std::numeric_limits<std::size_t>::max() || (policy.max_bytes && size > *policy.max_bytes))
        throw ResourceExhausted("manifest input resource budget exhausted");
    std::string content;
    if (size > content.max_size()) throw ResourceExhausted("manifest allocation resource exhausted");
    content.resize(static_cast<std::size_t>(size));
    std::size_t offset = 0;
    while (offset < content.size()) {
        auto amount = static_cast<unsigned>(std::min<std::size_t>(content.size() - offset, 65536));
#ifdef _WIN32
        DWORD got = 0;
        if (!ReadFile(h.value, content.data() + offset, amount, &got, nullptr)) throw StoreError("cannot read manifest");
#else
        auto got = ::read(h.value, content.data() + offset, amount);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) throw StoreError("cannot read manifest");
#endif
        if (!got) throw StoreError("manifest changed while reading");
        offset += static_cast<std::size_t>(got);
    }
    // Capture checks EOF as well as the reported size: virtual regular files may
    // return bytes while stat continues reporting zero. Never certify that empty.
    if (require_eof) {
        char extra;
#ifdef _WIN32
        DWORD got = 0;
        if (!ReadFile(h.value, &extra, 1, &got, nullptr)) throw StoreError("cannot read state file");
#else
        ssize_t got;
        do { got = ::read(h.value, &extra, 1); } while (got < 0 && errno == EINTR);
        if (got < 0) throw StoreError("cannot read state file");
#endif
        if (got) throw StoreError("state changed while capturing");
    }
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION after{};
    if (!GetFileInformationByHandle(h.value, &after) || before.nFileSizeHigh != after.nFileSizeHigh ||
        before.nFileSizeLow != after.nFileSizeLow || before.nNumberOfLinks != after.nNumberOfLinks ||
        CompareFileTime(&before.ftLastWriteTime, &after.ftLastWriteTime)) throw StoreError("manifest changed while reading");
#else
    struct stat after{};
    if (::fstat(h.value, &after) || before.st_size != after.st_size || before.st_nlink != after.st_nlink ||
        before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec)
        throw StoreError("manifest changed while reading");
#endif
    return content;
}
} // namespace c2::frontend::store_paths
