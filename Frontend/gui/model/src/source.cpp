#include "c2/frontend/gui/source.hpp"
#include "c2/frontend/store.hpp"
#include <exception>
#include <system_error>

namespace c2::frontend::gui {
namespace {
void append_utf8(std::string& out, char32_t c) {
    if (c >= 0xD800 && c <= 0xDFFF) c = 0xFFFD;
    if (c > 0x10FFFF) c = 0xFFFD;
    if (c < 0x80) {
        out.push_back(static_cast<char>(c));
    } else if (c < 0x800) {
        out.push_back(static_cast<char>(0xC0 | (c >> 6)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else if (c < 0x10000) {
        out.push_back(static_cast<char>(0xE0 | (c >> 12)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    } else {
        out.push_back(static_cast<char>(0xF0 | (c >> 18)));
        out.push_back(static_cast<char>(0x80 | ((c >> 12) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | ((c >> 6) & 0x3F)));
        out.push_back(static_cast<char>(0x80 | (c & 0x3F)));
    }
}
} // namespace

std::string to_utf8(const std::u32string& text) {
    std::string out;
    out.reserve(text.size());
    for (char32_t c : text) append_utf8(out, c);
    return out;
}

std::u32string to_utf32(const std::string& utf8) {
    std::u32string out;
    std::size_t i = 0;
    const auto n = utf8.size();
    while (i < n) {
        const auto b0 = static_cast<unsigned char>(utf8[i]);
        std::size_t len = 0;
        char32_t c = 0;
        if (b0 < 0x80) { len = 1; c = b0; }
        else if (b0 >= 0xC2 && b0 <= 0xDF) { len = 2; c = b0 & 0x1F; }
        else if ((b0 & 0xF0) == 0xE0) { len = 3; c = b0 & 0x0F; }
        else if (b0 >= 0xF0 && b0 <= 0xF4) { len = 4; c = b0 & 0x07; }
        else { out.push_back(0xFFFD); ++i; continue; }   // stray continuation, 0xC0/0xC1, 0xF5+
        if (i + len > n) { out.push_back(0xFFFD); break; }
        bool valid = true;
        for (std::size_t k = 1; k < len; ++k) {
            const auto b = static_cast<unsigned char>(utf8[i + k]);
            if ((b & 0xC0) != 0x80) { valid = false; break; }
            c = (c << 6) | (b & 0x3F);
        }
        const bool overlong = (len == 2 && c < 0x80) || (len == 3 && c < 0x800) || (len == 4 && c < 0x10000);
        if (!valid || overlong || c > 0x10FFFF || (c >= 0xD800 && c <= 0xDFFF)) {
            out.push_back(0xFFFD);
            ++i;   // resynchronize on the next byte; stray continuations show as U+FFFD too
            continue;
        }
        out.push_back(c);
        i += len;
    }
    return out;
}

std::string expedition_label(const ExpeditionRow& row) {
    const bool nt = row.path_flavor == "nt";
    std::string path = row.path;
    auto is_sep = [nt](char c) { return c == '/' || (nt && c == '\\'); };
    while (!path.empty() && is_sep(path.back())) path.pop_back();
    std::size_t cut = path.size();
    while (cut > 0 && !is_sep(path[cut - 1])) --cut;
    std::string label = path.substr(cut);
    return label.empty() ? row.path : label;
}

SnapshotResult read_store_snapshot(const std::filesystem::path& directory) {
    SnapshotResult result;
    try {
        std::error_code ec;
        if (!std::filesystem::is_directory(directory, ec)) {
            result.error = "store directory does not exist: " + directory.u8string();
            return result;
        }
        const bool manifest_present = std::filesystem::is_regular_file(directory / "lodge.json", ec);
        Store store(directory);
        const Manifest manifest = store.read();
        StoreSnapshot snapshot;
        snapshot.directory = store.directory().u8string();
        snapshot.manifest_present = manifest_present;
        snapshot.schema_version = manifest.schema_version();
        snapshot.associations = play_loop::associations(manifest);
        if (auto active = manifest.active_hunter()) snapshot.active_hunter = to_utf8(*active);
        for (const auto& h : manifest.hunters())
            snapshot.hunters.push_back({to_utf8(h.id), to_utf8(h.name), h.archived});
        for (const auto& e : manifest.expeditions())
            snapshot.expeditions.push_back({to_utf8(e.id), to_utf8(e.mode), to_utf8(e.path_flavor), to_utf8(e.path)});
        result.snapshot = std::move(snapshot);
    } catch (const ResourceExhausted& e) {
        result.error = std::string("store read exhausted a resource limit: ") + e.what();
    } catch (const StoreError& e) {
        result.error = std::string("store cannot be read: ") + e.what();
    } catch (const std::exception& e) {
        result.error = std::string("store read failed: ") + e.what();
    }
    if (!result.ok() && result.error.empty()) result.error = "store read failed";
    return result;
}
} // namespace c2::frontend::gui
