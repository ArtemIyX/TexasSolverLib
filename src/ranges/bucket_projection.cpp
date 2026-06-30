#include "ranges/bucket_projection.hpp"

#include <stdexcept>

namespace core {

namespace {

std::size_t street_bucket_index(Street street) {
    switch (street) {
        case Street::Flop:
            return 0;
        case Street::Turn:
            return 1;
        case Street::River:
            return 2;
        default:
            throw std::invalid_argument("bucket projection requires Flop/Turn/River street");
    }
}

RangeMask full_bucket_mask(std::size_t bucket_count) {
    RangeMask mask;
    mask.kind = RangeVector::Kind::Bucket;
    mask.enabled.assign(bucket_count, 1U);
    return mask;
}

RangeVector make_bucket_vector(std::size_t bucket_count) {
    RangeVector range;
    range.kind = RangeVector::Kind::Bucket;
    range.weights.assign(bucket_count, 0.0);
    return range;
}

}  // namespace

std::uint32_t bucket_count_for_street(const AbstractionTables& tables, Street street) {
    const auto index = street_bucket_index(street);
    if (index >= tables.metadata.bucket_counts.size()) {
        throw std::out_of_range("bucket_count_for_street missing metadata for street");
    }
    return tables.metadata.bucket_counts[index];
}

BucketProjectionResult project_combo_range_to_buckets(
    const CanonicalRange& combo_range,
    const ComboIndex& combos,
    const AbstractionTables& tables,
    Street street) {
    if (combo_range.range.kind != RangeVector::Kind::Combo) {
        throw std::invalid_argument("project_combo_range_to_buckets requires combo range input");
    }
    if (combo_range.range.size() != combos.size()) {
        throw std::invalid_argument("project_combo_range_to_buckets requires combo-aligned inputs");
    }

    const auto bucket_count = bucket_count_for_street(tables, street);
    auto bucket_vector = make_bucket_vector(bucket_count);

    BucketProjectionResult result;
    result.input_mass = combo_range.range.sum();
    result.bucket_count = bucket_count;

    const bool use_mask = !combo_range.mask.empty();
    if (use_mask &&
        (combo_range.mask.kind != RangeVector::Kind::Combo ||
         combo_range.mask.size() != combo_range.range.size())) {
        throw std::invalid_argument("project_combo_range_to_buckets requires a combo-aligned mask");
    }

    for (std::size_t combo_idx = 0; combo_idx < combos.hands.size(); ++combo_idx) {
        if (use_mask && !combo_range.mask.allows(combo_idx)) {
            continue;
        }

        const auto weight = combo_range.range.weights[combo_idx];
        if (weight == 0.0) {
            continue;
        }

        const auto bucket = lookup_bucket(tables, combos.board, combos.hands[combo_idx], street);
        if (bucket < 0 || static_cast<std::size_t>(bucket) >= bucket_vector.weights.size()) {
            result.dropped_mass += weight;
            continue;
        }

        bucket_vector.weights[static_cast<std::size_t>(bucket)] += weight;
        result.projected_mass += weight;
    }

    // Keep bucketed and exact paths comparable by returning a canonical normalized bucket range.
    bucket_vector.normalize();
    result.bucket_range.range = std::move(bucket_vector);
    result.bucket_range.mask = full_bucket_mask(bucket_count);
    result.bucket_range.source_kind = combo_range.source_kind;
    result.bucket_range.context = combo_range.context;
    return result;
}

std::vector<double> combo_weights_to_bucket_weights(
    const std::vector<double>& combo_weights,
    const ComboIndex& combos,
    const AbstractionTables& tables,
    Street street) {
    CanonicalRange combo_range;
    combo_range.range.kind = RangeVector::Kind::Combo;
    combo_range.range.weights = combo_weights;
    combo_range.mask.kind = RangeVector::Kind::Combo;
    combo_range.mask.enabled.assign(combo_weights.size(), 1U);
    const auto projection = project_combo_range_to_buckets(combo_range, combos, tables, street);
    return projection.bucket_range.range.weights;
}

}  // namespace core
