#include "solver/multiway_continuation_selector.hpp"
#include "test_harness.hpp"

TEST_CASE(multiway_fixed_continuation_selector_uses_only_the_public_information_set_key) {
    const core::MultiwayFixedContinuationSelector selector(
        core::MultiwayContinuationPolicyKind::RaiseBiased);
    const core::MultiwayContinuationSelectionKey first = {
        {17U}, 2, core::Street::Turn, 51U, 3U, 9U,
    };
    const core::MultiwayContinuationSelectionKey second = {
        {17U}, 2, core::Street::Turn, 51U, 3U, 9U,
    };

    EXPECT_TRUE(selector.valid());
    EXPECT_TRUE(first.valid());
    EXPECT_EQ(selector.select(first), core::MultiwayContinuationPolicyKind::RaiseBiased);
    EXPECT_EQ(selector.select(second), core::MultiwayContinuationPolicyKind::RaiseBiased);
}

TEST_CASE(multiway_fixed_continuation_selector_rejects_invalid_public_keys) {
    const core::MultiwayFixedContinuationSelector selector(
        core::MultiwayContinuationPolicyKind::CallBiased);
    const core::MultiwayContinuationSelectionKey invalid = {
        {}, 0, core::Street::Flop, 0U, 1U, 1U,
    };
    const core::MultiwayFixedContinuationSelector invalid_selector(
        static_cast<core::MultiwayContinuationPolicyKind>(255U));

    EXPECT_TRUE(!invalid.valid());
    EXPECT_EQ(selector.select(invalid), core::MultiwayContinuationPolicyKind::Blueprint);
    EXPECT_TRUE(!invalid_selector.valid());
}
