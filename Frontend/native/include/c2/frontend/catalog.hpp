#pragma once
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace c2::frontend::catalog {
class Error : public std::runtime_error { public: using std::runtime_error::runtime_error; };
class ScalarConversionError : public Error { public: using Error::Error; };
// Canonical arbitrary-magnitude decimal, never a machine integer or binary64.
struct Integer { std::string decimal; };
using Scalar = std::variant<std::u32string, Integer>;
struct Attribute { std::u32string key, raw; std::size_t line; std::u32string source; };
struct Raw { std::u32string raw; std::size_t line; std::u32string source; };
struct Diagnostic {
    std::u32string code, message, source;
    std::optional<std::size_t> line;
};
// Immutable owned tree handles. Children and copies outlive the original script.
class Node {
public:
    const std::u32string& name() const noexcept;
    std::size_t line() const;
    std::vector<Attribute> attributes() const;
    std::vector<Raw> raw() const;
    std::vector<Node> children() const;
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit Node(std::shared_ptr<const Impl>);
    friend struct Access;
    friend std::optional<Scalar> attribute(const Node&, std::u32string_view);
};
class Script {
public:
    const std::u32string& source() const noexcept;
    const std::string& sha256() const noexcept;
    // All input bytes, including ignored text after a root-level terminator.
    const std::string& bytes() const noexcept;
    Node tree() const;
    std::vector<Diagnostic> diagnostics() const;
    // Python parse_script result: original key order, ASCII escapes, indent 2 + LF.
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit Script(std::shared_ptr<const Impl>);
    friend Script parse_script(std::string_view, std::u32string_view);
};
// Pure memory observations. Source is an opaque label, including NUL/surrogates.
// Actual bytes are bounded at 8 MiB; the nesting budget includes the root frame.
Script parse_script(std::string_view bytes, std::u32string_view source);
// Matching quotes strip first. Decimal conversion follows pinned CPython 3.12's
// default 4300-digit limit (including leading zeros, excluding an ASCII sign).
// Conversion failure does not alter the retained raw tree or private JSON policy.
Scalar scalar(std::u32string_view raw);
// Fold only the stored spelling; requested key/name is compared as supplied.
std::optional<Scalar> attribute(const Node&, std::u32string_view key);
std::vector<Node> blocks(const Script&, std::u32string_view name);
}
