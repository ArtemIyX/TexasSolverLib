#include "solver/multiway_checkpoint.hpp"

#include <array>
#include <fstream>
#include <stdexcept>

namespace core {
namespace {

constexpr std::array<char, 8> kMagic = {'M', 'W', 'B', 'P', '0', '0', '0', '1'};

template <class T>
void write_value(std::ofstream& out, const T& value) {
    out.write(reinterpret_cast<const char*>(&value), sizeof(value));
    if (!out) throw std::runtime_error("multiway checkpoint write failed");
}

template <class T>
T read_value(std::ifstream& in) {
    T value{};
    in.read(reinterpret_cast<char*>(&value), sizeof(value));
    if (!in) throw std::runtime_error("multiway checkpoint is truncated");
    return value;
}

}  // namespace

void MultiwayCheckpoint::save_atomic(
    const std::filesystem::path& path,
    const MultiwayBlueprintSnapshot& snapshot) {
    snapshot.validate();
    const auto temp = path.string() + ".tmp";
    std::ofstream out(temp, std::ios::binary | std::ios::trunc);
    if (!out) throw std::runtime_error("multiway checkpoint temporary file cannot be opened");
    out.write(kMagic.data(), static_cast<std::streamsize>(kMagic.size()));
    write_value(out, snapshot.identity);
    write_value(out, snapshot.public_state);
    write_value(out, snapshot.infoset);
    write_value(out, snapshot.bucket);
    write_value(out, snapshot.trajectories);
    const auto count = static_cast<std::uint32_t>(snapshot.actions.size());
    write_value(out, count);
    for (const auto& action : snapshot.actions) write_value(out, action);
    out.close();
    std::error_code error;
    std::filesystem::rename(temp, path, error);
    if (error) {
        std::filesystem::remove(path, error);
        error.clear();
        std::filesystem::rename(temp, path, error);
        if (error) throw std::runtime_error("multiway checkpoint publish failed");
    }
}

MultiwayBlueprintSnapshot MultiwayCheckpoint::load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("multiway checkpoint cannot be opened");
    std::array<char, kMagic.size()> magic{};
    in.read(magic.data(), static_cast<std::streamsize>(magic.size()));
    if (!in || magic != kMagic) throw std::runtime_error("multiway checkpoint schema is invalid");
    MultiwayBlueprintSnapshot snapshot;
    snapshot.identity = read_value<MultiwayModelIdentity>(in);
    snapshot.public_state = read_value<MultiwayPublicStateId>(in);
    snapshot.infoset = read_value<MultiwayInfosetId>(in);
    snapshot.bucket = read_value<std::uint32_t>(in);
    snapshot.trajectories = read_value<std::uint64_t>(in);
    const auto count = read_value<std::uint32_t>(in);
    if (count == 0U || count > 64U) throw std::runtime_error("multiway checkpoint action count is invalid");
    snapshot.actions.resize(count);
    for (auto& action : snapshot.actions) action = read_value<MultiwayQuantizedRootAction>(in);
    snapshot.validate();
    return snapshot;
}

}  // namespace core
