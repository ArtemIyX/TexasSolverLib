#include <cstddef>
#include <cstdint>

#pragma pack(push, 1)
#include "solver/multiway/abstraction/multiway_model_identity_types.hpp"
#include "games/multiway_rake_types.hpp"

struct PackingRestorationProbe
{
	std::uint8_t first;
	std::uint64_t second;
};
#pragma pack(pop)

static_assert(alignof(texas::solver::multiway::MultiwayModelIdentity) == alignof(std::uint64_t),
	"model identity must use its natural 64-bit alignment under pack 8");
static_assert(offsetof(PackingRestorationProbe, second) == 1,
	"the identity header must restore its caller's packing");
static_assert(sizeof(texas::solver::multiway::MultiwayModelIdentity) == 13 * sizeof(std::uint64_t),
	"the identity must retain its contiguous field layout");
static_assert(alignof(texas::games::multiway::MultiwayRakePolicy) >= alignof(int),
	"rake policy members must retain natural int alignment");
static_assert(offsetof(texas::games::multiway::MultiwayRakePolicy, cap) % alignof(int) == 0,
	"the rake cap must be naturally aligned");
