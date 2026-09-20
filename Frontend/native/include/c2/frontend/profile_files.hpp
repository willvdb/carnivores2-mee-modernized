#pragma once
#include "profile.hpp"
#include <filesystem>
#include <vector>

namespace c2::frontend {
struct ProfileDiagnostic {
    std::u32string code, message;
    std::optional<std::u32string> path;
};
struct ProfileFile {
    std::u32string path, kind;
    std::uintmax_t size{};
};
struct ProfileCompanion {
    std::u32string path;
    std::uintmax_t size{};
};
struct ProfileBlob { std::u32string path; std::string bytes; };
struct InspectedProfileFile {
    ProfileFile file;
    std::string sha256;
    ProfileInspection decoded;
};
class ProfileSetInspection;
class ProfileInventory;
// An owned inventory observation, bound to its original native root. It confers
// no ownership/acceptance authority and is not an externally atomic SAV/SAB pair.
class ProfileState {
public:
    const std::filesystem::path& root() const noexcept;
    const std::u32string& key() const noexcept;
    // Canonical arbitrary-magnitude decimal integer, never narrowed to int64.
    const std::string& filename_slot() const noexcept;
    const std::vector<ProfileFile>& files() const noexcept;
    const std::vector<ProfileCompanion>& companions() const noexcept;
    const std::vector<ProfileDiagnostic>& diagnostics() const noexcept;
    std::string export_json() const;
    std::vector<ProfileBlob> read() const;
    std::vector<ProfileBlob> stable_read() const;
    ProfileSetInspection inspect(ProfileCodec = ProfileCodec::unavailable,
        std::string_view dialect = "unknown") const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit ProfileState(std::shared_ptr<const Impl>);
    friend ProfileInventory inventory_profiles(const std::filesystem::path&);
};
class ProfileInventory {
public:
    const std::filesystem::path& root() const noexcept;
    const std::vector<ProfileState>& states() const noexcept;
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit ProfileInventory(std::shared_ptr<const Impl>);
    friend ProfileInventory inventory_profiles(const std::filesystem::path&);
};
class ProfileSetInspection {
public:
    const ProfileState& state() const noexcept;
    const std::vector<InspectedProfileFile>& files() const noexcept;
    const std::vector<ProfileBlob>& blobs() const noexcept;
    const std::vector<ProfileDiagnostic>& diagnostics() const noexcept;
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit ProfileSetInspection(std::shared_ptr<const Impl>);
    friend class ProfileState;
};
// Low-level native inventory: no tilde expansion, foreign-path filtering, store
// policy, helper/environment selection, or writes. Missing/non-directory => [].
ProfileInventory inventory_profiles(const std::filesystem::path& root);
}
