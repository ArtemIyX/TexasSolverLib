#pragma once

#include "ranges/propagation.hpp"
#include "util/abstraction.hpp"

#include <cstdint>
#include <vector>

namespace core {

struct BucketProjectionResult {
    CanonicalRange bucket_range;
    Probability input_mass = 0.0;
    Probability projected_mass = 0.0;
    Probability dropped_mass = 0.0;
    std::uint32_t bucket_count = 0;
};

[[nodiscard]] std::uint32_t bucket_count_for_street(
    const AbstractionTables& tables,
    Street street);

[[nodiscard]] BucketProjectionResult project_combo_range_to_buckets(
    const CanonicalRange& combo_range,
    const ComboIndex& combos,
    const AbstractionTables& tables,
    Street street);

[[nodiscard]] std::vector<double> combo_weights_to_bucket_weights(
    const std::vector<double>& combo_weights,
    const ComboIndex& combos,
    const AbstractionTables& tables,
    Street street);

}  // namespace core
