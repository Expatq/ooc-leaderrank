#include "memory_budget.hpp"

#include <common/core/constants.hpp>

#include <format>
#include <stdexcept>

namespace lr::common {

MemoryBudget::MemoryBudget(uint64_t budgetBytes)
    : LimitBytes_(kUsableNumerator * budgetBytes / kUsableDenominator), ReservedBytes_(0) {}

void MemoryBudget::Reserve(std::string_view what, uint64_t bytes) {
	if (ReservedBytes_ + bytes > LimitBytes_) {
		throw std::runtime_error(
		    std::format("memory budget exceeded: {} requires {} bytes, {} of {} already reserved",
		                what, bytes, ReservedBytes_, LimitBytes_));
	}
	ReservedBytes_ += bytes;
}

uint64_t MemoryBudget::ReservedBytes() const {
	return ReservedBytes_;
}

uint64_t MemoryBudget::LimitBytes() const {
	return LimitBytes_;
}

} // namespace lr::common
