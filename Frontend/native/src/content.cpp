#include "c2/frontend/content.hpp"
#include "content_internal.hpp"
#include "c2/frontend/core.hpp"
#include "store_paths.hpp"
#include "schema_compat.hpp"
#include <picosha2/picosha2.h>
#include <algorithm>
#include <array>
#include <cerrno>
#include <set>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
namespace c2::frontend {
namespace fs = std::filesystem;
using compat::Value;
using compat::Kind;
namespace {
Value string(std::u32string s) { Value v; v.kind = Kind::string; v.string = std::move(s); return v; }
Value number(std::string s) { Value v; v.kind = Kind::integer; v.integer = std::move(s); return v; }
Value array() { Value v; v.kind = Kind::array; return v; }
Value object() { Value v; v.kind = Kind::object; return v; }
std::u32string ascii(std::string_view s) { return {s.begin(), s.end()}; }
void no_nul(const fs::path& p) {
    if (p.native().find(fs::path::value_type{}) != fs::path::string_type::npos) throw ContentError("native path contains NUL");
}
fs::path syscall_path(fs::path p) {
    no_nul(p);
#ifdef _WIN32
    p.make_preferred();
#endif
    return p;
}
std::u32string relative_points(const fs::path& relative) {
    auto text = store_paths::native_points(relative);
#ifdef _WIN32
    std::replace(text.begin(), text.end(), U'\\', U'/');
#endif
    return text;
}
fs::path native_units_impl(std::u32string_view points) {
#ifdef _WIN32
    std::wstring out;
    for (auto c : points) {
        if (c > 0xffff) { c -= 0x10000; out.push_back(static_cast<wchar_t>(0xd800 + (c >> 10))); out.push_back(static_cast<wchar_t>(0xdc00 + (c & 1023))); }
        else out.push_back(static_cast<wchar_t>(c));
    }
#else
    std::string out;
    for (auto c : points) {
        if (c >= 0xdc80 && c <= 0xdcff) out.push_back(static_cast<char>(c - 0xdc00));
        else if (c < 0x80) out.push_back(static_cast<char>(c));
        else {
            if (c < 0x800) out.push_back(static_cast<char>(0xc0 | (c >> 6)));
            else {
                if (c < 0x10000) out.push_back(static_cast<char>(0xe0 | (c >> 12)));
                else { out.push_back(static_cast<char>(0xf0 | (c >> 18))); out.push_back(static_cast<char>(0x80 | ((c >> 12) & 63))); }
                out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63)));
            }
            out.push_back(static_cast<char>(0x80 | (c & 63)));
        }
    }
#endif
    return fs::path(out);
}
// Path construction removes '.' and repeated separators, but retains '..'.
fs::path pathlib_spelling(const fs::path& p) {
#ifdef _WIN32
    constexpr bool nt = true;
#else
    constexpr bool nt = false;
#endif
    auto parsed = schema::path(store_paths::native_points(p), nt);
    auto text = parsed.drive + parsed.root;
    for (const auto& part : parsed.parts) {
        if (!text.empty() && text.back() != (nt ? U'\\' : U'/') && !(nt && text == parsed.drive && parsed.root.empty())) text += nt ? U'\\' : U'/';
        text += part;
    }
    return native_units_impl(text.empty() ? U"." : text);
}
bool ignored(const std::error_code& ec) {
#ifdef _WIN32
    if (ec.category() == std::system_category() && (ec.value() == 21 || ec.value() == 123 || ec.value() == 1921)) return true;
#endif
    return ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory ||
        ec == std::errc::bad_file_descriptor || ec == std::errc::too_many_symbolic_link_levels;
}
fs::file_status source_status(const fs::path& p) {
    std::error_code ec; auto s = fs::status(p, ec);
    if (ec && !ignored(ec)) throw fs::filesystem_error("cannot inspect content path", p, ec);
    return s;
}
bool is_link(const fs::path& p) {
#ifdef _WIN32
    WIN32_FIND_DATAW data{};
    auto handle = FindFirstFileW(p.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
        const std::error_code ec(static_cast<int>(GetLastError()), std::system_category());
        if (!ignored(ec)) throw fs::filesystem_error("cannot inspect content path", p, ec);
        return false;
    }
    FindClose(handle);
    // pathlib.is_symlink excludes junctions, which os.walk does traverse.
    return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && data.dwReserved0 == IO_REPARSE_TAG_SYMLINK;
#else
    std::error_code ec; auto s = fs::symlink_status(p, ec);
    if (ec && !ignored(ec)) throw fs::filesystem_error("cannot inspect content path", p, ec);
    return fs::is_symlink(s);
#endif
}
bool contained(const fs::path& root, const fs::path& p) {
#ifdef _WIN32
    auto r = schema::path(store_paths::native_points(root), true);
    auto child = schema::path(store_paths::native_points(p), true);
    r.drive = schema::lower(r.drive); child.drive = schema::lower(child.drive);
    for (auto& s : r.parts) s = schema::lower(s);
    for (auto& s : child.parts) s = schema::lower(s);
    return child.below(r) || (child.drive == r.drive && child.root == r.root && child.parts == r.parts);
#else
    auto a = root.begin(), b = p.begin();
    for (; a != root.end(); ++a, ++b) if (b == p.end() || *a != *b) return false;
    return true;
#endif
}
// Decimal arithmetic keeps stat's signed nanoseconds, Windows 128-bit IDs and
// aggregate byte counts independent of the host's integer widths.
std::string plus(std::string a, std::string b) {
    std::string out; unsigned carry = 0;
    while (!a.empty() || !b.empty() || carry) {
        if (!a.empty()) { carry += static_cast<unsigned>(a.back() - '0'); a.pop_back(); }
        if (!b.empty()) { carry += static_cast<unsigned>(b.back() - '0'); b.pop_back(); }
        out.push_back(static_cast<char>('0' + carry % 10)); carry /= 10;
    }
    std::reverse(out.begin(), out.end()); return out.empty() ? "0" : out;
}
std::string minus(std::string a, std::string b) {
    if (a.size() < b.size() || (a.size() == b.size() && a < b)) return "-" + minus(b, a);
    std::string out; int borrow = 0;
    while (!a.empty()) {
        int d = a.back() - '0' - borrow; a.pop_back();
        if (!b.empty()) { d -= b.back() - '0'; b.pop_back(); }
        borrow = d < 0; if (borrow) d += 10;
        out.push_back(static_cast<char>('0' + d));
    }
    while (out.size() > 1 && out.back() == '0') out.pop_back();
    std::reverse(out.begin(), out.end()); return out.empty() ? "0" : out;
}
std::string signed_plus(std::string a, std::string b) {
    bool an = a[0] == '-', bn = b[0] == '-';
    if (an) a.erase(0, 1);
    if (bn) b.erase(0, 1);
    if (an != bn) return an ? minus(b, a) : minus(a, b);
    auto sum = plus(a, b); return an && sum != "0" ? "-" + sum : sum;
}
std::string scale(std::string a, unsigned zeros) { if (a != "0") a.append(zeros, '0'); return a; }
struct Metadata {
    std::string size, modified, inode, device;
    bool regular = false;
    bool same(const Metadata& b) const { return size == b.size && modified == b.modified && inode == b.inode && device == b.device && regular == b.regular; }
};
#ifdef _WIN32
struct Handle { HANDLE h = INVALID_HANDLE_VALUE; ~Handle() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); } };
Metadata metadata(HANDLE h) {
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(h, &info)) throw ContentError("cannot stat content");
    auto size = (std::uint64_t(info.nFileSizeHigh) << 32) | info.nFileSizeLow;
    auto ticks = (std::uint64_t(info.ftLastWriteTime.dwHighDateTime) << 32) | info.ftLastWriteTime.dwLowDateTime;
    auto inode = std::to_string((std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow);
    FILE_ID_INFO id{};
    if (GetFileInformationByHandleEx(h, FileIdInfo, &id, sizeof(id))) {
        inode = "0";
        for (int i = 15; i >= 0; --i) {
            for (int bit = 0; bit < 8; ++bit) inode = plus(inode, inode);
            inode = plus(inode, std::to_string(id.FileId.Identifier[i]));
        }
    }
    return {std::to_string(size), minus(scale(std::to_string(ticks), 2), "11644473600000000000"),
        inode, std::to_string(info.dwVolumeSerialNumber),
        GetFileType(h) == FILE_TYPE_DISK && !(info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)};
}
Metadata stat_path(const fs::path& source) {
    auto p = syscall_path(source);
    Handle h{CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (h.h == INVALID_HANDLE_VALUE) throw ContentError("cannot stat content");
    return metadata(h.h);
}
#else
struct Handle { int h = -1; ~Handle() { if (h >= 0) ::close(h); } };
Metadata metadata(const struct stat& s) {
    return {std::to_string(s.st_size), signed_plus(scale(std::to_string(s.st_mtim.tv_sec), 9), std::to_string(s.st_mtim.tv_nsec)),
        std::to_string(s.st_ino), std::to_string(s.st_dev), S_ISREG(s.st_mode)};
}
Metadata stat_path(const fs::path& source) {
    auto p = syscall_path(source); struct stat s{};
    if (::stat(p.c_str(), &s)) throw ContentError("cannot stat content");
    return metadata(s);
}
#endif
struct WalkDirectory { fs::path relative; std::vector<fs::path> ancestors; };
// Validation and walking deliberately remain separate passes. os.walk preserves
// scandir order for dirs+files in the former, including ignored subtrees.
std::vector<fs::path> walk(const fs::path& source, bool validate) {
    no_nul(source);
    auto root = syscall_path(source.empty() ? fs::path(".") : source);
    std::vector<fs::path> result;
    std::vector<WalkDirectory> pending{{{}, {}}};
    while (!pending.empty()) {
        auto item = std::move(pending.back()); pending.pop_back();
        auto directory = root / item.relative;
        if (!item.relative.empty() && is_link(directory)) continue;
        std::vector<fs::path> dirs, files;
        std::error_code ec;
        fs::directory_iterator it(directory, ec), end;
        for (; !ec && it != end; it.increment(ec)) {
            std::error_code kind_error;
            (fs::is_directory(it->path(), kind_error) ? dirs : files).push_back(it->path().filename());
        }
        if (ec) continue; // Interrupted scandir discards the directory, as os.walk.
        auto canonical = store_paths::resolve_native(directory);
        for (const auto& ancestor : item.ancestors) {
#ifdef _WIN32
            bool same = store_paths::windows_path_equal(store_paths::native_points(canonical), store_paths::native_points(ancestor));
#else
            bool same = canonical == ancestor;
#endif
            if (same) throw ContentError("content inventory directory cycle");
        }
        item.ancestors.push_back(std::move(canonical));
        if (validate) {
            std::set<std::u32string> seen;
            for (const auto* names : {&dirs, &files}) for (const auto& name : *names) {
                auto relative = item.relative / name;
                if (is_link(root / relative)) throw ContentError("content symlink requires explicit policy: " + compat::compact(string(relative_points(relative))));
                if (!seen.insert(schema::casefold(store_paths::native_points(name))).second)
                    throw ContentError("case-colliding content names: " + compat::compact(string(relative_points(relative))));
            }
        } else {
            dirs.erase(std::remove_if(dirs.begin(), dirs.end(), [&](const fs::path& p) { return is_link(directory / p); }), dirs.end());
            auto less = [](const fs::path& a, const fs::path& b) { return store_paths::native_points(a) < store_paths::native_points(b); };
            std::sort(dirs.begin(), dirs.end(), less); std::sort(files.begin(), files.end(), less);
            for (const auto& f : files) {
                auto p = directory / f;
                if (!is_link(p) && fs::is_regular_file(source_status(p))) result.push_back(item.relative / f);
            }
        }
        for (auto i = dirs.rbegin(); i != dirs.rend(); ++i) pending.push_back({item.relative / *i, item.ancestors});
    }
    return result;
}
const char* status_name(ReferenceStatus status) {
    switch (status) { case ReferenceStatus::found: return "found"; case ReferenceStatus::missing: return "missing";
        case ReferenceStatus::ambiguous: return "ambiguous"; case ReferenceStatus::unsafe: return "unsafe"; }
    throw ContentError("invalid reference status");
}
} // namespace
namespace content_internal {
fs::path native_units(std::u32string_view s) {
    for (auto c : s) {
        if (c > 0x10ffff) throw ContentError("invalid native code point");
#ifndef _WIN32
        if (c >= 0xd800 && c <= 0xdfff && !(c >= 0xdc80 && c <= 0xdcff)) throw ContentError("unencodable native code point");
#endif
    }
    return native_units_impl(s);
}
std::vector<fs::path> walk_files(const fs::path& root) {
    auto result = walk(root, false);
    for (auto& p : result) p = pathlib_spelling(root / p);
    return result;
}
std::vector<Member> content_inventory(const fs::path& root) {
    (void)walk(root, true);
    std::vector<Member> result;
    for (const auto& relative : walk(root, false)) {
        auto name = store_paths::native_points(relative.filename());
        auto dot = name.find_last_of(U'.');
        auto suffix = dot != name.npos && dot && dot + 1 < name.size() ? schema::lower(name.substr(dot)) : U"";
        if (suffix == U".sav" || suffix == U".sab" || suffix == U".log" || suffix == U".tmp" || suffix == U".bak") continue;
        bool ignored_parent = false;
        for (const auto& p : relative.parent_path()) {
            auto folded = schema::casefold(store_paths::native_points(p));
            if (folded == U"saves" || folded == U"logs" || folded == U"screenshots" || folded == U"cache") ignored_parent = true;
        }
        if (ignored_parent) continue;
        auto meta = stat_path((root.empty() ? fs::path(".") : root) / relative);
        result.push_back({relative_points(relative), meta.size, meta.modified, meta.inode});
    }
    std::sort(result.begin(), result.end(), [](const Member& a, const Member& b) { return a.relative < b.relative; });
    return result;
}
std::string hash_file(const fs::path& source) {
    auto p = syscall_path(source);
#ifdef _WIN32
    Handle h{CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (h.h == INVALID_HANDLE_VALUE) throw ContentError("cannot read content");
    auto before = metadata(h.h);
#else
    // Follow ordinary aliases like Python hash_file, but do not block on a FIFO
    // substituted after inventory. Owned handle must be regular before reading.
    Handle h{::open(p.c_str(), O_RDONLY | O_NONBLOCK | O_CLOEXEC)};
    if (h.h < 0) throw ContentError("cannot read content");
    struct stat st{};
    if (::fstat(h.h, &st)) throw ContentError("cannot stat content");
    auto before = metadata(st);
#endif
    if (!before.regular) throw ContentError("content is not a regular file");
    picosha2::hash256_one_by_one digest;
    std::vector<unsigned char> buffer(1024 * 1024);
    for (;;) {
#ifdef _WIN32
        DWORD got = 0;
        if (!ReadFile(h.h, buffer.data(), static_cast<DWORD>(buffer.size()), &got, nullptr)) throw ContentError("cannot read content");
#else
        auto got = ::read(h.h, buffer.data(), buffer.size());
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) throw ContentError("cannot read content");
#endif
        if (!got) break;
        digest.process(buffer.begin(), buffer.begin() + got);
    }
#ifdef _WIN32
    auto after = metadata(h.h);
#else
    if (::fstat(h.h, &st)) throw ContentError("cannot stat content");
    auto after = metadata(st);
#endif
    if (!before.same(after) || !after.same(stat_path(p))) throw ContentError("content changed while hashing");
    digest.finish(); return picosha2::get_hash_hex_string(digest);
}
Value inventory_value(const std::vector<Member>& members) {
    auto out = array();
    for (const auto& m : members) { auto row = array(); row.array = {string(m.relative), number(m.size), number(m.modified), number(m.inode)}; out.array.push_back(std::move(row)); }
    return out;
}
std::string fingerprint_payload(const fs::path& root, const std::function<void(FingerprintPhase)>& phase) {
    auto content = resolved_path(root, U"HUNTDAT");
    auto before = content_inventory(content); auto entries = array();
    if (phase) phase(FingerprintPhase::initial_inventory);
    for (const auto& member : before) {
        auto path = content / native_units(member.relative);
        auto digest = hash_file(path);
        if (phase) phase(FingerprintPhase::member_hashed);
        auto after = stat_path(path);
        if (member.size != after.size || member.modified != after.modified || member.inode != after.inode)
            throw ContentError("content changed while hashing: " + compat::compact(string(member.relative)));
        auto entry = array(); entry.array = {string(member.relative), number(member.size), string(ascii(digest))};
        entries.array.push_back(std::move(entry));
    }
    if (phase) phase(FingerprintPhase::final_inventory);
    if (before != content_inventory(content)) throw ContentError("content inventory changed while hashing; close content updaters");
    return compat::ContentFingerprintV1(entries);
}
} // namespace content_internal
fs::path native_path(const fs::path& source) {
    auto text = store_paths::native_points(source); auto windows = schema::path(text, true);
#ifdef _WIN32
    const bool reject = windows.drive.empty() != windows.root.empty();
#else
    const bool reject = !windows.drive.empty() || text.find(U'\\') != text.npos;
#endif
    if (reject) throw ContentError("foreign or ambiguous path; supply an explicit native location");
    return store_paths::resolve_root(source);
}
ReferenceObservation resolve_reference(const fs::path& source, std::u32string reference) {
    auto root = store_paths::resolve_native(source);
    auto normalized = reference; std::replace(normalized.begin(), normalized.end(), U'\\', U'/');
    auto parts = schema::path(normalized, false);
    ReferenceObservation result{std::move(reference), ReferenceStatus::unsafe, {}};
    if (parts.absolute() || parts.parent() || result.reference.find(U':') != result.reference.npos || parts.parts.empty()) return result;
    auto current = root; fs::path relative;
    for (const auto& component : parts.parts) {
        if (!fs::is_directory(source_status(current))) { result.status = ReferenceStatus::missing; return result; }
        std::vector<fs::path> matches;
        for (const auto& entry : fs::directory_iterator(current))
            if (schema::casefold(store_paths::native_points(entry.path().filename())) == schema::casefold(component)) matches.push_back(entry.path().filename());
        if (matches.size() != 1) { result.status = matches.empty() ? ReferenceStatus::missing : ReferenceStatus::ambiguous; return result; }
        current /= matches[0]; relative /= matches[0];
        if (!contained(root, store_paths::resolve_native(current))) return result;
    }
    result.status = ReferenceStatus::found; result.path = relative_points(relative); return result;
}
fs::path resolved_path(const fs::path& root, std::u32string reference) {
    auto result = resolve_reference(root, std::move(reference));
    if (result.status != ReferenceStatus::found) throw ContentError(compat::compact(string(result.reference)) + ": " + status_name(result.status));
    return pathlib_spelling(root / content_internal::native_units(*result.path));
}
std::string ReferenceObservation::export_json() const {
    auto out = object(); out.object = {{U"reference", string(reference)}, {U"status", string(ascii(status_name(status)))}};
    if (path) out.object.emplace_back(U"path", string(*path));
    return compat::display(out);
}
struct ContentFingerprint::Impl { std::string algorithm = "huntdat-sha256-v1", sha256, byte_count = "0"; std::size_t file_count = 0; };
ContentFingerprint::ContentFingerprint(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
const std::string& ContentFingerprint::algorithm() const noexcept { return impl_->algorithm; }
const std::string& ContentFingerprint::sha256() const noexcept { return impl_->sha256; }
std::size_t ContentFingerprint::file_count() const noexcept { return impl_->file_count; }
const std::string& ContentFingerprint::byte_count() const noexcept { return impl_->byte_count; }
ContentFingerprint fingerprint(const fs::path& root) {
    auto payload = content_internal::fingerprint_payload(root);
    auto entries = compat::parse(payload); auto result = std::make_shared<ContentFingerprint::Impl>();
    result->sha256 = c2::frontend::sha256(payload); result->file_count = entries.array.size();
    for (const auto& e : entries.array) result->byte_count = plus(result->byte_count, e.array[1].integer);
    return ContentFingerprint(result);
}
std::string ContentFingerprint::export_json() const {
    auto out = object(); out.object = {{U"algorithm", string(ascii(algorithm()))}, {U"sha256", string(ascii(sha256()))},
        {U"file_count", number(std::to_string(file_count()))}, {U"byte_count", number(byte_count())}};
    return compat::display(out);
}
} // namespace c2::frontend
