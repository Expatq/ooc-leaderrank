#pragma once

#include <cstdint>

namespace lr::rank {

struct RankResult {
	uint32_t iterations;
	bool converged;
	double finalDeltaPerVertex;
	double groundScore;
	uint32_t rankFileSide;
};

} // namespace lr::rank
