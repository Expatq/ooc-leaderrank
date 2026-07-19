#pragma once

#include <common/core/edge.hpp>

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>

namespace lr::common {

class CsvEdgeStream {
public:
	CsvEdgeStream(const std::filesystem::path& path, bool transpose);

	bool Next(Edge* out);

private:
	void ParseDataLine(std::string_view line, Edge* out) const;
	uint32_t ParseId(std::string_view token) const;
	[[noreturn]] void Fail(std::string_view reason) const;

	std::ifstream Input_;
	std::string Path_;
	std::string Line_;
	uint64_t LineNo_;
	bool HeaderChecked_;
	bool Transpose_;
};

struct ScanResult {
	uint32_t maxId;
	uint64_t edgesRaw;
};

class EdgeScanner {
public:
	explicit EdgeScanner(const std::filesystem::path& path);

	ScanResult Run();

private:
	std::filesystem::path Path_;
};

} // namespace lr::common
