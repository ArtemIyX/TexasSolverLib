#include "solver/multiway_continuation_selector.hpp"
#include "test_harness.hpp"

TEST_CASE(multiway_fixed_continuation_selector_uses_only_the_public_information_set_key) {
    const texas::MultiwayFixedContinuationSelector selector(
        texas::MultiwayContinuationPolicyKind::RaiseBiased);
    const texas::MultiwayContinuationSelectionKey first = {
        {17U}, 2, texas::Street::Turn, 51U, 3U, 9U,
    };
    const texas::MultiwayContinuationSelectionKey second = {
        {17U}, 2, texas::Street::Turn, 51U, 3U, 9U,
    };

    EXPECT_TRUE(selector.valid());
    EXPECT_TRUE(first.valid());
    EXPECT_EQ(selector.select(first), texas::MultiwayContinuationPolicyKind::RaiseBiased);
    EXPECT_EQ(selector.select(second), texas::MultiwayContinuationPolicyKind::RaiseBiased);
}

TEST_CASE(multiway_fixed_continuation_selector_rejects_invalid_public_keys) {
    const texas::MultiwayFixedContinuationSelector selector(
        texas::MultiwayContinuationPolicyKind::CallBiased);
    const texas::MultiwayContinuationSelectionKey invalid = {
        {}, 0, texas::Street::Flop, 0U, 1U, 1U,
    };
    const texas::MultiwayFixedContinuationSelector invalid_selector(
        static_cast<texas::MultiwayContinuationPolicyKind>(255U));

    EXPECT_TRUE(!invalid.valid());
    EXPECT_EQ(selector.select(invalid), texas::MultiwayContinuationPolicyKind::Blueprint);
    EXPECT_TRUE(!invalid_selector.valid());
}

TEST_CASE(multiway_continuation_selector_regret_matches_each_information_set) {
    texas::MultiwayFixedContinuationSelector selector(
        texas::MultiwayContinuationPolicyKind::Blueprint);
    const texas::MultiwayContinuationSelectionKey key = {
        {29U}, 1, texas::Street::River, 7U, 3U, 9U,
    };
    const texas::MultiwayContinuationSelectionKey other = {
        {30U}, 1, texas::Street::River, 7U, 3U, 9U,
    };
    selector.set_regrets(key, {0.0, 1.0, 4.0, 0.0});
    selector.set_regrets(other, {0.0, 0.0, 0.0, 2.0});
    EXPECT_EQ(selector.select(key), texas::MultiwayContinuationPolicyKind::CallBiased);
    EXPECT_EQ(selector.select(other), texas::MultiwayContinuationPolicyKind::RaiseBiased);
    EXPECT_EQ(selector.select({{31U}, 1, texas::Street::River, 7U, 3U, 9U}),
        texas::MultiwayContinuationPolicyKind::Blueprint);
}
