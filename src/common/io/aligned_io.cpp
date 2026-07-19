#include "aligned_io.hpp"

#include <common/core/constants.hpp>
#include <common/core/size_literals.hpp>

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <cerrno>
#include <cstring>
#include <format>
#include <stdexcept>

namespace lr::common {

namespace {

constexpr static mode_t kFileMode = 0644;
constexpr static size_t kZeroPadBufferBytes = 4096;

void SyncRangeAndDrop(int fd, uint64_t offsetBytes, uint64_t bytes) {
#if defined(__linux__)
	constexpr static int kSyncFlags = SYNC_FILE_RANGE_WAIT_BEFORE | SYNC_FILE_RANGE_WRITE | SYNC_FILE_RANGE_WAIT_AFTER;
	const int synced = ::sync_file_range(fd, static_cast<off_t>(offsetBytes), static_cast<off_t>(bytes), kSyncFlags);
	if (synced != 0) {
		::fdatasync(fd);
	}
	::posix_fadvise(fd, static_cast<off_t>(offsetBytes), static_cast<off_t>(bytes), POSIX_FADV_DONTNEED);
#else
	(void)fd;
	(void)offsetBytes;
	(void)bytes;
#endif
}

void DropReadCache(int fd, uint64_t offsetBytes, uint64_t bytes) {
#if defined(__linux__)
	::posix_fadvise(fd, static_cast<off_t>(offsetBytes), static_cast<off_t>(bytes), POSIX_FADV_DONTNEED);
#else
	(void)fd;
	(void)offsetBytes;
	(void)bytes;
#endif
}

[[noreturn]] void FailErrno(const std::string& path, std::string_view action) {
	throw std::runtime_error(std::format("{}: {}: {}", path, action, std::strerror(errno)));
}

void ReadExact(int fd, const std::string& path, uint64_t offsetBytes, void* dst, size_t bytes) {
	char* cursor = static_cast<char*>(dst);
	size_t remaining = bytes;
	uint64_t offset = offsetBytes;
	while (remaining > 0) {
		const ssize_t got = ::pread(fd, cursor, remaining, static_cast<off_t>(offset));
		if (got < 0) {
			FailErrno(path, "pread");
		}
		if (got == 0) {
			throw std::runtime_error(std::format("{}: unexpected end of file at offset {}", path, offset));
		}
		cursor += got;
		offset += static_cast<uint64_t>(got);
		remaining -= static_cast<size_t>(got);
	}
}

void WriteExact(int fd, const std::string& path, uint64_t offsetBytes, const void* src, size_t bytes) {
	const char* cursor = static_cast<const char*>(src);
	size_t remaining = bytes;
	uint64_t offset = offsetBytes;
	while (remaining > 0) {
		const ssize_t put = ::pwrite(fd, cursor, remaining, static_cast<off_t>(offset));
		if (put < 0) {
			FailErrno(path, "pwrite");
		}
		cursor += put;
		offset += static_cast<uint64_t>(put);
		remaining -= static_cast<size_t>(put);
	}
}

} // namespace

InputFile::InputFile(const std::filesystem::path& path)
    : Fd_(::open(path.c_str(), O_RDONLY)), Path_(path.string()) {
	if (Fd_ < 0) {
		FailErrno(Path_, "open");
	}
	struct stat info{};
	if (::fstat(Fd_, &info) != 0) {
		FailErrno(Path_, "fstat");
	}
	SizeBytes_ = static_cast<uint64_t>(info.st_size);
}

InputFile::~InputFile() {
	::close(Fd_);
}

void InputFile::ReadAt(uint64_t offsetBytes, void* dst, size_t bytes) const {
	ReadExact(Fd_, Path_, offsetBytes, dst, bytes);
}

void InputFile::AdviseSequential() const {
#if defined(__linux__)
	::posix_fadvise(Fd_, 0, 0, POSIX_FADV_SEQUENTIAL);
#endif
}

void InputFile::AdviseDontNeed(uint64_t offsetBytes, uint64_t bytes) const {
	DropReadCache(Fd_, offsetBytes, bytes);
}

RandomAccessFile::RandomAccessFile(const std::filesystem::path& path, uint64_t sizeBytes)
    : Fd_(::open(path.c_str(), O_RDWR | O_CREAT, kFileMode)), Path_(path.string()) {
	if (Fd_ < 0) {
		FailErrno(Path_, "open");
	}
	if (::ftruncate(Fd_, static_cast<off_t>(sizeBytes)) != 0) {
		FailErrno(Path_, "ftruncate");
	}
}

RandomAccessFile::~RandomAccessFile() {
	::close(Fd_);
}

void RandomAccessFile::ReadAt(uint64_t offsetBytes, void* dst, size_t bytes) const {
	ReadExact(Fd_, Path_, offsetBytes, dst, bytes);
}

void RandomAccessFile::WriteAt(uint64_t offsetBytes, const void* src, size_t bytes) {
	WriteExact(Fd_, Path_, offsetBytes, src, bytes);
}

void RandomAccessFile::SyncAndDrop(uint64_t offsetBytes, uint64_t bytes) const {
	SyncRangeAndDrop(Fd_, offsetBytes, bytes);
}

void RandomAccessFile::AdviseDontNeed(uint64_t offsetBytes, uint64_t bytes) const {
	DropReadCache(Fd_, offsetBytes, bytes);
}

OutputFile::OutputFile(const std::filesystem::path& path)
    : Fd_(::open(path.c_str(), O_WRONLY | O_CREAT | O_TRUNC, kFileMode)), Path_(path.string()),
      OffsetBytes_(0), SyncedBytes_(0) {
	if (Fd_ < 0) {
		FailErrno(Path_, "open");
	}
}

OutputFile::~OutputFile() {
	::close(Fd_);
}

void OutputFile::Append(const void* src, size_t bytes) {
	WriteExact(Fd_, Path_, OffsetBytes_, src, bytes);
	OffsetBytes_ += bytes;
	MaybeWriteback();
}

void OutputFile::PadToAlignment(uint64_t alignmentBytes) {
	static const std::array<char, kZeroPadBufferBytes> zeros{};
	uint64_t padBytes = (alignmentBytes - OffsetBytes_ % alignmentBytes) % alignmentBytes;
	while (padBytes > 0) {
		const size_t portion = std::min<uint64_t>(padBytes, zeros.size());
		Append(zeros.data(), portion);
		padBytes -= portion;
	}
}

void OutputFile::MaybeWriteback() {
	if (OffsetBytes_ - SyncedBytes_ >= kWritebackChunkBytes) {
		SyncRangeAndDrop(Fd_, SyncedBytes_, OffsetBytes_ - SyncedBytes_);
		SyncedBytes_ = OffsetBytes_;
	}
}

void AppendToFile(const std::filesystem::path& path, const void* src, size_t bytes) {
	const int fd = ::open(path.c_str(), O_WRONLY | O_CREAT | O_APPEND, kFileMode);
	if (fd < 0) {
		FailErrno(path.string(), "open");
	}
	const char* cursor = static_cast<const char*>(src);
	size_t remaining = bytes;
	while (remaining > 0) {
		const ssize_t put = ::write(fd, cursor, remaining);
		if (put < 0) {
			::close(fd);
			FailErrno(path.string(), "write");
		}
		cursor += put;
		remaining -= static_cast<size_t>(put);
	}
#if defined(__linux__)
	::sync_file_range(fd, 0, 0, SYNC_FILE_RANGE_WRITE);
#endif
	if (::close(fd) != 0) {
		FailErrno(path.string(), "close");
	}
}

} // namespace lr::common
