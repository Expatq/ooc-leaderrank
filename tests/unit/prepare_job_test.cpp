#include <prepare/lib/prepare_job.hpp>

#include <common/core/size_literals.hpp>
#include <common/io/aligned_io.hpp>
#include <grid/layout.hpp>
#include <grid/meta.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <string>
#include <vector>

namespace {

using lr::grid::WorkdirRoot;
using lr::prepare::PrepareConfig;
using lr::prepare::PrepareJob;

std::vector<char> FileBytes(const std::filesystem::path& path) {
	const lr::common::InputFile file(path);
	std::vector<char> bytes(file.SizeBytes());
	file.ReadAt(0, bytes.data(), bytes.size());
	return bytes;
}

std::vector<char> WorkdirSnapshot(const WorkdirRoot& workdir) {
	const lr::grid::Meta meta = lr::grid::Meta::Load(workdir);
	std::vector<char> snapshot = FileBytes(workdir.BlocksBin());
	const auto append = [&snapshot](const std::vector<char>& bytes) {
		snapshot.insert(snapshot.end(), bytes.begin(), bytes.end());
	};
	append(FileBytes(workdir.BlocksIdx()));
	append(FileBytes(workdir.MetaFile()));
	for (uint32_t interval = 0; interval < meta.scheme.partitions; ++interval) {
		append(FileBytes(workdir.DegreesDir().Degrees(interval)));
		append(FileBytes(workdir.PresentDir().Present(interval)));
	}
	return snapshot;
}

TEST(PrepareJob, RepeatedParallelPrepareIsByteIdentical) {
	std::string csv = "from,to\n";
	for (uint32_t i = 0; i < 40'000; ++i) {
		csv += std::format("{},{}\n", (i * 37 + 11) % 500, (i * 53 + 29) % 500);
	}
	const std::string csvPath = testing::TempDir() + "prepare_repeat.csv";
	std::ofstream file(csvPath, std::ios::binary);
	file << csv;
	file.close();

	PrepareConfig config;
	config.edgesPath = csvPath;
	config.workdir = testing::TempDir() + "prepare_repeat_workdir";
	config.budgetBytes = 64_MiB;
	config.threadsPlanned = 4;

	PrepareJob(config, nullptr).Run();
	const std::vector<char> first = WorkdirSnapshot(WorkdirRoot(config.workdir));
	PrepareJob(config, nullptr).Run();
	const std::vector<char> second = WorkdirSnapshot(WorkdirRoot(config.workdir));
	EXPECT_EQ(first, second);
}

} // namespace
