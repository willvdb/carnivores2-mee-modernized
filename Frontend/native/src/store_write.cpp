#include "store_write.hpp"
#include "manifest_access.hpp"
#include "manifest_schema.hpp"
#include "schema_compat.hpp"
#include "store_paths.hpp"
#include <algorithm>
#include <cerrno>
#include <chrono>
#include <ctime>
#include <set>
#include <system_error>
#include <type_traits>
#include <utility>
#ifdef _WIN32
#define NOMINMAX
#include <windows.h>
#include <bcrypt.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#ifdef __linux__
#include <sys/random.h>
#endif
#endif
namespace c2::frontend::store_write {
namespace fs = std::filesystem;
using compat::Kind;
using compat::Value;
namespace {
#ifdef _WIN32
std::error_code last_error() { return {static_cast<int>(GetLastError()), std::system_category()}; }
// Move-only owner: a copy would let two destructors close one HANDLE and a
// later write reach an unrelated reopened handle. Never rely on elision.
struct Handle {
    HANDLE value = INVALID_HANDLE_VALUE;
    Handle() = default;
    explicit Handle(HANDLE h) noexcept : value(h) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.value) { other.value = INVALID_HANDLE_VALUE; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { close(); value = other.value; other.value = INVALID_HANDLE_VALUE; }
        return *this;
    }
    bool open() const { return value != INVALID_HANDLE_VALUE; }
    void close() { if (open()) { CloseHandle(value); value = INVALID_HANDLE_VALUE; } }
    ~Handle() { close(); }
};
// Generalized UTF-8 for diagnostics: unpaired surrogates never throw.
std::string display_path(const fs::path& path) {
    std::string out;
    for (auto c : store_paths::native_points(path)) {
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) { out.push_back(static_cast<char>(0xc0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
        else if (c < 0x10000) { out.push_back(static_cast<char>(0xe0 | (c >> 12))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
        else { out.push_back(static_cast<char>(0xf0 | (c >> 18))); out.push_back(static_cast<char>(0x80 | ((c >> 12) & 63))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
    }
    return out;
}
void random_bytes(unsigned char* out, std::size_t n) {
    if (BCryptGenRandom(nullptr, out, static_cast<ULONG>(n), BCRYPT_USE_SYSTEM_PREFERRED_RNG) != 0)
        throw StoreError("system random source unavailable");
}
std::u32string hostname() {
    // socket.gethostname(): GetComputerNameExW(ComputerNameDnsHostname).
    DWORD size = 0;
    GetComputerNameExW(ComputerNameDnsHostname, nullptr, &size);
    std::wstring name(size, L'\0');
    if (!GetComputerNameExW(ComputerNameDnsHostname, name.data(), &size)) throw std::system_error(last_error(), "gethostname");
    name.resize(size);
    return store_paths::native_points(fs::path(name));
}
std::string pid() { return std::to_string(GetCurrentProcessId()); }
#else
std::error_code last_error() { return {errno, std::system_category()}; }
// Move-only owner: a copy would let two destructors close one descriptor and
// a later write reach an unrelated reused descriptor. Never rely on elision.
struct Handle {
    int value = -1;
    Handle() = default;
    explicit Handle(int fd) noexcept : value(fd) {}
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
    Handle(Handle&& other) noexcept : value(other.value) { other.value = -1; }
    Handle& operator=(Handle&& other) noexcept {
        if (this != &other) { close(); value = other.value; other.value = -1; }
        return *this;
    }
    bool open() const { return value >= 0; }
    void close() { if (open()) { ::close(value); value = -1; } }
    ~Handle() { close(); }
};
std::string display_path(const fs::path& path) { return path.native(); }
void random_bytes(unsigned char* out, std::size_t n) {
    std::size_t offset = 0;
#ifdef __linux__
    while (offset < n) {
        auto got = ::getrandom(out + offset, n - offset, 0);
        if (got < 0 && errno == EINTR) continue;
        if (got < 0) break;  // ENOSYS on old kernels: use the device below.
        offset += static_cast<std::size_t>(got);
    }
    if (offset == n) return;
#endif
    Handle device(::open("/dev/urandom", O_RDONLY | O_CLOEXEC));
    if (!device.open()) throw StoreError("system random source unavailable");
    while (offset < n) {
        auto got = ::read(device.value, out + offset, n - offset);
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) throw StoreError("system random source unavailable");
        offset += static_cast<std::size_t>(got);
    }
}
std::u32string hostname() {
    char name[1024];
    if (::gethostname(name, sizeof name) != 0) throw std::system_error(last_error(), "gethostname");
    name[sizeof name - 1] = '\0';
    return store_paths::native_points(fs::path(name));
}
std::string pid() { return std::to_string(::getpid()); }
void fsync_directory(const fs::path& directory) {
    Handle h(::open(directory.c_str(), O_RDONLY | O_CLOEXEC));
    if (!h.open()) throw fs::filesystem_error("cannot open directory", directory, last_error());
    if (::fsync(h.value) != 0) throw fs::filesystem_error("cannot fsync directory", directory, last_error());
}
#endif
static_assert(!std::is_copy_constructible_v<Handle> && !std::is_copy_assignable_v<Handle>, "handles are move-only");
static_assert(std::is_nothrow_move_constructible_v<Handle> && std::is_nothrow_move_assignable_v<Handle>, "handles move without throwing");
void fire(const FailureHook& hook, WritePhase phase, const fs::path& path) { if (hook) hook(phase, path); }
void write_all(Handle& h, std::string_view content, const fs::path& path) {
    std::size_t offset = 0;
    while (offset < content.size()) {
#ifdef _WIN32
        DWORD amount = static_cast<DWORD>(std::min<std::size_t>(content.size() - offset, 1u << 30)), wrote = 0;
        if (!WriteFile(h.value, content.data() + offset, amount, &wrote, nullptr))
            throw fs::filesystem_error("cannot write", path, last_error());
#else
        auto wrote = ::write(h.value, content.data() + offset, content.size() - offset);
        if (wrote < 0 && errno == EINTR) continue;
        if (wrote < 0) throw fs::filesystem_error("cannot write", path, last_error());
#endif
        offset += static_cast<std::size_t>(wrote);
    }
}
void fsync_file(Handle& h, const fs::path& path) {
#ifdef _WIN32
    if (!FlushFileBuffers(h.value)) throw fs::filesystem_error("cannot fsync", path, last_error());
#else
    if (::fsync(h.value) != 0) throw fs::filesystem_error("cannot fsync", path, last_error());
#endif
}
void unlink_quietly(const fs::path& path) noexcept {
    std::error_code ec;
    fs::remove(path, ec);
}
// tempfile.mkstemp(prefix=".pending-", dir=parent): exclusive create, 0600.
Handle create_temporary(const fs::path& parent, fs::path& temporary) {
    static constexpr char alphabet[] = "abcdefghijklmnopqrstuvwxyz0123456789_";
    for (int attempt = 0; attempt < 10000; ++attempt) {
        unsigned char raw[8];
        random_bytes(raw, sizeof raw);
        std::string name = ".pending-";
        for (auto b : raw) name.push_back(alphabet[b % 37]);
        auto candidate = parent / name;
#ifdef _WIN32
        Handle h(CreateFileW(candidate.c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (h.open()) { temporary = candidate; return h; }
        auto ec = last_error();
        if (ec.value() != ERROR_FILE_EXISTS && ec.value() != ERROR_ALREADY_EXISTS)
            throw fs::filesystem_error("cannot create temporary", candidate, ec);
#else
        Handle h(::open(candidate.c_str(), O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600));
        if (h.open()) { temporary = candidate; return h; }
        if (errno != EEXIST) throw fs::filesystem_error("cannot create temporary", candidate, last_error());
#endif
    }
    throw fs::filesystem_error("No usable temporary file name found", parent, std::make_error_code(std::errc::file_exists));
}
Value text(std::u32string s) { Value v; v.kind = Kind::string; v.string = std::move(s); return v; }
Value ascii(std::string_view s) { return text(std::u32string(s.begin(), s.end())); }
Value integer(std::string s) { Value v; v.kind = Kind::integer; v.integer = std::move(s); return v; }
fs::path units(const std::u32string& points) {
#ifdef _WIN32
    std::wstring out;
    for (auto c : points) {
        if (c > 0xffff) { c -= 0x10000; out.push_back(static_cast<wchar_t>(0xd800 + (c >> 10))); out.push_back(static_cast<wchar_t>(0xdc00 + (c & 1023))); }
        else out.push_back(static_cast<wchar_t>(c));
    }
    return fs::path(out);
#else
    // Inverse of native_points: escaped bytes return, other code points are UTF-8.
    std::string out;
    for (auto c : points) {
        if (c >= 0xdc80 && c <= 0xdcff) out.push_back(static_cast<char>(c - 0xdc00));
        else if (c >= 0xd800 && c <= 0xdfff) throw StoreError("unencodable captured state path");
        else if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) { out.push_back(static_cast<char>(0xc0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
        else if (c < 0x10000) { out.push_back(static_cast<char>(0xe0 | (c >> 12))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
        else { out.push_back(static_cast<char>(0xf0 | (c >> 18))); out.push_back(static_cast<char>(0x80 | ((c >> 12) & 63))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
    }
    return fs::path(out);
#endif
}
std::string two(int v) { return (v < 10 ? "0" : "") + std::to_string(v); }
}
std::string isoformat_utc(std::int64_t seconds, unsigned microseconds) {
    std::time_t t = static_cast<std::time_t>(seconds);
    std::tm parts{};
#ifdef _WIN32
    if (gmtime_s(&parts, &t) != 0) throw StoreError("cannot format UTC timestamp");
#else
    if (!gmtime_r(&t, &parts)) throw StoreError("cannot format UTC timestamp");
#endif
    int year = parts.tm_year + 1900;
    if (year < 1 || year > 9999 || microseconds >= 1000000) throw StoreError("cannot format UTC timestamp");
    std::string y = std::to_string(year);
    y.insert(0, 4 - y.size(), '0');
    std::string out = y + "-" + two(parts.tm_mon + 1) + "-" + two(parts.tm_mday) + "T" + two(parts.tm_hour) + ":" +
                      two(parts.tm_min) + ":" + two(parts.tm_sec);
    if (microseconds) {
        std::string fraction = std::to_string(microseconds);
        out += "." + std::string(6 - fraction.size(), '0') + fraction;
    }
    return out + "+00:00";
}
std::string now() {
    using namespace std::chrono;
    const auto point = system_clock::now();
    const auto since = duration_cast<microseconds>(point.time_since_epoch()).count();
    // Floor division keeps the fraction non-negative before 1970 as well.
    auto seconds = since / 1000000, fraction = since % 1000000;
    if (fraction < 0) { fraction += 1000000; --seconds; }
    return isoformat_utc(static_cast<std::int64_t>(seconds), static_cast<unsigned>(fraction));
}
std::string new_id() {
    unsigned char raw[16];
    random_bytes(raw, sizeof raw);
    raw[6] = static_cast<unsigned char>((raw[6] & 0x0f) | 0x40);
    raw[8] = static_cast<unsigned char>((raw[8] & 0x3f) | 0x80);
    static constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (std::size_t i = 0; i < 16; ++i) {
        if (i == 4 || i == 6 || i == 8 || i == 10) out.push_back('-');
        out.push_back(digits[raw[i] >> 4]);
        out.push_back(digits[raw[i] & 15]);
    }
    return out;
}
void atomic_write(const fs::path& path, std::string_view content, const FailureHook& hook) {
    const auto parent = path.parent_path().empty() ? fs::path(".") : path.parent_path();
    fs::path temporary;
    fire(hook, WritePhase::temp_create, parent);
    Handle h = create_temporary(parent, temporary);
    bool pending = true;
    try {
        fire(hook, WritePhase::temp_write, temporary);
        write_all(h, content, temporary);
        fire(hook, WritePhase::temp_fsync, temporary);
        fsync_file(h, temporary);
        h.close();
        fire(hook, WritePhase::replace, path);
#ifdef _WIN32
        // os.replace: MoveFileExW with MOVEFILE_REPLACE_EXISTING, no retry.
        if (!MoveFileExW(temporary.c_str(), path.c_str(), MOVEFILE_REPLACE_EXISTING))
            throw fs::filesystem_error("cannot replace", temporary, path, last_error());
        pending = false;
#else
        if (::rename(temporary.c_str(), path.c_str()) != 0)
            throw fs::filesystem_error("cannot replace", temporary, path, last_error());
        pending = false;
        fire(hook, WritePhase::directory_fsync, parent);
        fsync_directory(parent);
#endif
    } catch (...) {
        h.close();
        if (pending) unlink_quietly(temporary);
        throw;
    }
}
WriterLock::WriterLock(const fs::path& directory, const FailureHook& hook) : path_(directory / "lodge.lock") {
    fs::create_directories(directory);
#ifdef _WIN32
    Handle h(CreateFileW(path_.c_str(), GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, CREATE_NEW,
                         FILE_ATTRIBUTE_NORMAL, nullptr));
    if (!h.open()) {
        auto ec = last_error();
        if (ec.value() == ERROR_FILE_EXISTS || ec.value() == ERROR_ALREADY_EXISTS)
            throw StoreError("frontend writer lock exists: " + display_path(path_) + "; verify its owner before manual recovery");
        throw fs::filesystem_error("cannot create writer lock", path_, ec);
    }
#else
    Handle h(::open(path_.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600));
    if (!h.open()) {
        if (errno == EEXIST)
            throw StoreError("frontend writer lock exists: " + display_path(path_) + "; verify its owner before manual recovery");
        throw fs::filesystem_error("cannot create writer lock", path_, last_error());
    }
#endif
    held_ = true;
    try {
        Value info; info.kind = Kind::object;
        info.object.emplace_back(U"pid", integer(pid()));
        info.object.emplace_back(U"host", text(hostname()));
        info.object.emplace_back(U"created_at", ascii(now()));
        const auto content = compat::dumps(info, false);
        fire(hook, WritePhase::lock_write, path_);
        write_all(h, content, path_);
        fire(hook, WritePhase::lock_fsync, path_);
        fsync_file(h, path_);
        h.close();
    } catch (...) {
        // The reference's finally still unlinks its own lock; the original
        // failure is reported rather than any secondary unlink failure.
        h.close();
        unlink_quietly(path_);
        held_ = false;
        throw;
    }
}
WriterLock::~WriterLock() { if (held_) unlink_quietly(path_); }
void WriterLock::release() {
    if (!held_) return;
    held_ = false;
    std::error_code ec;
    if (!fs::remove(path_, ec) || ec) throw fs::filesystem_error("cannot remove writer lock", path_, ec ? ec : std::make_error_code(std::errc::no_such_file_or_directory));
}
bool transaction(const Store& store, const std::function<void(Value&)>& mutate, const FailureHook& hook) {
    WriterLock lock(store.directory(), hook);
    const auto manifest = store.read();
    const auto& policy = ManifestAccess::policy(manifest);
    Value data = ManifestAccess::data(manifest);
    bool written = false;
    try {
        const auto before = compat::dumps(data, true, policy.max_depth);
        mutate(data);
        schema::validate_manifest(data);
        const auto path = store.directory() / "lodge.json";
        const bool exists = fs::exists(path);
        if (!exists || compat::dumps(data, true, policy.max_depth) != before) {
            if (exists) {
                auto current = store_paths::read(path, policy);
                if (!current) throw StoreError("cannot read manifest");
                atomic_write(store.directory() / "lodge.json.bak", *current, hook);
            }
            atomic_write(path, compat::display(data, policy.max_depth), hook);
            written = true;
        }
    } catch (const compat::ResourceError& e) { throw ResourceExhausted(e.what()); }
      catch (const std::bad_alloc&) { throw ResourceExhausted("manifest transaction allocation exhausted"); }
      catch (const std::length_error&) { throw ResourceExhausted("manifest transaction size exhausted"); }
      catch (const compat::Error& e) { throw StoreError(e.what()); }
    lock.release();
    return written;
}
fs::path session_root(const Store& store, std::u32string_view identity) {
    if (!schema::valid_id(identity)) throw StoreError("invalid session UUID");
    auto root = store.directory() / "sessions" / fs::path(std::string(identity.begin(), identity.end()));
    store_paths::safe_path(root);
    return root;
}
void write_blobs(const fs::path& directory, const std::vector<CapturedBlob>& blobs, const FailureHook& hook) {
    store_paths::safe_path(directory);
    fs::create_directories(directory);
    std::set<fs::path> directories{directory, directory.parent_path()};
    for (const auto& blob : blobs) {
#ifdef _WIN32
        constexpr bool nt = true;
#else
        constexpr bool nt = false;
#endif
        const auto parsed = schema::path(blob.path, nt);
        if (parsed.absolute() || parsed.parent() || blob.path.find(U'\\') != std::u32string::npos ||
            blob.path.find(U':') != std::u32string::npos)
            throw StoreError("unsafe captured state path");
        // PurePath join: a rooted drive-less NT name replaces the directory
        // tail exactly as the reference does; parts are already normalized.
        auto path = directory;
        if (!parsed.root.empty()) path /= units(parsed.drive + parsed.root);
        for (const auto& part : parsed.parts) path /= units(part);
        store_paths::safe_path(path);
        fs::create_directories(path.parent_path());
        atomic_write(path, blob.bytes, hook);
        if (parsed.root.empty()) {
            auto ancestor = directory;
            for (std::size_t i = 0; i + 1 < parsed.parts.size(); ++i) { ancestor /= units(parsed.parts[i]); directories.insert(ancestor); }
        }
    }
#ifndef _WIN32
    // File replacement fsyncs its own directory. Also persist every new
    // nested directory entry, deepest first.
    std::vector<fs::path> ordered(directories.begin(), directories.end());
    std::stable_sort(ordered.begin(), ordered.end(), [](const fs::path& a, const fs::path& b) {
        return std::distance(a.begin(), a.end()) > std::distance(b.begin(), b.end());
    });
    for (const auto& path : ordered) {
        store_paths::safe_path(path);
        fire(hook, WritePhase::blob_directory_fsync, path);
        fsync_directory(path);
    }
#else
    (void)hook;
#endif
}
}
