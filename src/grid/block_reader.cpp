#include "block_reader.hpp"

#include <common/core/constants.hpp>

#include <algorithm>

namespace lr::grid {

BlockReader::BlockReader(const common::InputFile* blocks, uint64_t chunkBytes)
    : Blocks_(blocks), Chunk_(chunkBytes / common::kEdgeBytes), Ref_{0, 0}, DoneEdges_(0) {}

void BlockReader::Open(const BlockRef& ref) {
	Ref_ = ref;
	DoneEdges_ = 0;
}

bool BlockReader::Next(std::span<const common::Edge>* chunk) {
	if (DoneEdges_ >= Ref_.edgeCount) {
		return false;
	}
	const uint64_t portion = std::min<uint64_t>(Chunk_.size(), Ref_.edgeCount - DoneEdges_);
	const uint64_t offsetBytes = Ref_.offsetBytes + DoneEdges_ * common::kEdgeBytes;
	Blocks_->ReadAt(offsetBytes, Chunk_.data(), portion * common::kEdgeBytes);
	Blocks_->AdviseDontNeed(offsetBytes, portion * common::kEdgeBytes);
	DoneEdges_ += portion;
	*chunk = std::span<const common::Edge>(Chunk_.data(), portion);
	return true;
}

} // namespace lr::grid
