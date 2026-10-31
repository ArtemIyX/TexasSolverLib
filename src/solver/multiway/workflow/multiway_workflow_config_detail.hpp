#pragma once

#include <cstdint>

namespace texas::solver::multiway::workflow_config_detail
{
	inline constexpr std::uint8_t kRegretCapacity = 1U;
	inline constexpr std::uint8_t kContinuationCapacity = 2U;
	inline constexpr std::uint8_t kDiscoveryPublicCapacity = 4U;
	inline constexpr std::uint8_t kDiscoveryRowsCapacity = 8U;
}
