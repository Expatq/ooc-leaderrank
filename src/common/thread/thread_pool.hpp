#pragma once

#include <pthread.h>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <exception>
#include <functional>
#include <mutex>
#include <vector>

namespace lr::common {

class ThreadPool {
public:
	using Body = std::function<void(uint32_t worker, uint64_t task)>;

	explicit ThreadPool(uint32_t threads);
	~ThreadPool();

	ThreadPool(const ThreadPool&) = delete;
	ThreadPool& operator=(const ThreadPool&) = delete;

	uint32_t Threads() const;
	void ParallelFor(uint64_t taskCount, const Body& body);

private:
	struct WorkerContext {
		ThreadPool* pool;
		uint32_t worker;
	};

	static void* WorkerEntry(void* context);

	void WorkerLoop(uint32_t worker);
	void RunTasks(uint32_t worker);
	void FinishTask();

	uint32_t Threads_;
	std::mutex Mutex_;
	std::condition_variable WorkAvailable_;
	std::condition_variable EpochDone_;
	uint64_t Epoch_;
	bool Stop_;
	const Body* Body_;
	uint64_t TaskCount_;
	std::atomic<uint64_t> NextTask_;
	uint64_t CompletedTasks_;
	uint32_t ActiveWorkers_;
	std::exception_ptr FirstError_;
	std::vector<WorkerContext> Contexts_;
	std::vector<pthread_t> Workers_;
};

} // namespace lr::common
