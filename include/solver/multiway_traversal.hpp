#pragma once

#include "solver/multiway_cfr.hpp"
#include "solver/multiway_solver.hpp"

#include <cstdint>

namespace core {

// Allocation-free after the caller has prepared the external-sampling request.
// Recursive game traversal owns action-value estimation; this kernel owns only
// the mathematically shared CFR-to-worker-delta conversion.
class MultiwayExternalSamplingTraversal {
public:
    [[nodiscard]] static bool append_infoset_update(
        MultiwayWorkerDeltaStream& stream,
        MultiwayInfosetId infoset,
        std::uint32_t bucket,
        std::uint64_t trajectory_id,
        const MultiwayExternalSamplingRequest& request);
};

}  // namespace core
