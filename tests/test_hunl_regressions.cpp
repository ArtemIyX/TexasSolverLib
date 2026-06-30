#include "core/lib.hpp"
#include "games/hunl.hpp"
#include "test_harness.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <optional>

namespace {

struct EnvGuard {
    std::string name;
    std::optional<std::string> previous;

    EnvGuard(std::string env_name, std::optional<std::string> prev)
        : name(std::move(env_name)), previous(std::move(prev)) {}

    ~EnvGuard() {
#if defined(_MSC_VER)
        if (previous.has_value()) {
            _putenv_s(name.c_str(), previous->c_str());
        } else {
            _putenv_s(name.c_str(), "");
        }
#else
        if (previous.has_value()) {
            setenv(name.c_str(), previous->c_str(), 1);
        } else {
            unsetenv(name.c_str());
        }
#endif
    }
};

std::optional<std::string> get_env(const char* name) {
    const char* value = std::getenv(name);
    if (value == nullptr) {
        return std::nullopt;
    }
    return std::string(value);
}

core::HUNLState river_state() {
    return core::HUNLState::initial(std::make_shared<const core::HUNLConfig>(core::default_tiny_subgame()));
}

}  // namespace

TEST_CASE(hunl_regression_infoset_key_format_is_stable) {
    const auto state = river_state();
    EXPECT_EQ(state.infoset_key(0), std::string("KcAh|2d5s7cKhAs|r|"));
    EXPECT_EQ(state.infoset_key(1), std::string("QhQd|2d5s7cKhAs|r|"));
}

TEST_CASE(hunl_regression_flat_backend_populates_value_metrics) {
    auto config = core::default_tiny_subgame();
    const auto prev = get_env("TEXASSOLVER_HUNL_FLAT_BACKEND");
    EnvGuard guard("TEXASSOLVER_HUNL_FLAT_BACKEND", prev);
#if defined(_MSC_VER)
    _putenv_s("TEXASSOLVER_HUNL_FLAT_BACKEND", "flat");
#else
    setenv("TEXASSOLVER_HUNL_FLAT_BACKEND", "flat", 1);
#endif

    const auto output = core::lib::solve_hunl_postflop(config, 10, 1.5, 0.0, 2.0, 4, 8, true);

    EXPECT_TRUE(std::isfinite(output.game_value));
    EXPECT_TRUE(std::isfinite(output.exploitability));
    EXPECT_TRUE(output.game_value != 0.0);
    EXPECT_TRUE(output.exploitability != 0.0);
    EXPECT_TRUE(!output.average_strategy.empty());
}

