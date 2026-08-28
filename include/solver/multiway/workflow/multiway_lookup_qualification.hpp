#pragma once

#include <cstdint>
#include <filesystem>

namespace texas::solver::multiway {

struct MultiwayLookupQualificationReport {
    std::uint64_t lookup_hits = 0U;
    std::uint64_t missing_infosets = 0U;
    std::uint64_t missing_buckets = 0U;
    std::uint64_t action_menu_mismatches = 0U;
    std::uint64_t replay_fingerprint = 0U;

    [[nodiscard]] bool passed() const noexcept {
        return missing_infosets == 0U && missing_buckets == 0U && action_menu_mismatches == 0U;
    }
};

void save_multiway_lookup_qualification_report_atomic(
    const std::filesystem::path& path,
    const MultiwayLookupQualificationReport& report);

}  // namespace texas::solver::multiway
