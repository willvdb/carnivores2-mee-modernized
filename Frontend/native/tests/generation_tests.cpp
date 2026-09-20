// Test-only driver for the public immutable API and private capture prerequisite.
#include "c2/frontend/store.hpp"
#include "capture.hpp"
#include "store_paths.hpp"
#include <iostream>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace c2::frontend;
namespace {
void wait() { std::cout << "ready\n" << std::flush; std::string signal; std::getline(std::cin, signal); }
void output(const std::vector<CapturedEntry>& entries, const std::vector<CapturedBlob>& blobs) {
    Capture c; c.entries = entries;
    std::cout << compat::display(c.entry_value());
    // Length framed raw bytes: compare complete bytes, not another digest mirror.
    for (const auto& b : blobs) { std::cout << b.bytes.size() << '\n'; std::cout.write(b.bytes.data(), b.bytes.size()); }
}
int run(const std::vector<std::filesystem::path>& args) {
    try {
        if (args.size() < 3) return 9;
        auto mode = args[1];
        if (mode == "capture") {
            auto c = capture(args[2]); output(c.entries, c.blobs); return 0;
        }
        if (args.size() < 4) return 9;
        auto manifest = Store(args[2]).read();
        if (mode == "manifest-owned") wait();
        std::optional<std::u32string> identity;
        if (args.size() > 4) identity = store_paths::native_points(args[4]);
        auto observation = manifest.resolve_generation(store_paths::native_points(args[3]), identity);
        if (mode == "owned") wait();
        auto copy = observation;
        if (mode == "generation") std::cout << copy.export_generation_json();
        else if (mode == "history") std::cout << copy.export_history_json();
        else {
            if (copy.id().empty() || !copy.root().is_absolute()) return 8;
            output(copy.entries(), copy.blobs());
        }
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
    std::vector<std::filesystem::path> args;
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    return run(args);
}
