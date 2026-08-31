#pragma once

#include "solver/multiway/abstraction/multiway_bucket_artifact.hpp"

#include <cstddef>
#include <cstdint>
#include <array>
#include <stdexcept>

namespace texas::solver::multiway {

class MultiwayBucketBoardCatalog {
public:
    explicit MultiwayBucketBoardCatalog(core::Street street);

    [[nodiscard]] core::Street street() const noexcept { return street_; }
    [[nodiscard]] std::uint64_t size() const noexcept;
    [[nodiscard]] std::uint64_t fingerprint() const noexcept;

    // Calls callback once for each board in [begin_index, end_index).
    // The callback receives a temporary request valid for the duration of the call.
    template <typename Callback>
    void for_each(std::uint64_t begin_index, std::uint64_t end_index, Callback&& callback) const {
        enumerate(begin_index, end_index, callback);
    }

    template <typename Callback>
    void for_each_fixed_board(std::uint64_t begin_index, std::uint64_t end_index, Callback&& callback) const {
        enumerate_fixed(begin_index, end_index, callback);
    }

private:
    core::Street street_;

    template <typename Callback>
    void enumerate(std::uint64_t begin_index, std::uint64_t end_index, Callback&& callback) const {
        const std::size_t k = street_ == core::Street::Flop ? 3U : street_ == core::Street::Turn ? 4U : 5U;
        if (begin_index > end_index || end_index > size()) throw std::out_of_range("bucket catalog range is invalid");
        auto choose = [](std::uint8_t n, std::size_t count) {
            std::uint64_t result = 1U;
            for (std::size_t i = 1U; i <= count; ++i) result = result * (n - count + i) / i;
            return result;
        };
        std::array<std::uint8_t, 5U> board{};
        for (std::uint64_t index = begin_index; index < end_index; ++index) {
            auto rank = index;
            std::uint8_t next = 0U;
            for (std::size_t position = 0U; position < k; ++position) {
                for (; next < 52U; ++next) {
                    const auto remaining = choose(static_cast<std::uint8_t>(51U - next), k - position - 1U);
                    if (rank < remaining) { board[position] = next++; break; }
                    rank -= remaining;
                }
            }
            MultiwayBucketBoardRequest request;
            request.street = street_;
            request.canonical_board.assign(board.begin(), board.begin() + static_cast<std::ptrdiff_t>(k));
            callback(request);
        }
    }

    template <typename Callback>
    void enumerate_fixed(std::uint64_t begin_index, std::uint64_t end_index, Callback&& callback) const {
        const std::size_t k = street_ == core::Street::Flop ? 3U : street_ == core::Street::Turn ? 4U : 5U;
        if (begin_index > end_index || end_index > size()) throw std::out_of_range("bucket catalog range is invalid");
        const auto choose = [](std::uint8_t n, std::size_t count) {
            std::uint64_t result = 1U;
            for (std::size_t i = 1U; i <= count; ++i) result = result * (n - count + i) / i;
            return result;
        };
        std::array<std::uint8_t, 5U> board{};
        auto rank = begin_index;
        std::uint8_t next = 0U;
        for (std::size_t position = 0U; position < k; ++position) {
            for (; next < 52U; ++next) {
                const auto remaining = choose(static_cast<std::uint8_t>(51U - next), k - position - 1U);
                if (rank < remaining) { board[position] = next++; break; }
                rank -= remaining;
            }
        }
        for (std::uint64_t index = begin_index; index < end_index; ++index) {
            callback(board);
            if (index + 1U == end_index) break;
            for (std::size_t position = k; position-- > 0U;) {
                if (board[position] < static_cast<std::uint8_t>(52U - (k - position))) {
                    ++board[position];
                    for (std::size_t reset = position + 1U; reset < k; ++reset) {
                        board[reset] = static_cast<std::uint8_t>(board[reset - 1U] + 1U);
                    }
                    break;
                }
            }
        }
    }
};

[[nodiscard]] std::uint64_t multiway_bucket_board_count(core::Street street);

}  // namespace texas::solver::multiway
