#include <edge_sentinel/concurrency/bounded_queue.hpp>
#include <edge_sentinel/memory/fixed_object_pool.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <numeric>
#include <thread>

namespace {

using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;

constexpr std::size_t kEventCount = 50'000;

struct BenchmarkEvent {
    std::uint64_t sequence{0};
    Clock::time_point created_at{};
};

std::uint64_t percentile(
    const std::array<std::uint64_t, kEventCount>& sorted,
    double fraction) {
    const auto index = static_cast<std::size_t>(
        static_cast<double>(sorted.size() - 1) * fraction);
    return sorted[index];
}

}  // namespace

int main() {
    using Pool = edge_sentinel::memory::FixedObjectPool<BenchmarkEvent, 256>;
    using Queue = edge_sentinel::concurrency::BoundedQueue<typename Pool::Handle, 128>;

    Pool pool;
    Queue queue;
    std::array<std::uint64_t, kEventCount> latencies_ns{};

    std::jthread consumer([&queue, &latencies_ns] {
        for (std::size_t index = 0; index < kEventCount; ++index) {
            auto queued = queue.wait_pop();
            if (!queued) {
                break;
            }
            const auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(
                Clock::now() - (*queued)->created_at);
            latencies_ns[index] = static_cast<std::uint64_t>(latency.count());
        }
    });

    const auto started_at = Clock::now();
    for (std::size_t sequence = 0; sequence < kEventCount; ++sequence) {
        bool published = false;
        while (!published) {
            auto event = pool.try_create(
                static_cast<std::uint64_t>(sequence),
                Clock::now());
            if (!event) {
                std::this_thread::yield();
                continue;
            }
            published = queue.wait_push(std::move(event), 1s) ==
                        edge_sentinel::concurrency::QueuePushResult::pushed;
        }
    }
    queue.close();
    consumer.join();
    const auto elapsed = Clock::now() - started_at;

    std::sort(latencies_ns.begin(), latencies_ns.end());
    const std::uint64_t total_latency =
        std::accumulate(latencies_ns.begin(), latencies_ns.end(), std::uint64_t{0});
    const double seconds = std::chrono::duration<double>(elapsed).count();
    const double events_per_second = static_cast<double>(kEventCount) / seconds;
    const auto pool_stats = pool.stats();
    const auto queue_stats = queue.stats();

    std::cout << "events=" << kEventCount << '\n'
              << "elapsed_seconds=" << seconds << '\n'
              << "throughput_events_per_second=" << events_per_second << '\n'
              << "latency_average_ns=" << total_latency / kEventCount << '\n'
              << "latency_p50_ns=" << percentile(latencies_ns, 0.50) << '\n'
              << "latency_p95_ns=" << percentile(latencies_ns, 0.95) << '\n'
              << "latency_p99_ns=" << percentile(latencies_ns, 0.99) << '\n'
              << "latency_max_ns=" << latencies_ns.back() << '\n'
              << "pool_high_watermark=" << pool_stats.high_watermark << '/'
              << pool_stats.capacity << '\n'
              << "queue_high_watermark=" << queue_stats.high_watermark << '/'
              << queue_stats.capacity << '\n'
              << "allocation_failures=" << pool_stats.allocation_failures << '\n';
    return pool.outstanding() == 0 ? 0 : 1;
}
