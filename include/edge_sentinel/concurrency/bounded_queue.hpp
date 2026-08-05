#pragma once

#include <array>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <optional>
#include <utility>

namespace edge_sentinel::concurrency {

enum class QueuePushResult {
    pushed,
    full,
    timeout,
    closed,
};

struct QueueStats {
    std::size_t capacity{0};
    std::size_t size{0};
    std::size_t high_watermark{0};
    std::size_t pushed{0};
    std::size_t popped{0};
    std::size_t rejected_full{0};
    std::size_t push_timeouts{0};
};

template <typename T, std::size_t Capacity>
class BoundedQueue final {
    static_assert(Capacity > 0);

public:
    BoundedQueue() = default;
    ~BoundedQueue() = default;

    BoundedQueue(const BoundedQueue&) = delete;
    BoundedQueue& operator=(const BoundedQueue&) = delete;
    BoundedQueue(BoundedQueue&&) = delete;
    BoundedQueue& operator=(BoundedQueue&&) = delete;

    [[nodiscard]] QueuePushResult try_push(T value) {
        std::unique_lock lock(mutex_);
        if (closed_) {
            return QueuePushResult::closed;
        }
        if (size_ == Capacity) {
            ++stats_.rejected_full;
            return QueuePushResult::full;
        }

        push_locked(std::move(value));
        lock.unlock();
        not_empty_.notify_one();
        return QueuePushResult::pushed;
    }

    template <typename Rep, typename Period>
    [[nodiscard]] QueuePushResult wait_push(
        T value,
        const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock lock(mutex_);
        const bool ready = not_full_.wait_for(lock, timeout, [this] {
            return closed_ || size_ < Capacity;
        });

        if (!ready) {
            ++stats_.push_timeouts;
            return QueuePushResult::timeout;
        }
        if (closed_) {
            return QueuePushResult::closed;
        }

        push_locked(std::move(value));
        lock.unlock();
        not_empty_.notify_one();
        return QueuePushResult::pushed;
    }

    [[nodiscard]] std::optional<T> try_pop() {
        std::unique_lock lock(mutex_);
        if (size_ == 0) {
            return std::nullopt;
        }

        std::optional<T> value = pop_locked();
        lock.unlock();
        not_full_.notify_one();
        return value;
    }

    [[nodiscard]] std::optional<T> wait_pop() {
        std::unique_lock lock(mutex_);
        not_empty_.wait(lock, [this] {
            return closed_ || size_ > 0;
        });

        if (size_ == 0) {
            return std::nullopt;
        }

        std::optional<T> value = pop_locked();
        lock.unlock();
        not_full_.notify_one();
        return value;
    }

    template <typename Rep, typename Period>
    [[nodiscard]] std::optional<T> wait_pop_for(
        const std::chrono::duration<Rep, Period>& timeout) {
        std::unique_lock lock(mutex_);
        const bool ready = not_empty_.wait_for(lock, timeout, [this] {
            return closed_ || size_ > 0;
        });

        if (!ready || size_ == 0) {
            return std::nullopt;
        }

        std::optional<T> value = pop_locked();
        lock.unlock();
        not_full_.notify_one();
        return value;
    }

    void close() {
        {
            std::lock_guard lock(mutex_);
            closed_ = true;
        }
        not_empty_.notify_all();
        not_full_.notify_all();
    }

    [[nodiscard]] bool closed() const {
        std::lock_guard lock(mutex_);
        return closed_;
    }

    [[nodiscard]] QueueStats stats() const {
        std::lock_guard lock(mutex_);
        QueueStats snapshot = stats_;
        snapshot.capacity = Capacity;
        snapshot.size = size_;
        return snapshot;
    }

private:
    void push_locked(T value) {
        slots_[tail_].emplace(std::move(value));
        tail_ = (tail_ + 1) % Capacity;
        ++size_;
        ++stats_.pushed;
        if (size_ > stats_.high_watermark) {
            stats_.high_watermark = size_;
        }
    }

    [[nodiscard]] std::optional<T> pop_locked() {
        std::optional<T> value{std::move(*slots_[head_])};
        slots_[head_].reset();
        head_ = (head_ + 1) % Capacity;
        --size_;
        ++stats_.popped;
        return value;
    }

    mutable std::mutex mutex_;
    std::condition_variable not_empty_;
    std::condition_variable not_full_;
    std::array<std::optional<T>, Capacity> slots_{};
    std::size_t head_{0};
    std::size_t tail_{0};
    std::size_t size_{0};
    bool closed_{false};
    QueueStats stats_{};
};

}  // namespace edge_sentinel::concurrency
