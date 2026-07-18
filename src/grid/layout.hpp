#pragma once

#include <cstdint>
#include <filesystem>

namespace lr::grid {

class DegreesDir {
public:
	explicit DegreesDir(std::filesystem::path path);

	const std::filesystem::path& Path() const;
	std::filesystem::path Degrees(uint32_t interval) const;

private:
	std::filesystem::path Path_;
};

class PresentDir {
public:
	explicit PresentDir(std::filesystem::path path);

	const std::filesystem::path& Path() const;
	std::filesystem::path Present(uint32_t interval) const;

private:
	std::filesystem::path Path_;
};

class TmpDir {
public:
	explicit TmpDir(std::filesystem::path path);

	const std::filesystem::path& Path() const;
	std::filesystem::path Block(uint32_t srcInterval, uint32_t dstInterval) const;

private:
	std::filesystem::path Path_;
};

class WorkdirRoot {
public:
	explicit WorkdirRoot(std::filesystem::path path);

	const std::filesystem::path& Path() const;
	DegreesDir DegreesDir() const;
	PresentDir PresentDir() const;
	TmpDir TmpDir() const;
	std::filesystem::path BlocksBin() const;
	std::filesystem::path BlocksIdx() const;
	std::filesystem::path MetaFile() const;
	std::filesystem::path RankFile(uint32_t side) const;

private:
	std::filesystem::path Path_;
};

class WorkdirLayout {
public:
	explicit WorkdirLayout(std::filesystem::path workdir);

	WorkdirRoot Root() const;

private:
	std::filesystem::path Workdir_;
};

} // namespace lr::grid
