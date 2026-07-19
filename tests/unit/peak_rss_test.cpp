#include <common/io/peak_rss.hpp>

#include <gtest/gtest.h>

#include <vector>

constexpr static size_t kMibBytes = size_t{1} << 20;
constexpr static size_t kPageStride = 4096;
constexpr static size_t kBlockMib = 64;
constexpr static int64_t kMinGrowthKib = 48 * 1024;

static void Escape(void* p) {
	asm volatile("" : : "g"(p) : "memory");
}

TEST(PeakRss, ReportsPositiveValue) {
	EXPECT_GT(lr::common::PeakRss::Kib(), 0);
}

TEST(PeakRss, GrowsAfterTouchingBlock) {
	const auto before = lr::common::PeakRss::Kib();
	ASSERT_GT(before, 0);

	std::vector<char> block(kBlockMib * kMibBytes);
	for (size_t i = 0; i < block.size(); i += kPageStride) {
		block[i] = 1;
	}

	Escape(block.data());
	const auto after = lr::common::PeakRss::Kib();

	EXPECT_GE(after - before, kMinGrowthKib);
}
