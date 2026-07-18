#pragma once

#include "partition.hpp"

#include <cstdint>
#include <filesystem>
#include <vector>

namespace lr::grid {

struct BlockRef {
	uint64_t offsetBytes;
	uint64_t edgeCount;
};

class BlockIndex {
public:
	explicit BlockIndex(const PartitionScheme& scheme);
	static BlockIndex Load(const std::filesystem::path& path, const PartitionScheme& scheme);

	void Save(const std::filesystem::path& path) const;
	const BlockRef& At(uint32_t srcInterval, uint32_t dstInterval) const;
	BlockRef* MutableAt(uint32_t srcInterval, uint32_t dstInterval);
	uint64_t SizeBytes() const;

private:
	PartitionScheme Scheme_;
	std::vector<BlockRef> Refs_;
};

} // namespace lr::grid
