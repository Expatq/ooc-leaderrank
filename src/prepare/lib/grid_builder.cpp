#include "grid_builder.hpp"

#include <common/core/constants.hpp>
#include <common/io/aligned_io.hpp>
#include <grid/block_index.hpp>
#include <grid/block_reader.hpp>

#include <algorithm>
#include <filesystem>
#include <format>
#include <stdexcept>

namespace lr::prepare {

namespace {

bool DstSrcLess(const common::Edge& left, const common::Edge& right) {
	if (left.dst != right.dst) {
		return left.dst < right.dst;
	}
	return left.src < right.src;
}

} // namespace

EdgeScatterer::EdgeScatterer(const grid::PartitionScheme& scheme, const grid::WorkdirRoot& workdir,
                             uint64_t arenaBytes)
    : Scheme_(scheme), Tmp_(workdir.TmpDir()) {
	const uint64_t blocks = scheme.BlockCount();
	const uint64_t bufferBytes = arenaBytes / blocks;
	if (bufferBytes < common::kScatterMinBufferBytes) {
		throw std::runtime_error(
		    std::format("scatter buffer of {} bytes per block is below the minimum of {}: "
		                "increase --budget",
		                bufferBytes, common::kScatterMinBufferBytes));
	}
	CapacityEdges_ = bufferBytes / common::kEdgeBytes;
	Arena_.resize(CapacityEdges_ * blocks);
	Counts_.assign(blocks, 0);
}

ScatterResult EdgeScatterer::Run(common::CsvEdgeStream* input) {
	std::filesystem::create_directories(Tmp_.Path());
	ScatterResult result{0, 0};
	common::Edge edge{};
	while (input->Next(&edge)) {
		if (edge.src == edge.dst) {
			++result.droppedSelfLoops;
			continue;
		}
		const uint32_t block =
		    Scheme_.IntervalOf(edge.src) * Scheme_.partitions + Scheme_.IntervalOf(edge.dst);
		Arena_[block * CapacityEdges_ + Counts_[block]] = edge;
		if (++Counts_[block] == CapacityEdges_) {
			Flush(block);
		}
		++result.edgesWritten;
	}
	for (uint32_t block = 0; block < Counts_.size(); ++block) {
		Flush(block);
	}
	return result;
}

void EdgeScatterer::Flush(uint32_t block) {
	if (Counts_[block] == 0) {
		return;
	}
	const uint32_t srcInterval = block / Scheme_.partitions;
	const uint32_t dstInterval = block % Scheme_.partitions;
	common::AppendToFile(Tmp_.Block(srcInterval, dstInterval).string(),
	                     Arena_.data() + block * CapacityEdges_,
	                     Counts_[block] * common::kEdgeBytes);
	Counts_[block] = 0;
}

BlockAssembler::BlockAssembler(const grid::PartitionScheme& scheme,
                               const grid::WorkdirRoot& workdir, uint64_t arenaBytes)
    : Scheme_(scheme), Workdir_(workdir) {
	const uint64_t inDegreeBytes = common::kDegreeBytesPerVertex * uint64_t{scheme.intervalSize};
	if (arenaBytes <= inDegreeBytes) {
		throw std::runtime_error("prepare budget is smaller than the in-degree accumulator: "
		                         "increase --budget");
	}
	SortCapacityEdges_ = (arenaBytes - inDegreeBytes) / common::kEdgeBytes;
	SortBuffer_.reserve(SortCapacityEdges_);
	InDegrees_.assign(scheme.intervalSize, 0);
}

AssembleResult BlockAssembler::Run() {
	common::OutputFile blocksBin(Workdir_.BlocksBin().string());
	grid::BlockIndex index(Scheme_);
	const grid::TmpDir tmp = Workdir_.TmpDir();
	AssembleResult result{0, 0};

	for (uint32_t dstInterval = 0; dstInterval < Scheme_.partitions; ++dstInterval) {
		const uint32_t columnLength = Scheme_.IntervalLength(dstInterval);
		const uint32_t columnBase = Scheme_.IntervalBase(dstInterval);
		common::Bitmap dstPresent(columnLength);
		std::fill_n(InDegrees_.begin(), columnLength, 0);

		for (uint32_t srcInterval = 0; srcInterval < Scheme_.partitions; ++srcInterval) {
			blocksBin.PadToAlignment(common::kBlockAlignBytes);
			grid::BlockRef* ref = index.MutableAt(srcInterval, dstInterval);
			ref->offsetBytes = blocksBin.OffsetBytes();
			ref->edgeCount = 0;

			const std::filesystem::path tmpPath = tmp.Block(srcInterval, dstInterval);
			if (!std::filesystem::exists(tmpPath)) {
				continue;
			}
			const common::InputFile tmpFile(tmpPath.string());
			const uint64_t rawCount = tmpFile.SizeBytes() / common::kEdgeBytes;
			if (rawCount * common::kEdgeBytes != tmpFile.SizeBytes()) {
				throw std::runtime_error(
				    std::format("{}: size is not a multiple of the edge size", tmpPath.string()));
			}
			if (rawCount > SortCapacityEdges_) {
				throw std::runtime_error(
				    std::format("block ({}, {}) with {} edges does not fit into prepare memory: "
				                "increase --budget",
				                srcInterval, dstInterval, rawCount));
			}
			SortBuffer_.resize(rawCount);
			tmpFile.ReadAt(0, SortBuffer_.data(), tmpFile.SizeBytes());
			tmpFile.AdviseDontNeed(0, tmpFile.SizeBytes());
			std::sort(SortBuffer_.begin(), SortBuffer_.end(), DstSrcLess);
			const auto uniqueEnd = std::unique(SortBuffer_.begin(), SortBuffer_.end());
			const uint64_t uniqueCount = static_cast<uint64_t>(uniqueEnd - SortBuffer_.begin());
			result.droppedDuplicates += rawCount - uniqueCount;

			for (uint64_t i = 0; i < uniqueCount; ++i) {
				const uint32_t local = SortBuffer_[i].dst - columnBase;
				++InDegrees_[local];
				dstPresent.Set(local);
			}
			blocksBin.Append(SortBuffer_.data(), uniqueCount * common::kEdgeBytes);
			ref->edgeCount = uniqueCount;
			std::filesystem::remove(tmpPath);
		}

		for (uint32_t local = 0; local < columnLength; ++local) {
			result.maxInDegree = std::max(result.maxInDegree, InDegrees_[local]);
		}
		dstPresent.Save(Workdir_.PresentDir().Present(dstInterval).string());
	}

	index.Save(Workdir_.BlocksIdx());
	std::filesystem::remove_all(tmp.Path());
	return result;
}

DegreeBuilder::DegreeBuilder(const grid::PartitionScheme& scheme, const grid::WorkdirRoot& workdir)
    : Scheme_(scheme), Workdir_(workdir) {
	OutDegrees_.assign(scheme.intervalSize, 0);
}

DegreesResult DegreeBuilder::Run() {
	const common::InputFile blocksBin(Workdir_.BlocksBin().string());
	const grid::BlockIndex index = grid::BlockIndex::Load(Workdir_.BlocksIdx(), Scheme_);
	grid::BlockReader reader(&blocksBin, common::kMinIoChunkBytes);

	DegreesResult result{0, 0};
	for (uint32_t srcInterval = 0; srcInterval < Scheme_.partitions; ++srcInterval) {
		const uint32_t rowLength = Scheme_.IntervalLength(srcInterval);
		const uint32_t rowBase = Scheme_.IntervalBase(srcInterval);
		std::fill_n(OutDegrees_.begin(), rowLength, 0);
		common::Bitmap srcPresent(rowLength);

		for (uint32_t dstInterval = 0; dstInterval < Scheme_.partitions; ++dstInterval) {
			reader.Open(index.At(srcInterval, dstInterval));
			std::span<const common::Edge> chunk;
			while (reader.Next(&chunk)) {
				for (const common::Edge& edge : chunk) {
					const uint32_t local = edge.src - rowBase;
					++OutDegrees_[local];
					srcPresent.Set(local);
				}
			}
		}

		const std::string presentPath = Workdir_.PresentDir().Present(srcInterval).string();
		common::Bitmap present = common::Bitmap::Load(presentPath, rowLength);
		present.OrWith(srcPresent);
		present.Save(presentPath);
		result.vertices += present.PopCount();

		for (uint32_t local = 0; local < rowLength; ++local) {
			result.maxOutDegree = std::max(result.maxOutDegree, OutDegrees_[local]);
		}
		common::OutputFile degFile(Workdir_.DegreesDir().Degrees(srcInterval).string());
		degFile.Append(OutDegrees_.data(), uint64_t{rowLength} * common::kDegreeBytesPerVertex);
	}

	if (result.vertices == 0) {
		throw std::runtime_error("graph is empty after dropping self-loops and duplicates");
	}
	return result;
}

} // namespace lr::prepare
