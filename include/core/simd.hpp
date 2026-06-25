#pragma once

namespace core {

enum class SimdBackend {
    Scalar,
};

SimdBackend detect_simd_backend() noexcept;

}  // namespace core
