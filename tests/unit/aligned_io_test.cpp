#include <common/core/constants.hpp>
#include <common/io/aligned_io.hpp>

#include <gtest/gtest.h>

#include <filesystem>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

using lr::common::AppendToFile;
using lr::common::InputFile;
using lr::common::OutputFile;
using lr::common::RandomAccessFile;

TEST(OutputFile, AppendAndPadProduceAlignedLayout) {
	const std::string path = testing::TempDir() + "out.bin";
	{
		OutputFile file(path);
		const std::string payload = "abcdefgh";
		file.Append(payload.data(), payload.size());
		file.PadToAlignment(lr::common::kBlockAlignBytes);
		EXPECT_EQ(file.OffsetBytes(), lr::common::kBlockAlignBytes);
		file.Append(payload.data(), payload.size());
	}
	const InputFile input(path);
	EXPECT_EQ(input.SizeBytes(), lr::common::kBlockAlignBytes + 8);
	std::vector<char> tail(8);
	input.ReadAt(lr::common::kBlockAlignBytes, tail.data(), tail.size());
	EXPECT_EQ(std::string(tail.begin(), tail.end()), "abcdefgh");
	std::vector<char> pad(8);
	input.ReadAt(8, pad.data(), pad.size());
	EXPECT_EQ(std::string(pad.begin(), pad.end()), std::string(8, '\0'));
}

TEST(InputFile, ReadPastEndFails) {
	const std::string path = testing::TempDir() + "short.bin";
	{
		OutputFile file(path);
		file.Append("xy", 2);
	}
	const InputFile input(path);
	std::vector<char> buffer(8);
	EXPECT_THROW(input.ReadAt(0, buffer.data(), buffer.size()), std::runtime_error);
}

TEST(RandomAccessFile, ReadsBackWrites) {
	const std::string path = testing::TempDir() + "rank.bin";
	RandomAccessFile file(path, 64);
	const double value = 0.25;
	file.WriteAt(16, &value, sizeof(value));
	double got = 0;
	file.ReadAt(16, &got, sizeof(got));
	EXPECT_EQ(got, value);
	double zero = 1;
	file.ReadAt(0, &zero, sizeof(zero));
	EXPECT_EQ(zero, 0.0);
}

TEST(AppendToFile, AccumulatesAcrossCalls) {
	const std::string path = testing::TempDir() + "append.bin";
	std::filesystem::remove(path);
	AppendToFile(path, "one", 3);
	AppendToFile(path, "two", 3);
	const InputFile input(path);
	EXPECT_EQ(input.SizeBytes(), 6u);
	std::vector<char> data(6);
	input.ReadAt(0, data.data(), data.size());
	EXPECT_EQ(std::string(data.begin(), data.end()), "onetwo");
}

TEST(InputFile, MissingFileFails) {
	EXPECT_THROW(InputFile("/nonexistent/blocks.bin"), std::runtime_error);
}

} // namespace
