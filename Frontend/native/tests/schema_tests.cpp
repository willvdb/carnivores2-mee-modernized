#include "manifest_schema.hpp"
#include "schema_compat.hpp"
#include <iostream>
#include <iterator>
#include <new>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#endif
int main(int argc, char **argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
#endif
    const std::string mode = argc > 1 ? argv[1] : "--manifest-stdin";
    if (mode == "--batch") {
        std::string line;
        while (std::getline(std::cin, line)) {
            try {
                auto v = c2::frontend::schema::decode_manifest(line);
                std::cout << "valid\t" << c2::frontend::compat::compact(v) << '\n';
            } catch (const std::bad_alloc &) {
                std::cout << "resource\n";
            } catch (const c2::frontend::compat::Error &e) {
                std::cout << (std::string(e.what()).find("resource limit") != std::string::npos
                                  ? "resource"
                                  : "invalid")
                          << '\n';
            } catch (const std::exception &) {
                std::cout << "diagnostic\n";
            }
        }
        return 0;
    }
    try {
        std::string input{std::istreambuf_iterator<char>(std::cin), {}};
        if (mode == "--manifest-stdin") {
            auto v = c2::frontend::schema::decode_manifest(input);
            std::cout << "valid\n" << c2::frontend::compat::compact(v);
        } else if (mode == "--equal-stdin") {
            auto v = c2::frontend::compat::parse(input);
            std::cout << (c2::frontend::schema::equal(v.array.at(0), v.array.at(1)) ? "true"
                                                                                    : "false");
        } else if (mode == "--lower-stdin" || mode == "--fold-stdin") {
            auto v = c2::frontend::compat::parse(input);
            v.string = mode == "--lower-stdin" ? c2::frontend::schema::lower(v.string)
                                               : c2::frontend::schema::casefold(v.string);
            std::cout << c2::frontend::compat::compact(v);
        } else {
            std::cerr << "unknown test mode";
            return 2;
        }
    } catch (const std::bad_alloc &) {
        std::cout << "resource\nallocation exhausted";
        return 3;
    } catch (const c2::frontend::compat::Error &e) {
        std::string msg = e.what();
        bool resource = msg.find("resource limit") != std::string::npos;
        std::cout << (resource ? "resource\n" : "invalid\n") << msg;
        return resource ? 3 : 1;
    } catch (const std::exception &e) {
        std::cout << "diagnostic\n" << e.what();
        return 2;
    }
}
