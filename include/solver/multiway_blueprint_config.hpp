#pragma once

#include "games/multiway_rake.hpp"

#include <cstdint>

namespace core {

// Immutable configuration for one offline multiway blueprint. All chip values
// are exact integers; stack bands and action templates are added in later
// phases and receive their own version identities.
struct MultiwayBlueprintConfig {
    std::uint8_t player_count = 6;
    int initial_stack_chips = 10'000;
    int small_blind_chips = 50;
    int big_blind_chips = 100;
    int ante_chips = 0;
    MultiwayRakePolicy rake_policy = MultiwayRakePolicy::explicit_zero();

    std::uint32_t flop_bucket_count = 96;
    std::uint32_t turn_bucket_count = 128;
    std::uint32_t river_bucket_count = 192;

    std::uint64_t action_abstraction_version = 1;
    std::uint64_t bucket_model_version = 1;
    std::uint64_t terminal_model_version = 1;
    std::uint64_t code_schema_version = 1;

    void validate() const;
};

}  // namespace core
