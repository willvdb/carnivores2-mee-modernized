#pragma once
// One owned worker thread and one completion queue. Work runs off the UI
// thread; completions run only when the UI thread calls drain(). Foreign
// threads (for example a native dialog callback) marshal with post().
// Deliberately not a job framework: no priorities, no cancellation, no pools.
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <utility>

namespace c2::frontend::gui {
class Worker {
public:
    Worker();
    // Stops accepting work, lets the running job finish, joins the thread and
    // discards queued completions. Nothing is detached.
    ~Worker();
    void shutdown(); // join before releasing stores, documents or SDL
    Worker(const Worker&) = delete;
    Worker& operator=(const Worker&) = delete;

    // Runs `work` on the worker thread, then queues `completion` for drain().
    void submit(std::function<void()> work, std::function<void()> completion);
    // Runs `work` on the worker thread and hands its result to `completion`
    // on the draining thread.
    template <typename T>
    void submit_result(std::function<T()> work, std::function<void(T)> completion) {
        auto slot = std::make_shared<T>();
        submit([slot, work = std::move(work)] { *slot = work(); },
               [slot, completion = std::move(completion)] { completion(std::move(*slot)); });
    }
    // Queues a completion from any thread without running work first.
    void post(std::function<void()> completion);
    // Runs queued completions on the calling thread. Returns how many ran.
    std::size_t drain();
    // Optional: called (from any thread, never under the queue lock) whenever a
    // completion is queued, so an event loop blocked in a wait can wake up.
    void set_wake(std::function<void()> wake);
    bool idle() const;   // no work running or waiting (completions may be queued)
    std::size_t pending_completions() const;

private:
    void run();
    mutable std::mutex mutex_;
    std::condition_variable wake_;
    std::deque<std::pair<std::function<void()>, std::function<void()>>> work_;
    std::deque<std::function<void()>> completions_;
    std::function<void()> wake_callback_;
    bool stopping_ = false;
    bool busy_ = false;
    std::thread thread_;
};
} // namespace c2::frontend::gui
