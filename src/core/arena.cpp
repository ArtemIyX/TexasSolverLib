#include "core/arena.hpp"

namespace core {

void Arena::reset() noexcept {
    release(0);
}

std::size_t Arena::mark() const noexcept {
    return total_used_;
}

void Arena::release(std::size_t mark_value) noexcept {
    if (mark_value >= total_used_) {
        return;
    }

    std::size_t remaining = mark_value;
    std::size_t active_block = 0;
    for (; active_block < blocks_.size(); ++active_block) {
        auto& block = blocks_[active_block];
        if (remaining <= block.used) {
            block.used = remaining;
            ++active_block;
            break;
        }
        remaining -= block.used;
    }

    for (std::size_t i = active_block; i < blocks_.size(); ++i) {
        blocks_[i].used = 0;
    }

    current_block_ = blocks_.empty() ? 0 : std::min(active_block == 0 ? std::size_t{0} : active_block - 1, blocks_.size() - 1);
    total_used_ = mark_value;
}

Arena::Block& Arena::acquire_block(std::size_t min_bytes) {
    if (blocks_.empty()) {
        blocks_.push_back(Block{std::vector<std::byte>(std::max(kDefaultBlockSize, min_bytes)), 0});
        current_block_ = 0;
        return blocks_.back();
    }

    for (std::size_t i = current_block_; i < blocks_.size(); ++i) {
        auto& block = blocks_[i];
        if ((block.storage.size() - block.used) >= min_bytes || (block.used == 0 && block.storage.size() >= min_bytes)) {
            current_block_ = i;
            return block;
        }
    }

    blocks_.push_back(Block{std::vector<std::byte>(std::max(kDefaultBlockSize, min_bytes)), 0});
    current_block_ = blocks_.size() - 1;
    return blocks_.back();
}

}  // namespace core


