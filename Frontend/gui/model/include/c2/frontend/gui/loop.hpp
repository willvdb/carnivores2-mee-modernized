#pragma once
#include "c2/frontend/play_loop.hpp"
#include <variant>
namespace c2::frontend::gui {
enum class Operation { catalog, plan, prepare, run, inspect, preview, accept, recover, recover_acceptance, upgrade };
enum class LoopState { selection, validating, validated, preparing, prepared, running, returned,
                       inspecting, reviewing, previewing, eligible, blocked, accepting, accepted,
                       recovering, declined, error };
using LoopValue = std::variant<std::monostate, play_loop::Plan, play_loop::Session, play_loop::Preview,
                               play_loop::Receipt, play_loop::Evidence, catalog::Projection>;
struct LoopResult { LoopValue value; std::string error; };
struct LoopRequest {
    std::uint64_t id = 0;
    Operation operation = Operation::plan;
    std::u32string association, session, generation;
    planning::Selection selection = planning::Selection::hunt(U"areas:0",{U"licenses:0"},{U"weapons:0"},{"1"});
    play_loop::Preview preview;
};
// A UI-owned state machine. Async completions never contain document pointers.
class HuntLoop {
public:
    explicit HuntLoop(bool writable = false) : writable_(writable) {}
    bool select(play_loop::Association);
    void observe_associations(const std::vector<play_loop::Association>&);
    bool loadout(planning::Selection);
    bool inspect_session(std::u32string id); // explicit UUID, including historical/blocked sessions
    std::optional<LoopRequest> begin(Operation);
    bool complete(std::uint64_t id, LoopResult);
    void cancel();
    void decline();
    bool can(Operation) const;
    const std::optional<catalog::Projection>& catalog() const { return catalog_; }
    bool busy() const { return pending_.has_value(); }
    LoopState state() const { return state_; }
    const std::optional<play_loop::Association>& association() const { return association_; }
    const std::optional<play_loop::Session>& session() const { return session_; }
    const std::optional<play_loop::Preview>& preview() const { return preview_; }
    const std::string& details() const { return details_; }
    const std::string& error() const { return error_; }
    std::shared_ptr<std::atomic<bool>> cancellation() const { return cancel_; }
    std::uint64_t version() const { return version_; }
private:
    std::optional<catalog::Projection> catalog_;
    bool writable_;
    LoopState state_ = LoopState::selection;
    std::uint64_t next_ = 1, version_ = 0;
    std::optional<LoopRequest> pending_;
    std::optional<play_loop::Association> association_;
    std::optional<play_loop::Session> session_;
    std::optional<play_loop::Preview> preview_;
    planning::Selection selection_ = planning::Selection::hunt(U"areas:0",{U"licenses:0"},{U"weapons:0"},{"1"});
    std::string details_, error_;
    std::shared_ptr<std::atomic<bool>> cancel_ = std::make_shared<std::atomic<bool>>(false);
};
LoopResult execute_loop(const play_loop::Client&, const LoopRequest&, const play_loop::Authorization&,
                        const std::atomic<bool>& cancel);
}
