#include "c2/frontend/play_loop.hpp"
#include <iostream>
#include <stdexcept>
using namespace c2::frontend;
int main() {
    play_loop::Client client(Store("missing-read-only-play-loop-test"), std::nullopt);
    play_loop::Preview preview;
    try { client.accept(preview); throw std::runtime_error("read-only accept succeeded"); }
    catch (const StoreError& e) {
        if (std::string(e.what()).find("read-only") == std::string::npos) return 1;
    }
    try { client.upgrade(); return 2; } catch (const StoreError&) {}
    try { client.recover(U"bad-id"); return 3; } catch (const StoreError&) {}
    try { client.recover_acceptance(U"bad-id"); return 4; } catch (const StoreError&) {}
    try { client.reconcile(U"bad-id"); return 5; } catch (const StoreError&) {}
    std::atomic<bool> cancel{true};
    try { client.run(U"bad-id", {}, cancel); return 6; } catch (const StoreError&) {}
    try { client.prepare(U"bad-id", planning::Selection::hunt(U"areas:0",{U"licenses:0"},{U"weapons:0"},{"1"}), {}); return 7; }
    catch (const StoreError&) {}
    std::cout << "public play-loop read-only boundary passed\n";
}
