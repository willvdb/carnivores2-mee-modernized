#pragma once
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace c2::frontend {
class StoreError : public std::runtime_error { public: using std::runtime_error::runtime_error; };
// Operational exhaustion is not an invalid persistent format or recovery signal.
class ResourceExhausted : public StoreError { public: using StoreError::StoreError; };
struct ReadPolicy {
    std::size_t max_depth = 1000;
    std::optional<std::size_t> max_bytes;
};
struct HunterSummary { std::u32string id, name; bool archived; };
struct ExpeditionSummary { std::u32string id, mode, path_flavor, path; };
enum class ManifestView { status, hunters, expeditions, host_settings };
class Manifest {
public:
    int schema_version() const;
    bool has_active_hunter() const;
    std::optional<std::u32string> active_hunter() const;
    std::vector<HunterSummary> hunters() const;
    std::vector<ExpeditionSummary> expeditions() const;
    // Presentation only, insertion ordered and ASCII escaped, ending in LF.
    // Nonfinite metadata in the selected view throws; read itself retains it.
    std::string export_json(ManifestView view) const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit Manifest(std::shared_ptr<const Impl>);
    friend class Store;
};
class Store {
public:
    explicit Store(std::filesystem::path directory, ReadPolicy policy = {});
    const std::filesystem::path& directory() const noexcept { return directory_; }
    Manifest read() const;
    static std::filesystem::path default_directory();
private:
    std::filesystem::path directory_;
    ReadPolicy policy_;
};
} // namespace c2::frontend
