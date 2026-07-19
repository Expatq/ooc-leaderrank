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

void MergeRuns(const std::vector<common::Edge>& source, std::vector<common::Edge>* target, uint64_t begin, uint64_t middle, uint64_t end) {
	uint64_t left = begin;
	uint64_t right = middle;
	uint64_t out = begin;
	while (left < middle && right < end) {
		if (DstSrcLess(source[right], source[left])) {
			(*target)[out++] = source[right++];
		} else {
			(*target)[out++] = source[left++];
		}
	}
	while (left < middle) {
		(*target)[out++] = source[left++];
	}
	while (right < end) {
		(*target)[out++] = source[right++];
	}
}

uint32_t AlignDownToBitmapByte(uint32_t vertex) {
	return vertex & ~7u;
}

} // namespace

EdgeScatterer::EdgeScatterer(const grid::PartitionScheme& scheme, const grid::WorkdirRoot& workdir, uint64_t arenaBytes, common::ThreadPool* pool)
    : Scheme_(scheme), Workdir_(workdir), Pool_(pool), Mutexes_(scheme.BlockCount()) {
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

ScatterResult EdgeScatterer::Run(const std::filesystem::path& edgesPath, bool transpose) {
	std::filesystem::create_directories(Workdir_.TmpDir().Path());
	ScatterResult result{0, 0};
	if (Pool_->Threads() == 1) {
		common::CsvEdgeStream stream(edgesPath, transpose);
		result = ScatterStream(&stream);
	} else {
		const uint64_t fileBytes = common::InputFile(edgesPath).SizeBytes();
		const uint64_t dataStart = common::CsvEdgeStream::DataStartBytes(edgesPath);
		const uint64_t dataBytes = fileBytes - dataStart;
		const uint64_t chunkCount = std::max<uint64_t>(Pool_->Threads(), (dataBytes + common::kScanChunkBytes - 1) / common::kScanChunkBytes);
		std::vector<ScatterResult> partials(chunkCount, ScatterResult{0, 0});
		Pool_->ParallelFor(chunkCount, [this, &partials, &edgesPath, transpose, dataStart, dataBytes, chunkCount](uint32_t, uint64_t chunk) {
			const uint64_t begin = dataStart + dataBytes * chunk / chunkCount;
			const uint64_t end = dataStart + dataBytes * (chunk + 1) / chunkCount;
			common::CsvEdgeStream stream(edgesPath, transpose, begin, end, false);
			partials[chunk] = ScatterStream(&stream);
		});
		for (const ScatterResult& part : partials) {
			result.edgesWritten += part.edgesWritten;
			result.droppedSelfLoops += part.droppedSelfLoops;
		}
	}
	for (uint64_t block = 0; block < Counts_.size(); ++block) {
		const std::lock_guard lock(Mutexes_[block]);
		Flush(block);
	}
	return result;
}

ScatterResult EdgeScatterer::ScatterStream(common::CsvEdgeStream* input) {
	ScatterResult result{0, 0};
	common::Edge edge{};
	while (input->Next(&edge)) {
		if (edge.src == edge.dst) {
			++result.droppedSelfLoops;
			continue;
		}
		const uint64_t block = Scheme_.BlockPosition(Scheme_.IntervalOf(edge.src), Scheme_.IntervalOf(edge.dst));
		const std::lock_guard lock(Mutexes_[block]);
		Arena_[block * CapacityEdges_ + Counts_[block]] = edge;
		if (++Counts_[block] == CapacityEdges_) {
			Flush(block);
		}
		++result.edgesWritten;
	}
	return result;
}

void EdgeScatterer::Flush(uint64_t block) {
	if (Counts_[block] == 0) {
		return;
	}
	const uint32_t srcInterval = static_cast<uint32_t>(block % Scheme_.partitions);
	const uint32_t dstInterval = static_cast<uint32_t>(block / Scheme_.partitions);
	const std::filesystem::path blockPath = Workdir_.TmpDir().Block(srcInterval, dstInterval);
	common::AppendToFile(blockPath, Arena_.data() + block * CapacityEdges_, Counts_[block] * common::kEdgeBytes);
	Counts_[block] = 0;
}

BlockAssembler::BlockAssembler(const grid::PartitionScheme& scheme, const grid::WorkdirRoot& workdir, uint64_t arenaBytes, common::ThreadPool* pool)
    : Scheme_(scheme), Workdir_(workdir), Pool_(pool) {
	const uint64_t inDegreeBytes = common::kDegreeBytesPerVertex * uint64_t{scheme.intervalSize};
	if (arenaBytes <= inDegreeBytes) {
		throw std::runtime_error("prepare budget is smaller than the in-degree accumulator: "
		                         "increase --budget");
	}
	SortCapacityEdges_ = (arenaBytes - inDegreeBytes) / 2 / common::kEdgeBytes;
	if (SortCapacityEdges_ == 0) {
		throw std::runtime_error("prepare budget is smaller than the sort arena: increase --budget");
	}
	Arena_.reserve(SortCapacityEdges_);
	Aux_.reserve(SortCapacityEdges_);
	InDegrees_.assign(scheme.intervalSize, 0);
}

AssembleResult BlockAssembler::Run() {
	common::OutputFile blocksBin(Workdir_.BlocksBin());
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
			const common::InputFile tmpFile(tmpPath);
			const uint64_t rawCount = tmpFile.SizeBytes() / common::kEdgeBytes;
			if (rawCount * common::kEdgeBytes != tmpFile.SizeBytes()) {
				throw std::runtime_error(std::format("{}: size is not a multiple of the edge size", tmpPath.string()));
			}
			if (rawCount > SortCapacityEdges_) {
				throw std::runtime_error(
				    std::format("block ({}, {}) with {} edges does not fit into prepare memory: "
				                "increase --budget",
				                srcInterval, dstInterval, rawCount));
			}
			Arena_.resize(rawCount);
			tmpFile.ReadAt(0, Arena_.data(), tmpFile.SizeBytes());
			tmpFile.AdviseDontNeed(0, tmpFile.SizeBytes());
			SortBlock(rawCount);
			const auto uniqueEnd = std::unique(Arena_.begin(), Arena_.begin() + static_cast<int64_t>(rawCount));
			const uint64_t uniqueCount = static_cast<uint64_t>(uniqueEnd - Arena_.begin());
			result.droppedDuplicates += rawCount - uniqueCount;

			CountColumnEdges(uniqueCount, columnBase, columnLength, &dstPresent);
			blocksBin.Append(Arena_.data(), uniqueCount * common::kEdgeBytes);
			ref->edgeCount = uniqueCount;
			std::filesystem::remove(tmpPath);
		}

		for (uint32_t local = 0; local < columnLength; ++local) {
			result.maxInDegree = std::max(result.maxInDegree, InDegrees_[local]);
		}
		dstPresent.Save(Workdir_.PresentDir().Present(dstInterval));
	}

	index.Save(Workdir_);
	std::filesystem::remove_all(tmp.Path());
	return result;
}

void BlockAssembler::SortBlock(uint64_t edgeCount) {
	const uint32_t threads = Pool_->Threads();
	if (edgeCount < common::kParallelSortMinEdges || threads == 1) {
		std::sort(Arena_.begin(), Arena_.begin() + static_cast<int64_t>(edgeCount), DstSrcLess);
		return;
	}
	Runs_.clear();
	for (uint32_t run = 0; run <= threads; ++run) {
		Runs_.push_back(edgeCount * run / threads);
	}
	Pool_->ParallelFor(threads, [this](uint32_t, uint64_t run) {
		std::sort(Arena_.begin() + static_cast<int64_t>(Runs_[run]), Arena_.begin() + static_cast<int64_t>(Runs_[run + 1]), DstSrcLess);
	});

	Aux_.resize(edgeCount);
	std::vector<common::Edge>* source = &Arena_;
	std::vector<common::Edge>* target = &Aux_;
	while (Runs_.size() > 2) {
		const uint64_t pairCount = (Runs_.size() - 1) / 2;
		Pool_->ParallelFor(pairCount, [this, source, target](uint32_t, uint64_t pair) {
			MergeRuns(*source, target, Runs_[2 * pair], Runs_[2 * pair + 1], Runs_[2 * pair + 2]);
		});
		std::vector<uint64_t> mergedRuns;
		for (uint64_t boundary = 0; boundary < Runs_.size(); boundary += 2) {
			mergedRuns.push_back(Runs_[boundary]);
		}
		if ((Runs_.size() - 1) % 2 == 1) {
			std::copy(source->begin() + static_cast<int64_t>(Runs_[Runs_.size() - 2]),
			          source->begin() + static_cast<int64_t>(Runs_.back()),
			          target->begin() + static_cast<int64_t>(Runs_[Runs_.size() - 2]));
			if (mergedRuns.back() != Runs_.back()) {
				mergedRuns.push_back(Runs_.back());
			}
		}
		Runs_ = std::move(mergedRuns);
		std::swap(source, target);
	}
	if (source != &Arena_) {
		std::copy(source->begin(), source->begin() + static_cast<int64_t>(edgeCount), Arena_.begin());
	}
}

void BlockAssembler::CountColumnEdges(uint64_t uniqueCount, uint32_t columnBase, uint32_t columnLength, common::Bitmap* dstPresent) {
	const uint32_t threads = Pool_->Threads();
	Pool_->ParallelFor(threads, [this, uniqueCount, columnBase, columnLength, dstPresent, threads](uint32_t, uint64_t part) {
		const uint32_t lowLocal = AlignDownToBitmapByte(static_cast<uint32_t>(uint64_t{columnLength} * part / threads));
		const uint32_t highLocal = part + 1 == threads ? columnLength : AlignDownToBitmapByte(static_cast<uint32_t>(uint64_t{columnLength} * (part + 1) / threads));
		if (lowLocal >= highLocal) {
			return;
		}
		const auto lessByDst = [](const common::Edge& edge, uint32_t dst) {
			return edge.dst < dst;
		};
		const auto begin = std::lower_bound(Arena_.begin(), Arena_.begin() + static_cast<int64_t>(uniqueCount), columnBase + lowLocal, lessByDst);
		const auto end = std::lower_bound(begin, Arena_.begin() + static_cast<int64_t>(uniqueCount), columnBase + highLocal, lessByDst);
		for (auto it = begin; it != end; ++it) {
			const uint32_t local = it->dst - columnBase;
			++InDegrees_[local];
			dstPresent->Set(local);
		}
	});
}

DegreeBuilder::DegreeBuilder(const grid::PartitionScheme& scheme, const grid::WorkdirRoot& workdir, uint64_t chunkBytes, common::ThreadPool* pool)
    : Scheme_(scheme), Workdir_(workdir), ChunkBytes_(chunkBytes), Pool_(pool) {
	OutDegrees_.assign(scheme.intervalSize, 0);
}

DegreesResult DegreeBuilder::Run() {
	const common::InputFile blocksBin(Workdir_.BlocksBin());
	const grid::BlockIndex index = grid::BlockIndex::Load(Scheme_, Workdir_);
	grid::BlockReader reader(&blocksBin, ChunkBytes_);
	const uint32_t threads = Pool_->Threads();

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
				Pool_->ParallelFor(threads, [this, chunk, rowBase, rowLength, threads, &srcPresent](uint32_t, uint64_t part) {
					const uint32_t lowLocal = AlignDownToBitmapByte(static_cast<uint32_t>(uint64_t{rowLength} * part / threads));
					const uint32_t highLocal = part + 1 == threads ? rowLength : AlignDownToBitmapByte(static_cast<uint32_t>(uint64_t{rowLength} * (part + 1) / threads));
					if (lowLocal >= highLocal) {
						return;
					}
					for (const common::Edge& edge : chunk) {
						const uint32_t local = edge.src - rowBase;
						if (local >= lowLocal && local < highLocal) {
							++OutDegrees_[local];
							srcPresent.Set(local);
						}
					}
				});
			}
		}

		const std::filesystem::path presentPath = Workdir_.PresentDir().Present(srcInterval);
		common::Bitmap present = common::Bitmap::Load(presentPath, rowLength);
		present.OrWith(srcPresent);
		present.Save(presentPath);
		result.vertices += present.PopCount();

		for (uint32_t local = 0; local < rowLength; ++local) {
			result.maxOutDegree = std::max(result.maxOutDegree, OutDegrees_[local]);
		}
		common::OutputFile degFile(Workdir_.DegreesDir().Degrees(srcInterval));
		degFile.Append(OutDegrees_.data(), uint64_t{rowLength} * common::kDegreeBytesPerVertex);
	}

	if (result.vertices == 0) {
		throw std::runtime_error("graph is empty after dropping self-loops and duplicates");
	}
	return result;
}

} // namespace lr::prepare
