#include "c2/frontend/gui/worker.hpp"
#include <memory>

namespace c2::frontend::gui {
Worker::Worker() : thread_([this] { run(); }) {}

Worker::~Worker() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
        work_.clear();
    }
    wake_.notify_all();
    if (thread_.joinable()) thread_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    completions_.clear();
}

void Worker::submit(std::function<void()> work, std::function<void()> completion) {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        work_.emplace_back(std::move(work), std::move(completion));
    }
    wake_.notify_one();
}

void Worker::post(std::function<void()> completion) {
    std::function<void()> wake;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (stopping_) return;
        completions_.push_back(std::move(completion));
        wake = wake_callback_;
    }
    if (wake) wake();
}

void Worker::set_wake(std::function<void()> wake) {
    std::lock_guard<std::mutex> lock(mutex_);
    wake_callback_ = std::move(wake);
}

std::size_t Worker::drain() {
    std::size_t ran = 0;
    for (;;) {
        std::function<void()> completion;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (completions_.empty()) break;
            completion = std::move(completions_.front());
            completions_.pop_front();
        }
        completion();
        ++ran;
    }
    return ran;
}

bool Worker::idle() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return work_.empty() && !busy_;
}

std::size_t Worker::pending_completions() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return completions_.size();
}

void Worker::run() {
    for (;;) {
        std::pair<std::function<void()>, std::function<void()>> job;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            wake_.wait(lock, [this] { return stopping_ || !work_.empty(); });
            if (stopping_) return;
            job = std::move(work_.front());
            work_.pop_front();
            busy_ = true;
        }
        job.first();
        std::function<void()> wake;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            busy_ = false;
            if (!stopping_) {
                completions_.push_back(std::move(job.second));
                wake = wake_callback_;
            }
        }
        if (wake) wake();
    }
}
} // namespace c2::frontend::gui
