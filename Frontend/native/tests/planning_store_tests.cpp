// Test-only driver for store-backed planning; disposable stores only.
#include "planning_store.hpp"
#include "probe_process.hpp"
#include <iostream>
#include <string>
#include <iterator>
#include "planning_internal.hpp"
#include "catalog_internal.hpp"
#include "content_internal.hpp"
#include <cstdlib>
using namespace c2::frontend;
namespace fs = std::filesystem;
int main(int argc, char** argv) {
    if (argc < 5) return 2;
    try {
        if ((std::string(argv[1]) == "observer" || std::string(argv[1]) == "hunt") && argc == 6) {
            const std::string content((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
            const auto args = compat::parse(content);
            const std::string id = argv[3], probe = argv[4];
            const auto path = probe == "-" ? std::nullopt : std::optional<fs::path>(fs::u8path(probe));
            planning_store::PolicyEvaluator test_policy;
            if (std::string(argv[5]) == "fixture-policy") test_policy = [](const compat::Value& revision,
                const catalog::Projection& projection, const compat::Value& slot, const compat::Value& selection,
                const compat::Value& score) {
                auto result = planning_internal::object_value();
                result.object = {{U"adapter", planning_internal::ascii_value("asset-free-planning-policy-double")},
                    {U"revision", revision}, {U"catalog", *catalog::projection_value(projection)},
                    {U"native_slot", slot}, {U"selection", selection}, {U"observed_score", score},
                    {U"process_launch_allowed", planning_internal::boolean_value(false)}};
                return result;
            };
            compat::Value result;
            if (std::string(argv[1]) == "observer") {
                const auto& selection = args.at(U"selection");
                const auto& time = selection.at(U"time_of_day");
                result = planning_store::plan_observer(Store(fs::u8path(argv[2])), selection.at(U"area").string,
                    std::u32string(id.begin(), id.end()), planning::Integer{time.integer}, path,
                    args.contains(U"engine") ? std::optional<fs::path>(content_internal::native_units(args.at(U"engine").string)) : std::nullopt,
                    test_policy);
            } else result = planning_store::plan_hunt(Store(fs::u8path(argv[2])), std::u32string(id.begin(), id.end()),
                args.at(U"selection"), path, test_policy);
            std::cout << "ok " << compat::compact(result) << '\n';
            return 0;
        }
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
            store_write::FailureHook failure;
            if (std::getenv("C2_TEST_WRITE_FAILURE")) failure = [](store_write::WritePhase phase, const fs::path& target) {
                if (phase == store_write::WritePhase::replace && target.filename() == "lodge.json")
                    throw fs::filesystem_error("injected manifest replacement", std::make_error_code(std::errc::io_error));
            };
            const auto request = planning_store::launch_dry_run(Store(fs::u8path(argv[2])), text(argv[3]), selection,
                probe == "-" ? std::nullopt : std::optional<fs::path>(fs::u8path(probe)), failure);
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
