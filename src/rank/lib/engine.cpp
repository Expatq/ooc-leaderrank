#include "engine.hpp"

#include <common/budget/memory_budget.hpp>
#include <common/core/constants.hpp>
#include <common/io/bitmap.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <ostream>
#include <stdexcept>

namespace lr::rank {

namespace {

constexpr static uint32_t kCurrentSide = 0;
constexpr static uint32_t kNextSide = 1;

} // namespace

Engine::Engine(const grid::WorkdirRoot& workdir, const RankConfig& config, std::ostream* progress)
    : Workdir_(workdir), Meta_(grid::Meta::Load(workdir)), Scheme_(Meta_.scheme), Eps_(config.eps),
      MaxIterations_(config.maxIterations), Progress_(progress), GroundScore_(0.0) {
	ValidateWorkdir();

	const grid::PartitionPlanner planner(config.budgetBytes, Meta_.threadsPlanned);
	ChunkBytes_ = planner.IoChunkBytes();
	const uint64_t intervalSize = Scheme_.intervalSize;

	common::MemoryBudget budget(config.budgetBytes);
	budget.Reserve("source contributions", common::kRankBytesPerVertex * intervalSize);
	budget.Reserve("column accumulator", common::kRankBytesPerVertex * intervalSize);
	budget.Reserve("row degrees", common::kDegreeBytesPerVertex * intervalSize);
	budget.Reserve("column bitmap", intervalSize / 8 + 1);
	budget.Reserve("io chunk", ChunkBytes_);
	budget.Reserve("block index", Scheme_.BlockCount() * sizeof(grid::BlockRef));

	Contrib_.resize(intervalSize);
	Accumulator_.resize(intervalSize);
	Degrees_.resize(intervalSize);
	Index_ = std::make_unique<grid::BlockIndex>(grid::BlockIndex::Load(Scheme_, Workdir_));
}

RankResult Engine::Run() {
	const uint64_t rankFileBytes = Scheme_.VertexCount() * common::kRankBytesPerVertex;
	common::RandomAccessFile rankA(Workdir_.RankFile(kCurrentSide), rankFileBytes);
	common::RandomAccessFile rankB(Workdir_.RankFile(kNextSide), rankFileBytes);
	InitializeRanks(&rankA);

	const common::InputFile blocksBin(Workdir_.BlocksBin());
	blocksBin.AdviseSequential();
	grid::BlockReader reader(&blocksBin, ChunkBytes_);

	common::RandomAccessFile* current = &rankA;
	common::RandomAccessFile* next = &rankB;
	uint32_t currentSide = kCurrentSide;

	RankResult result{0, false, 0.0, 0.0, currentSide};
	for (uint32_t iteration = 1; iteration <= MaxIterations_; ++iteration) {
		const auto startedAt = std::chrono::steady_clock::now();
		const double deltaPerVertex = IterateOnce(&reader, *current, next);
		const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - startedAt;

		std::swap(current, next);
		currentSide = 1 - currentSide;
		Report(std::format("iteration {}: L1/N={:.3e} s_g={:.6f} time={:.3f}s", iteration, deltaPerVertex, GroundScore_, elapsed.count()));

		result.iterations = iteration;
		result.finalDeltaPerVertex = deltaPerVertex;
		if (deltaPerVertex < Eps_) {
			result.converged = true;
			break;
		}
	}
	result.groundScore = GroundScore_;
	result.rankFileSide = currentSide;
	return result;
}

const grid::Meta& Engine::GetMeta() const {
	return Meta_;
}

void Engine::ValidateWorkdir() const {
	if (Meta_.vertices == 0 || Meta_.edges == 0) {
		throw std::runtime_error("graph is empty after dropping self-loops and duplicates");
	}
	Workdir_.Validate(Scheme_);
}

void Engine::InitializeRanks(common::RandomAccessFile* rankFile) {
	for (uint32_t interval = 0; interval < Scheme_.partitions; ++interval) {
		const uint32_t length = Scheme_.IntervalLength(interval);
		const common::Bitmap present = common::Bitmap::Load(Workdir_.PresentDir().Present(interval), length);
		for (uint32_t i = 0; i < length; ++i) {
			Accumulator_[i] = present.Test(i) ? 1.0 : 0.0;
		}
		const grid::ByteRange range = Scheme_.RankByteRange(interval);
		rankFile->WriteAt(range.offsetBytes, Accumulator_.data(), range.bytes);
		rankFile->SyncAndDrop(range.offsetBytes, range.bytes);
	}
	GroundScore_ = 0.0;
}

double Engine::IterateOnce(grid::BlockReader* reader, const common::RandomAccessFile& current, common::RandomAccessFile* next) {
	const double vertexCount = static_cast<double>(Meta_.vertices);
	const double groundShare = GroundScore_ / vertexCount;
	IterationSums total;

	for (uint32_t dstInterval = 0; dstInterval < Scheme_.partitions; ++dstInterval) {
		total.ground += AccumulateColumn(reader, current, dstInterval);
		const ColumnSums column = FinalizeColumn(current, next, dstInterval, groundShare);
		total.deltaL1 += column.deltaL1;
		total.mass += column.mass;
	}

	CheckMassInvariant(total, vertexCount);
	GroundScore_ = total.ground;
	return total.deltaL1 / vertexCount;
}

double Engine::AccumulateColumn(grid::BlockReader* reader, const common::RandomAccessFile& current, uint32_t dstInterval) {
	const uint32_t columnBase = Scheme_.IntervalBase(dstInterval);
	std::fill_n(Accumulator_.begin(), Scheme_.IntervalLength(dstInterval), 0.0);
	double ground = 0.0;

	for (uint32_t srcInterval = 0; srcInterval < Scheme_.partitions; ++srcInterval) {
		const uint32_t rowLength = Scheme_.IntervalLength(srcInterval);
		const uint32_t rowBase = Scheme_.IntervalBase(srcInterval);
		const grid::ByteRange row = Scheme_.RankByteRange(srcInterval);
		current.ReadAt(row.offsetBytes, Contrib_.data(), row.bytes);
		current.AdviseDontNeed(row.offsetBytes, row.bytes);
		const common::InputFile degFile(Workdir_.DegreesDir().Degrees(srcInterval));
		degFile.ReadAt(0, Degrees_.data(), uint64_t{rowLength} * common::kDegreeBytesPerVertex);
		degFile.AdviseDontNeed(0, uint64_t{rowLength} * common::kDegreeBytesPerVertex);
		for (uint32_t i = 0; i < rowLength; ++i) {
			Contrib_[i] /= static_cast<double>(Degrees_[i]) + 1.0;
		}
		if (dstInterval == 0) {
			for (uint32_t i = 0; i < rowLength; ++i) {
				ground += Contrib_[i];
			}
		}

		reader->Open(Index_->At(srcInterval, dstInterval));
		std::span<const common::Edge> chunk;
		while (reader->Next(&chunk)) {
			for (const common::Edge& edge : chunk) {
				Accumulator_[edge.dst - columnBase] += Contrib_[edge.src - rowBase];
			}
		}
	}
	return ground;
}

Engine::ColumnSums Engine::FinalizeColumn(const common::RandomAccessFile& current, common::RandomAccessFile* next, uint32_t dstInterval, double groundShare) {
	const uint32_t columnLength = Scheme_.IntervalLength(dstInterval);
	const common::Bitmap present = common::Bitmap::Load(Workdir_.PresentDir().Present(dstInterval), columnLength);
	for (uint32_t i = 0; i < columnLength; ++i) {
		if (present.Test(i)) {
			Accumulator_[i] += groundShare;
		}
	}

	const grid::ByteRange column = Scheme_.RankByteRange(dstInterval);
	current.ReadAt(column.offsetBytes, Contrib_.data(), column.bytes);
	current.AdviseDontNeed(column.offsetBytes, column.bytes);
	ColumnSums sums{0.0, 0.0};
	for (uint32_t i = 0; i < columnLength; ++i) {
		sums.deltaL1 += std::abs(Accumulator_[i] - Contrib_[i]);
		sums.mass += Accumulator_[i];
	}
	next->WriteAt(column.offsetBytes, Accumulator_.data(), column.bytes);
	next->SyncAndDrop(column.offsetBytes, column.bytes);
	return sums;
}

void Engine::CheckMassInvariant(const IterationSums& sums, double vertexCount) const {
	const double massError = std::abs(sums.mass + sums.ground - vertexCount);
	if (massError > common::kMassTolerancePerVertex * vertexCount) {
		throw std::runtime_error(std::format("mass invariant violated: |{} + {} - {}| = {}", sums.mass, sums.ground, vertexCount, massError));
	}
}

void Engine::Report(const std::string& line) const {
	if (Progress_ != nullptr) {
		*Progress_ << line << '\n';
		Progress_->flush();
	}
}

} // namespace lr::rank
