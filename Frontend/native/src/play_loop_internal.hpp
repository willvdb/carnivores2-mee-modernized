#pragma once
#include "c2/frontend/play_loop.hpp"
#include "session_policy.hpp"
namespace c2::frontend::play_loop {
struct Client::Impl {
    Store store;
    std::optional<std::filesystem::path> probe;
    bool writes;
    session_policy::Policies policies;
    Impl(Store s, std::optional<std::filesystem::path> p, bool w)
        : store(std::move(s)), probe(std::move(p)), writes(w) {}
};
// Private test seam. Production Client always uses the production policies.
struct Access {
    static void policies(Client& c, session_policy::Policies p) { c.impl_->policies = std::move(p); }
};
}
