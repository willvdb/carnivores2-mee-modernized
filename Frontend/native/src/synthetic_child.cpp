// Workstream A: compiled controlled synthetic child (replaces lodge/synthetic_child.py
// for native synthetic sessions). Trusted process fixture: only mutates state in
// its disposable explicit cwd. No engine, content loader, subprocess, native save
// discovery or profile creation. Standalone: it links no frontend library.
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <thread>
#include <vector>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <process.h>
#else
#include <signal.h>
#include <unistd.h>
#endif
namespace fs = std::filesystem;
namespace {
int usage(const char* message) {
    std::fprintf(stderr, "usage: c2-frontend-synthetic-child --scenario SCENARIO --slot {0..7} --literal LITERAL\n%s\n", message);
    return 2;
}
// json.dumps default ensure_ascii escaping of the reference's str values. On
// POSIX, argv/cwd bytes decode as UTF-8 with surrogateescape so undecodable
// bytes appear as \udcXX like the reference; NT arguments are native UTF-16.
void escape_unit(std::string& out, unsigned unit) {
    static constexpr char digits[] = "0123456789abcdef";
    out += "\\u";
    for (int shift = 12; shift >= 0; shift -= 4) out.push_back(digits[(unit >> shift) & 15]);
}
void escape_point(std::string& out, char32_t c) {
    if (c == U'"') out += "\\\"";
    else if (c == U'\\') out += "\\\\";
    else if (c == U'\n') out += "\\n";
    else if (c == U'\r') out += "\\r";
    else if (c == U'\t') out += "\\t";
    else if (c == U'\b') out += "\\b";
    else if (c == U'\f') out += "\\f";
    else if (c < 0x20 || c > 0x7e) {
        if (c > 0xffff) { c -= 0x10000; escape_unit(out, 0xd800 + (c >> 10)); escape_unit(out, 0xdc00 + (c & 1023)); }
        else escape_unit(out, static_cast<unsigned>(c));
    } else out.push_back(static_cast<char>(c));
}
#ifdef _WIN32
std::string quoted(const std::wstring& s) {
    std::string out = "\"";
    for (std::size_t i = 0; i < s.size(); ++i) {
        char32_t c = static_cast<char32_t>(s[i]);
        if (c >= 0xd800 && c <= 0xdbff && i + 1 < s.size() && s[i + 1] >= 0xdc00 && s[i + 1] <= 0xdfff)
            c = 0x10000 + ((c - 0xd800) << 10) + (static_cast<char32_t>(s[++i]) - 0xdc00);
        escape_point(out, c);
    }
    return out + "\"";
}
using Argument = std::wstring;
std::string narrow(const std::wstring& s) { return std::string(s.begin(), s.end()); }
#else
std::string quoted(const std::string& s) {
    std::string out = "\"";
    for (std::size_t i = 0; i < s.size();) {
        const auto c = static_cast<unsigned char>(s[i]);
        if (c < 0x80) { escape_point(out, c); ++i; continue; }
        const unsigned n = c >= 0xc2 && c <= 0xdf ? 2 : c >= 0xe0 && c <= 0xef ? 3 : c >= 0xf0 && c <= 0xf4 ? 4 : 0;
        char32_t point = n ? c & ((1u << (7 - n)) - 1) : 0;
        bool valid = n && i + n <= s.size();
        for (unsigned j = 1; valid && j < n; ++j) {
            const auto tail = static_cast<unsigned char>(s[i + j]);
            valid = (tail & 0xc0) == 0x80;
            point = (point << 6) | (tail & 63);
        }
        valid = valid && (n != 3 || point >= 0x800) && (n != 4 || point >= 0x10000) && point <= 0x10ffff && !(point >= 0xd800 && point <= 0xdfff);
        if (valid) { escape_point(out, point); i += n; }
        else { escape_point(out, 0xdc00 + c); ++i; }
    }
    return out + "\"";
}
using Argument = std::string;
const std::string& narrow(const std::string& s) { return s; }
#endif
std::string read_all(const fs::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) throw std::runtime_error("cannot read " + path.string());
    return std::string(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
}
void write_all(const fs::path& path, const std::string& bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream || !stream.write(bytes.data(), static_cast<std::streamsize>(bytes.size())) || !stream.flush())
        throw std::runtime_error("cannot write " + path.string());
}
// struct.pack_into('<i', buffer, offset, value)
void pack_int32(std::string& bytes, std::size_t offset, std::int32_t value) {
    if (bytes.size() < offset + 4) throw std::runtime_error("pack_into requires a buffer of at least " + std::to_string(offset + 4) + " bytes");
    const auto raw = static_cast<std::uint32_t>(value);
    for (int i = 0; i < 4; ++i) bytes[offset + static_cast<std::size_t>(i)] = static_cast<char>((raw >> (8 * i)) & 0xff);
}
int run(const std::vector<Argument>& args) {
    Argument scenario, literal;
    int slot = -1;
    bool have_scenario = false, have_slot = false, have_literal = false;
    for (std::size_t i = 1; i < args.size(); ++i) {
        const std::string key = narrow(args[i]);
        if (i + 1 >= args.size()) return usage(("argument " + key + ": expected one argument").c_str());
        const Argument& value = args[++i];
        if (key == "--scenario") { scenario = value; have_scenario = true; }
        else if (key == "--literal") { literal = value; have_literal = true; }
        else if (key == "--slot") {
            const std::string digits = narrow(value);
            if (digits.size() != 1 || digits[0] < '0' || digits[0] > '7') return usage("argument --slot: invalid choice (choose from 0..7)");
            slot = digits[0] - '0';
            have_slot = true;
        } else return usage(("unrecognized arguments: " + key).c_str());
    }
    if (!have_scenario || !have_slot || !have_literal) return usage("the following arguments are required: --scenario, --slot, --literal");
    std::string receipt = "{\"cwd\": " + quoted(fs::current_path().native()) + ", \"argv\": [";
    for (std::size_t i = 1; i < args.size(); ++i) receipt += (i > 1 ? ", " : "") + quoted(args[i]);
#ifdef _WIN32
    receipt += "], \"pid\": " + std::to_string(::GetCurrentProcessId()) + "}\n";
#else
    receipt += "], \"pid\": " + std::to_string(::getpid()) + "}\n";
#endif
    std::fputs(receipt.c_str(), stdout);
    std::fflush(stdout);
    std::fputs("synthetic fixture stderr\n", stderr);
    std::fflush(stderr);
    const std::string name = "trophy0" + std::to_string(slot);
    const fs::path sav = fs::path("state") / (name + ".sav"), sab = fs::path("state") / (name + ".sab");
    const std::string s = narrow(scenario);
    if (s == "sav" || s == "pair" || s == "changed-nonzero" || s == "missing-sab") {
        std::string value = read_all(sav);
        pack_int32(value, 132, 175);
        write_all(sav, value);
    }
    if (s == "pair") {
        std::string value = read_all(sab);
        if (value.empty()) throw std::runtime_error("empty room");
        value.back() = static_cast<char>(static_cast<unsigned char>(value.back()) ^ 1);
        write_all(sab, value);
    }
    if (s == "corrupt-sav") write_all(sav, read_all(sav).substr(0, 20));
    if (s == "corrupt-sab") write_all(sab, read_all(sab).substr(0, 20));
    if (s == "missing-sab" && !fs::remove(sab)) throw std::runtime_error("cannot unlink " + sab.string());
    if (s == "deleted-sav" && !fs::remove(sav)) throw std::runtime_error("cannot unlink " + sav.string());
    if (s == "extra") write_all(fs::path("state") / (name + ".extra"), "unknown companion, preserve me");
    if (s == "registration") {
        std::string value = read_all(sav);
        pack_int32(value, 128, (slot + 1) % 8);
        write_all(sav, value);
    }
    if (s == "hang") std::this_thread::sleep_for(std::chrono::seconds(60));
    if (s == "terminated") {
#ifdef _WIN32
        ::TerminateProcess(::GetCurrentProcess(), 15);
#else
        ::kill(::getpid(), SIGTERM);
#endif
    }
    if (s == "logs") {
        const std::string x(200000, 'x'), y(200000, 'y');
        std::fwrite(x.data(), 1, x.size(), stdout);
        std::fwrite(y.data(), 1, y.size(), stderr);
    }
    return s == "nonzero" || s == "changed-nonzero" ? 7 : 0;
}
} // namespace
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    try { return run(std::vector<std::wstring>(argv, argv + argc)); }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
#else
int main(int argc, char** argv) {
    try { return run(std::vector<std::string>(argv, argv + argc)); }
    catch (const std::exception& e) { std::fprintf(stderr, "%s\n", e.what()); return 1; }
}
#endif
