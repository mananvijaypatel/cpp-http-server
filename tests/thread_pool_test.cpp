#include <gtest/gtest.h>
#include <stdexcept>  // std::runtime_error

#include <atomic>
#include <chrono>
#include <mutex>
#include <set>
#include <thread>

#include "util/thread_pool.hpp"

TEST(ThreadPool, RunsAllSubmittedTasks) {
    std::atomic<int> counter{0};

    {
        util::ThreadPool pool(4);
        for (int i = 0; i < 1000; ++i) {
            pool.submit([&counter] { ++counter; });
        }
    }  // destructor drains the queue and joins all workers

    EXPECT_EQ(counter.load(), 1000);
}

TEST(ThreadPool, UsesMultipleThreads) {
    std::mutex m;
    std::set<std::thread::id> ids;

    {
        util::ThreadPool pool(4);
        for (int i = 0; i < 50; ++i) {
            pool.submit([&m, &ids] {
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                std::lock_guard<std::mutex> lock(m);
                ids.insert(std::this_thread::get_id());
            });
        }
    }

    EXPECT_GT(ids.size(), 1u);  // more than one worker actually ran tasks
}

TEST(ThreadPool, TasksThatThrowDoNotKillWorkers) {
    std::atomic<int> completed{0};

    {
        util::ThreadPool pool(2);
        for (int i = 0; i < 10; ++i) {
            pool.submit([&completed, i] {
                if (i % 2 == 0) {
                    throw std::runtime_error("boom");  // half the tasks fail
                }
                ++completed;
            });
        }
    }  // pool drains and joins; workers must still be alive to finish the rest

    EXPECT_EQ(completed.load(), 5);
}

TEST(ThreadPool, SubmitSucceedsWhileRunning) {
    util::ThreadPool pool(2);
    EXPECT_TRUE(pool.submit([] {}));
}