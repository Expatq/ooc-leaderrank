#pragma once

#include "rank_config.hpp"
#include "rank_result.hpp"

#include <common/io/aligned_io.hpp>
#include <common/thread/thread_pool.hpp>
#include <grid/block_index.hpp>
#include <grid/block_reader.hpp>
#include <grid/layout.hpp>
#include <grid/meta.hpp>
#include <grid/partition.hpp>

#include <cstdint>
#include <iosfwd>
#include <memory>
#include <span>
#include <string>
#include <vector>

namespace lr::rank {

class Engine {
public:
	Engine(const grid::WorkdirRoot& workdir, const grid::Meta& meta, const RankConfig& config,
	       common::ThreadPool* pool, std::ostream* progress);

	RankResult Run();

private:
	struct ColumnSums {
		double deltaL1;
		double mass;
	};

	struct IterationSums {
		double ground = 0.0;
		double deltaL1 = 0.0;
		double mass = 0.0;
	};

	void ValidateWorkdir() const;
	void InitializeRanks(common::RandomAccessFile* rankFile);
	double IterateOnce(grid::BlockReader* reader, const common::RandomAccessFile& current, common::RandomAccessFile* next);
	double AccumulateColumn(grid::BlockReader* reader, const common::RandomAccessFile& current, uint32_t dstInterval);
	ColumnSums FinalizeColumn(const common::RandomAccessFile& current, common::RandomAccessFile* next, uint32_t dstInterval, double groundShare);
	void BuildDstSegments(std::span<const common::Edge> chunk);
	void CheckMassInvariant(const IterationSums& sums, double vertexCount) const;
	void Report(const std::string& line) const;

	grid::WorkdirRoot Workdir_;
	grid::Meta Meta_;
	grid::PartitionScheme Scheme_;
	double Eps_;
	uint32_t MaxIterations_;
	uint64_t ChunkBytes_;
	common::ThreadPool* Pool_;
	std::ostream* Progress_;
	double GroundScore_;
	std::vector<double> Contrib_;
	std::vector<double> Accumulator_;
	std::vector<uint32_t> Degrees_;
	std::vector<uint64_t> SegmentBounds_;
	std::vector<double> GroundPartials_;
	std::vector<ColumnSums> ColumnPartials_;
	std::unique_ptr<grid::BlockIndex> Index_;
};

} // namespace lr::rank
