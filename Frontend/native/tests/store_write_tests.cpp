// Test-only driver for the private write primitives; authored mutations only.
#include "c2/frontend/store.hpp"
#include "capture.hpp"
#include "manifest_schema.hpp"
#include "store_paths.hpp"
#include "store_write.hpp"
#include <cstdlib>
#include <iostream>
#include <map>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace c2::frontend;
namespace fs = std::filesystem;
namespace {
struct Injected : std::runtime_error { using std::runtime_error::runtime_error; };
struct Callback : std::runtime_error { using std::runtime_error::runtime_error; };
const std::map<std::string, store_write::WritePhase> phases{
    {"temp_create", store_write::WritePhase::temp_create}, {"temp_write", store_write::WritePhase::temp_write},
    {"temp_fsync", store_write::WritePhase::temp_fsync}, {"replace", store_write::WritePhase::replace},
    {"directory_fsync", store_write::WritePhase::directory_fsync}, {"lock_write", store_write::WritePhase::lock_write},
    {"lock_fsync", store_write::WritePhase::lock_fsync}, {"blob_directory_fsync", store_write::WritePhase::blob_directory_fsync}};
std::string phase_name(store_write::WritePhase phase) {
    for (const auto& entry : phases) if (entry.second == phase) return entry.first;
    return "?";
}
std::string line() { std::string s; std::getline(std::cin, s); if (!s.empty() && s.back() == '\r') s.pop_back(); return s; }
std::string bytes(std::size_t n) { std::string s(n, '\0'); std::cin.read(s.data(), static_cast<std::streamsize>(n)); return s; }
void wait() { std::cout << "ready\n" << std::flush; line(); }
// Options: fail=PHASE (throw at first occurrence), fail-second=PHASE (throw at
// the second occurrence), hold=PHASE (print ready and wait once), trace.
struct Options {
    std::string fail, fail_second, hold; bool trace = false; int seen = 0;
    store_write::FailureHook hook() {
        if (fail.empty() && fail_second.empty() && hold.empty() && !trace) return {};
        return [this](store_write::WritePhase phase, const fs::path& path) {
            auto name = phase_name(phase);
            if (trace) std::cout << "phase " << name << ' ' << path.u8string() << '\n' << std::flush;
            if (name == fail) throw Injected("injected " + name);
            if (name == fail_second && ++seen == 2) throw Injected("injected second " + name);
            if (name == hold) { hold.clear(); wait(); }
        };
    }
};
compat::Value& locate(compat::Value& data, const compat::Value& keys, std::size_t depth) {
    // Navigate through objects (string keys) and arrays (integer indexes).
    compat::Value* current = &data;
    for (std::size_t i = 0; i < depth; ++i) {
        const auto& key = keys.array[i];
        if (current->kind == compat::Kind::object) {
            if (key.kind != compat::Kind::string) throw Callback("authored key kind");
            bool found = false;
            for (auto& member : current->object) if (member.first == key.string) { current = &member.second; found = true; break; }
            if (!found) throw Callback("authored key missing");
        } else if (current->kind == compat::Kind::array) {
            if (key.kind != compat::Kind::integer) throw Callback("authored index kind");
            current = &current->array.at(static_cast<std::size_t>(std::stoul(key.integer)));
        } else throw Callback("authored path into scalar");
    }
    return *current;
}
void apply(compat::Value& data, const compat::Value& ops) {
    for (const auto& op : ops.array) {
        const auto& name = op.array.at(0).string;
        if (name == U"throw") throw Callback("authored callback failure");
        if (name == U"replace") { data = op.array.at(1); continue; }
        const auto& keys = op.array.at(1);
        auto& parent = locate(data, keys, keys.array.size() - 1);
        const auto& last = keys.array.back();
        if (name == U"set") {
            if (parent.kind == compat::Kind::object) {
                for (auto& member : parent.object) if (member.first == last.string) { member.second = op.array.at(2); goto next; }
                parent.object.emplace_back(last.string, op.array.at(2));
            } else if (parent.kind == compat::Kind::array) {
                auto index = static_cast<std::size_t>(std::stoul(last.integer));
                if (index == parent.array.size()) parent.array.push_back(op.array.at(2));
                else parent.array.at(index) = op.array.at(2);
            } else throw Callback("authored set into scalar");
        } else if (name == U"del") {
            if (parent.kind == compat::Kind::object) {
                for (auto it = parent.object.begin(); it != parent.object.end(); ++it)
                    if (it->first == last.string) { parent.object.erase(it); goto next; }
                throw Callback("authored delete missing");
            } else parent.array.erase(parent.array.begin() + static_cast<std::ptrdiff_t>(std::stoul(last.integer)));
        } else throw Callback("unknown authored op");
        next:;
    }
}
void output_capture(const Capture& c) {
    std::cout << compat::display(c.entry_value());
    for (const auto& b : c.blobs) { std::cout << b.bytes.size() << '\n'; std::cout.write(b.bytes.data(), b.bytes.size()); }
}
int run(const std::vector<fs::path>& args) {
    try {
        if (args.size() < 2) return 9;
        const auto mode = args[1].string();
        Options options;
        for (std::size_t i = 2; i < args.size(); ++i) {
            auto a = args[i].string();
            if (a.rfind("fail=", 0) == 0) options.fail = a.substr(5);
            else if (a.rfind("fail-second=", 0) == 0) options.fail_second = a.substr(12);
            else if (a.rfind("hold=", 0) == 0) options.hold = a.substr(5);
            else if (a == "trace") options.trace = true;
        }
        if (mode == "now") { std::cout << store_write::now() << '\n'; return 0; }
        if (mode == "isoformat") {
            std::cout << store_write::isoformat_utc(std::stoll(args.at(2).string()), static_cast<unsigned>(std::stoul(args.at(3).string()))) << '\n';
            return 0;
        }
        if (mode == "new-id") { for (auto n = std::stoul(args.at(2).string()); n; --n) std::cout << store_write::new_id() << '\n'; return 0; }
        if (mode == "valid-id") {
            auto list = compat::parse(bytes(static_cast<std::size_t>(std::stoul(line()))));
            for (const auto& v : list.array) std::cout << (v.kind == compat::Kind::string && schema::valid_id(v.string)) << '\n';
            return 0;
        }
        if (mode == "dumps") {
            auto value = compat::parse(bytes(static_cast<std::size_t>(std::stoul(line()))));
            std::cout << compat::dumps(value, args.at(2).string() == "1") << '\n';
            return 0;
        }
        if (mode == "atomic-write") {
            auto content = bytes(static_cast<std::size_t>(std::stoul(line())));
            store_write::atomic_write(args.at(2), content, options.hook());
            std::cout << "ok\n"; return 0;
        }
        if (mode == "lock") {
            store_write::WriterLock lock(args.at(2), options.hook());
            bool hold = false;
            for (std::size_t i = 3; i < args.size(); ++i) if (args[i] == "hold") hold = true;
            if (hold) wait();
            lock.release();
            std::cout << "released\n"; return 0;
        }
        if (mode == "transaction") {
            auto ops = compat::parse(bytes(static_cast<std::size_t>(std::stoul(line()))));
            Store store(args.at(2));
            bool written = store_write::transaction(store, [&](compat::Value& data) { apply(data, ops); }, options.hook());
            std::cout << "written " << written << '\n'; return 0;
        }
        if (mode == "session-root") {
            auto identity = compat::parse(bytes(static_cast<std::size_t>(std::stoul(line()))));
            std::cout << store_write::session_root(Store(args.at(2)), identity.string).u8string() << '\n'; return 0;
        }
        if (mode == "write-blobs") {
            auto header = compat::parse(bytes(static_cast<std::size_t>(std::stoul(line()))));
            std::vector<CapturedBlob> blobs;
            for (const auto& item : header.array)
                blobs.push_back({item.array.at(0).string, bytes(static_cast<std::size_t>(std::stoul(item.array.at(1).integer)))});
            store_write::write_blobs(args.at(2), blobs, options.hook());
            std::cout << "ok\n"; return 0;
        }
        if (mode == "capture") { output_capture(capture(args.at(2))); return 0; }
        return 9;
    } catch (const Injected& e) { std::cerr << "injected: " << e.what() << '\n'; return 4; }
      catch (const Callback& e) { std::cerr << "callback: " << e.what() << '\n'; return 6; }
      catch (const ResourceExhausted& e) { std::cerr << "resource: " << e.what() << '\n'; return 3; }
      catch (const fs::filesystem_error& e) { std::cerr << "oserror: " << e.what() << '\n'; return 5; }
      catch (const std::system_error& e) { std::cerr << "oserror: " << e.what() << '\n'; return 5; }
      catch (const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; return 2; }
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#else
int main(int argc, char** argv) {
#endif
    std::vector<fs::path> args;
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    return run(args);
}
