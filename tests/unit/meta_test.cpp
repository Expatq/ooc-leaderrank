#include <grid/layout.hpp>
#include <grid/meta.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <stdexcept>

namespace {

using lr::grid::Meta;
using lr::grid::WorkdirLayout;
using lr::grid::WorkdirRoot;

Meta SampleMeta() {
	Meta meta;
	meta.transpose = 1;
	meta.threadsPlanned = 8;
	meta.maxId = 4;
	meta.intervalSize = 3;
	meta.partitions = 2;
	meta.vertices = 4;
	meta.edgesRaw = 6;
	meta.edges = 4;
	meta.droppedSelfLoops = 1;
	meta.droppedDuplicates = 1;
	meta.maxOutDegree = 1;
	meta.maxInDegree = 2;
	return meta;
}

TEST(Meta, SaveLoadRoundTrip) {
	const WorkdirRoot workdir = WorkdirLayout(testing::TempDir()).Root();
	SampleMeta().Save(workdir);
	const Meta loaded = Meta::Load(workdir);
	EXPECT_EQ(loaded.transpose, 1u);
	EXPECT_EQ(loaded.threadsPlanned, 8u);
	EXPECT_EQ(loaded.maxId, 4u);
	EXPECT_EQ(loaded.intervalSize, 3u);
	EXPECT_EQ(loaded.partitions, 2u);
	EXPECT_EQ(loaded.vertices, 4u);
	EXPECT_EQ(loaded.edgesRaw, 6u);
	EXPECT_EQ(loaded.edges, 4u);
	EXPECT_EQ(loaded.droppedSelfLoops, 1u);
	EXPECT_EQ(loaded.droppedDuplicates, 1u);
	EXPECT_EQ(loaded.maxOutDegree, 1u);
	EXPECT_EQ(loaded.maxInDegree, 2u);
	EXPECT_EQ(loaded.Scheme().partitions, 2u);
	EXPECT_EQ(loaded.Scheme().IntervalLength(1), 2u);
}

TEST(Meta, MissingKeyFails) {
	const WorkdirRoot workdir = WorkdirLayout(testing::TempDir()).Root();
	std::ofstream file(workdir.MetaFile(), std::ios::trunc);
	file << "transpose=0\n";
	file.close();
	EXPECT_THROW(Meta::Load(workdir), std::runtime_error);
}

TEST(Meta, MissingFileFails) {
	EXPECT_THROW(Meta::Load(WorkdirLayout("/nonexistent").Root()), std::runtime_error);
}

} // namespace
