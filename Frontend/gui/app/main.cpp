#include "app.hpp"
#include "self_test.hpp"
#include <SDL3/SDL_main.h>
#include <cstring>
#include <iostream>

namespace {
void usage() {
    std::cout << "c2-frontend-gui [--store DIR] [--assets DIR] [--size WxH] [--self-test] [--help]\n"
                 "  --store DIR   open an existing lodge store read-only (never created, upgraded or written)\n"
                 "  --assets DIR  asset root override (else C2_FRONTEND_GUI_ASSETS, then the install layout)\n"
                 "  --size WxH    initial window size in window coordinates (default 1280x720)\n"
                 "  --self-test   drive the real RmlUi screens headlessly and exit non-zero on failure\n"
                 "  --capture DIR with --self-test: write a PNG of each stage into DIR\n"
                 "Without --store an authored, disposable demo store is created in the temp directory\n"
                 "and removed on exit. No game assets are needed or read.\n";
}
} // namespace

int main(int argc, char** argv) {
    using namespace c2::frontend::gui::app;
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        auto value = [&](const char* name) -> const char* {
            if (i + 1 >= argc) { std::cerr << name << " requires a value\n"; std::exit(2); }
            return argv[++i];
        };
        if (arg == "--help" || arg == "-h") { usage(); return 0; }
        if (arg == "--store") { options.store = std::filesystem::u8path(value("--store")); continue; }
        if (arg == "--assets") { options.assets = std::filesystem::u8path(value("--assets")); continue; }
        if (arg == "--self-test") { options.self_test = true; continue; }
        if (arg == "--capture") { options.capture_dir = std::filesystem::u8path(value("--capture")); continue; }
        if (arg == "--size") {
            int w = 0, h = 0;
            if (std::sscanf(value("--size"), "%dx%d", &w, &h) != 2 || w < 320 || h < 200) { std::cerr << "--size expects WxH\n"; return 2; }
            options.width = w; options.height = h; continue;
        }
        std::cerr << "unknown argument: " << arg << "\n";
        usage();
        return 2;
    }
    const bool self_test = options.self_test;
    App app(options);
    std::string error;
    if (!app.init(error)) {
        std::cerr << "c2-frontend-gui: " << error << "\n";
        if (!self_test)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Carnivores frontend", error.c_str(), nullptr);
        return 1;
    }
    if (self_test) return run_self_test(app);
    return app.run();
}
