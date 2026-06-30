#include <chrono>
#include <cstdint>
#include <iostream>

namespace {

std::uint64_t fibonacci(int n) {
    if (n <= 1) {
        return static_cast<std::uint64_t>(n);
    }
    return fibonacci(n - 1) + fibonacci(n - 2);
}

std::uint64_t busy_loop(int rounds) {
    std::uint64_t sum = 0;
    for (int i = 0; i < rounds; ++i) {
        sum += static_cast<std::uint64_t>(i) * 2654435761ULL;
    }
    return sum;
}

template <typename Fn>
void profile_step(const char* name, Fn&& fn) {
    const auto start = std::chrono::steady_clock::now();
    const auto value = fn();
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed = end - start;
    const auto us = std::chrono::duration_cast<std::chrono::microseconds>(elapsed).count();
    const auto ms = std::chrono::duration<double, std::milli>(elapsed).count();

    std::cout << name << ": result=" << value << ", elapsed_ms=" << ms << '\n';
    std::cout << "  elapsed_us=" << us << '\n';
}

} // namespace

int main() {
    std::cout << "Simple console profiling demo\n";
    profile_step("fibonacci(20)", [] { return fibonacci(20); });
    profile_step("busy_loop(2500000)", [] { return busy_loop(2'500'000); });
    profile_step("mixed_workload", [] {
        auto a = fibonacci(18);
        auto b = busy_loop(1'000'000);
        return a ^ b;
    });
    return 0;
}
