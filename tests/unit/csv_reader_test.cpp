#include <common/csv/csv_reader.hpp>

#include <common/thread/thread_pool.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using lr::common::CsvEdgeStream;
using lr::common::Edge;
using lr::common::EdgeScanner;

std::string WriteInput(const std::string& name, const std::string& content) {
	const std::string path = testing::TempDir() + name;
	std::ofstream file(path, std::ios::binary);
	file << content;
	return path;
}

std::vector<Edge> ReadAll(const std::string& path, bool transpose = false) {
	CsvEdgeStream stream(path, transpose);
	std::vector<Edge> edges;
	Edge edge{};
	while (stream.Next(&edge)) {
		edges.push_back(edge);
	}
	return edges;
}

TEST(CsvEdgeStream, ParsesPlainCsvWithHeader) {
	const auto path = WriteInput("plain.csv", "from,to\n1,2\n2,3\n");
	EXPECT_EQ(ReadAll(path), (std::vector<Edge>{{1, 2}, {2, 3}}));
}

TEST(CsvEdgeStream, ParsesTsvWithSpacesAndMixedCaseHeader) {
	const auto path = WriteInput("spaced.tsv", " From \t To \n 1 \t 2 \n 10 \t 20 \n");
	EXPECT_EQ(ReadAll(path), (std::vector<Edge>{{1, 2}, {10, 20}}));
}

TEST(CsvEdgeStream, HandlesCrlfBlankLinesAndComments) {
	const auto path =
	    WriteInput("crlf.csv", "# comment\r\n\r\nfrom,to\r\n1,2\r\n\r\n# more\r\n3,4\r\n");
	EXPECT_EQ(ReadAll(path), (std::vector<Edge>{{1, 2}, {3, 4}}));
}

TEST(CsvEdgeStream, FirstLineWithDataIsNotSwallowedAsHeader) {
	const auto path = WriteInput("no_header.csv", "5,6\n7,8\n");
	EXPECT_EQ(ReadAll(path), (std::vector<Edge>{{5, 6}, {7, 8}}));
}

TEST(CsvEdgeStream, ExtraColumnsAreIgnored) {
	const auto path = WriteInput("extra.csv", "from,to,weight\n1,2,99\n");
	EXPECT_EQ(ReadAll(path), (std::vector<Edge>{{1, 2}}));
}

TEST(CsvEdgeStream, TransposeSwapsEndpoints) {
	const auto path = WriteInput("transpose.csv", "from,to\n1,2\n");
	EXPECT_EQ(ReadAll(path, true), (std::vector<Edge>{{2, 1}}));
}

TEST(CsvEdgeStream, EmptyFileYieldsNoEdges) {
	const auto path = WriteInput("empty.csv", "");
	EXPECT_TRUE(ReadAll(path).empty());
}

TEST(CsvEdgeStream, NegativeIdFailsWithLineNumber) {
	const auto path = WriteInput("negative.csv", "from,to\n1,2\n3,-4\n");
	try {
		ReadAll(path);
		FAIL() << "expected exception";
	} catch (const std::runtime_error& error) {
		EXPECT_NE(std::string(error.what()).find(":3:"), std::string::npos);
		EXPECT_NE(std::string(error.what()).find("negative id"), std::string::npos);
	}
}

TEST(CsvEdgeStream, NegativeIdOnFirstLineIsErrorNotHeader) {
	const auto path = WriteInput("negative_first.csv", "-1,2\n");
	EXPECT_THROW(ReadAll(path), std::runtime_error);
}

TEST(CsvEdgeStream, IdAboveInt32Fails) {
	const auto path = WriteInput("overflow.csv", "from,to\n1,3000000000\n");
	EXPECT_THROW(ReadAll(path), std::runtime_error);
}

TEST(CsvEdgeStream, HugeIdBeyondUint64Fails) {
	const auto path = WriteInput("huge.csv", "from,to\n1,99999999999999999999999\n");
	EXPECT_THROW(ReadAll(path), std::runtime_error);
}

TEST(CsvEdgeStream, GarbageTokenFails) {
	const auto path = WriteInput("garbage.csv", "from,to\n1,two\n");
	EXPECT_THROW(ReadAll(path), std::runtime_error);
}

TEST(CsvEdgeStream, SingleColumnFails) {
	const auto path = WriteInput("one_column.csv", "from,to\n42\n");
	EXPECT_THROW(ReadAll(path), std::runtime_error);
}

TEST(CsvEdgeStream, MissingFileFails) {
	EXPECT_THROW(CsvEdgeStream("/nonexistent/edges.csv", false), std::runtime_error);
}

TEST(EdgeScanner, CountsRawEdgesAndMaxId) {
	const auto path = WriteInput("scan.csv", "from,to\n1,2\n7,7\n2,1\n2,1\n");
	const auto result = EdgeScanner(path).Run();
	EXPECT_EQ(result.maxId, 7u);
	EXPECT_EQ(result.edgesRaw, 4u);
}

std::vector<Edge> ReadChunkPair(const std::string& path, uint64_t dataStart, uint64_t mid) {
	const uint64_t fileBytes = std::filesystem::file_size(path);
	std::vector<Edge> edges;
	Edge edge{};
	CsvEdgeStream left(path, false, dataStart, mid, false);
	while (left.Next(&edge)) {
		edges.push_back(edge);
	}
	CsvEdgeStream right(path, false, mid, fileBytes, false);
	while (right.Next(&edge)) {
		edges.push_back(edge);
	}
	return edges;
}

TEST(CsvChunk, EveryBoundarySplitYieldsSequentialEdges) {
	const std::string content = "# lead\nfrom,to\n1,2\n33,44\r\n5,6\n777,888";
	const auto path = WriteInput("chunk_bound.csv", content);
	const auto expected = ReadAll(path);
	ASSERT_EQ(expected.size(), 4u);
	const uint64_t dataStart = CsvEdgeStream::DataStartBytes(path);
	EXPECT_EQ(dataStart, 15u);
	for (uint64_t mid = dataStart; mid <= content.size(); ++mid) {
		EXPECT_EQ(ReadChunkPair(path, dataStart, mid), expected) << "mid=" << mid;
	}
}

TEST(CsvChunk, DataStartHandlesHeaderlessAndEmptyFiles) {
	EXPECT_EQ(CsvEdgeStream::DataStartBytes(WriteInput("ds_no_header.csv", "5,6\n7,8\n")), 0u);
	EXPECT_EQ(CsvEdgeStream::DataStartBytes(WriteInput("ds_empty.csv", "")), 0u);
	EXPECT_EQ(CsvEdgeStream::DataStartBytes(WriteInput("ds_only_comments.csv", "# a\n# b\n")), 8u);
}

TEST(CsvChunk, ChunkErrorCarriesByteOffset) {
	const auto path = WriteInput("chunk_error.csv", "1,2\n3,oops\n");
	CsvEdgeStream stream(path, false, 0, 4, true);
	Edge edge{};
	EXPECT_TRUE(stream.Next(&edge));
	CsvEdgeStream tail(path, false, 4, 11, false);
	try {
		while (tail.Next(&edge)) {
		}
		FAIL() << "expected exception";
	} catch (const lr::common::CsvChunkError& error) {
		EXPECT_EQ(error.OffsetBytes(), 4u);
	}
}

TEST(EdgeScanner, ChunkedScanMatchesSequential) {
	std::string content = "from,to\n";
	for (uint32_t i = 1; i <= 1'000; ++i) {
		content += std::format("{},{}\n", i, i % 97);
	}
	const auto path = WriteInput("chunk_scan.csv", content);
	const auto sequential = EdgeScanner(path).Run();
	lr::common::ThreadPool pool(4);
	const auto parallel = EdgeScanner(path, &pool).Run();
	EXPECT_EQ(parallel.maxId, sequential.maxId);
	EXPECT_EQ(parallel.edgesRaw, sequential.edgesRaw);
}

TEST(EdgeScanner, ParallelErrorReportsExactLine) {
	std::string content = "from,to\n";
	for (uint32_t i = 1; i <= 999; ++i) {
		content += i == 500 ? std::string("1,oops\n") : std::format("{},{}\n", i, i + 1);
	}
	const auto path = WriteInput("chunk_error_line.csv", content);
	lr::common::ThreadPool pool(4);
	try {
		EdgeScanner(path, &pool).Run();
		FAIL() << "expected exception";
	} catch (const std::runtime_error& error) {
		EXPECT_NE(std::string(error.what()).find(":501:"), std::string::npos) << error.what();
		EXPECT_NE(std::string(error.what()).find("not a number"), std::string::npos);
	}
}

} // namespace
