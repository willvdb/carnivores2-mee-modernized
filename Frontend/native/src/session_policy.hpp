#pragma once
// PRIVATE shared seam (runtime completion): the edition policy evaluators used
// by every native-session operation. Default-constructed (empty) members mean
// the production pinned policies (planning_internal::observer_policy /
// hunt_policy). Non-empty evaluators exist only for asset-free tests and the
// test-only fixture CLI; the production executable never installs one and has
// no option, environment variable or file that can.
#include "planning_store.hpp"
namespace c2::frontend::session_policy {
struct Policies {
    planning_store::PolicyEvaluator observer; // lodge.genesis.observer_policy
    planning_store::PolicyEvaluator hunt;     // lodge.genesis_hunt.hunt_policy
};
}
