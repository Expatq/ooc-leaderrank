#include "partition.hpp"

#include <common/core/constants.hpp>
#include <common/core/size_literals.hpp>

#include <algorithm>
#include <cmath>
#include <format>
#include <stdexcept>

namespace lr::grid {

namespace {

using Uint128 = unsigned __int128;

uint64_t CeilDiv(uint64_t dividend, uint64_t divisor) {
	return (dividend + divisor - 1) / divisor;
}

bool SortArenaFits(uint64_t vertexCount, uint64_t edgesRaw, uint64_t usableBytes,
                   uint64_t partitions) {
	const Uint128 arena = Uint128(common::kEdgeBytes) * edgesRaw +
	                      Uint128(common::kDegreeBytesPerVertex) * vertexCount * partitions;
	return arena <= Uint128(usableBytes) * partitions * partitions;
}

} // namespace

uint64_t PartitionScheme::VertexCount() const {
	return uint64_t{maxId} + 1;
}

uint64_t PartitionScheme::BlockCount() const {
	return uint64_t{partitions} * partitions;
}

uint32_t PartitionScheme::IntervalOf(uint32_t vertex) const {
	return vertex / intervalSize;
}

uint32_t PartitionScheme::IntervalBase(uint32_t interval) const {
	return interval * intervalSize;
}

uint32_t PartitionScheme::IntervalLength(uint32_t interval) const {
	const uint64_t begin = uint64_t{interval} * intervalSize;
	return static_cast<uint32_t>(std::min<uint64_t>(intervalSize, VertexCount() - begin));
}

uint64_t PartitionScheme::BlockPosition(uint32_t srcInterval, uint32_t dstInterval) const {
	return uint64_t{dstInterval} * partitions + srcInterval;
}

PartitionPlanner::PartitionPlanner(uint64_t budgetBytes, uint32_t threads)
    : BudgetBytes_(budgetBytes), Threads_(threads) {
	if (threads == 0) {
		throw std::runtime_error("thread count must be positive");
	}
}

uint64_t PartitionPlanner::IoChunkBytes() const {
	const uint64_t raw = common::kIoChunkNumerator * BudgetBytes_ /
	                     (common::kIoChunkDenominatorPerThread * Threads_);
	return std::clamp(raw, common::kMinIoChunkBytes, common::kMaxIoChunkBytes);
}

uint64_t PartitionPlanner::UsableBytes() const {
	const uint64_t gross = common::kUsableNumerator * BudgetBytes_ / common::kUsableDenominator;
	const uint64_t ioBuffers = 2 * uint64_t{Threads_} * IoChunkBytes();
	if (gross <= ioBuffers) {
		return 0;
	}
	return gross - ioBuffers;
}

uint64_t PartitionPlanner::ChoosePartitions(uint64_t vertexCount, uint64_t edgesRaw) const {
	const uint64_t usable = UsableBytes();
	const uint64_t windowBound = CeilDiv(common::kColumnBytesPerVertex * vertexCount, usable);

	const double v = static_cast<double>(vertexCount);
	const double discriminant =
	    4.0 * v * v + 8.0 * static_cast<double>(usable) * static_cast<double>(edgesRaw);
	const double estimate = (2.0 * v + std::sqrt(discriminant)) / static_cast<double>(usable);
	uint64_t sortBound = std::max<uint64_t>(1, static_cast<uint64_t>(estimate));
	while (sortBound > 1 && SortArenaFits(vertexCount, edgesRaw, usable, sortBound - 1)) {
		--sortBound;
	}
	while (!SortArenaFits(vertexCount, edgesRaw, usable, sortBound)) {
		++sortBound;
	}

	return std::min(std::max(windowBound, sortBound), vertexCount);
}

bool PartitionPlanner::Feasible(uint32_t maxId, uint64_t edgesRaw) const {
	const uint64_t vertexCount = uint64_t{maxId} + 1;
	const uint64_t usable = UsableBytes();
	if (usable < common::kColumnBytesPerVertex) {
		return false;
	}
	const uint64_t partitions = ChoosePartitions(vertexCount, edgesRaw);
	if (common::kColumnBytesPerVertex * CeilDiv(vertexCount, partitions) > usable) {
		return false;
	}
	return usable / (partitions * partitions) >= common::kScatterMinBufferBytes;
}

PartitionScheme PartitionPlanner::Plan(uint32_t maxId, uint64_t edgesRaw) const {
	if (!Feasible(maxId, edgesRaw)) {
		throw std::runtime_error(
		    std::format("budget of {} bytes is infeasible for this graph (max_id={}, edges={}): "
		                "minimum feasible budget is {} bytes",
		                BudgetBytes_, maxId, edgesRaw, MinBudgetBytes(maxId, edgesRaw)));
	}
	const uint64_t vertexCount = uint64_t{maxId} + 1;
	const uint64_t partitions = ChoosePartitions(vertexCount, edgesRaw);
	const uint64_t intervalSize = CeilDiv(vertexCount, partitions);
	return PartitionScheme{maxId, static_cast<uint32_t>(partitions),
	                       static_cast<uint32_t>(intervalSize)};
}

uint64_t PartitionPlanner::MinBudgetBytes(uint32_t maxId, uint64_t edgesRaw) const {
	uint64_t lowBytes = 64_KiB;
	uint64_t highBytes = 1_TiB;
	while (lowBytes < highBytes) {
		const uint64_t midBytes = lowBytes + (highBytes - lowBytes) / 2;
		if (PartitionPlanner(midBytes, Threads_).Feasible(maxId, edgesRaw)) {
			highBytes = midBytes;
		} else {
			lowBytes = midBytes + 1;
		}
	}
	return lowBytes;
}

} // namespace lr::grid
