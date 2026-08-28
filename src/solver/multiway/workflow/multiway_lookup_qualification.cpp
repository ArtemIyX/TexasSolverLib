#include "solver/multiway/workflow/multiway_lookup_qualification.hpp"

#include "core/atomic_publish.hpp"

#include <fstream>
#include <stdexcept>

namespace texas::solver::multiway {
void save_multiway_lookup_qualification_report_atomic(
    const std::filesystem::path& path, const MultiwayLookupQualificationReport& report) {
    const auto temporary = path.string() + ".tmp";
    std::ofstream output(temporary, std::ios::trunc);
    if (!output) throw std::runtime_error("cannot open lookup qualification report");
    output << "{\n  \"lookup_hits\": " << report.lookup_hits
           << ",\n  \"missing_infosets\": " << report.missing_infosets
           << ",\n  \"missing_buckets\": " << report.missing_buckets
           << ",\n  \"action_menu_mismatches\": " << report.action_menu_mismatches
           << ",\n  \"replay_fingerprint\": " << report.replay_fingerprint
           << ",\n  \"passed\": " << (report.passed() ? "true" : "false") << "\n}\n";
    output.close();
    core::publish_atomic_replace(temporary, path, "cannot publish lookup qualification report");
}
}  // namespace texas::solver::multiway
