#include "core/arena.hpp"

namespace core {

void Arena::reset() noexcept {
    offset_ = 0;
}

std::size_t Arena::mark() const noexcept {
    return offset_;
}

void Arena::release(std::size_t mark_value) noexcept {
    offset_ = mark_value <= buffer_.size() ? mark_value : buffer_.size();
}

}  // namespace core
