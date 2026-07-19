#include <prepare/lib/grid_builder.hpp>

#include <common/core/constants.hpp>
#include <common/core/size_literals.hpp>
#include <common/csv/csv_reader.hpp>
#include <common/io/aligned_io.hpp>
#include <grid/block_index.hpp>
#include <grid/layout.hpp>
#include <grid/partition.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using lr::common::CsvEdgeStream;
using lr::common::Edge;
using lr::common::InputFile;
using lr::grid::BlockRef;
using lr::grid::PartitionScheme;
using lr::grid::WorkdirRoot;
using lr::prepare::BlockAssembler;
using lr::prepare::DegreeBuilder;
using lr::prepare::EdgeScatterer;

constexpr uint64_t kTestArenaBytes = 1_MiB;

WorkdirRoot MakeWorkdir(const std::string& name) {
	const WorkdirRoot root(testing::TempDir() + name);
	std::filesystem::remove_all(root.Path());
	std::filesystem::create_directories(root.DegreesDir().Path());
	std::filesystem::create_directories(root.PresentDir().Path());
	return root;
}

std::string WriteCsv(const std::string& name, const std::string& content) {
	const std::string path = testing::TempDir() + name;
	std::ofstream file(path, std::ios::binary);
	file << content;
	return path;
}

std::vector<Edge> ReadBlock(const InputFile& blocksBin, const BlockRef& ref) {
	std::vector<Edge> edges(ref.edgeCount);
	blocksBin.ReadAt(ref.offsetBytes, edges.data(), ref.edgeCount * sizeof(Edge));
	return edges;
}

TEST(GridBuilder, MatchesFormatByteExample) {
	const PartitionScheme scheme{4, 2, 3};
	const WorkdirRoot workdir = MakeWorkdir("format_example");
	const std::string csv = WriteCsv("format_example.csv", "from,to\n1,2\n2,3\n3,1\n4,1\n");

	CsvEdgeStream stream(csv, false);
	const auto scatter = EdgeScatterer(scheme, workdir, kTestArenaBytes).Run(&stream);
	EXPECT_EQ(scatter.edgesWritten, 4u);
	EXPECT_EQ(scatter.droppedSelfLoops, 0u);

	const auto assembly = BlockAssembler(scheme, workdir, kTestArenaBytes).Run();
	EXPECT_EQ(assembly.droppedDuplicates, 0u);
	EXPECT_EQ(assembly.maxInDegree, 2u);

	const auto degrees = DegreeBuilder(scheme, workdir, lr::common::kMinIoChunkBytes).Run();
	EXPECT_EQ(degrees.vertices, 4u);
	EXPECT_EQ(degrees.maxOutDegree, 1u);

	const InputFile idxFile(workdir.BlocksIdx());
	ASSERT_EQ(idxFile.SizeBytes(), 4 * sizeof(BlockRef));
	std::vector<BlockRef> index(4);
	idxFile.ReadAt(0, index.data(), idxFile.SizeBytes());

	EXPECT_EQ(index[0].offsetBytes, 0u);
	EXPECT_EQ(index[0].edgeCount, 1u);
	EXPECT_EQ(index[1].offsetBytes, 4096u);
	EXPECT_EQ(index[1].edgeCount, 2u);
	EXPECT_EQ(index[2].offsetBytes, 8192u);
	EXPECT_EQ(index[2].edgeCount, 1u);
	EXPECT_EQ(index[3].offsetBytes, 12288u);
	EXPECT_EQ(index[3].edgeCount, 0u);

	const InputFile blocksBin(workdir.BlocksBin());
	EXPECT_EQ(blocksBin.SizeBytes(), 12288u);
	EXPECT_EQ(ReadBlock(blocksBin, index[0]), (std::vector<Edge>{{1, 2}}));
	EXPECT_EQ(ReadBlock(blocksBin, index[1]), (std::vector<Edge>{{3, 1}, {4, 1}}));
	EXPECT_EQ(ReadBlock(blocksBin, index[2]), (std::vector<Edge>{{2, 3}}));

	const InputFile degreesFirst(workdir.DegreesDir().Degrees(0));
	std::vector<uint32_t> row(3);
	degreesFirst.ReadAt(0, row.data(), 12);
	EXPECT_EQ(row, (std::vector<uint32_t>{0, 1, 1}));
	const InputFile degreesSecond(workdir.DegreesDir().Degrees(1));
	std::vector<uint32_t> tail(2);
	degreesSecond.ReadAt(0, tail.data(), 8);
	EXPECT_EQ(tail, (std::vector<uint32_t>{1, 1}));

	std::ifstream presentFirst(workdir.PresentDir().Present(0), std::ios::binary);
	std::ifstream presentSecond(workdir.PresentDir().Present(1), std::ios::binary);
	EXPECT_EQ(presentFirst.get(), 0x06);
	EXPECT_EQ(presentSecond.get(), 0x03);
}

TEST(GridBuilder, RoundTripFiltersLoopsAndDuplicates) {
	const PartitionScheme scheme{9, 3, 4};
	const WorkdirRoot workdir = MakeWorkdir("round_trip");
	const std::string csv = WriteCsv(
	    "round_trip.csv", "from,to\n5,5\n1,2\n1,2\n9,0\n0,9\n7,3\n3,7\n7,3\n2,2\n8,1\n1,8\n");

	CsvEdgeStream stream(csv, false);
	const auto scatter = EdgeScatterer(scheme, workdir, kTestArenaBytes).Run(&stream);
	EXPECT_EQ(scatter.droppedSelfLoops, 2u);
	EXPECT_EQ(scatter.edgesWritten, 9u);

	const auto assembly = BlockAssembler(scheme, workdir, kTestArenaBytes).Run();
	EXPECT_EQ(assembly.droppedDuplicates, 2u);

	const InputFile blocksBin(workdir.BlocksBin());
	const InputFile idxFile(workdir.BlocksIdx());
	std::vector<BlockRef> index(9);
	idxFile.ReadAt(0, index.data(), idxFile.SizeBytes());

	std::set<std::pair<uint32_t, uint32_t>> collected;
	uint64_t total = 0;
	for (const BlockRef& ref : index) {
		for (const Edge& edge : ReadBlock(blocksBin, ref)) {
			collected.emplace(edge.src, edge.dst);
			++total;
		}
	}
	const std::set<std::pair<uint32_t, uint32_t>> expected{{1, 2}, {9, 0}, {0, 9}, {7, 3}, {3, 7}, {8, 1}, {1, 8}};
	EXPECT_EQ(collected, expected);
	EXPECT_EQ(total, expected.size());

	const auto degrees = DegreeBuilder(scheme, workdir, lr::common::kMinIoChunkBytes).Run();
	EXPECT_EQ(degrees.vertices, 7u);
}

TEST(GridBuilder, BlocksSortedByDstThenSrc) {
	const PartitionScheme scheme{5, 1, 6};
	const WorkdirRoot workdir = MakeWorkdir("sorted");
	const std::string csv = WriteCsv("sorted.csv", "from,to\n5,1\n3,1\n4,2\n1,2\n2,1\n");

	CsvEdgeStream stream(csv, false);
	EdgeScatterer(scheme, workdir, kTestArenaBytes).Run(&stream);
	BlockAssembler(scheme, workdir, kTestArenaBytes).Run();

	const InputFile blocksBin(workdir.BlocksBin());
	std::vector<Edge> edges(5);
	blocksBin.ReadAt(0, edges.data(), 5 * sizeof(Edge));
	EXPECT_EQ(edges, (std::vector<Edge>{{2, 1}, {3, 1}, {5, 1}, {1, 2}, {4, 2}}));
}

TEST(GridBuilder, AllSelfLoopsFailAtDegrees) {
	const PartitionScheme scheme{9, 1, 10};
	const WorkdirRoot workdir = MakeWorkdir("only_loops");
	const std::string csv = WriteCsv("only_loops.csv", "from,to\n5,5\n6,6\n");

	CsvEdgeStream stream(csv, false);
	const auto scatter = EdgeScatterer(scheme, workdir, kTestArenaBytes).Run(&stream);
	EXPECT_EQ(scatter.edgesWritten, 0u);
	BlockAssembler(scheme, workdir, kTestArenaBytes).Run();
	EXPECT_THROW(DegreeBuilder(scheme, workdir, lr::common::kMinIoChunkBytes).Run(), std::runtime_error);
}

} // namespace
