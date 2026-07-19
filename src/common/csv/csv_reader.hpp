#pragma once

#include <common/core/edge.hpp>
#include <common/io/aligned_io.hpp>

#include <cstdint>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace lr::common {

class ThreadPool;

class CsvChunkError : public std::runtime_error {
public:
	CsvChunkError(uint64_t offsetBytes, std::string reason);

	uint64_t OffsetBytes() const;
	const std::string& Reason() const;

private:
	uint64_t OffsetBytes_;
	std::string Reason_;
};

class CsvEdgeStream {
public:
	CsvEdgeStream(const std::filesystem::path& path, bool transpose);
	CsvEdgeStream(const std::filesystem::path& path, bool transpose, uint64_t beginBytes, uint64_t endBytes, bool firstChunk);

	static uint64_t DataStartBytes(const std::filesystem::path& path);

	bool Next(Edge* out);

private:
	bool NextLine(std::string_view* line);
	void Refill();
	bool FileExhausted() const;
	void AlignToOwnedLine();
	void ParseDataLine(std::string_view line, Edge* out) const;
	uint32_t ParseId(std::string_view token) const;
	[[noreturn]] void Fail(std::string_view reason) const;

	InputFile File_;
	std::string Path_;
	bool Transpose_;
	uint64_t FileBytes_;
	uint64_t BeginBytes_;
	uint64_t EndBytes_;
	bool ChunkMode_;
	bool HeaderChecked_;
	std::vector<char> Buffer_;
	uint64_t BufferFileOffset_;
	size_t BufferBytes_;
	size_t Cursor_;
	uint64_t LineStartOffset_;
	uint64_t LineNo_;
	uint64_t AdvisedUntilBytes_;
};

struct ScanResult {
	uint32_t maxId;
	uint64_t edgesRaw;
};

class EdgeScanner {
public:
	explicit EdgeScanner(const std::filesystem::path& path, ThreadPool* pool = nullptr);

	ScanResult Run();

private:
	ScanResult ScanChunk(uint64_t beginBytes, uint64_t endBytes, bool firstChunk) const;
	[[noreturn]] void RethrowWithLineNumber(const CsvChunkError& error) const;

	std::filesystem::path Path_;
	ThreadPool* Pool_;
};

} // namespace lr::common
