// Public API consumer; the parents mode alone tests the private path adapter.
#include "c2/frontend/store.hpp"
#include "store_paths.hpp"
#include <iostream>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace c2::frontend;
namespace {
void codepoints(const std::u32string& s) {
    for (auto c : s) std::cout << static_cast<unsigned>(c) << ',';
    std::cout << '\n';
}
int run(int argc, std::filesystem::path* argv) {
    try {
        if (argc < 3) return 9;
        auto mode = argv[1];
        if (mode == "default") { std::cout << Store::default_directory().u8string() << '\n'; return 0; }
        if (mode == "parents") {
            for (const auto& p : store_paths::ancestor_paths(argv[2])) std::cout << p.u8string() << '\n';
            return 0;
        }
        ReadPolicy policy;
        if (mode == "depth") policy.max_depth = 2048;
        if (mode == "bytes") policy.max_bytes = 64;
        auto path = argv[2];
        if (mode == "nul") {
            auto value = path.native(); value.push_back(0); value += path.native(); path = value;
        }
        Store store(path, policy);
        if (mode == "directory") { std::cout << store.directory().u8string() << '\n'; return 0; }
        if (mode == "hold") {
            std::cout << "ready\n" << std::flush;
            std::string signal; std::getline(std::cin, signal);
        }
        auto snapshot = store.read();
        if (mode == "read") { std::cout << "read\n"; return 0; }
        if (mode == "summary") {
            std::cout << snapshot.schema_version() << '\n' << snapshot.has_active_hunter() << '\n';
            codepoints(snapshot.active_hunter().value_or(U""));
            auto hunters = snapshot.hunters(); auto expeditions = snapshot.expeditions();
            std::cout << hunters.size() << '\n';
            for (const auto& h : hunters) { codepoints(h.id); codepoints(h.name); std::cout << h.archived << '\n'; }
            std::cout << expeditions.size() << '\n';
            for (const auto& e : expeditions) { codepoints(e.id); codepoints(e.mode); codepoints(e.path_flavor); codepoints(e.path); }
            return 0;
        }
        if (mode == "owned") {
            std::cout << "ready\n" << std::flush;
            std::string signal; std::getline(std::cin, signal);
        }
        auto copy = snapshot;
        std::cout << copy.export_json(ManifestView::status);
        return 0;
    } catch (const ResourceExhausted& e) { std::cerr << "resource: " << e.what() << '\n'; return 3; }
      catch (const std::exception& e) { std::cerr << "error: " << e.what() << '\n'; return 2; }
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#else
int main(int argc, char** argv) {
#endif
    std::vector<std::filesystem::path> paths;
    for (int i = 0; i < argc; ++i) paths.emplace_back(argv[i]);
    return run(argc, paths.data());
}
