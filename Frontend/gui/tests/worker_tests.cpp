// Worker + model: a deterministic slow/failing source proves that the UI
// side keeps moving while work is pending, that stale completions are
// rejected, and that shutdown with pending work is safe.
#include "c2/frontend/gui/presentation.hpp"
#include "c2/frontend/gui/worker.hpp"
#include "check.hpp"
#include <atomic>
#include <chrono>
#include <future>
#include <thread>

using namespace c2::frontend::gui;

namespace {
// A source whose result is released by the test, never by a timer.
struct GatedSource {
    std::promise<SnapshotResult> gate;
    std::shared_future<SnapshotResult> result = gate.get_future().share();
    std::atomic<bool> started{false};
    SnapshotResult operator()() {
        started = true;
        return result.get();
    }
};
SnapshotResult ready_snapshot(const char* dir) {
    StoreSnapshot s;
    s.directory = dir;
    s.hunters = {{"h-1", "Hunter", false}};
    SnapshotResult r;
    r.snapshot = std::move(s);
    return r;
}
void wait_until(const std::function<bool()>& predicate) {
    for (int i = 0; i < 2000 && !predicate(); ++i) std::this_thread::sleep_for(std::chrono::milliseconds(1));
}

void ui_stays_responsive_and_stale_results_are_dropped() {
    Worker worker;
    PresentationModel model(DataSource{DataSource::Kind::supplied, "/slow"});
    GatedSource slow;
    const auto first = model.begin_load();
    worker.submit_result<SnapshotResult>([&slow] { return slow(); },
        [&model, first](SnapshotResult r) { model.complete_load(first, std::move(r)); });
    wait_until([&] { return slow.started.load(); });
    CHECK(slow.started);
    CHECK(model.load_state() == LoadState::loading);
    // The UI thread is not blocked: it navigates and drains nothing.
    CHECK(model.navigate(LodgeDestination::expeditions));
    CHECK(model.back());
    CHECK(worker.drain() == 0);

    // A newer request supersedes the slow one before it finishes.
    GatedSource fast;
    const auto second = model.begin_load();
    worker.submit_result<SnapshotResult>([&fast] { return fast(); },
        [&model, second](SnapshotResult r) { model.complete_load(second, std::move(r)); });
    // Release the old one first: its completion must be ignored.
    slow.gate.set_value(ready_snapshot("/slow"));
    wait_until([&] { return worker.pending_completions() >= 1; });
    CHECK(worker.drain() == 1);
    CHECK(model.load_state() == LoadState::loading);
    CHECK(!model.snapshot());
    // Now the newer one, failing honestly.
    SnapshotResult failed;
    failed.error = "store cannot be read: deterministic failure";
    fast.gate.set_value(failed);
    wait_until([&] { return worker.pending_completions() >= 1; });
    CHECK(worker.drain() == 1);
    CHECK(model.load_state() == LoadState::error);
    CHECK(model.load_error() == failed.error);
    CHECK(worker.idle());
}

void post_marshals_foreign_completions() {
    Worker worker;
    std::atomic<int> ran{0};
    std::thread foreign([&] { worker.post([&ran] { ++ran; }); });
    foreign.join();
    CHECK(ran == 0);                       // nothing runs until the UI drains
    CHECK(worker.drain() == 1);
    CHECK(ran == 1);
}

void shutdown_with_pending_work_is_safe() {
    std::atomic<int> completions{0};
    std::atomic<bool> finished{false};
    GatedSource blocked;
    {
        Worker worker;
        worker.submit_result<SnapshotResult>([&blocked] { return blocked(); },
            [&completions](SnapshotResult) { ++completions; });
        worker.submit([] {}, [&completions] { ++completions; });   // queued behind the blocked job
        wait_until([&] { return blocked.started.load(); });
        // Release from another thread while the worker is being destroyed:
        // the destructor must join without running any completion.
        std::thread releaser([&] {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            blocked.gate.set_value(ready_snapshot("/x"));
        });
        releaser.join();
        finished = true;
    }
    CHECK(finished);
    CHECK(completions == 0);
}
} // namespace

int main() {
    ui_stays_responsive_and_stale_results_are_dropped();
    post_marshals_foreign_completions();
    shutdown_with_pending_work_is_safe();
    if (test::failures) {
        std::cerr << test::failures << " check(s) failed\n";
        return 1;
    }
    std::cout << "gui worker tests passed\n";
    return 0;
}
