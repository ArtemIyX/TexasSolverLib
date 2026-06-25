#include "core/arena.hpp"
#include "test_harness.hpp"

TEST_CASE(arena_mark_release_reuses_storage) {
    core::Arena arena;
    auto* first = arena.allocate<int>(4);
    for (int i = 0; i < 4; ++i) {
        first[i] = 10 + i;
    }

    const auto mark = arena.mark();
    auto* second = arena.allocate<int>(8);
    second[0] = 42;
    EXPECT_TRUE(arena.mark() > mark);

    arena.release(mark);
    EXPECT_EQ(arena.mark(), mark);

    auto* third = arena.allocate<int>(8);
    EXPECT_EQ(third, second);
}

TEST_CASE(arena_reset_rewinds_to_zero) {
    core::Arena arena;
    (void)arena.allocate<double>(3);
    EXPECT_TRUE(arena.mark() > 0);
    arena.reset();
    EXPECT_EQ(arena.mark(), 0U);
}
