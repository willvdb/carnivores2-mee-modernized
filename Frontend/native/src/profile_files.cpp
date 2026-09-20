#include "c2/frontend/profile_files.hpp"
#include "c2/frontend/core.hpp"
#include "store_paths.hpp"
#include "profile_source.hpp"
#include "schema_compat.hpp"
#include <algorithm>
#include <array>
#include <cerrno>
#include <map>
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
constexpr std::size_t max_state_bytes = 16 * 1024 * 1024;
#include "profile_unicode.inc"
Value string(std::u32string s) { Value v; v.kind = Kind::string; v.string = std::move(s); return v; }
Value number(std::string s) { Value v; v.kind = Kind::integer; v.integer = std::move(s); return v; }
Value object() { Value v; v.kind = Kind::object; return v; }
Value array() { Value v; v.kind = Kind::array; return v; }
std::u32string ascii(std::string_view s) { return {s.begin(), s.end()}; }
Value diagnostic_value(const ProfileDiagnostic& d) {
    auto v = object(); v.object = {{U"code", string(d.code)}, {U"message", string(d.message)}};
    if (d.path) v.object.emplace_back(U"path", string(*d.path));
    return v;
}
Value diagnostics_value(const std::vector<ProfileDiagnostic>& ds) {
    auto v = array(); for (const auto& d : ds) v.array.push_back(diagnostic_value(d)); return v;
}
Value file_value(const ProfileFile& f) {
    auto v = object(); v.object = {{U"path", string(f.path)}, {U"kind", string(f.kind)},
        {U"size", number(std::to_string(f.size))}}; return v;
}
Value state_value(const ProfileState& s) {
    auto files = array(), companions = array();
    for (const auto& f : s.files()) files.array.push_back(file_value(f));
    for (const auto& f : s.companions()) {
        auto v = object(); v.object = {{U"path", string(f.path)}, {U"size", number(std::to_string(f.size))}};
        companions.array.push_back(std::move(v));
    }
    auto v = object(); v.object = {{U"key", string(s.key())}, {U"filename_slot", number(s.filename_slot())},
        {U"files", std::move(files)}, {U"ownership", string(U"unclaimed")},
        {U"diagnostics", diagnostics_value(s.diagnostics())}, {U"unclassified_companions", std::move(companions)}};
    return v;
}
void add(std::vector<ProfileDiagnostic>& ds, std::u32string code, std::u32string message,
         std::optional<std::u32string> path = {}) { ds.push_back({std::move(code), std::move(message), std::move(path)}); }
int decimal(char32_t c) {
    auto end = std::upper_bound(std::begin(decimal_starts), std::end(decimal_starts), c);
    if (end == std::begin(decimal_starts)) return -1;
    auto d = c - *--end; return d < 10 ? static_cast<int>(d) : -1;
}
// Python re.IGNORECASE against these ASCII literals. The only non-ASCII
// equivalence in trophy/sav/sab is long-s; lower() deliberately retains it.
char32_t regex_lower(char32_t c) {
    if (c >= U'A' && c <= U'Z') return c + 32;
    return c == 0x17f ? U's' : c;
}
struct Name { std::u32string stem, kind; std::string slot; bool state = false; };
std::optional<Name> classify(std::u32string_view name) {
    if (name.size() < 9) return {};
    constexpr std::u32string_view prefix = U"trophy";
    for (std::size_t i = 0; i < prefix.size(); ++i) if (regex_lower(name[i]) != prefix[i]) return {};
    std::size_t end = 6; std::string slot;
    for (; end < name.size() && decimal(name[end]) >= 0; ++end) slot.push_back(static_cast<char>('0' + decimal(name[end])));
    if (end == 6 || end + 1 >= name.size() || name[end] != U'.') return {};
    auto ext = name.substr(end + 1);
    // (.+) does not match newline, even with anchors/fullmatch.
    if (ext.find(U'\n') != ext.npos) return {};
    auto first = slot.find_first_not_of('0'); slot = first == slot.npos ? "0" : slot.substr(first);
    const bool state = ext.size() == 3 && regex_lower(ext[0]) == U's' && regex_lower(ext[1]) == U'a' &&
        (regex_lower(ext[2]) == U'v' || regex_lower(ext[2]) == U'b');
    return Name{schema::casefold(name.substr(0, end)), schema::lower(ext), std::move(slot), state};
}
std::u32string relative_points(const fs::path& relative) {
    auto text = store_paths::native_points(relative);
#ifdef _WIN32
    std::replace(text.begin(), text.end(), U'\\', U'/');
#endif
    return text;
}
fs::path native_relative(std::u32string_view points) {
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
bool ignored(const std::error_code& ec) {
#ifdef _WIN32
    if (ec.category() == std::system_category() && (ec.value() == 21 || ec.value() == 123 || ec.value() == 1921)) return true;
#endif
    return ec == std::errc::no_such_file_or_directory || ec == std::errc::not_a_directory ||
        ec == std::errc::bad_file_descriptor || ec == std::errc::too_many_symbolic_link_levels;
}
fs::file_status source_status(const fs::path& p) {
    std::error_code ec; auto s = fs::status(p, ec);
    if (ec && !ignored(ec)) throw fs::filesystem_error("cannot inspect profile path", p, ec);
    return s;
}
bool is_link(const fs::path& p) {
#ifdef _WIN32
    WIN32_FIND_DATAW data{};
    auto handle = FindFirstFileW(p.c_str(), &data);
    if (handle == INVALID_HANDLE_VALUE) {
        const std::error_code ec(static_cast<int>(GetLastError()), std::system_category());
        if (!ignored(ec)) throw fs::filesystem_error("cannot inspect profile path", p, ec);
        return false;
    }
    FindClose(handle);
    // pathlib.is_symlink excludes junctions, which os.walk does traverse.
    return (data.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) && data.dwReserved0 == IO_REPARSE_TAG_SYMLINK;
#else
    std::error_code ec; auto s = fs::symlink_status(p, ec);
    if (ec && !ignored(ec)) throw fs::filesystem_error("cannot inspect profile path", p, ec);
    return fs::is_symlink(s);
#endif
}
struct WalkFile { fs::path relative; std::uintmax_t size; };
std::vector<WalkFile> walk(const fs::path& root) {
    std::vector<WalkFile> result;
    struct Directory { fs::path relative; std::vector<fs::path> ancestors; };
    std::vector<Directory> pending{{fs::path{}, {}}};
    while (!pending.empty()) {
        auto directory = std::move(pending.back()); pending.pop_back();
        const auto& relative = directory.relative;
        // os.walk checks islink again immediately before descending. The caller
        // root itself may be a link; only discovered descendants are excluded.
        if (!relative.empty() && is_link(root / relative)) continue;
        std::vector<fs::path> files, dirs;
        std::error_code ec;
        fs::directory_iterator it(root / relative, ec), end;
        // os.walk ignores scandir errors when no onerror is supplied.
        for (; !ec && it != end; it.increment(ec)) {
            const auto p = it->path();
            std::error_code kind_error;
            // DirEntry.is_dir errors classify as nondirectory in os.walk.
            if (fs::is_directory(p, kind_error)) dirs.push_back(p.filename());
            else files.push_back(p.filename());
        }
        if (ec) continue; // An interrupted scandir discards the whole directory.
        // Operational guard against repeated junction traversal. This is local
        // to the current ancestry, so finite sibling aliases are not deduplicated.
        // Check only after successful scandir: its permission omission remains.
        auto canonical = store_paths::resolve_native(root / relative);
        for (const auto& ancestor : directory.ancestors) {
#ifdef _WIN32
            const bool same = store_paths::windows_path_equal(store_paths::native_points(canonical), store_paths::native_points(ancestor));
#else
            const bool same = canonical == ancestor;
#endif
            if (same) throw ProfileError("profile inventory directory cycle");
        }
        directory.ancestors.push_back(std::move(canonical));
        dirs.erase(std::remove_if(dirs.begin(), dirs.end(), [&](const fs::path& d) { return is_link(root / relative / d); }), dirs.end());
        auto less = [](const fs::path& a, const fs::path& b) { return store_paths::native_points(a) < store_paths::native_points(b); };
        std::sort(files.begin(), files.end(), less); std::sort(dirs.begin(), dirs.end(), less);
        for (const auto& f : files) {
            auto p = relative / f;
            // stat occurs only for regex candidates in Python; unrelated files
            // need no additional observation (and no size failure).
            if (!is_link(root / p) && fs::is_regular_file(source_status(root / p)) && classify(store_paths::native_points(f)))
                result.push_back({p, fs::file_size(root / p)});
        }
        for (auto d = dirs.rbegin(); d != dirs.rend(); ++d) pending.push_back({relative / *d, directory.ancestors});
    }
    return result;
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
} // namespace
namespace profile_source {
std::string read_member(const fs::path& root, const fs::path& relative) {
    const auto p = root / relative;
    if (is_link(p) || !contained(root, store_paths::resolve_native(p))) throw ProfileError("state member escaped its root");
#ifdef _WIN32
    struct Handle { HANDLE h; ~Handle() { if (h != INVALID_HANDLE_VALUE) CloseHandle(h); } };
    Handle handle{CreateFileW(p.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (handle.h == INVALID_HANDLE_VALUE) throw ProfileError("cannot read state member");
    BY_HANDLE_FILE_INFORMATION before{};
    if (!GetFileInformationByHandle(handle.h, &before) || GetFileType(handle.h) != FILE_TYPE_DISK ||
        (before.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY))
        throw ProfileError("state member is not a regular file");
#else
    struct Handle { int h; ~Handle() { if (h >= 0) ::close(h); } };
    Handle handle{::open(p.c_str(), O_RDONLY | O_NONBLOCK | O_NOFOLLOW | O_CLOEXEC)};
    if (handle.h < 0) throw ProfileError("cannot read state member");
    struct stat before{};
    if (::fstat(handle.h, &before) || !S_ISREG(before.st_mode)) throw ProfileError("state member is not a regular file");
#endif
    std::string bytes; std::array<char, 65536> buffer{};
    // Read actual bytes, including virtual files whose reported extent is zero.
    while (bytes.size() <= max_state_bytes) {
        auto n = static_cast<unsigned>(std::min(buffer.size(), max_state_bytes + 1 - bytes.size()));
#ifdef _WIN32
        DWORD got = 0;
        if (!ReadFile(handle.h, buffer.data(), n, &got, nullptr)) throw ProfileError("cannot read state member");
#else
        auto got = ::read(handle.h, buffer.data(), n);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) throw ProfileError("cannot read state member");
#endif
        if (!got) break;
        bytes.append(buffer.data(), static_cast<std::size_t>(got));
    }
    if (bytes.size() > max_state_bytes) throw ProfileError("state member exceeds conservative inspection limit");
#ifdef _WIN32
    BY_HANDLE_FILE_INFORMATION after{}, current{};
    Handle now{CreateFileW(p.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
    if (now.h == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(now.h, &current) ||
        before.dwVolumeSerialNumber != current.dwVolumeSerialNumber || before.nFileIndexHigh != current.nFileIndexHigh ||
        before.nFileIndexLow != current.nFileIndexLow || !GetFileInformationByHandle(handle.h, &after) || before.nFileSizeHigh != after.nFileSizeHigh ||
        before.nFileSizeLow != after.nFileSizeLow || CompareFileTime(&before.ftLastWriteTime, &after.ftLastWriteTime))
        throw ProfileError("native state changed while reading");
#else
    struct stat after{}, current{};
    if (::fstat(handle.h, &after) || ::stat(p.c_str(), &current) || before.st_dev != current.st_dev || before.st_ino != current.st_ino ||
        before.st_size != after.st_size || before.st_mtim.tv_sec != after.st_mtim.tv_sec || before.st_mtim.tv_nsec != after.st_mtim.tv_nsec)
        throw ProfileError("native state changed while reading");
#endif
    if (is_link(p) || !contained(root, store_paths::resolve_native(p))) throw ProfileError("state member escaped its root");
    return bytes;
}
}
struct ProfileState::Impl {
    fs::path root;
    std::u32string key;
    std::string slot;
    std::vector<ProfileFile> files;
    std::vector<ProfileCompanion> companions;
    std::vector<ProfileDiagnostic> diagnostics;
};
struct ProfileInventory::Impl { fs::path root; std::vector<ProfileState> states; };
struct ProfileSetInspection::Impl {
    explicit Impl(ProfileState s) : state(std::move(s)) {}
    ProfileState state;
    std::vector<InspectedProfileFile> files;
    std::vector<ProfileBlob> blobs;
    std::vector<ProfileDiagnostic> diagnostics;
};
ProfileState::ProfileState(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
ProfileInventory::ProfileInventory(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
ProfileSetInspection::ProfileSetInspection(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
const fs::path& ProfileState::root() const noexcept { return impl_->root; }
const std::u32string& ProfileState::key() const noexcept { return impl_->key; }
const std::string& ProfileState::filename_slot() const noexcept { return impl_->slot; }
const std::vector<ProfileFile>& ProfileState::files() const noexcept { return impl_->files; }
const std::vector<ProfileCompanion>& ProfileState::companions() const noexcept { return impl_->companions; }
const std::vector<ProfileDiagnostic>& ProfileState::diagnostics() const noexcept { return impl_->diagnostics; }
const fs::path& ProfileInventory::root() const noexcept { return impl_->root; }
const std::vector<ProfileState>& ProfileInventory::states() const noexcept { return impl_->states; }
const ProfileState& ProfileSetInspection::state() const noexcept { return impl_->state; }
const std::vector<InspectedProfileFile>& ProfileSetInspection::files() const noexcept { return impl_->files; }
const std::vector<ProfileBlob>& ProfileSetInspection::blobs() const noexcept { return impl_->blobs; }
const std::vector<ProfileDiagnostic>& ProfileSetInspection::diagnostics() const noexcept { return impl_->diagnostics; }
std::string ProfileState::export_json() const { return compat::display(state_value(*this)); }
std::string ProfileInventory::export_json() const {
    auto v = array(); for (const auto& s : states()) v.array.push_back(state_value(s)); return compat::display(v);
}
ProfileInventory inventory_profiles(const fs::path& source) {
    auto result = std::make_shared<ProfileInventory::Impl>();
    // Check before any native C-string API (including Windows absolute()).
    if (source.native().find(fs::path::value_type{}) != fs::path::string_type::npos) {
        result->root = source; return ProfileInventory(result);
    }
    // Binding to cwd must not collapse link/.. or expand literal '~'.
    result->root = fs::absolute(source.empty() ? fs::path(".") : source);
    if (!fs::is_directory(source_status(result->root))) return ProfileInventory(result);
    std::map<std::u32string, std::shared_ptr<ProfileState::Impl>> groups;
    std::map<std::u32string, std::vector<ProfileCompanion>> companions;
    for (const auto& f : walk(result->root)) {
        auto name = *classify(store_paths::native_points(f.relative.filename()));
        auto parent = relative_points(f.relative.parent_path());
        auto key = parent.empty() ? name.stem : parent + U"/" + name.stem;
        auto path = relative_points(f.relative);
        if (!name.state) { companions[key].push_back({path, f.size}); continue; }
        auto& item = groups[key];
        if (!item) { item = std::make_shared<ProfileState::Impl>(); item->root = result->root; item->key = key; item->slot = name.slot; }
        item->files.push_back({path, name.kind, f.size});
    }
    for (auto& group : groups) {
        auto& s = *group.second;
        s.companions = std::move(companions[s.key]);
        if (!s.companions.empty()) add(s.diagnostics, U"unclassified-companion", U"Other files share the slot basename; relationship is unknown. Managed import requires a companion policy.");
        unsigned saves = 0, rooms = 0;
        for (const auto& f : s.files) { saves += f.kind == U"sav"; rooms += f.kind == U"sab"; }
        if (saves > 1 || rooms > 1) add(s.diagnostics, U"ambiguous-state-files", U"Case-colliding state members; no file chosen.");
        if (!saves) add(s.diagnostics, U"orphan-companion", U"Room has no corresponding save; retained without repair.");
        if (s.slot.size() != 1 || s.slot[0] > '7') add(s.diagnostics, U"slot-outside-menu", U"Outside evidenced eight-slot menu range.");
        auto cut = s.key.find_last_of(U'/'); auto base = cut == s.key.npos ? s.key : s.key.substr(cut + 1);
        if (base != U"trophy" + ascii(s.slot.size() < 2 ? "0" + s.slot : s.slot))
            add(s.diagnostics, U"noncanonical-slot-name", U"Filename differs from menu convention.");
        if (cut != s.key.npos) add(s.diagnostics, U"non-root-state", U"Packaged/example/backup candidate; not an active root slot.");
        add(s.diagnostics, U"ownership-unproven", U"Discovery cannot distinguish user saves from packaged/example state.");
        result->states.push_back(ProfileState(group.second));
    }
    return ProfileInventory(result);
}
std::vector<ProfileBlob> ProfileState::read() const {
    for (const auto& d : diagnostics()) if (d.code == U"ambiguous-state-files") throw ProfileError("ambiguous native state file names");
    auto resolved = store_paths::resolve_native(root());
    std::vector<ProfileBlob> result;
    for (const auto& f : files()) result.push_back({f.path, profile_source::read_member(resolved, native_relative(f.path))});
    return result;
}
std::vector<ProfileBlob> ProfileState::stable_read() const {
    auto first = read();
    auto again = inventory_profiles(root());
    auto found = std::find_if(again.states().begin(), again.states().end(), [&](const ProfileState& s) { return s.key() == key(); });
    std::set<std::u32string> paths, new_paths;
    for (const auto& f : first) paths.insert(f.path);
    if (found != again.states().end()) for (const auto& f : found->files()) new_paths.insert(f.path);
    if (found == again.states().end() || paths != new_paths) throw ProfileError("state membership changed during snapshot");
    const auto& a = companions(); const auto& b = found->companions();
    if (a.size() != b.size() || !std::equal(a.begin(), a.end(), b.begin(), [](const ProfileCompanion& x, const ProfileCompanion& y) { return x.path == y.path && x.size == y.size; }))
        throw ProfileError("unclassified companion inventory changed during snapshot");
    auto second = found->read();
    // Dict equality in Python ignores insertion order, so compare by path.
    std::map<std::u32string, std::string_view> bytes;
    for (const auto& f : second) bytes.emplace(f.path, f.bytes);
    for (const auto& f : first) if (bytes.at(f.path) != f.bytes) throw ProfileError("native state changed during snapshot; close legacy writers");
    return first;
}
ProfileSetInspection ProfileState::inspect(ProfileCodec codec, std::string_view dialect) const {
    auto result = std::make_shared<ProfileSetInspection::Impl>(*this);
    result->blobs = stable_read(); result->diagnostics = diagnostics();
    for (std::size_t i = 0; i < files().size(); ++i) {
        const auto& bytes = result->blobs[i].bytes;
        // The pure codec distinguishes only exact 'sav'; Unicode long-s kinds
        // remain non-save just as in Python (no silently normalized extension).
        auto decoded = inspect_profile_bytes(bytes, files()[i].kind == U"sav" ? "sav" : "sab", codec, dialect);
        auto file = files()[i]; file.size = bytes.size();
        if (decoded.save() && std::to_string(decoded.save()->registration) != filename_slot())
            add(result->diagnostics, U"registration-mismatch", U"Filename slot disagrees with embedded registration; no normalization performed.");
        if (!decoded.codec_roundtrip_exact()) add(result->diagnostics, U"unreadable-layout", U"Preserved as opaque bytes; no save compatibility claim.", file.path);
        result->files.push_back({std::move(file), sha256(bytes), std::move(decoded)});
    }
    add(result->diagnostics, U"pair-coherence-unverified", U"Two equal observations cannot certify an externally updated SAV/SAB transaction.");
    return ProfileSetInspection(result);
}
std::string ProfileSetInspection::export_json() const {
    auto v = state_value(state()), fs_value = array();
    for (const auto& f : files()) {
        auto item = file_value(f.file);
        item.object.emplace_back(U"sha256", string(ascii(f.sha256)));
        item.object.emplace_back(U"decoded", compat::parse(f.decoded.export_json()));
        fs_value.array.push_back(std::move(item));
    }
    for (auto& field : v.object) {
        if (field.first == U"files") field.second = std::move(fs_value);
        else if (field.first == U"diagnostics") field.second = diagnostics_value(diagnostics());
    }
    return compat::display(v);
}
}
