// Production native frontend entry point. Installs no policy double.
#include "cli.hpp"
#include <csignal>
#include <iostream>
#include <string>
#include <vector>
#ifdef _WIN32
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#endif
namespace {
#ifdef _WIN32
BOOL WINAPI console_control(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
    c2::frontend::cli::interrupt_flag().store(true);
    return TRUE;
}
#else
extern "C" void interrupt(int) { c2::frontend::cli::interrupt_flag().store(true); }
#endif
// Ctrl-C never abandons a held writer lock or an owned child: a running
// session is cancelled through its owned-child path; other commands finish
// their bounded operation.
void install_interrupt_handler() {
#ifdef _WIN32
    SetConsoleCtrlHandler(console_control, TRUE);
#else
    struct sigaction action {};
    action.sa_handler = interrupt;
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
#endif
}
}
#ifdef _WIN32
int wmain(int argc, wchar_t** argv) {
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    install_interrupt_handler();
    std::vector<std::filesystem::path> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return c2::frontend::cli::run(args, std::cout, std::cerr);
}
#else
int main(int argc, char** argv) {
    install_interrupt_handler();
    std::vector<std::filesystem::path> args;
    for (int i = 1; i < argc; ++i) args.emplace_back(argv[i]);
    return c2::frontend::cli::run(args, std::cout, std::cerr);
}
#endif
