#include "c2/frontend/catalog_presentation.hpp"
#include <chrono>
#include <fstream>
#include <iostream>
using namespace c2::frontend;
namespace fs = std::filesystem;
static void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
static void write(const fs::path& path, const std::string& text) {
    fs::create_directories(path.parent_path());
    std::ofstream file(path, std::ios::binary); file << text;
    if (!file) throw std::runtime_error("fixture write");
}
int main() {
    const auto root = fs::temp_directory_path() / ("c2-roster-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    struct Cleanup { fs::path root; ~Cleanup() { std::error_code ec; fs::remove_all(root, ec); } } cleanup{root};
    try {
        std::string script = "characters {\n{\nname = 'Ambient'\nai = 3\n}\n";
        const char* names[] = {"Parasaurolophus", "Ankylosaurus", "Stegosaurus", "Allosaurus", "Chasmosaurus", "Velociraptor", "Spinosaurus", "Iguanodon", "Carnotaurus", "T-Rex"};
        for (int i = 0; i < 10; ++i) {
            const int ai = i < 8 ? i + 10 : i == 8 ? 17 : 18;
            script += "{\nname = '" + std::string(names[i]) + "'\nai = " + std::to_string(ai) + "\n";
            if (i == 8) script += "pic = 'huntdat\\menu\\pics\\custom.tga'\n";
            script += "}\n";
            write(root / "HuntDat/Menu/Pics" / ("Dino" + std::to_string(i + 1) + ".tga"), "thumbnail");
            write(root / "HuntDat/Menu/Txt" / ("Dino" + std::to_string(i + 1) + ".txm"), names[i]);
        }
        script += "}\nprices {\n";
        for (int i = 0; i < 10; ++i) script += "dino = " + std::to_string(i * 100) + ".0f\n";
        script += "}\n";
        write(root / "HuntDat/Menu/Pics/custom.tga", "custom");
        write(root / "HuntDat/_RES.TXT", script);
        const auto projection = catalog::project(root);
        const auto frozen = projection.export_json();
        const auto roster = catalog::resolve_huntables(root, projection);
        require(roster.entries.size() == 10 && roster.diagnostics.empty(), "stock roster");
        for (int i = 0; i < 10; ++i) {
            const auto& e = roster.entries[i];
            require(e.ordinal == static_cast<std::size_t>(i) && e.price == i * 100, "ordered prices");
            require(e.thumbnail.status == ReferenceStatus::found, "case-insensitive picture resolution");
            require(e.description.lines && e.description.lines->at(0) == std::u32string(names[i], names[i] + std::char_traits<char>::length(names[i])), "roster description");
        }
        require(roster.entries[7].ai == 17 && roster.entries[8].ai == 17 && roster.entries[9].ai == 18, "duplicate AI ordering");
        require(roster.entries[7].thumbnail.reference == U"HUNTDAT/MENU/PICS/DINO8.TGA", "Iguanodon slot 8");
        require(roster.entries[8].explicit_picture && roster.entries[8].thumbnail.path == U"HuntDat/Menu/Pics/custom.tga", "pic override");
        require(roster.entries[9].thumbnail.reference == U"HUNTDAT/MENU/PICS/DINO10.TGA", "T-Rex slot 10");
        require(projection.export_json() == frozen && !projection.entries(catalog::Group::licenses)[1].price(), "frozen observation preserved");
        // Remove the override and use an engine-compatible AI numeric suffix.
        auto pos = script.find("pic = '"); auto end = script.find('\n', pos); script.erase(pos, end - pos + 1);
        pos = script.find("ai = 17\n"); script.replace(pos, 8, "ai = 17L\n");
        write(root / "HuntDat/_RES.TXT", script);
        const auto prefixed = catalog::resolve_huntables(root, catalog::project(root));
        require(prefixed.entries.size() == 10 && prefixed.entries[8].thumbnail.reference == U"HUNTDAT/MENU/PICS/DINO9.TGA", "Carnotaurus default and numeric prefix");
        write(root / "HuntDat/_MENU.TXT", "characters {\n{\nai = 18\npic = '../escape.tga'\n}\n}\n");
        const auto preferred = catalog::resolve_huntables(root, catalog::project(root));
        require(preferred.entries.size() == 1 && preferred.entries[0].thumbnail.status == ReferenceStatus::unsafe, "menu precedence and unsafe pic");
        catalog::Attribute a{U"ai", U"17garbage", 1, U"fixture"};
        require(catalog::legacy_integer(a) == 17, "legacy prefix");
        for (auto raw : {U"99999999999999999999", U"nan", U"'17'", U"", U"\u0661"}) {
            a.raw = raw; require(!catalog::legacy_integer(a), "no recovery grants");
        }
        std::cout << "huntable presentation contracts passed\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << e.what() << '\n'; return 1; }
}
