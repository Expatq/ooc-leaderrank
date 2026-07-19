#pragma once

#include <cstdint>
#include <filesystem>
#include <vector>

namespace lr::common {

class Bitmap {
public:
	explicit Bitmap(uint32_t bitCount);

	void Set(uint32_t bit);
	bool Test(uint32_t bit) const;
	uint64_t PopCount() const;
	void OrWith(const Bitmap& other);
	uint32_t BitCount() const { return BitCount_; }

	static Bitmap Load(const std::filesystem::path& path, uint32_t bitCount);
	void Save(const std::filesystem::path& path) const;

private:
	uint32_t BitCount_;
	std::vector<uint8_t> Bytes_;
};

} // namespace lr::common
