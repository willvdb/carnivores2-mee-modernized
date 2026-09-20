#pragma once
// Private read-only access; never serialize/reparse or project retained metadata.
#include "c2/frontend/store.hpp"
#include "json_compat.hpp"
namespace c2::frontend {
struct ManifestAccess {
    static const compat::Value& instances(const Manifest&);
    static const ReadPolicy& policy(const Manifest&);
};
}
