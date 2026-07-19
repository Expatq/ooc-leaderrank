#include "csv_reader.hpp"

#include <common/core/constants.hpp>
#include <common/thread/thread_pool.hpp>

#include <algorithm>
#include <charconv>
#include <cstring>
#include <format>
#include <utility>

namespace lr::common {

namespace {

constexpr static char kComma = ',';
constexpr static char kTab = '\t';
constexpr static char kNewline = '\n';
constexpr static char kCommentChar = '#';
constexpr static char kMinusChar = '-';
constexpr static std::string_view kStripChars = " \r";
constexpr static uint64_t kWholeFile = UINT64_MAX;

std::string_view Strip(std::string_view text) {
	const size_t begin = text.find_first_not_of(kStripChars);
	if (begin == std::string_view::npos) {
		return {};
	}
	const size_t end = text.find_last_not_of(kStripChars);
	return text.substr(begin, end - begin + 1);
}

bool LooksLikeNumber(std::string_view token) {
	std::string_view digits = token;
	if (digits.starts_with(kMinusChar)) {
		digits.remove_prefix(1);
	}
	if (digits.empty()) {
		return false;
	}
	return std::ranges::all_of(digits, [](char symbol) { return symbol >= '0' && symbol <= '9'; });
}

struct TwoColumns {
	std::string_view first;
	std::string_view second;
	bool ok;
};

TwoColumns SplitTwoColumns(std::string_view line) {
	const char separator = line.find(kTab) != std::string_view::npos ? kTab : kComma;
	const size_t firstCut = line.find(separator);
	if (firstCut == std::string_view::npos) {
		return {{}, {}, false};
	}
	std::string_view rest = line.substr(firstCut + 1);
	const size_t secondCut = rest.find(separator);
	if (secondCut != std::string_view::npos) {
		rest = rest.substr(0, secondCut);
	}
	return {Strip(line.substr(0, firstCut)), Strip(rest), true};
}

} // namespace

CsvChunkError::CsvChunkError(uint64_t offsetBytes, std::string reason)
    : std::runtime_error(std::format("byte offset {}: {}", offsetBytes, reason)),
      OffsetBytes_(offsetBytes), Reason_(std::move(reason)) {}

uint64_t CsvChunkError::OffsetBytes() const {
	return OffsetBytes_;
}

const std::string& CsvChunkError::Reason() const {
	return Reason_;
}

CsvEdgeStream::CsvEdgeStream(const std::filesystem::path& path, bool transpose)
    : CsvEdgeStream(path, transpose, 0, kWholeFile, true) {}

uint64_t CsvEdgeStream::DataStartBytes(const std::filesystem::path& path) {
	CsvEdgeStream stream(path, false);
	std::string_view line;
	while (stream.NextLine(&line)) {
		const std::string_view stripped = Strip(line);
		if (stripped.empty() || stripped.front() == kCommentChar) {
			continue;
		}
		const TwoColumns columns = SplitTwoColumns(stripped);
		if (!columns.ok || !LooksLikeNumber(columns.first) || !LooksLikeNumber(columns.second)) {
			return stream.BufferFileOffset_ + stream.Cursor_;
		}
		return stream.LineStartOffset_;
	}
	return stream.FileBytes_;
}

CsvEdgeStream::CsvEdgeStream(const std::filesystem::path& path, bool transpose, uint64_t beginBytes, uint64_t endBytes, bool firstChunk)
    : File_(path), Path_(path.string()), Transpose_(transpose), FileBytes_(File_.SizeBytes()),
      BeginBytes_(std::min(beginBytes, FileBytes_)), EndBytes_(std::min(endBytes, FileBytes_)),
      ChunkMode_(!(BeginBytes_ == 0 && EndBytes_ == FileBytes_ && firstChunk)),
      HeaderChecked_(!firstChunk), Buffer_(kParseBufferBytes), BufferFileOffset_(BeginBytes_),
      BufferBytes_(0), Cursor_(0), LineStartOffset_(BeginBytes_), LineNo_(0),
      AdvisedUntilBytes_(BeginBytes_) {
	AlignToOwnedLine();
}

bool CsvEdgeStream::Next(Edge* out) {
	std::string_view line;
	while (NextLine(&line)) {
		const std::string_view stripped = Strip(line);
		if (stripped.empty() || stripped.front() == kCommentChar) {
			continue;
		}
		if (!HeaderChecked_) {
			HeaderChecked_ = true;
			const TwoColumns columns = SplitTwoColumns(stripped);
			if (!columns.ok || !LooksLikeNumber(columns.first) || !LooksLikeNumber(columns.second)) {
				continue;
			}
		}
		ParseDataLine(stripped, out);
		return true;
	}
	return false;
}

bool CsvEdgeStream::NextLine(std::string_view* line) {
	while (true) {
		if (Cursor_ == BufferBytes_) {
			if (FileExhausted()) {
				return false;
			}
			Refill();
			continue;
		}
		const uint64_t lineStart = BufferFileOffset_ + Cursor_;
		if (lineStart >= EndBytes_) {
			return false;
		}
		const void* found = std::memchr(Buffer_.data() + Cursor_, kNewline, BufferBytes_ - Cursor_);
		if (found != nullptr) {
			const size_t newlineIndex = static_cast<size_t>(static_cast<const char*>(found) - Buffer_.data());
			*line = std::string_view(Buffer_.data() + Cursor_, newlineIndex - Cursor_);
			LineStartOffset_ = lineStart;
			Cursor_ = newlineIndex + 1;
			++LineNo_;
			return true;
		}
		if (FileExhausted()) {
			*line = std::string_view(Buffer_.data() + Cursor_, BufferBytes_ - Cursor_);
			LineStartOffset_ = lineStart;
			Cursor_ = BufferBytes_;
			++LineNo_;
			return true;
		}
		if (Cursor_ == 0 && BufferBytes_ == Buffer_.size()) {
			LineStartOffset_ = lineStart;
			Fail(std::format("line is longer than {} bytes", Buffer_.size()));
		}
		Refill();
	}
}

void CsvEdgeStream::Refill() {
	const uint64_t consumedEnd = BufferFileOffset_ + Cursor_;
	if (consumedEnd - AdvisedUntilBytes_ >= kParseAdviseBytes) {
		File_.AdviseDontNeed(AdvisedUntilBytes_, consumedEnd - AdvisedUntilBytes_);
		AdvisedUntilBytes_ = consumedEnd;
	}
	if (Cursor_ > 0) {
		std::memmove(Buffer_.data(), Buffer_.data() + Cursor_, BufferBytes_ - Cursor_);
		BufferFileOffset_ += Cursor_;
		BufferBytes_ -= Cursor_;
		Cursor_ = 0;
	}
	const uint64_t readFrom = BufferFileOffset_ + BufferBytes_;
	const size_t portion = static_cast<size_t>(std::min<uint64_t>(Buffer_.size() - BufferBytes_, FileBytes_ - readFrom));
	if (portion > 0) {
		File_.ReadAt(readFrom, Buffer_.data() + BufferBytes_, portion);
		BufferBytes_ += portion;
	}
}

bool CsvEdgeStream::FileExhausted() const {
	return BufferFileOffset_ + BufferBytes_ >= FileBytes_;
}

void CsvEdgeStream::AlignToOwnedLine() {
	if (BeginBytes_ == 0 || BeginBytes_ >= FileBytes_) {
		return;
	}
	BufferFileOffset_ = BeginBytes_ - 1;
	Refill();
	if (Buffer_[0] == kNewline) {
		Cursor_ = 1;
		return;
	}
	Cursor_ = 1;
	while (true) {
		const void* found = std::memchr(Buffer_.data() + Cursor_, kNewline, BufferBytes_ - Cursor_);
		if (found != nullptr) {
			Cursor_ = static_cast<size_t>(static_cast<const char*>(found) - Buffer_.data()) + 1;
			return;
		}
		if (FileExhausted()) {
			Cursor_ = BufferBytes_;
			return;
		}
		Cursor_ = BufferBytes_;
		Refill();
	}
}

void CsvEdgeStream::ParseDataLine(std::string_view line, Edge* out) const {
	const TwoColumns columns = SplitTwoColumns(line);
	if (!columns.ok) {
		Fail("expected two columns");
	}
	const uint32_t from = ParseId(columns.first);
	const uint32_t to = ParseId(columns.second);
	*out = Transpose_ ? Edge{to, from} : Edge{from, to};
}

uint32_t CsvEdgeStream::ParseId(std::string_view token) const {
	if (token.starts_with(kMinusChar)) {
		Fail(std::format("negative id: {}", token));
	}
	uint64_t value = 0;
	const auto [parsedEnd, errorCode] = std::from_chars(token.data(), token.data() + token.size(), value);
	if (errorCode == std::errc::result_out_of_range) {
		Fail(std::format("id exceeds int32: {}", token));
	}
	if (errorCode != std::errc{} || parsedEnd != token.data() + token.size()) {
		Fail(std::format("not a number: '{}'", token));
	}
	if (value > kMaxVertexId) {
		Fail(std::format("id exceeds int32: {}", value));
	}
	return static_cast<uint32_t>(value);
}

void CsvEdgeStream::Fail(std::string_view reason) const {
	if (ChunkMode_) {
		throw CsvChunkError(LineStartOffset_, std::string(reason));
	}
	throw std::runtime_error(std::format("{}:{}: {}", Path_, LineNo_, reason));
}

EdgeScanner::EdgeScanner(const std::filesystem::path& path, ThreadPool* pool)
    : Path_(path), Pool_(pool) {}

ScanResult EdgeScanner::Run() {
	const uint64_t fileBytes = InputFile(Path_).SizeBytes();
	const uint32_t threads = Pool_ == nullptr ? 1 : Pool_->Threads();
	if (threads == 1) {
		return ScanChunk(0, fileBytes, true);
	}
	const uint64_t dataStart = CsvEdgeStream::DataStartBytes(Path_);
	const uint64_t dataBytes = fileBytes - dataStart;
	const uint64_t chunkCount = std::max<uint64_t>(threads, (dataBytes + kScanChunkBytes - 1) / kScanChunkBytes);
	std::vector<ScanResult> partials(chunkCount, ScanResult{0, 0});
	try {
		Pool_->ParallelFor(chunkCount, [this, &partials, dataStart, dataBytes, chunkCount](uint32_t, uint64_t chunk) {
			const uint64_t begin = dataStart + dataBytes * chunk / chunkCount;
			const uint64_t end = dataStart + dataBytes * (chunk + 1) / chunkCount;
			partials[chunk] = ScanChunk(begin, end, false);
		});
	} catch (const CsvChunkError& error) {
		RethrowWithLineNumber(error);
	}
	ScanResult total{0, 0};
	for (const ScanResult& part : partials) {
		total.maxId = std::max(total.maxId, part.maxId);
		total.edgesRaw += part.edgesRaw;
	}
	return total;
}

ScanResult EdgeScanner::ScanChunk(uint64_t beginBytes, uint64_t endBytes, bool firstChunk) const {
	CsvEdgeStream stream(Path_, false, beginBytes, endBytes, firstChunk);
	ScanResult result{0, 0};
	Edge edge{};
	while (stream.Next(&edge)) {
		result.maxId = std::max({result.maxId, edge.src, edge.dst});
		++result.edgesRaw;
	}
	return result;
}

void EdgeScanner::RethrowWithLineNumber(const CsvChunkError& error) const {
	const InputFile file(Path_);
	std::vector<char> buffer(kParseBufferBytes);
	uint64_t newlines = 0;
	uint64_t offset = 0;
	while (offset < error.OffsetBytes()) {
		const size_t portion = static_cast<size_t>(std::min<uint64_t>(buffer.size(), error.OffsetBytes() - offset));
		file.ReadAt(offset, buffer.data(), portion);
		newlines += static_cast<uint64_t>(std::count(buffer.begin(), buffer.begin() + static_cast<int64_t>(portion), '\n'));
		offset += portion;
	}
	throw std::runtime_error(std::format("{}:{}: {}", Path_.string(), newlines + 1, error.Reason()));
}

} // namespace lr::common
