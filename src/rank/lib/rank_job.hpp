#pragma once

#include "engine.hpp"

#include <cstdint>
#include <string>

namespace lr::rank {

struct RankConfig {
	std::string workdir;
	std::string outPath;
	uint64_t budgetBytes = 0;
	double eps = 0.0;
	uint32_t maxIterations = 0;
};

class RankJob {
public:
	explicit RankJob(RankConfig config);

	RankResult Run();

private:
	RankConfig Config_;
};

} // namespace lr::rank
