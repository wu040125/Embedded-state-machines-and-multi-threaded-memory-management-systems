#pragma once

#include <edge_sentinel/concurrency/bounded_queue.hpp>
#include <edge_sentinel/domain/event.hpp>
#include <edge_sentinel/fsm/motor_state_machine.hpp>
#include <edge_sentinel/hal/sensor_source.hpp>
#include <edge_sentinel/memory/fixed_object_pool.hpp>
#include <edge_sentinel/runtime/runtime_metrics.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <stop_token>
#include <thread>
#include <utility>

namespace edge_sentinel::runtime {

enum class PublishResult {
    published,
    not_running,
    pool_exhausted,
    queue_full,
    queue_timeout,
    queue_closed,
};

struct RuntimeConfig {
    std::chrono::milliseconds sample_period{100};
    std::chrono::milliseconds monitor_period{1'000};
    std::chrono::milliseconds event_push_timeout{10};
    fsm::ProtectionConfig protection{};
};

struct RuntimeSnapshot {
    fsm::MachineSnapshot machine{};
    RuntimeMetricsSnapshot metrics{};
    memory::PoolStats pool{};
    concurrency::QueueStats queue{};
};

class EdgeRuntime final {
public:
    static constexpr std::size_t kEventPoolCapacity = 256;
    static constexpr std::size_t kEventQueueCapacity = 128;

    EdgeRuntime(std::shared_ptr<hal::ISensorSource> sensor, RuntimeConfig config = {})
        : sensor_(std::move(sensor)), config_(config), machine_(config_.protection) {}

    ~EdgeRuntime() {
        stop();
    }

    EdgeRuntime(const EdgeRuntime&) = delete;
    EdgeRuntime& operator=(const EdgeRuntime&) = delete;
    EdgeRuntime(EdgeRuntime&&) = delete;
    EdgeRuntime& operator=(EdgeRuntime&&) = delete;

    [[nodiscard]] bool start() {
        std::lock_guard lock(lifecycle_mutex_);
        if (started_) {
            return false;
        }

        started_ = true;
        accepting_events_.store(true, std::memory_order_release);
        running_.store(true, std::memory_order_release);

        control_thread_ = std::jthread([this](std::stop_token) {
            control_loop();
        });
        static_cast<void>(publish(domain::Event::startup()));
        static_cast<void>(publish(domain::Event::self_test_passed()));
        sensor_thread_ = std::jthread([this](std::stop_token token) {
            sensor_loop(token);
        });
        monitor_thread_ = std::jthread([this](std::stop_token token) {
            monitor_loop(token);
        });
        return true;
    }

    void stop() noexcept {
        {
            std::lock_guard lock(lifecycle_mutex_);
            if (!started_ || stopped_) {
                return;
            }
            stopped_ = true;
        }

        static_cast<void>(publish(domain::Event::shutdown_requested()));
        accepting_events_.store(false, std::memory_order_release);

        {
            std::unique_lock lock(machine_mutex_);
            state_changed_.wait_for(lock, std::chrono::seconds{1}, [this] {
                return machine_.state() == domain::MotorState::safe_shutdown;
            });
        }

        sensor_thread_.request_stop();
        monitor_thread_.request_stop();
        stop_waiters_.notify_all();
        join_if_needed(sensor_thread_);
        join_if_needed(monitor_thread_);

        queue_.close();
        control_thread_.request_stop();
        join_if_needed(control_thread_);
        running_.store(false, std::memory_order_release);
    }

    [[nodiscard]] PublishResult publish(const domain::Event& event) {
        if (!accepting_events_.load(std::memory_order_acquire)) {
            return PublishResult::not_running;
        }

        auto pooled_event = pool_.try_create(event, std::chrono::steady_clock::now());
        if (!pooled_event) {
            metrics_.record_dropped();
            return PublishResult::pool_exhausted;
        }

        const concurrency::QueuePushResult result =
            queue_.wait_push(std::move(pooled_event), config_.event_push_timeout);
        switch (result) {
        case concurrency::QueuePushResult::pushed:
            metrics_.record_published();
            return PublishResult::published;
        case concurrency::QueuePushResult::full:
            metrics_.record_dropped();
            return PublishResult::queue_full;
        case concurrency::QueuePushResult::timeout:
            metrics_.record_dropped();
            return PublishResult::queue_timeout;
        case concurrency::QueuePushResult::closed:
            metrics_.record_dropped();
            return PublishResult::queue_closed;
        }
        metrics_.record_dropped();
        return PublishResult::queue_closed;
    }

    [[nodiscard]] bool running() const noexcept {
        return running_.load(std::memory_order_acquire);
    }

    [[nodiscard]] RuntimeSnapshot snapshot() const {
        std::lock_guard lock(machine_mutex_);
        return RuntimeSnapshot{
            machine_.snapshot(), metrics_.snapshot(), pool_.stats(), queue_.stats()};
    }

private:
    struct PooledEvent {
        PooledEvent(domain::Event event_value, std::chrono::steady_clock::time_point created_value)
            : event(event_value), created_at(created_value) {}

        domain::Event event{};
        std::chrono::steady_clock::time_point created_at{};
    };

    using EventPool = memory::FixedObjectPool<PooledEvent, kEventPoolCapacity>;
    using EventQueue = concurrency::BoundedQueue<typename EventPool::Handle, kEventQueueCapacity>;

    void sensor_loop(std::stop_token token) {
        while (!token.stop_requested()) {
            if (sensor_) {
                static_cast<void>(publish(domain::Event::sensor_sample(sensor_->read())));
            }
            if (!wait_for_period(token, config_.sample_period)) {
                break;
            }
        }
    }

    void control_loop() {
        while (auto queued = queue_.wait_pop()) {
            auto event_handle = std::move(*queued);
            const auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - event_handle->created_at);

            domain::StateTransition transition;
            {
                std::lock_guard lock(machine_mutex_);
                transition = machine_.handle(event_handle->event);
            }

            metrics_.record_processed(static_cast<std::uint64_t>(latency.count()));
            if (transition.changed) {
                metrics_.record_transition();
                state_changed_.notify_all();
            }
        }
    }

    void monitor_loop(std::stop_token token) {
        while (wait_for_period(token, config_.monitor_period)) {
            static_cast<void>(pool_.stats());
            static_cast<void>(queue_.stats());
            metrics_.record_monitor_tick();
        }
    }

    template <typename Duration>
    [[nodiscard]] bool wait_for_period(std::stop_token token, Duration duration) {
        std::unique_lock lock(stop_wait_mutex_);
        stop_waiters_.wait_for(lock, token, duration, [] {
            return false;
        });
        return !token.stop_requested();
    }

    static void join_if_needed(std::jthread& thread) noexcept {
        if (thread.joinable()) {
            thread.join();
        }
    }

    std::shared_ptr<hal::ISensorSource> sensor_;
    RuntimeConfig config_;
    EventPool pool_{};
    EventQueue queue_{};
    mutable std::mutex machine_mutex_;
    fsm::MotorStateMachine machine_;
    RuntimeMetrics metrics_{};
    std::condition_variable state_changed_;
    std::mutex lifecycle_mutex_;
    std::mutex stop_wait_mutex_;
    std::condition_variable_any stop_waiters_;
    bool started_{false};
    bool stopped_{false};
    std::atomic<bool> accepting_events_{false};
    std::atomic<bool> running_{false};
    std::jthread sensor_thread_{};
    std::jthread control_thread_{};
    std::jthread monitor_thread_{};
};

}  // namespace edge_sentinel::runtime
