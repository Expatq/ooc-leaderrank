#pragma once

#include <cstdint>
#include <filesystem>

namespace lr::grid {

class PartitionScheme;

class Directory {
public:
	explicit Directory(std::filesystem::path path);

	const std::filesystem::path& Path() const;

private:
	std::filesystem::path Path_;
};

class DegreesDir : public Directory {
public:
	using Directory::Directory;

	std::filesystem::path Degrees(uint32_t interval) const;
};

class PresentDir : public Directory {
public:
	using Directory::Directory;

	std::filesystem::path Present(uint32_t interval) const;
};

class TmpDir : public Directory {
public:
	using Directory::Directory;

	std::filesystem::path Block(uint32_t srcInterval, uint32_t dstInterval) const;
};

class WorkdirRoot : public Directory {
public:
	using Directory::Directory;

	DegreesDir DegreesDir() const;
	PresentDir PresentDir() const;
	TmpDir TmpDir() const;
	std::filesystem::path BlocksBin() const;
	std::filesystem::path BlocksIdx() const;
	std::filesystem::path MetaFile() const;
	std::filesystem::path RankFile(uint32_t side) const;
	void Validate(const PartitionScheme& scheme) const;
};

} // namespace lr::grid
