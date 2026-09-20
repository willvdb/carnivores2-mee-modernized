#pragma once
// Private pure validation seam; production repository reads belong to 1A.2.
#include "json_compat.hpp"
namespace c2::frontend::schema {
compat::Value decode_manifest(std::string_view bytes);
void validate_manifest(const compat::Value &);
} // namespace c2::frontend::schema
