#include "solver/multiway_resolver_policy.hpp"
#include "test_harness.hpp"

#include <limits>
#include <vector>

namespace {

texas::MultiwayResolverActionProbability policy_entry(double probability) {
    texas::MultiwayResolverActionProbability entry;
    entry.probability = probability;
    return entry;
}

}  // namespace

TEST_CASE(multiway_resolver_policy_normalizes_finite_nonnegative_mass) {
    std::vector<texas::MultiwayResolverActionProbability> policy = {
        policy_entry(2.0), policy_entry(3.0), policy_entry(5.0),
    };

    EXPECT_TRUE(texas::normalize_multiway_resolver_policy(&policy));
    EXPECT_NEAR(policy[0].probability, 0.2, 1e-12);
    EXPECT_NEAR(policy[1].probability, 0.3, 1e-12);
    EXPECT_NEAR(policy[2].probability, 0.5, 1e-12);
}

TEST_CASE(multiway_resolver_policy_rejects_invalid_mass) {
    std::vector<texas::MultiwayResolverActionProbability> empty;
    std::vector<texas::MultiwayResolverActionProbability> negative = {policy_entry(-1.0)};
    std::vector<texas::MultiwayResolverActionProbability> non_finite = {
        policy_entry(std::numeric_limits<double>::infinity())};
    std::vector<texas::MultiwayResolverActionProbability> zero = {policy_entry(0.0)};

    EXPECT_TRUE(!texas::normalize_multiway_resolver_policy(nullptr));
    EXPECT_TRUE(!texas::normalize_multiway_resolver_policy(&empty));
    EXPECT_TRUE(!texas::normalize_multiway_resolver_policy(&negative));
    EXPECT_TRUE(!texas::normalize_multiway_resolver_policy(&non_finite));
    EXPECT_TRUE(!texas::normalize_multiway_resolver_policy(&zero));
}
