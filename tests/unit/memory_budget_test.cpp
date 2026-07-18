#include <common/budget/memory_budget.hpp>
#include <common/core/size_literals.hpp>

#include <gtest/gtest.h>

#include <stdexcept>

namespace {

using lr::common::MemoryBudget;

TEST(MemoryBudget, ReserveTracksAndThrowsOnOverflow) {
	MemoryBudget budget(100_MiB);
	budget.Reserve("window", 50_MiB);
	EXPECT_EQ(budget.ReservedBytes(), 50_MiB);
	EXPECT_THROW(budget.Reserve("accumulator", 40_MiB), std::runtime_error);
}

TEST(MemoryBudget, LimitIsUsableShareOfBudget) {
	const MemoryBudget budget(100_MiB);
	EXPECT_EQ(budget.LimitBytes(), 85_MiB);
}

} // namespace
