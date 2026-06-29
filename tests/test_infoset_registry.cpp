#include "util/infoset_registry.hpp"
#include "test_harness.hpp"

#include <stdexcept>

TEST_CASE(infoset_registry_returns_stable_ids) {
    core::InfosetRegistry registry;
    const auto a = registry.intern("foo", 2);
    const auto b = registry.intern("foo", 2);
    const auto c = registry.intern("bar", 3);

    EXPECT_EQ(a, b);
    EXPECT_TRUE(a != c);
    EXPECT_EQ(registry.size(), 2U);
    EXPECT_TRUE(!registry.empty());
}

TEST_CASE(infoset_registry_supports_round_trip_lookup) {
    core::InfosetRegistry registry;
    const auto foo = registry.intern("foo", 2);
    const auto bar = registry.intern("bar", 3);

    const auto found = registry.find("bar");
    EXPECT_TRUE(found.has_value());
    EXPECT_EQ(*found, bar);
    EXPECT_EQ(registry.key_for(foo), "foo");
    EXPECT_EQ(registry.key_for(bar), "bar");
    EXPECT_EQ(registry.meta_for(foo).action_count, 2U);
    EXPECT_EQ(registry.meta_for(bar).action_count, 3U);
}

TEST_CASE(infoset_registry_clear_resets_mapping) {
    core::InfosetRegistry registry;
    (void)registry.intern("foo", 2);
    registry.clear();

    EXPECT_TRUE(registry.empty());
    EXPECT_EQ(registry.size(), 0U);
    EXPECT_TRUE(!registry.find("foo").has_value());
}

TEST_CASE(infoset_registry_rejects_invalid_ids) {
    core::InfosetRegistry registry;
    (void)registry.intern("foo", 2);

    EXPECT_THROW(static_cast<void>(registry.key_for(core::InfosetId{99})), std::out_of_range);
}

TEST_CASE(infoset_registry_rejects_mismatched_action_count) {
    core::InfosetRegistry registry;
    (void)registry.intern("foo", 2);

    EXPECT_THROW(static_cast<void>(registry.intern("foo", 3)), std::logic_error);
}
