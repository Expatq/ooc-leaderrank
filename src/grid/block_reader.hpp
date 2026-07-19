#pragma once

#include "block_index.hpp"

#include <common/core/edge.hpp>
#include <common/io/aligned_io.hpp>

#include <cstdint>
#include <span>
#include <vector>

namespace lr::grid {

class BlockReader {
public:
	BlockReader(const common::InputFile* blocks, uint64_t chunkBytes);

	void Open(const BlockRef& ref);
	bool Next(std::span<const common::Edge>* chunk);

private:
	const common::InputFile* Blocks_;
	std::vector<common::Edge> Chunk_;
	BlockRef Ref_;
	uint64_t DoneEdges_;
};

} // namespace lr::grid
