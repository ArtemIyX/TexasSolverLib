#pragma once

#include <cstddef>
#include <vector>

namespace core {

class Arena {
public:
    void reset() noexcept;
    std::size_t mark() const noexcept;
    void release(std::size_t mark) noexcept;

    template <class T>
    T* allocate(std::size_t count = 1);

private:
    std::vector<std::byte> buffer_;
    std::size_t offset_ = 0;
};

}  // namespace core
