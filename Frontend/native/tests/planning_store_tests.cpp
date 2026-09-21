// Test-only driver for store-backed planning; disposable stores only.
#include "planning_store.hpp"
#include "probe_process.hpp"
#include <iostream>
#include <string>
using namespace c2::frontend;
namespace fs = std::filesystem;
int main(int argc, char** argv) {
    if (argc != 5 || std::string(argv[1]) != "refresh-state") return 2;
    try {
        const std::string id = argv[3], probe = argv[4];
        const auto value = planning_store::refresh_state(Store(fs::u8path(argv[2])), std::u32string(id.begin(), id.end()),
            probe == "-" ? std::nullopt : std::optional<fs::path>(fs::u8path(probe)));
        std::cout << "ok " << compat::compact(value) << '\n';
    } catch (const planning_store::NotImplemented& e) { std::cout << "not-implemented " << e.what() << '\n';
    } catch (const probe_process::ProbeError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const StoreError& e) { std::cout << "frontend " << e.what() << '\n'; }
    return 0;
}
