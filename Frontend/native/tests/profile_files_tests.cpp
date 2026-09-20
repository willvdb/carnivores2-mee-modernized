// Test-only public API driver; synchronization is outside the production API.
#include "c2/frontend/profile_files.hpp"
#include "store_paths.hpp"
#include "profile_source.hpp"
#include <iostream>
#include <type_traits>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace c2::frontend;
static_assert(!std::is_default_constructible_v<ProfileState>);
static_assert(!std::is_default_constructible_v<ProfileInventory>);
static_assert(!std::is_default_constructible_v<ProfileSetInspection>);
static_assert(std::is_const_v<std::remove_reference_t<decltype(std::declval<ProfileState>().files())>>);
static void wait() { std::cout << "ready\n" << std::flush; std::string s; std::getline(std::cin, s); }
static void blobs(const std::vector<ProfileBlob>& values) {
    for (const auto& b : values) { std::cout << b.bytes.size() << '\n'; std::cout.write(b.bytes.data(), b.bytes.size()); }
}
static int run(std::vector<std::filesystem::path> args) {
    try {
        if (args.size() < 3) return 9;
        auto mode = args[1];
        if (mode == "source-read") {
            auto value = profile_source::read_member(store_paths::resolve_native(args[2]), args.at(3));
            std::cout << value.size() << '\n' << value; return 0;
        }
        if (mode == "resolve-nul") {
            auto invalid = args[2]; invalid += std::filesystem::path::string_type(1, 0);
            try { (void)store_paths::resolve_native(invalid); }
            catch (const StoreError&) { return 0; }
            return 8;
        }
        if (mode == "nul") args[2] += std::filesystem::path::string_type(1, 0) + args[2].native();
        auto inventory = inventory_profiles(args[2]);
        if (mode == "inventory" || mode == "nul") { std::cout << inventory.export_json(); return 0; }
        if (args.size() < 4) return 9;
        auto key = store_paths::native_points(args[3]);
        auto state = [&] {
            for (const auto& s : inventory.states()) if (s.key() == key) return s;
            throw ProfileError("missing state");
        }();
        auto original = state.export_json();
        // State survives destruction of the original inventory and source args.
        inventory = inventory_profiles(args[2] / "nonexistent-child");
        args[2].clear();
        if (!state.root().is_absolute() || state.filename_slot().empty()) return 8;
        if (mode == "wait-stable" || mode == "wait-read" || mode == "wait-inspect") wait();
        if (mode == "cwd") std::filesystem::current_path(args.at(4));
        if (mode == "read" || mode == "wait-read") blobs(state.read());
        else if (mode == "stable" || mode == "wait-stable" || mode == "cwd") blobs(state.stable_read());
        else {
            auto result = state.inspect(args.size() > 4 && args[4] == "native" ? ProfileCodec::native : ProfileCodec::unavailable,
                args.size() > 5 && args[5] == "iceage-triassic" ? "iceage-triassic" : "unknown");
            auto copy = result;
            if (copy.state().export_json() != original || copy.files().size() != state.files().size()) return 8;
            if (mode == "owned") wait();
            auto json = copy.export_json();
            // Framing keeps exact presentation and complete raw bytes independent.
            std::cout << json.size() << '\n' << json;
            blobs(copy.blobs());
        }
        if (state.export_json() != original) return 8;
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 2; }
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#else
int main(int argc, char** argv) {
#endif
    std::vector<std::filesystem::path> args;
    for (int i = 0; i < argc; ++i) args.emplace_back(argv[i]);
    return run(std::move(args));
}
