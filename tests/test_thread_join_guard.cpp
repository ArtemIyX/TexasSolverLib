#include "test_harness.hpp"
#include "util/thread_join_guard.hpp"

#include <atomic>
#include <cstddef>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

void expect_partial_launch_is_joined(std::size_t failure_index) {
    std::atomic<bool> stop{false};
    std::atomic<std::size_t> started{0};
    std::atomic<std::size_t> exited{0};
    bool caught = false;

    try {
        std::vector<std::thread> threads;
        auto guard = texas::detail::make_thread_join_guard(
            threads,
            [&stop] { stop.store(true, std::memory_order_release); });
        threads.reserve(20U);
        for (std::size_t worker = 0; worker < 20U; ++worker) {
            if (worker == failure_index) {
                throw std::runtime_error("injected thread launch failure");
            }
            threads.emplace_back([&] {
                started.fetch_add(1U, std::memory_order_relaxed);
                while (!stop.load(std::memory_order_acquire)) {
                    std::this_thread::yield();
                }
                exited.fetch_add(1U, std::memory_order_relaxed);
            });
        }
        guard.release();
    } catch (const std::runtime_error&) {
        caught = true;
    }

    EXPECT_TRUE(caught);
    EXPECT_EQ(started.load(std::memory_order_relaxed), failure_index);
    EXPECT_EQ(exited.load(std::memory_order_relaxed), failure_index);
}

}  // namespace

#define THREAD_LAUNCH_FAILURE_CASE(index)                                  \
    TEST_CASE(thread_join_guard_joins_partial_launch_at_##index) {         \
        expect_partial_launch_is_joined(index);                            \
    }

THREAD_LAUNCH_FAILURE_CASE(0)
THREAD_LAUNCH_FAILURE_CASE(1)
THREAD_LAUNCH_FAILURE_CASE(2)
THREAD_LAUNCH_FAILURE_CASE(3)
THREAD_LAUNCH_FAILURE_CASE(4)
THREAD_LAUNCH_FAILURE_CASE(5)
THREAD_LAUNCH_FAILURE_CASE(6)
THREAD_LAUNCH_FAILURE_CASE(7)
THREAD_LAUNCH_FAILURE_CASE(8)
THREAD_LAUNCH_FAILURE_CASE(9)
THREAD_LAUNCH_FAILURE_CASE(10)
THREAD_LAUNCH_FAILURE_CASE(11)
THREAD_LAUNCH_FAILURE_CASE(12)
THREAD_LAUNCH_FAILURE_CASE(13)
THREAD_LAUNCH_FAILURE_CASE(14)
THREAD_LAUNCH_FAILURE_CASE(15)
THREAD_LAUNCH_FAILURE_CASE(16)
THREAD_LAUNCH_FAILURE_CASE(17)
THREAD_LAUNCH_FAILURE_CASE(18)
THREAD_LAUNCH_FAILURE_CASE(19)

#undef THREAD_LAUNCH_FAILURE_CASE
