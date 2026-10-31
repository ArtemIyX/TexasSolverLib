#include "solver/hunl/sampled/hunl_sampled_storage.hpp"
#include "games/hunl_flat_graph_types.hpp"
#include "solver/hunl/sampled/hunl_sampled_builder.hpp"
#include "solver/multiway/engine/multiway_solver.hpp"
#include "test_harness.hpp"

#include <cstddef>
#include <cstdint>
#include <type_traits>

using SampledInfosetShape = texas::solver::hunl::HUNLSampledInfosetShape;
using SampledInfosetMeta = texas::solver::hunl::HUNLSampledInfosetMeta;
using MultiwayInfosetId = texas::solver::multiway::MultiwayInfosetId;
using MultiwaySparseRowShape = texas::solver::multiway::MultiwaySparseRowShape;

static_assert(sizeof(SampledInfosetShape) == 20, "sampled row shape ABI size changed");
static_assert(alignof(SampledInfosetShape) == 4, "sampled row shape alignment changed");
static_assert(offsetof(SampledInfosetShape, bucket_count) == 12, "sampled bucket offset changed");
static_assert(offsetof(SampledInfosetShape, action_count) == 16, "sampled action offset changed");

static_assert(sizeof(SampledInfosetMeta) == 40, "sampled metadata must remain compact");
static_assert(alignof(SampledInfosetMeta) == 8, "sampled metadata alignment changed");
static_assert(sizeof(MultiwayInfosetId) == 16, "multiway infoset ID ABI size changed");
static_assert(alignof(MultiwayInfosetId) == 8, "multiway infoset ID alignment changed");
static_assert(sizeof(MultiwaySparseRowShape) == 24, "multiway row shape ABI size changed");
static_assert(alignof(MultiwaySparseRowShape) == 8, "multiway row shape alignment changed");

TEST_CASE(sampled_infoset_meta_uses_natural_compact_layout)
{
	using texas::solver::hunl::HUNLSampledInfosetMeta;
	EXPECT_EQ(sizeof(HUNLSampledInfosetMeta), std::size_t{ 40 });
	EXPECT_EQ(alignof(HUNLSampledInfosetMeta), alignof(std::size_t));
	EXPECT_EQ(offsetof(HUNLSampledInfosetMeta, regret_offset), std::size_t{ 0 });
	EXPECT_EQ(offsetof(HUNLSampledInfosetMeta, strategy_sum_offset), sizeof(std::size_t));
	EXPECT_EQ(offsetof(HUNLSampledInfosetMeta, id), std::size_t{ 16 });
	EXPECT_EQ(offsetof(HUNLSampledInfosetMeta, player), std::size_t{ 20 });
	EXPECT_EQ(offsetof(HUNLSampledInfosetMeta, bucket_count), std::size_t{ 24 });
	EXPECT_EQ(offsetof(HUNLSampledInfosetMeta, last_discount_iter), std::size_t{ 28 });
	EXPECT_EQ(offsetof(HUNLSampledInfosetMeta, action_count), std::size_t{ 32 });
	EXPECT_EQ(offsetof(HUNLSampledInfosetMeta, street), std::size_t{ 33 });
	EXPECT_TRUE(std::is_trivially_copyable_v<HUNLSampledInfosetMeta>);
	EXPECT_TRUE(std::is_standard_layout_v<HUNLSampledInfosetMeta>);
}

TEST_CASE(candidate_layouts_remain_explicit_and_record_compatible)
{
	using texas::games::hunl::HUNLFlatNodeMeta;
	using texas::solver::hunl::HUNLSampledNode;
	using texas::solver::multiway::MultiwayInfosetId;
	using texas::solver::multiway::MultiwayPackedPublicNode;
	using texas::solver::multiway::MultiwaySparseRowShape;
	EXPECT_EQ(alignof(HUNLFlatNodeMeta), std::size_t{ 8 });
	EXPECT_EQ(alignof(HUNLSampledNode), std::size_t{ 8 });
	EXPECT_EQ(alignof(MultiwayInfosetId), std::size_t{ 8 });
	EXPECT_EQ(alignof(MultiwayPackedPublicNode), std::size_t{ 8 });
	EXPECT_EQ(alignof(MultiwaySparseRowShape), std::size_t{ 8 });
	EXPECT_EQ(offsetof(MultiwayPackedPublicNode, id), std::size_t{ 0 });
	EXPECT_EQ(offsetof(MultiwayPackedPublicNode, parent_id), sizeof(std::uint64_t));
	EXPECT_EQ(offsetof(MultiwaySparseRowShape, infoset), std::size_t{ 0 });
	EXPECT_EQ(offsetof(MultiwaySparseRowShape, bucket_count), std::size_t{ 16 });
	EXPECT_EQ(offsetof(MultiwaySparseRowShape, action_count), std::size_t{ 20 });
	EXPECT_TRUE(std::is_trivially_copyable_v<MultiwayPackedPublicNode>);
	EXPECT_TRUE(std::is_standard_layout_v<MultiwayPackedPublicNode>);
}
