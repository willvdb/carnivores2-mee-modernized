#include "c2/frontend/store.hpp"
#include "manifest_schema.hpp"
#include "schema_compat.hpp"
#include "store_paths.hpp"
#include "capture.hpp"
#include "manifest_access.hpp"
#include <new>

namespace c2::frontend {
using compat::Value;
struct Manifest::Impl { Value data; ReadPolicy policy; std::filesystem::path directory; };
const Value& ManifestAccess::data(const Manifest& m) { return m.impl_->data; }
const Value& ManifestAccess::instances(const Manifest& m) { return m.impl_->data.at(U"instances"); }
const ReadPolicy& ManifestAccess::policy(const Manifest& m) { return m.impl_->policy; }
struct GenerationObservation::Impl {
    Value generation, history;
    std::u32string id;
    std::filesystem::path root;
    Capture capture;
    ReadPolicy policy;
    bool is_head = false;
};
GenerationObservation::GenerationObservation(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
const std::u32string& GenerationObservation::id() const noexcept { return impl_->id; }
const std::filesystem::path& GenerationObservation::root() const noexcept { return impl_->root; }
const std::vector<CapturedEntry>& GenerationObservation::entries() const noexcept { return impl_->capture.entries; }
const std::vector<CapturedBlob>& GenerationObservation::blobs() const noexcept { return impl_->capture.blobs; }
namespace {
std::string export_value(const Value& v, const ReadPolicy& policy) {
    try { return compat::display(v, policy.max_depth); }
    catch (const compat::ResourceError& e) { throw ResourceExhausted(e.what()); }
    catch (const std::bad_alloc&) { throw ResourceExhausted("generation export allocation exhausted"); }
    catch (const std::length_error&) { throw ResourceExhausted("generation export size exhausted"); }
    catch (const compat::Error& e) { throw StoreError(e.what()); }
}
std::filesystem::path uuid_path(const std::u32string& id) {
    // Only validated schema UUIDs reach filesystem construction, hence ASCII.
    return std::filesystem::path(std::string(id.begin(), id.end()));
}
}
std::string GenerationObservation::export_generation_json() const { return export_value(impl_->generation, impl_->policy); }
std::string GenerationObservation::export_history_json() const {
    if (!impl_->is_head) throw StoreError("history inspection requires the current generation observation");
    return export_value(impl_->history, impl_->policy);
}
GenerationObservation Manifest::resolve_generation(const std::u32string& association_id,
        std::optional<std::u32string> generation_id) const {
    try {
        const auto& associations = impl_->data.at(U"associations");
        if (!associations.contains(association_id)) throw StoreError("unknown association");
        const auto& a = associations.at(association_id);
        if (schema_version() != 2 || !a.contains(U"authority") ||
            a.at(U"authority").kind != compat::Kind::string || a.at(U"authority").string != U"managed-state-history")
            throw StoreError("explicit managed-state schema upgrade required");
        // The whole immutable manifest, including this history and its IDs/kind/
        // snapshot/provenance contract, was validated exactly once by Store.read.
        const auto& history = a.at(U"managed_state");
        const auto& generations = history.at(U"generations");
        const auto& head = history.at(U"current_generation").string;
        const auto& identity = generation_id ? *generation_id : head;
        if (!generations.contains(identity)) throw StoreError("unknown managed-state generation");
        const auto& g = generations.at(identity);
        auto result = std::make_shared<GenerationObservation::Impl>();
        result->root = g.at(U"kind").string == U"original-import" ?
            impl_->directory / "snapshots" / uuid_path(association_id) :
            impl_->directory / "generations" / uuid_path(association_id) / uuid_path(identity);
        result->capture = capture(result->root);
        if (!schema::equal(result->capture.entry_value(), g.at(U"members")))
            throw StoreError("managed generation snapshot is missing, changed or unsafe; no fallback");
        result->generation = g; result->id = identity; result->policy = impl_->policy;
        result->is_head = identity == head;
        if (result->is_head) {
            auto& out = result->history; out.kind = compat::Kind::object;
            for (const auto& mapping : {std::pair{U"association_id", U"id"}, {U"hunter_id", U"hunter_id"},
                 {U"instance_id", U"instance_id"}, {U"native_slot", U"filename_slot"}, {U"authority", U"authority"}})
                out.object.emplace_back(mapping.first, a.at(mapping.second));
            out.object.emplace_back(U"current_generation", g.at(U"id"));
            out.object.emplace_back(U"history", history);
            Value provenance; provenance.kind = compat::Kind::object;
            for (const auto& field : a.object)
                if (field.first != U"managed_state" && field.first != U"last_observation" && field.first != U"authority")
                    provenance.object.push_back(field);
            out.object.emplace_back(U"import_provenance", std::move(provenance));
        }
        return GenerationObservation(std::move(result));
    } catch (const compat::ResourceError& e) { throw ResourceExhausted(e.what()); }
      catch (const std::bad_alloc&) { throw ResourceExhausted("generation read allocation exhausted"); }
      catch (const std::length_error&) { throw ResourceExhausted("generation read size exhausted"); }
      catch (const std::filesystem::filesystem_error& e) { throw StoreError(e.what()); }
      catch (const compat::Error& e) { throw StoreError(e.what()); }
}
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
        p->directory = directory_;
        return Manifest(std::move(p));
    } catch (const compat::ResourceError& e) { throw ResourceExhausted(e.what()); }
      catch (const std::bad_alloc&) { throw ResourceExhausted("manifest read allocation exhausted"); }
      catch (const std::length_error&) { throw ResourceExhausted("manifest read size exhausted"); }
      catch (const std::filesystem::filesystem_error& e) { throw StoreError(e.what()); }
      catch (const compat::Error& e) { throw StoreError(e.what()); }
}
} // namespace c2::frontend
