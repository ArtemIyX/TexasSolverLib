#include "solver/multiway_traversal.hpp"

#include <stdexcept>

namespace core {

bool MultiwayExternalSamplingTraversal::append_infoset_update(
    MultiwayWorkerDeltaStream& stream,
    MultiwayInfosetId infoset,
    std::uint32_t bucket,
    std::uint64_t trajectory_id,
    const MultiwayExternalSamplingRequest& request) {
    if (infoset.public_state.value == 0U || infoset.seat != request.traverser) {
        throw std::invalid_argument("multiway traversal update infoset must identify the traverser");
    }
    const auto update = make_multiway_external_sampling_cfr_update(request);
    if (update.regret_deltas.size() != update.strategy_deltas.size()) {
        throw std::logic_error("multiway traversal produced mismatched action deltas");
    }
    if (stream.capacity() - stream.size() < update.regret_deltas.size()) return false;
    for (std::size_t action = 0; action < update.regret_deltas.size(); ++action) {
        if (!stream.try_append({
                infoset,
                bucket,
                static_cast<std::uint8_t>(action),
                update.regret_deltas[action],
                update.strategy_deltas[action],
                trajectory_id,
            })) {
            throw std::logic_error("multiway traversal delta capacity changed during append");
        }
    }
    return true;
}

}  // namespace core
