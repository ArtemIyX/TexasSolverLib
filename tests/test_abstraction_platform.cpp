#include "test_harness.hpp"
#include "util/abstraction.hpp"

#if !defined(_WIN32)

#include <stdexcept>

TEST_CASE(abstraction_loader_reports_optional_platform_requirement) {
    EXPECT_THROW(
        texas::load_abstraction("unsupported-platform.npz"),
        std::runtime_error);
}

#endif
