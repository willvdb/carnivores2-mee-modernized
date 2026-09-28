// Real-core integration: the GUI source reads authored disposable stores
// through c2::frontend::Store and never modifies them.
#include "c2/frontend/gui/demo_store.hpp"
#include "c2/frontend/gui/source.hpp"
#include "check.hpp"
#include <fstream>
#include <sstream>

using namespace c2::frontend::gui;

namespace {
std::string read_bytes(const std::filesystem::path& p) {
    std::ifstream in(p, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}
void write_bytes(const std::filesystem::path& p, const std::string& bytes) {
    std::ofstream out(p, std::ios::binary);
    out << bytes;
}
struct Scratch {
    std::filesystem::path root;
    Scratch() {
        root = create_demo_store();   // uniquely owned; only place tests write
    }
    ~Scratch() { remove_demo_store(root); }
};

void demo_store_reads_through_core() {
    Scratch scratch;
    const auto manifest = scratch.root / "lodge.json";
    const auto before = read_bytes(manifest);
    CHECK(before == demo_manifest_bytes());
    const auto result = read_store_snapshot(scratch.root);
    CHECK_NOTE(result.ok(), result.error);
    if (!result.ok()) return;
    const auto& s = *result.snapshot;
    const auto& ids = demo_identities();
    CHECK(s.schema_version == 1);
    CHECK(s.active_hunter == ids.active_hunter);
    CHECK(s.hunters.size() == ids.hunter_ids.size());
    for (std::size_t i = 0; i < s.hunters.size() && i < ids.hunter_ids.size(); ++i)
        CHECK_NOTE(s.hunters[i].id == ids.hunter_ids[i], s.hunters[i].id);
    CHECK(s.hunters.size() >= 4);
    if (s.hunters.size() >= 4) {
        CHECK(s.hunters[0].name == "Anne Trapper" && s.hunters[1].name == "Anne Trapper");
        CHECK(s.hunters[0].id != s.hunters[1].id);
        CHECK(s.hunters[2].name == "Bj\xC3\xB6rn \xC3\x98" "deg\xC3\xA5rd \xE2\x80\x94 \xE7\x8C\x9F\xE4\xBA\xBA");
        CHECK(!s.hunters[0].archived && s.hunters[3].archived);
    }
    CHECK(s.expeditions.size() == ids.expedition_ids.size());
    if (s.expeditions.size() == 3) {
        CHECK(s.expeditions[0].path_flavor == "posix" && expedition_label(s.expeditions[0]) == "carnivores2-mee");
        CHECK(s.expeditions[1].path_flavor == "nt" && expedition_label(s.expeditions[1]) == "Carnivores 2 Classic");
        CHECK(expedition_label(s.expeditions[2]).size() > 60);
    }
    // Reading is observation only: bytes are unchanged and nothing else appeared.
    CHECK(read_bytes(manifest) == before);
    std::size_t entries = 0;
    for (const auto& e : std::filesystem::directory_iterator(scratch.root)) { (void)e; ++entries; }
    CHECK(entries == 1);
}

void invalid_inputs_are_errors_not_fakes() {
    Scratch scratch;
    const auto manifest = scratch.root / "lodge.json";

    auto missing = read_store_snapshot(scratch.root / "does-not-exist");
    CHECK(!missing.ok() && !missing.error.empty());

    // An existing directory without a manifest is the core's empty default
    // manifest, reported honestly, and reading it creates nothing.
    const auto empty_dir = scratch.root / "empty";
    std::filesystem::create_directories(empty_dir);
    auto empty = read_store_snapshot(empty_dir);
    CHECK_NOTE(empty.ok(), empty.error);
    if (empty.ok()) {
        CHECK(!empty.snapshot->manifest_present);
        CHECK(empty.snapshot->hunters.empty() && empty.snapshot->expeditions.empty());
        CHECK(!empty.snapshot->active_hunter);
    }
    CHECK(!std::filesystem::exists(empty_dir / "lodge.json"));

    const auto original = demo_manifest_bytes();
    write_bytes(manifest, "{\"schema_version\": 1, \"hunters\": {");
    auto malformed = read_store_snapshot(scratch.root);
    CHECK(!malformed.ok() && !malformed.error.empty());
    CHECK(read_bytes(manifest) == "{\"schema_version\": 1, \"hunters\": {");   // no repair

    std::string unsupported = original;
    unsupported.replace(unsupported.find("\"schema_version\": 1"), 19, "\"schema_version\": 9");
    write_bytes(manifest, unsupported);
    auto future = read_store_snapshot(scratch.root);
    CHECK(!future.ok() && !future.error.empty());
    CHECK(read_bytes(manifest) == unsupported);   // no upgrade or rewrite

    write_bytes(manifest, "");
    auto blank = read_store_snapshot(scratch.root);
    CHECK(!blank.ok() && !blank.error.empty());

    write_bytes(manifest, original);
    auto restored = read_store_snapshot(scratch.root);
    CHECK_NOTE(restored.ok(), restored.error);
}

void utf8_round_trip_and_corruption_visibility() {
    const std::string text = "Bj\xC3\xB6rn \xE2\x80\x94 \xF0\x9F\xA6\x96 x";
    CHECK(to_utf8(to_utf32(text)) == text);
    CHECK(to_utf32(text).size() == 11);
    // Invalid sequences become U+FFFD instead of silently disappearing.
    CHECK(to_utf32("a\xFFz") == U"a�z");
    CHECK(to_utf32("\xC3") == U"�");               // truncated
    CHECK(to_utf32("\xC0\xAF") == U"��");     // overlong '/': invalid lead + stray continuation
    CHECK(to_utf32("\xE0\x80\x80") == U"\uFFFD\uFFFD\uFFFD");   // overlong NUL
    CHECK(to_utf32("\xED\xA0\x80") == U"\uFFFD\uFFFD\uFFFD");   // encoded surrogate
    CHECK(to_utf8(U"\xD800") == "\xEF\xBF\xBD");        // unpaired surrogate code point
}

void demo_store_removal_is_guarded() {
    Scratch scratch;
    CHECK(!remove_demo_store(scratch.root.parent_path()));   // refuses non-demo names
    CHECK(std::filesystem::exists(scratch.root));
}
} // namespace

int main() {
    demo_store_reads_through_core();
    invalid_inputs_are_errors_not_fakes();
    utf8_round_trip_and_corruption_visibility();
    demo_store_removal_is_guarded();
    if (test::failures) {
        std::cerr << test::failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "gui store tests passed\n";
    return 0;
}
