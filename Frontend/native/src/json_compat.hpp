#pragma once

// PRIVATE implementation/test interface. Do not include from public headers.
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace c2::frontend::compat {
enum class Kind { null, boolean, integer, floating, string, array, object };

struct Value {
    Value() = default;
    Value(const Value& other);
    Value(Value&& other) noexcept;
    Value& operator=(const Value& other);
    Value& operator=(Value&& other) noexcept;
    ~Value() noexcept;

    Kind kind = Kind::null;
    bool boolean = false;
    // Arbitrary-magnitude canonical decimal; never narrowed to double/int64.
    std::string integer;
    double floating = 0;
    // Python str code points, including retained escaped unpaired surrogates.
    std::u32string string;
    std::vector<Value> array;
    // Preserve insertion order for compact json.dumps; journal encoder sorts.
    std::vector<std::pair<std::u32string, Value>> object;
    const Value& at(std::u32string_view key) const;
    bool contains(std::u32string_view key) const;

private:
    void swap(Value& other) noexcept;
    // Used only during destruction to walk ownership without recursion or
    // allocation (including unwinding an allocation/parse/encoding failure).
    Value* cleanup_parent_ = nullptr;
};

class Error : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

Value parse(std::string_view utf8);
std::string compact(const Value& value);
std::string ContentFingerprintV1(const Value& entries);
std::string JournalEvidenceV1(const Value& decoded_journal);
} // namespace c2::frontend::compat
