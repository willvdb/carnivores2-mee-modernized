// Test-only lossless line-framed differential driver, never a production CLI.
#include "c2/frontend/content.hpp"
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
static_assert(!std::is_default_constructible_v<ContentFingerprint>);
static Value string(std::u32string s) { Value v; v.kind = Kind::string; v.string = std::move(s); return v; }
static Value run(const Value& request) {
    auto root = content_internal::native_units(request.at(U"root").string);
    const auto& op = request.at(U"op").string;
    if (op == U"native_path") return string(store_paths::native_points(native_path(root)));
    if (op == U"resolve_reference") return compat::parse(resolve_reference(root, request.at(U"reference").string).export_json());
    if (op == U"resolved_path") return string(store_paths::native_points(resolved_path(root, request.at(U"reference").string)));
    if (op == U"walk_files") {
        Value v; v.kind = Kind::array;
        for (const auto& p : content_internal::walk_files(root)) v.array.push_back(string(store_paths::native_points(p)));
        return v;
    }
    if (op == U"content_inventory") return content_internal::inventory_value(content_internal::content_inventory(root));
    if (op == U"fingerprint_barrier") {
        const auto& wanted = request.at(U"phase").string;
        bool waited = false;
        auto payload = content_internal::fingerprint_payload(root, [&](content_internal::FingerprintPhase phase) {
            const auto name = phase == content_internal::FingerprintPhase::initial_inventory ? U"initial_inventory" :
                phase == content_internal::FingerprintPhase::member_hashed ? U"member_hashed" : U"final_inventory";
            if (!waited && wanted == name) {
                waited = true; std::cout << "{\"ready\":true}\n" << std::flush;
                std::string continuation;
                if (!std::getline(std::cin, continuation)) throw ContentError("missing test continuation");
            }
        });
        if (!waited) throw ContentError("test phase not reached");
        return string({payload.begin(), payload.end()});
    }
    if (op == U"hash_file" || op == U"fingerprint_payload") {
        auto s = op == U"hash_file" ? content_internal::hash_file(root) : content_internal::fingerprint_payload(root);
        return string({s.begin(), s.end()});
    }
    if (op == U"fingerprint") {
        auto f = fingerprint(root); auto copy = f; root.clear();
        if (copy.algorithm() != "huntdat-sha256-v1" || copy.sha256().size() != 64 || copy.byte_count().empty() || copy.export_json() != f.export_json()) throw ContentError("owned API check failed");
        return compat::parse(copy.export_json());
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
        try { auto value = run(compat::parse(line)); ok.boolean = true; out.object = {{U"ok", ok}, {U"value", std::move(value)}}; }
        catch (const std::exception& e) { std::string message = e.what(); out.object = {{U"ok", ok}, {U"error", string(store_paths::native_points(std::filesystem::path(message)))}}; }
        std::cout << compat::compact(out) << '\n' << std::flush;
    }
}
