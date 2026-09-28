#include "c2/frontend/catalog_presentation.hpp"
#include "schema_compat.hpp"
#include "../../../Hunt/Loaders/ScriptValueParse.h"
#include <algorithm>

namespace c2::frontend::catalog {
std::optional<int> legacy_integer(const Attribute& a) {
    std::string bytes;
    for (char32_t c : a.raw) {
        if (c > 255 || c == 0) return {};
        bytes.push_back(static_cast<char>(c));
    }
    const auto parsed = ParseScriptIntStatus(bytes.c_str(), 0);
    if (parsed.status != ScriptScalarStatus::Ok) return {};
    return parsed.value;
}
namespace {
std::optional<Attribute> last_attribute(const Node& node, std::u32string_view key) {
    std::optional<Attribute> result;
    for (const auto& a : node.attributes())
        if (schema::casefold(a.key) == key) result = a;
    return result;
}
std::u32string digits(std::size_t n) {
    const auto s = std::to_string(n);
    return {s.begin(), s.end()};
}
}
HuntableRoster resolve_huntables(const std::filesystem::path& root, const Projection& projection) {
    HuntableRoster out;
    const auto scripts = projection.scripts();
    if (scripts.empty()) return out;
    const auto& script = scripts.front().second;
    bool uncertain_slot = false;
    for (const auto& section : blocks(script, U"characters")) {
        for (const auto& node : section.children()) {
            const auto ai_source = last_attribute(node, U"ai");
            const auto ai = ai_source ? legacy_integer(*ai_source) : std::nullopt;
            if (!ai) {
                // Missing AI defaults to ambient in the engine; malformed AI can
                // be clamped to a huntable value. Do not guess later slot IDs.
                if (ai_source) {
                    uncertain_slot = true;
                    out.diagnostics.push_back({U"huntable-ai-unresolved", U"Invalid AI leaves subsequent roster positions unresolved.", script.source(), node.line()});
                }
                continue;
            }
            if (*ai < 10) continue;
            if (uncertain_slot) continue;
            const auto ordinal = out.entries.size();
            auto picture = U"HUNTDAT/MENU/PICS/DINO" + digits(ordinal + 1) + U".TGA";
            bool explicit_picture = false;
            if (const auto pic = last_attribute(node, U"pic")) {
                const auto value = scalar(pic->raw);
                if (const auto text = std::get_if<std::u32string>(&value); text && !text->empty()) {
                    picture = *text;
                    explicit_picture = true;
                }
            }
            out.entries.push_back({ordinal, script.source(), node.line(), *ai,
                attribute(node, U"name"), {}, explicit_picture,
                resolve_reference(root, std::move(picture)),
                text_reference(root, U"HUNTDAT/MENU/TXT/DINO" + digits(ordinal + 1) + U".TXM")});
        }
    }
    // Upstream prices begin at the first AI 10 (or the first huntable if absent).
    const auto first = std::find_if(out.entries.begin(), out.entries.end(), [](const auto& e) { return e.ai == 10; });
    std::size_t index = first == out.entries.end() ? 0 : static_cast<std::size_t>(first - out.entries.begin());
    for (const auto& section : blocks(script, U"prices")) {
        for (const auto& a : section.attributes()) {
            if (schema::casefold(a.key) != U"dino") continue;
            const auto value = legacy_integer(a);
            if (index < out.entries.size() && value && *value >= 0) out.entries[index].price = value;
            else out.diagnostics.push_back({U"huntable-price-unresolved", U"Invalid, negative, or surplus huntable price retained only as an observation.", a.source, a.line});
            ++index;
        }
    }
    return out;
}
}
