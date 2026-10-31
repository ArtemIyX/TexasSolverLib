#include "multiway_external_storage_detail.hpp"

namespace texas::solver::multiway
{
	void MultiwayExternalStorageConfig::validate(std::size_t payload_bytes) const
	{
		if (directory.empty() || payload_bytes == 0U || payload_bytes > std::numeric_limits<std::uint32_t>::max() || cache_bytes < payload_bytes + 8U || maximum_wal_bytes < payload_bytes + 56U || segment_target_bytes < payload_bytes + 8U || compaction_fan_in < 2U || compaction_scratch_bytes == 0U || disk_reserve_bytes == 0U)
			throw std::invalid_argument("external storage requires a directory and positive bounded resources");
		if (disk_amplification_limit != 0U && disk_accounting_root.empty())
			throw std::invalid_argument("external disk amplification accounting root is missing");
		if (aggregate_transaction_covers_append_recovery && (disk_amplification_limit == 0U || !synchronous_durability))
			throw std::invalid_argument("aggregate append recovery requires a synchronous bounded transaction");
		constexpr std::uint64_t kCompactionCursorBytes = 2048U;
		const auto per_cursor = checked_add(payload_bytes, kCompactionCursorBytes);
		if (compaction_fan_in != 0U && per_cursor > std::numeric_limits<std::uint64_t>::max() / compaction_fan_in)
			throw std::invalid_argument("external storage compaction scratch bound overflows");
		const auto minimum_scratch = static_cast<std::uint64_t>(compaction_fan_in) * per_cursor;
		if (compaction_scratch_bytes < minimum_scratch)
			throw std::invalid_argument("external storage compaction scratch is below its bounded fan-in requirement");
	}
} // namespace texas::solver::multiway
