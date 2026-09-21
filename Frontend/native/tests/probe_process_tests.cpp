// Test-only driver for the private probe runner; authored helpers only.
#include "probe_process.hpp"
#include "capture.hpp"
#include <filesystem>
#include <iostream>
#include <iterator>
#include <string>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
using namespace c2::frontend;
namespace fs = std::filesystem;
namespace {
std::string hex(const std::string& bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    for (const unsigned char c : bytes) { out.push_back(digits[c >> 4]); out.push_back(digits[c & 15]); }
    return out;
}
std::optional<fs::path> optional_path(const std::string& text) {
    if (text == "-") return std::nullopt;
    return fs::u8path(text);
}
}
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc < 2) return 2;
    const std::string command = argv[1];
    try {
        if (command == "inspect" && argc == 6) {
            const std::string content((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
            const auto value = probe_process::codec_inspect(content, argv[2], optional_path(argv[4]), argv[3],
                                                            std::chrono::milliseconds(std::stol(argv[5])));
            std::cout << "ok " << compat::compact(value) << '\n';
        } else if (command == "run" && argc >= 5) {
            // run <timeout_ms> <limit> <executable> [arguments...]; stdin is the input.
            const std::string content((std::istreambuf_iterator<char>(std::cin)), std::istreambuf_iterator<char>());
            std::vector<std::string> arguments(argv + 5, argv + argc);
            const auto result = probe_process::run(fs::u8path(argv[4]), arguments, content,
                std::chrono::milliseconds(std::stol(argv[2])), static_cast<std::size_t>(std::stoull(argv[3])));
            std::cout << "ok " << result.returncode << ' ' << hex(result.out) << ' ' << hex(result.err) << '\n';
        } else if (command == "inspect-set" && argc == 6) {
            // inspect-set <root> <key> <dialect> <probe|->
            const auto inventory = inventory_profiles(fs::u8path(argv[2]));
            const std::string key = argv[3];
            for (const auto& state : inventory.states())
                if (state.key() == std::u32string(key.begin(), key.end())) {
                    const auto value = probe_process::inspect_set(state, optional_path(argv[5]), argv[4]);
                    std::cout << "ok " << compat::compact(value) << '\n';
                    return 0;
                }
            std::cout << "missing\n";
        } else if (command == "inspect-bytes" && argc == 5) {
            const auto captured = capture(fs::u8path(argv[2]));
            const auto value = probe_process::inspect_bytes(captured.blobs, std::stoi(argv[3]), fs::u8path(argv[4]));
            std::cout << "ok " << compat::compact(value) << '\n';
        } else if (command == "evidence" && argc == 3) {
            const auto value = probe_process::codec_evidence(optional_path(argv[2]));
            std::cout << "ok " << compat::compact(value) << '\n';
        } else return 2;
    } catch (const probe_process::Timeout& e) { std::cout << "timeout " << e.what() << '\n';
    } catch (const probe_process::ProbeError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const probe_process::OutputLimit& e) { std::cout << "limit " << e.what() << '\n';
    } catch (const fs::filesystem_error& e) { std::cout << "oserror " << e.code().value() << '\n';
    } catch (const std::invalid_argument& e) { std::cout << "invalid " << e.what() << '\n';
    } catch (const compat::Error& e) { std::cout << "json " << e.what() << '\n'; }
    return 0;
}
