#pragma once
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>
#include <exception>
// Bounded persistent worker pool. Generation tags reject obsolete job results.
template <class Result> class TerrainWorker {
  struct Job {
    uint64_t generation;
    std::function<Result()> run;
  };
  std::mutex mutex;
  std::condition_variable wake;
  std::deque<Job> jobs;
  std::deque<Result> ready;
  std::vector<std::thread> threads;
  uint64_t generation = 0;
  bool stopping = false;
  size_t busy = 0;
  std::exception_ptr failure;

public:
  explicit TerrainWorker(int count = 2) {
    for (int i = 0; i < std::clamp(count, 1, 12); ++i)
      threads.emplace_back([this] {
        std::unique_lock lock(mutex);
        while (true) {
          wake.wait(lock, [&] { return stopping || !jobs.empty(); });
          if (stopping)
            return;
          auto job = std::move(jobs.front());
          jobs.pop_front();
          ++busy;
          lock.unlock();
          Result result{};
          std::exception_ptr error;
          try {
            result = job.run();
          } catch (...) {
            error = std::current_exception();
          }
          lock.lock();
          --busy;
          if (job.generation == generation) {
            if (error)
              failure = error;
            else
              ready.push_back(std::move(result));
          }
          wake.notify_all();
        }
      });
  }
  ~TerrainWorker() {
    {
      std::lock_guard lock(mutex);
      stopping = true;
      jobs.clear();
    }
    wake.notify_all();
    for (auto &thread : threads)
      thread.join();
  }
  bool submit(std::function<Result()> task) {
    std::lock_guard lock(mutex);
    if (jobs.size() + ready.size() + size_t(busy) >= 12)
      return false;
    jobs.push_back({generation, std::move(task)});
    wake.notify_one();
    return true;
  }
  bool take(Result &result) {
    std::lock_guard lock(mutex);
    if (failure)
      std::rethrow_exception(failure);
    if (ready.empty())
      return false;
    result = std::move(ready.front());
    ready.pop_front();
    return true;
  }
  void reset(bool drain = true) {
    std::unique_lock lock(mutex);
    ++generation;
    jobs.clear();
    ready.clear();
    if (drain)
      wake.wait(lock, [&] { return !busy; });
    ready.clear();
    failure = nullptr;
  }
};
