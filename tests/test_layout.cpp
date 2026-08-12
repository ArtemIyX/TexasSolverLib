#include "util/layout.hpp"
#include "test_harness.hpp"

#include <limits>
#include <string>
#include <type_traits>

TEST_CASE(layout_intern_returns_stable_id) {
    texas::FlatInfosetStore store(3);
    const auto a = store.intern("foo", 2);
    const auto b = store.intern("foo", 2);
    const auto c = store.intern("bar", 3);

    EXPECT_EQ(a, b);
    EXPECT_TRUE(a != c);
    EXPECT_EQ(store.len(), 2U);
    EXPECT_TRUE(!store.is_empty());
}

TEST_CASE(layout_row_mut_yields_rw_access) {
    texas::FlatInfosetStore store(3);
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
    texas::FlatInfosetStore store(8);
    for (std::size_t i = 0; i < texas::BLOCK_SIZE + 1; ++i) {
        store.intern("k" + std::to_string(i), 8);
    }

    EXPECT_TRUE(store.regret_arena_size() >= 2 * texas::BLOCK_SIZE * 8);
    EXPECT_EQ(store.regret_arena_size() % (texas::BLOCK_SIZE * 8), 0U);
    EXPECT_EQ(store.regret_arena_size(), store.strategy_arena_size());
}

TEST_CASE(layout_offsets_use_size_t_and_remain_exact_across_twenty_widths) {
    static_assert(
        std::is_same_v<
            decltype(texas::RowMeta{}.offset),
            std::size_t>,
        "row offsets must use the host address width");

    for (std::size_t width = 1; width <= 20; ++width) {
        texas::FlatInfosetStore store(width);
        for (std::size_t row = 0; row <= texas::BLOCK_SIZE; ++row) {
            const auto id = store.intern(
                "w" + std::to_string(width) + "-r" + std::to_string(row),
                1U + row % width);
            EXPECT_EQ(
                store.meta().at(id.value).offset,
                row * width);
        }
        EXPECT_EQ(
            store.regret_arena_size() % (texas::BLOCK_SIZE * width),
            0U);
        EXPECT_EQ(
            store.regret_arena_size(),
            store.strategy_arena_size());
    }
}

TEST_CASE(layout_rejects_twenty_reused_key_action_shape_mismatches) {
    for (std::size_t actions = 1; actions <= 20; ++actions) {
        texas::FlatInfosetStore store(21);
        const auto id = store.intern("same-key", actions);
        EXPECT_THROW(
            store.intern("same-key", actions + 1U),
            std::invalid_argument);
        EXPECT_EQ(store.len(), 1U);
        EXPECT_EQ(store.row_size(id), actions);
        EXPECT_EQ(store.meta().at(id.value).offset, 0U);
    }
}

TEST_CASE(layout_rejects_unrepresentable_row_widths_and_action_counts) {
    EXPECT_THROW(texas::FlatInfosetStore(0), std::invalid_argument);
    EXPECT_THROW(
        texas::FlatInfosetStore(
            static_cast<std::size_t>(
                std::numeric_limits<std::uint16_t>::max()) + 1U),
        std::invalid_argument);

    texas::FlatInfosetStore store(20);
    EXPECT_THROW(store.intern("zero", 0), std::invalid_argument);
    EXPECT_THROW(store.intern("too-wide", 21), std::invalid_argument);
    EXPECT_TRUE(store.is_empty());
}


