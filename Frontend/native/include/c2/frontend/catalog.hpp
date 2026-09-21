#pragma once
#include "c2/frontend/content.hpp"
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
    friend struct Access;
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

// Filesystem text observation. A found regular file of at most 1 MiB carries
// its digest and Latin-1 projected str.splitlines lines; a larger one reports
// too_large. Directories and non-found references pass through unchanged.
enum class TextStatus { found, missing, ambiguous, unsafe, too_large };
struct TextObservation {
    std::u32string reference;
    TextStatus status = TextStatus::unsafe;
    std::optional<std::u32string> path; // relative POSIX spelling when resolved
    std::optional<std::string> sha256;
    std::optional<std::u32string> encoding; // 'latin1-byte-projection'
    std::optional<std::vector<std::u32string>> lines;
    std::string export_json() const;
};
TextObservation text_reference(const std::filesystem::path& root, std::u32string reference);

struct Dialect { std::u32string hint, observed, effective, engine_build; };
enum class Group { areas, licenses, weapons, equipment };
struct DeclaredReference { std::u32string field; ReferenceObservation observation; };
struct MapCandidate { std::u32string stem; ReferenceObservation map, rsc; };
// Projection diagnostics retain their reference context fields; absent fields
// were not observed rather than empty.
struct ProjectionDiagnostic {
    std::u32string code, message;
    std::optional<std::u32string> source, entry_id, category;
    std::optional<std::size_t> line, ordinal;
    std::optional<Integer> ai;
    std::optional<std::vector<std::u32string>> entries, paths;
    std::optional<Attribute> observation;
};
// Immutable owned catalog entry. Copies and entries outlive the projection.
class Entry {
public:
    const std::u32string& id() const noexcept;
    const std::u32string& kind() const noexcept;
    std::size_t ordinal() const;
    // Verbatim scalar: absent for None, otherwise the retained string or integer.
    std::optional<Scalar> label() const;
    std::optional<Integer> price() const;
    // Present only when a price observation was assigned to this entry.
    std::optional<Attribute> price_source() const;
    const std::u32string& source() const noexcept;
    std::size_t line() const;
    std::optional<Integer> ai() const;                 // licenses only
    std::optional<std::size_t> slot() const;           // advertised areas only
    std::optional<std::u32string> launch_stem() const; // exactly one complete pair
    std::vector<TextObservation> references() const;
    std::vector<DeclaredReference> declared_references() const;
    std::vector<MapCandidate> map_candidates() const;
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit Entry(std::shared_ptr<const Impl>);
    friend struct ProjectionAccess;
};
class Projection {
public:
    std::size_t projection_version() const noexcept;
    const std::u32string& source() const noexcept;
    const std::string& source_sha256() const noexcept;
    Dialect dialect() const;
    std::vector<Entry> entries(Group) const;
    std::vector<Attribute> starting_score_observations() const;
    std::vector<Node> score_modifier_observations() const;
    std::vector<std::u32string> physical_maps() const;
    std::vector<std::u32string> presentation_references() const;
    std::vector<std::u32string> title_hints() const;
    // _MENU.TXT before _RES.TXT, only those found.
    std::vector<std::pair<std::u32string, Script>> scripts() const;
    std::vector<ProjectionDiagnostic> diagnostics() const;
    std::vector<std::pair<std::u32string, std::u32string>> capabilities() const;
    // Python project result: original key order, ASCII escapes, indent 2 + LF.
    std::string export_json() const;
private:
    struct Impl;
    std::shared_ptr<const Impl> impl_;
    explicit Projection(std::shared_ptr<const Impl>);
    friend struct ProjectionAccess;
};
// Read-only static projection of a resolved root's _MENU.TXT/_RES.TXT and
// referenced presentation files. Scripts above 8 MiB, ambiguous/unsafe script
// references and roots without either script raise the reference errors.
// The hint is passed through verbatim; nothing is executed or interpreted.
Projection project(const std::filesystem::path& root, std::u32string_view dialect_hint = U"unknown");
}
