#pragma once
#include "c2/frontend/discovery.hpp"
#include "json_compat.hpp"
namespace c2::frontend::discovery_internal {
// Pure baseline seam for testing reference cases unreachable through today's
// validated manifest (for example an absent historical engine baseline).
compat::Value inspect(const compat::Value&);
// Retained values behind the owned handles, for sibling TUs that evaluate over
// them without serializing and reparsing exported JSON.
const compat::Value& instance_value(const InstanceObservation&);
const compat::Value& observation_value(const DiscoveryObservation&);
}
