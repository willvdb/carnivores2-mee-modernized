#include "c2/frontend/core.hpp"
#include <iostream>
#include <string_view>

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--version") {
        std::cout << "c2-frontend-native " << c2::frontend::version() << '\n';
        return 0;
    }
    if (argc == 1 || (argc == 2 && std::string_view(argv[1]) == "--help")) {
        std::cout << "Usage: c2-frontend-native [--help | --version]\n"
                     "Native frontend foundation. Backend operations are not implemented.\n";
        return 0;
    }
    std::cerr << "Unsupported argument. Use --help.\n";
    return 2;
}
