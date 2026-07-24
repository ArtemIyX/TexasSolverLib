#pragma once

#include <thread>
#include <utility>
#include <vector>

namespace core::detail {

template <class StopFunction>
class ThreadJoinGuard {
public:
    ThreadJoinGuard(
        std::vector<std::thread>& threads,
        StopFunction stop_function)
        : threads_(threads),
          stop_function_(std::move(stop_function)) {
    }

    ThreadJoinGuard(const ThreadJoinGuard&) = delete;
    ThreadJoinGuard& operator=(const ThreadJoinGuard&) = delete;

    ~ThreadJoinGuard() noexcept {
        if (!active_) {
            return;
        }
        try {
            stop_function_();
        } catch (...) {
            // Joining already-created workers remains mandatory during unwind.
        }
        for (auto& thread : threads_) {
            if (!thread.joinable()) {
                continue;
            }
            try {
                thread.join();
            } catch (...) {
                // Each joinable thread is owned and joined exactly once here.
            }
        }
    }

    void release() noexcept {
        active_ = false;
    }

private:
    std::vector<std::thread>& threads_;
    StopFunction stop_function_;
    bool active_ = true;
};

template <class StopFunction>
ThreadJoinGuard<StopFunction> make_thread_join_guard(
    std::vector<std::thread>& threads,
    StopFunction stop_function) {
    return ThreadJoinGuard<StopFunction>(
        threads,
        std::move(stop_function));
}

}  // namespace core::detail
