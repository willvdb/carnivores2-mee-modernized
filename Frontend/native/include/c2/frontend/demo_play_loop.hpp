#pragma once
// Asset-free demo support is a separate library, never linked into the CLI.
#include "c2/frontend/play_loop.hpp"
namespace c2::frontend::play_loop {
struct Demo {
    std::filesystem::path owned_root, directory;
    Client client;
    Authorization authorization;
};
// Always creates a uniquely owned directory. Cannot open or grant fixture
// policy to a supplied store. Caller removes directory only after joining work.
Demo create_demo(const std::filesystem::path& probe, const std::filesystem::path& fixture);
}
