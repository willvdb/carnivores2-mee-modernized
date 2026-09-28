#pragma once
#include "c2/frontend/catalog.hpp"

namespace c2::frontend::catalog {
// Engine-compatible interpretation is separate from the frozen v1 observation
// and launch-plan schemas. Only valid, representable legacy numeric prefixes
// resolve; recovery/clamping never grants a selection or a price here.
std::optional<int> legacy_integer(const Attribute&);
struct HuntablePresentation {
    std::size_t ordinal;
    std::u32string source;
    std::size_t line;
    int ai;
    std::optional<Scalar> label;
    std::optional<int> price;
    bool explicit_picture;
    ReferenceObservation thumbnail;
    TextObservation description;
};
struct HuntableRoster {
    std::vector<HuntablePresentation> entries;
    std::vector<Diagnostic> diagnostics;
};
// Uses the selected menu script (_MENU.TXT before _RES.TXT), counts huntables
// (AI >= 10) in file order, including duplicate AI values, and uses slot+1 for
// DINO thumbnails and descriptions. Nonempty pic overrides only the thumbnail.
// All paths retain the portable resolver's ambiguity/traversal protections.
HuntableRoster resolve_huntables(const std::filesystem::path&, const Projection&);
}
