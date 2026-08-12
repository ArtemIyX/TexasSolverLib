#include "solver/multiway_blueprint_config.hpp"

#include <stdexcept>

namespace core {

void MultiwayBlueprintConfig::validate() const {
    if (player_count < 2U || player_count > 6U) {
        throw std::invalid_argument("multiway blueprint requires two through six players");
    }
    if (initial_stack_chips <= 0 || small_blind_chips <= 0 || big_blind_chips <= 0 ||
        small_blind_chips > big_blind_chips || ante_chips < 0 ||
        initial_stack_chips < big_blind_chips) {
        throw std::invalid_argument("multiway blueprint has invalid chip rules");
    }
    rake_policy.validate();
    if (flop_bucket_count == 0U || turn_bucket_count == 0U || river_bucket_count == 0U) {
        throw std::invalid_argument("multiway blueprint requires non-zero bucket counts");
    }
    if (action_abstraction_version == 0U || bucket_model_version == 0U ||
        terminal_model_version == 0U || rules_profile_version == 0U ||
        resolver_schema_version == 0U || code_schema_version == 0U ||
        range_semantics_version == 0U || future_bucket_model_version == 0U ||
        off_tree_policy_version == 0U || continuation_policy_version == 0U ||
        runtime_search_schema_version == 0U ||
        !is_valid_multiway_continuation_policy(continuation_policy)) {
        throw std::invalid_argument("multiway blueprint requires non-zero model versions");
    }
}

}  // namespace core
