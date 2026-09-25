// Test-only driver for store-backed planning; disposable stores only.
#include "planning_store.hpp"
#include "probe_process.hpp"
#include <iostream>
#include <string>
#include <iterator>
#include "planning_internal.hpp"
using namespace c2::frontend;
namespace fs = std::filesystem;
int main(int argc, char** argv) {
    if (argc < 5) return 2;
    try {
        if (std::string(argv[1]) == "snapshot" && argc == 8) {
            const std::string content((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
            const auto args = compat::parse(content);
            const std::string id = argv[3], probe = argv[4], mode = argv[5], generation = argv[7];
            const auto result = planning_store::snapshot_pins(Store(fs::u8path(argv[2])), std::u32string(id.begin(), id.end()),
                args.at(U"selection"), probe == "-" ? std::nullopt : std::optional<fs::path>(fs::u8path(probe)),
                args.contains(U"expected_codec") ? std::optional<compat::Value>(args.at(U"expected_codec")) : std::nullopt,
                std::u32string(mode.begin(), mode.end()), std::string(argv[6]) == "managed",
                generation == "-" ? std::nullopt : std::optional<std::u32string>(std::u32string(generation.begin(), generation.end())));
            auto value = planning_internal::object_value();
            value.object.emplace_back(U"pins", result.pins);
            auto blobs = planning_internal::object_value();
            const char hex[] = "0123456789abcdef";
            for (const auto& blob : result.blobs) {
                std::string encoded;
                for (const unsigned char c : blob.bytes) { encoded += hex[c >> 4]; encoded += hex[c & 15]; }
                blobs.object.emplace_back(blob.path, planning_internal::ascii_value(encoded));
            }
            value.object.emplace_back(U"blobs", std::move(blobs));
            std::cout << "ok " << compat::compact(value) << '\n';
            return 0;
        }
        if (std::string(argv[1]) == "launch-dry-run" && argc == 10) {
            // launch-dry-run <store> <association> <probe|-> <area> <licenses,> <weapons,> <mode> <time>
            auto text = [](const std::string& s) { return std::u32string(s.begin(), s.end()); };
            auto list = [&](const std::string& s) {
                std::vector<std::u32string> out;
                for (std::size_t at = 0; at < s.size();) { auto end = s.find(',', at); if (end == std::string::npos) end = s.size(); out.push_back(text(s.substr(at, end - at))); at = end + 1; }
                return out;
            };
            planning::LaunchSelection selection;
            selection.area = text(argv[5]); selection.licenses = list(argv[6]); selection.weapons = list(argv[7]);
            selection.mode = text(argv[8]); selection.time_of_day = planning::Integer{argv[9]};
            const std::string probe = argv[4];
            const auto request = planning_store::launch_dry_run(Store(fs::u8path(argv[2])), text(argv[3]), selection,
                probe == "-" ? std::nullopt : std::optional<fs::path>(fs::u8path(probe)));
            std::cout << "ok " << request.export_json();
            return 0;
        }
        if (std::string(argv[1]) != "refresh-state" || argc != 5) return 2;
        const std::string id = argv[3], probe = argv[4];
        const auto value = planning_store::refresh_state(Store(fs::u8path(argv[2])), std::u32string(id.begin(), id.end()),
            probe == "-" ? std::nullopt : std::optional<fs::path>(fs::u8path(probe)));
        std::cout << "ok " << compat::compact(value) << '\n';
    } catch (const planning::Error& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const probe_process::ProbeError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const fs::filesystem_error& e) { std::cout << "oserror " << e.code().value() << '\n';
    } catch (const std::invalid_argument& e) { std::cout << "invalid " << e.what() << '\n';
    } catch (const StoreError& e) { std::cout << "frontend " << e.what() << '\n'; }
    return 0;
}
