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

TEST_CASE(multiway_continuation_selector_learns_a_regret_matched_policy_mixture) {
    texas::MultiwayFixedContinuationSelector selector(
        texas::MultiwayContinuationPolicyKind::Blueprint);
    const texas::MultiwayContinuationSelectionKey key = {
        {41U}, 0, texas::Street::Turn, 2U, 3U, 9U,
    };

    selector.update_regrets(key, {1.0, 0.0, 0.0, 0.0}, {0.0, 1.0, 2.0, 3.0});
    const auto mixture = selector.strategy(key);

    EXPECT_NEAR(mixture[0], 0.0, 1e-12);
    EXPECT_NEAR(mixture[1], 1.0 / 6.0, 1e-12);
    EXPECT_NEAR(mixture[2], 1.0 / 3.0, 1e-12);
    EXPECT_NEAR(mixture[3], 0.5, 1e-12);
    EXPECT_EQ(selector.select(key), texas::MultiwayContinuationPolicyKind::RaiseBiased);
}

TEST_CASE(multiway_continuation_selector_merges_worker_deltas_in_trajectory_order) {
    const texas::MultiwayContinuationSelectionKey key = {
        {51U}, 0, texas::Street::Turn, 3U, 3U, 9U,
    };
    const auto merged_strategy = [&](std::size_t worker_count) {
        texas::MultiwayFixedContinuationSelector selector(
            texas::MultiwayContinuationPolicyKind::Blueprint);
        std::vector<texas::MultiwayContinuationDeltaStream> streams;
        std::vector<const texas::MultiwayContinuationDeltaStream*> views;
        streams.reserve(worker_count);
        views.reserve(worker_count);
        for (std::size_t worker = 0U; worker < worker_count; ++worker) {
            streams.emplace_back(worker, 4U);
        }
        for (std::uint64_t trajectory = 0U; trajectory < 4U; ++trajectory) {
            const auto worker = static_cast<std::size_t>(trajectory % worker_count);
            EXPECT_TRUE(streams[worker].try_append({
                key,
                {1.0, 0.0, 0.0, 0.0},
                {0.0, static_cast<double>(trajectory + 1U), 2.0, 3.0},
                trajectory,
                0U,
            }));
        }
        for (auto& stream : streams) stream.sort_fixed_order();
        for (const auto& stream : streams) views.push_back(&stream);
        selector.merge_worker_streams(views);
        return selector.strategy(key);
    };

    const auto one_worker = merged_strategy(1U);
    const auto two_workers = merged_strategy(2U);
    for (std::size_t policy = 0U; policy < one_worker.size(); ++policy) {
        EXPECT_EQ(one_worker[policy], two_workers[policy]);
    }
}
