#include "solver/multiway_checkpoint.hpp"

#include "core/atomic_publish.hpp"
#include "core/portable_binary.hpp"

#include <array>
#include <fstream>
#include <stdexcept>

namespace texas::solver::multiway {
namespace {

namespace portable = texas::core::portable;

// MultiwayModelIdentity gained Phase 0 semantic identity components.
constexpr std::array<char, 8> kMagic = {'M', 'W', 'B', 'P', '0', '0', '0', '5'};

void write_identity(std::ofstream& out, const MultiwayModelIdentity& identity) {
    using namespace texas::core::portable;
    visit_multiway_model_identity_fields(identity, [&](std::uint64_t field) {
        write_u64(out, field);
    });
}

MultiwayModelIdentity read_identity(std::ifstream& in) {
    using namespace texas::core::portable;
    MultiwayModelIdentity identity;
    bool valid = true;
    visit_multiway_model_identity_fields(identity, [&](std::uint64_t& field) {
        if (valid) valid = read_u64(in, field);
    });
    if (!valid) {
        throw std::runtime_error("multiway checkpoint is truncated");
    }
    return identity;
}

void write_action(std::ofstream& out, const MultiwayQuantizedRootAction& action) {
    using namespace texas::core::portable;
    write_u8(out, static_cast<std::uint8_t>(action.action.action));
    write_u32(out, action.action.action_index);
    write_i32(out, action.action.target_street_contribution);
    write_u64(out, action.action.action_menu_id);
    write_u16(out, action.probability);
}

MultiwayQuantizedRootAction read_action(std::ifstream& in) {
    using namespace texas::core::portable;
    MultiwayQuantizedRootAction action;
    std::uint8_t kind = 0;
    if (!read_u8(in, kind) || !read_u32(in, action.action.action_index) ||
        !read_i32(in, action.action.target_street_contribution) ||
        !read_u64(in, action.action.action_menu_id) || !read_u16(in, action.probability)) {
        throw std::runtime_error("multiway checkpoint is truncated");
    }
    action.action.action = static_cast<MultiwayAction>(kind);
    return action;
}

}  // namespace

void MultiwayRootPolicyArtifact::save_atomic(
    const std::filesystem::path& path,
    const MultiwayBlueprintSnapshot& snapshot) {
    snapshot.validate();
    const auto temp = path.string() + ".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("multiway checkpoint temporary file cannot be opened");
    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    write_identity(out, snapshot.identity);
    portable::write_u64(out, snapshot.public_state.value);
    portable::write_u64(out, snapshot.infoset.public_state.value);
    portable::write_i32(out, snapshot.infoset.seat);
    portable::write_u32(out, snapshot.bucket);
    portable::write_u64(out, snapshot.trajectories);
    portable::write_u8(out, static_cast<std::uint8_t>(snapshot.policy_kind));
    portable::write_u64(out, snapshot.training.batches);
    portable::write_u64(out, snapshot.training.trajectories);
    portable::write_u64(out, snapshot.training.deterministic_seed);
    portable::write_u64(out, snapshot.training.late_window_start_batch);
    portable::write_u64(out, snapshot.training.schedule_hash);
    portable::write_u64(out, snapshot.training.pruned_negative_regrets);
    portable::write_u8(out, snapshot.training.linear_iteration_weighting);
    portable::write_u8(out, snapshot.training.discounting_enabled);
    portable::write_u8(out, snapshot.training.negative_regret_pruning_enabled);
    portable::write_u8(out, snapshot.training.reserved);
    const auto count = static_cast<std::uint32_t>(snapshot.actions.size());
    portable::write_u32(out, count);
    for (const auto& action : snapshot.actions) write_action(out, action);
    out.close();
    texas::core::publish_atomic_replace(temp, path, "multiway checkpoint publish failed");
}

MultiwayBlueprintSnapshot MultiwayRootPolicyArtifact::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("multiway checkpoint cannot be opened");
    std::array<char, kMagic.size()> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kMagic) throw std::runtime_error("multiway checkpoint schema is invalid");
    MultiwayBlueprintSnapshot snapshot;
    snapshot.identity = read_identity(in);
    if (!portable::read_u64(in, snapshot.public_state.value) ||
        !portable::read_u64(in, snapshot.infoset.public_state.value) ||
        !portable::read_i32(in, snapshot.infoset.seat) ||
        !portable::read_u32(in, snapshot.bucket) ||
        !portable::read_u64(in, snapshot.trajectories)) {
        throw std::runtime_error("multiway checkpoint is truncated");
    }
    std::uint8_t policy_kind = 0;
    if (!portable::read_u8(in, policy_kind) ||
        !portable::read_u64(in, snapshot.training.batches) ||
        !portable::read_u64(in, snapshot.training.trajectories) ||
        !portable::read_u64(in, snapshot.training.deterministic_seed) ||
        !portable::read_u64(in, snapshot.training.late_window_start_batch) ||
        !portable::read_u64(in, snapshot.training.schedule_hash) ||
        !portable::read_u64(in, snapshot.training.pruned_negative_regrets) ||
        !portable::read_u8(in, snapshot.training.linear_iteration_weighting) ||
        !portable::read_u8(in, snapshot.training.discounting_enabled) ||
        !portable::read_u8(in, snapshot.training.negative_regret_pruning_enabled) ||
        !portable::read_u8(in, snapshot.training.reserved)) {
        throw std::runtime_error("multiway checkpoint is truncated");
    }
    snapshot.policy_kind = static_cast<MultiwayBlueprintPolicyKind>(policy_kind);
    std::uint32_t count = 0;
    if (!portable::read_u32(in, count)) throw std::runtime_error("multiway checkpoint is truncated");
    if (count == 0U || count > 64U) throw std::runtime_error("multiway checkpoint action count is invalid");
    snapshot.actions.resize(count);
    for (auto& action : snapshot.actions) action = read_action(in);
    if (in.peek() != std::char_traits<char>::eof()) {
        throw std::runtime_error("multiway checkpoint has trailing data");
    }
    snapshot.validate();
    return snapshot;
}

MultiwayBlueprintSnapshot MultiwayRootPolicyArtifact::load_for_resume(
    const std::filesystem::path& path,
    const MultiwayModelIdentity& expected_identity) {
    const auto snapshot = load(path);
    validate_resume_identity(snapshot, expected_identity);
    return snapshot;
}

void MultiwayRootPolicyArtifact::validate_resume_identity(
    const MultiwayBlueprintSnapshot& snapshot,
    const MultiwayModelIdentity& expected_identity) {
    snapshot.validate();
    expected_identity.validate();
    if (snapshot.identity != expected_identity) {
        throw std::invalid_argument("multiway checkpoint model identity does not match the requested resume");
    }
}

}  // namespace texas::solver::multiway
