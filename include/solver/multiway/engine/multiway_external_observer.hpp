#pragma once

#include <cstdint>

namespace texas::solver::multiway
{
	// Called after a component run is published, before aggregate commit.
	// An exception propagates after component publication; an owning aggregate
	// transaction must roll back. The observer and context must outlive its use.
	using MultiwayExternalPublicationObserver = void (*)(void*, std::uint32_t,
		std::uint64_t, std::uint64_t, std::uint64_t);
} // namespace texas::solver::multiway
