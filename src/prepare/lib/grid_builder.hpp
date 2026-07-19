#pragma once

#include <common/core/edge.hpp>
#include <common/csv/csv_reader.hpp>
#include <common/io/bitmap.hpp>
#include <grid/layout.hpp>
#include <grid/partition.hpp>

#include <cstdint>
#include <vector>

namespace lr::prepare {

struct ScatterResult {
	uint64_t edgesWritten;
	uint64_t droppedSelfLoops;
};

class EdgeScatterer {
public:
	EdgeScatterer(const grid::PartitionScheme& scheme, const grid::WorkdirRoot& workdir, uint64_t arenaBytes);

	ScatterResult Run(common::CsvEdgeStream* input);

private:
	void Flush(uint32_t block);

	grid::PartitionScheme Scheme_;
	grid::WorkdirRoot Workdir_;
	uint64_t CapacityEdges_;
	std::vector<common::Edge> Arena_;
	std::vector<uint32_t> Counts_;
};

struct AssembleResult {
	uint32_t maxInDegree;
	uint64_t droppedDuplicates;
};

class BlockAssembler {
public:
	BlockAssembler(const grid::PartitionScheme& scheme, const grid::WorkdirRoot& workdir, uint64_t arenaBytes);

	AssembleResult Run();

private:
	grid::PartitionScheme Scheme_;
	grid::WorkdirRoot Workdir_;
	uint64_t SortCapacityEdges_;
	std::vector<common::Edge> SortBuffer_;
	std::vector<uint32_t> InDegrees_;
};

struct DegreesResult {
	uint64_t vertices;
	uint32_t maxOutDegree;
};

class DegreeBuilder {
public:
	DegreeBuilder(const grid::PartitionScheme& scheme, const grid::WorkdirRoot& workdir, uint64_t chunkBytes);

	DegreesResult Run();

private:
	grid::PartitionScheme Scheme_;
	grid::WorkdirRoot Workdir_;
	uint64_t ChunkBytes_;
	std::vector<uint32_t> OutDegrees_;
};

} // namespace lr::prepare
