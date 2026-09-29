#include "c2/frontend/gui/demo_store.hpp"
#include <chrono>
#include <fstream>
#include <random>
#include <stdexcept>
#include <system_error>

namespace c2::frontend::gui {
namespace {
constexpr const char* kPrefix = "c2-frontend-gui-demo-";
// Authored fixture. Hunters: an active hunter, a second hunter with the same
// display name (duplicate names are legal; identity is the UUID), a hunter
// with non-ASCII letters, and an archived hunter. Expeditions: one posix
// locator, one nt locator, one deliberately long locator; all `registered`.
// The revision digests are labeled fixture values, not real content hashes.
const char* const kManifest =
    "{\"schema_version\": 1,\n"
    " \"hunters\": {\n"
    "  \"5f1c2a9e-0d3b-4c7a-9e21-000000000001\": {\"id\": \"5f1c2a9e-0d3b-4c7a-9e21-000000000001\", \"name\": \"Anne Trapper\"},\n"
    "  \"5f1c2a9e-0d3b-4c7a-9e21-000000000002\": {\"id\": \"5f1c2a9e-0d3b-4c7a-9e21-000000000002\", \"name\": \"Anne Trapper\"},\n"
    "  \"5f1c2a9e-0d3b-4c7a-9e21-000000000003\": {\"id\": \"5f1c2a9e-0d3b-4c7a-9e21-000000000003\", \"name\": \"Bj\xC3\xB6rn \xC3\x98" "deg\xC3\xA5rd \xE2\x80\x94 \xE7\x8C\x9F\xE4\xBA\xBA\"},\n"
    "  \"5f1c2a9e-0d3b-4c7a-9e21-000000000004\": {\"id\": \"5f1c2a9e-0d3b-4c7a-9e21-000000000004\", \"name\": \"Retired Hunter\", \"archived_at\": \"2026-09-01T00:00:00Z\"}\n"
    " },\n"
    " \"active_hunter\": \"5f1c2a9e-0d3b-4c7a-9e21-000000000001\",\n"
    " \"instances\": {\n"
    "  \"a7d4e6c2-8b19-4f3e-b5a0-00000000000a\": {\"id\": \"a7d4e6c2-8b19-4f3e-b5a0-00000000000a\", \"path\": \"/opt/games/carnivores2-mee\", \"path_flavor\": \"posix\", \"mode\": \"registered\", \"managed_root\": null, \"dialect_hint\": \"mee-newer\",\n"
    "   \"revision\": {\"algorithm\": \"huntdat-sha256-v1\", \"sha256\": \"0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a\", \"file_count\": 12, \"byte_count\": 4096},\n"
    "   \"revisions\": [{\"algorithm\": \"huntdat-sha256-v1\", \"sha256\": \"0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a0a\", \"file_count\": 12, \"byte_count\": 4096}],\n"
    "   \"engine_evidence\": []},\n"
    "  \"a7d4e6c2-8b19-4f3e-b5a0-00000000000b\": {\"id\": \"a7d4e6c2-8b19-4f3e-b5a0-00000000000b\", \"path\": \"C:\\\\Games\\\\Carnivores 2 Classic\", \"path_flavor\": \"nt\", \"mode\": \"registered\", \"managed_root\": null, \"dialect_hint\": \"c2-classic\",\n"
    "   \"revision\": {\"algorithm\": \"huntdat-sha256-v1\", \"sha256\": \"0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b\", \"file_count\": 7, \"byte_count\": 2048},\n"
    "   \"revisions\": [{\"algorithm\": \"huntdat-sha256-v1\", \"sha256\": \"0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b0b\", \"file_count\": 7, \"byte_count\": 2048}],\n"
    "   \"engine_evidence\": []},\n"
    "  \"a7d4e6c2-8b19-4f3e-b5a0-00000000000c\": {\"id\": \"a7d4e6c2-8b19-4f3e-b5a0-00000000000c\", \"path\": \"/home/hunter/mods/An Extraordinarily Long Total Conversion Folder Name That Does Not Fit In A Narrow List Column\", \"path_flavor\": \"posix\", \"mode\": \"registered\", \"managed_root\": null, \"dialect_hint\": \"unknown\",\n"
    "   \"revision\": {\"algorithm\": \"huntdat-sha256-v1\", \"sha256\": \"0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c\", \"file_count\": 1, \"byte_count\": 1},\n"
    "   \"revisions\": [{\"algorithm\": \"huntdat-sha256-v1\", \"sha256\": \"0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c0c\", \"file_count\": 1, \"byte_count\": 1}],\n"
    "   \"engine_evidence\": []}\n"
    " },\n"
    " \"associations\": {},\n"
    " \"host_settings\": {}\n"
    "}\n";

std::string unique_suffix() {
    static const char* const alphabet = "0123456789abcdefghijklmnopqrstuvwxyz";
    std::random_device device;
    std::mt19937_64 engine(device() ^ static_cast<std::uint64_t>(
        std::chrono::steady_clock::now().time_since_epoch().count()));
    std::uniform_int_distribution<int> pick(0, 35);
    std::string out;
    for (int i = 0; i < 12; ++i) out.push_back(alphabet[pick(engine)]);
    return out;
}
} // namespace

std::string demo_manifest_bytes() { return kManifest; }

const DemoStoreIdentities& demo_identities() {
    static const DemoStoreIdentities ids{
        {"5f1c2a9e-0d3b-4c7a-9e21-000000000001", "5f1c2a9e-0d3b-4c7a-9e21-000000000002",
         "5f1c2a9e-0d3b-4c7a-9e21-000000000003", "5f1c2a9e-0d3b-4c7a-9e21-000000000004"},
        {"a7d4e6c2-8b19-4f3e-b5a0-00000000000a", "a7d4e6c2-8b19-4f3e-b5a0-00000000000b",
         "a7d4e6c2-8b19-4f3e-b5a0-00000000000c"},
        "5f1c2a9e-0d3b-4c7a-9e21-000000000001"};
    return ids;
}

std::filesystem::path create_demo_store(const std::filesystem::path& parent) {
    for (int attempt = 0; attempt < 16; ++attempt) {
        const auto directory = parent / (kPrefix + unique_suffix());
        std::error_code ec;
        if (std::filesystem::exists(directory, ec)) continue;
        if (!std::filesystem::create_directories(directory, ec) || ec) continue;
        std::ofstream out(directory / "lodge.json", std::ios::binary);
        out << kManifest;
        out.close();
        if (!out) throw std::runtime_error("cannot write demo store manifest in " + directory.u8string());
        return directory;
    }
    throw std::runtime_error("cannot create a unique demo store directory in " + parent.u8string());
}

std::filesystem::path create_demo_store() { return create_demo_store(std::filesystem::temp_directory_path()); }

bool remove_demo_store(const std::filesystem::path& directory) {
    const auto name = directory.filename().u8string();
    if (name.rfind(kPrefix, 0) != 0) return false;
    std::error_code ec;
    std::filesystem::remove_all(directory, ec);
    return !ec;
}
} // namespace c2::frontend::gui
