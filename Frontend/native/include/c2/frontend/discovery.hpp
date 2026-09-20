#pragma once
#include "c2/frontend/content.hpp"
#include "c2/frontend/store.hpp"
#include <vector>

namespace c2::frontend {
struct EngineEvidence { std::u32string path; std::string sha256; std::u32string semantics = U"unknown"; };
// Owned immutable filesystem evidence. Optional fields remain absent when the
// reference did not observe them (notably unavailable roots and foreign paths).
class DiscoveryObservation {
public:
    bool recognized() const noexcept;
    std::optional<std::u32string> path() const;
    const std::optional<std::vector<std::u32string>>& executables() const noexcept;
    std::optional<bool> revision_changed() const;
    std::optional<bool> engine_changed() const;
    std::optional<bool> engine_review_required() const;
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit DiscoveryObservation(std::shared_ptr<const Impl>);
    friend struct DiscoveryAccess;
};
class InstanceObservation {
public:
    const std::u32string& id() const noexcept;
    const std::u32string& path() const noexcept;
    const std::u32string& path_flavor() const noexcept;
    // Complete retained baseline, including unknown metadata. Display can reject
    // nonfinite metadata without preventing inspection of the retained baseline.
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit InstanceObservation(std::shared_ptr<const Impl>);
    friend InstanceObservation get_instance(const Manifest&, const std::u32string&);
    friend DiscoveryObservation inspect_instance(const InstanceObservation&);
};
DiscoveryObservation recognize(const std::filesystem::path&);
std::vector<DiscoveryObservation> discover(const std::filesystem::path&);
std::vector<EngineEvidence> engine_evidence(const std::filesystem::path&, const DiscoveryObservation&);
InstanceObservation get_instance(const Manifest&, const std::u32string& identity);
DiscoveryObservation inspect_instance(const InstanceObservation&);
std::vector<std::u32string> move_candidates(const Manifest&, const std::filesystem::path&);
}
