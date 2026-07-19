#pragma once

#include <cstdint>

constexpr uint64_t operator""_B(unsigned long long value) noexcept {
	return value;
}

constexpr uint64_t operator""_KiB(unsigned long long value) noexcept {
	return value * 1024_B;
}

constexpr uint64_t operator""_MiB(unsigned long long value) noexcept {
	return value * 1024_KiB;
}

constexpr uint64_t operator""_GiB(unsigned long long value) noexcept {
	return value * 1024_MiB;
}

constexpr uint64_t operator""_TiB(unsigned long long value) noexcept {
	return value * 1024_GiB;
}
