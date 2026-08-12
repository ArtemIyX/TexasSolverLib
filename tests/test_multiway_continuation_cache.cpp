#include "solver/multiway_rollout_leaf.hpp"
#include "test_harness.hpp"

#include <cstdint>
#include <limits>

namespace {

texas::MultiwayContinuationCacheKey make_key() {
    texas::MultiwayContinuationCacheKey key;
    key.public_state = {11U};
    key.traverser = 2;
    key.actor = 3;
    key.future_bucket = 7U;
    key.action_abstraction_version = 13U;
    key.leaf_model_version = 17U;
    key.range_context_identity = 19U;
    key.private_context_identity = 23U;
    key.seed_batch_identity = 29U;
    key.bias_factor_bits = 31U;
    key.seed_count = 3U;
    key.max_betting_actions = 64U;
    key.max_exact_runouts = 1'200U;
    return key;
}

texas::MultiwayRolloutProfileResult make_result() {
    texas::MultiwayRolloutProfileResult result;
    result.values = {1.0, 2.0, 3.0, 4.0};
    result.status = texas::MultiwayRolloutStatus::Complete;
    result.runout_mode = texas::MultiwayRolloutRunoutMode::None;
    result.seed_count = 3U;
    return result;
}

}  // namespace

TEST_CASE(multiway_continuation_cache_key_accepts_nominal_context) {
    EXPECT_TRUE(make_key().valid());
}

TEST_CASE(multiway_continuation_cache_key_accepts_zero_future_bucket) {
    auto key = make_key(); key.future_bucket = 0U; EXPECT_TRUE(key.valid());
}

TEST_CASE(multiway_continuation_cache_key_accepts_first_traverser) {
    auto key = make_key(); key.traverser = 0; EXPECT_TRUE(key.valid());
}

TEST_CASE(multiway_continuation_cache_key_accepts_last_traverser) {
    auto key = make_key(); key.traverser = 5; EXPECT_TRUE(key.valid());
}

TEST_CASE(multiway_continuation_cache_key_accepts_first_actor) {
    auto key = make_key(); key.actor = 0; EXPECT_TRUE(key.valid());
}

TEST_CASE(multiway_continuation_cache_key_accepts_last_actor) {
    auto key = make_key(); key.actor = 5; EXPECT_TRUE(key.valid());
}

TEST_CASE(multiway_continuation_cache_key_rejects_zero_public_state) {
    auto key = make_key(); key.public_state = {}; EXPECT_TRUE(!key.valid());
}

TEST_CASE(multiway_continuation_cache_key_rejects_negative_traverser) {
    auto key = make_key(); key.traverser = -1; EXPECT_TRUE(!key.valid());
}

TEST_CASE(multiway_continuation_cache_key_rejects_traverser_past_seat_limit) {
    auto key = make_key(); key.traverser = 6; EXPECT_TRUE(!key.valid());
}

TEST_CASE(multiway_continuation_cache_key_rejects_negative_actor) {
    auto key = make_key(); key.actor = -1; EXPECT_TRUE(!key.valid());
}

TEST_CASE(multiway_continuation_cache_key_rejects_actor_past_seat_limit) {
    auto key = make_key(); key.actor = 6; EXPECT_TRUE(!key.valid());
}

TEST_CASE(multiway_continuation_cache_key_rejects_zero_action_version) {
    auto key = make_key(); key.action_abstraction_version = 0U; EXPECT_TRUE(!key.valid());
}

TEST_CASE(multiway_continuation_cache_key_rejects_zero_leaf_version) {
    auto key = make_key(); key.leaf_model_version = 0U; EXPECT_TRUE(!key.valid());
}

TEST_CASE(multiway_continuation_cache_key_rejects_zero_range_identity) {
    auto key = make_key(); key.range_context_identity = 0U; EXPECT_TRUE(!key.valid());
}

TEST_CASE(multiway_continuation_cache_key_rejects_zero_private_identity) {
    auto key = make_key(); key.private_context_identity = 0U; EXPECT_TRUE(!key.valid());
}

TEST_CASE(multiway_continuation_cache_key_rejects_zero_seed_identity) {
    auto key = make_key(); key.seed_batch_identity = 0U; EXPECT_TRUE(!key.valid());
}

TEST_CASE(multiway_continuation_cache_key_rejects_zero_seed_count) {
    auto key = make_key(); key.seed_count = 0U; EXPECT_TRUE(!key.valid());
}

TEST_CASE(multiway_continuation_cache_key_rejects_zero_action_limit) {
    auto key = make_key(); key.max_betting_actions = 0U; EXPECT_TRUE(!key.valid());
}

TEST_CASE(multiway_continuation_cache_key_rejects_zero_exact_runout_limit) {
    auto key = make_key(); key.max_exact_runouts = 0U; EXPECT_TRUE(!key.valid());
}

TEST_CASE(multiway_continuation_cache_context_matches_identical_keys) {
    EXPECT_TRUE(make_key().same_context(make_key()));
}

TEST_CASE(multiway_continuation_cache_context_ignores_seed_batch_identity) {
    auto other = make_key(); ++other.seed_batch_identity; EXPECT_TRUE(make_key().same_context(other));
}

TEST_CASE(multiway_continuation_cache_context_separates_public_state) {
    auto other = make_key(); ++other.public_state.value; EXPECT_TRUE(!make_key().same_context(other));
}

TEST_CASE(multiway_continuation_cache_context_separates_traverser) {
    auto other = make_key(); --other.traverser; EXPECT_TRUE(!make_key().same_context(other));
}

TEST_CASE(multiway_continuation_cache_context_separates_actor) {
    auto other = make_key(); --other.actor; EXPECT_TRUE(!make_key().same_context(other));
}

TEST_CASE(multiway_continuation_cache_context_separates_future_bucket) {
    auto other = make_key(); ++other.future_bucket; EXPECT_TRUE(!make_key().same_context(other));
}

TEST_CASE(multiway_continuation_cache_context_separates_action_version) {
    auto other = make_key(); ++other.action_abstraction_version; EXPECT_TRUE(!make_key().same_context(other));
}

TEST_CASE(multiway_continuation_cache_context_separates_leaf_version) {
    auto other = make_key(); ++other.leaf_model_version; EXPECT_TRUE(!make_key().same_context(other));
}

TEST_CASE(multiway_continuation_cache_context_separates_range_identity) {
    auto other = make_key(); ++other.range_context_identity; EXPECT_TRUE(!make_key().same_context(other));
}

TEST_CASE(multiway_continuation_cache_context_separates_private_identity) {
    auto other = make_key(); ++other.private_context_identity; EXPECT_TRUE(!make_key().same_context(other));
}

TEST_CASE(multiway_continuation_cache_context_separates_bias_bits) {
    auto other = make_key(); ++other.bias_factor_bits; EXPECT_TRUE(!make_key().same_context(other));
}

TEST_CASE(multiway_continuation_cache_context_separates_seed_count) {
    auto other = make_key(); ++other.seed_count; EXPECT_TRUE(!make_key().same_context(other));
}

TEST_CASE(multiway_continuation_cache_context_separates_action_limit) {
    auto other = make_key(); ++other.max_betting_actions; EXPECT_TRUE(!make_key().same_context(other));
}

TEST_CASE(multiway_continuation_cache_context_separates_exact_runout_limit) {
    auto other = make_key(); ++other.max_exact_runouts; EXPECT_TRUE(!make_key().same_context(other));
}

TEST_CASE(multiway_continuation_cache_order_uses_public_state) {
    auto lower = make_key(); --lower.public_state.value; EXPECT_TRUE(lower < make_key());
}

TEST_CASE(multiway_continuation_cache_order_uses_traverser) {
    auto lower = make_key(); --lower.traverser; EXPECT_TRUE(lower < make_key());
}

TEST_CASE(multiway_continuation_cache_order_uses_actor) {
    auto lower = make_key(); --lower.actor; EXPECT_TRUE(lower < make_key());
}

TEST_CASE(multiway_continuation_cache_order_uses_future_bucket) {
    auto lower = make_key(); --lower.future_bucket; EXPECT_TRUE(lower < make_key());
}

TEST_CASE(multiway_continuation_cache_order_uses_action_version) {
    auto lower = make_key(); --lower.action_abstraction_version; EXPECT_TRUE(lower < make_key());
}

TEST_CASE(multiway_continuation_cache_order_uses_leaf_version) {
    auto lower = make_key(); --lower.leaf_model_version; EXPECT_TRUE(lower < make_key());
}

TEST_CASE(multiway_continuation_cache_order_uses_range_identity) {
    auto lower = make_key(); --lower.range_context_identity; EXPECT_TRUE(lower < make_key());
}

TEST_CASE(multiway_continuation_cache_order_uses_private_identity) {
    auto lower = make_key(); --lower.private_context_identity; EXPECT_TRUE(lower < make_key());
}

TEST_CASE(multiway_continuation_cache_order_uses_seed_identity) {
    auto lower = make_key(); --lower.seed_batch_identity; EXPECT_TRUE(lower < make_key());
}

TEST_CASE(multiway_continuation_cache_order_uses_bias_bits) {
    auto lower = make_key(); --lower.bias_factor_bits; EXPECT_TRUE(lower < make_key());
}

TEST_CASE(multiway_continuation_cache_order_uses_seed_count) {
    auto lower = make_key(); --lower.seed_count; EXPECT_TRUE(lower < make_key());
}

TEST_CASE(multiway_continuation_cache_order_uses_action_limit) {
    auto lower = make_key(); --lower.max_betting_actions; EXPECT_TRUE(lower < make_key());
}

TEST_CASE(multiway_continuation_cache_order_uses_exact_runout_limit) {
    auto lower = make_key(); --lower.max_exact_runouts; EXPECT_TRUE(lower < make_key());
}

TEST_CASE(multiway_continuation_cache_zero_entry_cap_rejects_admission) {
    texas::MultiwayContinuationCache cache(0U); EXPECT_TRUE(!cache.try_insert(make_key(), make_result()));
}

TEST_CASE(multiway_continuation_cache_subentry_byte_cap_rejects_admission) {
    texas::MultiwayContinuationCache cache(1U, texas::MultiwayContinuationCache::entry_bytes() - 1U);
    EXPECT_EQ(cache.capacity(), std::size_t{0});
}

TEST_CASE(multiway_continuation_cache_entry_cap_limits_capacity) {
    texas::MultiwayContinuationCache cache(2U); EXPECT_EQ(cache.capacity(), std::size_t{2});
}

TEST_CASE(multiway_continuation_cache_byte_cap_limits_capacity) {
    texas::MultiwayContinuationCache cache(4U, 2U * texas::MultiwayContinuationCache::entry_bytes());
    EXPECT_EQ(cache.capacity(), std::size_t{2});
}

TEST_CASE(multiway_continuation_cache_complete_result_round_trips) {
    texas::MultiwayContinuationCache cache(1U); texas::MultiwayRolloutProfileResult found;
    EXPECT_TRUE(cache.try_insert(make_key(), make_result())); EXPECT_TRUE(cache.find(make_key(), &found));
    EXPECT_NEAR(found.values[3], 4.0, 1e-12);
}

TEST_CASE(multiway_continuation_cache_accepts_capped_result) {
    auto result = make_result(); result.status = texas::MultiwayRolloutStatus::CappedFallback;
    texas::MultiwayContinuationCache cache(1U); EXPECT_TRUE(cache.try_insert(make_key(), result));
}

TEST_CASE(multiway_continuation_cache_accepts_exact_runout_mode) {
    auto result = make_result(); result.runout_mode = texas::MultiwayRolloutRunoutMode::Exact;
    texas::MultiwayContinuationCache cache(1U); EXPECT_TRUE(cache.try_insert(make_key(), result));
}

TEST_CASE(multiway_continuation_cache_accepts_seeded_runout_mode) {
    auto result = make_result(); result.runout_mode = texas::MultiwayRolloutRunoutMode::Seeded;
    texas::MultiwayContinuationCache cache(1U); EXPECT_TRUE(cache.try_insert(make_key(), result));
}

TEST_CASE(multiway_continuation_cache_accepts_mixed_runout_mode) {
    auto result = make_result(); result.runout_mode = texas::MultiwayRolloutRunoutMode::Mixed;
    texas::MultiwayContinuationCache cache(1U); EXPECT_TRUE(cache.try_insert(make_key(), result));
}

TEST_CASE(multiway_continuation_cache_duplicate_insert_preserves_size) {
    texas::MultiwayContinuationCache cache(2U); EXPECT_TRUE(cache.try_insert(make_key(), make_result()));
    EXPECT_TRUE(cache.try_insert(make_key(), make_result())); EXPECT_EQ(cache.size(), std::size_t{1});
}

TEST_CASE(multiway_continuation_cache_rejects_invalid_key_insert) {
    auto key = make_key(); key.public_state = {}; texas::MultiwayContinuationCache cache(1U);
    EXPECT_TRUE(!cache.try_insert(key, make_result()));
}

TEST_CASE(multiway_continuation_cache_rejects_invalid_status) {
    auto result = make_result(); result.status = texas::MultiwayRolloutStatus::InvalidContext;
    texas::MultiwayContinuationCache cache(1U); EXPECT_TRUE(!cache.try_insert(make_key(), result));
}

TEST_CASE(multiway_continuation_cache_rejects_invalid_runout_mode) {
    auto result = make_result(); result.runout_mode = static_cast<texas::MultiwayRolloutRunoutMode>(255U);
    texas::MultiwayContinuationCache cache(1U); EXPECT_TRUE(!cache.try_insert(make_key(), result));
}

TEST_CASE(multiway_continuation_cache_rejects_seed_count_mismatch) {
    auto result = make_result(); ++result.seed_count; texas::MultiwayContinuationCache cache(1U);
    EXPECT_TRUE(!cache.try_insert(make_key(), result));
}

TEST_CASE(multiway_continuation_cache_rejects_nan_policy_value) {
    auto result = make_result(); result.values[0] = std::numeric_limits<double>::quiet_NaN();
    texas::MultiwayContinuationCache cache(1U); EXPECT_TRUE(!cache.try_insert(make_key(), result));
}

TEST_CASE(multiway_continuation_cache_rejects_positive_infinite_policy_value) {
    auto result = make_result(); result.values[1] = std::numeric_limits<double>::infinity();
    texas::MultiwayContinuationCache cache(1U); EXPECT_TRUE(!cache.try_insert(make_key(), result));
}

TEST_CASE(multiway_continuation_cache_rejects_negative_infinite_policy_value) {
    auto result = make_result(); result.values[2] = -std::numeric_limits<double>::infinity();
    texas::MultiwayContinuationCache cache(1U); EXPECT_TRUE(!cache.try_insert(make_key(), result));
}

TEST_CASE(multiway_continuation_cache_find_rejects_null_output) {
    texas::MultiwayContinuationCache cache(1U); EXPECT_TRUE(cache.try_insert(make_key(), make_result()));
    EXPECT_TRUE(!cache.find(make_key(), nullptr));
}

TEST_CASE(multiway_continuation_cache_find_rejects_invalid_key) {
    auto key = make_key(); key.actor = -1; texas::MultiwayRolloutProfileResult found;
    texas::MultiwayContinuationCache cache(1U); EXPECT_TRUE(!cache.find(key, &found));
}

TEST_CASE(multiway_continuation_cache_find_reports_valid_miss) {
    auto missing = make_key(); ++missing.seed_batch_identity; texas::MultiwayRolloutProfileResult found;
    texas::MultiwayContinuationCache cache(1U); EXPECT_TRUE(!cache.find(missing, &found));
}

TEST_CASE(multiway_continuation_cache_full_admission_preserves_existing_entry) {
    texas::MultiwayContinuationCache cache(1U); auto other = make_key(); ++other.public_state.value;
    texas::MultiwayRolloutProfileResult found; EXPECT_TRUE(cache.try_insert(make_key(), make_result()));
    EXPECT_TRUE(!cache.try_insert(other, make_result())); EXPECT_TRUE(cache.find(make_key(), &found));
}

TEST_CASE(multiway_continuation_cache_sorted_insertion_preserves_all_lookup_values) {
    auto low = make_key(); --low.seed_batch_identity; auto high = make_key(); ++high.seed_batch_identity;
    auto low_value = make_result(); low_value.values[0] = -1.0; auto high_value = make_result(); high_value.values[0] = 9.0;
    texas::MultiwayContinuationCache cache(3U); texas::MultiwayRolloutProfileResult found;
    EXPECT_TRUE(cache.try_insert(high, high_value)); EXPECT_TRUE(cache.try_insert(low, low_value));
    EXPECT_TRUE(cache.try_insert(make_key(), make_result())); EXPECT_TRUE(cache.find(low, &found));
    EXPECT_NEAR(found.values[0], -1.0, 1e-12); EXPECT_TRUE(cache.find(high, &found)); EXPECT_NEAR(found.values[0], 9.0, 1e-12);
}

TEST_CASE(multiway_continuation_cache_memory_bytes_matches_reserved_payload) {
    texas::MultiwayContinuationCache cache(2U); EXPECT_EQ(cache.memory_bytes(), 2U * texas::MultiwayContinuationCache::entry_bytes());
}

TEST_CASE(multiway_continuation_cache_zero_capacity_has_zero_payload_bytes) {
    texas::MultiwayContinuationCache cache(0U); EXPECT_EQ(cache.memory_bytes(), std::uint64_t{0});
}

TEST_CASE(multiway_continuation_cache_variance_ignores_same_seed_identity) {
    texas::MultiwayContinuationCache cache(1U); texas::MultiwayContinuationDiagnostics diagnostics;
    EXPECT_TRUE(cache.try_insert(make_key(), make_result())); cache.record_repeated_seed_variance(make_key(), make_result(), &diagnostics);
    EXPECT_EQ(diagnostics.repeated_seed_pairs, std::uint64_t{0});
}

TEST_CASE(multiway_continuation_cache_variance_records_zero_for_equal_values) {
    texas::MultiwayContinuationCache cache(1U); texas::MultiwayContinuationDiagnostics diagnostics; auto other = make_key(); ++other.seed_batch_identity;
    EXPECT_TRUE(cache.try_insert(make_key(), make_result())); cache.record_repeated_seed_variance(other, make_result(), &diagnostics);
    EXPECT_EQ(diagnostics.repeated_seed_pairs, std::uint64_t{1}); EXPECT_NEAR(diagnostics.repeated_seed_variance[0], 0.0, 1e-12);
}

TEST_CASE(multiway_continuation_cache_variance_uses_pairwise_unbiased_estimate) {
    texas::MultiwayContinuationCache cache(1U); texas::MultiwayContinuationDiagnostics diagnostics; auto other = make_key(); ++other.seed_batch_identity;
    auto result = make_result(); result.values = {3.0, 1.0, 3.0, 8.0}; EXPECT_TRUE(cache.try_insert(make_key(), make_result()));
    cache.record_repeated_seed_variance(other, result, &diagnostics); EXPECT_NEAR(diagnostics.repeated_seed_variance[0], 2.0, 1e-12);
    EXPECT_NEAR(diagnostics.repeated_seed_variance[1], 0.5, 1e-12); EXPECT_NEAR(diagnostics.repeated_seed_variance[2], 0.0, 1e-12);
    EXPECT_NEAR(diagnostics.repeated_seed_variance[3], 8.0, 1e-12);
}

TEST_CASE(multiway_continuation_cache_variance_ignores_different_context) {
    texas::MultiwayContinuationCache cache(1U); texas::MultiwayContinuationDiagnostics diagnostics; auto other = make_key(); ++other.public_state.value;
    EXPECT_TRUE(cache.try_insert(make_key(), make_result())); cache.record_repeated_seed_variance(other, make_result(), &diagnostics);
    EXPECT_EQ(diagnostics.repeated_seed_pairs, std::uint64_t{0});
}

TEST_CASE(multiway_continuation_cache_variance_ignores_invalid_key) {
    texas::MultiwayContinuationCache cache(1U); texas::MultiwayContinuationDiagnostics diagnostics; auto invalid = make_key(); invalid.actor = -1;
    cache.record_repeated_seed_variance(invalid, make_result(), &diagnostics); EXPECT_EQ(diagnostics.repeated_seed_pairs, std::uint64_t{0});
}

TEST_CASE(multiway_continuation_cache_variance_accepts_null_diagnostics) {
    texas::MultiwayContinuationCache cache(1U); auto other = make_key(); ++other.seed_batch_identity;
    EXPECT_TRUE(cache.try_insert(make_key(), make_result())); cache.record_repeated_seed_variance(other, make_result(), nullptr);
    EXPECT_EQ(cache.size(), std::size_t{1});
}

TEST_CASE(multiway_continuation_cache_variance_averages_multiple_seed_pairs) {
    texas::MultiwayContinuationCache cache(2U); texas::MultiwayContinuationDiagnostics diagnostics;
    auto second = make_key(); ++second.seed_batch_identity; auto third = second; ++third.seed_batch_identity;
    auto zero = make_result(); zero.values.fill(0.0); auto two = make_result(); two.values.fill(2.0); auto four = make_result(); four.values.fill(4.0);
    EXPECT_TRUE(cache.try_insert(make_key(), zero)); EXPECT_TRUE(cache.try_insert(second, two));
    cache.record_repeated_seed_variance(third, four, &diagnostics); EXPECT_EQ(diagnostics.repeated_seed_pairs, std::uint64_t{2});
    EXPECT_NEAR(diagnostics.repeated_seed_variance[0], 5.0, 1e-12);
}
