// Test-only line-framed pure adapter; never added to the production CLI.
#include "c2/frontend/catalog.hpp"
#include "c2/frontend/core.hpp"
#include "json_compat.hpp"
#include <iostream>
#include <type_traits>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace c2::frontend;
using compat::Value;
using compat::Kind;
static_assert(!std::is_default_constructible_v<catalog::Node>);
static_assert(!std::is_default_constructible_v<catalog::Script>);
static Value text(std::u32string s) { Value v; v.kind = Kind::string; v.string = std::move(s); return v; }
static Value ascii(std::string_view s) { return text({s.begin(), s.end()}); }
static Value scalar_value(const catalog::Scalar& s) {
    if (auto p = std::get_if<std::u32string>(&s)) return text(*p);
    Value v; v.kind = Kind::integer; v.integer = std::get<catalog::Integer>(s).decimal; return v;
}
static std::string unhex(std::u32string_view s) {
    if (s.size() % 2) throw std::runtime_error("odd hex");
    auto digit = [](char32_t c) -> unsigned {
        if (c >= U'0' && c <= U'9') return c - U'0';
        if (c >= U'a' && c <= U'f') return c - U'a' + 10;
        if (c >= U'A' && c <= U'F') return c - U'A' + 10;
        throw std::runtime_error("bad hex");
    };
    std::string out;
    for (std::size_t i = 0; i < s.size(); i += 2) out.push_back(static_cast<char>((digit(s[i]) << 4) | digit(s[i + 1])));
    return out;
}
static void require(bool v) { if (!v) throw std::runtime_error("catalog typed ownership assertion"); }
static void typed(const catalog::Node& root) {
    // Every typed field must retain the same raw/source metadata as the export.
    std::vector<catalog::Node> pending{root};
    while (!pending.empty()) {
        auto node = pending.back(); pending.pop_back();
        auto v = compat::parse(node.export_json());
        require(node.name() == v.at(U"name").string && std::to_string(node.line()) == v.at(U"line").integer);
        auto attrs = node.attributes(); require(attrs.size() == v.at(U"attributes").array.size());
        for (std::size_t i = 0; i < attrs.size(); ++i) {
            const auto& a = attrs[i]; const auto& b = v.at(U"attributes").array[i];
            require(a.key == b.at(U"key").string && a.raw == b.at(U"raw").string && a.source == b.at(U"source").string && std::to_string(a.line) == b.at(U"line").integer);
        }
        auto raws = node.raw(); require(raws.size() == v.at(U"raw").array.size());
        for (std::size_t i = 0; i < raws.size(); ++i) {
            const auto& a = raws[i]; const auto& b = v.at(U"raw").array[i];
            require(a.raw == b.at(U"raw").string && a.source == b.at(U"source").string && std::to_string(a.line) == b.at(U"line").integer);
        }
        auto children = node.children(); require(children.size() == v.at(U"children").array.size());
        for (std::size_t i = 0; i < children.size(); ++i) {
            require(children[i].export_json() == compat::display(v.at(U"children").array[i]));
            pending.push_back(children[i]);
        }
    }
}
static std::string run(const Value& r) {
    const auto& op = r.at(U"op").string;
    if (op == U"scalar") return compat::display(scalar_value(catalog::scalar(r.at(U"raw").string)));
    auto original = unhex(r.at(U"bytes_hex").string); auto source = r.at(U"source").string;
    auto bytes = original; auto label = source;
    auto script = [&]() { auto parsed = catalog::parse_script(bytes, label); return parsed; }();
    bytes.assign("changed"); label.assign(U"changed"); // Caller buffers are independent.
    auto copy = script;
    require(copy.bytes() == original && copy.source() == source && copy.sha256() == sha256(original));
    if (op == U"parse") return copy.export_json();
    if (op == U"typed") {
        auto v = compat::parse(copy.export_json()); auto ds = copy.diagnostics();
        require(ds.size() == v.at(U"diagnostics").array.size());
        for (std::size_t i = 0; i < ds.size(); ++i) {
            const auto& d = ds[i]; const auto& e = v.at(U"diagnostics").array[i];
            require(d.code == e.at(U"code").string && d.message == e.at(U"message").string && d.source == e.at(U"source").string);
            require(d.line.has_value() == e.contains(U"line"));
            if (d.line) require(std::to_string(*d.line) == e.at(U"line").integer);
        }
        typed(copy.tree());
        // Retain a node independently, release every owning script, then query.
        auto retained = copy.tree(); auto before = retained.export_json();
        script = catalog::parse_script("", U""); copy = script;
        require(retained.export_json() == before); typed(retained);
        return compat::display(v);
    }
    if (op == U"attribute") {
        auto node = copy.tree();
        for (const auto& i : r.at(U"node_path").array) node = node.children().at(static_cast<std::size_t>(std::stoull(i.integer)));
        auto value = catalog::attribute(node, r.at(U"key").string);
        return compat::display(value ? scalar_value(*value) : Value{});
    }
    if (op == U"blocks") {
        auto nodes = catalog::blocks(copy, r.at(U"name").string);
        script = catalog::parse_script("", U""); copy = script;
        Value value; value.kind = Kind::array;
        for (const auto& node : nodes) value.array.push_back(compat::parse(node.export_json()));
        return compat::display(value);
    }
    throw std::runtime_error("unknown test operation");
}
int main() {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY); _setmode(_fileno(stdout), _O_BINARY);
#endif
    // JSON transport combines adjacent surrogate escapes. The pure API itself
    // must retain two separately supplied surrogate code points without folding.
    const std::u32string surrogate_source{0xd800, 0xdfff};
    auto retained_source = catalog::parse_script("a=1\n", surrogate_source);
    require(retained_source.source() == surrogate_source);
    require(retained_source.tree().attributes().front().source == surrogate_source);
    std::string line;
    while (std::getline(std::cin, line)) {
        Value out; out.kind = Kind::object; Value ok; ok.kind = Kind::boolean;
        auto failure = [&](std::string_view kind, const std::exception& e) {
            out.object = {{U"ok", ok}, {U"error", ascii(e.what())}, {U"kind", ascii(kind)}};
        };
        try {
            auto bytes = run(compat::parse(line)); ok.boolean = true;
            out.object = {{U"ok", ok}, {U"value", compat::parse(bytes)}, {U"json", ascii(bytes)}};
        } catch (const catalog::ScalarConversionError& e) { failure("scalar-conversion", e); }
        catch (const catalog::Error& e) { failure("catalog", e); }
        catch (const std::exception& e) { failure("unexpected", e); }
        std::cout << compat::compact(out) << '\n' << std::flush;
    }
}
