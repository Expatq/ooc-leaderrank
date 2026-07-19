#include "ranks_writer.hpp"

#include <common/core/constants.hpp>
#include <common/io/aligned_io.hpp>
#include <common/io/bitmap.hpp>

#include <algorithm>
#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace lr::rank {

RanksCsvWriter::RanksCsvWriter(const grid::WorkdirRoot& workdir, const grid::Meta& meta, common::ThreadPool* pool)
    : Workdir_(workdir), Meta_(meta), Pool_(pool) {}

void RanksCsvWriter::Write(const RankResult& result, const std::filesystem::path& outPath) const {
	const grid::PartitionScheme scheme = Meta_.scheme;
	const common::RandomAccessFile rankFile(Workdir_.RankFile(result.rankFileSide), scheme.VertexCount() * common::kRankBytesPerVertex);

	std::ofstream output(outPath, std::ios::trunc);
	if (!output) {
		throw std::runtime_error(std::format("cannot create {}", outPath.string()));
	}
	output << "vertex,rank\n";

	const double vertexCount = static_cast<double>(Meta_.vertices);
	const double groundShare = result.groundScore / vertexCount;
	std::vector<double> window(scheme.intervalSize);
	std::vector<std::string> wave(Pool_->Threads());
	for (std::string& buffer : wave) {
		buffer.reserve(uint64_t{common::kCsvFormatChunkVertices} * common::kCsvLineBytes);
	}

	for (uint32_t interval = 0; interval < scheme.partitions; ++interval) {
		const uint32_t length = scheme.IntervalLength(interval);
		const uint32_t base = scheme.IntervalBase(interval);
		const grid::ByteRange range = scheme.RankByteRange(interval);
		rankFile.ReadAt(range.offsetBytes, window.data(), range.bytes);
		rankFile.AdviseDontNeed(range.offsetBytes, range.bytes);
		const common::Bitmap present = common::Bitmap::Load(Workdir_.PresentDir().Present(interval), length);

		const uint32_t pieces = (length + common::kCsvFormatChunkVertices - 1) / common::kCsvFormatChunkVertices;
		for (uint32_t firstPiece = 0; firstPiece < pieces; firstPiece += Pool_->Threads()) {
			const uint32_t wavePieces = std::min(Pool_->Threads(), pieces - firstPiece);
			Pool_->ParallelFor(wavePieces, [&, firstPiece, base, length](uint32_t, uint64_t offset) {
				const uint32_t piece = firstPiece + static_cast<uint32_t>(offset);
				const uint32_t begin = piece * common::kCsvFormatChunkVertices;
				const uint32_t end = std::min(begin + common::kCsvFormatChunkVertices, length);
				std::string& buffer = wave[offset];
				buffer.clear();
				for (uint32_t i = begin; i < end; ++i) {
					if (present.Test(i)) {
						buffer += std::format("{},{:.15g}\n", base + i, (window[i] + groundShare) / vertexCount);
					}
				}
			});
			for (uint32_t offset = 0; offset < wavePieces; ++offset) {
				output << wave[offset];
			}
		}
	}
	if (!output.flush()) {
		throw std::runtime_error(std::format("cannot write {}", outPath.string()));
	}
}

} // namespace lr::rank
