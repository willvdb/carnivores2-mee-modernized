#pragma once
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>

namespace c2::frontend {
class ContentError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
enum class ReferenceStatus { found, missing, ambiguous, unsafe };
struct ReferenceObservation {
    std::u32string reference;
    ReferenceStatus status = ReferenceStatus::unsafe;
    std::optional<std::u32string> path; // relative POSIX spelling, only when found
    std::string export_json() const;
};
// Reject foreign/ambiguous native locators, expand user, then resolve.
std::filesystem::path native_path(const std::filesystem::path&);
// Unlike native_path, reference observation resolves its root without expanding
// tilde or rejecting native literal backslashes. All equal-casefold siblings
// participate; an exact spelling is never preferred over an ambiguous sibling.
ReferenceObservation resolve_reference(const std::filesystem::path&, std::u32string reference);
std::filesystem::path resolved_path(const std::filesystem::path&, std::u32string reference);
class ContentFingerprint {
public:
    const std::string& algorithm() const noexcept;
    const std::string& sha256() const noexcept;
    std::size_t file_count() const noexcept;
    // Canonical arbitrary-magnitude decimal; sum cannot overflow native integers.
    const std::string& byte_count() const noexcept;
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit ContentFingerprint(std::shared_ptr<const Impl>);
    friend ContentFingerprint fingerprint(const std::filesystem::path&);
};
// Read-only HUNTDAT identity, not ownership, semantic validity, or externally
// atomic filesystem authority. Detected concurrent changes fail closed.
ContentFingerprint fingerprint(const std::filesystem::path& root);
}
