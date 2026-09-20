#include "c2/frontend/core.hpp"
#include "json_compat.hpp"

#include <fstream>
#include <iostream>
#include <iterator>
#include <locale>
#include <string>
#ifdef _WIN32
#include <cstdio>
#include <fcntl.h>
#include <io.h>
#endif

namespace {
using namespace c2::frontend;
using namespace c2::frontend::compat;

void require(bool condition, const std::string& description) {
    if (!condition) throw std::runtime_error(description);
}
std::string ascii(const Value& value) {
    require(value.kind == Kind::string, "expected fixture string");
    std::string result;
    for (auto c : value.string) {
        require(c < 128, "fixture field is not ASCII");
        result.push_back(static_cast<char>(c));
    }
    return result;
}
std::string unhex(const Value& value) {
    const auto text = ascii(value);
    require(text.size() % 2 == 0, "odd fixture hex length");
    std::string result;
    auto digit = [](char c) {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        throw std::runtime_error("invalid fixture hex");
    };
    for (std::size_t i = 0; i < text.size(); i += 2)
        result.push_back(static_cast<char>(digit(text[i]) * 16 + digit(text[i + 1])));
    return result;
}
template<class Function> void rejects(Function function, const std::string& description) {
    bool rejected = false;
    try { function(); } catch (const Error&) { rejected = true; }
    require(rejected, description);
}
std::string kind(const Value& value) {
    switch (value.kind) {
    case Kind::null: return "NoneType";
    case Kind::boolean: return "bool";
    case Kind::integer: return "int";
    case Kind::floating: return "float";
    case Kind::string: return "str";
    case Kind::array: return "list";
    case Kind::object: return "dict";
    }
    throw std::runtime_error("unrecognized kind");
}
void golden(const Value& corpus) {
    for (const auto& entry : corpus.at(U"cases").array) {
        const auto name = ascii(entry.at(U"name"));
        const auto input = unhex(entry.at(U"input_hex"));
        if (entry.contains(U"parse_error")) {
            rejects([&] { parse(input); }, name + ": accepted rejected Python input");
            continue;
        }
        const auto value = parse(input);
        require(kind(value) == ascii(entry.at(U"kind")), name + ": kind differs");
        require(compact(value) == unhex(entry.at(U"compact_hex")), name + ": compact bytes differ");
        if (entry.contains(U"journal_error")) {
            rejects([&] { JournalEvidenceV1(value); }, name + ": encoded nonfinite journal");
        } else {
            const auto encoded = JournalEvidenceV1(value);
            require(encoded == unhex(entry.at(U"journal_hex")), name + ": journal bytes differ");
            require(sha256(encoded) == ascii(entry.at(U"journal_sha256")), name + ": journal hash differs");
            require(JournalEvidenceV1(parse(encoded)) == encoded, name + ": re-encoding differs");
        }
    }
    const auto& fingerprint = corpus.at(U"fingerprint");
    const auto payload = ContentFingerprintV1(fingerprint.at(U"entries"));
    require(payload == unhex(fingerprint.at(U"payload_hex")), "fingerprint bytes differ");
    require(sha256(payload) == ascii(fingerprint.at(U"sha256")), "fingerprint digest differs");
}
void hash_answers() {
    require(sha256("") == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA empty");
    require(sha256("abc") == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA abc");
    require(sha256("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq") ==
            "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1", "SHA two-block padding");
    require(sha256(std::string(1000000, 'a')) ==
            "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA million-a/chunking");
    require(sha256(std::string("\x00\x80\xff", 3)) ==
            "5240672d7b51756b829ad0ef8d9468b7a078afa2f410484fd3892dab47becb72", "SHA binary signed bytes");
}
void strict_representation() {
    const auto numbers = parse("[true,false,1,1.0,-0,-0.0,18446744073709551616]");
    require(numbers.array[0].kind == Kind::boolean && numbers.array[1].kind == Kind::boolean,
            "bool must remain separate from int");
    require(numbers.array[2].kind == Kind::integer && numbers.array[3].kind == Kind::floating,
            "integer and float kinds collapsed");
    require(numbers.array[4].integer == "0" && compact(numbers.array[5]) == "-0.0", "zero kind/sign lost");
    require(numbers.array[6].integer == "18446744073709551616", "integer magnitude lost");
    for (const auto* input : {"[[\"a\",true,\"hash\"]]", "[[\"a\",1.0,\"hash\"]]", "{}", "[[1,2,3]]"})
        rejects([&] { ContentFingerprintV1(parse(input)); }, "fingerprint must require exact entry kinds");
    // Same-path tie breaks are Python list ordering, including arbitrary ints.
    require(ContentFingerprintV1(parse("[[\"x\",10,\"b\"],[\"x\",2,\"z\"],[\"x\",10,\"a\"]]")) ==
            "[[\"x\",2,\"z\"],[\"x\",10,\"a\"],[\"x\",10,\"b\"]]", "fingerprint tuple sort");
    for (const auto& bytes : {std::string("\xc0\x80", 2), std::string("\xed\xa0\x80", 3),
                             std::string("\xf4\x90\x80\x80", 4), std::string("\xe0\x80\x80", 3),
                             std::string("\x80", 1), std::string("\xf0\x9f", 2)})
        rejects([&] { parse("\"" + bytes + "\""); }, "invalid UTF-8 accepted");
    const std::string nested = std::string(256, '[') + "0" + std::string(256, ']');
    require(compact(parse(nested)) == nested, "nested arrays changed");
    rejects([&] { parse(std::string(1002, '[')); }, "nesting guard missing");
    require(JournalEvidenceV1(parse("{\"b\":2,\"a\":1}")) ==
            JournalEvidenceV1(parse(" { \"a\" : 1, \"b\" : 2 }\n")), "journal hashes disk spelling");
}
std::string nested_document(bool object, std::size_t depth) {
    std::string result;
    for (std::size_t i = 0; i < depth; ++i) result += object ? "{\"x\":" : "[";
    result += '0';
    result.append(depth, object ? '}' : ']');
    return result;
}
void deep_representation() {
    for (bool object : {false, true}) {
        std::cerr << "deep " << (object ? "objects" : "arrays") << ": accepted/copy/encode\n";
        const auto input = nested_document(object, 1000);
        auto value = parse(input);
        require(compact(value) == input, "near-guard compact bytes differ");
        const auto evidence = JournalEvidenceV1(value);
        require(compact(parse(evidence)) == input, "near-guard journal round trip differs");
        Value copy(value);
        Value assigned;
        assigned = copy;
        require(JournalEvidenceV1(assigned) == evidence, "deep copy changed evidence");
        value = std::move(assigned); // Also destroys the previous deep value.
        require(compact(value) == input, "deep move assignment changed evidence");
        assigned = copy;
        assigned = Value{}; // Explicitly exercise replacement destruction.

        std::cerr << "deep " << (object ? "objects" : "arrays") << ": rejection/unwind\n";
        rejects([&] { parse(nested_document(object, 1001)); }, "over-limit value accepted");
        auto malformed = input;
        malformed.resize(malformed.size() - 1);
        rejects([&] { parse(malformed); }, "incomplete near-guard tree accepted");
        std::string over_limit;
        for (int i = 0; i < 1002; ++i) over_limit += object ? "{\"x\":" : "[";
        rejects([&] { parse(over_limit); }, "malformed over-limit tree accepted");
        // Build beyond the parser guard to check encoder rejection and cleanup
        // independently. Copying is also safe for privately constructed trees.
        Value parent;
        parent.kind = object ? Kind::object : Kind::array;
        if (object) parent.object.emplace_back(U"x", std::move(value));
        else parent.array.push_back(std::move(value));
        Value beyond_guard(parent);
        rejects([&] { compact(beyond_guard); }, "compact encoder depth guard missing");
        rejects([&] { JournalEvidenceV1(beyond_guard); }, "journal encoder depth guard missing");
        rejects([&] { ContentFingerprintV1(beyond_guard); }, "deep invalid fingerprint accepted");
    }
    // Empty containers at depth 1000 are accepted just like the original guard.
    const auto empty = std::string(1001, '[') + std::string(1001, ']');
    require(compact(parse(empty)) == empty, "empty-container depth boundary changed");
}
struct CommaLocale : std::numpunct<char> {
    char do_decimal_point() const override { return ','; }
    char do_thousands_sep() const override { return '.'; }
    std::string do_grouping() const override { return "\3"; }
};
} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 2 && std::string(argv[1]) == "--deep-stack") {
            deep_representation();
            std::cout << "Deep representation checks passed\n";
            return 0;
        }
        // Test-only differential interface; production CLI has no JSON commands.
        if (argc == 2 && (std::string(argv[1]) == "--journal-stdin" || std::string(argv[1]) == "--compact-stdin")) {
#ifdef _WIN32
            require(_setmode(_fileno(stdin), _O_BINARY) != -1 &&
                    _setmode(_fileno(stdout), _O_BINARY) != -1, "cannot enable binary test byte I/O");
#endif
            const std::string input(std::istreambuf_iterator<char>(std::cin), {});
            const auto value = parse(input);
            std::cout << (std::string(argv[1]) == "--journal-stdin" ? JournalEvidenceV1(value) : compact(value));
            return 0;
        }
        require(argc == 2, "expected golden corpus path");
        std::ifstream stream(argv[1], std::ios::binary);
        require(bool(stream), "cannot open golden corpus");
        const std::string source(std::istreambuf_iterator<char>(stream), {});
        const auto corpus = parse(source);
        std::cerr << "SHA known answers\n";
        hash_answers();
        std::cerr << "Strict representation and original nesting guard\n";
        strict_representation();
        std::cerr << "Deep representation lifecycle\n";
        deep_representation();
        std::cerr << "Python golden corpus\n";
        golden(corpus);
        // Neither JSON decimal formatting nor SHA hexadecimal uses global locale.
        const auto previous = std::locale::global(std::locale(std::locale::classic(), new CommaLocale));
        std::cerr << "Locale independence\n";
        golden(corpus);
        hash_answers();
        std::locale::global(previous);
        std::cout << corpus.at(U"cases").array.size() << " Python golden cases passed; SHA/type/UTF-8 checks passed\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
