#pragma once
// Private pure validation seam; production repository reads belong to 1A.2.
#include "json_compat.hpp"
namespace c2::frontend::schema {
compat::Value decode_manifest(std::string_view bytes);
void validate_manifest(const compat::Value &);
// str(uuid.UUID(value)) == value: canonical lowercase hyphenated form only.
bool valid_id(std::u32string_view);
} // namespace c2::frontend::schema
