#pragma once

#include "rank_config.hpp"
#include "rank_result.hpp"

#include <iosfwd>

namespace lr::rank {

class RankJob {
public:
	RankJob(RankConfig config, std::ostream* progress);

	RankResult Run();

private:
	RankConfig Config_;
	std::ostream* Progress_;
};

} // namespace lr::rank
