#include <grid/layout.hpp>
#include <grid/meta.hpp>
#include <grid/partition.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <stdexcept>

namespace {

using lr::grid::Meta;
using lr::grid::PartitionScheme;
using lr::grid::WorkdirRoot;

Meta SampleMeta() {
	Meta meta;
	meta.scheme = PartitionScheme::Create(4, 2);
	meta.transpose = 1;
	meta.threadsPlanned = 8;
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
	const WorkdirRoot workdir(testing::TempDir());
	SampleMeta().Save(workdir);
	const Meta loaded = Meta::Load(workdir);
	EXPECT_EQ(loaded.transpose, 1u);
	EXPECT_EQ(loaded.threadsPlanned, 8u);
	EXPECT_EQ(loaded.scheme.maxId, 4u);
	EXPECT_EQ(loaded.scheme.partitions, 2u);
	EXPECT_EQ(loaded.scheme.intervalSize, 3u);
	EXPECT_EQ(loaded.vertices, 4u);
	EXPECT_EQ(loaded.edgesRaw, 6u);
	EXPECT_EQ(loaded.edges, 4u);
	EXPECT_EQ(loaded.droppedSelfLoops, 1u);
	EXPECT_EQ(loaded.droppedDuplicates, 1u);
	EXPECT_EQ(loaded.maxOutDegree, 1u);
	EXPECT_EQ(loaded.maxInDegree, 2u);
	EXPECT_EQ(loaded.scheme.IntervalLength(1), 2u);
}

TEST(Meta, MissingKeyFails) {
	const WorkdirRoot workdir(testing::TempDir());
	std::ofstream file(workdir.MetaFile(), std::ios::trunc);
	file << "transpose=0\n";
	file.close();
	EXPECT_THROW(Meta::Load(workdir), std::runtime_error);
}

TEST(Meta, MissingFileFails) {
	EXPECT_THROW(Meta::Load(WorkdirRoot("/nonexistent")), std::runtime_error);
}

TEST(Meta, ValueOutOfRangeFails) {
	const WorkdirRoot workdir(testing::TempDir());
	std::ofstream file(workdir.MetaFile(), std::ios::trunc);
	file << "transpose=1\nthreads_planned=8\nmax_id=4294967296\npartitions=2\nvertices=4\n"
	        "edges_raw=6\nedges=4\ndropped_self_loops=1\ndropped_duplicates=1\n"
	        "max_out_degree=1\nmax_in_degree=2\n";
	file.close();
	EXPECT_THROW(Meta::Load(workdir), std::runtime_error);
}

TEST(Meta, ZeroPartitionsFails) {
	const WorkdirRoot workdir(testing::TempDir());
	std::ofstream file(workdir.MetaFile(), std::ios::trunc);
	file << "transpose=0\nthreads_planned=1\nmax_id=4\npartitions=0\nvertices=4\nedges_raw=4\n"
	        "edges=4\ndropped_self_loops=0\ndropped_duplicates=0\nmax_out_degree=1\n"
	        "max_in_degree=1\n";
	file.close();
	EXPECT_THROW(Meta::Load(workdir), std::runtime_error);
}

} // namespace
