// Authored binary helper: the same pipe/argv stress cases execute on Windows.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <thread>
#include <chrono>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <fcntl.h>
#include <io.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif
int main(int argc, char** argv) {
#ifdef _WIN32
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
#endif
    if (argc < 2) return 2;
    const std::string mode = argv[1];
    if (mode == "args") {
        for (int i = 2; i < argc; ++i) { std::fwrite(argv[i], 1, std::strlen(argv[i]), stdout); std::fputc('\0', stdout); }
    } else if (mode == "streams") {
        // Fill BOTH output pipes before reading input; writing stdin first in
        // the parent would deadlock. Then echo binary input, including NUL.
        const std::string block(65536, 'x');
        for (int i = 0; i < 32; ++i) {
            std::fwrite(block.data(), 1, block.size(), stdout);
            std::fwrite(block.data(), 1, block.size(), stderr);
        }
        char data[65536];
        for (std::size_t n; (n = std::fread(data, 1, sizeof data, stdin));)
            std::fwrite(data, 1, n, stdout);
        return 7;
    } else if (mode == "early-close") {
        std::fclose(stdin);
        std::fputs("closed", stdout);
        return 9;
    } else if (mode == "hold") {
        if (argc == 3) {
            FILE* marker = std::fopen(argv[2], "wb");
            if (!marker) return 3;
#ifdef _WIN32
            std::fprintf(marker, "%lu", static_cast<unsigned long>(::GetCurrentProcessId()));
#else
            std::fprintf(marker, "%ld", static_cast<long>(::getpid()));
#endif
            std::fclose(marker);
        }
        std::this_thread::sleep_for(std::chrono::seconds(60));
    } else if (mode == "overflow") {
        const std::string block(65536, 'x');
        for (;;) { std::fwrite(block.data(), 1, block.size(), stdout); std::fwrite(block.data(), 1, block.size(), stderr); }
    } else if (mode == "inherited") {
        if (argc != 3) return 2;
#ifdef _WIN32
        DWORD flags;
        const auto h = reinterpret_cast<HANDLE>(static_cast<std::uintptr_t>(std::strtoull(argv[2], nullptr, 10)));
        return ::GetHandleInformation(h, &flags) ? 10 : 0;
#else
        return ::fcntl(std::atoi(argv[2]), F_GETFD) < 0 ? 0 : 10;
#endif
    } else if (mode == "descendant") {
        // A descendant deliberately retains stdout/stderr after its parent
        // exits. The owner's deadline must not turn into a blocking join.
#ifdef _WIN32
        std::string line = "\"" + std::string(argv[0]) + "\" hold \"" + argv[2] + "\"";
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
        if (child == 0) { ::execl(argv[0], argv[0], "hold", argv[2], nullptr); ::_exit(4); }
#endif
    } else return 2;
    return 0;
}
