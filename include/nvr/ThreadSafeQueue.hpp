#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <optional>
#include <queue>
#include <utility>

namespace nvr {

enum class OverflowPolicy { DropNew, DropOldest };

template <typename T>
class ThreadSafeQueue {
public:
    explicit ThreadSafeQueue(size_t capacity = 256,
                             OverflowPolicy policy = OverflowPolicy::DropNew)
        : capacity_(capacity), policy_(policy) {}

    bool tryPush(T value) {
        {
            std::lock_guard<std::mutex> lk(mu_);
            if (closed_) return false;
            if (q_.size() >= capacity_) {
                dropped_.fetch_add(1, std::memory_order_relaxed);
                if (policy_ == OverflowPolicy::DropNew) return false;
                q_.pop();
            }
            q_.push(std::move(value));
        }
        cv_.notify_one();
        return true;
    }

    std::optional<T> waitAndPop() {
        std::unique_lock<std::mutex> lk(mu_);
        cv_.wait(lk, [&] { return !q_.empty() || closed_; });
        if (q_.empty()) return std::nullopt;
        T v = std::move(q_.front());
        q_.pop();
        return v;
    }

    void close() {
        {
            std::lock_guard<std::mutex> lk(mu_);
            closed_ = true;
        }
        cv_.notify_all();
    }

    size_t size() const {
        std::lock_guard<std::mutex> lk(mu_);
        return q_.size();
    }

    uint64_t droppedCount() const noexcept {
        return dropped_.load(std::memory_order_relaxed);
    }

private:
    mutable std::mutex      mu_;
    std::condition_variable cv_;
    std::queue<T>           q_;
    size_t                  capacity_;
    OverflowPolicy          policy_{OverflowPolicy::DropNew};
    bool                    closed_{false};
    std::atomic<uint64_t>   dropped_{0};
};

}
