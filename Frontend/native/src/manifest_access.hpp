#pragma once
// Private read-only access; never serialize/reparse or project retained metadata.
#include "c2/frontend/store.hpp"
#include "json_compat.hpp"
namespace c2::frontend {
struct ManifestAccess {
    // Complete validated manifest object (schema_version, hunters, instances,
    // associations, host_settings and retained unknown metadata).
    static const compat::Value& data(const Manifest&);
    static const compat::Value& instances(const Manifest&);
    static const ReadPolicy& policy(const Manifest&);
    // Validate/copy a transaction's retained value without another disk read,
    // serialization round trip, or different store/path policy.
    static Manifest snapshot(const Store&, compat::Value);
    static const compat::Value& generation(const GenerationObservation&);
};
}
