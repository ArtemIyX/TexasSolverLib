#include "logging/log.hpp"
#include "test_harness.hpp"

namespace
{
	int side_effects = 0;
	int side_effect()
	{
		++side_effects;
		return side_effects;
	}
} // namespace

TEST_CASE(logging_disabled_macros_do_not_evaluate_arguments)
{
	TEXAS_LOG_TRACE(Core, "{}", side_effect());
	TEXAS_LOG_DEBUG(Core, "{}", side_effect());
	TEXAS_LOG_INFO(Core, "{}", side_effect());
	TEXAS_LOG_WARNING(Core, "{}", side_effect());
	TEXAS_LOG_ERROR(Core, "{}", side_effect());
	TEXAS_LOG_CRITICAL(Core, "{}", side_effect());
	EXPECT_EQ(side_effects, 0);
}
