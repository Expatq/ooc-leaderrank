#include "rank_job.hpp"

#include "ranks_writer.hpp"

#include <grid/layout.hpp>
#include <grid/meta.hpp>

#include <utility>

namespace lr::rank {

RankJob::RankJob(RankConfig config) : Config_(std::move(config)) {}

RankResult RankJob::Run() {
	const grid::WorkdirRoot workdir = grid::WorkdirLayout(Config_.workdir).Root();
	grid::Meta meta;
	RankResult result{};
	{
		Engine engine(workdir, Config_.budgetBytes, Config_.eps, Config_.maxIterations);
		result = engine.Run();
		meta = engine.GetMeta();
	}
	const RanksCsvWriter writer(workdir, meta);
	writer.Write(result, Config_.outPath);
	return result;
}

} // namespace lr::rank
