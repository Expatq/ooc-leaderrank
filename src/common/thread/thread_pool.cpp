#include "thread_pool.hpp"

#include <common/core/constants.hpp>

#include <cstring>
#include <format>
#include <stdexcept>
#include <utility>

namespace lr::common {

namespace {

thread_local bool tInsideParallelFor = false;

class ParallelForScope {
public:
	ParallelForScope() {
		if (tInsideParallelFor) {
			throw std::logic_error("nested ParallelFor is not allowed");
		}
		tInsideParallelFor = true;
	}

	~ParallelForScope() {
		tInsideParallelFor = false;
	}
};

} // namespace

ThreadPool::ThreadPool(uint32_t threads)
    : Threads_(threads), Epoch_(0), Stop_(false), Body_(nullptr), TaskCount_(0), NextTask_(0),
      CompletedTasks_(0), ActiveWorkers_(0) {
	if (threads == 0) {
		throw std::runtime_error("thread count must be positive");
	}
	if (threads == 1) {
		return;
	}
	pthread_attr_t attributes;
	if (::pthread_attr_init(&attributes) != 0) {
		throw std::runtime_error("pthread_attr_init failed");
	}
	if (::pthread_attr_setstacksize(&attributes, kWorkerStackBytes) != 0) {
		::pthread_attr_destroy(&attributes);
		throw std::runtime_error("pthread_attr_setstacksize failed");
	}
	Contexts_.reserve(threads - 1);
	Workers_.reserve(threads - 1);
	for (uint32_t worker = 1; worker < threads; ++worker) {
		Contexts_.push_back(WorkerContext{this, worker});
	}
	for (uint32_t index = 0; index < threads - 1; ++index) {
		pthread_t handle;
		const int created = ::pthread_create(&handle, &attributes, WorkerEntry, &Contexts_[index]);
		if (created != 0) {
			::pthread_attr_destroy(&attributes);
			{
				std::lock_guard lock(Mutex_);
				Stop_ = true;
			}
			WorkAvailable_.notify_all();
			for (pthread_t started : Workers_) {
				::pthread_join(started, nullptr);
			}
			throw std::runtime_error(std::format("pthread_create failed: {}", std::strerror(created)));
		}
		Workers_.push_back(handle);
	}
	::pthread_attr_destroy(&attributes);
}

ThreadPool::~ThreadPool() {
	{
		std::lock_guard lock(Mutex_);
		Stop_ = true;
	}
	WorkAvailable_.notify_all();
	for (pthread_t worker : Workers_) {
		::pthread_join(worker, nullptr);
	}
}

uint32_t ThreadPool::Threads() const {
	return Threads_;
}

void ThreadPool::ParallelFor(uint64_t taskCount, const Body& body) {
	const ParallelForScope scope;
	if (taskCount == 0) {
		return;
	}
	if (Threads_ == 1) {
		for (uint64_t task = 0; task < taskCount; ++task) {
			body(0, task);
		}
		return;
	}
	{
		std::unique_lock lock(Mutex_);
		EpochDone_.wait(lock, [this] { return ActiveWorkers_ == 0; });
		Body_ = &body;
		TaskCount_ = taskCount;
		NextTask_.store(0);
		CompletedTasks_ = 0;
		FirstError_ = nullptr;
		++Epoch_;
	}
	WorkAvailable_.notify_all();
	RunTasks(0);
	std::exception_ptr error;
	{
		std::unique_lock lock(Mutex_);
		EpochDone_.wait(lock, [this] { return CompletedTasks_ == TaskCount_ && ActiveWorkers_ == 0; });
		Body_ = nullptr;
		error = std::exchange(FirstError_, nullptr);
	}
	if (error) {
		std::rethrow_exception(error);
	}
}

void* ThreadPool::WorkerEntry(void* context) {
	WorkerContext* workerContext = static_cast<WorkerContext*>(context);
	workerContext->pool->WorkerLoop(workerContext->worker);
	return nullptr;
}

void ThreadPool::WorkerLoop(uint32_t worker) {
	uint64_t lastEpoch = 0;
	while (true) {
		{
			std::unique_lock lock(Mutex_);
			WorkAvailable_.wait(lock, [this, lastEpoch] { return Stop_ || Epoch_ != lastEpoch; });
			if (Stop_) {
				return;
			}
			lastEpoch = Epoch_;
			++ActiveWorkers_;
		}
		RunTasks(worker);
		{
			std::lock_guard lock(Mutex_);
			--ActiveWorkers_;
		}
		EpochDone_.notify_all();
	}
}

void ThreadPool::RunTasks(uint32_t worker) {
	while (true) {
		const uint64_t task = NextTask_.fetch_add(1);
		if (task >= TaskCount_) {
			return;
		}
		try {
			(*Body_)(worker, task);
		} catch (...) {
			std::lock_guard lock(Mutex_);
			if (!FirstError_) {
				FirstError_ = std::current_exception();
			}
		}
		FinishTask();
	}
}

void ThreadPool::FinishTask() {
	bool epochComplete = false;
	{
		std::lock_guard lock(Mutex_);
		++CompletedTasks_;
		epochComplete = CompletedTasks_ == TaskCount_;
	}
	if (epochComplete) {
		EpochDone_.notify_all();
	}
}

} // namespace lr::common
