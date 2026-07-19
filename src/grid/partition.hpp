#pragma once

#include <cstdint>

namespace lr::grid {

class PartitionScheme {
public:
	uint32_t maxId;
	uint32_t partitions;
	uint32_t intervalSize;

	uint64_t VertexCount() const;
	uint64_t BlockCount() const;
	uint32_t IntervalOf(uint32_t vertex) const;
	uint32_t IntervalBase(uint32_t interval) const;
	uint32_t IntervalLength(uint32_t interval) const;
	uint64_t BlockPosition(uint32_t srcInterval, uint32_t dstInterval) const;
};

class PartitionPlanner {
public:
	PartitionPlanner(uint64_t budgetBytes, uint32_t threads);

	PartitionScheme Plan(uint32_t maxId, uint64_t edgesRaw) const;
	uint64_t MinBudgetBytes(uint32_t maxId, uint64_t edgesRaw) const;
	uint64_t IoChunkBytes() const;
	uint64_t UsableBytes() const;

private:
	uint64_t ChoosePartitions(uint64_t vertexCount, uint64_t edgesRaw) const;
	bool Feasible(uint32_t maxId, uint64_t edgesRaw) const;

	uint64_t BudgetBytes_;
	uint32_t Threads_;
};

} // namespace lr::grid
