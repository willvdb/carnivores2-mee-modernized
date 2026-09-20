#pragma once
#include "json_compat.hpp"
namespace c2::frontend::schema {
bool equal(const compat::Value &, const compat::Value &);
bool truth(const compat::Value &);
std::u32string casefold(std::u32string_view);
std::u32string lower(std::u32string_view);
bool blank(std::u32string_view);
struct PurePath {
    std::u32string drive, root;
    std::vector<std::u32string> parts;
    bool nt = false;
    bool absolute() const { return !root.empty() && (!nt || !drive.empty()); }
    bool parent() const;
    bool below(const PurePath &) const;
};
PurePath path(std::u32string, bool nt);
} // namespace c2::frontend::schema
