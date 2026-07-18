#include "engine.hpp"

#include <common/budget/memory_budget.hpp>
#include <common/core/constants.hpp>
#include <common/io/bitmap.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <format>
#include <iostream>
#include <stdexcept>

namespace lr::rank {

namespace {

constexpr uint32_t kCurrentSide = 0;
constexpr uint32_t kNextSide = 1;

} // namespace

Engine::Engine(const grid::WorkdirRoot& workdir, uint64_t budgetBytes, double eps,
               uint32_t maxIterations)
    : Workdir_(workdir), Meta_(grid::Meta::Load(workdir)), Scheme_(Meta_.Scheme()), Eps_(eps),
      MaxIterations_(maxIterations), GroundScore_(0.0) {
	ValidateWorkdir();

	const grid::PartitionPlanner planner(budgetBytes, 1);
	ChunkBytes_ = planner.IoChunkBytes();
	const uint64_t intervalSize = Scheme_.intervalSize;

	common::MemoryBudget budget(budgetBytes);
	budget.Reserve("source contributions", common::kRankBytesPerVertex * intervalSize);
	budget.Reserve("column accumulator", common::kRankBytesPerVertex * intervalSize);
	budget.Reserve("row degrees", common::kDegreeBytesPerVertex * intervalSize);
	budget.Reserve("column bitmap", intervalSize / 8 + 1);
	budget.Reserve("io chunk", ChunkBytes_);
	budget.Reserve("block index", Scheme_.BlockCount() * sizeof(grid::BlockRef));

	Contrib_.resize(intervalSize);
	Accumulator_.resize(intervalSize);
	Degrees_.resize(intervalSize);
	Index_ =
	    std::make_unique<grid::BlockIndex>(grid::BlockIndex::Load(Workdir_.BlocksIdx(), Scheme_));
}

const grid::Meta& Engine::GetMeta() const {
	return Meta_;
}

void Engine::ValidateWorkdir() const {
	if (Meta_.vertices == 0 || Meta_.edges == 0) {
		throw std::runtime_error("graph is empty after dropping self-loops and duplicates");
	}
	if (uint64_t{Scheme_.partitions} * Scheme_.intervalSize < Scheme_.VertexCount()) {
		throw std::runtime_error("meta: intervals do not cover the id range");
	}
}

void Engine::InitializeRanks(common::RandomAccessFile* rankFile) {
	for (uint32_t interval = 0; interval < Scheme_.partitions; ++interval) {
		const uint32_t length = Scheme_.IntervalLength(interval);
		const common::Bitmap present =
		    common::Bitmap::Load(Workdir_.PresentDir().Present(interval).string(), length);
		for (uint32_t i = 0; i < length; ++i) {
			Accumulator_[i] = present.Test(i) ? 1.0 : 0.0;
		}
		const uint64_t offsetBytes =
		    uint64_t{Scheme_.IntervalBase(interval)} * common::kRankBytesPerVertex;
		rankFile->WriteAt(offsetBytes, Accumulator_.data(),
		                  uint64_t{length} * common::kRankBytesPerVertex);
		rankFile->SyncAndDrop(offsetBytes, uint64_t{length} * common::kRankBytesPerVertex);
	}
	GroundScore_ = 0.0;
}

double Engine::IterateOnce(grid::BlockReader* reader, const common::RandomAccessFile& current,
                           common::RandomAccessFile* next) {
	const double vertexCount = static_cast<double>(Meta_.vertices);
	const double groundShare = GroundScore_ / vertexCount;
	double groundNext = 0.0;
	double deltaL1 = 0.0;
	double massNew = 0.0;

	for (uint32_t dstInterval = 0; dstInterval < Scheme_.partitions; ++dstInterval) {
		const uint32_t columnLength = Scheme_.IntervalLength(dstInterval);
		const uint32_t columnBase = Scheme_.IntervalBase(dstInterval);
		std::fill_n(Accumulator_.begin(), columnLength, 0.0);

		for (uint32_t srcInterval = 0; srcInterval < Scheme_.partitions; ++srcInterval) {
			const uint32_t rowLength = Scheme_.IntervalLength(srcInterval);
			const uint32_t rowBase = Scheme_.IntervalBase(srcInterval);
			current.ReadAt(uint64_t{rowBase} * common::kRankBytesPerVertex, Contrib_.data(),
			               uint64_t{rowLength} * common::kRankBytesPerVertex);
			current.AdviseDontNeed(uint64_t{rowBase} * common::kRankBytesPerVertex,
			                       uint64_t{rowLength} * common::kRankBytesPerVertex);
			const common::InputFile degFile(Workdir_.DegreesDir().Degrees(srcInterval).string());
			degFile.ReadAt(0, Degrees_.data(), uint64_t{rowLength} * common::kDegreeBytesPerVertex);
			degFile.AdviseDontNeed(0, uint64_t{rowLength} * common::kDegreeBytesPerVertex);
			for (uint32_t i = 0; i < rowLength; ++i) {
				Contrib_[i] /= static_cast<double>(Degrees_[i]) + 1.0;
			}
			if (dstInterval == 0) {
				for (uint32_t i = 0; i < rowLength; ++i) {
					groundNext += Contrib_[i];
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

		const common::Bitmap present =
		    common::Bitmap::Load(Workdir_.PresentDir().Present(dstInterval).string(), columnLength);
		for (uint32_t i = 0; i < columnLength; ++i) {
			if (present.Test(i)) {
				Accumulator_[i] += groundShare;
			}
		}
		current.ReadAt(uint64_t{columnBase} * common::kRankBytesPerVertex, Contrib_.data(),
		               uint64_t{columnLength} * common::kRankBytesPerVertex);
		current.AdviseDontNeed(uint64_t{columnBase} * common::kRankBytesPerVertex,
		                       uint64_t{columnLength} * common::kRankBytesPerVertex);
		for (uint32_t i = 0; i < columnLength; ++i) {
			deltaL1 += std::abs(Accumulator_[i] - Contrib_[i]);
			massNew += Accumulator_[i];
		}
		next->WriteAt(uint64_t{columnBase} * common::kRankBytesPerVertex, Accumulator_.data(),
		              uint64_t{columnLength} * common::kRankBytesPerVertex);
		next->SyncAndDrop(uint64_t{columnBase} * common::kRankBytesPerVertex,
		                  uint64_t{columnLength} * common::kRankBytesPerVertex);
	}

	const double massError = std::abs(massNew + groundNext - vertexCount);
	if (massError > common::kMassTolerancePerVertex * vertexCount) {
		throw std::runtime_error(std::format("mass invariant violated: |{} + {} - {}| = {}",
		                                     massNew, groundNext, vertexCount, massError));
	}
	GroundScore_ = groundNext;
	return deltaL1 / vertexCount;
}

RankResult Engine::Run() {
	const uint64_t rankFileBytes = Scheme_.VertexCount() * common::kRankBytesPerVertex;
	common::RandomAccessFile rankA(Workdir_.RankFile(kCurrentSide).string(), rankFileBytes);
	common::RandomAccessFile rankB(Workdir_.RankFile(kNextSide).string(), rankFileBytes);
	InitializeRanks(&rankA);

	const common::InputFile blocksBin(Workdir_.BlocksBin().string());
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
		std::cout << std::format("iteration {}: L1/N={:.3e} s_g={:.6f} time={:.3f}s\n", iteration,
		                         deltaPerVertex, GroundScore_, elapsed.count());
		std::cout.flush();

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

} // namespace lr::rank
