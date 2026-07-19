#include "csv_reader.hpp"

#include <common/core/constants.hpp>

#include <algorithm>
#include <charconv>
#include <format>
#include <stdexcept>

namespace lr::common {

namespace {

constexpr char kComma = ',';
constexpr char kTab = '\t';
constexpr char kCommentChar = '#';
constexpr char kMinusChar = '-';
constexpr std::string_view kStripChars = " \r";

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

CsvEdgeStream::CsvEdgeStream(const std::string& path, bool transpose)
    : Input_(path), Path_(path), LineNo_(0), HeaderChecked_(false), Transpose_(transpose) {
	if (!Input_) {
		throw std::runtime_error(std::format("cannot open {}", path));
	}
}

bool CsvEdgeStream::Next(Edge* out) {
	while (std::getline(Input_, Line_)) {
		++LineNo_;
		const std::string_view line = Strip(Line_);
		if (line.empty() || line.front() == kCommentChar) {
			continue;
		}
		if (!HeaderChecked_) {
			HeaderChecked_ = true;
			const TwoColumns columns = SplitTwoColumns(line);
			if (!columns.ok || !LooksLikeNumber(columns.first) ||
			    !LooksLikeNumber(columns.second)) {
				continue;
			}
		}
		ParseDataLine(line, out);
		return true;
	}
	return false;
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
	const auto [parsedEnd, errorCode] =
	    std::from_chars(token.data(), token.data() + token.size(), value);
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
	throw std::runtime_error(std::format("{}:{}: {}", Path_, LineNo_, reason));
}

EdgeScanner::EdgeScanner(const std::string& path) : Path_(path) {}

ScanResult EdgeScanner::Run() {
	CsvEdgeStream stream(Path_, false);
	ScanResult result{0, 0};
	Edge edge{};
	while (stream.Next(&edge)) {
		result.maxId = std::max({result.maxId, edge.src, edge.dst});
		++result.edgesRaw;
	}
	return result;
}

} // namespace lr::common
