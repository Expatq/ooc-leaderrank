#pragma once

#include <cstdint>
#include <string>

namespace lr::rank {

struct RankConfig {
	std::string workdir;
	std::string outPath;
	uint64_t budgetBytes = 0;
	double eps = 0.0;
	uint32_t maxIterations = 0;
	uint32_t threads = 0;
};

} // namespace lr::rank
