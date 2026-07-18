#include "bitmap.hpp"

#include "aligned_io.hpp"

#include <bit>
#include <format>
#include <stdexcept>

namespace lr::common {

namespace {

constexpr uint32_t kBitsPerByte = 8;

} // namespace

Bitmap::Bitmap(uint32_t bitCount)
    : BitCount_(bitCount), Bytes_((bitCount + kBitsPerByte - 1) / kBitsPerByte, 0) {}

void Bitmap::Set(uint32_t bit) {
	Bytes_[bit / kBitsPerByte] |= static_cast<uint8_t>(1u << (bit % kBitsPerByte));
}

bool Bitmap::Test(uint32_t bit) const {
	return (Bytes_[bit / kBitsPerByte] >> (bit % kBitsPerByte)) & 1u;
}

uint64_t Bitmap::PopCount() const {
	uint64_t total = 0;
	for (const uint8_t byte : Bytes_) {
		total += static_cast<uint64_t>(std::popcount(byte));
	}
	return total;
}

void Bitmap::OrWith(const Bitmap& other) {
	if (other.BitCount_ != BitCount_) {
		throw std::runtime_error(std::format("OR of bitmaps with different lengths: {} and {}",
		                                     BitCount_, other.BitCount_));
	}
	for (size_t i = 0; i < Bytes_.size(); ++i) {
		Bytes_[i] |= other.Bytes_[i];
	}
}

Bitmap Bitmap::Load(const std::string& path, uint32_t bitCount) {
	Bitmap bitmap(bitCount);
	const InputFile file(path);
	if (file.SizeBytes() != bitmap.Bytes_.size()) {
		throw std::runtime_error(std::format("{}: size is {} bytes, expected {}", path,
		                                     file.SizeBytes(), bitmap.Bytes_.size()));
	}
	file.ReadAt(0, bitmap.Bytes_.data(), bitmap.Bytes_.size());
	return bitmap;
}

void Bitmap::Save(const std::string& path) const {
	OutputFile file(path);
	file.Append(Bytes_.data(), Bytes_.size());
}

} // namespace lr::common
