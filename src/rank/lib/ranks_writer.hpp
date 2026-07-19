#pragma once

#include "rank_result.hpp"

#include <common/thread/thread_pool.hpp>
#include <grid/layout.hpp>
#include <grid/meta.hpp>

#include <filesystem>

namespace lr::rank {

class RanksCsvWriter {
public:
	RanksCsvWriter(const grid::WorkdirRoot& workdir, const grid::Meta& meta, common::ThreadPool* pool);

	void Write(const RankResult& result, const std::filesystem::path& outPath) const;

private:
	grid::WorkdirRoot Workdir_;
	grid::Meta Meta_;
	common::ThreadPool* Pool_;
};

} // namespace lr::rank
