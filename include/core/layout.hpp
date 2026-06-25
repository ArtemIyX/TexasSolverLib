#pragma once

#include <cstddef>

namespace core {

struct LayoutIndex {
    std::size_t offset = 0;
    std::size_t stride = 0;
};

}  // namespace core
