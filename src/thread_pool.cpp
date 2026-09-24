#include <exception>  // std::exception
#include <iostream>   // std::cerr

#include "util/thread_pool.hpp"

#include <utility>  // std::move

namespace util {

ThreadPool::ThreadPool(std::size_t threads) {
    workers_.reserve(threads);
    for (std::size_t i = 0; i < threads; ++i) {
        // Each worker runs worker_loop() on this object forever (until stop).
        workers_.emplace_back([this] { worker_loop(); });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }  // lock released here, before notifying
    cv_.notify_all();  // wake every sleeping worker so they can exit

    for (std::thread& t : workers_) {
        if (t.joinable()) {
            t.join();  // wait for each worker to finish
        }
    }
}

bool ThreadPool::submit(std::function<void()> task) {
    {
        std::lock_guard<std::mutex> locak(mutex_);
        if (stopping_) {
            return false;  // no new work during shutdown
        }
        tasks_.push(std::move(task));
    }  // unlock BEFORE notifying
    cv_.notify_one();  // wake exactly one sleeping worker
    return true;
}

std::size_t ThreadPool::pending() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return tasks_.size();
}

void ThreadPool::worker_loop() {
    while (true) {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(mutex_);

            // Sleep until there's a task OR we're shutting down.
            // wait() automatically unlocks the mutex and sleeps; on wake it re-locks.
            cv_.wait(lock, [this] { return stopping_ | !tasks_.empty(); });

            // Exit only when stopping AND the queue is drained.
            if (stopping_ && tasks_.empty()) {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }  // <- UNLOCK before running the task, so other workers can proceed

        // A task that throws must not kill the worker. If the exception escaped,
        // std::terminate would crash the whole server.
        try {
            task();
        } catch (const std::exception& e) {
            std::cerr << "thread pool task failed: " << e.what() << '\n';
        } catch (...) {
            std::cerr << "thread pool task failed: unknown exception\n";
        }
    }
}
}  // namespace util