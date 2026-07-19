#include <common/io/bitmap.hpp>

#include <gtest/gtest.h>

#include <fstream>
#include <stdexcept>
#include <string>

namespace {

using lr::common::Bitmap;

TEST(Bitmap, SetTestPopCount) {
	Bitmap bitmap(10);
	bitmap.Set(1);
	bitmap.Set(2);
	bitmap.Set(9);
	EXPECT_TRUE(bitmap.Test(1));
	EXPECT_FALSE(bitmap.Test(0));
	EXPECT_TRUE(bitmap.Test(9));
	EXPECT_EQ(bitmap.PopCount(), 3u);
}

TEST(Bitmap, OrWithMerges) {
	Bitmap left(5);
	left.Set(0);
	Bitmap right(5);
	right.Set(4);
	left.OrWith(right);
	EXPECT_TRUE(left.Test(0));
	EXPECT_TRUE(left.Test(4));
	EXPECT_EQ(left.PopCount(), 2u);
}

TEST(Bitmap, OrWithDifferentSizesFails) {
	Bitmap left(5);
	Bitmap right(6);
	EXPECT_THROW(left.OrWith(right), std::runtime_error);
}

TEST(Bitmap, SaveLoadRoundTrip) {
	const std::string path = testing::TempDir() + "present.bin";
	Bitmap bitmap(12);
	bitmap.Set(3);
	bitmap.Set(11);
	bitmap.Save(path);
	const Bitmap loaded = Bitmap::Load(path, 12);
	EXPECT_TRUE(loaded.Test(3));
	EXPECT_TRUE(loaded.Test(11));
	EXPECT_EQ(loaded.PopCount(), 2u);
}

TEST(Bitmap, LoadSizeMismatchFails) {
	const std::string path = testing::TempDir() + "present_mismatch.bin";
	Bitmap bitmap(16);
	bitmap.Save(path);
	EXPECT_THROW(Bitmap::Load(path, 64), std::runtime_error);
}

TEST(Bitmap, MatchesFormatExampleByteLayout) {
	Bitmap first(3);
	first.Set(1);
	first.Set(2);
	const std::string firstPath = testing::TempDir() + "present0.bin";
	first.Save(firstPath);
	Bitmap second(2);
	second.Set(0);
	second.Set(1);
	const std::string secondPath = testing::TempDir() + "present1.bin";
	second.Save(secondPath);
	std::ifstream firstFile(firstPath, std::ios::binary);
	std::ifstream secondFile(secondPath, std::ios::binary);
	EXPECT_EQ(firstFile.get(), 0x06);
	EXPECT_EQ(secondFile.get(), 0x03);
}

} // namespace
