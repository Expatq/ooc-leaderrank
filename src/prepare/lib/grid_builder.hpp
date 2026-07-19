#pragma once

#include <common/core/edge.hpp>
#include <common/csv/csv_reader.hpp>
#include <common/io/bitmap.hpp>
#include <common/thread/thread_pool.hpp>
#include <grid/layout.hpp>
#include <grid/partition.hpp>

#include <cstdint>
#include <filesystem>
#include <mutex>
#include <vector>

namespace lr::prepare {

struct ScatterResult {
	uint64_t edgesWritten;
	uint64_t droppedSelfLoops;
};

class EdgeScatterer {
public:
	EdgeScatterer(const grid::PartitionScheme& scheme, const grid::WorkdirRoot& workdir, uint64_t arenaBytes, common::ThreadPool* pool);

	ScatterResult Run(const std::filesystem::path& edgesPath, bool transpose);

private:
	ScatterResult ScatterStream(common::CsvEdgeStream* input);
	void Flush(uint64_t block);

	grid::PartitionScheme Scheme_;
	grid::WorkdirRoot Workdir_;
	common::ThreadPool* Pool_;
	uint64_t CapacityEdges_;
	std::vector<common::Edge> Arena_;
	std::vector<uint32_t> Counts_;
	std::vector<std::mutex> Mutexes_;
};

struct AssembleResult {
	uint32_t maxInDegree;
	uint64_t droppedDuplicates;
};

class BlockAssembler {
public:
	BlockAssembler(const grid::PartitionScheme& scheme, const grid::WorkdirRoot& workdir, uint64_t arenaBytes, common::ThreadPool* pool);

	AssembleResult Run();

private:
	void SortBlock(uint64_t edgeCount);
	void CountColumnEdges(uint64_t uniqueCount, uint32_t columnBase, uint32_t columnLength, common::Bitmap* dstPresent);

	grid::PartitionScheme Scheme_;
	grid::WorkdirRoot Workdir_;
	common::ThreadPool* Pool_;
	uint64_t SortCapacityEdges_;
	std::vector<common::Edge> Arena_;
	std::vector<common::Edge> Aux_;
	std::vector<uint64_t> Runs_;
	std::vector<uint32_t> InDegrees_;
};

struct DegreesResult {
	uint64_t vertices;
	uint32_t maxOutDegree;
};

class DegreeBuilder {
public:
	DegreeBuilder(const grid::PartitionScheme& scheme, const grid::WorkdirRoot& workdir, uint64_t chunkBytes, common::ThreadPool* pool);

	DegreesResult Run();

private:
	grid::PartitionScheme Scheme_;
	grid::WorkdirRoot Workdir_;
	uint64_t ChunkBytes_;
	common::ThreadPool* Pool_;
	std::vector<uint32_t> OutDegrees_;
};

} // namespace lr::prepare
