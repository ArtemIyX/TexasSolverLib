#include "core/layout.hpp"
#include "test_harness.hpp"

TEST_CASE(layout_intern_returns_stable_id) {
    core::FlatInfosetStore store(3);
    const auto a = store.intern("foo", 2);
    const auto b = store.intern("foo", 2);
    const auto c = store.intern("bar", 3);

    EXPECT_EQ(a, b);
    EXPECT_TRUE(a != c);
    EXPECT_EQ(store.len(), 2U);
    EXPECT_TRUE(!store.is_empty());
}

TEST_CASE(layout_row_mut_yields_rw_access) {
    core::FlatInfosetStore store(3);
    const auto id = store.intern("foo", 3);

    auto [regret, strategy, meta] = store.row_mut(id);
    regret[0] = 1.0;
    strategy[1] = 2.0;
    meta->last_discount_iter = 42;

    EXPECT_EQ(store.regret(id)[0], 1.0);
    EXPECT_EQ(store.strategy_sum(id)[1], 2.0);
    EXPECT_EQ(store.meta().at(id.value).last_discount_iter, 42U);
    EXPECT_EQ(store.row_size(id), 3U);
}

TEST_CASE(layout_arena_grows_in_block_increments) {
    core::FlatInfosetStore store(8);
    for (std::size_t i = 0; i < core::BLOCK_SIZE + 1; ++i) {
        store.intern("k" + std::to_string(i), 8);
    }

    EXPECT_TRUE(store.regret_arena_size() >= 2 * core::BLOCK_SIZE * 8);
    EXPECT_EQ(store.regret_arena_size() % (core::BLOCK_SIZE * 8), 0U);
    EXPECT_EQ(store.regret_arena_size(), store.strategy_arena_size());
}
