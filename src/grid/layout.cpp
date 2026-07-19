#include "layout.hpp"

#include "block_index.hpp"
#include "partition.hpp"

#include <common/core/constants.hpp>
#include <common/io/aligned_io.hpp>

#include <algorithm>
#include <format>
#include <stdexcept>
#include <utility>

namespace lr::grid {

namespace {

constexpr static std::string_view kDegreesDirName = "deg";
constexpr static std::string_view kPresentDirName = "present";
constexpr static std::string_view kTmpDirName = "tmp";
constexpr static std::string_view kBlocksBinName = "blocks.bin";
constexpr static std::string_view kBlocksIdxName = "blocks.idx";
constexpr static std::string_view kMetaFileName = "meta";

} // namespace

Directory::Directory(std::filesystem::path path) : Path_(std::move(path)) {}

const std::filesystem::path& Directory::Path() const {
	return Path_;
}

std::filesystem::path DegreesDir::Degrees(uint32_t interval) const {
	return Path() / std::format("{}.bin", interval);
}

std::filesystem::path PresentDir::Present(uint32_t interval) const {
	return Path() / std::format("{}.bin", interval);
}

std::filesystem::path TmpDir::Block(uint32_t srcInterval, uint32_t dstInterval) const {
	return Path() / std::format("b_{}_{}.raw", srcInterval, dstInterval);
}

DegreesDir WorkdirRoot::DegreesDir() const {
	return grid::DegreesDir(Path() / kDegreesDirName);
}

PresentDir WorkdirRoot::PresentDir() const {
	return grid::PresentDir(Path() / kPresentDirName);
}

TmpDir WorkdirRoot::TmpDir() const {
	return grid::TmpDir(Path() / kTmpDirName);
}

std::filesystem::path WorkdirRoot::BlocksBin() const {
	return Path() / kBlocksBinName;
}

std::filesystem::path WorkdirRoot::BlocksIdx() const {
	return Path() / kBlocksIdxName;
}

std::filesystem::path WorkdirRoot::MetaFile() const {
	return Path() / kMetaFileName;
}

std::filesystem::path WorkdirRoot::RankFile(uint32_t side) const {
	return Path() / std::format("rank_{}.bin", side == 0 ? "a" : "b");
}

void WorkdirRoot::Validate(const PartitionScheme& scheme) const {
	const BlockIndex index = BlockIndex::Load(scheme, *this);
	uint64_t blocksBinNeedBytes = 0;
	for (uint32_t srcInterval = 0; srcInterval < scheme.partitions; ++srcInterval) {
		for (uint32_t dstInterval = 0; dstInterval < scheme.partitions; ++dstInterval) {
			const BlockRef& ref = index.At(srcInterval, dstInterval);
			blocksBinNeedBytes = std::max(blocksBinNeedBytes, ref.offsetBytes + ref.edgeCount * common::kEdgeBytes);
		}
	}
	const common::InputFile blocksBin(BlocksBin());
	if (blocksBin.SizeBytes() < blocksBinNeedBytes) {
		throw std::runtime_error(
		    std::format("{}: size is {} bytes, blocks.idx expects at least {}",
		                BlocksBin().string(), blocksBin.SizeBytes(), blocksBinNeedBytes));
	}
	for (uint32_t interval = 0; interval < scheme.partitions; ++interval) {
		const std::filesystem::path degPath = DegreesDir().Degrees(interval);
		const common::InputFile degFile(degPath);
		const uint64_t expectedBytes = uint64_t{scheme.IntervalLength(interval)} * common::kDegreeBytesPerVertex;
		if (degFile.SizeBytes() != expectedBytes) {
			throw std::runtime_error(
			    std::format("{}: size is {} bytes, expected {}",
			                degPath.string(), degFile.SizeBytes(), expectedBytes));
		}
	}
}

} // namespace lr::grid
