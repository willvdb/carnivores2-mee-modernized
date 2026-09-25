#pragma once
// Interrupt handling shared by the production and test-only entry points.
#include "cli.hpp"
#include <csignal>
#ifdef _WIN32
#include <windows.h>
#endif
namespace c2::frontend::cli_main {
#ifdef _WIN32
inline BOOL WINAPI console_control(DWORD event) {
    if (event != CTRL_C_EVENT && event != CTRL_BREAK_EVENT) return FALSE;
    c2::frontend::cli::interrupt_flag().store(true);
    return TRUE;
}
#else
inline void interrupt(int) { c2::frontend::cli::interrupt_flag().store(true); }
#endif
// Ctrl-C never abandons a held writer lock or an owned child: a running
// session is cancelled through its owned-child path; other commands finish
// their bounded operation.
inline void install_interrupt_handler() {
#ifdef _WIN32
    SetConsoleCtrlHandler(console_control, TRUE);
#else
    struct sigaction action {};
    action.sa_handler = interrupt;
    action.sa_flags = SA_RESTART; // supervision loops poll the flag; I/O never sees EINTR
    sigemptyset(&action.sa_mask);
    sigaction(SIGINT, &action, nullptr);
#endif
}
}
