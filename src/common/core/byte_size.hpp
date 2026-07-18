#pragma once

#include <cstdint>
#include <string_view>

namespace lr::common {

class ByteSize {
public:
	static uint64_t Parse(std::string_view text);
};

} // namespace lr::common
