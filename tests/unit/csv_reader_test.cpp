#include <common/csv/csv_reader.hpp>

#include <gtest/gtest.h>

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

} // namespace
