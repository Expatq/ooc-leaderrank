#pragma once

#include <cstdint>
#include <string_view>

namespace lr::common {

class MemoryBudget {
public:
	explicit MemoryBudget(uint64_t budgetBytes);

	void Reserve(std::string_view what, uint64_t bytes);
	uint64_t ReservedBytes() const;
	uint64_t LimitBytes() const;

private:
	uint64_t LimitBytes_;
	uint64_t ReservedBytes_;
};

} // namespace lr::common
