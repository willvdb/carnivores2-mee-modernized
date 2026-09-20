#include "c2/frontend/store.hpp"
#include "manifest_schema.hpp"
#include "schema_compat.hpp"
#include "store_paths.hpp"
#include <new>

namespace c2::frontend {
using compat::Value;
struct Manifest::Impl { Value data; ReadPolicy policy; };
Manifest::Manifest(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
int Manifest::schema_version() const { return impl_->data.at(U"schema_version").integer == "1" ? 1 : 2; }
bool Manifest::has_active_hunter() const { return impl_->data.contains(U"active_hunter"); }
std::optional<std::u32string> Manifest::active_hunter() const {
    if (!has_active_hunter() || impl_->data.at(U"active_hunter").kind == compat::Kind::null) return std::nullopt;
    return impl_->data.at(U"active_hunter").string;
}
std::vector<HunterSummary> Manifest::hunters() const {
    std::vector<HunterSummary> out;
    for (const auto& entry : impl_->data.at(U"hunters").object) {
        const auto& v = entry.second;
        out.push_back({entry.first, v.at(U"name").string,
            v.contains(U"archived_at") && schema::truth(v.at(U"archived_at"))});
    }
    return out;
}
std::vector<ExpeditionSummary> Manifest::expeditions() const {
    std::vector<ExpeditionSummary> out;
    for (const auto& entry : impl_->data.at(U"instances").object) {
        const auto& v = entry.second;
        out.push_back({entry.first, v.at(U"mode").string, v.at(U"path_flavor").string, v.at(U"path").string});
    }
    return out;
}
std::string Manifest::export_json(ManifestView view) const {
    try {
        const auto& data = impl_->data;
        if (view == ManifestView::status) return compat::display(data, impl_->policy.max_depth);
        if (view == ManifestView::host_settings) return compat::display(data.at(U"host_settings"), impl_->policy.max_depth);
        Value list; list.kind = compat::Kind::array;
        for (const auto& entry : data.at(view == ManifestView::hunters ? U"hunters" : U"instances").object)
            list.array.push_back(entry.second);
        if (view == ManifestView::expeditions) return compat::display(list, impl_->policy.max_depth);
        Value result; result.kind = compat::Kind::object;
        // Python execute indexes this optional field: preserve failure, not synthesis.
        if (!has_active_hunter()) throw StoreError("active_hunter is absent");
        result.object.emplace_back(U"active_hunter", data.at(U"active_hunter"));
        result.object.emplace_back(U"hunters", std::move(list));
        return compat::display(result, impl_->policy.max_depth);
    } catch (const compat::ResourceError& e) { throw ResourceExhausted(e.what()); }
      catch (const std::bad_alloc&) { throw ResourceExhausted("manifest export allocation exhausted"); }
      catch (const std::length_error&) { throw ResourceExhausted("manifest export size exhausted"); }
      catch (const std::filesystem::filesystem_error& e) { throw StoreError(e.what()); }
      catch (const compat::Error& e) { throw StoreError(e.what()); }
}
Store::Store(std::filesystem::path directory, ReadPolicy policy)
    try : directory_(store_paths::resolve_root(std::move(directory))), policy_(policy) {}
    catch (const std::bad_alloc&) { throw ResourceExhausted("store path allocation exhausted"); }
    catch (const std::length_error&) { throw ResourceExhausted("store path size exhausted"); }
    catch (const std::filesystem::filesystem_error& e) { throw StoreError(e.what()); }
std::filesystem::path Store::default_directory() {
    try { return store_paths::default_directory(); }
    catch (const std::bad_alloc&) { throw ResourceExhausted("store path allocation exhausted"); }
    catch (const std::length_error&) { throw ResourceExhausted("store path size exhausted"); }
}
Manifest Store::read() const {
    try {
        auto bytes = store_paths::read(directory_ / "lodge.json", policy_);
        if (!bytes) {
            if (std::filesystem::exists(directory_ / "lodge.json.bak") ||
                std::filesystem::exists(directory_ / "lodge.schema-1.backup.json"))
                throw StoreError("manifest missing with backup present; use explicit recovery");
            bytes = "{\"schema_version\":1,\"hunters\":{},\"active_hunter\":null,\"instances\":{},\"associations\":{},\"host_settings\":{}}";
        }
        auto p = std::make_shared<Manifest::Impl>();
        p->data = compat::parse(*bytes, policy_.max_depth);
        schema::validate_manifest(p->data);
        p->policy = policy_;
        return Manifest(std::move(p));
    } catch (const compat::ResourceError& e) { throw ResourceExhausted(e.what()); }
      catch (const std::bad_alloc&) { throw ResourceExhausted("manifest read allocation exhausted"); }
      catch (const std::length_error&) { throw ResourceExhausted("manifest read size exhausted"); }
      catch (const std::filesystem::filesystem_error& e) { throw StoreError(e.what()); }
      catch (const compat::Error& e) { throw StoreError(e.what()); }
}
} // namespace c2::frontend
