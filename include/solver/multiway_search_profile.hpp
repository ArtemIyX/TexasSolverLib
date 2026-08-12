#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>

namespace core {

enum class MultiwaySearchProfileMode : std::uint8_t {
    Disabled = 0U,
    Checkpoints = 1U,
};

enum class MultiwaySearchProfileStage : std::uint8_t {
    PrivateDealSampling = 0U,
    PublicChanceSampling = 1U,
    ActionMenuGeneration = 2U,
    PublicGraphAdmission = 3U,
    RowLookup = 4U,
    RegretMatching = 5U,
    TerminalSettlement = 6U,
    ContinuationLeaf = 7U,
    DeltaMerge = 8U,
    RootExport = 9U,
    Count = 10U,
};

inline constexpr std::size_t MULTIWAY_SEARCH_PROFILE_STAGE_COUNT =
    static_cast<std::size_t>(MultiwaySearchProfileStage::Count);

struct MultiwaySearchProfileCheckpoint {
    std::uint64_t elapsed_nanoseconds = 0U;
    std::uint64_t calls = 0U;
};

struct MultiwaySearchProfileSnapshot {
    MultiwaySearchProfileMode mode = MultiwaySearchProfileMode::Disabled;
    std::array<MultiwaySearchProfileCheckpoint, MULTIWAY_SEARCH_PROFILE_STAGE_COUNT> checkpoints{};

    [[nodiscard]] bool profiled() const noexcept {
        return mode == MultiwaySearchProfileMode::Checkpoints;
    }

    [[nodiscard]] const MultiwaySearchProfileCheckpoint& checkpoint(
        MultiwaySearchProfileStage stage) const noexcept {
        return checkpoints[static_cast<std::size_t>(stage)];
    }
};

struct MultiwaySearchProfileRankingEntry {
    MultiwaySearchProfileStage stage = MultiwaySearchProfileStage::PrivateDealSampling;
    std::uint64_t elapsed_nanoseconds = 0U;
};

class MultiwaySearchProfile {
public:
    explicit MultiwaySearchProfile(
        MultiwaySearchProfileMode mode = MultiwaySearchProfileMode::Disabled) noexcept
        : mode_(mode) {}

    void reset(MultiwaySearchProfileMode mode) noexcept {
        mode_ = mode;
        checkpoints_ = {};
    }

    [[nodiscard]] bool enabled() const noexcept {
        return mode_ == MultiwaySearchProfileMode::Checkpoints;
    }

    void add(
        MultiwaySearchProfileStage stage,
        std::uint64_t elapsed_nanoseconds,
        std::uint64_t calls = 1U) noexcept {
        if (!enabled()) return;
        auto& checkpoint = checkpoints_[static_cast<std::size_t>(stage)];
        checkpoint.elapsed_nanoseconds += elapsed_nanoseconds;
        checkpoint.calls += calls;
    }

    void merge(const MultiwaySearchProfileSnapshot& snapshot) noexcept {
        if (!snapshot.profiled()) return;
        if (!enabled()) mode_ = MultiwaySearchProfileMode::Checkpoints;
        for (std::size_t index = 0U; index < checkpoints_.size(); ++index) {
            checkpoints_[index].elapsed_nanoseconds += snapshot.checkpoints[index].elapsed_nanoseconds;
            checkpoints_[index].calls += snapshot.checkpoints[index].calls;
        }
    }

    [[nodiscard]] MultiwaySearchProfileSnapshot snapshot() const noexcept {
        return {mode_, checkpoints_};
    }

private:
    MultiwaySearchProfileMode mode_ = MultiwaySearchProfileMode::Disabled;
    std::array<MultiwaySearchProfileCheckpoint, MULTIWAY_SEARCH_PROFILE_STAGE_COUNT> checkpoints_{};
};

class MultiwaySearchProfileScope {
public:
    MultiwaySearchProfileScope(
        MultiwaySearchProfile* profile,
        MultiwaySearchProfileStage stage) noexcept
        : profile_(profile),
          stage_(stage),
          started_(profile_ != nullptr && profile_->enabled()
              ? std::chrono::steady_clock::now()
              : std::chrono::steady_clock::time_point{}) {}

    ~MultiwaySearchProfileScope() {
        if (profile_ == nullptr || !profile_->enabled()) return;
        const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - started_).count();
        profile_->add(stage_, elapsed > 0 ? static_cast<std::uint64_t>(elapsed) : 0U);
    }

    MultiwaySearchProfileScope(const MultiwaySearchProfileScope&) = delete;
    MultiwaySearchProfileScope& operator=(const MultiwaySearchProfileScope&) = delete;

private:
    MultiwaySearchProfile* profile_ = nullptr;
    MultiwaySearchProfileStage stage_ = MultiwaySearchProfileStage::PrivateDealSampling;
    std::chrono::steady_clock::time_point started_{};
};

[[nodiscard]] inline std::array<MultiwaySearchProfileRankingEntry, MULTIWAY_SEARCH_PROFILE_STAGE_COUNT>
rank_multiway_search_profile(const MultiwaySearchProfileSnapshot& snapshot) noexcept {
    std::array<MultiwaySearchProfileRankingEntry, MULTIWAY_SEARCH_PROFILE_STAGE_COUNT> result{};
    for (std::size_t index = 0U; index < result.size(); ++index) {
        result[index] = {
            static_cast<MultiwaySearchProfileStage>(index),
            snapshot.checkpoints[index].elapsed_nanoseconds,
        };
    }
    std::stable_sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return left.elapsed_nanoseconds > right.elapsed_nanoseconds;
    });
    return result;
}

}  // namespace core
