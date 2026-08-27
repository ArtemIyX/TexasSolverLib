#include "solver/multiway/evaluation/multiway_evaluation.hpp"
#include "test_harness.hpp"

#include <stdexcept>

namespace {

texas::solver::multiway::MultiwayAivatEvaluationRecord record_fixture() {
    texas::solver::multiway::MultiwayBlueprintConfig config;
    texas::solver::multiway::MultiwayAivatEvaluationRecord record;
    record.identity = texas::solver::multiway::make_multiway_model_identity(config);
    record.public_history = texas::games::multiway::MultiwayHandHistory::from_rules(texas::games::multiway::MultiwayGameRules::standard_6max(), 0, 71U);
    record.public_history.events.push_back({
        texas::games::multiway::MultiwayReplayEventKind::Decision,
        {0, texas::games::multiway::MultiwayAction::Fold, 0, 73U},
        texas::core::Street::Flop,
        0});
    record.raw_chip_outcome.seat_count = 6U;
    record.raw_chip_outcome.values[0] = -100.0;
    record.raw_chip_outcome.values[1] = 100.0;
    texas::solver::multiway::MultiwayAivatDecisionRecord decision;
    decision.decision_index = 1U;
    decision.acting_seat = 0;
    decision.sampled_action = {texas::games::multiway::MultiwayAction::Fold, 0U, 0, 17U};
    decision.decision_seed = 73U;
    decision.action_values = {
        {{texas::games::multiway::MultiwayAction::Fold, 0U, 0, 17U}, 0.25, -100.0},
        {{texas::games::multiway::MultiwayAction::Call, 1U, 100, 17U}, 0.75, 25.0},
    };
    record.decisions.push_back(decision);
    record.seal();
    return record;
}

bool accepting_sink(const texas::solver::multiway::MultiwayAivatEvaluationRecord& record, const void* context) noexcept {
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

    const auto accepted = texas::solver::multiway::publish_multiway_aivat_evaluation_record(
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
        texas::solver::multiway::publish_multiway_aivat_evaluation_record(record, nullptr, nullptr),
        std::invalid_argument);
}

TEST_CASE(multiway_aivat_estimator_applies_action_value_correction) {
    const auto estimate = texas::solver::multiway::estimate_multiway_aivat({record_fixture()});
    EXPECT_EQ(estimate.samples, 1U);
    EXPECT_NEAR(estimate.means[0], -6.25, 1e-12);
    EXPECT_NEAR(estimate.means[1], 100.0, 1e-12);
    EXPECT_NEAR(estimate.standard_errors[0], 0.0, 1e-12);
}

TEST_CASE(multiway_aivat_estimator_rejects_mixed_model_identities) {
    auto other = record_fixture();
    ++other.identity.code_schema_hash;
    other.seal();
    EXPECT_THROW(
        texas::solver::multiway::estimate_multiway_aivat({record_fixture(), other}),
        std::invalid_argument);
}
