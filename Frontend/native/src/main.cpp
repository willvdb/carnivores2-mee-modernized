#include "c2/frontend/core.hpp"
#include "c2/frontend/store.hpp"
#include "json_compat.hpp"
#include "store_paths.hpp"
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
namespace {
using namespace c2::frontend;
void diagnostic(const char* message) {
    // Decode the UTF-8 diagnostic through the same lossless private codec;
    // quote controls first so paths/OS messages cannot break the JSON envelope.
    std::string quoted = "\"";
    constexpr char hex[] = "0123456789abcdef";
    for (const unsigned char c : std::string_view(message)) {
        if (c == '"' || c == '\\') { quoted += '\\'; quoted += static_cast<char>(c); }
        else if (c < 32) { quoted += "\\u00"; quoted += hex[c >> 4]; quoted += hex[c & 15]; }
        else quoted += static_cast<char>(c);
    }
    quoted += '"';
    try {
        compat::Value error; error.kind = compat::Kind::object;
        error.object.emplace_back(U"error", compat::parse(quoted));
        std::cerr << compat::compact(error) << '\n';
    } catch (...) { std::cerr << "{\"error\":\"native diagnostic unavailable\"}\n"; }
}
int run(const std::vector<std::filesystem::path>& args) {
    if (args.size() == 1 && args[0] == "--version") {
        std::cout << "c2-frontend-native " << version() << '\n'; return 0;
    }
    if (args.empty() || (args.size() == 1 && args[0] == "--help")) {
        std::cout << "Usage: c2-frontend-native [--store PATH] [--probe PATH] COMMAND\n"
                     "Read-only commands: status, hunter list, expedition list, host-settings, managed-state inspect ASSOCIATION\n"
                     "Options: --help, --version\n";
        return 0;
    }
    try {
        std::optional<std::filesystem::path> directory;
        std::size_t i = 0;
        for (; i < args.size(); ++i) {
            auto option = args[i].native();
            auto equal = option.find('=');
            auto key = std::filesystem::path(option.substr(0, equal));
            if (key != "--store" && key != "--probe") break;
            std::filesystem::path value;
            if (equal != option.npos) value = option.substr(equal + 1);
            else {
                if (++i == args.size()) throw StoreError("option requires a path");
                value = args[i];
                if (!value.empty() && value.native().front() == '-') throw StoreError("option requires a path; use = for a leading dash");
            }
            if (key == "--store") directory = std::move(value);
        }
        ManifestView view = ManifestView::status;
        std::optional<std::u32string> association;
        if (i + 1 == args.size() && args[i] == "status") view = ManifestView::status;
        else if (i + 1 == args.size() && args[i] == "host-settings") view = ManifestView::host_settings;
        else if (i + 2 == args.size() && args[i + 1] == "list" && args[i] == "hunter") view = ManifestView::hunters;
        else if (i + 2 == args.size() && args[i + 1] == "list" && args[i] == "expedition") view = ManifestView::expeditions;
        else if (i + 3 == args.size() && args[i] == "managed-state" && args[i + 1] == "inspect")
            association = store_paths::native_points(args[i + 2]);
        else throw StoreError("unsupported command; use --help");
        Store store(directory ? *directory : Store::default_directory());
        auto manifest = store.read();
        auto output = association ? manifest.resolve_generation(*association).export_history_json() : manifest.export_json(view);
        std::cout << output;
        return 0;
    } catch (const ResourceExhausted& e) {
        diagnostic(e.what()); return 3;
    } catch (const std::exception& e) {
        diagnostic(e.what()); return 2;
    }
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    std::vector<std::filesystem::path> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return run(args);
}
#else
int main(int argc, char** argv) {
    std::vector<std::filesystem::path> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return run(args);
}
#endif
