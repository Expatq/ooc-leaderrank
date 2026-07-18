#include <common/core/size_literals.hpp>
#include <grid/partition.hpp>

#include <gtest/gtest.h>

#include <stdexcept>
#include <string_view>

namespace {

using lr::grid::PartitionPlanner;
using lr::grid::PartitionScheme;

constexpr uint32_t kLiveJournalMaxId = 4'847'570;
constexpr uint64_t kLiveJournalEdges = 68'993'773;
constexpr uint32_t kTwitterMaxId = 41'652'229;
constexpr uint64_t kTwitterEdges = 1'468'364'884;
constexpr uint32_t kAuditThreads = 8;

uint32_t PlanPartitions(uint64_t budgetBytes, uint32_t maxId, uint64_t edges) {
	return PartitionPlanner(budgetBytes, kAuditThreads).Plan(maxId, edges).partitions;
}

TEST(PartitionScheme, GeometryOfFormatExample) {
	const PartitionScheme scheme{4, 2, 3};
	EXPECT_EQ(scheme.VertexCount(), 5u);
	EXPECT_EQ(scheme.BlockCount(), 4u);
	EXPECT_EQ(scheme.IntervalOf(0), 0u);
	EXPECT_EQ(scheme.IntervalOf(2), 0u);
	EXPECT_EQ(scheme.IntervalOf(3), 1u);
	EXPECT_EQ(scheme.IntervalBase(1), 3u);
	EXPECT_EQ(scheme.IntervalLength(0), 3u);
	EXPECT_EQ(scheme.IntervalLength(1), 2u);
	EXPECT_EQ(scheme.BlockPosition(0, 0), 0u);
	EXPECT_EQ(scheme.BlockPosition(1, 0), 1u);
	EXPECT_EQ(scheme.BlockPosition(0, 1), 2u);
	EXPECT_EQ(scheme.BlockPosition(1, 1), 3u);
}

TEST(PartitionPlanner, LiveJournalAuditTable) {
	EXPECT_EQ(PlanPartitions(32_MiB, kLiveJournalMaxId, kLiveJournalEdges), 7u);
	EXPECT_EQ(PlanPartitions(64_MiB, kLiveJournalMaxId, kLiveJournalEdges), 5u);
	EXPECT_EQ(PlanPartitions(128_MiB, kLiveJournalMaxId, kLiveJournalEdges), 3u);
}

TEST(PartitionPlanner, TwitterAuditTable) {
	EXPECT_EQ(PlanPartitions(32_MiB, kTwitterMaxId, kTwitterEdges), 46u);
	EXPECT_EQ(PlanPartitions(64_MiB, kTwitterMaxId, kTwitterEdges), 23u);
	EXPECT_EQ(PlanPartitions(128_MiB, kTwitterMaxId, kTwitterEdges), 14u);
}

TEST(PartitionPlanner, IntervalCoversAllVertices) {
	const PartitionScheme scheme =
	    PartitionPlanner(64_MiB, 1).Plan(kLiveJournalMaxId, kLiveJournalEdges);
	const uint64_t covered = uint64_t{scheme.partitions} * scheme.intervalSize;
	EXPECT_GE(covered, scheme.VertexCount());
	EXPECT_LT(covered - scheme.intervalSize, scheme.VertexCount());
}

TEST(PartitionPlanner, TinyGraphGetsSinglePartition) {
	const PartitionScheme scheme = PartitionPlanner(128_MiB, 1).Plan(4, 4);
	EXPECT_EQ(scheme.partitions, 1u);
	EXPECT_EQ(scheme.intervalSize, 5u);
}

TEST(PartitionPlanner, InfeasibleBudgetThrowsWithMinimum) {
	const PartitionPlanner planner(1_MiB, 14);
	try {
		planner.Plan(kTwitterMaxId, kTwitterEdges);
		FAIL() << "expected exception";
	} catch (const std::runtime_error& error) {
		EXPECT_NE(std::string_view{error.what()}.find("minimum feasible"), std::string_view::npos);
	}
}

TEST(PartitionPlanner, MinBudgetIsFeasibleAndTight) {
	const PartitionPlanner planner(128_MiB, kAuditThreads);
	const uint64_t minBytes = planner.MinBudgetBytes(kTwitterMaxId, kTwitterEdges);
	EXPECT_NO_THROW(PartitionPlanner(minBytes, kAuditThreads).Plan(kTwitterMaxId, kTwitterEdges));
	EXPECT_THROW(PartitionPlanner(minBytes - 1, kAuditThreads).Plan(kTwitterMaxId, kTwitterEdges),
	             std::runtime_error);
}

TEST(PartitionPlanner, IoChunkClampsToBounds) {
	EXPECT_EQ(PartitionPlanner(1_MiB, 14).IoChunkBytes(), 256_KiB);
	EXPECT_EQ(PartitionPlanner(1_GiB, 1).IoChunkBytes(), 4_MiB);
}

} // namespace
