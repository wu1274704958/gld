#pragma once

// Background I/O thread pool for async asset loading. Workers run pure CPU work
// (file I/O via IFileSystem + decode); they never touch GL or the Assets cache.

#include <thread>
#include <mutex>
#include <condition_variable>
#include <queue>
#include <functional>
#include <vector>
#include <atomic>

namespace gld::ecs {

    class IoPool {
    public:
        explicit IoPool(std::size_t workers = 2) { start(workers); }
        ~IoPool() { stop(); }

        IoPool(const IoPool&) = delete;
        IoPool& operator=(const IoPool&) = delete;

        void enqueue(std::function<void()> job) {
            {
                std::lock_guard<std::mutex> lk(m_);
                if (stopping_) return;
                jobs_.push(std::move(job));
            }
            cv_.notify_one();
        }

        void stop() {
            {
                std::lock_guard<std::mutex> lk(m_);
                if (stopping_) return;
                stopping_ = true;
            }
            cv_.notify_all();
            for (auto& t : workers_) if (t.joinable()) t.join();
        }

        std::size_t queued() const {
            std::lock_guard<std::mutex> lk(m_);
            return jobs_.size();
        }
        std::size_t active() const { return active_.load(std::memory_order_relaxed); }
        std::uint64_t completed() const { return completed_.load(std::memory_order_relaxed); }

    private:
        void start(std::size_t n) {
            for (std::size_t i = 0; i < n; ++i) {
                workers_.emplace_back([this] {
                    for (;;) {
                        std::function<void()> job;
                        {
                            std::unique_lock<std::mutex> lk(m_);
                            cv_.wait(lk, [this] { return stopping_ || !jobs_.empty(); });
                            if (stopping_ && jobs_.empty()) return;
                            job = std::move(jobs_.front());
                            jobs_.pop();
                        }
                        active_.fetch_add(1, std::memory_order_relaxed);
                        job();
                        active_.fetch_sub(1, std::memory_order_relaxed);
                        completed_.fetch_add(1, std::memory_order_relaxed);
                    }
                });
            }
        }

        std::vector<std::thread> workers_;
        std::queue<std::function<void()>> jobs_;
        mutable std::mutex m_;
        std::condition_variable cv_;
        bool stopping_ = false;
        std::atomic<std::size_t> active_{0};
        std::atomic<std::uint64_t> completed_{0};
    };

    // Main-thread completion queue: finalize closures pushed by workers, drained
    // on the main thread (GL context) by asset_update_system.
    struct CompletionQueue {
        void push(std::function<void()> fn) {
            std::lock_guard<std::mutex> lk(m_);
            items_.push_back(std::move(fn));
        }
        std::vector<std::function<void()>> drain() {
            std::lock_guard<std::mutex> lk(m_);
            return std::move(items_);
        }
        void clear() {
            std::lock_guard<std::mutex> lk(m_);
            items_.clear();
        }
        std::size_t size() const {
            std::lock_guard<std::mutex> lk(m_);
            return items_.size();
        }
    private:
        std::vector<std::function<void()>> items_;
        mutable std::mutex m_;
    };
}
