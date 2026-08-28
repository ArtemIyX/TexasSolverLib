#include "solver/multiway/abstraction/multiway_bucket_catalog.hpp"

#include "core/fingerprint.hpp"

#include <array>
#include <stdexcept>

namespace texas::solver::multiway {
namespace {

std::size_t board_size(core::Street street) {
    switch (street) {
        case core::Street::Flop: return 3U;
        case core::Street::Turn: return 4U;
        case core::Street::River: return 5U;
        default: throw std::invalid_argument("bucket catalog requires a postflop street");
    }
}

std::uint64_t choose(std::uint8_t n, std::size_t k) {
    std::uint64_t result = 1U;
    for (std::size_t i = 1U; i <= k; ++i) result = result * (n - k + i) / i;
    return result;
}

}  // namespace

std::uint64_t multiway_bucket_board_count(core::Street street) { return choose(52U, board_size(street)); }

MultiwayBucketBoardCatalog::MultiwayBucketBoardCatalog(core::Street street) : street_(street) { board_size(street); }

std::uint64_t MultiwayBucketBoardCatalog::size() const noexcept {
    switch (street_) {
        case core::Street::Flop: return 22100U;
        case core::Street::Turn: return 270725U;
        case core::Street::River: return 2598960U;
        default: return 0U;
    }
}

std::uint64_t MultiwayBucketBoardCatalog::fingerprint() const noexcept {
    auto hash = core::fingerprint::FNV1A_OFFSET;
    core::fingerprint::append_u8(hash, static_cast<std::uint8_t>(street_));
    core::fingerprint::append_u64(hash, size());
    return core::fingerprint::finish(hash);
}

}  // namespace texas::solver::multiway
