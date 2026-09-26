// Production native frontend entry point. Installs no policy double.
#include "cli.hpp"
#include "interrupt.hpp"
#include <csignal>
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    c2::frontend::cli_main::install_interrupt_handler();
    std::vector<std::filesystem::path> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return c2::frontend::cli::run(args, std::cout, std::cerr);
}
#else
int main(int argc, char** argv) {
    c2::frontend::cli_main::install_interrupt_handler();
    std::vector<std::filesystem::path> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return c2::frontend::cli::run(args, std::cout, std::cerr);
}
#endif
