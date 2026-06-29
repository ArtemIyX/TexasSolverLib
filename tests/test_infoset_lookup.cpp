#include "games/kuhn.hpp"
#include "test_harness.hpp"
#include "util/infoset_lookup.hpp"

TEST_CASE(infoset_lookup_interns_once_and_caches_locked_strategy) {
    core::InfosetRegistry registry;
    auto state = core::KuhnState::initial();
    state = state.next_state(11);
    state = state.next_state(12);

    std::unordered_map<core::InfosetKey, std::vector<core::Probability>> locked = {
        {state.infoset_key(0), {0.25, 0.75}},
    };
    std::unordered_map<core::InfosetId, std::vector<core::Probability>> locked_by_id;

    const auto id0 = core::lookup_infoset_id(
        state, 0, registry, state.legal_actions().size(), &locked, &locked_by_id);
    const auto id1 = core::lookup_infoset_id(
        state, 0, registry, state.legal_actions().size(), &locked, &locked_by_id);

    EXPECT_EQ(id0, id1);
    EXPECT_EQ(registry.size(), 1U);
    EXPECT_EQ(registry.meta_for(id0).action_count, state.legal_actions().size());
    EXPECT_EQ(locked_by_id.size(), 1U);
    EXPECT_EQ(locked_by_id.at(id0).size(), 2U);
    EXPECT_NEAR(locked_by_id.at(id0)[0], 0.25, 1e-12);
    EXPECT_NEAR(locked_by_id.at(id0)[1], 0.75, 1e-12);
}
