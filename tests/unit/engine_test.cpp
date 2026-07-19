#include <rank/lib/engine.hpp>

#include <common/core/constants.hpp>
#include <common/core/size_literals.hpp>
#include <common/csv/csv_reader.hpp>
#include <common/io/aligned_io.hpp>
#include <grid/layout.hpp>
#include <grid/meta.hpp>
#include <grid/partition.hpp>
#include <prepare/lib/grid_builder.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using lr::grid::Meta;
using lr::grid::PartitionScheme;
using lr::grid::WorkdirLayout;
using lr::grid::WorkdirRoot;
using lr::rank::Engine;
using lr::rank::RankResult;

constexpr uint64_t kTestArenaBytes = 1_MiB;
constexpr uint64_t kTestBudgetBytes = 64_MiB;
constexpr double kTestEps = 1e-13;
constexpr double kValueTolerance = 1e-9;

WorkdirRoot BuildWorkdir(const std::string& name, const PartitionScheme& scheme,
                         const std::string& csvContent) {
	const WorkdirRoot workdir = WorkdirLayout(testing::TempDir() + name).Root();
	std::filesystem::remove_all(workdir.Path());
	std::filesystem::create_directories(workdir.DegreesDir().Path());
	std::filesystem::create_directories(workdir.PresentDir().Path());
	const std::string csvPath = (workdir.Path() / "edges.csv").string();
	std::ofstream csv(csvPath, std::ios::binary);
	csv << csvContent;
	csv.close();

	lr::common::CsvEdgeStream stream(csvPath, false);
	const auto scatter = lr::prepare::EdgeScatterer(scheme, workdir, kTestArenaBytes).Run(&stream);
	const auto assembly = lr::prepare::BlockAssembler(scheme, workdir, kTestArenaBytes).Run();
	const auto degrees = lr::prepare::DegreeBuilder(scheme, workdir).Run();

	Meta meta;
	meta.maxId = scheme.maxId;
	meta.intervalSize = scheme.intervalSize;
	meta.partitions = scheme.partitions;
	meta.vertices = degrees.vertices;
	meta.edgesRaw = scatter.edgesWritten + scatter.droppedSelfLoops;
	meta.edges = scatter.edgesWritten - assembly.droppedDuplicates;
	meta.droppedSelfLoops = scatter.droppedSelfLoops;
	meta.droppedDuplicates = assembly.droppedDuplicates;
	meta.maxOutDegree = degrees.maxOutDegree;
	meta.maxInDegree = assembly.maxInDegree;
	meta.Save(workdir);
	return workdir;
}

std::vector<double> ReadRanks(const WorkdirRoot& workdir, const PartitionScheme& scheme,
                              const RankResult& result) {
	const lr::common::RandomAccessFile rankFile(workdir.RankFile(result.rankFileSide),
	                                            scheme.VertexCount() *
	                                                lr::common::kRankBytesPerVertex);
	std::vector<double> scores(uint64_t{scheme.maxId} + 1);
	rankFile.ReadAt(0, scores.data(), scores.size() * lr::common::kRankBytesPerVertex);
	return scores;
}

TEST(Engine, TaskExampleConvergesToStationaryPoint) {
	const PartitionScheme scheme{4, 2, 3};
	const WorkdirRoot workdir =
	    BuildWorkdir("engine_task", scheme, "from,to\n1,2\n2,3\n3,1\n4,1\n");

	Engine engine(workdir, kTestBudgetBytes, kTestEps, lr::common::kDefaultMaxIterations);
	const RankResult result = engine.Run();

	EXPECT_TRUE(result.converged);
	EXPECT_NEAR(result.groundScore, 4.0 / 3.0, kValueTolerance);

	const std::vector<double> scores = ReadRanks(workdir, scheme, result);
	EXPECT_EQ(scores[0], 0.0);
	EXPECT_NEAR(scores[1], 6.0 / 7.0, kValueTolerance);
	EXPECT_NEAR(scores[2], 16.0 / 21.0, kValueTolerance);
	EXPECT_NEAR(scores[3], 5.0 / 7.0, kValueTolerance);
	EXPECT_NEAR(scores[4], 1.0 / 3.0, kValueTolerance);
}

TEST(Engine, CycleGivesEqualScores) {
	const PartitionScheme scheme{3, 2, 2};
	const WorkdirRoot workdir = BuildWorkdir("engine_cycle", scheme, "from,to\n1,2\n2,3\n3,1\n");

	Engine engine(workdir, kTestBudgetBytes, kTestEps, lr::common::kDefaultMaxIterations);
	const RankResult result = engine.Run();

	EXPECT_TRUE(result.converged);
	const std::vector<double> scores = ReadRanks(workdir, scheme, result);
	EXPECT_NEAR(scores[1], 2.0 / 3.0, kValueTolerance);
	EXPECT_NEAR(scores[2], 2.0 / 3.0, kValueTolerance);
	EXPECT_NEAR(scores[3], 2.0 / 3.0, kValueTolerance);
}

TEST(Engine, EmptyGraphMetaFails) {
	const WorkdirRoot workdir = WorkdirLayout(testing::TempDir() + "engine_empty").Root();
	std::filesystem::remove_all(workdir.Path());
	std::filesystem::create_directories(workdir.Path());
	Meta meta;
	meta.maxId = 4;
	meta.intervalSize = 5;
	meta.partitions = 1;
	meta.vertices = 0;
	meta.edges = 0;
	meta.Save(workdir);
	EXPECT_THROW(Engine(workdir, kTestBudgetBytes, kTestEps, lr::common::kDefaultMaxIterations),
	             std::runtime_error);
}

} // namespace
