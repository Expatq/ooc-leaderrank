#include "layout.hpp"

#include <format>
#include <utility>

namespace lr::grid {

namespace {

constexpr std::string_view kDegreesDirName = "deg";
constexpr std::string_view kPresentDirName = "present";
constexpr std::string_view kTmpDirName = "tmp";
constexpr std::string_view kBlocksBinName = "blocks.bin";
constexpr std::string_view kBlocksIdxName = "blocks.idx";
constexpr std::string_view kMetaFileName = "meta";

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

WorkdirLayout::WorkdirLayout(std::filesystem::path workdir) : Workdir_(std::move(workdir)) {}

WorkdirRoot WorkdirLayout::Root() const {
	return WorkdirRoot(Workdir_);
}

} // namespace lr::grid
