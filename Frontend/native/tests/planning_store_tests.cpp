// Test-only driver for store-backed planning; disposable stores only.
#include "planning_store.hpp"
#include "probe_process.hpp"
#include <iostream>
#include <string>
using namespace c2::frontend;
namespace fs = std::filesystem;
int main(int argc, char** argv) {
    if (argc < 5) return 2;
    try {
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
    } catch (const planning_store::NotImplemented& e) { std::cout << "not-implemented " << e.what() << '\n';
    } catch (const probe_process::ProbeError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const StoreError& e) { std::cout << "frontend " << e.what() << '\n'; }
    return 0;
}
