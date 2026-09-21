#pragma once

#include <condition_variable>
#include <cstddef>
#include <functional>
#include <mutex>
#include <queue>
#include <vector>
#include <thread>

namespace util {

    // A fixed set of worker threads pulling tasks from a shared queue.
    class ThreadPool {
        public:
            // Starts 'threads' workers immediately.
            explicit ThreadPool(std::size_t threads);

            // Signals workers to stop and waits for them to finish (RAII).
            ~ThreadPool();

            ThreadPool(const ThreadPool&) = delete;                // shared state: not copyable
            ThreadPool& operator = (const ThreadPool&) = delete;  
            
            // Adds a task to the queue. Returns false if the pool is shutting down.
            bool submit(std::function<void()> task);

            // Number of tasks waiting (not yet started). For monitoring.
            std::size_t pending() const;

        private:
            void worker_loop();     // what each worker thread runs

            mutable std::mutex mutex_;       // protects tasks_ and stopping_
            std::condition_variable cv_;    // workers sleep on this
            std::queue<std::function<void()>> tasks_;
            std::vector<std::thread> workers_;
            bool stopping_ = false;         // set by the destructor
    };

}   // namespace set