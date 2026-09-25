// Test-only driver for store operations (workstream B); disposable stores only.
// Usage: driver <op> <store>  with a JSON argument object on stdin. Output is
// one line: "ok <compact json>", "frontend <message>", "oserror <errno>",
// "invalid <message>" or "resource <message>". C2_TEST_WRITE_FAILURE=phase or
// phase:filename installs a store_write failure hook (tests only).
#include "store_ops.hpp"
#include "c2/frontend/content.hpp"
#include "c2/frontend/profile.hpp"
#include "content_internal.hpp"
#include "planning_internal.hpp"
#include "probe_process.hpp"
#include <cstdlib>
#include <iostream>
#include <iterator>
#include <map>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace c2::frontend;
namespace fs = std::filesystem;
namespace {
const std::map<std::string, store_write::WritePhase> phases{
    {"temp_create", store_write::WritePhase::temp_create}, {"temp_write", store_write::WritePhase::temp_write},
    {"temp_fsync", store_write::WritePhase::temp_fsync}, {"replace", store_write::WritePhase::replace},
    {"directory_fsync", store_write::WritePhase::directory_fsync}, {"lock_write", store_write::WritePhase::lock_write},
    {"lock_fsync", store_write::WritePhase::lock_fsync}, {"blob_directory_fsync", store_write::WritePhase::blob_directory_fsync}};
store_write::FailureHook failure_hook() {
    const char* spec = std::getenv("C2_TEST_WRITE_FAILURE");
    if (!spec || !*spec) return {};
    std::string text = spec, name;
    if (auto cut = text.find(':'); cut != std::string::npos) { name = text.substr(cut + 1); text.resize(cut); }
    const auto phase = phases.at(text);
    return [phase, name](store_write::WritePhase current, const fs::path& target) {
        if (current == phase && (name.empty() || target.filename() == fs::u8path(name)))
            throw fs::filesystem_error("injected " + std::string(name.empty() ? "write" : name) + " failure", target,
                                       std::make_error_code(std::errc::io_error));
    };
}
std::string utf8(const std::u32string& s) {
    std::string out;
    for (char32_t c : s) {
        if (c < 0x80) out.push_back(static_cast<char>(c));
        else if (c < 0x800) { out.push_back(static_cast<char>(0xc0 | (c >> 6))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
        else if (c < 0x10000) { out.push_back(static_cast<char>(0xe0 | (c >> 12))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
        else { out.push_back(static_cast<char>(0xf0 | (c >> 18))); out.push_back(static_cast<char>(0x80 | ((c >> 12) & 63))); out.push_back(static_cast<char>(0x80 | ((c >> 6) & 63))); out.push_back(static_cast<char>(0x80 | (c & 63))); }
    }
    return out;
}
// Absent or null argument members are std::nullopt; strings are code points.
std::optional<std::u32string> text(const compat::Value& args, std::u32string_view key) {
    if (!args.contains(key) || args.at(key).kind == compat::Kind::null) return std::nullopt;
    return args.at(key).string;
}
std::optional<fs::path> path(const compat::Value& args, std::u32string_view key) {
    auto s = text(args, key);
    if (!s) return std::nullopt;
    return content_internal::native_units(*s);
}
std::u32string required(const compat::Value& args, std::u32string_view key) {
    auto s = text(args, key);
    if (!s) throw std::logic_error("driver argument missing");
    return *s;
}
compat::Value transaction(const Store& store, const std::function<compat::Value(compat::Value&)>& op) {
    compat::Value result;
    store_write::transaction(store, [&](compat::Value& data) { result = op(data); }, failure_hook());
    return result;
}
}
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc != 3) return 2;
    const std::string op = argv[1];
    try {
        const std::string content((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
        const auto args = compat::parse(content.empty() ? "{}" : content);
        const Store store(fs::u8path(argv[2]));
        compat::Value result;
        if (op == "hunter") {
            result = transaction(store, [&](compat::Value& data) {
                return store_ops::hunter(data, required(args, U"action"), text(args, U"id"), text(args, U"name"));
            });
        } else if (op == "host-settings") {
            const std::string json = utf8(required(args, U"json"));
            result = transaction(store, [&](compat::Value& data) { return store_ops::update_host_settings(data, json); });
        } else if (op == "recover-backup") {
            store_ops::restore_backup(store, failure_hook());
            result = planning_internal::object_value();
            result.object.emplace_back(U"result", planning_internal::ascii_value("backup-restored"));
        } else if (op == "register") {
            result = transaction(store, [&](compat::Value& data) {
                return store_ops::register_instance(data, *path(args, U"path"), text(args, U"mode").value_or(U"registered"),
                    text(args, U"dialect").value_or(U"unknown"), text(args, U"family"), text(args, U"release"), path(args, U"managed_root"));
            });
        } else if (op == "relocate") {
            result = transaction(store, [&](compat::Value& data) { return store_ops::relocate(data, required(args, U"id"), *path(args, U"path")); });
        } else if (op == "refresh") {
            result = transaction(store, [&](compat::Value& data) { return store_ops::refresh_instance(data, required(args, U"id")); });
        } else if (op == "discover") {
            result = store_ops::discover_view(store.read(), *path(args, U"path"));
        } else if (op == "discover-register") {
            result = transaction(store, [&](compat::Value& data) { return store_ops::discover_register(data, *path(args, U"path")); });
        } else return 2;
        std::cout << "ok " << compat::compact(result) << '\n';
    } catch (const ResourceExhausted& e) { std::cout << "resource " << e.what() << '\n';
    } catch (const StoreError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const ContentError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const ProfileError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const probe_process::ProbeError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const fs::filesystem_error& e) { std::cout << "oserror " << e.code().value() << '\n';
    } catch (const std::invalid_argument& e) { std::cout << "invalid " << e.what() << '\n'; }
    return 0;
}
