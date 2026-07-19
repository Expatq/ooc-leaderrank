#pragma once

#include <compare>
#include <cstdint>

namespace lr::common {

struct Edge {
	uint32_t src;
	uint32_t dst;

	auto operator<=>(const Edge&) const = default;
};

} // namespace lr::common
