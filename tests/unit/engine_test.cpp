#include <rank/lib/engine.hpp>

#include <common/core/constants.hpp>
#include <common/core/size_literals.hpp>
#include <common/csv/csv_reader.hpp>
#include <common/io/aligned_io.hpp>
#include <common/thread/thread_pool.hpp>
#include <grid/layout.hpp>
#include <grid/meta.hpp>
#include <grid/partition.hpp>
#include <prepare/lib/grid_builder.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using lr::common::ThreadPool;
using lr::grid::Meta;
using lr::grid::PartitionScheme;
using lr::grid::WorkdirRoot;
using lr::rank::Engine;
using lr::rank::RankConfig;
using lr::rank::RankResult;

constexpr uint64_t kTestArenaBytes = 1_MiB;
constexpr uint64_t kTestBudgetBytes = 64_MiB;
constexpr double kTestEps = 1e-13;
constexpr double kValueTolerance = 1e-9;

RankConfig TestConfig() {
	RankConfig config;
	config.budgetBytes = kTestBudgetBytes;
	config.eps = kTestEps;
	config.maxIterations = lr::common::kDefaultMaxIterations;
	return config;
}

WorkdirRoot BuildWorkdir(const std::string& name, const PartitionScheme& scheme,
                         const std::string& csvContent, uint32_t threadsPlanned = 1,
                         uint64_t arenaBytes = kTestArenaBytes) {
	const WorkdirRoot workdir(testing::TempDir() + name);
	std::filesystem::remove_all(workdir.Path());
	std::filesystem::create_directories(workdir.DegreesDir().Path());
	std::filesystem::create_directories(workdir.PresentDir().Path());
	const std::string csvPath = (workdir.Path() / "edges.csv").string();
	std::ofstream csv(csvPath, std::ios::binary);
	csv << csvContent;
	csv.close();

	ThreadPool stagePool(1);
	const auto scatter =
	    lr::prepare::EdgeScatterer(scheme, workdir, arenaBytes, &stagePool).Run(csvPath, false);
	const auto assembly = lr::prepare::BlockAssembler(scheme, workdir, arenaBytes, &stagePool).Run();
	const auto degrees =
	    lr::prepare::DegreeBuilder(scheme, workdir, lr::common::kMinIoChunkBytes, &stagePool).Run();

	Meta meta;
	meta.scheme = scheme;
	meta.threadsPlanned = threadsPlanned;
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

RankResult RunEngine(const WorkdirRoot& workdir, const RankConfig& config, uint32_t threads) {
	const Meta meta = Meta::Load(workdir);
	ThreadPool pool(threads);
	Engine engine(workdir, meta, config, &pool, nullptr);
	return engine.Run();
}

std::vector<char> ReadRankFileBytes(const WorkdirRoot& workdir, const PartitionScheme& scheme,
                                    const RankResult& result) {
	const uint64_t bytes = scheme.VertexCount() * lr::common::kRankBytesPerVertex;
	const lr::common::RandomAccessFile rankFile(workdir.RankFile(result.rankFileSide), bytes);
	std::vector<char> raw(bytes);
	rankFile.ReadAt(0, raw.data(), raw.size());
	return raw;
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

	const RankResult result = RunEngine(workdir, TestConfig(), 1);

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

	const RankResult result = RunEngine(workdir, TestConfig(), 1);

	EXPECT_TRUE(result.converged);
	const std::vector<double> scores = ReadRanks(workdir, scheme, result);
	EXPECT_NEAR(scores[1], 2.0 / 3.0, kValueTolerance);
	EXPECT_NEAR(scores[2], 2.0 / 3.0, kValueTolerance);
	EXPECT_NEAR(scores[3], 2.0 / 3.0, kValueTolerance);
}

TEST(Engine, EmptyGraphMetaFails) {
	const WorkdirRoot workdir(testing::TempDir() + "engine_empty");
	std::filesystem::remove_all(workdir.Path());
	std::filesystem::create_directories(workdir.Path());
	Meta meta;
	meta.scheme = PartitionScheme{4, 1, 5};
	meta.vertices = 0;
	meta.edges = 0;
	meta.Save(workdir);
	EXPECT_THROW(RunEngine(workdir, TestConfig(), 1), std::runtime_error);
}

TEST(Engine, TruncatedBlocksBinFails) {
	const PartitionScheme scheme{4, 2, 3};
	const WorkdirRoot workdir =
	    BuildWorkdir("engine_truncated", scheme, "from,to\n1,2\n2,3\n3,1\n4,1\n");
	std::filesystem::resize_file(workdir.BlocksBin(), 100);
	EXPECT_THROW(RunEngine(workdir, TestConfig(), 1), std::runtime_error);
}

TEST(Engine, WrongDegreesFileSizeFails) {
	const PartitionScheme scheme{4, 2, 3};
	const WorkdirRoot workdir =
	    BuildWorkdir("engine_bad_deg", scheme, "from,to\n1,2\n2,3\n3,1\n4,1\n");
	std::filesystem::resize_file(workdir.DegreesDir().Degrees(0), 16);
	EXPECT_THROW(RunEngine(workdir, TestConfig(), 1), std::runtime_error);
}

TEST(Engine, RankBytesIdenticalForAnyThreadCount) {
	const PartitionScheme scheme{99, 4, 25};
	std::string csv = "from,to\n";
	for (uint32_t src = 1; src < 100; ++src) {
		csv += std::format("{},{}\n", src, src % 7);
		csv += std::format("{},{}\n", src, (src * 13 + 1) % 100);
	}
	const WorkdirRoot workdir = BuildWorkdir("engine_threads", scheme, csv, 8);

	const RankResult baselineResult = RunEngine(workdir, TestConfig(), 1);
	const std::vector<char> baseline = ReadRankFileBytes(workdir, scheme, baselineResult);
	for (const uint32_t threads : {2u, 3u, 8u}) {
		const RankResult result = RunEngine(workdir, TestConfig(), threads);
		EXPECT_EQ(result.iterations, baselineResult.iterations) << "T=" << threads;
		EXPECT_EQ(ReadRankFileBytes(workdir, scheme, result), baseline) << "T=" << threads;
	}
}

TEST(Engine, HyperDstRunAcrossChunkBoundaryIsDeterministic) {
	const uint32_t vertexCount = 200'002;
	const uint64_t budgetBytes = 34_MiB;
	const uint32_t threadsPlanned = 20;
	const PartitionScheme scheme =
	    lr::grid::PartitionPlanner(budgetBytes, threadsPlanned).Plan(vertexCount - 1, 200'000);
	std::string csv = "from,to\n";
	for (uint32_t src = 2; src < vertexCount; ++src) {
		csv += std::format("{},1\n", src);
	}
	const WorkdirRoot workdir = BuildWorkdir("engine_hyperdst", scheme, csv, threadsPlanned, 4_MiB);

	RankConfig config = TestConfig();
	config.budgetBytes = budgetBytes;
	config.maxIterations = 3;
	const RankResult baselineResult = RunEngine(workdir, config, 1);
	const std::vector<char> baseline = ReadRankFileBytes(workdir, scheme, baselineResult);
	for (const uint32_t threads : {4u, 13u}) {
		const RankResult result = RunEngine(workdir, config, threads);
		EXPECT_EQ(ReadRankFileBytes(workdir, scheme, result), baseline) << "T=" << threads;
	}
}

} // namespace
