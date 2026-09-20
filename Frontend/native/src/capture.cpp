#include "capture.hpp"
#include "store_paths.hpp"
#include "schema_compat.hpp"
#include "c2/frontend/core.hpp"
#include <algorithm>
#include <cstdint>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#endif
namespace c2::frontend {
namespace fs = std::filesystem;
namespace {
constexpr std::uint64_t max_file = 16 * 1024 * 1024, max_total = 32 * 1024 * 1024;
struct Info {
    std::uint64_t size, inode, links, mode, seconds, nanos;
    bool directory, regular, link;
    bool same(const Info& x) const {
        return size == x.size && inode == x.inode && links == x.links && mode == x.mode &&
            seconds == x.seconds && nanos == x.nanos;
    }
};
Info inspect(const fs::path& path) {
#ifdef _WIN32
    // OPEN_REPARSE_POINT observes the link itself, including junctions.
    struct Handle { HANDLE h; ~Handle() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); } };
    Handle h{CreateFileW(path.c_str(), FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    BY_HANDLE_FILE_INFORMATION s{};
    if (h.h == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(h.h, &s)) throw StoreError("cannot inspect state entry");
    bool reparse = (s.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
    bool directory = (s.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    return { (std::uint64_t(s.nFileSizeHigh) << 32) | s.nFileSizeLow,
        (std::uint64_t(s.nFileIndexHigh) << 32) | s.nFileIndexLow, s.nNumberOfLinks, s.dwFileAttributes,
        s.ftLastWriteTime.dwHighDateTime, s.ftLastWriteTime.dwLowDateTime,
        directory, !directory && GetFileType(h.h) == FILE_TYPE_DISK, reparse };
#else
    struct stat s{};
    if (::lstat(path.c_str(), &s)) throw StoreError("cannot inspect state entry");
    return {static_cast<std::uint64_t>(s.st_size), static_cast<std::uint64_t>(s.st_ino),
        static_cast<std::uint64_t>(s.st_nlink), static_cast<std::uint64_t>(s.st_mode),
        static_cast<std::uint64_t>(s.st_mtim.tv_sec), static_cast<std::uint64_t>(s.st_mtim.tv_nsec),
        bool(S_ISDIR(s.st_mode)), bool(S_ISREG(s.st_mode)), bool(S_ISLNK(s.st_mode))};
#endif
}
struct Child { fs::path path; std::u32string name, order; };
std::vector<Child> children(const fs::path& directory) {
    std::vector<Child> out;
    for (const auto& entry : fs::directory_iterator(directory)) {
        auto name = store_paths::native_points(entry.path().filename());
#ifdef _WIN32
        auto order = schema::lower(name);
#else
        auto order = name;
#endif
        out.push_back({entry.path(), std::move(name), std::move(order)});
    }
    // Python's sorted(Path) is stable for case-equivalent NT components.
    std::stable_sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.order < b.order; });
    return out;
}
Capture once(const fs::path& root) {
    Capture result;
    std::uint64_t total = 0;
    struct Frame { std::vector<Child> children; std::size_t next; std::u32string prefix; };
    std::vector<Frame> pending;
    pending.push_back({children(root), 0, U""});
    while (!pending.empty()) {
        auto& frame = pending.back();
        if (frame.next == frame.children.size()) { pending.pop_back(); continue; }
        auto child = frame.children[frame.next++];
        if (result.entries.size() >= 128) throw StoreError("state inventory exceeds 128 entries; retained for review");
        auto relative = frame.prefix + child.name;
        auto info = inspect(child.path);
        result.entries.push_back({relative, "unsafe", info.size, std::nullopt});
        auto& entry = result.entries.back();
        if (child.name.find_first_of(U"\\:") != std::u32string::npos || child.name.find(U'\0') != std::u32string::npos) continue;
        if (info.link || !store_paths::resolves_to_self(child.path)) entry.type = "link-or-alias";
        else if (info.directory) {
            entry.type = "directory"; entry.size = 0;
            pending.push_back({children(child.path), 0, relative + U"/"});
        } else if (info.regular && info.links == 1) {
            // Saturation avoids overflow without changing the reference's unbounded total accounting.
            total = total > max_total || info.size > max_total ? max_total + 1 : total + info.size;
            if (info.size > max_file || total > max_total) { entry.type = "oversized"; continue; }
            ReadPolicy policy; policy.max_bytes = static_cast<std::size_t>(max_file);
            std::optional<std::string> content;
            try { content = store_paths::read(child.path, policy, true); }
            catch (const ResourceExhausted&) { throw StoreError("state changed while capturing"); }
            auto after = inspect(child.path);
            if (!content || content->size() != info.size || !info.same(after)) throw StoreError("state changed while capturing");
            entry.type = "file"; entry.sha256 = sha256(*content);
            result.blobs.push_back({relative, std::move(*content)});
        }
    }
    return result;
}
compat::Value str(std::u32string s) { compat::Value v; v.kind = compat::Kind::string; v.string = std::move(s); return v; }
compat::Value ascii(const std::string& s) { return str(std::u32string(s.begin(), s.end())); }
}
compat::Value Capture::entry_value() const {
    compat::Value list; list.kind = compat::Kind::array;
    for (const auto& e : entries) {
        compat::Value v; v.kind = compat::Kind::object;
        v.object.emplace_back(U"path", str(e.path));
        v.object.emplace_back(U"type", ascii(e.type));
        compat::Value size; size.kind = compat::Kind::integer; size.integer = std::to_string(e.size);
        v.object.emplace_back(U"size", std::move(size));
        if (e.sha256) v.object.emplace_back(U"sha256", ascii(*e.sha256));
        list.array.push_back(std::move(v));
    }
    return list;
}
Capture capture(const fs::path& root) {
    store_paths::safe_path(root);
    if (!fs::is_directory(root)) throw StoreError("state directory missing");
    auto first = once(root), second = once(root);
    bool same = schema::equal(first.entry_value(), second.entry_value()) && first.blobs.size() == second.blobs.size();
    for (std::size_t i = 0; same && i < first.blobs.size(); ++i)
        same = first.blobs[i].path == second.blobs[i].path && first.blobs[i].bytes == second.blobs[i].bytes;
    if (!same) throw StoreError("state changed during capture");
    return first;
}
}
