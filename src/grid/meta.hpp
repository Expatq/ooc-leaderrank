#pragma once

#include "layout.hpp"
#include "partition.hpp"

#include <cstdint>

namespace lr::grid {

class Meta {
public:
	static Meta Load(const WorkdirRoot& workdir);
	void Save(const WorkdirRoot& workdir) const;

	PartitionScheme Scheme() const;

	uint32_t transpose = 0;
	uint32_t threadsPlanned = 1;
	uint32_t maxId = 0;
	uint32_t intervalSize = 0;
	uint32_t partitions = 0;
	uint64_t vertices = 0;
	uint64_t edgesRaw = 0;
	uint64_t edges = 0;
	uint64_t droppedSelfLoops = 0;
	uint64_t droppedDuplicates = 0;
	uint32_t maxOutDegree = 0;
	uint32_t maxInDegree = 0;
};

} // namespace lr::grid
