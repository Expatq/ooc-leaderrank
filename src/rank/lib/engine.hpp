#pragma once

#include <common/io/aligned_io.hpp>
#include <grid/block_index.hpp>
#include <grid/block_reader.hpp>
#include <grid/layout.hpp>
#include <grid/meta.hpp>
#include <grid/partition.hpp>

#include <cstdint>
#include <memory>
#include <vector>

namespace lr::rank {

struct RankResult {
	uint32_t iterations;
	bool converged;
	double finalDeltaPerVertex;
	double groundScore;
	uint32_t rankFileSide;
};

class Engine {
public:
	Engine(const grid::WorkdirRoot& workdir, uint64_t budgetBytes, double eps,
	       uint32_t maxIterations);

	RankResult Run();
	const grid::Meta& GetMeta() const;

private:
	void ValidateWorkdir() const;
	void InitializeRanks(common::RandomAccessFile* rankFile);
	double IterateOnce(grid::BlockReader* reader, const common::RandomAccessFile& current,
	                   common::RandomAccessFile* next);

	grid::WorkdirRoot Workdir_;
	grid::Meta Meta_;
	grid::PartitionScheme Scheme_;
	double Eps_;
	uint32_t MaxIterations_;
	uint64_t ChunkBytes_;
	double GroundScore_;
	std::vector<double> Contrib_;
	std::vector<double> Accumulator_;
	std::vector<uint32_t> Degrees_;
	std::unique_ptr<grid::BlockIndex> Index_;
};

} // namespace lr::rank
