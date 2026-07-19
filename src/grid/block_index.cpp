#include "block_index.hpp"

#include <common/io/aligned_io.hpp>

#include <format>
#include <stdexcept>

namespace lr::grid {

BlockIndex::BlockIndex(const PartitionScheme& scheme)
    : Scheme_(scheme), Refs_(scheme.BlockCount(), BlockRef{0, 0}) {}

BlockIndex BlockIndex::Load(const PartitionScheme& scheme, const WorkdirRoot& root) {
	BlockIndex index(scheme);
	const std::filesystem::path path = root.BlocksIdx();
	const common::InputFile file(path);
	if (file.SizeBytes() != index.SizeBytes()) {
		throw std::runtime_error(std::format("{}: size is {} bytes, expected {}", path.string(), file.SizeBytes(), index.SizeBytes()));
	}
	file.ReadAt(0, index.Refs_.data(), index.Refs_.size() * sizeof(BlockRef));
	return index;
}

void BlockIndex::Save(const WorkdirRoot& root) const {
	common::OutputFile file(root.BlocksIdx());
	file.Append(Refs_.data(), Refs_.size() * sizeof(BlockRef));
}

const BlockRef& BlockIndex::At(uint32_t srcInterval, uint32_t dstInterval) const {
	return Refs_[Scheme_.BlockPosition(srcInterval, dstInterval)];
}

BlockRef* BlockIndex::MutableAt(uint32_t srcInterval, uint32_t dstInterval) {
	return &Refs_[Scheme_.BlockPosition(srcInterval, dstInterval)];
}

uint64_t BlockIndex::SizeBytes() const {
	return Refs_.size() * sizeof(BlockRef);
}

} // namespace lr::grid
