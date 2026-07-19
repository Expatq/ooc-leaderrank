#include <common/thread/thread_pool.hpp>

#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <vector>

namespace {

using lr::common::ThreadPool;

constexpr uint64_t kManyTasks = 1'000;
constexpr uint64_t kManyEpochs = 1'000;
constexpr uint64_t kThrowingTask = 37;

TEST(ThreadPool, RunsEveryTaskExactlyOnce) {
	ThreadPool pool(4);
	std::vector<std::atomic<uint32_t>> hits(kManyTasks);
	pool.ParallelFor(hits.size(), [&hits](uint32_t, uint64_t task) { ++hits[task]; });
	EXPECT_TRUE(std::ranges::all_of(hits, [](const auto& hit) { return hit == 1u; }));
}

TEST(ThreadPool, SingleThreadRunsInlineOnCallerThread) {
	ThreadPool pool(1);
	std::thread::id seen;
	pool.ParallelFor(1, [&seen](uint32_t, uint64_t) { seen = std::this_thread::get_id(); });
	EXPECT_EQ(seen, std::this_thread::get_id());
}

TEST(ThreadPool, ZeroTasksIsNoOp) {
	ThreadPool pool(4);
	std::atomic<uint32_t> calls{0};
	pool.ParallelFor(0, [&calls](uint32_t, uint64_t) { ++calls; });
	EXPECT_EQ(calls, 0u);
}

TEST(ThreadPool, WorkerIdsStayWithinBounds) {
	ThreadPool pool(4);
	std::atomic<uint32_t> outOfRange{0};
	pool.ParallelFor(kManyTasks, [&outOfRange, &pool](uint32_t worker, uint64_t) {
		if (worker >= pool.Threads()) {
			++outOfRange;
		}
	});
	EXPECT_EQ(outOfRange, 0u);
}

TEST(ThreadPool, PropagatesFirstWorkerException) {
	ThreadPool pool(4);
	std::atomic<uint64_t> completedOthers{0};
	const auto body = [&completedOthers](uint32_t, uint64_t task) {
		if (task == kThrowingTask) {
			throw std::runtime_error("task failed");
		}
		++completedOthers;
	};
	EXPECT_THROW(pool.ParallelFor(100, body), std::runtime_error);
	EXPECT_EQ(completedOthers, 99u);
}

TEST(ThreadPool, SurvivesExceptionAndRunsNextEpoch) {
	ThreadPool pool(4);
	EXPECT_THROW(pool.ParallelFor(1, [](uint32_t, uint64_t) { throw std::runtime_error("boom"); }),
	             std::runtime_error);
	std::atomic<uint64_t> calls{0};
	pool.ParallelFor(kManyTasks, [&calls](uint32_t, uint64_t) { ++calls; });
	EXPECT_EQ(calls, kManyTasks);
}

TEST(ThreadPool, ManySmallEpochsComplete) {
	ThreadPool pool(3);
	std::atomic<uint64_t> total{0};
	for (uint64_t epoch = 0; epoch < kManyEpochs; ++epoch) {
		pool.ParallelFor(4, [&total](uint32_t, uint64_t) { ++total; });
	}
	EXPECT_EQ(total, kManyEpochs * 4);
}

TEST(ThreadPool, SlowTaskFinishesBeforeReturn) {
	ThreadPool pool(2);
	std::atomic<bool> slowDone{false};
	pool.ParallelFor(2, [&slowDone](uint32_t, uint64_t task) {
		if (task == 1) {
			std::this_thread::sleep_for(std::chrono::milliseconds(50));
			slowDone = true;
		}
	});
	EXPECT_TRUE(slowDone);
}

TEST(ThreadPool, NestedParallelForThrowsLogicError) {
	ThreadPool pool(2);
	std::atomic<bool> nestedThrew{false};
	pool.ParallelFor(1, [&pool, &nestedThrew](uint32_t, uint64_t) {
		try {
			pool.ParallelFor(1, [](uint32_t, uint64_t) {});
		} catch (const std::logic_error&) {
			nestedThrew = true;
		}
	});
	EXPECT_TRUE(nestedThrew);
}

TEST(ThreadPool, DestructorJoinsIdleWorkers) {
	const auto startedAt = std::chrono::steady_clock::now();
	{
		ThreadPool pool(8);
	}
	const std::chrono::duration<double> elapsed = std::chrono::steady_clock::now() - startedAt;
	EXPECT_LT(elapsed.count(), 5.0);
}

TEST(ThreadPool, ZeroThreadsThrows) {
	EXPECT_THROW(ThreadPool(0), std::runtime_error);
}

} // namespace
