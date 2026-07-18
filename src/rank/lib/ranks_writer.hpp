#pragma once

#include "engine.hpp"

#include <grid/layout.hpp>
#include <grid/meta.hpp>

#include <filesystem>

namespace lr::rank {

class RanksCsvWriter {
public:
	RanksCsvWriter(const grid::WorkdirRoot& workdir, const grid::Meta& meta);

	void Write(const RankResult& result, const std::filesystem::path& outPath) const;

private:
	grid::WorkdirRoot Workdir_;
	grid::Meta Meta_;
};

} // namespace lr::rank
