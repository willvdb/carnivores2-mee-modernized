// Test-only driver for the private acceptance API over disposable stores.
// <command> <store> <probe|-> <policy: fixture|production> <failure|-> args...
//   preview <session> <expected_generation>
//   accept  <session> <expected_generation> <candidate_sha256>
//   recover <session>
//   digest  <session>
// failure = <phase>:<name>[:<nth>] throws an injected filesystem_error at the
// nth store_write phase whose path (or whose parent directory prefix) matches.
#include "acceptance.hpp"
#include "c2/frontend/planning.hpp"
#include "planning_internal.hpp"
#include "probe_process.hpp"
#include "session_journal.hpp"
#include <iostream>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace c2::frontend;
namespace fs = std::filesystem;
namespace {
using compat::Value;
std::u32string text(const std::string& s) { return std::u32string(s.begin(), s.end()); }
Value ascii(std::string_view s) { return planning_internal::ascii_value(s); }
// The Python fixture hunt policy of tests/test_acceptance_fixtures.py, key for key.
session_policy::Policies fixture_policies() {
    session_policy::Policies policies;
    policies.hunt = [](const Value&, const catalog::Projection&, const Value& slot, const Value& selection, const Value&) {
        auto argv = planning_internal::array_value();
        for (const std::string& a : {"reg=" + slot.integer, std::string("prj=huntdat/areas/area1"), std::string("din=1"),
                 std::string("wep=1"), "dtm=" + selection.at(U"time_of_day").integer, std::string("smod=0.85,0.70,0.80,1.0,1.25,1.0")})
            argv.array.push_back(ascii(a));
        auto result = planning_internal::object_value();
        result.object = {{U"adapter", planning_internal::string_value(std::u32string(planning::HUNT_POLICY_ID))},
            {U"fixture_only", ascii("authored disposable state, NOT Genesis")}, {U"selection", selection},
            {U"candidate_argv", std::move(argv)}};
        return result;
    };
    return policies;
}
store_write::FailureHook failure_hook(const std::string& spec) {
    if (spec == "-") return {};
    const auto first = spec.find(':'), second = spec.find(':', first + 1);
    const std::string phase_name = spec.substr(0, first);
    const std::string name = spec.substr(first + 1, second == std::string::npos ? std::string::npos : second - first - 1);
    const int nth = second == std::string::npos ? 1 : std::stoi(spec.substr(second + 1));
    static const std::pair<const char*, store_write::WritePhase> phases[] = {
        {"temp_create", store_write::WritePhase::temp_create}, {"temp_write", store_write::WritePhase::temp_write},
        {"temp_fsync", store_write::WritePhase::temp_fsync}, {"replace", store_write::WritePhase::replace},
        {"directory_fsync", store_write::WritePhase::directory_fsync}, {"lock_write", store_write::WritePhase::lock_write},
        {"lock_fsync", store_write::WritePhase::lock_fsync}, {"blob_directory_fsync", store_write::WritePhase::blob_directory_fsync}};
    std::optional<store_write::WritePhase> phase;
    for (const auto& p : phases) if (phase_name == p.first) phase = p.second;
    if (!phase) throw std::invalid_argument("unknown failure phase");
    auto seen = std::make_shared<int>(0);
    return [=](store_write::WritePhase at, const fs::path& path) {
        if (at != *phase) return;
        const std::string file = path.filename().string(), parent = path.parent_path().filename().string();
        if (file != name && parent.rfind(name, 0) != 0) return;
        if (++*seen != nth) return;
        throw fs::filesystem_error("injected failure " + spec, path, std::make_error_code(std::errc::io_error));
    };
}
}
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc < 7) return 2;
    const std::string command = argv[1], probe_text = argv[3], policy = argv[4];
    const Store store(fs::u8path(argv[2]));
    const auto probe = probe_text == "-" ? std::nullopt : std::optional<fs::path>(fs::u8path(probe_text));
    const auto policies = policy == "fixture" ? fixture_policies() : session_policy::Policies{};
    try {
        const auto hook = failure_hook(argv[5]);
        const std::u32string session = text(argv[6]);
        Value result;
        if (command == "preview" && argc == 8) result = acceptance::preview_acceptance(store, session, text(argv[7]), probe, policies);
        else if (command == "accept" && argc == 9)
            result = acceptance::accept_candidate(store, session, text(argv[7]), text(argv[8]), probe, policies, hook);
        else if (command == "recover" && argc == 7) result = acceptance::recover_acceptance(store, session, hook);
        else if (command == "digest" && argc == 7) {
            std::cout << "ok " << acceptance::candidate_digest(session_journal::read(store, session)) << '\n';
            return 0;
        } else return 2;
        std::cout << "ok " << compat::compact(result) << '\n';
    } catch (const ResourceExhausted& e) { std::cout << "resource " << e.what() << '\n';
    } catch (const StoreError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const planning::Error& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const probe_process::ProbeError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const fs::filesystem_error& e) { std::cout << "oserror " << e.code().value() << ' ' << e.what() << '\n';
    } catch (const std::invalid_argument& e) { std::cout << "invalid " << e.what() << '\n';
    } catch (const std::exception& e) { std::cout << "exception " << e.what() << '\n'; }
    return 0;
}
