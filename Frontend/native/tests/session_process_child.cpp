// Authored test child for the owned session supervisor: deterministic exit
// codes, signals, hangs, floods, binary bytes and descendants. Never a game.
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <thread>
#include <csignal>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <unistd.h>
#endif
namespace {
void marker(const char* path) {
    if (!path) return;
    FILE* file = std::fopen(path, "wb");
    if (!file) std::exit(3);
#ifdef _WIN32
    std::fprintf(file, "%lu", static_cast<unsigned long>(::GetCurrentProcessId()));
#else
    std::fprintf(file, "%ld", static_cast<long>(::getpid()));
#endif
    std::fclose(file);
}
void sleep_long() { std::this_thread::sleep_for(std::chrono::seconds(60)); }
// Stream byte i of stream s is (i * 7 + s) & 255: the harness recomputes the
// retained prefix without storing megabytes of expectations.
void flood(unsigned long long out_bytes, unsigned long long err_bytes) {
    char out[4096], err[4096];
    unsigned long long o = 0, e = 0;
    while (o < out_bytes || e < err_bytes) {
        std::size_t n = 0;
        for (; n < sizeof out && o < out_bytes; ++n, ++o) out[n] = static_cast<char>((o * 7) & 255);
        if (n) std::fwrite(out, 1, n, stdout);
        n = 0;
        for (; n < sizeof err && e < err_bytes; ++n, ++e) err[n] = static_cast<char>((e * 7 + 1) & 255);
        if (n) std::fwrite(err, 1, n, stderr);
    }
    std::fflush(stdout); std::fflush(stderr);
}
}
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    if (argc < 2) return 2;
    const std::string mode = argv[1];
    if (mode == "exit") return argc > 2 ? std::atoi(argv[2]) : 0;
    if (mode == "signal") {
#ifdef _WIN32
        return 2;
#else
        std::fputs("before signal", stdout);
        ::raise(argc > 2 ? std::atoi(argv[2]) : SIGTERM);
        return 2;
#endif
    }
    if (mode == "hang") { std::fputs("hanging", stdout); marker(argc > 2 ? argv[2] : nullptr); sleep_long(); return 0; }
    if (mode == "ignore-term") {
#ifndef _WIN32
        ::signal(SIGTERM, SIG_IGN);
#endif
        marker(argc > 2 ? argv[2] : nullptr);
        sleep_long();
        return 0;
    }
    if (mode == "flood") {
        if (argc != 4) return 2;
        flood(std::strtoull(argv[2], nullptr, 10), std::strtoull(argv[3], nullptr, 10));
        return 0;
    }
    if (mode == "flood-hang") {
        // Fill both pipes far past their capacity, then wait: only a
        // continuously draining owner reaches the timeout with the bytes counted.
        if (argc != 3) return 2;
        flood(std::strtoull(argv[2], nullptr, 10), std::strtoull(argv[2], nullptr, 10));
        sleep_long();
        return 0;
    }
    if (mode == "binary") {
        char bytes[256];
        for (int i = 0; i < 256; ++i) bytes[i] = static_cast<char>(i);
        for (int i = 0; i < 4; ++i) std::fwrite(bytes, 1, sizeof bytes, stdout);
        std::fwrite("err\0bin\0", 1, 8, stderr);
        return 0;
    }
    if (mode == "args") {
        for (int i = 0; i < argc; ++i) { std::fwrite(argv[i], 1, std::strlen(argv[i]), stdout); std::fputc('\0', stdout); }
        return 0;
    }
    if (mode == "cwd") {
        const std::string here = std::filesystem::current_path().string();
        std::fwrite(here.data(), 1, here.size(), stdout);
        return 0;
    }
    if (mode == "stdin") {
        char byte;
        const std::size_t got = std::fread(&byte, 1, 1, stdin);
        std::fputs(got == 0 && std::feof(stdin) ? "eof" : got == 0 ? "error" : "data", stdout);
        return 0;
    }
    if (mode == "close-hang") {
        std::fclose(stdout); std::fclose(stderr);
        marker(argc > 2 ? argv[2] : nullptr);
        sleep_long();
        return 0;
    }
    if (mode == "descendant") {
        // A descendant keeps stdout/stderr after this child exits; the owner's
        // log finish must stay bounded. The descendant writes its PID first.
        if (argc != 3) return 2;
        std::fputs("parent", stdout);
#ifdef _WIN32
        std::string line = "\"" + std::string(argv[0]) + "\" hang \"" + argv[2] + "\"";
        STARTUPINFOA startup{}; startup.cb = sizeof startup;
        startup.dwFlags = STARTF_USESTDHANDLES;
        startup.hStdInput = ::GetStdHandle(STD_INPUT_HANDLE);
        startup.hStdOutput = ::GetStdHandle(STD_OUTPUT_HANDLE);
        startup.hStdError = ::GetStdHandle(STD_ERROR_HANDLE);
        PROCESS_INFORMATION info{};
        if (!::CreateProcessA(argv[0], line.data(), nullptr, nullptr, TRUE, 0, nullptr, nullptr, &startup, &info)) return 3;
        ::CloseHandle(info.hProcess); ::CloseHandle(info.hThread);
#else
        const pid_t child = ::fork();
        if (child < 0) return 3;
        if (child == 0) { ::execl(argv[0], argv[0], "hang", argv[2], nullptr); ::_exit(4); }
#endif
        // Wait for the descendant's marker so the harness can always clean it up.
        for (int i = 0; i < 500 && !std::filesystem::exists(argv[2]); ++i)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        return 0;
    }
    return 2;
}
