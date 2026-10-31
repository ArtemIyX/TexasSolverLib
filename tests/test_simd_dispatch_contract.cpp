#include "util/simd.hpp"
#include "solver/hunl/sampled/hunl_sampled_simd.hpp"

#include "test_harness.hpp"

#include <cstdint>
#include <string_view>
#include <vector>

TEST_CASE(simd_backend_name_is_stable)
{
	EXPECT_EQ(texas::util::simd_backend_name(texas::util::SimdBackend::Scalar), "scalar");
	EXPECT_EQ(texas::util::simd_backend_name(texas::util::SimdBackend::Sse2), "sse2");
	EXPECT_EQ(texas::util::simd_backend_name(texas::util::SimdBackend::Avx2), "avx2");
}

TEST_CASE(sampled_backend_name_does_not_claim_fma)
{
	EXPECT_TRUE(std::string_view{
		texas::solver::hunl::hunl_sampled_simd_backend_name(
			texas::solver::hunl::HUNLSampledSimdBackend::Avx2)} == "avx2");
}

TEST_CASE(simd_update_strategy_supports_exact_in_place_alias)
{
	std::vector<double> values{ 1.0, -2.0, 3.0 };
	texas::util::update_strategy_sum_scalar(values.data(), values.data(), values.size(), 2.0);
	EXPECT_EQ(values[0], 3.0);
	EXPECT_EQ(values[1], -6.0);
	EXPECT_EQ(values[2], 9.0);
}

TEST_CASE(sampled_selected_kernel_matches_scalar_reference)
{
	constexpr std::uint32_t actions = 3;
	constexpr std::uint32_t buckets = 5;
	const std::vector<float> regrets{
		1.0f, -2.0f, 0.0f, 4.0f, -1.0f,
		2.0f, 3.0f, -4.0f, 0.0f, 1.0f,
		-1.0f, 2.0f, 5.0f, -3.0f, 0.0f };
	std::vector<float> expected(actions * buckets, 0.0f);
	std::vector<float> actual(actions * buckets, 0.0f);
	texas::solver::hunl::regret_matching_action_major_f32_scalar(
		regrets.data(), actions, buckets, expected.data());
	texas::solver::hunl::regret_matching_action_major_f32(
		regrets.data(), actions, buckets, actual.data());
	for (std::size_t i = 0; i < expected.size(); ++i)
	{
		EXPECT_NEAR(actual[i], expected[i], 1.0e-6f);
	}
}

TEST_CASE(simd_selected_double_kernels_match_scalar_reference)
{
	std::vector<double> expected{ 2.0, -3.0, 0.0, 4.0, -5.0, 6.0, 7.0 };
	std::vector<double> actual = expected;
	texas::util::discount_regrets_scalar(expected.data(), expected.size(), 0.5, 0.25);
	texas::util::discount_regrets(actual.data(), actual.size(), 0.5, 0.25);
	for (std::size_t i = 0; i < expected.size(); ++i)
	{
		EXPECT_EQ(actual[i], expected[i]);
	}

	const std::vector<double> lhs{ 1.0, -2.0, 3.0, 4.0, 5.0 };
	const std::vector<double> rhs{ 0.5, 1.5, -1.0, 2.0, 3.0 };
	EXPECT_NEAR(texas::util::dot_product(lhs.data(), rhs.data(), lhs.size()),
		texas::util::dot_product_scalar(lhs.data(), rhs.data(), lhs.size()), 1.0e-12);
}
