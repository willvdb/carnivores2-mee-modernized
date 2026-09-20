#pragma once
// Private read-only prerequisite of session I/O; no writing or codec inspection.
#include "c2/frontend/generation.hpp"
#include "json_compat.hpp"
namespace c2::frontend {
struct Capture {
    std::vector<CapturedEntry> entries;
    std::vector<CapturedBlob> blobs;
    compat::Value entry_value() const;
};
Capture capture(const std::filesystem::path&);
}
