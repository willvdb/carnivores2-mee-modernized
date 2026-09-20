#pragma once

#include <string>
#include <string_view>

namespace c2::frontend {
// Foundation only. No filesystem, engine, UI or JSON implementation types.
std::string_view version() noexcept;
std::string sha256(std::string_view bytes);
} // namespace c2::frontend
