#include "rank_job.hpp"

#include "engine.hpp"
#include "ranks_writer.hpp"

#include <grid/layout.hpp>
#include <grid/meta.hpp>

#include <utility>

namespace lr::rank {

RankJob::RankJob(RankConfig config, std::ostream* progress)
    : Config_(std::move(config)), Progress_(progress) {}

RankResult RankJob::Run() {
	const grid::WorkdirRoot workdir(Config_.workdir);
	grid::Meta meta;
	RankResult result{};
	{
		Engine engine(workdir, Config_, Progress_);
		result = engine.Run();
		meta = engine.GetMeta();
	}
	const RanksCsvWriter writer(workdir, meta);
	writer.Write(result, Config_.outPath);
	return result;
}

} // namespace lr::rank
