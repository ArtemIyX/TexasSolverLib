#pragma once

#include <cstdint>
#include <string_view>

namespace core::profiling {

bool enabled() noexcept;
void mark(std::string_view name, double seconds) noexcept;
void print_profiler_report();

class ScopedTimer {
public:
    explicit ScopedTimer(std::string_view name) noexcept;
    ~ScopedTimer();

    ScopedTimer(const ScopedTimer&) = delete;
    ScopedTimer& operator=(const ScopedTimer&) = delete;

private:
    std::string_view name_;
    std::uint64_t start_ns_ = 0;
};

void write_report();

}  // namespace core::profiling

#define TEXASSOLVER_PROFILE_SCOPE(name_literal) \
    TEXASSOLVER_PROFILE_SCOPE_IMPL(name_literal, __COUNTER__)

#define TEXASSOLVER_PROFILE_SCOPE_IMPL(name_literal, counter) \
    ::core::profiling::ScopedTimer TEXASSOLVER_PROFILE_SCOPE_NAME(counter){name_literal}

#define TEXASSOLVER_PROFILE_SCOPE_NAME(counter) TEXASSOLVER_PROFILE_SCOPE_NAME_IMPL(counter)
#define TEXASSOLVER_PROFILE_SCOPE_NAME_IMPL(counter) _profile_scope_##counter
