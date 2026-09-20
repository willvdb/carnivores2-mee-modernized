#pragma once
#include "json_compat.hpp"
namespace c2::frontend::discovery_internal {
// Pure baseline seam for testing reference cases unreachable through today's
// validated manifest (for example an absent historical engine baseline).
compat::Value inspect(const compat::Value&);
}
