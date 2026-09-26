#include "json_compat.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <limits>
#include <set>

namespace c2::frontend::compat {
namespace {
static_assert(std::numeric_limits<double>::is_iec559 &&
              std::numeric_limits<double>::digits == 53 && sizeof(double) == 8,
              "Python compatibility requires IEEE-754 binary64");

bool digit(char c) { return c >= '0' && c <= '9'; }
int hex(char c) {
    if (digit(c)) return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    throw Error("invalid Unicode escape");
}

// from_chars reports both overflow and underflow as result_out_of_range.
// The sign of the decimal order distinguishes these *after* conversion failed.
// Saturation only classifies a conversion failure; it never changes a value.
bool underflow(std::string_view token) {
    if (token.front() == '-') token.remove_prefix(1);
    const auto exponent_at = token.find_first_of("eE");
    const auto mantissa = token.substr(0, exponent_at);
    const auto dot = mantissa.find('.');
    const auto point = dot == std::string_view::npos ? mantissa.size() : dot;
    const auto nonzero = mantissa.find_first_not_of("0.");
    if (nonzero == std::string_view::npos) return true;
    long long exponent = 0;
    if (exponent_at != std::string_view::npos) {
        auto digits = token.substr(exponent_at + 1);
        const bool negative = digits.front() == '-';
        if (digits.front() == '-' || digits.front() == '+') digits.remove_prefix(1);
        // JSON input is bounded by its string_view. Saturate well beyond any
        // practical mantissa length without integer overflow on hostile tokens.
        for (char c : digits) exponent = std::min(1000000000LL, exponent * 10 + c - '0');
        if (negative) exponent = -exponent;
    }
    const auto order = static_cast<long long>(point) - static_cast<long long>(nonzero)
        - (nonzero < point ? 1 : 0);
    return exponent + order < 0;
}

class Parser {
    std::string_view source_;
    std::size_t position_ = 0;
    std::size_t max_depth_;
    bool last_wins_ = false;

    char peek() const { return position_ == source_.size() ? '\0' : source_[position_]; }
    char take() {
        if (position_ == source_.size()) throw Error("unexpected end of JSON");
        return source_[position_++];
    }
    void whitespace() {
        while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') ++position_;
    }
    void expect(char c) { if (take() != c) throw Error("unexpected JSON token"); }
    void literal(std::string_view text) {
        if (source_.substr(position_, text.size()) != text) throw Error("invalid JSON literal");
        position_ += text.size();
    }
    char32_t escaped_unit() {
        char32_t result = 0;
        for (int i = 0; i < 4; ++i) result = result * 16 + hex(take());
        return result;
    }
    std::u32string string() {
        expect('"');
        std::u32string result;
        while (true) {
            const auto byte = static_cast<unsigned char>(take());
            if (byte == '"') return result;
            if (byte < 0x20) throw Error("unescaped string control");
            if (byte == '\\') {
                switch (take()) {
                case '"': result.push_back('"'); break;
                case '\\': result.push_back('\\'); break;
                case '/': result.push_back('/'); break;
                case 'b': result.push_back('\b'); break;
                case 'f': result.push_back('\f'); break;
                case 'n': result.push_back('\n'); break;
                case 'r': result.push_back('\r'); break;
                case 't': result.push_back('\t'); break;
                case 'u': {
                    auto codepoint = escaped_unit();
                    if (codepoint >= 0xd800 && codepoint <= 0xdbff &&
                        source_.substr(position_, 2) == "\\u") {
                        const auto saved = position_;
                        position_ += 2;
                        const auto low = escaped_unit();
                        if (low >= 0xdc00 && low <= 0xdfff)
                            codepoint = 0x10000 + (codepoint - 0xd800) * 1024 + low - 0xdc00;
                        else position_ = saved;
                    }
                    result.push_back(codepoint);
                    break;
                }
                default: throw Error("invalid string escape");
                }
            } else if (byte < 0x80) result.push_back(byte);
            else {
                int count = 0;
                char32_t codepoint = 0;
                char32_t minimum = 0;
                if (byte >= 0xc2 && byte <= 0xdf) { count = 1; codepoint = byte & 31; minimum = 0x80; }
                else if (byte >= 0xe0 && byte <= 0xef) { count = 2; codepoint = byte & 15; minimum = 0x800; }
                else if (byte >= 0xf0 && byte <= 0xf4) { count = 3; codepoint = byte & 7; minimum = 0x10000; }
                else throw Error("invalid UTF-8 lead byte");
                while (count--) {
                    const auto next = static_cast<unsigned char>(take());
                    if ((next & 0xc0) != 0x80) throw Error("invalid UTF-8 continuation");
                    codepoint = codepoint * 64 + (next & 63);
                }
                if (codepoint < minimum || codepoint > 0x10ffff ||
                    (codepoint >= 0xd800 && codepoint <= 0xdfff)) throw Error("invalid UTF-8 code point");
                result.push_back(codepoint);
            }
        }
    }
    Value number() {
        const auto start = position_;
        if (peek() == '-') ++position_;
        if (peek() == '0') ++position_;
        else {
            if (!digit(peek())) throw Error("invalid JSON number");
            while (digit(peek())) ++position_;
        }
        bool floating = false;
        if (peek() == '.') {
            floating = true;
            ++position_;
            if (!digit(peek())) throw Error("missing fraction digits");
            while (digit(peek())) ++position_;
        }
        if (peek() == 'e' || peek() == 'E') {
            floating = true;
            ++position_;
            if (peek() == '+' || peek() == '-') ++position_;
            if (!digit(peek())) throw Error("missing exponent digits");
            while (digit(peek())) ++position_;
        }
        const auto token = source_.substr(start, position_ - start);
        Value result;
        if (!floating) {
            result.kind = Kind::integer;
            result.integer = token == "-0" ? "0" : std::string(token);
        } else {
            result.kind = Kind::floating;
            const auto converted = std::from_chars(token.data(), token.data() + token.size(),
                                                    result.floating, std::chars_format::general);
            if (converted.ec == std::errc::result_out_of_range) {
                result.floating = underflow(token) ? 0.0 : std::numeric_limits<double>::infinity();
                if (token.front() == '-') result.floating = -result.floating;
            } else if (converted.ec != std::errc{} || converted.ptr != token.data() + token.size())
                throw Error("binary64 conversion failed");
        }
        return result;
    }
    Value atom(std::size_t depth) {
        // Operational policy, not a persistent-format depth restriction.
        // The foundation default is preserved; repository callers may raise it.
        if (depth > max_depth_) throw ResourceError("JSON nesting resource limit");
        whitespace();
        Value result;
        switch (peek()) {
        case 'n': literal("null"); break;
        case 't': literal("true"); result.kind = Kind::boolean; result.boolean = true; break;
        case 'f': literal("false"); result.kind = Kind::boolean; break;
        case '"': result.kind = Kind::string; result.string = string(); break;
        case '[':
            result.kind = Kind::array;
            ++position_;
            break;
        case '{':
            result.kind = Kind::object;
            ++position_;
            break;
        case 'N': literal("NaN"); result.kind = Kind::floating;
            result.floating = std::numeric_limits<double>::quiet_NaN(); break;
        case 'I': literal("Infinity"); result.kind = Kind::floating;
            result.floating = std::numeric_limits<double>::infinity(); break;
        default:
            if (source_.substr(position_, 9) == "-Infinity") {
                literal("-Infinity"); result.kind = Kind::floating;
                result.floating = -std::numeric_limits<double>::infinity();
            } else result = number();
        }
        return result;
    }
public:
    explicit Parser(std::string_view source, std::size_t depth, bool last_wins = false)
        : source_(source), max_depth_(depth), last_wins_(last_wins) {}
    Value run() {
        struct Frame {
            Value* container;
            bool first = true;
            std::set<std::u32string> keys;
        };
        auto result = atom(0);
        std::vector<Frame> stack;
        if (result.kind == Kind::array || result.kind == Kind::object)
            stack.push_back({&result, true, {}});
        while (!stack.empty()) {
            auto& frame = stack.back();
            const bool object = frame.container->kind == Kind::object;
            whitespace();
            if (peek() == (object ? '}' : ']')) {
                ++position_;
                stack.pop_back();
                continue;
            }
            if (!frame.first) { expect(','); whitespace(); }
            frame.first = false;
            Value* child;
            if (object) {
                auto key = string();
                const bool duplicate = !frame.keys.insert(key).second;
                if (duplicate && !last_wins_) throw Error("duplicate JSON key");
                whitespace();
                expect(':');
                if (duplicate) {
                    // json.loads dict semantics: the key keeps its first
                    // position and takes the last value.
                    auto& members = frame.container->object;
                    child = &std::find_if(members.begin(), members.end(),
                        [&](const auto& m) { return m.first == key; })->second;
                    *child = Value{};
                } else {
                    frame.container->object.emplace_back(std::move(key), Value{});
                    child = &frame.container->object.back().second;
                }
            } else {
                frame.container->array.emplace_back();
                child = &frame.container->array.back();
            }
            *child = atom(stack.size());
            // Ancestors' child vectors are not grown while a descendant is
            // active, so pointers in these heap frames remain stable.
            if (child->kind == Kind::array || child->kind == Kind::object)
                stack.push_back({child, true, {}});
        }
        whitespace();
        if (position_ != source_.size()) throw Error("trailing JSON data");
        return result;
    }
};

void escape_unit(std::string& output, char32_t value) {
    constexpr char hex_digits[] = "0123456789abcdef";
    output += "\\u";
    for (int shift = 12; shift >= 0; shift -= 4) output += hex_digits[(value >> shift) & 15];
}
void quoted(std::string& output, const std::u32string& text) {
    output += '"';
    for (auto c : text) {
        switch (c) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (c >= 0x20 && c < 0x7f) output += static_cast<char>(c);
            else if (c <= 0xffff) escape_unit(output, c);
            else if (c <= 0x10ffff) {
                escape_unit(output, 0xd800 + ((c - 0x10000) >> 10));
                escape_unit(output, 0xdc00 + ((c - 0x10000) & 1023));
            } else throw Error("invalid Unicode code point in DOM");
        }
    }
    output += '"';
}

std::string float_text(double value, bool allow_nonfinite) {
    if (!std::isfinite(value)) {
        if (!allow_nonfinite) throw Error("nonfinite journal number");
        if (std::isnan(value)) return "NaN";
        return std::signbit(value) ? "-Infinity" : "Infinity";
    }
    if (value == 0) return std::signbit(value) ? "-0.0" : "0.0";
    // Scientific mode selects shortest significant digits independently of the
    // standard library's fixed-vs-scientific threshold. Python uses [-4, 16).
    char buffer[64];
    const auto converted = std::to_chars(buffer, buffer + sizeof(buffer), value,
                                          std::chars_format::scientific);
    if (converted.ec != std::errc{}) throw Error("binary64 formatting failed");
    std::string text(buffer, converted.ptr);
    const auto marker = text.find('e');
    int exponent = 0;
    auto exponent_start = text.data() + marker + 1;
    if (*exponent_start == '+') ++exponent_start;
    const auto parsed = std::from_chars(exponent_start, text.data() + text.size(), exponent);
    if (parsed.ec != std::errc{}) throw Error("binary64 exponent formatting failed");
    if (exponent < -4 || exponent >= 16) return text;
    const bool negative = text.front() == '-';
    auto digits = text.substr(negative ? 1 : 0, marker - (negative ? 1 : 0));
    digits.erase(std::remove(digits.begin(), digits.end(), '.'), digits.end());
    const int point = exponent + 1;
    std::string result = negative ? "-" : "";
    if (point <= 0) result += "0." + std::string(static_cast<std::size_t>(-point), '0') + digits;
    else if (static_cast<std::size_t>(point) >= digits.size())
        result += digits + std::string(static_cast<std::size_t>(point) - digits.size(), '0') + ".0";
    else result += digits.substr(0, point) + "." + digits.substr(point);
    return result;
}

// pretty: indent=2 and allow_nan=False (journal/manifest encoders); spaced:
// json.dumps default separators (', ', ': ') without indent. Both
// separators and sorting are independent of allow_nan handling.
void encode(std::string& output, const Value& value, bool pretty, bool sort_keys = true, std::size_t max_depth = 1000, bool spaced = false) {
    struct Frame {
        const Value* container;
        std::size_t next = 0;
        std::vector<std::size_t> order;
    };
    std::vector<Frame> stack;
    auto atom = [&](const Value& node) {
        if (stack.size() > max_depth) throw ResourceError("JSON nesting resource limit");
        switch (node.kind) {
        case Kind::null: output += "null"; break;
        case Kind::boolean: output += node.boolean ? "true" : "false"; break;
        case Kind::integer: output += node.integer; break;
        case Kind::floating: output += float_text(node.floating, !pretty); break;
        case Kind::string: quoted(output, node.string); break;
        case Kind::array:
        case Kind::object: {
            const bool object = node.kind == Kind::object;
            output += object ? '{' : '[';
            Frame frame{&node, 0, {}};
            if (object) {
                for (std::size_t i = 0; i < node.object.size(); ++i) frame.order.push_back(i);
                if (sort_keys) std::sort(frame.order.begin(), frame.order.end(), [&](auto a, auto b) {
                    return node.object[a].first < node.object[b].first;
                });
            }
            stack.push_back(std::move(frame));
            break;
        }
        }
    };
    atom(value);
    while (!stack.empty()) {
        auto& frame = stack.back();
        const auto& node = *frame.container;
        const bool object = node.kind == Kind::object;
        const auto count = object ? node.object.size() : node.array.size();
        if (frame.next == count) {
            if (pretty && count) { output += '\n'; output.append((stack.size() - 1) * 2, ' '); }
            output += object ? '}' : ']';
            stack.pop_back();
            continue;
        }
        if (frame.next) output += spaced ? ", " : ",";
        if (pretty) { output += '\n'; output.append(stack.size() * 2, ' '); }
        const auto index = frame.next++;
        if (object) {
            const auto& member = node.object[frame.order[index]];
            quoted(output, member.first);
            output += pretty || spaced ? ": " : ":";
            atom(member.second);
        } else atom(node.array[index]);
    }
}

bool integer_less(const std::string& a, const std::string& b) {
    const bool negative_a = a.front() == '-', negative_b = b.front() == '-';
    if (negative_a != negative_b) return negative_a;
    if (a.size() != b.size()) return negative_a ? a.size() > b.size() : a.size() < b.size();
    return negative_a ? a > b : a < b;
}
} // namespace

void Value::swap(Value& other) noexcept {
    using std::swap;
    swap(kind, other.kind);
    swap(boolean, other.boolean);
    integer.swap(other.integer);
    swap(floating, other.floating);
    string.swap(other.string);
    array.swap(other.array);
    object.swap(other.object);
}
Value::Value(Value&& other) noexcept { swap(other); }
Value& Value::operator=(Value&& other) noexcept {
    Value moved(std::move(other));
    swap(moved);
    return *this;
}
Value::Value(const Value& other) : Value() {
    std::vector<std::pair<const Value*, Value*>> pending{{&other, this}};
    while (!pending.empty()) {
        const auto pair = pending.back();
        pending.pop_back();
        const auto& source = *pair.first;
        auto& destination = *pair.second;
        destination.kind = source.kind;
        destination.boolean = source.boolean;
        destination.integer = source.integer;
        destination.floating = source.floating;
        destination.string = source.string;
        destination.array.resize(source.array.size());
        destination.object.resize(source.object.size());
        for (std::size_t i = 0; i < source.array.size(); ++i)
            pending.emplace_back(&source.array[i], &destination.array[i]);
        for (std::size_t i = 0; i < source.object.size(); ++i) {
            destination.object[i].first = source.object[i].first;
            pending.emplace_back(&source.object[i].second, &destination.object[i].second);
        }
    }
}
Value& Value::operator=(const Value& other) {
    Value copy(other);
    swap(copy);
    return *this;
}
Value::~Value() noexcept {
    // Postorder walk through existing ownership. No auxiliary allocation can
    // fail during exception unwinding. Each pop destroys an already empty
    // child, so vector/pair destruction never recurses with document depth.
    auto* current = this;
    while (true) {
        Value* child = nullptr;
        if (!current->array.empty()) child = &current->array.back();
        else if (!current->object.empty()) child = &current->object.back().second;
        if (child) {
            child->cleanup_parent_ = current;
            current = child;
        } else {
            if (current == this) break;
            auto* parent = current->cleanup_parent_;
            if (!parent->array.empty()) parent->array.pop_back();
            else parent->object.pop_back();
            current = parent;
        }
    }
}

Value parse(std::string_view utf8, std::size_t max_depth) { return Parser(utf8, max_depth).run(); }
Value parse_last_wins(std::string_view utf8, std::size_t max_depth) { return Parser(utf8, max_depth, true).run(); }
std::string display(const Value& value, std::size_t max_depth) {
    std::string output;
    encode(output, value, true, false, max_depth);
    output += '\n';
    return output;
}
const Value& Value::at(std::u32string_view key) const {
    if (kind == Kind::object) for (const auto& entry : object) if (entry.first == key) return entry.second;
    throw Error("missing object member");
}
bool Value::contains(std::u32string_view key) const {
    if (kind == Kind::object) for (const auto& entry : object) if (entry.first == key) return true;
    return false;
}
std::string compact(const Value& value) {
    std::string output;
    encode(output, value, false, false);
    return output;
}
std::string dumps(const Value& value, bool sort_keys, std::size_t max_depth) {
    std::string output;
    encode(output, value, false, sort_keys, max_depth, true);
    return output;
}
std::string JournalEvidenceV1(const Value& decoded_journal) {
    std::string output;
    encode(output, decoded_journal, true);
    output += '\n';
    return output;
}
std::string ContentFingerprintV1(const Value& entries) {
    if (entries.kind != Kind::array) throw Error("fingerprint entries must be an array");
    auto sorted = entries;
    for (const auto& entry : sorted.array)
        if (entry.kind != Kind::array || entry.array.size() != 3 ||
            entry.array[0].kind != Kind::string || entry.array[1].kind != Kind::integer ||
            entry.array[2].kind != Kind::string) throw Error("fingerprint entry kinds differ");
    std::sort(sorted.array.begin(), sorted.array.end(), [](const Value& a, const Value& b) {
        if (a.array[0].string != b.array[0].string) return a.array[0].string < b.array[0].string;
        if (a.array[1].integer != b.array[1].integer) return integer_less(a.array[1].integer, b.array[1].integer);
        return a.array[2].string < b.array[2].string;
    });
    return compact(sorted);
}
} // namespace c2::frontend::compat
