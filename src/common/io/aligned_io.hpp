#pragma once

#include <cstdint>
#include <string>

namespace lr::common {

class InputFile {
public:
	explicit InputFile(const std::string& path);
	~InputFile();

	InputFile(const InputFile&) = delete;
	InputFile& operator=(const InputFile&) = delete;

	uint64_t SizeBytes() const { return SizeBytes_; }
	void ReadAt(uint64_t offsetBytes, void* dst, size_t bytes) const;
	void AdviseSequential() const;
	void AdviseDontNeed(uint64_t offsetBytes, uint64_t bytes) const;

private:
	int Fd_;
	std::string Path_;
	uint64_t SizeBytes_;
};

class RandomAccessFile {
public:
	RandomAccessFile(const std::string& path, uint64_t sizeBytes);
	~RandomAccessFile();

	RandomAccessFile(const RandomAccessFile&) = delete;
	RandomAccessFile& operator=(const RandomAccessFile&) = delete;

	void ReadAt(uint64_t offsetBytes, void* dst, size_t bytes) const;
	void WriteAt(uint64_t offsetBytes, const void* src, size_t bytes);
	void SyncAndDrop(uint64_t offsetBytes, uint64_t bytes) const;
	void AdviseDontNeed(uint64_t offsetBytes, uint64_t bytes) const;

private:
	int Fd_;
	std::string Path_;
};

class OutputFile {
public:
	explicit OutputFile(const std::string& path);
	~OutputFile();

	OutputFile(const OutputFile&) = delete;
	OutputFile& operator=(const OutputFile&) = delete;

	void Append(const void* src, size_t bytes);
	void PadToAlignment(uint64_t alignmentBytes);
	uint64_t OffsetBytes() const { return OffsetBytes_; }

private:
	void MaybeWriteback();

	int Fd_;
	std::string Path_;
	uint64_t OffsetBytes_;
	uint64_t SyncedBytes_;
};

void AppendToFile(const std::string& path, const void* src, size_t bytes);

} // namespace lr::common
