// Test-only line-framed adapter. Production CLI remains unchanged.
#include "c2/frontend/discovery.hpp"
#include "discovery_internal.hpp"
#include "content_internal.hpp"
#include "store_paths.hpp"
#include <iostream>
#include <type_traits>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace c2::frontend;
using compat::Value;
using compat::Kind;
static_assert(!std::is_default_constructible_v<InstanceObservation>);
static_assert(!std::is_default_constructible_v<DiscoveryObservation>);
static Value text(std::u32string s) { Value v; v.kind = Kind::string; v.string = std::move(s); return v; }
static std::filesystem::path path(const Value& r, std::u32string_view key) { return content_internal::native_units(r.at(key).string); }
static std::string run(const Value& r) {
    const auto& op = r.at(U"op").string;
    if (op == U"recognize") {
        auto v = recognize(path(r, U"root")); auto copy = v;
        auto parsed = compat::parse(copy.export_json());
        if (!copy.executables() || copy.executables()->size() != parsed.at(U"executables").array.size() || copy.recognized() != parsed.at(U"recognized").boolean || !copy.path() || copy.revision_changed() || copy.engine_changed() || copy.engine_review_required()) throw ContentError("recognition typed API");
        return copy.export_json();
    }
    if (op == U"discover") {
        Value out; out.kind = Kind::array;
        for (const auto& v : discover(path(r, U"root"))) out.array.push_back(compat::parse(v.export_json()));
        return compat::display(out);
    }
    if (op == U"engine_evidence") {
        auto root = path(r, U"root"); auto observation = recognize(root);
        Value out; out.kind = Kind::array;
        for (const auto& e : engine_evidence(root, observation)) {
            Value v; v.kind = Kind::object;
            v.object = {{U"path", text(e.path)}, {U"sha256", text({e.sha256.begin(), e.sha256.end()})}, {U"semantics", text(e.semantics)}};
            out.array.push_back(std::move(v));
        }
        return compat::display(out);
    }
    if (op == U"script_prefix") {
        auto bytes = content_internal::read_prefix(path(r, U"root"), 8 * 1024 * 1024 + 1);
        std::u32string projected; for (unsigned char c : bytes) projected.push_back(c);
        return compat::display(text(std::move(projected)));
    }
    if (op == U"inspect_raw") return compat::display(discovery_internal::inspect(r.at(U"instance")));
    if (op == U"move_candidates") {
        auto manifest = Store(path(r, U"store")).read(); Value out; out.kind = Kind::array;
        for (auto& id : move_candidates(manifest, path(r, U"root"))) out.array.push_back(text(std::move(id)));
        return compat::display(out);
    }
    // Both Store and Manifest die here; instance must retain complete metadata.
    auto instance = get_instance(Store(path(r, U"store")).read(), r.at(U"identity").string);
    auto copy = instance;
    if (copy.id() != r.at(U"identity").string || copy.path().empty() || copy.path_flavor().empty()) throw ContentError("instance typed API");
    if (op == U"get_instance") return copy.export_json();
    if (op == U"inspect_retained") {
        std::cout << "{\"ready\":true}\n" << std::flush;
        std::string continuation;
        if (!std::getline(std::cin, continuation)) throw ContentError("missing test continuation");
    }
    if (op == U"inspect_instance" || op == U"inspect_retained") {
        auto observed = inspect_instance(copy); auto retained = observed;
        auto parsed = compat::parse(retained.export_json());
        if (retained.executables().has_value() != parsed.contains(U"executables") || (retained.executables() && retained.executables()->size() != parsed.at(U"executables").array.size()) || retained.recognized() != parsed.at(U"recognized").boolean || !retained.engine_review_required() ||
            retained.engine_review_required().value() != parsed.at(U"engine_review_required").boolean ||
            retained.path().has_value() != parsed.contains(U"path") || retained.revision_changed().has_value() != parsed.contains(U"revision_changed") ||
            retained.engine_changed().has_value() != parsed.contains(U"engine_changed")) throw ContentError("inspection typed API");
        return retained.export_json();
    }
    throw ContentError("unknown test operation");
}
int main() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
    std::string line;
    while (std::getline(std::cin, line)) {
        Value out; out.kind = Kind::object; Value ok; ok.kind = Kind::boolean;
        try {
            auto bytes = run(compat::parse(line)); ok.boolean = true;
            out.object = {{U"ok", ok}, {U"value", compat::parse(bytes)}, {U"json", text({bytes.begin(), bytes.end()})}};
        } catch (const std::exception& e) {
            out.object = {{U"ok", ok}, {U"error", text(store_paths::native_points(std::filesystem::path(e.what())))}};
        }
        std::cout << compat::compact(out) << '\n' << std::flush;
    }
}
