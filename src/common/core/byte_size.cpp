#include "byte_size.hpp"

#include "size_literals.hpp"

#include <charconv>
#include <format>
#include <stdexcept>

namespace lr::common {

uint64_t ByteSize::Parse(std::string_view text) {
	uint64_t multiplier = 1_B;
	std::string_view digits = text;
	if (!digits.empty()) {
		switch (digits.back()) {
		case 'K':
		case 'k':
			multiplier = 1_KiB;
			break;
		case 'M':
		case 'm':
			multiplier = 1_MiB;
			break;
		case 'G':
		case 'g':
			multiplier = 1_GiB;
			break;
		default:
			break;
		}
	}
	if (multiplier != 1_B) {
		digits.remove_suffix(1);
	}
	uint64_t value = 0;
	const auto [parsedEnd, errorCode] =
	    std::from_chars(digits.data(), digits.data() + digits.size(), value);
	if (errorCode != std::errc{} || parsedEnd != digits.data() + digits.size() || value == 0) {
		throw std::runtime_error(std::format("invalid size: '{}'", text));
	}
	return value * multiplier;
}

} // namespace lr::common
