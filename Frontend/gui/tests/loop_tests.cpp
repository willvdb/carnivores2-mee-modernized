#include "c2/frontend/gui/loop.hpp"
#include "check.hpp"
using namespace c2::frontend;
using namespace c2::frontend::gui;
int main() {
    HuntLoop loop(true);
    CHECK(!loop.can(Operation::accept));
    loop.decline(); CHECK(loop.state()==LoopState::selection);
    CHECK(loop.select({U"association",U"hunter",U"instance",U"personal",U"managed",U"managed-state-history",U"g0"}));
    auto plan = *loop.begin(Operation::plan);
    CHECK(!loop.complete(plan.id+1, {play_loop::Plan{}, {}}));
    CHECK(loop.complete(plan.id, {play_loop::Plan{}, {}}));
    CHECK(!loop.complete(plan.id, {play_loop::Plan{}, {}}));
    auto prepare = *loop.begin(Operation::prepare);
    CHECK(!loop.complete(plan.id,{play_loop::Plan{},{}}));
    CHECK(loop.state()==LoopState::preparing);
    play_loop::Session session; session.id=U"session"; session.generation=U"g0"; session.state=U"prepared";
    CHECK(loop.complete(prepare.id,{session,{}}));
    auto run = *loop.begin(Operation::run);
    loop.cancel(); CHECK(loop.cancellation()->load());
    CHECK(!loop.select({})); CHECK(!loop.can(Operation::accept));
    session.state=U"quarantined";
    CHECK(loop.complete(run.id,{session,{}}));
    auto inspect = *loop.begin(Operation::inspect); CHECK(loop.complete(inspect.id,{session,{}}));
    auto preview = *loop.begin(Operation::preview);
    play_loop::Preview p; p.session_id=U"session"; p.expected_generation=U"g0";
    CHECK(loop.complete(preview.id,{p,{}})); CHECK(loop.state()==LoopState::blocked);
    preview = *loop.begin(Operation::preview); p.allowed=true;
    CHECK(loop.complete(preview.id,{p,{}})); CHECK(!loop.can(Operation::accept));
    preview = *loop.begin(Operation::preview); p.candidate_sha256=U"digest";
    CHECK(loop.complete(preview.id,{p,{}})); CHECK(loop.can(Operation::accept));
    loop.decline(); CHECK(!loop.can(Operation::accept));
    preview = *loop.begin(Operation::preview); CHECK(loop.complete(preview.id,{p,{}}));
    auto accept = *loop.begin(Operation::accept); CHECK(accept.preview.candidate_sha256==U"digest");
    play_loop::Receipt receipt; receipt.result=U"accepted"; receipt.current_generation=U"g1";
    CHECK(loop.complete(accept.id,{receipt,{}})); CHECK(loop.state()==LoopState::accepted);
    CHECK(!loop.session()); CHECK(loop.association()->current_generation==U"g1");
    CHECK(loop.begin(Operation::prepare).has_value());
    CHECK(!loop.complete(accept.id,{receipt,{}}));
    CHECK(loop.state()==LoopState::preparing);
    HuntLoop changed(true); changed.select({});
    auto intent=*changed.begin(Operation::plan); changed.complete(intent.id,{play_loop::Plan{},{}});
    CHECK(changed.can(Operation::prepare));
    changed.loadout(planning::Selection::hunt(U"areas:1",{U"licenses:0"},{U"weapons:0"},{"2"}));
    CHECK(!changed.can(Operation::prepare));
    CHECK(changed.state()==LoopState::selection);
    HuntLoop ro; ro.select({}); CHECK(ro.begin(Operation::plan).has_value());
    CHECK(ro.complete(1,{play_loop::Plan{},{}})); CHECK(!ro.can(Operation::prepare));
    for (auto op : {Operation::recover,Operation::recover_acceptance,Operation::upgrade}) {
        HuntLoop recovery(true); recovery.inspect_session(U"session");
        auto r = *recovery.begin(op); CHECK(recovery.complete(r.id,{{},"blocked diagnostics"}));
        CHECK(recovery.state()==LoopState::error); CHECK(recovery.error()=="blocked diagnostics");
    }
    return test::failures ? 1 : 0;
}
