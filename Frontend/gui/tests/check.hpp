#pragma once
// Minimal check helper for the GUI test drivers: prints every failure with
// its location and makes main() return non-zero. No framework dependency.
#include <iostream>
#include <string>

namespace c2::frontend::gui::test {
inline int failures = 0;
inline void check(bool ok, const char* expr, const char* file, int line, const std::string& note = {}) {
    if (ok) return;
    ++failures;
    std::cerr << file << ':' << line << ": CHECK failed: " << expr;
    if (!note.empty()) std::cerr << " (" << note << ')';
    std::cerr << '\n';
}
} // namespace c2::frontend::gui::test
#define CHECK(expr) ::c2::frontend::gui::test::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__)
#define CHECK_NOTE(expr, note) ::c2::frontend::gui::test::check(static_cast<bool>(expr), #expr, __FILE__, __LINE__, (note))
