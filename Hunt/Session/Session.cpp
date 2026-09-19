#include "Session.h"
#include "../../Shared/LegacyProfile.h"
#include "../Platform/Files.h"
#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <map>
#include <set>
#include <stdexcept>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

namespace EngineSession {
namespace {
namespace fs = std::filesystem;
bool active = false, ready = false;
std::atomic<bool> failed{false};
int slot = -1;
fs::path root;
std::string sav, sab;
std::string Fold(std::string value) {
    for (auto& c : value) if (c >= 'A' && c <= 'Z') c += 'a' - 'A';
    return value;
}
bool Same(const fs::path& a, const fs::path& b) {
#ifdef _WIN32
    return Fold(a.generic_string()) == Fold(b.generic_string());
#else
    return a == b;
#endif
}
bool Within(const fs::path& child, const fs::path& parent) {
    auto c = child.begin();
    for (auto p = parent.begin(); p != parent.end(); ++p, ++c)
        if (c == child.end() || !Same(*c, *p)) return false;
    return true;
}
bool Overlap(const fs::path& a, const fs::path& b) { return Within(a,b) || Within(b,a); }
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
void Components(const fs::path& path) {
    Require(!path.empty(), "empty session path");
    for (const auto& part : path.relative_path()) {
        const auto text = part.string();
        Require(!text.empty() && text != "." && text != ".." &&
                text.find(':') == std::string::npos && text.back() != '.' && text.back() != ' ',
                "unsafe session path component");
    }
}
// Inspect every ancestor, including Windows junction/reparse points. The caller
// must own a quiescent workspace: this is not protection against racing attackers.
void Safe(const fs::path& path, bool missingLeaf = false) {
    Require(path.is_absolute(), "session paths must be absolute");
    Components(path);
    fs::path current = path.root_path();
    const auto relative = path.relative_path();
    for (auto it = relative.begin(); it != relative.end(); ++it) {
        current /= *it;
        std::error_code ec;
        const auto status = fs::symlink_status(current, ec);
        auto next = it; ++next;
        if (status.type() == fs::file_type::not_found && missingLeaf && next == relative.end()) return;
        Require(!ec && !fs::is_symlink(status), "missing or linked session path");
#ifdef _WIN32
        const auto attributes = GetFileAttributesW(current.c_str());
        Require(attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT),
                "session path contains a reparse point");
#endif
        Require(fs::is_directory(status) || fs::is_regular_file(status), "special session file rejected");
        if (fs::is_regular_file(status)) Require(fs::hard_link_count(current) == 1, "hard-linked session file rejected");
        Require(Same(fs::canonical(current), current), "session path alias rejected");
    }
}
fs::path Directory(const std::string& value) {
    fs::path path(Platform::NormalizePath(value));
    Safe(path);
    Require(fs::is_directory(path), "session root must be an existing directory");
    return path;
}
std::vector<std::uint8_t> Bytes(const fs::path& path, std::size_t size) {
    Safe(path);
    Require(fs::file_size(path) == size, "session profile has invalid length");
    std::ifstream stream(path, std::ios::binary);
    std::vector<std::uint8_t> bytes(size);
    Require(bool(stream.read(reinterpret_cast<char*>(bytes.data()), bytes.size())), "session profile cannot be read");
    Require(stream.peek() == std::char_traits<char>::eof(), "session profile changed while reading");
    return bytes;
}
bool Profile(const void* data, std::size_t size, bool room) {
    const auto* bytes = static_cast<const std::uint8_t*>(data);
    if (room) {
        LegacyProfile::Room value;
        return size == LegacyProfile::RoomSize && LegacyProfile::DecodeRoom(bytes, size, value);
    }
    LegacyProfile::Save value;
    return size == LegacyProfile::SaveSize && LegacyProfile::DecodeSave(bytes, size, value) &&
           value.profile.header.registration == slot;
}
void Members(const fs::path& directory, const std::set<std::string>& allowed, bool exact) {
    std::set<std::string> found;
    for (const auto& entry : fs::directory_iterator(directory)) {
        Safe(entry.path());
        const auto name = entry.path().filename().string();
        Require(allowed.count(name) != 0, "unexpected session directory member");
        found.insert(name);
    }
    Require(!exact || found == allowed, "incomplete session directory");
}
}
bool Active() { return active; }
int Slot() { return slot; }
void Fail() { if (active) failed = true; }
bool Check(bool success) { if (!success) Fail(); return success; }
int ExitStatus(int status) { return status ? status : (active && failed ? 3 : 0); }
std::string ConfigPath() { return (root / "config/config.cfg").string(); }
bool ValidateProfile(const void* bytes, std::size_t size, bool room) {
    return !active || Check(ready && Profile(bytes, size, room));
}
Startup Initialize(const std::vector<std::string>& arguments, const std::string& contentDirectory,
                   const std::string& moduleDirectory, std::vector<std::string>& legacy, std::string& error) {
    active = ready = false; failed = false; slot = -1; root.clear(); error.clear();
    legacy.clear();
    std::map<std::string,std::string> options;
    try {
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            const auto& arg = arguments[i];
            const auto lower = Fold(arg);
            if (i == 0 || lower.find("session-") == std::string::npos) { legacy.push_back(arg); continue; }
            active = true; // malformed attempted session launches fail closed too
            const auto equal = arg.find('=');
            const auto key = arg.substr(0, equal);
            Require(key == "--session-contract" || key == "--session-root" || key == "--session-slot" ||
                    key == "--session-source" || key == "--session-baseline" || key == "--session-capabilities",
                    "unknown or ambiguous session option");
            Require(options.emplace(key, equal == std::string::npos ? "" : arg.substr(equal+1)).second,
                    "duplicate session option");
            if (key == "--session-capabilities") Require(equal == std::string::npos, "capability query takes no value");
            else Require(equal != std::string::npos && equal+1 < arg.size(), "session options require =value");
        }
        if (!active) return Startup::Legacy;
        if (options.count("--session-capabilities")) {
            Require(options.size() == 1 && legacy.size() == 1, "capability query must be used alone");
            return Startup::Query;
        }
        Require(options.size() == 5 && options["--session-contract"] == "1", "incomplete or unsupported session contract");
        const auto number = options["--session-slot"];
        Require(number.size() == 1 && number[0] >= '0' && number[0] <= '7', "session slot must be 0..7");
        slot = number[0] - '0';
        sav = "trophy0" + number + ".sav"; sab = "trophy0" + number + ".sab";
        for (std::size_t i = 1; i < legacy.size(); ++i) {
            const auto& arg = legacy[i];
            const auto lower = Fold(arg);
            Require(lower.find("reg=") == std::string::npos, "reg= is ambiguous with session-slot");
            Require(lower.find("-multiplayer") == std::string::npos && lower.find("-host") == std::string::npos &&
                    lower.find("server=") == std::string::npos, "session v1 is single-player only");
            if (lower.find("prj=") != std::string::npos) {
                Require(arg.rfind("prj=", 0) == 0 && arg.size() > 4 && arg.size() < 120, "ambiguous session project");
                const fs::path project(Platform::NormalizePath(arg.substr(4)));
                Components(project);
                Require(!project.is_absolute() && !project.has_root_name(), "session project must be content-relative");
            }
        }
        root = Directory(options["--session-root"]);
        const auto source = Directory(options["--session-source"]);
        const auto baseline = Directory(options["--session-baseline"]);
        for (const auto& protectedRoot : {Directory(contentDirectory), Directory(moduleDirectory), source, baseline})
            Require(!Overlap(root, protectedRoot), "writable session overlaps protected root");
        Require(!Overlap(source, baseline), "source and baseline must be independent");
        Members(root, {"state", "config", "output"}, true);
        for (const auto* name : {"state", "config", "output"}) Directory((root / name).string());
        Members(root / "state", {sav, sab}, true);
        Members(baseline, {sav, sab}, true);
        Members(root / "config", {"config.cfg"}, false);
        Members(root / "output", {}, true);
        if (fs::exists(root / "config/config.cfg")) {
            Require(fs::is_regular_file(root / "config/config.cfg") && fs::file_size(root / "config/config.cfg") <= 1024*1024,
                    "invalid session config");
        }
        for (const auto& name : {sav, sab}) {
            const bool room = name == sab;
            const auto size = room ? LegacyProfile::RoomSize : LegacyProfile::SaveSize;
            const auto original = Bytes(source / name, size);
            Require(Profile(original.data(), original.size(), room), "invalid session profile or registration");
            Require(Bytes(baseline / name, size) == original && Bytes(root / "state" / name, size) == original,
                    "source, baseline and session state differ");
        }
        legacy.push_back("reg=" + number);
        ready = true;
        return Startup::Ready;
    } catch (const std::exception& e) {
        active = true; failed = true; error = e.what(); return Startup::Error;
    }
}
bool Resolve(const std::string& value, bool write, std::string& resolved) {
    if (!active) { resolved = value; return true; }
    try {
        Require(ready, "session policy is not ready");
        const fs::path path(Platform::NormalizePath(value));
        const auto name = path.filename().string();
        const auto lower = Fold(name);
        const bool profile = Fold(path.extension().string()) == ".sav" || Fold(path.extension().string()) == ".sab";
        const bool config = lower == "config.cfg";
        if (!write && !profile && !config) { resolved = value; return true; }
        Components(path);
        fs::path target;
        if (profile) {
            Require(lower == sav || lower == sab, "unselected profile access");
            target = root / "state" / lower;
        } else if (config) target = root / "config/config.cfg";
        else target = root / "output" / name;
        Require((!path.has_parent_path() && !path.has_root_name()) || Same(path, target), "output path escape");
        Safe(target, !profile); // never invent missing profile members
        if (fs::exists(target)) Require(fs::is_regular_file(target), "output is not a regular file");
        resolved = target.string();
        return true;
    } catch (const std::exception&) { Fail(); return false; }
}
}
