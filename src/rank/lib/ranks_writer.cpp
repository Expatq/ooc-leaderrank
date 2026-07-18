#include "ranks_writer.hpp"

#include <common/core/constants.hpp>
#include <common/core/size_literals.hpp>
#include <common/io/aligned_io.hpp>
#include <common/io/bitmap.hpp>

#include <format>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace lr::rank {

namespace {

constexpr uint64_t kFlushThresholdBytes = 1_MiB;

} // namespace

RanksCsvWriter::RanksCsvWriter(const grid::WorkdirRoot& workdir, const grid::Meta& meta)
    : Workdir_(workdir), Meta_(meta) {}

void RanksCsvWriter::Write(const RankResult& result, const std::filesystem::path& outPath) const {
	const grid::PartitionScheme scheme = Meta_.Scheme();
	const common::RandomAccessFile rankFile(Workdir_.RankFile(result.rankFileSide).string(),
	                                        scheme.VertexCount() * common::kRankBytesPerVertex);

	std::ofstream output(outPath, std::ios::trunc);
	if (!output) {
		throw std::runtime_error(std::format("cannot create {}", outPath.string()));
	}
	output << "vertex,rank\n";

	const double vertexCount = static_cast<double>(Meta_.vertices);
	const double groundShare = result.groundScore / vertexCount;
	std::vector<double> window(scheme.intervalSize);
	std::string buffer;

	for (uint32_t interval = 0; interval < scheme.partitions; ++interval) {
		const uint32_t length = scheme.IntervalLength(interval);
		const uint32_t base = scheme.IntervalBase(interval);
		rankFile.ReadAt(uint64_t{base} * common::kRankBytesPerVertex, window.data(),
		                uint64_t{length} * common::kRankBytesPerVertex);
		rankFile.AdviseDontNeed(uint64_t{base} * common::kRankBytesPerVertex,
		                        uint64_t{length} * common::kRankBytesPerVertex);
		const common::Bitmap present =
		    common::Bitmap::Load(Workdir_.PresentDir().Present(interval).string(), length);
		for (uint32_t i = 0; i < length; ++i) {
			if (present.Test(i)) {
				const double rank = (window[i] + groundShare) / vertexCount;
				buffer += std::format("{},{:.15g}\n", base + i, rank);
				if (buffer.size() >= kFlushThresholdBytes) {
					output << buffer;
					buffer.clear();
				}
			}
		}
	}
	output << buffer;
	if (!output.flush()) {
		throw std::runtime_error(std::format("cannot write {}", outPath.string()));
	}
}

} // namespace lr::rank
