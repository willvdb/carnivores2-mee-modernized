// Test-only driver for the private journal component; authored journals only.
#include "session_journal.hpp"
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
std::u32string ascii(const std::string& s) { return {s.begin(), s.end()}; }
std::string input() { return {std::istreambuf_iterator<char>(std::cin), std::istreambuf_iterator<char>()}; }
struct Injected : std::runtime_error { using std::runtime_error::runtime_error; };
}
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    if (argc < 2) return 2;
    const std::string command = argv[1];
    try {
        if (command == "validate" && argc == 3) {
            session_journal::validate(compat::parse(input()), ascii(argv[2]));
            std::cout << "ok\n";
        } else if (command == "encode" && argc == 2) {
            const auto bytes = session_journal::encode(compat::parse(input()));
            std::cout << "ok\n" << bytes;
        } else if (command == "persist" && argc == 3) {
            session_journal::persist(fs::u8path(argv[2]), compat::parse(input()));
            std::cout << "ok\n";
        } else if (command == "read" && argc == 4) {
            const auto journal = session_journal::read(Store(fs::u8path(argv[2])), ascii(argv[3]));
            std::cout << "ok\n" << session_journal::encode(journal);
        } else if (command == "transition" && (argc == 5 || argc == 6)) {
            // transition <root> <state> <fields-json-object> [fail]; stdin is the journal.
            auto journal = compat::parse(input());
            const auto before = session_journal::encode(journal);
            const auto fields = compat::parse(argv[4]);
            store_write::FailureHook hook;
            if (argc == 6) hook = [](store_write::WritePhase phase, const fs::path&) {
                if (phase == store_write::WritePhase::replace) throw Injected("injected");
            };
            try { session_journal::transition(fs::u8path(argv[2]), journal, argv[3], fields.object, hook); }
            catch (const Injected&) {
                std::cout << (session_journal::encode(journal) == before ? "injected-unchanged\n" : "injected-mutated\n");
                return 0;
            }
            std::cout << "ok\n" << session_journal::encode(journal);
        } else return 2;
    } catch (const StoreError& e) { std::cout << "frontend " << e.what() << '\n';
    } catch (const std::invalid_argument& e) { std::cout << "invalid " << e.what() << '\n';
    } catch (const compat::Error& e) { std::cout << "json " << e.what() << '\n'; }
    return 0;
}
