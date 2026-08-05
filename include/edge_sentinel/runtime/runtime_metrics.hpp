#pragma once

#include <atomic>
#include <cstdint>

namespace edge_sentinel::runtime {

struct RuntimeMetricsSnapshot {
    std::uint64_t events_published{0};
    std::uint64_t events_processed{0};
    std::uint64_t events_dropped{0};
    std::uint64_t transitions{0};
    std::uint64_t monitor_ticks{0};
    std::uint64_t last_event_latency_ns{0};
    std::uint64_t max_event_latency_ns{0};
};

class RuntimeMetrics final {
public:
    void record_published() noexcept {
        events_published_.fetch_add(1, std::memory_order_relaxed);
    }

    void record_processed(std::uint64_t latency_ns) noexcept {
        events_processed_.fetch_add(1, std::memory_order_relaxed);
        last_event_latency_ns_.store(latency_ns, std::memory_order_relaxed);

        std::uint64_t observed = max_event_latency_ns_.load(std::memory_order_relaxed);
        while (observed < latency_ns &&
               !max_event_latency_ns_.compare_exchange_weak(
                   observed,
                   latency_ns,
                   std::memory_order_relaxed)) {
        }
    }

    void record_dropped() noexcept {
        events_dropped_.fetch_add(1, std::memory_order_relaxed);
    }

    void record_transition() noexcept {
        transitions_.fetch_add(1, std::memory_order_relaxed);
    }

    void record_monitor_tick() noexcept {
        monitor_ticks_.fetch_add(1, std::memory_order_relaxed);
    }

    [[nodiscard]] RuntimeMetricsSnapshot snapshot() const noexcept {
        return RuntimeMetricsSnapshot{
            events_published_.load(std::memory_order_relaxed),
            events_processed_.load(std::memory_order_relaxed),
            events_dropped_.load(std::memory_order_relaxed),
            transitions_.load(std::memory_order_relaxed),
            monitor_ticks_.load(std::memory_order_relaxed),
            last_event_latency_ns_.load(std::memory_order_relaxed),
            max_event_latency_ns_.load(std::memory_order_relaxed)};
    }

private:
    std::atomic<std::uint64_t> events_published_{0};
    std::atomic<std::uint64_t> events_processed_{0};
    std::atomic<std::uint64_t> events_dropped_{0};
    std::atomic<std::uint64_t> transitions_{0};
    std::atomic<std::uint64_t> monitor_ticks_{0};
    std::atomic<std::uint64_t> last_event_latency_ns_{0};
    std::atomic<std::uint64_t> max_event_latency_ns_{0};
};

}  // namespace edge_sentinel::runtime
