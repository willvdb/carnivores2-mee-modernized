#include "c2/frontend/catalog.hpp"
#include "c2/frontend/core.hpp"
#include "json_compat.hpp"
#include "schema_compat.hpp"
#include <algorithm>

namespace c2::frontend::catalog {
using compat::Value;
using compat::Kind;
struct Node::Impl { std::shared_ptr<const Value> value; };
struct Script::Impl {
    std::shared_ptr<const Value> value;
    std::string bytes, hash;
};
struct Access {
    static Node node(std::shared_ptr<const Value> value) {
        auto p = std::make_shared<Node::Impl>(); p->value = std::move(value);
        return Node(std::move(p));
    }
};
namespace {
#include "profile_unicode.inc" // Existing pinned Unicode 15.0 Nd table and oracle.
Value string(std::u32string s) { Value v; v.kind = Kind::string; v.string = std::move(s); return v; }
Value number(std::size_t n) { Value v; v.kind = Kind::integer; v.integer = std::to_string(n); return v; }
Value array() { Value v; v.kind = Kind::array; return v; }
Value object() { Value v; v.kind = Kind::object; return v; }
Value node_value(std::u32string name, std::size_t line) {
    auto v = object();
    v.object = {{U"name", string(std::move(name))}, {U"line", number(line)},
        {U"attributes", array()}, {U"children", array()}, {U"raw", array()}};
    return v;
}
Value& field(Value& v, std::u32string_view key) {
    for (auto& kv : v.object) if (kv.first == key) return kv.second;
    throw Error("missing internal catalog field");
}
std::size_t line_number(const Value& v) { return static_cast<std::size_t>(std::stoull(v.at(U"line").integer)); }
std::u32string latin1(std::string_view s) {
    std::u32string out; out.reserve(s.size());
    for (unsigned char c : s) out.push_back(c);
    return out;
}
bool whitespace(unsigned char c) {
    return (c >= 9 && c <= 13) || (c >= 0x1c && c <= 0x20) || c == 0x85 || c == 0xa0;
}
bool generic(unsigned char c) {
    return !whitespace(c) && c != '{' && c != '}' && c != '=' && c != ';' && c != '#' && c != '\'' && c != '"';
}
struct Token { std::string_view text; std::size_t line; };
std::u32string join(const std::vector<Token>& tokens, std::size_t first, std::size_t last) {
    std::u32string out;
    for (auto i = first; i < last; ++i) {
        if (i != first) out.push_back(U' ');
        for (unsigned char c : tokens[i].text) out.push_back(c);
    }
    return out;
}
Value diagnostic(std::u32string code, std::u32string message, std::u32string_view source,
                 std::optional<std::size_t> line = {}) {
    auto v = object();
    v.object = {{U"code", string(std::move(code))}, {U"message", string(std::move(message))},
                {U"source", string(std::u32string(source))}};
    if (line) v.object.emplace_back(U"line", number(*line));
    return v;
}
int decimal(char32_t c) {
    auto end = std::upper_bound(std::begin(decimal_starts), std::end(decimal_starts), c);
    if (end == std::begin(decimal_starts)) return -1;
    auto digit = c - *--end;
    return digit < 10 ? static_cast<int>(digit) : -1;
}
}
Node::Node(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
Script::Script(std::shared_ptr<const Impl> p) : impl_(std::move(p)) {}
const std::u32string& Node::name() const noexcept { return impl_->value->at(U"name").string; }
std::size_t Node::line() const { return line_number(*impl_->value); }
std::vector<Attribute> Node::attributes() const {
    std::vector<Attribute> out;
    for (const auto& v : impl_->value->at(U"attributes").array)
        out.push_back({v.at(U"key").string, v.at(U"raw").string, line_number(v), v.at(U"source").string});
    return out;
}
std::vector<Raw> Node::raw() const {
    std::vector<Raw> out;
    for (const auto& v : impl_->value->at(U"raw").array)
        out.push_back({v.at(U"raw").string, line_number(v), v.at(U"source").string});
    return out;
}
std::vector<Node> Node::children() const {
    std::vector<Node> out;
    for (const auto& v : impl_->value->at(U"children").array)
        out.push_back(Access::node(std::shared_ptr<const Value>(impl_->value, &v)));
    return out;
}
std::string Node::export_json() const { return compat::display(*impl_->value); }
const std::u32string& Script::source() const noexcept { return impl_->value->at(U"source").string; }
const std::string& Script::sha256() const noexcept { return impl_->hash; }
const std::string& Script::bytes() const noexcept { return impl_->bytes; }
Node Script::tree() const { return Access::node(std::shared_ptr<const Value>(impl_->value, &impl_->value->at(U"tree"))); }
std::vector<Diagnostic> Script::diagnostics() const {
    std::vector<Diagnostic> out;
    for (const auto& v : impl_->value->at(U"diagnostics").array) {
        Diagnostic d{v.at(U"code").string, v.at(U"message").string, v.at(U"source").string, {}};
        if (v.contains(U"line")) d.line = line_number(v);
        out.push_back(std::move(d));
    }
    return out;
}
std::string Script::export_json() const { return compat::display(*impl_->value); }
Script parse_script(std::string_view bytes, std::u32string_view source) {
    if (bytes.size() > 8 * 1024 * 1024) throw Error("script exceeds conservative 8 MiB observation limit");
    auto tree = node_value(U"", 1), diagnostics = array();
    std::vector<Value*> stack{&tree};
    std::vector<Token> pending;
    // The first '=' alone divides key/value; LF flushes only assignments.
    auto equal = [&]() { return std::find_if(pending.begin(), pending.end(), [](const Token& t) { return t.text == "="; }); };
    auto flush = [&]() {
        if (pending.empty()) return;
        auto e = equal(); auto value = object();
        if (e != pending.end()) {
            auto index = static_cast<std::size_t>(e - pending.begin());
            value.object = {{U"key", string(join(pending, 0, index))},
                {U"raw", string(join(pending, index + 1, pending.size()))},
                {U"line", number(pending.front().line)}, {U"source", string(std::u32string(source))}};
            field(*stack.back(), U"attributes").array.push_back(std::move(value));
        } else {
            value.object = {{U"raw", string(join(pending, 0, pending.size()))},
                {U"line", number(pending.front().line)}, {U"source", string(std::u32string(source))}};
            field(*stack.back(), U"raw").array.push_back(std::move(value));
        }
        pending.clear();
    };
    std::size_t pos = 0, line = 1;
    // Ordered regex finditer over Latin-1. A comment starts only at a new match;
    // slashes inside an already-consumed generic token are ordinary characters.
    while (pos < bytes.size()) {
        const auto start = pos;
        const auto c = static_cast<unsigned char>(bytes[pos]);
        if (c == ';' || c == '#' || (c == '/' && pos + 1 < bytes.size() && bytes[pos + 1] == '/')) {
            while (pos < bytes.size() && bytes[pos] != '\n') ++pos;
            continue;
        }
        if (c == '\'' || c == '"') {
            auto end = pos + 1;
            while (end < bytes.size() && bytes[end] != static_cast<char>(c) && bytes[end] != '\n') ++end;
            if (end == bytes.size() || bytes[end] == '\n') { ++pos; continue; }
            pos = end + 1;
        } else if (c == '\n' || c == '{' || c == '}' || c == '=') ++pos;
        else if (generic(c)) { do { ++pos; } while (pos < bytes.size() && generic(static_cast<unsigned char>(bytes[pos]))); }
        else { ++pos; continue; }
        const auto token = bytes.substr(start, pos - start);
        if (token == "." && stack.size() == 1) { flush(); break; }
        if (token == "\n") {
            if (equal() != pending.end()) flush();
            ++line;
        } else if (token == "{") {
            if (stack.size() >= 128) throw Error("script nesting exceeds observation limit");
            auto node = node_value(join(pending, 0, pending.size()), pending.empty() ? line : pending.front().line);
            pending.clear();
            auto& children = field(*stack.back(), U"children").array;
            children.push_back(std::move(node)); stack.push_back(&children.back());
        } else if (token == "}") {
            flush();
            if (stack.size() == 1) diagnostics.array.push_back(diagnostic(U"unmatched-brace", U"Unmatched closing brace.", source, line));
            else stack.pop_back();
        } else pending.push_back({token, line});
    }
    flush();
    if (stack.size() != 1) diagnostics.array.push_back(diagnostic(U"unclosed-block", U"Unclosed script block.", source));
    auto result = std::make_shared<Script::Impl>();
    result->bytes = std::string(bytes); result->hash = c2::frontend::sha256(bytes);
    auto value = object();
    value.object = {{U"source", string(std::u32string(source))}, {U"sha256", string(latin1(result->hash))},
        {U"tree", std::move(tree)}, {U"diagnostics", std::move(diagnostics)}};
    result->value = std::make_shared<const Value>(std::move(value));
    return Script(std::move(result));
}
Scalar scalar(std::u32string_view raw) {
    if (raw.size() >= 2 && raw.front() == raw.back() && (raw.front() == U'\'' || raw.front() == U'"'))
        return std::u32string(raw.substr(1, raw.size() - 2));
    auto first = !raw.empty() && (raw.front() == U'+' || raw.front() == U'-') ? std::size_t{1} : std::size_t{0};
    if (first == raw.size()) return std::u32string(raw);
    std::string digits; digits.reserve(raw.size() - first);
    for (auto i = first; i < raw.size(); ++i) {
        auto d = decimal(raw[i]);
        if (d < 0) return std::u32string(raw);
        digits.push_back(static_cast<char>('0' + d));
    }
    if (digits.size() > 4300) throw ScalarConversionError("Exceeds the limit (4300 digits) for integer string conversion: value has " +
        std::to_string(digits.size()) + " digits; use sys.set_int_max_str_digits() to increase the limit");
    auto nonzero = digits.find_first_not_of('0');
    digits = nonzero == digits.npos ? "0" : digits.substr(nonzero);
    if (raw.front() == U'-' && digits != "0") digits.insert(digits.begin(), '-');
    return Integer{std::move(digits)};
}
std::optional<Scalar> attribute(const Node& node, std::u32string_view key) {
    const Value* found = nullptr;
    for (const auto& v : node.impl_->value->at(U"attributes").array) {
        if (schema::casefold(v.at(U"key").string) != key) continue;
        if (found) return {};
        found = &v;
    }
    return found ? std::optional<Scalar>(scalar(found->at(U"raw").string)) : std::nullopt;
}
std::vector<Node> blocks(const Script& script, std::u32string_view name) {
    std::vector<Node> out;
    for (auto& node : script.tree().children()) if (schema::casefold(node.name()) == name) out.push_back(std::move(node));
    return out;
}
}
