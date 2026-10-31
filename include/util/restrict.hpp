#pragma once

// Internal-only aliasing contract for bulk kernels. Do not expose in public APIs.
#if defined(_MSC_VER)
	#define TEXAS_RESTRICT __restrict
#elif defined(__GNUC__) || defined(__clang__)
	#define TEXAS_RESTRICT __restrict__
#else
	#define TEXAS_RESTRICT
#endif
