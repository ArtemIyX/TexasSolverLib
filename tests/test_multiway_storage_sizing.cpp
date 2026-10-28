#include "solver/multiway/engine/multiway_memory.hpp"
#include "test_harness.hpp"

#include <limits>

TEST_CASE(multiway_catalog_capacity_fixture_matches_packed_layout_terms)
{
	const auto estimate = texas::estimate_multiway_catalog_capacity(3U, 2U, 5U);
	EXPECT_EQ(estimate.node_bytes, 3U * sizeof(texas::MultiwayPackedPublicNode));
	EXPECT_EQ(estimate.menu_header_bytes, 2U * sizeof(std::vector<texas::MultiwayActionDescriptor>));
	EXPECT_EQ(estimate.menu_action_bytes, 5U * sizeof(texas::MultiwayActionDescriptor));
	EXPECT_TRUE(estimate.index_bytes > 0U);
	EXPECT_EQ(estimate.total_bytes,
		estimate.node_bytes + estimate.index_bytes + estimate.menu_header_bytes + estimate.menu_action_bytes + estimate.admission_scratch_bytes);
}

TEST_CASE(multiway_catalog_capacity_fixture_saturates_overflow)
{
	const auto estimate = texas::estimate_multiway_catalog_capacity(
		std::numeric_limits<std::uint64_t>::max(), 1U, 1U);
	EXPECT_EQ(estimate.total_bytes, std::numeric_limits<std::uint64_t>::max());
}
