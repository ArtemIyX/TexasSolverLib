#include "core/lib.hpp"
#include "test_harness.hpp"

#include <stdexcept>

namespace {

core::MultiwayAivatEvaluationRecord record_fixture() {
    core::MultiwayBlueprintConfig config;
    core::MultiwayAivatEvaluationRecord record;
    record.identity = core::make_multiway_model_identity(config);
    record.public_history = core::MultiwayHandHistory::from_rules(core::MultiwayGameRules::standard_6max(), 0, 71U);
    record.public_history.events.push_back({
        core::MultiwayReplayEventKind::Decision,
        {0, core::MultiwayAction::Fold, 0, 73U},
        core::Street::Flop,
        0});
    record.raw_chip_outcome.seat_count = 6U;
    record.raw_chip_outcome.values[0] = -100.0;
    record.raw_chip_outcome.values[1] = 100.0;
    core::MultiwayAivatDecisionRecord decision;
    decision.decision_index = 1U;
    decision.acting_seat = 0;
    decision.sampled_action = {core::MultiwayAction::Fold, 0U, 0, 17U};
    decision.decision_seed = 73U;
    decision.action_values = {
        {{core::MultiwayAction::Fold, 0U, 0, 17U}, 0.25, -100.0},
        {{core::MultiwayAction::Call, 1U, 100, 17U}, 0.75, 25.0},
    };
    record.decisions.push_back(decision);
    record.seal();
    return record;
}

bool accepting_sink(const core::MultiwayAivatEvaluationRecord& record, const void* context) noexcept {
    const auto* expected_hash = static_cast<const std::uint64_t*>(context);
    return record.integrity_hash == *expected_hash;
}

}  // namespace

TEST_CASE(multiway_aivat_record_seals_public_history_policy_values_and_raw_outcome) {
    const auto record = record_fixture();
    record.validate();
    EXPECT_TRUE(record.integrity_hash != 0U);
    EXPECT_EQ(record.decisions.size(), std::size_t{1});
    EXPECT_EQ(record.raw_chip_outcome.values[0], -100.0);

    const auto accepted = core::publish_multiway_aivat_evaluation_record(
        record, accepting_sink, &record.integrity_hash);
    EXPECT_TRUE(accepted);
}

TEST_CASE(multiway_aivat_record_rejects_tampered_history_policy_and_outcome) {
    auto history_tampered = record_fixture();
    history_tampered.public_history.events.front().decision.decision_seed = 74U;
    EXPECT_THROW(history_tampered.validate(), std::invalid_argument);

    auto policy_tampered = record_fixture();
    policy_tampered.decisions.front().action_values[0].probability = 0.5;
    EXPECT_THROW(policy_tampered.validate(), std::invalid_argument);

    auto outcome_tampered = record_fixture();
    outcome_tampered.raw_chip_outcome.values[0] = -99.0;
    EXPECT_THROW(outcome_tampered.validate(), std::invalid_argument);
}

TEST_CASE(multiway_aivat_record_rejects_missing_protected_sink) {
    const auto record = record_fixture();
    EXPECT_THROW(
        core::publish_multiway_aivat_evaluation_record(record, nullptr, nullptr),
        std::invalid_argument);
}
