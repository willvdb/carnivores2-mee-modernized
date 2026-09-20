#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace c2::frontend {
struct CapturedEntry {
    std::u32string path;
    std::string type;
    std::uint64_t size;
    std::optional<std::string> sha256;
};
struct CapturedBlob { std::u32string path; std::string bytes; };
// Immutable owned observation: no live filesystem handles or mutable authority.
class GenerationObservation {
public:
    const std::u32string& id() const noexcept;
    const std::filesystem::path& root() const noexcept;
    const std::vector<CapturedEntry>& entries() const noexcept;
    const std::vector<CapturedBlob>& blobs() const noexcept;
    std::string export_generation_json() const;
    // Only a resolved manifest-head observation can export inspect_history.
    // Explicit prior generations throw rather than misrepresent head validation.
    std::string export_history_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit GenerationObservation(std::shared_ptr<const Impl>);
    friend class Manifest;
};
}
