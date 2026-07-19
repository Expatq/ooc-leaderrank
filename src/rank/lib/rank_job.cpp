#include "rank_job.hpp"

#include "engine.hpp"
#include "ranks_writer.hpp"

#include <common/thread/thread_pool.hpp>
#include <grid/layout.hpp>
#include <grid/meta.hpp>

#include <format>
#include <stdexcept>
#include <utility>

namespace lr::rank {

RankJob::RankJob(RankConfig config, std::ostream* progress)
    : Config_(std::move(config)), Progress_(progress) {}

RankResult RankJob::Run() {
	const grid::WorkdirRoot workdir(Config_.workdir);
	const grid::Meta meta = grid::Meta::Load(workdir);
	const uint32_t threads = Config_.threads == 0 ? meta.threadsPlanned : Config_.threads;
	if (threads > meta.threadsPlanned) {
		throw std::runtime_error(std::format("requested {} threads, but the workdir was planned for at most {}", threads, meta.threadsPlanned));
	}
	common::ThreadPool pool(threads);
	RankResult result{};
	{
		Engine engine(workdir, meta, Config_, &pool, Progress_);
		result = engine.Run();
	}
	const RanksCsvWriter writer(workdir, meta, &pool);
	writer.Write(result, Config_.outPath);
	return result;
}

} // namespace lr::rank
